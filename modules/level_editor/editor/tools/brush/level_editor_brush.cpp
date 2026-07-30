/**************************************************************************/
/*  level_editor_brush.cpp                                                */
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

// Block tool: drag-out ghost box with resize handles, dimension labels,
// and brush commit. Includes the shared box-handle picking used by the
// select-mode resize handles. These are LevelEditorScreen member functions,
// split out of level_editor_screen.cpp for organization.

#include "../../../level_constants.h"
#include "../../level_editor_screen.h"
#include "../../level_helpers.h"

#include "core/math/geometry_2d.h"
#include "editor/editor_interface.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/themes/editor_scale.h"

using namespace LevelHelpers;

// Shared box-handle picking: corners (high priority) then face centers.
// Positions are computed in world space via p_xform.
int LevelEditorScreen::_pick_box_handle(LevelEditorViewport *p_vp, const Vector2 &p_screen, const AABB &p_aabb, const Transform3D &p_xform) const {
	const real_t face_tol = LevelEditorHandles::FACE_PICK_TOL * EDSCALE;
	const real_t corner_tol = LevelEditorHandles::CORNER_PICK_TOL * EDSCALE;

	Vector3 corners[8];
	aabb_corners(p_aabb, corners);
	for (int i = 0; i < 8; i++) {
		Vector2 sp;
		if (p_vp->project(p_xform.xform(corners[i]), sp) && sp.distance_to(p_screen) < corner_tol) {
			return GHOST_CORNER_0 + i;
		}
	}

	int best = GHOST_NONE;
	real_t best_d = face_tol;
	for (int i = 0; i < 6; i++) {
		Vector2 sp;
		if (p_vp->project(p_xform.xform(aabb_face_center(p_aabb, i)), sp)) {
			real_t d = sp.distance_to(p_screen);
			if (d < best_d) {
				best_d = d;
				best = GHOST_FACE_XN + i;
			}
		}
	}
	return best;
}

// Is ghost handle h usable for the current brush type in p_vp? Thin
// wrapper over _box_handle_usable with the ghost's quad state.
bool LevelEditorScreen::_ghost_handle_usable(LevelEditorViewport *p_vp, int p_handle) const {
	return _box_handle_usable(p_vp, p_handle, brush_type == BRUSH_QUAD ? ghost_flat_axis : -1);
}

bool LevelEditorScreen::_box_handle_usable(LevelEditorViewport *p_vp, int p_handle, int p_flat_axis) const {
	if (p_handle == GHOST_NONE) {
		return false;
	}
	// View axis of this ortho view (-1 for perspective).
	const int view_axis = LevelHelpers::ortho_view_axis((int)p_vp->get_view_type());
	if (p_handle < GHOST_CORNER_0 && view_axis >= 0) {
		const int axis = (p_handle - GHOST_FACE_XN) / 2;
		if (axis == view_axis) {
			return false; // Undraggable center-stacked handle in this view.
		}
	}
	const int flat = p_flat_axis;
	if (flat < 0) {
		return true; // Box: everything else stays.
	}
	const bool edge_on = (view_axis >= 0 && view_axis != flat);
	if (p_handle >= GHOST_CORNER_0) {
		return !edge_on;
	}
	const int axis = (p_handle - GHOST_FACE_XN) / 2;
	if (axis == flat) {
		return false; // Thickness handle.
	}
	if (edge_on) {
		// Only the handles on the one axis that is neither view nor flat
		// (0+1+2 = 3, so it is 3 - view - flat).
		return axis == 3 - view_axis - flat;
	}
	return true;
}

int LevelEditorScreen::_pick_ghost_handle(LevelEditorViewport *p_vp, const Vector2 &p_screen) const {
	int h = _pick_box_handle(p_vp, p_screen, ghost_aabb, Transform3D());
	return _ghost_handle_usable(p_vp, h) ? h : GHOST_NONE;
}

DisplayServerEnums::CursorShape LevelEditorScreen::_handle_cursor(LevelEditorViewport *p_vp, int p_handle, const AABB &p_aabb, const Transform3D &p_xform) const {
	// Ortho views only: pick the resize cursor from the handle's position
	// relative to the box center - top/bottom faces resize vertically, left/
	// right faces horizontally, corners diagonally (towards their corner).
	if (p_vp->get_view_type() == LevelEditorViewport::VIEW_PERSPECTIVE) {
		return DisplayServerEnums::CURSOR_ARROW;
	}
	Vector2 center_sp, handle_sp;
	if (!p_vp->project(p_xform.xform(p_aabb.get_center()), center_sp)) {
		return DisplayServerEnums::CURSOR_ARROW;
	}
	Vector3 handle_local;
	if (p_handle >= GHOST_CORNER_0) {
		Vector3 corners[8];
		aabb_corners(p_aabb, corners);
		handle_local = corners[p_handle - GHOST_CORNER_0];
	} else {
		handle_local = aabb_face_center(p_aabb, p_handle - GHOST_FACE_XN);
	}
	if (!p_vp->project(p_xform.xform(handle_local), handle_sp)) {
		return DisplayServerEnums::CURSOR_ARROW;
	}
	const Vector2 d = handle_sp - center_sp;
	if (p_handle >= GHOST_CORNER_0) {
		// FDIAG runs top-left to bottom-right (\), BDIAG the other way.
		return (d.x * d.y > 0.0) ? DisplayServerEnums::CURSOR_FDIAGSIZE : DisplayServerEnums::CURSOR_BDIAGSIZE;
	}
	return Math::abs(d.x) > Math::abs(d.y) ? DisplayServerEnums::CURSOR_HSIZE : DisplayServerEnums::CURSOR_VSIZE;
}

bool LevelEditorScreen::_ghost_hit_test(LevelEditorViewport *p_vp, const Vector2 &p_screen) const {
	// A quad seen edge-on has no clickable area (only its endpoint handles):
	// if a corner handle wouldn't be usable, the inside-drag isn't either.
	if (brush_type == BRUSH_QUAD && ghost_flat_axis >= 0 && !_ghost_handle_usable(p_vp, GHOST_CORNER_0)) {
		return false;
	}
	// Screen-space point-in-polygon test against the ghost's projected faces.
	Vector3 corners[8];
	aabb_corners(ghost_aabb, corners);
	static const int face_idx[6][4] = {
		{ 4, 5, 7, 6 },
		{ 1, 0, 2, 3 }, // +Z, -Z
		{ 5, 1, 3, 7 },
		{ 0, 4, 6, 2 }, // +X, -X
		{ 7, 6, 2, 3 },
		{ 0, 1, 5, 4 }, // +Y, -Y
	};
	for (auto &f : face_idx) {
		Vector<Vector2> quad;
		quad.resize(4);
		bool ok = true;
		for (int i = 0; i < 4; i++) {
			if (!p_vp->project(corners[f[i]], quad.write[i])) {
				ok = false;
				break;
			}
		}
		if (!ok) {
			continue;
		}
		if (Geometry2D::is_point_in_polygon(p_screen, quad)) {
			return true;
		}
	}
	return false;
}

bool LevelEditorScreen::_ghost_ray_to_edit_plane(LevelEditorViewport *p_vp, const Vector2 &p_screen, Vector3 &r_hit) const {
	return p_vp->ray_to_view_plane(p_screen, ghost_aabb.get_center(), r_hit);
}

// Intersect the mouse ray with a plane that contains p_point and the given
// axis, oriented as perpendicular to the camera as possible. This is what
// allows face/corner handles to move along the view plane's fixed axis (e.g.
// up/down in the top view), which the view-plane intersection cannot do.
bool LevelEditorScreen::_ray_to_axis_plane(LevelEditorViewport *p_vp, const Vector2 &p_screen, const Vector3 &p_point, int p_axis, Vector3 &r_hit) const {
	Vector3 ro, rd;
	p_vp->get_ray(p_screen, ro, rd);
	Plane pl = LevelHelpers::axis_drag_plane(p_point, p_axis, p_vp->get_camera()->get_global_position());
	return pl.intersects_ray(ro, rd, &r_hit);
}

void LevelEditorScreen::_ghost_handle_drag_to(LevelEditorViewport *p_vp, const Vector2 &p_mouse) {
	// Drag the handle along its axis (face) or freely (corner), snapped.
	Vector3 c = ghost_aabb.get_center();
	Vector3 mins = ghost_aabb.position;
	Vector3 maxs = ghost_aabb.position + ghost_aabb.size;

	int h = ghost_handle_drag;
	if (h >= GHOST_CORNER_0) {
		int ci = h - GHOST_CORNER_0;
		Vector3 corners[8];
		aabb_corners(ghost_aabb, corners);
		const Vector3 &corner = corners[ci];

		// Drag each of the corner's three axes on its own camera-facing plane,
		// so vertical movement works in the top view (and vice versa). Each
		// component uses the last mouse hit on that axis' plane.
		for (int axis = 0; axis < 3; axis++) {
			if (axis == ghost_flat_axis) {
				continue; // Quad corners stay on the flat plane.
			}
			Vector3 hit;
			if (!_ray_to_axis_plane(p_vp, p_mouse, corner, axis, hit)) {
				continue;
			}
			real_t v = _snap(hit[axis]);
			if (ci & (1 << axis)) {
				maxs[axis] = MAX(v, mins[axis] + CMP_EPSILON);
			} else {
				mins[axis] = MIN(v, maxs[axis] - CMP_EPSILON);
			}
		}
	} else {
		// Face handle: slide that face along its own axis. Intersect the mouse
		// ray with a camera-facing plane that contains the axis - the view plane
		// is parallel to the face axis in two of the three views (e.g. the top
		// view's XZ plane can never move a top/bottom face up or down).
		int axis = (h - GHOST_FACE_XN) / 2; // 0=x, 1=y, 2=z
		bool is_max = ((h - GHOST_FACE_XN) % 2) == 1;

		Vector3 hit;
		if (!_ray_to_axis_plane(p_vp, p_mouse, c, axis, hit)) {
			return;
		}
		real_t v = _snap(hit[axis]);
		if (is_max) {
			maxs[axis] = MAX(v, mins[axis] + CMP_EPSILON);
		} else {
			mins[axis] = MIN(v, maxs[axis] - CMP_EPSILON);
		}
	}

	ghost_aabb = AABB(mins, maxs - mins);
	_update_overlays();
}

void LevelEditorScreen::_ghost_commit() {
	ghost_active = false;
	ghost_handle_hover = GHOST_NONE;
	ghost_handle_drag = GHOST_NONE;
	ghost_moving = false;
	const int committed_flat_axis = ghost_flat_axis;
	ghost_flat_axis = -1;

	LevelMap *map = _get_or_create_map();
	ERR_FAIL_NULL(map);

	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	ERR_FAIL_NULL(root);

	Transform3D map_inv = map->get_global_transform().affine_inverse();

	LevelBrush *brush = memnew(LevelBrush);
	brush->set_name("Brush");
	switch (brush_type) {
		case BRUSH_QUAD: {
			// Quad: a single flat polygon on the ghost's plane (recorded at drag
			// time), wound CCW seen from the +axis side - same outward convention
			// as setup_box.
			AABB bb = map_inv.xform(ghost_aabb);
			Vector3 mn = bb.position;
			Vector3 mx = bb.position + bb.size;
			int flat_axis = committed_flat_axis;
			if (flat_axis < 0) {
				flat_axis = 1; // Fallback (shouldn't happen): floor quad.
			}
			Vector3 quad[4];
			switch (flat_axis) {
				case 0: // YZ plane at max X, normal +X (CCW seen from +X).
					quad[0] = Vector3(mx.x, mn.y, mn.z);
					quad[1] = Vector3(mx.x, mx.y, mn.z);
					quad[2] = Vector3(mx.x, mx.y, mx.z);
					quad[3] = Vector3(mx.x, mn.y, mx.z);
					break;
				case 1: // XZ plane at max Y, normal +Y (CCW seen from +Y).
					quad[0] = Vector3(mn.x, mx.y, mn.z);
					quad[1] = Vector3(mn.x, mx.y, mx.z);
					quad[2] = Vector3(mx.x, mx.y, mx.z);
					quad[3] = Vector3(mx.x, mx.y, mn.z);
					break;
				default: // XY plane at max Z, normal +Z (CCW seen from +Z).
					quad[0] = Vector3(mn.x, mn.y, mx.z);
					quad[1] = Vector3(mx.x, mn.y, mx.z);
					quad[2] = Vector3(mx.x, mx.y, mx.z);
					quad[3] = Vector3(mn.x, mx.y, mx.z);
					break;
			}
			brush->setup_quad(quad);
		} break;
		case BRUSH_SPHERE:
			brush->setup_sphere(map_inv.xform(ghost_aabb), brush_sphere_sides);
			break;
		case BRUSH_BLOCK:
		default:
			brush->setup_box(map_inv.xform(ghost_aabb));
			break;
	}
	// New brushes inherit the active material; a null active material leaves
	// faces empty so the map default applies at bake/preview time.
	if (active_material.is_valid()) {
		brush->set_all_face_materials(active_material);
	}
	// NOTE: do NOT set_owner here - the brush is not in the tree until the
	// undo action's add_child runs, and set_owner before that errors with
	// "Invalid owner. Owner must be an ancestor in the tree."

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Add Level Brush"));
	undo_redo->add_do_method(map, "add_child", brush);
	undo_redo->add_do_method(brush, "set_owner", root);
	undo_redo->add_do_method(map, "refresh");
	undo_redo->add_undo_method(map, "remove_child", brush);
	undo_redo->add_undo_method(map, "refresh");
	undo_redo->add_do_reference(brush); // Keep the node alive across undo.
	undo_redo->commit_action();

	_mesh_selection_set(brush);
	_refresh_map();
}

void LevelEditorScreen::_ghost_cancel() {
	ghost_active = false;
	ghost_handle_hover = GHOST_NONE;
	ghost_handle_drag = GHOST_NONE;
	ghost_moving = false;
	ghost_flat_axis = -1;
	sphere_preview_valid = false; // Drop the cached wireframe with the ghost.
	_update_overlays();
}

void LevelEditorScreen::_rebuild_sphere_preview(const AABB &p_aabb) {
	if (sphere_preview_valid && sphere_preview_sides == brush_sphere_sides &&
			sphere_preview_aabb.position.is_equal_approx(p_aabb.position) &&
			sphere_preview_aabb.size.is_equal_approx(p_aabb.size)) {
		return; // Cache hit: same AABB + sides.
	}
	sphere_preview_segs.clear();

	// Build the wireframe DIRECTLY (latitude rings + longitude spokes) - no
	// LevelBrush, no faces, no per-face rewind (setup_sphere's costly part).
	// For a pure line preview, winding/normals are irrelevant, so this is far
	// cheaper and identical on screen (fixes AABB-drag lag).
	const int sides = CLAMP(brush_sphere_sides, 4, 64);
	const int rings = MAX(sides / 2, 2);
	const Vector3 center = p_aabb.get_center();
	const Vector3 radii = p_aabb.size * 0.5;

	auto ring_point = [&](int p_ring, int p_side) -> Vector3 {
		const real_t phi = Math::PI * (real_t)p_ring / (real_t)rings; // 0..PI
		const real_t theta = Math::TAU * (real_t)p_side / (real_t)sides;
		const real_t ring_r = Math::sin(phi);
		return center + Vector3(ring_r * Math::cos(theta) * radii.x, Math::cos(phi) * radii.y, ring_r * Math::sin(theta) * radii.z);
	};

	// Latitude rings (interior).
	for (int r = 1; r < rings; r++) {
		for (int s = 0; s < sides; s++) {
			sphere_preview_segs.push_back(ring_point(r, s));
			sphere_preview_segs.push_back(ring_point(r, s + 1));
		}
	}
	// Longitude spokes (pole to pole through each side).
	const Vector3 top = center + Vector3(0, radii.y, 0);
	const Vector3 bottom = center - Vector3(0, radii.y, 0);
	for (int s = 0; s < sides; s++) {
		Vector3 prev = top;
		for (int r = 1; r < rings; r++) {
			const Vector3 p = ring_point(r, s);
			sphere_preview_segs.push_back(prev);
			sphere_preview_segs.push_back(p);
			prev = p;
		}
		sphere_preview_segs.push_back(prev);
		sphere_preview_segs.push_back(bottom);
	}

	sphere_preview_aabb = p_aabb;
	sphere_preview_sides = brush_sphere_sides;
	sphere_preview_valid = true;
}

void LevelEditorScreen::_draw_sphere_preview(LevelEditorViewport *p_vp, Control *p_canvas, const Color &p_col) {
	for (uint32_t i = 0; i + 1 < sphere_preview_segs.size(); i += 2) {
		Vector2 a, b;
		if (p_vp->project_segment(sphere_preview_segs[i], sphere_preview_segs[i + 1], a, b)) {
			p_canvas->draw_line(a, b, p_col, 2.0);
		}
	}
}

void LevelEditorScreen::_draw_ghost(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (!ghost_active) {
		return;
	}

	Vector3 corners[8];
	aabb_corners(ghost_aabb, corners);

	Color col = LevelEditorColors::GHOST;
	// Shape wireframe. The sphere draws its inscribed mesh on top of the box
	// cage (the cage carries the resize handles, so it always shows).
	switch (brush_type) {
		case BRUSH_SPHERE:
			_rebuild_sphere_preview(ghost_aabb);
			_draw_sphere_preview(p_vp, p_canvas, col);
			break;
		case BRUSH_BLOCK:
		case BRUSH_QUAD:
		default:
			break;
	}

	// Box edges (the AABB cage the handles act on).
	for (auto &e : AABB_EDGE_IDX) {
		Vector2 a, b;
		if (p_vp->project(corners[e[0]], a) && p_vp->project(corners[e[1]], b)) {
			p_canvas->draw_line(a, b, col, 2.0);
		}
	}

	// Face handles: squares at face centers (quad: no thickness handles, and
	// edge-on views show only the two endpoint handles).
	for (int i = 0; i < 6; i++) {
		if (!_ghost_handle_usable(p_vp, GHOST_FACE_XN + i)) {
			continue;
		}
		Vector3 fc = aabb_face_center(ghost_aabb, i);
		Vector2 sp;
		if (p_vp->project(fc, sp)) {
			bool hot = (ghost_handle_hover == GHOST_FACE_XN + i || ghost_handle_drag == GHOST_FACE_XN + i);
			Color hc = hot ? LevelEditorColors::GHOST_HANDLE_HOT : LevelEditorColors::GHOST_HANDLE;
			real_t hs_px = LevelEditorHandles::FACE_SIZE * EDSCALE;
			p_canvas->draw_rect(Rect2(sp - Vector2(hs_px, hs_px), Size2(hs_px * 2, hs_px * 2)), hc);
		}
	}

	// Corner handles.
	for (int i = 0; i < 8; i++) {
		if (!_ghost_handle_usable(p_vp, GHOST_CORNER_0 + i)) {
			continue;
		}
		Vector2 sp;
		if (p_vp->project(corners[i], sp)) {
			bool hot = (ghost_handle_hover == GHOST_CORNER_0 + i || ghost_handle_drag == GHOST_CORNER_0 + i);
			Color hc = hot ? LevelEditorColors::GHOST_HANDLE_HOT : LevelEditorColors::GHOST_HANDLE;
			real_t hs_px = LevelEditorHandles::CORNER_SIZE * EDSCALE;
			p_canvas->draw_rect(Rect2(sp - Vector2(hs_px, hs_px), Size2(hs_px * 2, hs_px * 2)), hc);
		}
	}

	_draw_dim_labels(p_vp, p_canvas, ghost_aabb);
}

void LevelEditorScreen::_draw_dim_labels(LevelEditorViewport *p_vp, Control *p_canvas, const AABB &p_aabb) {
	Vector3 corners[8];
	aabb_corners(p_aabb, corners);

	Ref<Font> font = get_theme_font(SNAME("font"), SNAME("Label"));
	const int font_size = get_theme_font_size(SNAME("font_size"), SNAME("Label"));
	Color text_col = LevelEditorColors::TEXT;

	struct DimLabel {
		int edge_a, edge_b;
		int axis;
	};
	static const DimLabel dim_labels[3] = {
		{ 2, 3, 0 }, // X edge along the top -> width
		{ 6, 7, 2 }, // Z edge along the top -> depth
		{ 0, 2, 1 }, // Y edge -> height
	};
	for (const DimLabel &dl : dim_labels) {
		// Ortho views show only the two axes visible in that view; the
		// perspective view shows all three.
		switch (p_vp->get_view_type()) {
			case LevelEditorViewport::VIEW_TOP: // X and Z
				if (dl.axis == 1) {
					continue;
				}
				break;
			case LevelEditorViewport::VIEW_FRONT: // X and Y
				if (dl.axis == 2) {
					continue;
				}
				break;
			case LevelEditorViewport::VIEW_SIDE: // Z and Y
				if (dl.axis == 0) {
					continue;
				}
				break;
			default:
				break;
		}
		Vector3 mid = (corners[dl.edge_a] + corners[dl.edge_b]) * 0.5;
		Vector2 sp;
		if (!p_vp->project(mid, sp)) {
			continue;
		}
		String text = String::num(p_aabb.size[dl.axis], 2);
		Vector2 text_size = font->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
		const Vector2 pos = sp - text_size * 0.5 + Vector2(0, text_size.y * 0.35);
		// Black outline behind the text, like the vertex markers.
		p_canvas->draw_string_outline(font, pos, text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, 2 * EDSCALE, LevelEditorColors::VERTEX_OUTLINE);
		p_canvas->draw_string(font, pos, text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, text_col);
	}
}
void LevelEditorScreen::_compute_drag_aabb(Vector3 &r_mins, Vector3 &r_maxs) const {
	r_mins = drag_start.min(drag_current);
	r_maxs = drag_start.max(drag_current);

	LevelEditorViewport::ViewType vt = drag_viewport->get_view_type();
	real_t thickness = grid_size;

	// Quads are flat: zero thickness along the view axis.
	if (brush_type == BRUSH_QUAD) {
		switch (vt) {
			case LevelEditorViewport::VIEW_TOP:
			case LevelEditorViewport::VIEW_PERSPECTIVE:
				r_mins.y = r_maxs.y = _snap(drag_start.y);
				ghost_flat_axis = 1;
				break;
			case LevelEditorViewport::VIEW_FRONT:
				r_mins.z = r_maxs.z = _snap(drag_start.z);
				ghost_flat_axis = 2;
				break;
			case LevelEditorViewport::VIEW_SIDE:
				r_mins.x = r_maxs.x = _snap(drag_start.x);
				ghost_flat_axis = 0;
				break;
		}
		return;
	}
	ghost_flat_axis = -1;

	// Reuse the last brush's Y height if there is one, so walls of uniform
	// height are quick to lay out (edits to the previous block carry over).
	const LevelBrush *ref_brush = selected_brush;
	Vector<LevelBrush *> map_brushes;
	if (!ref_brush && current_map) {
		map_brushes = current_map->get_brushes();
		if (!map_brushes.is_empty()) {
			ref_brush = map_brushes[map_brushes.size() - 1];
		}
	}
	real_t ref_height = -1.0;
	if (ref_brush) {
		real_t min_y = (real_t)Math::INF, max_y = -(real_t)Math::INF;
		for (int i = 0; i < ref_brush->get_vertex_count(); i++) {
			Vector3 w = ref_brush->get_global_transform().xform(ref_brush->get_vertex(i));
			min_y = MIN(min_y, w.y);
			max_y = MAX(max_y, w.y);
		}
		if (max_y - min_y > CMP_EPSILON) {
			ref_height = max_y - min_y;
		}
	}

	switch (vt) {
		case LevelEditorViewport::VIEW_TOP:
		case LevelEditorViewport::VIEW_PERSPECTIVE:
			// Drag plane is the ground; block rises in Y.
			if (ref_height > 0.0) {
				thickness = ref_height;
			}
			r_mins.y = _snap(drag_start.y);
			r_maxs.y = r_mins.y + thickness;
			break;
		case LevelEditorViewport::VIEW_FRONT:
			r_mins.z = _snap(drag_start.z);
			r_maxs.z = r_mins.z + thickness;
			break;
		case LevelEditorViewport::VIEW_SIDE:
			r_mins.x = _snap(drag_start.x);
			r_maxs.x = r_mins.x + thickness;
			break;
	}

	for (int i = 0; i < 3; i++) {
		if (r_maxs[i] - r_mins[i] < CMP_EPSILON) {
			r_maxs[i] = r_mins[i] + thickness;
		}
	}
}

// ---------------------------------------------------------------------------
// Input handler (dispatched from LevelEditorScreen::forward_input).
// ---------------------------------------------------------------------------

bool LevelEditorScreen::_brush_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	if (tool != TOOL_BLOCK) {
		return false;
	}
	Ref<InputEventMouseButton> mb = p_event;
	Ref<InputEventMouseMotion> mm = p_event;
	LevelEditorViewport *vp = p_vp;

	// --- Stage 2: ghost box with resize handles ---
	if (ghost_active) {
		if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
			if (mb->is_pressed()) {
				int h = _pick_ghost_handle(vp, mb->get_position());
				if (h != GHOST_NONE) {
					ghost_handle_drag = h;
					ghost_drag_viewport = vp;
				} else if (_ghost_hit_test(vp, mb->get_position())) {
					// Clicked inside the ghost: drag the whole box.
					ghost_moving = true;
					ghost_drag_viewport = vp;
					Vector3 hit;
					if (_ghost_ray_to_edit_plane(vp, mb->get_position(), hit)) {
						ghost_move_offset = hit - ghost_aabb.position;
					} else {
						ghost_move_offset = Vector3();
					}
				}
			} else {
				ghost_handle_drag = GHOST_NONE;
				ghost_moving = false;
				ghost_drag_viewport = nullptr;
			}
			return true;
		}
		if (mm.is_valid()) {
			if (ghost_moving && ghost_drag_viewport == vp) {
				Vector3 hit;
				if (_ghost_ray_to_edit_plane(vp, mm->get_position(), hit)) {
					Vector3 new_pos = _snap(hit - ghost_move_offset);
					ghost_aabb.position = new_pos;
					_update_overlays();
				}
			} else if (ghost_handle_drag != GHOST_NONE && ghost_drag_viewport == vp) {
				_ghost_handle_drag_to(vp, mm->get_position());
			} else {
				int prev = ghost_handle_hover;
				ghost_handle_hover = _pick_ghost_handle(vp, mm->get_position());
				if (prev != ghost_handle_hover) {
					_update_overlays();
				}
				vp->set_hover_cursor(ghost_handle_hover != GHOST_NONE ? _handle_cursor(vp, ghost_handle_hover, ghost_aabb, Transform3D()) : DisplayServerEnums::CURSOR_ARROW);
			}
			return true;
		}
		Ref<InputEventKey> k = p_event;
		if (k.is_valid() && k->is_pressed()) {
			if (k->get_keycode() == Key::ENTER || k->get_keycode() == Key::KP_ENTER) {
				_ghost_commit();
				return true;
			}
			if (k->get_keycode() == Key::ESCAPE) {
				_ghost_cancel();
				return true;
			}
		}
		return true;
	}

	// --- Stage 1: initial drag ---
	// Esc cancels an in-progress drag.
	Ref<InputEventKey> k = p_event;
	if (k.is_valid() && k->is_pressed() && k->get_keycode() == Key::ESCAPE && dragging) {
		dragging = false;
		drag_active = false;
		drag_viewport = nullptr;
		_update_overlays();
		return true;
	}
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			Vector3 hit;
			if (vp->ray_to_view_plane(mb->get_position(), Vector3(), hit)) {
				drag_start = _snap(hit);
				dragging = true;
				drag_active = false;
				drag_viewport = vp;
				drag_current = drag_start;
			}
		} else if (dragging && drag_viewport == vp) {
			if (drag_active) {
				// Enter ghost state instead of committing immediately.
				Vector3 mins, maxs;
				_compute_drag_aabb(mins, maxs);
				ghost_aabb = AABB(mins, maxs - mins);
				ghost_active = true;
				ghost_handle_hover = GHOST_NONE;
				ghost_handle_drag = GHOST_NONE;
			}
			dragging = false;
			drag_viewport = nullptr;
			_update_overlays();
		}
	} else if (mm.is_valid() && dragging && drag_viewport == vp) {
		Vector3 hit;
		if (vp->ray_to_view_plane(mm->get_position(), Vector3(), hit)) {
			drag_current = _snap(hit);
			drag_active = (drag_current - drag_start).length() > grid_size * 0.5;
			_update_overlays();
		}
	}
	return true;
}
