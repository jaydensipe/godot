/**************************************************************************/
/*  level_map.cpp                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "level_map.h"

#include "level_constants.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "scene/3d/occluder_instance_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "scene/resources/mesh.h"

void LevelMap::_bind_methods() {
	ClassDB::bind_method(D_METHOD("refresh"), &LevelMap::refresh);

	ClassDB::bind_method(D_METHOD("set_default_material", "material"), &LevelMap::set_default_material);
	ClassDB::bind_method(D_METHOD("get_default_material"), &LevelMap::get_default_material);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "default_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_default_material", "get_default_material");
}

Vector<LevelBrush *> LevelMap::get_brushes() const {
	Vector<LevelBrush *> out;
	for (int i = 0; i < get_child_count(); i++) {
		LevelBrush *b = Object::cast_to<LevelBrush>(get_child(i));
		if (b) {
			out.push_back(b);
		}
	}
	return out;
}

int LevelMap::get_brush_count() const {
	int count = 0;
	for (int i = 0; i < get_child_count(); i++) {
		if (Object::cast_to<LevelBrush>(get_child(i))) {
			count++;
		}
	}
	return count;
}

void LevelMap::set_default_material(const Ref<Material> &p_material) {
	if (p_material.is_valid()) {
		default_material = p_material;
	}
	// Faces with no material resolve to the default: every brush's cache is
	// potentially affected.
	for (KeyValue<LevelBrush *, BrushCache> &E : brush_cache) {
		E.value.dirty = true;
	}
	preview_dirty = true;
}

Ref<Material> LevelMap::get_default_material() const {
	return default_material;
}

void LevelMap::refresh() {
	// Coalesce: mark dirty and let NOTIFICATION_PROCESS rebuild ONCE per
	// frame, no matter how many edits/events landed this frame. Gizmo drags
	// call refresh per mouse-motion - a synchronous rebuild here bakes per
	// event (dozens/sec), which is the interactive slowdown. Once per frame
	// is still fully live (60fps) and the overlay already redraws per frame.
	preview_dirty = true;
	// NB: update_configuration_warnings() is NOT here - it re-scans children
	// and only needs to run when the brush COUNT changes (child order change),
	// not on every geometry edit.
}

Ref<Material> LevelMap::_get_face_material_or_default(LevelBrush *p_brush, int p_face) const {
	Ref<Material> mat = p_brush->get_face_material(p_face);
	if (mat.is_null()) {
		mat = default_material;
	}
	return mat;
}

PackedStringArray LevelMap::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (get_brush_count() == 0) {
		warnings.push_back(RTR("This LevelMap has no brushes. Use the Level editor tab to drag out blocks."));
	}
	return warnings;
}

void LevelMap::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				if (!preview_mesh_instance) {
					preview_mesh_instance = memnew(MeshInstance3D);
					preview_mesh_instance->set_name("_LevelPreview");
					// Don't let the preview cast shadows on itself or the scene.
					preview_mesh_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
					add_child(preview_mesh_instance, false, INTERNAL_MODE_FRONT);
					preview_mesh_instance->set_owner(nullptr);
				}
				preview_dirty = true;
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (Engine::get_singleton()->is_editor_hint() && preview_mesh_instance) {
				preview_mesh_instance->queue_free();
				preview_mesh_instance = nullptr;
			}
			// Clear stale bookkeeping so a fresh scene doesn't reuse old
			// surface indices (GOTCHA: stale arrays survive scene change).
			brush_cache.clear();
			preview_surface_brush.clear();
			preview_surface_idx.clear();
		} break;
		case NOTIFICATION_PROCESS: {
			if (preview_dirty) {
				preview_dirty = false;
				_update_preview();
			}
		} break;
		case NOTIFICATION_CHILD_ORDER_CHANGED: {
			if (Engine::get_singleton()->is_editor_hint()) {
				preview_dirty = true;
				// Brush count may have changed (the only thing the warnings check).
				update_configuration_warnings();
			}
		} break;
	}
}

void LevelMap::notify_brush_changed(LevelBrush *p_brush) {
	BrushCache *cache = brush_cache.getptr(p_brush);
	if (cache) {
		cache->dirty = true;
	} else {
		BrushCache fresh;
		brush_cache.insert(p_brush, fresh); // dirty = true by default.
	}
	refresh(); // Coalesced: one preview rebuild per frame.
}

// Tessellate one brush into map-local per-material surface arrays.
Transform3D LevelMap::_brush_to_map_transform(LevelBrush *p_brush) const {
	const Transform3D map_inv = is_inside_tree() ? get_global_transform().affine_inverse() : get_transform().affine_inverse();
	return map_inv * (p_brush->is_inside_tree() ? p_brush->get_global_transform() : p_brush->get_transform());
}

void LevelMap::_append_face_geometry(LevelBrush *p_brush, int p_face, const Transform3D &p_to_map, const Basis &p_normal_basis, PackedVector3Array &r_verts, PackedVector3Array &r_normals, PackedVector2Array &r_uvs) {
	Vector<Vector3> v, n;
	Vector<Vector2> uv;
	p_brush->get_bake_surface_data(p_face, v, n, uv);
	for (int j = 0; j < v.size(); j++) {
		r_verts.push_back(p_to_map.xform(v[j]));
		r_normals.push_back(p_normal_basis.xform(n[j]).normalized());
		r_uvs.push_back(uv[j]);
	}
}

void LevelMap::_rebuild_brush_cache(LevelBrush *p_brush, BrushCache &r_cache) const {
	r_cache.surfaces.clear();
	const Transform3D brush_to_map = _brush_to_map_transform(p_brush);
	const Basis normal_basis = brush_to_map.basis.inverse().transposed();

	// Group faces by material (insertion order for stable surfaces).
	HashMap<Ref<Material>, LocalVector<int>> mat_faces;
	LocalVector<Ref<Material>> materials;
	for (int f = 0; f < p_brush->get_face_count(); f++) {
		Ref<Material> mat = _get_face_material_or_default(p_brush, f);
		LocalVector<int> *list = mat_faces.getptr(mat);
		if (!list) {
			materials.push_back(mat);
			mat_faces.insert(mat, LocalVector<int>());
			list = mat_faces.getptr(mat);
		}
		list->push_back(f);
	}

	for (const Ref<Material> &mat : materials) {
		BrushCache::SurfaceData sd;
		sd.material = mat;
		for (int f : mat_faces[mat]) {
			_append_face_geometry(p_brush, f, brush_to_map, normal_basis, sd.verts, sd.normals, sd.uvs);
		}
		if (!sd.verts.is_empty()) {
			r_cache.surfaces.push_back(sd);
		}
	}
	r_cache.dirty = false;
}

void LevelMap::_update_preview() {
	if (!Engine::get_singleton()->is_editor_hint() || !preview_mesh_instance) {
		return;
	}

	// Prune caches for brushes that left the map.
	Vector<LevelBrush *> brushes = get_brushes();
	HashSet<LevelBrush *> live;
	for (LevelBrush *b : brushes) {
		live.insert(b);
	}
	List<LevelBrush *> dead;
	for (KeyValue<LevelBrush *, BrushCache> &E : brush_cache) {
		if (!live.has(E.key)) {
			dead.push_back(E.key);
		}
	}
	for (LevelBrush *b : dead) {
		brush_cache.erase(b);
	}

	// Rebuild dirty brush caches (only dirty brushes re-tessellate). Track
	// WHICH brushes were rebuilt: _rebuild_brush_cache clears their dirty
	// flag, but the surface-replace path below still needs to know whose GPU
	// surfaces to re-upload.
	bool any_dirty = false;
	HashSet<LevelBrush *> rebuilt;
	for (LevelBrush *b : brushes) {
		BrushCache &cache = brush_cache[b]; // Inserts (dirty) if new.
		if (cache.dirty) {
			_rebuild_brush_cache(b, cache);
			rebuilt.insert(b);
			any_dirty = true;
		}
	}

	Ref<ArrayMesh> mesh = preview_mesh_instance->get_mesh();
	const bool structure_changed = any_dirty || dead.size() > 0 || mesh.is_null() ||
			(int)preview_surface_brush.size() != mesh->get_surface_count();

	if (!structure_changed && mesh.is_valid()) {
		return; // Nothing to do.
	}

	// Reassemble the preview mesh. One surface per brush per material; only
	// surfaces of brushes whose cache changed are re-uploaded (remove + re-add
	// replaces just that surface's GPU buffers).
	if (mesh.is_null()) {
		mesh.instantiate();
		preview_mesh_instance->set_mesh(mesh);
	}

	// Map existing surfaces to (brush, cache idx) so unchanged ones survive.
	// Simplest correct approach: rebuild the surface list, but reuse cached
	// arrays - the GPU re-upload is per-surface, and only dirty brushes'
	// surfaces differ. To avoid re-uploading CLEAN brushes' surfaces, keep
	// their existing surfaces in place and only replace dirty ones.
	//
	// Surface order is stable: brushes in child order, cache surfaces in
	// material insertion order. Rebuild the bookkeeping and diff.
	LocalVector<LevelBrush *> new_brush;
	LocalVector<int> new_idx;
	for (LevelBrush *b : brushes) {
		const BrushCache &cache = brush_cache[b];
		for (uint32_t s = 0; s < cache.surfaces.size(); s++) {
			new_brush.push_back(b);
			new_idx.push_back(s);
		}
	}

	// If the surface layout changed (brush added/removed, or a dirty brush's
	// material split changed its surface count), a full rebuild is simplest
	// and correct; per-frame drags keep the SAME layout, so they hit the
	// cheap per-surface replace path below.
	const bool layout_same = (int)new_brush.size() == (int)preview_surface_brush.size() &&
			(new_brush.is_empty() ||
					(memcmp(new_brush.ptr(), preview_surface_brush.ptr(), new_brush.size() * sizeof(LevelBrush *)) == 0 &&
							(new_idx.is_empty() || memcmp(new_idx.ptr(), preview_surface_idx.ptr(), new_idx.size() * sizeof(int)) == 0)));

	if (!layout_same) {
		mesh->clear_surfaces();
		for (uint32_t i = 0; i < new_brush.size(); i++) {
			const BrushCache::SurfaceData &sd = brush_cache[new_brush[i]].surfaces[new_idx[i]];
			Array arrays;
			arrays.resize(Mesh::ARRAY_MAX);
			arrays[Mesh::ARRAY_VERTEX] = sd.verts;
			arrays[Mesh::ARRAY_NORMAL] = sd.normals;
			arrays[Mesh::ARRAY_TEX_UV] = sd.uvs;
			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
			mesh->surface_set_material(mesh->get_surface_count() - 1, sd.material);
		}
	} else {
		// Same layout: replace only dirty brushes' surfaces. surface_remove +
		// add appends at the END, so process dirty surfaces from LAST to FIRST
		// (earlier indices stay valid), then rotate the moved entries in the
		// bookkeeping to match the final mesh order.
		LocalVector<uint32_t> replaced; // Original indices, in replace order.
		for (int i = (int)new_brush.size() - 1; i >= 0; i--) {
			if (!rebuilt.has(new_brush[i])) {
				continue; // Clean brush: keep the existing GPU surface.
			}
			const BrushCache::SurfaceData &sd = brush_cache[new_brush[i]].surfaces[new_idx[i]];
			Array arrays;
			arrays.resize(Mesh::ARRAY_MAX);
			arrays[Mesh::ARRAY_VERTEX] = sd.verts;
			arrays[Mesh::ARRAY_NORMAL] = sd.normals;
			arrays[Mesh::ARRAY_TEX_UV] = sd.uvs;
			mesh->surface_remove(i);
			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
			mesh->surface_set_material(mesh->get_surface_count() - 1, sd.material);
			replaced.push_back(i);
		}
		// Rebuild bookkeeping: entries NOT replaced keep their relative order
		// (compacted), then replaced entries append in replace order (which was
		// last-to-first, so reverse it for the append order... each remove+add
		// appends immediately, so final order = unreplaced (in order) followed
		// by replaced in the order they were re-added = reverse of `replaced`).
		LocalVector<LevelBrush *> final_brush;
		LocalVector<int> final_idx;
		for (uint32_t i = 0; i < new_brush.size(); i++) {
			bool was_replaced = false;
			for (uint32_t r : replaced) {
				if (r == i) {
					was_replaced = true;
					break;
				}
			}
			if (!was_replaced) {
				final_brush.push_back(new_brush[i]);
				final_idx.push_back(new_idx[i]);
			}
		}
		for (int r = (int)replaced.size() - 1; r >= 0; r--) {
			final_brush.push_back(new_brush[replaced[r]]);
			final_idx.push_back(new_idx[replaced[r]]);
		}
		new_brush = final_brush;
		new_idx = final_idx;
	}

	preview_surface_brush = new_brush;
	preview_surface_idx = new_idx;
}

Node3D *LevelMap::bake(bool p_geometry_only) const {
	Vector<LevelBrush *> brushes = get_brushes();
	if (brushes.is_empty()) {
		return nullptr;
	}

	// One surface per unique material. Single pass: group each brush's faces
	// by material up front (the old per-material re-scan of every face was
	// O(materials x faces) - quadratic with per-face unique materials).
	HashMap<Ref<Material>, LocalVector<int>> mat_faces; // material -> flat (brush_idx, face) pairs.
	LocalVector<Ref<Material>> materials; // Insertion order for stable surfaces.
	for (int b = 0; b < brushes.size(); b++) {
		LevelBrush *brush = brushes[b];
		for (int f = 0; f < brush->get_face_count(); f++) {
			Ref<Material> mat = _get_face_material_or_default(brush, f);
			LocalVector<int> *list = mat_faces.getptr(mat);
			if (!list) {
				materials.push_back(mat);
				mat_faces.insert(mat, LocalVector<int>());
				list = mat_faces.getptr(mat);
			}
			list->push_back(b);
			list->push_back(f);
		}
	}

	Ref<ArrayMesh> mesh;
	mesh.instantiate();

	PackedVector3Array collision_faces;

	// Per-brush transforms, computed once (not per material x brush).
	LocalVector<Transform3D> brush_to_map;
	LocalVector<Basis> brush_normal_basis;
	for (LevelBrush *brush : brushes) {
		const Transform3D t = _brush_to_map_transform(brush);
		brush_to_map.push_back(t);
		brush_normal_basis.push_back(t.basis.inverse().transposed());
	}

	for (const Ref<Material> &mat : materials) {
		PackedVector3Array verts;
		PackedVector3Array normals;
		PackedVector2Array uvs;

		const LocalVector<int> &list = mat_faces[mat];
		for (uint32_t i = 0; i < list.size(); i += 2) {
			const int b = list[i];
			const int f = list[i + 1];
			_append_face_geometry(brushes[b], f, brush_to_map[b], brush_normal_basis[b], verts, normals, uvs);
		}

		if (verts.is_empty()) {
			continue;
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = verts;
		arrays[Mesh::ARRAY_NORMAL] = normals;
		arrays[Mesh::ARRAY_TEX_UV] = uvs;

		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		mesh->surface_set_material(mesh->get_surface_count() - 1, mat);
	}

	if (mesh->get_surface_count() == 0) {
		return nullptr; // Nothing renderable.
	}

	MeshInstance3D *mi = memnew(MeshInstance3D);
	mi->set_mesh(mesh);
	mi->set_name(String(get_name()) + "_Baked");

	if (p_geometry_only) {
		return mi; // Preview: mesh only, no collision/occluder.
	}

	// Collision + occluder geometry (map-local).
	for (LevelBrush *brush : brushes) {
		const Transform3D to_map = _brush_to_map_transform(brush);
		Vector<Vector3> faces;
		brush->get_collision_faces(faces);
		for (const Vector3 &p : faces) {
			collision_faces.push_back(to_map.xform(p));
		}
	}

	if (collision_faces.is_empty()) {
		memdelete(mi);
		return nullptr; // Nothing solid (e.g. all faces deleted).
	}

	StaticBody3D *body = memnew(StaticBody3D);
	body->set_name("Collision");
	CollisionShape3D *shape_node = memnew(CollisionShape3D);
	Ref<ConcavePolygonShape3D> concave;
	concave.instantiate();
	concave->set_faces(collision_faces);
	shape_node->set_shape(concave);
	body->add_child(shape_node);
	mi->add_child(body);

	OccluderInstance3D *occluder = memnew(OccluderInstance3D);
	occluder->set_name("Occluder");
	Ref<ArrayOccluder3D> occ;
	occ.instantiate();
	occ->set_arrays(collision_faces, PackedInt32Array());
	occluder->set_occluder(occ);
	mi->add_child(occluder);

	return mi;
}

LevelMap::LevelMap() {
	default_material.instantiate();
	default_material->set_albedo(LevelEditorColors::DEFAULT_BRUSH_ALBEDO);
	// Standard back-face culling: the bake emits Vulkan-style clockwise-front
	// triangles (see get_bake_surface_data), so exteriors draw and interiors cull.
	default_material->set_cull_mode(BaseMaterial3D::CULL_BACK);

	if (Engine::get_singleton()->is_editor_hint()) {
		set_process(true);
	}
}
