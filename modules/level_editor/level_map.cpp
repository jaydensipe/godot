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
	preview_dirty = true;
}

Ref<Material> LevelMap::get_default_material() const {
	return default_material;
}

void LevelMap::refresh() {
	preview_dirty = true;
	if (Engine::get_singleton()->is_editor_hint()) {
		// Rebuild immediately rather than waiting for a process frame, so
		// interactive edits (gizmo drags) stay in sync.
		_update_preview();
	}
	update_configuration_warnings();
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
			}
		} break;
	}
}

void LevelMap::_update_preview() {
	if (!Engine::get_singleton()->is_editor_hint() || !preview_mesh_instance) {
		return;
	}

	Node3D *baked = bake();
	if (!baked) {
		preview_mesh_instance->set_mesh(Ref<Mesh>());
		return;
	}

	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(baked);
	if (mi) {
		preview_mesh_instance->set_mesh(mi->get_mesh());
		for (int i = 0; i < mi->get_surface_override_material_count(); i++) {
			preview_mesh_instance->set_surface_override_material(i, mi->get_surface_override_material(i));
		}
	}
	memdelete(baked);
}

Node3D *LevelMap::bake() const {
	Vector<LevelBrush *> brushes = get_brushes();
	if (brushes.is_empty()) {
		return nullptr;
	}

	// Fall back to the local transform when detached from the tree (tests,
	// offline tools) instead of spamming get_global_transform errors.
	const Transform3D map_inv = is_inside_tree() ? get_global_transform().affine_inverse() : get_transform().affine_inverse();

	// One surface per unique material.
	LocalVector<Ref<Material>> materials;
	for (LevelBrush *brush : brushes) {
		for (int f = 0; f < brush->get_face_count(); f++) {
			Ref<Material> mat = _get_face_material_or_default(brush, f);
			bool found = false;
			for (const Ref<Material> &m : materials) {
				if (m == mat) {
					found = true;
					break;
				}
			}
			if (!found) {
				materials.push_back(mat);
			}
		}
	}

	Ref<ArrayMesh> mesh;
	mesh.instantiate();

	PackedVector3Array collision_faces;

	for (const Ref<Material> &mat : materials) {
		PackedVector3Array verts;
		PackedVector3Array normals;
		PackedVector2Array uvs;

		for (LevelBrush *brush : brushes) {
			// Brush-local -> map-local transform.
			const Transform3D brush_to_map = map_inv * (brush->is_inside_tree() ? brush->get_global_transform() : brush->get_transform());
			const Basis normal_basis = brush_to_map.basis.inverse().transposed();

			for (int f = 0; f < brush->get_face_count(); f++) {
				if (_get_face_material_or_default(brush, f) != mat) {
					continue;
				}
				Vector<Vector3> v, n;
				Vector<Vector2> uv;
				brush->get_bake_surface_data(f, v, n, uv);
				for (int i = 0; i < v.size(); i++) {
					verts.push_back(brush_to_map.xform(v[i]));
					Vector3 nn = (normal_basis.xform(n[i])).normalized();
					normals.push_back(nn);
					uvs.push_back(uv[i]);
				}
			}
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

	// Collision + occluder geometry (map-local).
	for (LevelBrush *brush : brushes) {
		const Transform3D brush_to_map = map_inv * (brush->is_inside_tree() ? brush->get_global_transform() : brush->get_transform());
		Vector<Vector3> faces;
		brush->get_collision_faces(faces);
		for (const Vector3 &p : faces) {
			collision_faces.push_back(brush_to_map.xform(p));
		}
	}

	if (mesh->get_surface_count() == 0 || collision_faces.is_empty()) {
		return nullptr; // Nothing renderable/solid (e.g. all faces deleted).
	}

	MeshInstance3D *mi = memnew(MeshInstance3D);
	mi->set_mesh(mesh);
	mi->set_name(String(get_name()) + "_Baked");

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
