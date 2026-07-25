/**************************************************************************/
/*  level_editor_clip.cpp                                                 */
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

// Clip tool: interactive two-point cut-line state machine (begin, drag,
// cycle keep-side, apply/cancel) plus the cut-line + edge-color preview.
// These are LevelEditorScreen member functions, split out for organization.

#include "../../../level_constants.h"
#include "../../level_editor_screen.h"

#include "editor/themes/editor_scale.h"
#include "scene/gui/control.h"

// ---- Clip tool ---------------------------------------------------------------

void LevelEditorScreen::_clip_begin(LevelBrush *p_brush, const Vector3 &p_point, LevelEditorViewport *p_vp) {
	_clip_cancel();
	clip_brush = p_brush;
	clip_active = true;
	clip_drawing = true;
	clip_drag_point = 1;
	clip_viewport = p_vp;
	clip_side = CLIP_KEEP_FRONT;
	clip_points[0] = _snap(p_point);
	clip_points[1] = clip_points[0];
	clip_view_dir = -p_vp->get_camera()->get_global_transform().basis[2];
	_update_overlays();
}

void LevelEditorScreen::_clip_update_second(const Vector3 &p_point) {
	clip_points[1] = _snap(p_point);
	_update_overlays();
}

int LevelEditorScreen::_pick_clip_point(LevelEditorViewport *p_vp, const Vector2 &p_screen) const {
	for (int i = 0; i < 2; i++) {
		Vector2 sp;
		if (p_vp->project(clip_points[i], sp) && sp.distance_to(p_screen) < 10.0 * EDSCALE) {
			return i;
		}
	}
	return -1;
}

Plane LevelEditorScreen::_two_point_plane(const Vector3 p_points[2], const Vector3 &p_view_dir, const LevelBrush *p_brush) {
	// Plane through both points, containing the view direction. The normal
	// points to the LEFT of the line as drawn on screen (along x view_dir).
	Vector3 along = p_points[1] - p_points[0];
	if (along.length() < CMP_EPSILON) {
		return Plane();
	}
	Vector3 n = along.cross(p_view_dir);
	if (n.length_squared() < CMP_EPSILON) {
		// Line parallel to view dir - degenerate.
		return Plane();
	}
	n.normalize();

	// World plane -> brush-local plane.
	Plane world_plane(n, n.dot(p_points[0]));
	Transform3D gt = p_brush->get_global_transform();
	Transform3D inv = gt.affine_inverse();
	Vector3 local_n = inv.basis.xform(world_plane.normal).normalized();
	Vector3 local_point = inv.xform(p_points[0]);
	return Plane(local_n, local_n.dot(local_point));
}

Plane LevelEditorScreen::_clip_plane() const {
	return _two_point_plane(clip_points, clip_view_dir, clip_brush);
}

void LevelEditorScreen::_clip_apply() {
	if (!clip_active || !clip_brush) {
		return;
	}
	Plane plane = _clip_plane();
	if (plane.normal.is_zero_approx()) {
		_clip_cancel();
		return;
	}
	if (clip_side == CLIP_KEEP_BACK) {
		plane = -plane;
	}

	if (clip_side == CLIP_KEEP_BOTH) {
		// Keep-both: subdivide faces along the line in-place - no caps, no
		// seam, no new brush node. The brush stays one solid.
		LevelBrush *target = clip_brush;
		PackedVector3Array old_verts = target->get_vertices_data();
		Array old_faces = target->get_faces_data();
		Array old_mats = target->get_face_materials_data();

		target->split_faces(plane);
		_commit_brush_undo(TTR("Split Brush Faces"), target, old_verts, old_faces, old_mats);
	} else {
		LevelBrush *target = clip_brush;
		PackedVector3Array old_verts = target->get_vertices_data();
		Array old_faces = target->get_faces_data();
		Array old_mats = target->get_face_materials_data();

		target->clip(plane);
		_commit_brush_undo(TTR("Clip Brush"), target, old_verts, old_faces, old_mats);
	}

	clip_active = false;
	clip_drawing = false;
	clip_drag_point = -1;
	clip_brush = nullptr;
	clip_viewport = nullptr;
	_refresh_map();
}

void LevelEditorScreen::_clip_cancel() {
	clip_active = false;
	clip_drawing = false;
	clip_drag_point = -1;
	clip_brush = nullptr;
	clip_viewport = nullptr;
	_update_overlays();
}

void LevelEditorScreen::_clip_cycle_side() {
	clip_side = (ClipSide)(((int)clip_side + 1) % 3);
	_update_overlays();
}

void LevelEditorScreen::_draw_clip(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (tool != TOOL_CLIP || !clip_active || !clip_brush) {
		return;
	}

	// Clip points + line.
	Color pt_col = LevelEditorColors::CLIP;
	Color line_col = LevelEditorColors::CLIP_LINE;
	Vector2 s0, s1;
	bool has0 = p_vp->project(clip_points[0], s0);
	bool has1 = p_vp->project(clip_points[1], s1);
	if (has0 && has1) {
		p_canvas->draw_line(s0, s1, line_col, 2.0);
	}
	for (int i = 0; i < 2; i++) {
		Vector2 sp;
		if (p_vp->project(clip_points[i], sp)) {
			bool hot = (clip_drag_point == i);
			real_t hs_px = 4.0 * EDSCALE;
			p_canvas->draw_rect(Rect2(sp - Vector2(hs_px, hs_px), Size2(hs_px * 2, hs_px * 2)), hot ? LevelEditorColors::CLIP_POINT_HOT : pt_col);
		}
	}

	// Preview the cut by coloring each brush EDGE: the segment on the kept
	// side of the plane is green, the discarded side red - so the cut reads
	// exactly where the line passes through the brush.
	Plane plane = _clip_plane();
	if (plane.normal.is_zero_approx()) {
		return;
	}
	Transform3D gt = clip_brush->get_global_transform();

	Color kept_col = LevelEditorColors::CLIP_KEPT;
	Color cut_col = LevelEditorColors::CLIP_CUT;

	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = clip_brush->get_edges();
	for (const LevelBrush::EdgeKey &e : edges) {
		Vector3 a_local = clip_brush->get_vertex(e.a);
		Vector3 b_local = clip_brush->get_vertex(e.b);
		real_t da = plane.distance_to(a_local);
		real_t db = plane.distance_to(b_local);

		// Split the edge at the plane if it crosses.
		Vector3 mid_local;
		bool crosses = (da > 0.0) != (db > 0.0);
		if (crosses) {
			real_t t = da / (da - db);
			mid_local = a_local + (b_local - a_local) * t;
		}

		// For keep-both, show the two halves in green/blue instead of green/red.
		Color col_a, col_b;
		switch (clip_side) {
			case CLIP_KEEP_FRONT:
				col_a = (da >= 0.0) ? kept_col : cut_col;
				col_b = (db >= 0.0) ? kept_col : cut_col;
				break;
			case CLIP_KEEP_BACK:
				col_a = (da < 0.0) ? kept_col : cut_col;
				col_b = (db < 0.0) ? kept_col : cut_col;
				break;
			case CLIP_KEEP_BOTH:
			default:
				col_a = (da >= 0.0) ? kept_col : LevelEditorColors::CLIP_HALF;
				col_b = (db >= 0.0) ? kept_col : LevelEditorColors::CLIP_HALF;
				break;
		}

		auto draw_seg = [&](const Vector3 &p1, const Vector3 &p2, const Color &c) {
			Vector2 s1, s2;
			if (p_vp->project(gt.xform(p1), s1) && p_vp->project(gt.xform(p2), s2)) {
				p_canvas->draw_line(s1, s2, c, 2.5);
			}
		};

		if (crosses) {
			draw_seg(a_local, mid_local, col_a);
			draw_seg(mid_local, b_local, col_b);
			// Mark the cut point.
			Vector2 sm;
			if (p_vp->project(gt.xform(mid_local), sm)) {
				p_canvas->draw_rect(Rect2(sm - Vector2(3, 3), Size2(6, 6)), LevelEditorColors::CLIP_MARKER);
			}
		} else {
			draw_seg(a_local, b_local, col_a);
		}
	}

	// Mode hint text in the corner.
	String side_text;
	switch (clip_side) {
		case CLIP_KEEP_FRONT:
			side_text = TTR("Clip: keep LEFT of line (click Clip to cycle, Enter to apply, Esc to cancel)");
			break;
		case CLIP_KEEP_BACK:
			side_text = TTR("Clip: keep RIGHT of line (click Clip to cycle, Enter to apply, Esc to cancel)");
			break;
		case CLIP_KEEP_BOTH:
			side_text = TTR("Clip: split faces along line (click Clip to cycle, Enter to apply, Esc to cancel)");
			break;
	}
	p_canvas->draw_string(get_theme_font(SNAME("font"), SNAME("Label")), Vector2(8, 18), side_text, HORIZONTAL_ALIGNMENT_LEFT, -1, 13, LevelEditorColors::TEXT_DIM);
}

// ---------------------------------------------------------------------------
// Input handler (dispatched from LevelEditorScreen::forward_input).
// ---------------------------------------------------------------------------

bool LevelEditorScreen::_clip_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	if (tool != TOOL_CLIP) {
		return false;
	}
	Ref<InputEventMouseButton> mb = p_event;
	Ref<InputEventMouseMotion> mm = p_event;
	LevelEditorViewport *vp = p_vp;

	// Keys: Enter applies, Esc cancels. (Side cycling is on the Clip
	// toolbar button - Tab is eaten by GUI focus navigation.)
	Ref<InputEventKey> k = p_event;
	if (k.is_valid() && k->is_pressed() && clip_active) {
		if (k->get_keycode() == Key::ENTER || k->get_keycode() == Key::KP_ENTER) {
			_clip_apply();
			return true;
		}
		if (k->get_keycode() == Key::ESCAPE) {
			_clip_cancel();
			return true;
		}
	}

	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			if (clip_active && !clip_drawing) {
				// Grab a clip point to adjust.
				int pi = _pick_clip_point(vp, mb->get_position());
				if (pi >= 0) {
					clip_drag_point = pi;
					clip_viewport = vp;
					return true;
				}
			}
			// Otherwise start a new clip on the clicked brush.
			Vector3 hit;
			LevelBrush *brush = nullptr;
			int f;
			if (_pick_face(p_camera, mb->get_position(), brush, f, hit)) {
				_clip_begin(brush, hit, vp);
			} else if (vp->get_view_type() != LevelEditorViewport::VIEW_PERSPECTIVE) {
				// Ortho views: click anywhere - use the selected brush (or the
				// most recent one) and place the point on the edit plane.
				LevelBrush *target = selected_brush;
				if (!target) {
					Vector<LevelBrush *> brushes = current_map->get_brushes();
					if (!brushes.is_empty()) {
						target = brushes[brushes.size() - 1];
					}
				}
				if (target) {
					// Place the point on the edit plane at the brush's depth.
					Vector3 center = target->get_global_transform().xform(target->get_center());
					if (vp->ray_to_view_plane(mb->get_position(), center, hit)) {
						_clip_begin(target, hit, vp);
					}
				}
			}
		} else {
			if (clip_drawing && clip_viewport == vp) {
				clip_drawing = false;
				clip_drag_point = -1;
			} else if (clip_drag_point >= 0 && clip_viewport == vp) {
				clip_drag_point = -1;
			}
		}
		return true;
	}
	if (mm.is_valid()) {
		if ((clip_drawing || clip_drag_point >= 0) && clip_viewport == vp) {
			// Move the active point on the edit plane THROUGH THE FIRST clip
			// point, so both points stay coplanar (same Y in top view, etc).
			Vector3 hit;
			if (vp->ray_to_view_plane(mm->get_position(), clip_points[0], hit)) {
				if (clip_drawing) {
					_clip_update_second(hit);
				} else if (clip_drag_point >= 0) {
					clip_points[clip_drag_point] = _snap(hit);
					_update_overlays();
				}
			}
		}
		return true;
	}
	return true;
}
