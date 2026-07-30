/**************************************************************************/
/*  level_editor_materials.cpp                                            */
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

#include "level_editor_materials.h"

#include "../../level_constants.h"
#include "../../level_helpers.h"
#include "../level_editor_screen.h"

#include "core/io/resource_loader.h"
#include "core/math/geometry_2d.h"
#include "core/object/class_db.h"
#include "editor/themes/editor_scale.h"
#include "scene/resources/texture.h"

Ref<Material> LevelEditorMaterialCache::get_or_create(const String &p_texture_path) {
	const Ref<Material> *cached = cache.getptr(p_texture_path);
	if (cached && cached->is_valid()) {
		return *cached;
	}
	Ref<Texture2D> tex = ResourceLoader::load(p_texture_path);
	ERR_FAIL_COND_V_MSG(tex.is_null(), Ref<Material>(), "Cannot load texture from path '" + p_texture_path + "'.");
	Ref<StandardMaterial3D> std_mat;
	std_mat.instantiate();
	std_mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
	std_mat->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_NEAREST_WITH_MIPMAPS);
	std_mat->set_name(tex->get_path().get_file());
	cache[p_texture_path] = std_mat;
	return std_mat;
}

bool LevelEditorMaterials::path_is_material_or_texture(const String &p_path) {
	const String res_type = ResourceLoader::get_resource_type(p_path);
	return ClassDB::is_parent_class(res_type, "Material") || ClassDB::is_parent_class(res_type, "Texture2D");
}

bool LevelEditorMaterials::drag_data_is_material(const Variant &p_data) {
	Dictionary d = p_data;
	if (!d.has("type")) {
		return false;
	}
	const String type = d["type"];
	if (type == "resource") {
		Ref<Material> mat = d["resource"];
		return mat.is_valid();
	}
	if (type == "files") {
		Vector<String> files = d["files"];
		if (files.size() != 1) {
			return false;
		}
		return path_is_material_or_texture(files[0]);
	}
	return false;
}

Ref<Material> LevelEditorMaterials::material_from_drag_data(const Variant &p_data, LevelEditorMaterialCache &p_cache) {
	Dictionary d = p_data;
	if (!d.has("type")) {
		return Ref<Material>();
	}
	const String type = d["type"];

	if (type == "resource") {
		Ref<Material> mat = d["resource"];
		return mat; // Null if the dragged resource isn't a material.
	}

	if (type == "files") {
		Vector<String> files = d["files"];
		if (files.size() != 1) {
			return Ref<Material>();
		}
		const String res_type = ResourceLoader::get_resource_type(files[0]);
		if (ClassDB::is_parent_class(res_type, "Material")) {
			Ref<Material> mat = ResourceLoader::load(files[0]);
			ERR_FAIL_COND_V_MSG(mat.is_null(), Ref<Material>(), "Cannot load material from path '" + files[0] + "'.");
			return mat;
		}
		if (ClassDB::is_parent_class(res_type, "Texture2D")) {
			return p_cache.get_or_create(files[0]);
		}
	}

	return Ref<Material>();
}

// ---------------------------------------------------------------------------
// LevelEditorScreen material-drop members: payload probe + face pick, the
// undo-committed apply, and the marching-ants drop-target highlight drawn on
// the viewport's PreviewOverlay. The viewport-side drag forwarding and drop
// state live in level_editor_viewport.cpp.

bool LevelEditorScreen::_material_drop_probe(Camera3D *p_camera, const Vector2 &p_screen, const Variant &p_data, LevelBrush *&r_brush, int &r_face) const {
	if (!LevelEditorMaterials::drag_data_is_material(p_data)) {
		r_brush = nullptr;
		r_face = -1;
		return false;
	}
	return _material_drop_pick(p_camera, p_screen, r_brush, r_face);
}

bool LevelEditorScreen::_material_drop_pick(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, int &r_face) const {
	r_brush = nullptr;
	r_face = -1;
	if (!current_map) {
		return false;
	}

	// Face target drops on the hovered face; everything else drops on the
	// whole brush under the cursor (face ray-pick doubles as the brush pick).
	Vector3 hit;
	if (!_pick_face(p_camera, p_screen, r_brush, r_face, hit)) {
		r_brush = nullptr;
		r_face = -1;
		return false;
	}
	if (selection_target != TARGET_FACE) {
		r_face = -1;
	}
	return true;
}

void LevelEditorScreen::_apply_material_drop(LevelBrush *p_brush, int p_face, const Variant &p_data) {
	Ref<Material> mat = LevelEditorMaterials::material_from_drag_data(p_data, texture_material_cache);
	ERR_FAIL_COND(mat.is_null());

	// Snapshot the serialized properties for undo, then apply live.
	PackedVector3Array old_verts = p_brush->get_vertices_data();
	Array old_faces = p_brush->get_faces_data();
	Array old_mats = p_brush->get_face_materials_data();

	if (p_face >= 0) {
		p_brush->set_face_material(p_face, mat);
	} else {
		p_brush->set_all_face_materials(mat);
	}

	_commit_brush_undo(p_face >= 0 ? TTR("Apply Face Material") : TTR("Apply Brush Material"), p_brush, old_verts, old_faces, old_mats, false);
	_update_overlays();
}

void LevelEditorScreen::_draw_material_drop(LevelEditorViewport *p_vp, Control *p_canvas) {
	// Material drop target highlight (dragging a material/texture over this
	// viewport): face mode highlights the hovered face, other targets the
	// whole brush outline - both with marching-ants dashes (drop_phase
	// scrolls the pattern along the path). Drawn on the viewport's dedicated
	// PreviewOverlay so the animation doesn't repaint the main overlay.
	if (!p_vp->drop_active || !p_vp->drop_brush) {
		return;
	}
	Transform3D gt = p_vp->drop_brush->get_global_transform();
	const real_t dash_len = 8.0 * EDSCALE;
	const real_t period = dash_len * 2.0;
	// Near-plane-clipped segments can unproject to endpoints hundreds of
	// thousands of pixels off-screen (asymptotic projection) - the helper clips
	// to the overlay rect (with a small margin) before dashing.
	const Rect2 visible_rect = LevelHelpers::overlay_visible_rect(p_canvas);
	auto march = [&](const Vector2 &p_a, const Vector2 &p_b, real_t p_phase, real_t p_width) -> real_t {
		return LevelHelpers::draw_marching_segment(p_canvas, p_a, p_b, p_phase, p_width, LevelEditorColors::SELECTED_ELEMENT, dash_len, visible_rect);
	};
	if (p_vp->drop_face >= 0) {
		LocalVector<int> poly = p_vp->drop_brush->get_face(p_vp->drop_face);
		if (poly.size() >= 3) {
			Vector<Vector3> world;
			for (int idx : poly) {
				world.push_back(gt.xform(p_vp->drop_brush->get_vertex(idx)));
			}
			PackedVector2Array pts;
			if (p_vp->project_polygon(world, pts)) {
				// Skip the fill when near-plane clipping blew the polygon up to
				// astronomic screen coordinates (rasterizing it would scan
				// millions of pixels). The clipped dashes still draw.
				bool fill_ok = true;
				const real_t coord_limit = LevelEditorHandles::FILL_COORD_LIMIT;
				for (const Vector2 &p : pts) {
					if (Math::abs(p.x) > coord_limit || Math::abs(p.y) > coord_limit) {
						fill_ok = false;
						break;
					}
				}
				if (fill_ok && !Geometry2D::triangulate_polygon(pts).is_empty()) {
					p_canvas->draw_colored_polygon(pts, LevelEditorColors::SELECTED_FACE_FILL);
				}
				// The phase runs continuously around the loop so dashes turn
				// corners instead of resetting per edge. Orange + thicker than
				// the green hover highlight underneath so the drop target is
				// distinguishable from a plain hover.
				real_t phase = Math::fposmod((real_t)p_vp->drop_phase * EDSCALE, period);
				for (int i = 0; i < pts.size(); i++) {
					phase = march(pts[i], pts[(i + 1) % pts.size()], phase, 3.0);
				}
			}
		}
	} else {
		const real_t phase = Math::fposmod((real_t)p_vp->drop_phase * EDSCALE, period);
		const HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> &edges = p_vp->drop_brush->get_edges();
		for (const LevelBrush::EdgeKey &e : edges) {
			Vector2 a, b;
			if (p_vp->project_segment(gt.xform(p_vp->drop_brush->get_vertex(e.a)), gt.xform(p_vp->drop_brush->get_vertex(e.b)), a, b)) {
				march(a, b, phase, 3.0);
			}
		}
	}
}
