/**************************************************************************/
/*  level_editor_mirror.cpp                                               */
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

// Mirror tool: clip-style two-point plane line; the overlay previews the
// mirrored brush live and Enter creates it as a NEW LevelBrush node sibling
// (Hammer Ctrl+M semantics - no welding into the original).
// These are LevelEditorScreen member functions, split out for organization.

#include "../../../level_constants.h"
#include "../../level_editor_screen.h"

#include "editor/editor_interface.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/control.h"

void LevelEditorScreen::_mirror_begin(LevelBrush *p_brush, const Vector3 &p_point, LevelEditorViewport *p_vp) {
	_mirror_cancel();
	mirror_brush = p_brush;
	mirror_active = true;
	mirror_drawing = true;
	mirror_drag_point = 1;
	mirror_viewport = p_vp;
	mirror_points[0] = _snap(p_point);
	mirror_points[1] = mirror_points[0];
	mirror_view_dir = -p_vp->get_camera()->get_global_transform().basis[2];
	_update_overlays();
}

Plane LevelEditorScreen::_mirror_plane() const {
	return _two_point_plane(mirror_points, mirror_view_dir, mirror_brush);
}

void LevelEditorScreen::_mirror_apply() {
	if (!mirror_active || !mirror_brush || !current_map) {
		return;
	}
	Plane plane = _mirror_plane();
	if (plane.normal.is_zero_approx()) {
		_mirror_cancel();
		return;
	}

	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	ERR_FAIL_NULL(root);

	// Build the mirrored copy (deep copy of the source, reflected) and give
	// it the source's transform so it lands mirrored in world space.
	LevelBrush *copy = mirror_brush->duplicate_brush();
	copy->set_name("Brush");
	copy->mirror(plane);
	copy->set_transform(mirror_brush->get_transform());

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Mirror Brush"));
	undo_redo->add_do_method(current_map, "add_child", copy);
	undo_redo->add_do_method(copy, "set_owner", root);
	undo_redo->add_do_method(current_map, "refresh");
	undo_redo->add_undo_method(current_map, "remove_child", copy);
	undo_redo->add_undo_method(current_map, "refresh");
	undo_redo->add_do_reference(copy); // Keep the node alive across undo.
	undo_redo->commit_action();

	mirror_active = false;
	mirror_drawing = false;
	mirror_drag_point = -1;
	mirror_brush = nullptr;
	mirror_viewport = nullptr;
	_refresh_map();
}

void LevelEditorScreen::_mirror_cancel() {
	mirror_active = false;
	mirror_drawing = false;
	mirror_drag_point = -1;
	mirror_brush = nullptr;
	mirror_viewport = nullptr;
	_update_overlays();
}

void LevelEditorScreen::_draw_mirror(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (tool != TOOL_MIRROR || !mirror_active || !mirror_brush) {
		return;
	}

	// Mirror points + line (clip colors - same interaction language).
	Vector2 s0, s1;
	bool has0 = p_vp->project(mirror_points[0], s0);
	bool has1 = p_vp->project(mirror_points[1], s1);
	if (has0 && has1) {
		p_canvas->draw_line(s0, s1, LevelEditorColors::CLIP_LINE, 2.0);
	}
	for (int i = 0; i < 2; i++) {
		Vector2 sp;
		if (p_vp->project(mirror_points[i], sp)) {
			bool hot = (mirror_drag_point == i);
			real_t hs_px = 4.0 * EDSCALE;
			p_canvas->draw_rect(Rect2(sp - Vector2(hs_px, hs_px), Size2(hs_px * 2, hs_px * 2)), hot ? LevelEditorColors::CLIP_POINT_HOT : LevelEditorColors::CLIP);
		}
	}

	// Preview the mirrored copy: wireframe of the source edges reflected
	// across the plane (green, like the ghost block).
	Plane plane = _mirror_plane();
	if (plane.normal.is_zero_approx()) {
		return;
	}
	Transform3D gt = mirror_brush->get_global_transform();
	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = mirror_brush->get_edges();
	for (const LevelBrush::EdgeKey &e : edges) {
		Vector3 a = mirror_brush->get_vertex(e.a) - plane.normal * (2.0 * plane.distance_to(mirror_brush->get_vertex(e.a)));
		Vector3 b = mirror_brush->get_vertex(e.b) - plane.normal * (2.0 * plane.distance_to(mirror_brush->get_vertex(e.b)));
		Vector2 sa, sb;
		if (p_vp->project(gt.xform(a), sa) && p_vp->project(gt.xform(b), sb)) {
			p_canvas->draw_line(sa, sb, LevelEditorColors::GHOST, 2.0);
		}
	}

	p_canvas->draw_string(get_theme_font(SNAME("font"), SNAME("Label")), Vector2(8, 18),
			TTR("Mirror: draw the mirror plane (Enter to apply, Esc to cancel)"),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 13, LevelEditorColors::TEXT_DIM);
}

// ---------------------------------------------------------------------------
// Input handler (dispatched from LevelEditorScreen::forward_input).
// Keys (Enter applies, Esc cancels) are handled in input().
// ---------------------------------------------------------------------------

bool LevelEditorScreen::_mirror_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	if (tool != TOOL_MIRROR) {
		return false;
	}
	Ref<InputEventMouseButton> mb = p_event;
	Ref<InputEventMouseMotion> mm = p_event;
	LevelEditorViewport *vp = p_vp;

	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			if (mirror_active && !mirror_drawing) {
				// Grab a mirror point to adjust.
				for (int i = 0; i < 2; i++) {
					Vector2 sp;
					if (vp->project(mirror_points[i], sp) && sp.distance_to(mb->get_position()) < 10.0 * EDSCALE) {
						mirror_drag_point = i;
						mirror_viewport = vp;
						return true;
					}
				}
			}
			// Otherwise start a new mirror on the clicked brush.
			Vector3 hit;
			LevelBrush *brush = nullptr;
			int f;
			if (_pick_face(p_camera, mb->get_position(), brush, f, hit)) {
				_mirror_begin(brush, hit, vp);
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
					Vector3 center = target->get_global_transform().xform(target->get_center());
					if (vp->ray_to_view_plane(mb->get_position(), center, hit)) {
						_mirror_begin(target, hit, vp);
					}
				}
			}
		} else {
			if (mirror_drawing && mirror_viewport == vp) {
				mirror_drawing = false;
				mirror_drag_point = -1;
			} else if (mirror_drag_point >= 0 && mirror_viewport == vp) {
				mirror_drag_point = -1;
			}
		}
		return true;
	}
	if (mm.is_valid()) {
		if ((mirror_drawing || mirror_drag_point >= 0) && mirror_viewport == vp) {
			// Move the active point on the edit plane THROUGH THE FIRST
			// mirror point (same coplanar constraint as the clip tool).
			Vector3 hit;
			if (vp->ray_to_view_plane(mm->get_position(), mirror_points[0], hit)) {
				if (mirror_drawing) {
					mirror_points[1] = _snap(hit);
					_update_overlays();
				} else if (mirror_drag_point >= 0) {
					mirror_points[mirror_drag_point] = _snap(hit);
					_update_overlays();
				}
			}
		}
		return true;
	}
	return true;
}
