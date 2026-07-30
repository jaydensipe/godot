/**************************************************************************/
/*  level_editor_select.cpp                                               */
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

// Select tool (TOOL_SELECT, Mesh target): the AABB resize handles on a single
// selected brush - picking, dragging (per-axis snap against the original
// AABB), undo commit, and overlay drawing. Shares the GhostHandle enum and
// the box-handle picking/usability helpers with the block tool's ghost box
// (tools/brush/level_editor_brush.cpp). These are LevelEditorScreen member
// functions, split out of level_editor_screen.cpp for organization.

#include "../../../level_constants.h"
#include "../../level_editor_screen.h"
#include "../../../level_helpers.h"

#include "editor/themes/editor_scale.h"

using namespace LevelHelpers;

bool LevelEditorScreen::_select_ray_to_edit_plane(LevelEditorViewport *p_vp, const Vector2 &p_screen, Vector3 &r_hit) const {
	Vector3 pos = selected_brush ? selected_brush->get_global_position() : Vector3();
	return p_vp->ray_to_view_plane(p_screen, pos, r_hit);
}

AABB LevelEditorScreen::_get_brush_local_aabb(LevelBrush *p_brush) const {
	return LevelHelpers::aabb_from_points(p_brush->get_vertices_data());
}

void LevelEditorScreen::_apply_brush_aabb(LevelBrush *p_brush, const AABB &p_aabb) {
	// Scale current vertices from the current AABB into the target AABB.
	AABB cur = _get_brush_local_aabb(p_brush);
	for (int i = 0; i < p_brush->get_vertex_count(); i++) {
		Vector3 v = p_brush->get_vertex(i);
		for (int axis = 0; axis < 3; axis++) {
			real_t cur_size = cur.size[axis];
			real_t t = (cur_size > CMP_EPSILON) ? (v[axis] - cur.position[axis]) / cur_size : 0.0;
			v[axis] = p_aabb.position[axis] + t * p_aabb.size[axis];
		}
		p_brush->set_vertex(i, v);
	}
}

int LevelEditorScreen::_pick_select_handle(LevelEditorViewport *p_vp, const Vector2 &p_screen) const {
	if (!selected_brush || selected_brushes.size() != 1) {
		return GHOST_NONE;
	}
	const int h = _pick_box_handle(p_vp, p_screen, _get_brush_local_aabb(selected_brush), selected_brush->get_global_transform());
	return _box_handle_usable(p_vp, h, -1) ? h : GHOST_NONE;
}

void LevelEditorScreen::_select_handle_drag_to(LevelEditorViewport *p_vp, const Vector2 &p_mouse) {
	if (!selected_brush) {
		return;
	}

	// Restore original geometry, then apply the resize from scratch (absolute).
	// Use the vertex snapshot - remapping into the original AABB would bake in
	// any data lost by a degenerate intermediate drag.
	selected_brush->set_vertices_data(select_drag_original_verts);

	Transform3D gt = selected_brush->get_global_transform();
	Transform3D inv = gt.affine_inverse();

	AABB bb = select_drag_original_aabb;
	Vector3 mins = bb.position;
	Vector3 maxs = bb.position + bb.size;

	int h = select_handle_drag;

	// Intersect with a camera-facing plane that contains the dragged axis in
	// world space (the view plane is parallel to the Y axis in top view, so it
	// can never move handles up/down), then convert to brush-local.
	if (h >= GHOST_CORNER_0) {
		int ci = h - GHOST_CORNER_0;
		Vector3 corners[8];
		aabb_corners(bb, corners);
		Vector3 wc = gt.xform(corners[ci]);
		for (int axis = 0; axis < 3; axis++) {
			Vector3 hit;
			if (!_ray_to_axis_plane(p_vp, p_mouse, wc, axis, hit)) {
				continue;
			}
			real_t v = _snap(inv.xform(hit)[axis]);
			if (ci & (1 << axis)) {
				maxs[axis] = MAX(v, mins[axis] + grid_size);
			} else {
				mins[axis] = MIN(v, maxs[axis] - grid_size);
			}
		}
	} else {
		int axis = (h - GHOST_FACE_XN) / 2;
		bool is_max = ((h - GHOST_FACE_XN) % 2) == 1;
		Vector3 hit;
		Vector3 wc = gt.xform(bb.get_center());
		if (!_ray_to_axis_plane(p_vp, p_mouse, wc, axis, hit)) {
			return;
		}
		real_t v = _snap(inv.xform(hit)[axis]);
		if (is_max) {
			maxs[axis] = MAX(v, mins[axis] + grid_size);
		} else {
			mins[axis] = MIN(v, maxs[axis] - grid_size);
		}
	}

	_apply_brush_aabb(selected_brush, AABB(mins, maxs - mins));
	_refresh_map();
	_update_overlays();
}

void LevelEditorScreen::_select_handle_end_drag() {
	if (select_handle_drag == GHOST_NONE || !selected_brush) {
		select_handle_drag = GHOST_NONE;
		return;
	}
	select_handle_drag = GHOST_NONE;

	// Commit as undo: original vertices vs current geometry.
	LevelBrush *target = selected_brush;
	PackedVector3Array old_verts = select_drag_original_verts;
	select_drag_original_verts.clear();

	PackedVector3Array new_verts = target->get_vertices_data();
	if (new_verts == old_verts) {
		return;
	}

	Array cur_faces = target->get_faces_data();
	Array cur_mats = target->get_face_materials_data();
	_commit_brush_undo(TTR("Resize Brush"), target, old_verts, cur_faces, cur_mats);
}

void LevelEditorScreen::_draw_select_handles(LevelEditorViewport *p_vp, Control *p_canvas) {
	// Resize handles are single-brush only (per-brush local AABB).
	if (tool != TOOL_SELECT || selection_target != TARGET_MESH || !selected_brush || selected_brushes.size() != 1) {
		return;
	}

	AABB bb = _get_brush_local_aabb(selected_brush);

	Transform3D gt = selected_brush->get_global_transform();
	Color box_col = LevelEditorColors::BRUSH_OUTLINE_SELECTED;

	// Bounding box edges.
	Vector3 corners[8];
	aabb_corners(bb, corners);
	for (auto &e : AABB_EDGE_IDX) {
		Vector2 a, b;
		if (p_vp->project(gt.xform(corners[e[0]]), a) && p_vp->project(gt.xform(corners[e[1]]), b)) {
			p_canvas->draw_line(a, b, box_col, 1.5);
		}
	}

	// Face handles.
	for (int i = 0; i < 6; i++) {
		if (!_box_handle_usable(p_vp, GHOST_FACE_XN + i, -1)) {
			continue;
		}
		Vector3 fc = gt.xform(aabb_face_center(bb, i));
		Vector2 sp;
		if (p_vp->project(fc, sp)) {
			bool hot = (select_handle_hover == GHOST_FACE_XN + i || select_handle_drag == GHOST_FACE_XN + i);
			Color hc = hot ? LevelEditorColors::SELECT_HANDLE_HOT : LevelEditorColors::SELECT_HANDLE;
			real_t hs_px = LevelEditorHandles::FACE_SIZE * EDSCALE;
			p_canvas->draw_rect(Rect2(sp - Vector2(hs_px, hs_px), Size2(hs_px * 2, hs_px * 2)), hc);
		}
	}

	// Corner handles.
	for (int i = 0; i < 8; i++) {
		Vector2 sp;
		if (p_vp->project(gt.xform(corners[i]), sp)) {
			bool hot = (select_handle_hover == GHOST_CORNER_0 + i || select_handle_drag == GHOST_CORNER_0 + i);
			Color hc = hot ? LevelEditorColors::SELECT_HANDLE_HOT : LevelEditorColors::SELECT_HANDLE;
			real_t hs_px = LevelEditorHandles::CORNER_SIZE * EDSCALE;
			p_canvas->draw_rect(Rect2(sp - Vector2(hs_px, hs_px), Size2(hs_px * 2, hs_px * 2)), hc);
		}
	}
}

bool LevelEditorScreen::_select_handles_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	// Select-tool box handles take priority over the move gizmo (Mesh target
	// only - element targets transform via the gizmo).
	if (tool != TOOL_SELECT || selection_target != TARGET_MESH || !selected_brush) {
		return false;
	}
	Ref<InputEventMouseButton> mb = p_event;
	Ref<InputEventMouseMotion> mm = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			int h = _pick_select_handle(p_vp, mb->get_position());
			if (h != GHOST_NONE) {
				select_handle_drag = h;
				select_drag_viewport = p_vp;
				select_drag_original_aabb = _get_brush_local_aabb(selected_brush);
				select_drag_original_verts = selected_brush->get_vertices_data();
				return true;
			}
		} else if (select_handle_drag != GHOST_NONE) {
			_select_handle_end_drag();
			return true;
		}
	} else if (mm.is_valid()) {
		if (select_handle_drag != GHOST_NONE && select_drag_viewport == p_vp) {
			_select_handle_drag_to(p_vp, mm->get_position());
			return true;
		}
		int prev = select_handle_hover;
		select_handle_hover = _pick_select_handle(p_vp, mm->get_position());
		if (prev != select_handle_hover) {
			_update_overlays();
		}
		p_vp->set_hover_cursor(select_handle_hover != GHOST_NONE ? _handle_cursor(p_vp, select_handle_hover, _get_brush_local_aabb(selected_brush), selected_brush->get_global_transform()) : DisplayServerEnums::CURSOR_ARROW);
	}
	return false;
}
