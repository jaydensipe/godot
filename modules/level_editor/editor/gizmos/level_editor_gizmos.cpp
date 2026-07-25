/**************************************************************************/
/*  level_editor_gizmos.cpp                                               */
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
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

// Manipulation gizmos for the level editor: the translate/scale arrow gizmo
// and the rotate ring gizmo (picking, dragging, drawing, undo commits, and
// the Face-mode Shift+drag extrude). These are LevelEditorScreen member
// functions, split out of level_editor_screen.cpp for organization.

#include "../../level_constants.h"
#include "../level_helpers.h"
#include "../level_editor_screen.h"

#include "editor/editor_undo_redo_manager.h"
#include "editor/themes/editor_scale.h"

using LevelEditorColors::GIZMO_PLANE_EXTENT;

// ---- Manipulation gizmo ---------------------------------------------------

bool LevelEditorScreen::_has_selection() const {
	switch (mode) {
		case MODE_SELECT:
		case MODE_ROTATE:
		case MODE_SCALE:
			return selected_brush != nullptr; // Whole brush is the selection.
		case MODE_FACE:
			return !selected_faces.is_empty();
		case MODE_EDGE:
			return !selected_edges.is_empty();
		case MODE_VERTEX:
			return !selected_vertices.is_empty();
		default:
			return false;
	}
}

Vector3 LevelEditorScreen::_get_gizmo_origin() const {
	if (mode == MODE_SELECT || mode == MODE_ROTATE || mode == MODE_SCALE) {
		if (!selected_brush) {
			return Vector3();
		}
		// Gizmo at the brush's geometry center, in world space.
		return selected_brush->get_global_transform().xform(selected_brush->get_center());
	}

	Vector3 sum;
	int count = 0;

	switch (mode) {
		case MODE_FACE: {
			for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
				Transform3D gt = E.key->get_global_transform();
				for (int f : E.value) {
					LocalVector<int> loop = E.key->get_face(f);
					for (int idx : loop) {
						sum += gt.xform(E.key->get_vertex(idx));
						count++;
					}
				}
			}
		} break;
		case MODE_EDGE: {
			for (const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
				Transform3D gt = E.key->get_global_transform();
				for (const LevelBrush::EdgeKey &e : E.value) {
					sum += gt.xform(E.key->get_vertex(e.a));
					sum += gt.xform(E.key->get_vertex(e.b));
					count += 2;
				}
			}
		} break;
		case MODE_VERTEX: {
			for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_vertices) {
				Transform3D gt = E.key->get_global_transform();
				for (int v : E.value) {
					sum += gt.xform(E.key->get_vertex(v));
					count++;
				}
			}
		} break;
		default:
			break;
	}
	return count > 0 ? sum / count : Vector3();
}

Vector<int> LevelEditorScreen::_get_gizmo_vertex_indices(LevelBrush *p_brush) const {
	Vector<int> out;
	if (!p_brush) {
		return out;
	}
	HashSet<int> seen;
	auto add = [&](int idx) {
		if (!seen.has(idx)) {
			seen.insert(idx);
			out.push_back(idx);
		}
	};
	switch (mode) {
		case MODE_FACE: {
			const HashSet<int> *set = selected_faces.getptr(p_brush);
			if (set) {
				for (int f : *set) {
					LocalVector<int> loop = p_brush->get_face(f);
					for (int idx : loop) {
						add(idx);
					}
				}
			}
		} break;
		case MODE_EDGE: {
			const HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> *set = selected_edges.getptr(p_brush);
			if (set) {
				for (const LevelBrush::EdgeKey &e : *set) {
					add(e.a);
					add(e.b);
				}
			}
		} break;
		case MODE_VERTEX: {
			const HashSet<int> *set = selected_vertices.getptr(p_brush);
			if (set) {
				for (int v : *set) {
					add(v);
				}
			}
		} break;
		default:
			break;
	}
	return out;
}

int LevelEditorScreen::_pick_gizmo(Camera3D *p_camera, const Vector2 &p_screen) const {
	if (!_has_selection()) {
		return GIZMO_NONE;
	}
	Vector3 origin = _get_gizmo_origin();
	if (p_camera->is_position_behind(origin)) {
		return GIZMO_NONE;
	}
	Vector2 so = p_camera->unproject_position(origin);

	// Axis length in pixels (fixed screen size, scaled by editor scale).
	const real_t axis_len = 64.0 * EDSCALE;
	const real_t axis_tol = 9.0 * EDSCALE;
	const real_t plane_tol = 12.0 * EDSCALE;
	const real_t center_tol = 7.0 * EDSCALE;

	// World-space scale so the axis projects to axis_len pixels.
	// Approximate: project origin + unit axis and measure pixel length.
	Vector3 axes[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };
	Vector2 axis_end[3];
	bool axis_ok[3] = { false, false, false };
	for (int i = 0; i < 3; i++) {
		Vector3 tip = origin + axes[i];
		if (p_camera->is_position_behind(tip)) {
			continue; // Only this axis is unusable - keep the rest of the gizmo.
		}
		Vector2 st = p_camera->unproject_position(tip);
		axis_end[i] = so + (st - so).normalized() * axis_len;
		axis_ok[i] = true;
	}

	// Check plane handles first (smaller targets). A plane is pickable only
	// if both its axes are visible.
	struct PlaneHandle {
		int a, b;
		GizmoPart part;
	};
	static const PlaneHandle planes[3] = {
		{ 0, 1, GIZMO_XY },
		{ 0, 2, GIZMO_XZ },
		{ 1, 2, GIZMO_YZ },
	};
	for (int p = 0; p < 3; p++) {
		if (!axis_ok[planes[p].a] || !axis_ok[planes[p].b]) {
			continue;
		}
		Vector2 pa = so + (axis_end[planes[p].a] - so) * GIZMO_PLANE_EXTENT;
		Vector2 pb = so + (axis_end[planes[p].b] - so) * GIZMO_PLANE_EXTENT;
		Vector2 center = (so + pa + pb) / 3.0;
		if (p_screen.distance_to(center) < plane_tol) {
			return planes[p].part;
		}
	}

	// Axis lines.
	for (int i = 0; i < 3; i++) {
		if (!axis_ok[i]) {
			continue;
		}
		Vector2 a = so;
		Vector2 b = axis_end[i];
		Vector2 ab = b - a;
		real_t len2 = ab.length_squared();
		real_t t = (len2 > 0) ? CLAMP((p_screen - a).dot(ab) / len2, 0.0, 1.0) : 0.0;
		real_t d = (a + ab * t).distance_to(p_screen);
		if (d < axis_tol) {
			return i;
		}
	}

	// Center: free move in the camera plane.
	if (p_screen.distance_to(so) < center_tol) {
		// Pick the plane most facing the camera for a natural drag.
		Vector3 cam_fwd = -p_camera->get_global_transform().basis[2];
		Vector3 ac = cam_fwd.abs();
		if (ac.z >= ac.x && ac.z >= ac.y) {
			return GIZMO_XY;
		} else if (ac.y >= ac.x) {
			return GIZMO_XZ;
		}
		return GIZMO_YZ;
	}
	return GIZMO_NONE;
}

void LevelEditorScreen::_gizmo_begin_drag(LevelEditorViewport *p_vp, const Vector2 &p_mouse) {
	gizmo_dragging = true;
	gizmo_drag_viewport = p_vp;
	gizmo_drag_mouse_start = p_mouse;
	gizmo_drag_start_origin = _get_gizmo_origin();

	// Snapshot brush vertices for absolute drags + undo.
	gizmo_drag_brush_verts.clear();
	if (mode == MODE_SELECT || mode == MODE_ROTATE || mode == MODE_SCALE) {
		gizmo_drag_original_verts = selected_brush->get_vertices_data();
		gizmo_drag_original_position = selected_brush->get_position();
	} else {
		// Element modes: one snapshot per selected brush.
		HashSet<LevelBrush *> brushes;
		switch (mode) {
			case MODE_FACE:
				for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
					brushes.insert(E.key);
				}
				break;
			case MODE_EDGE:
				for (const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
					brushes.insert(E.key);
				}
				break;
			case MODE_VERTEX:
				for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_vertices) {
					brushes.insert(E.key);
				}
				break;
			default:
				break;
		}
		for (LevelBrush *b : brushes) {
			gizmo_drag_brush_verts[b] = b->get_vertices_data();
		}
	}

	// Shift+drag in Face mode: extrude the selected faces once, then the drag
	// moves the new cap faces along their normals (Hammer-style pull).
	gizmo_extrude_cap_faces.clear();
	gizmo_extrude_normals.clear();
	gizmo_extrude_orig_verts.clear();
	gizmo_extrude_orig_faces.clear();
	gizmo_extrude_orig_mats.clear();
	gizmo_extrude_moved_verts.clear();
	if (gizmo_extrude_drag) {
		for (KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
			LevelBrush *b = E.key;
			gizmo_extrude_orig_verts[b] = b->get_vertices_data();
			gizmo_extrude_orig_faces[b] = b->get_faces_data();
			gizmo_extrude_orig_mats[b] = b->get_face_materials_data();

			// Highest index first so earlier indices stay valid as faces append.
			Vector<int> sorted;
			for (int f : E.value) {
				sorted.push_back(f);
			}
			sorted.sort();
			Vector<int> caps;
			for (int i = sorted.size() - 1; i >= 0; i--) {
				gizmo_extrude_normals.push_back(b->get_face_normal(sorted[i]));
				b->extrude_face(sorted[i], 0.001); // Minimal stub; the drag sets the real distance.
				caps.push_back(sorted[i]); // extrude_face replaces src with the cap in place.
			}
			gizmo_extrude_cap_faces[b] = caps;
			gizmo_extrude_moved_verts[b] = b->get_vertices_data();

			// Update the selection to the caps so overlays track the extrusion.
			E.value.clear();
			for (int c : caps) {
				E.value.insert(c);
			}
		}
		_refresh_map();
	}

	// Build the constraint plane: passes through the gizmo origin, faces the camera.
	Camera3D *cam = p_vp->get_camera();
	Vector3 cam_pos = cam->get_global_position();
	Vector3 n;
	switch (gizmo_drag_part) {
		case GIZMO_X:
		case GIZMO_Y:
		case GIZMO_Z: {
			// Plane contains the axis and faces the camera.
			Vector3 axis = Vector3(gizmo_drag_part == GIZMO_X ? 1 : 0, gizmo_drag_part == GIZMO_Y ? 1 : 0, gizmo_drag_part == GIZMO_Z ? 1 : 0);
			Vector3 to_cam = (cam_pos - gizmo_drag_start_origin).normalized();
			n = axis.cross(to_cam).cross(axis).normalized();
			if (n.is_zero_approx()) {
				n = axis.get_any_perpendicular();
			}
		} break;
		case GIZMO_XY:
			n = Vector3(0, 0, 1);
			break;
		case GIZMO_XZ:
			n = Vector3(0, 1, 0);
			break;
		case GIZMO_YZ:
			n = Vector3(1, 0, 0);
			break;
		default:
			n = Vector3(0, 1, 0);
			break;
	}
	gizmo_drag_plane_normal = n;
	gizmo_drag_plane_point = gizmo_drag_start_origin;

	// Grab offset: where on the drag axis/plane the user actually grabbed,
	// relative to the gizmo origin. Subtracting this from drag hits keeps the
	// first mouse move from jumping (the click is never exactly at the origin).
	gizmo_drag_grab_offset = Vector3();
	Vector3 gro, grd;
	p_vp->get_ray(p_mouse, gro, grd);
	if (gizmo_drag_part == GIZMO_X || gizmo_drag_part == GIZMO_Y || gizmo_drag_part == GIZMO_Z) {
		Vector3 axis;
		axis[gizmo_drag_part] = 1.0;
		Vector3 grab;
		if (LevelHelpers::closest_point_on_line_to_ray(gizmo_drag_start_origin, axis, gro, grd, grab)) {
			gizmo_drag_grab_offset = grab - gizmo_drag_start_origin;
		}
	} else {
		Plane plane(gizmo_drag_plane_normal, gizmo_drag_plane_normal.dot(gizmo_drag_plane_point));
		Vector3 grab;
		if (plane.intersects_ray(gro, grd, &grab)) {
			gizmo_drag_grab_offset = grab - gizmo_drag_start_origin;
		}
	}
}

void LevelEditorScreen::_gizmo_drag_to(LevelEditorViewport *p_vp, const Vector2 &p_mouse) {
	Vector3 ro, rd;
	p_vp->get_ray(p_mouse, ro, rd);

	Vector3 delta;

	if (gizmo_drag_part == GIZMO_X || gizmo_drag_part == GIZMO_Y || gizmo_drag_part == GIZMO_Z) {
		// Axis drag: find the closest point on the axis line to the mouse ray.
		// Works for any view angle, including looking straight down the axis.
		Vector3 axis;
		axis[gizmo_drag_part] = 1.0;

		Vector3 axis_point;
		if (!LevelHelpers::closest_point_on_line_to_ray(gizmo_drag_start_origin, axis, ro, rd, axis_point)) {
			return; // Mouse ray parallel to the axis.
		}
		// Scale mode wants the raw (unsnapped) delta; it snaps the resulting
		// brush SIZE to the grid instead, which avoids start-of-drag jitter.
		if (mode == MODE_SCALE) {
			delta = axis_point - gizmo_drag_start_origin - gizmo_drag_grab_offset;
		} else {
			delta = _snap(axis_point - gizmo_drag_grab_offset) - _snap(gizmo_drag_start_origin);
		}
		// Keep only the axis component.
		Vector3 constrained;
		constrained[gizmo_drag_part] = delta[gizmo_drag_part];
		delta = constrained;
	} else {
		// Plane drag: intersect the mouse ray with the constraint plane.
		Plane plane(gizmo_drag_plane_normal, gizmo_drag_plane_normal.dot(gizmo_drag_plane_point));
		Vector3 hit;
		if (!plane.intersects_ray(ro, rd, &hit)) {
			return;
		}
		if (mode == MODE_SCALE) {
			delta = hit - gizmo_drag_start_origin - gizmo_drag_grab_offset;
		} else {
			delta = _snap(hit - gizmo_drag_grab_offset) - _snap(gizmo_drag_start_origin);
		}

		switch (gizmo_drag_part) {
			case GIZMO_XY:
				delta.z = 0;
				break;
			case GIZMO_XZ:
				delta.y = 0;
				break;
			case GIZMO_YZ:
				delta.x = 0;
				break;
			default:
				break;
		}
	}

	// Scale mode: axis drag distance -> scale factor along that axis.
	if (mode == MODE_SCALE) {
		if (gizmo_drag_uniform_scale) {
			// Click-anywhere uniform drag (started off-gizmo): use mouse X.
			// 400px of drag = 2x scale (100px = 2x felt twitchy).
			real_t factor = 1.0 + (p_mouse.x - gizmo_drag_mouse_start.x) * 0.0025;
			_apply_gizmo_scale_uniform(factor);
		} else {
			_apply_gizmo_scale(delta);
		}
		_update_overlays();
		return;
	}

	_apply_gizmo_delta(delta);
	_update_overlays();
}

// ---- Rotate gizmo ------------------------------------------------------------

// Radius in pixels of the rotate rings on screen (before EDSCALE).
static const real_t ROTATE_RING_PX = 64.0 * EDSCALE;

// World-space ring radius that projects to ROTATE_RING_PX pixels at the
// gizmo origin. Shared by pick + draw so they always agree.
real_t LevelEditorScreen::_rotate_world_radius(LevelEditorViewport *p_vp, const Vector3 &p_origin, const Vector2 &p_center) const {
	Camera3D *cam = p_vp->get_camera();
	Vector3 test = p_origin + Vector3(1, 0, 0);
	if (!cam->is_position_behind(test)) {
		real_t px = cam->unproject_position(test).distance_to(p_center);
		if (px > 0.001) {
			return ROTATE_RING_PX / px;
		}
	}
	return 1.0;
}

// The only usable rotate axis per ortho view (-1 = all, perspective).
int LevelEditorScreen::_rotate_allowed_axis(LevelEditorViewport::ViewType p_type) const {
	switch (p_type) {
		case LevelEditorViewport::VIEW_TOP:
			return 1;
		case LevelEditorViewport::VIEW_FRONT:
			return 2;
		case LevelEditorViewport::VIEW_SIDE:
			return 0;
		default:
			return -1;
	}
}

int LevelEditorScreen::_pick_rotate_ring(LevelEditorViewport *p_vp, const Vector2 &p_screen) const {
	if (!selected_brush) {
		return -1;
	}
	Vector3 origin = _get_gizmo_origin();
	Vector2 center;
	if (!p_vp->project(origin, center)) {
		return -1;
	}

	// The ring plane normal axes; ring radius matches the drawn circle.
	const real_t tol = 8.0 * EDSCALE;
	int best_axis = -1;
	real_t best_dist = tol;

	// In ortho views, only the ring perpendicular to the view plane is usable.
	const int allowed_axis = _rotate_allowed_axis(p_vp->get_view_type());
	const real_t world_radius = _rotate_world_radius(p_vp, origin, center);

	for (int axis = 0; axis < 3; axis++) {
		if (allowed_axis >= 0 && axis != allowed_axis) {
			continue; // Ring disabled in this ortho view.
		}
		// Sample the ring in 3D and project; measure min distance to the mouse.
		const int SEGMENTS = 48;
		real_t min_d = (real_t)Math::INF;
		bool any_front = false;
		for (int s = 0; s < SEGMENTS; s++) {
			real_t a = (real_t)s / SEGMENTS * Math::TAU;
			Vector3 p;
			// Ring in the plane perpendicular to the axis.
			int u = (axis + 1) % 3, v = (axis + 2) % 3;
			p[u] = Math::cos(a) * world_radius;
			p[v] = Math::sin(a) * world_radius;
			Vector3 world = origin + p;
			Vector2 sp;
			if (!p_vp->project(world, sp)) {
				continue;
			}
			any_front = true;
			min_d = MIN(min_d, sp.distance_to(p_screen));
		}
		if (!any_front) {
			continue;
		}
		// Closest projected ring point wins.
		if (min_d < best_dist) {
			best_dist = min_d;
			best_axis = axis;
		}
	}
	return best_axis;
}

real_t LevelEditorScreen::_rotate_screen_angle(LevelEditorViewport *p_vp, const Vector2 &p_screen, int p_axis) const {
	// Intersect the mouse ray with the plane perpendicular to the axis
	// through the gizmo origin; return the angle around the axis from the
	// view-right reference.
	Vector3 origin = _get_gizmo_origin();
	Vector3 ro, rd;
	p_vp->get_ray(p_screen, ro, rd);
	Vector3 normal;
	normal[p_axis] = 1.0;
	Plane pl(normal, normal.dot(origin));
	Vector3 hit;
	if (!pl.intersects_ray(ro, rd, &hit)) {
		return 0.0;
	}
	Vector3 rel = hit - origin;
	int u = (p_axis + 1) % 3, v = (p_axis + 2) % 3;
	return Math::atan2(rel[v], rel[u]);
}

void LevelEditorScreen::_draw_rotate_gizmo(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (mode != MODE_ROTATE || !selected_brush) {
		return;
	}
	Vector3 origin = _get_gizmo_origin();
	Vector2 center;
	if (!p_vp->project(origin, center)) {
		return;
	}

	Color axis_col[3] = { LevelEditorColors::GIZMO_AXIS_X, LevelEditorColors::GIZMO_AXIS_Y, LevelEditorColors::GIZMO_AXIS_Z };

	// World-space radius so the ring projects to ~ROTATE_RING_PX pixels.
	const real_t world_radius = _rotate_world_radius(p_vp, origin, center);

	// In ortho views, only the ring perpendicular to the view plane is usable.
	const int allowed_axis = _rotate_allowed_axis(p_vp->get_view_type());

	const int SEGMENTS = 48;
	for (int axis = 0; axis < 3; axis++) {
		if (allowed_axis >= 0 && axis != allowed_axis) {
			continue; // Ring disabled in this ortho view.
		}
		bool hot = (rotate_hover_axis == axis || rotate_drag_axis == axis);
		Color col = hot ? LevelEditorColors::hot(axis_col[axis]) : axis_col[axis];
		real_t width = (hot ? 3.0 : 2.0) * EDSCALE;

		int u = (axis + 1) % 3, v = (axis + 2) % 3;
		Vector2 prev;
		bool has_prev = false;
		for (int s = 0; s <= SEGMENTS; s++) {
			real_t a = (real_t)s / SEGMENTS * Math::TAU;
			Vector3 p;
			p[u] = Math::cos(a) * world_radius;
			p[v] = Math::sin(a) * world_radius;
			Vector2 sp;
			if (p_vp->project(origin + p, sp)) {
				if (has_prev) {
					p_canvas->draw_line(prev, sp, col, width);
				}
				prev = sp;
				has_prev = true;
			} else {
				has_prev = false;
			}
		}
	}

	// Center dot.
	p_canvas->draw_circle(center, 3.0 * EDSCALE, LevelEditorColors::GIZMO_CENTER);
}

void LevelEditorScreen::_rotate_end_drag() {
	if (rotate_drag_axis < 0 || !selected_brush) {
		rotate_drag_axis = -1;
		return;
	}
	rotate_drag_axis = -1;

	// Commit as undo: vertices before vs after.
	LevelBrush *target = selected_brush;

	PackedVector3Array new_verts = target->get_vertices_data();
	if (new_verts == gizmo_drag_original_verts) {
		return;
	}

	Array cur_faces = target->get_faces_data();
	Array cur_mats = target->get_face_materials_data();
	_commit_brush_undo(TTR("Rotate Brush"), target, gizmo_drag_original_verts, cur_faces, cur_mats);

	gizmo_drag_original_verts.clear();
}

void LevelEditorScreen::_apply_gizmo_rotate(int p_axis, real_t p_angle) {
	if (!selected_brush) {
		return;
	}
	// Rotate original vertices around the brush center, absolute per drag.
	selected_brush->set_vertices_data(gizmo_drag_original_verts);

	Vector3 center = selected_brush->get_center();
	Vector3 axis;
	axis[p_axis] = 1.0;
	Basis rot(axis, p_angle);

	for (int i = 0; i < selected_brush->get_vertex_count(); i++) {
		Vector3 v = selected_brush->get_vertex(i);
		v = center + rot.xform(v - center);
		selected_brush->set_vertex(i, v);
	}
	_refresh_map();
}

void LevelEditorScreen::_apply_gizmo_scale_uniform(real_t p_factor) {
	if (!selected_brush) {
		return;
	}
	selected_brush->set_vertices_data(gizmo_drag_original_verts);

	// Scale around the center with the raw factor, then snap the resulting
	// AABB edges to the grid (same as axis scale).
	AABB bb;
	for (int i = 0; i < gizmo_drag_original_verts.size(); i++) {
		if (i == 0) {
			bb.position = gizmo_drag_original_verts[0];
		} else {
			bb.expand_to(gizmo_drag_original_verts[i]);
		}
	}
	Vector3 center = bb.get_center();

	real_t f = MAX(p_factor, 0.01);
	Vector3 mins = center + (bb.position - center) * f;
	Vector3 maxs = center + (bb.position + bb.size - center) * f;
	for (int axis = 0; axis < 3; axis++) {
		mins[axis] = _snap(mins[axis]);
		maxs[axis] = _snap(maxs[axis]);
		if (maxs[axis] - mins[axis] < grid_size) {
			maxs[axis] = mins[axis] + grid_size;
		}
	}

	for (int i = 0; i < selected_brush->get_vertex_count(); i++) {
		Vector3 v = selected_brush->get_vertex(i);
		for (int axis = 0; axis < 3; axis++) {
			real_t t = (bb.size[axis] > CMP_EPSILON) ? (v[axis] - bb.position[axis]) / bb.size[axis] : 0.0;
			v[axis] = mins[axis] + t * (maxs[axis] - mins[axis]);
		}
		selected_brush->set_vertex(i, v);
	}
	_refresh_map();
}

void LevelEditorScreen::_apply_gizmo_scale(const Vector3 &p_world_delta) {
	if (!selected_brush) {
		return;
	}
	// Restore, then scale original vertices around the brush center (same
	// pivot as uniform scale).
	selected_brush->set_vertices_data(gizmo_drag_original_verts);

	Transform3D inv = selected_brush->get_global_transform().affine_inverse();
	Vector3 local_delta = inv.basis.xform(p_world_delta);

	// Original size from the drag-start snapshot (the live brush is already
	// restored above, but the AABB must come from the ORIGINAL geometry).
	AABB bb;
	for (int i = 0; i < gizmo_drag_original_verts.size(); i++) {
		if (i == 0) {
			bb.position = gizmo_drag_original_verts[0];
		} else {
			bb.expand_to(gizmo_drag_original_verts[i]);
		}
	}

	// Scale around the center with the raw factor, then snap the resulting
	// min/max of each axis to the grid independently - the brush grows from
	// its center AND its edges land on grid lines (the snapped size may be
	// asymmetric per side, which is expected at grid boundaries).
	const real_t SCALE_RATE = 0.25; // 4 world units of drag = 2x scale.
	Vector3 center = bb.get_center();
	Vector3 factors(1, 1, 1);
	if (gizmo_drag_part == GIZMO_XY || gizmo_drag_part == GIZMO_XZ || gizmo_drag_part == GIZMO_YZ) {
		// Center/plane drag: uniform scale by the largest dragged component.
		real_t f = 1.0 + MAX(local_delta.x, MAX(local_delta.y, local_delta.z)) * SCALE_RATE;
		factors = Vector3(f, f, f);
	} else if (gizmo_drag_part >= GIZMO_X && gizmo_drag_part <= GIZMO_Z) {
		factors[gizmo_drag_part] = 1.0 + local_delta[gizmo_drag_part] * SCALE_RATE;
	}
	factors.x = MAX(factors.x, 0.01);
	factors.y = MAX(factors.y, 0.01);
	factors.z = MAX(factors.z, 0.01);

	// Scaled AABB around the center, edges snapped to the grid.
	Vector3 mins = center + (bb.position - center) * factors;
	Vector3 maxs = center + (bb.position + bb.size - center) * factors;
	for (int axis = 0; axis < 3; axis++) {
		mins[axis] = _snap(mins[axis]);
		maxs[axis] = _snap(maxs[axis]);
		if (maxs[axis] - mins[axis] < grid_size) {
			maxs[axis] = mins[axis] + grid_size;
		}
	}

	// Remap original verts from the original AABB into the snapped one.
	for (int i = 0; i < selected_brush->get_vertex_count(); i++) {
		Vector3 v = selected_brush->get_vertex(i);
		for (int axis = 0; axis < 3; axis++) {
			real_t t = (bb.size[axis] > CMP_EPSILON) ? (v[axis] - bb.position[axis]) / bb.size[axis] : 0.0;
			v[axis] = mins[axis] + t * (maxs[axis] - mins[axis]);
		}
		selected_brush->set_vertex(i, v);
	}
	_refresh_map();
}

void LevelEditorScreen::_apply_gizmo_delta(const Vector3 &p_world_delta) {
	if (!selected_brush) {
		return;
	}

	if (mode == MODE_SELECT) {
		// Move the whole brush node. Delta is world; convert into the parent
		// (map) space and apply on top of the drag-start position.
		Node3D *parent = Object::cast_to<Node3D>(selected_brush->get_parent());
		Vector3 local = p_world_delta;
		if (parent) {
			local = parent->get_global_transform().affine_inverse().basis.xform(p_world_delta);
		}
		selected_brush->set_position(gizmo_drag_original_position + local);
		_refresh_map();
		return;
	}

	if (gizmo_extrude_drag) {
		// Extrude drag: reset to the post-extrude topology, then offset each
		// cap face along its own normal by the delta's signed distance onto it.
		int order = 0;
		for (KeyValue<LevelBrush *, Vector<int>> &E : gizmo_extrude_cap_faces) {
			LevelBrush *brush = E.key;
			brush->set_vertices_data(gizmo_extrude_moved_verts[brush]);

			Transform3D inv = brush->get_global_transform().affine_inverse();
			Vector3 local_delta = inv.basis.xform(p_world_delta);

			for (int cap : E.value) {
				const Vector3 &n = gizmo_extrude_normals[order++];
				Vector<int> loop_verts;
				LocalVector<int> loop = brush->get_face(cap);
				for (int idx : loop) {
					loop_verts.push_back(idx);
				}
				brush->move_vertices(loop_verts, n * local_delta.dot(n));
			}
		}
		_refresh_map();
		return;
	}

	// Restore original vertices, then apply the new delta -> absolute drags.
	// Multi-brush: each selected brush's own vertex subset moves.
	for (KeyValue<LevelBrush *, PackedVector3Array> &E : gizmo_drag_brush_verts) {
		LevelBrush *brush = E.key;
		brush->set_vertices_data(E.value);

		// World delta -> brush-local delta (direction only).
		Transform3D inv = brush->get_global_transform().affine_inverse();
		Vector3 local_delta = inv.basis.xform(p_world_delta);

		// Move only the selected vertices; faces deform to fit (Blender-style).
		Vector<int> indices = _get_gizmo_vertex_indices(brush);
		brush->move_vertices(indices, local_delta);
	}
	_refresh_map();
}

void LevelEditorScreen::_gizmo_end_drag() {
	if (!gizmo_dragging) {
		return;
	}
	gizmo_dragging = false;
	gizmo_drag_uniform_scale = false;

	if (!selected_brush) {
		return;
	}

	// Select mode: undoable node move.
	if (mode == MODE_SELECT) {
		Vector3 new_pos = selected_brush->get_position();
		if (!new_pos.is_equal_approx(gizmo_drag_original_position)) {
			LevelBrush *target = selected_brush;
			Vector3 old_pos = gizmo_drag_original_position;
			EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
			undo_redo->create_action(TTR("Move Brush"));
			undo_redo->add_do_property(target, "position", new_pos);
			undo_redo->add_undo_property(target, "position", old_pos);
			undo_redo->commit_action(false);
		}
		return;
	}

	// Scale mode: commit against the drag-start vertex snapshot (the shared
	// element-path map is unused in this mode).
	if (mode == MODE_SCALE) {
		LevelBrush *target = selected_brush;
		PackedVector3Array old_verts = gizmo_drag_original_verts;
		PackedVector3Array new_verts = target->get_vertices_data();
		if (new_verts != old_verts) {
			Array cur_faces = target->get_faces_data();
			Array cur_mats = target->get_face_materials_data();
			_commit_brush_undo(TTR("Scale Brush"), target, old_verts, cur_faces, cur_mats);
		}
		gizmo_drag_original_verts.clear();
		return;
	}

	if (gizmo_extrude_drag) {
		gizmo_extrude_drag = false;
		// Commit the extrusion (topology change at drag start + cap pull) as a
		// single undo action, recorded against the pre-extrude snapshots.
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action(TTR("Extrude Faces"));
		bool any = false;
		for (KeyValue<LevelBrush *, PackedVector3Array> &E : gizmo_extrude_orig_verts) {
			any = true;
			_add_brush_undo_pair(undo_redo, E.key, E.value, gizmo_extrude_orig_faces[E.key], gizmo_extrude_orig_mats[E.key]);
		}
		if (any) {
			undo_redo->add_do_method(current_map, "refresh");
			undo_redo->add_undo_method(current_map, "refresh");
			undo_redo->commit_action(false);
		}
		gizmo_extrude_orig_verts.clear();
		gizmo_extrude_orig_faces.clear();
		gizmo_extrude_orig_mats.clear();
		gizmo_extrude_cap_faces.clear();
		gizmo_extrude_normals.clear();
		gizmo_extrude_moved_verts.clear();
		gizmo_drag_brush_verts.clear();
		return;
	}

	if (gizmo_drag_brush_verts.is_empty()) {
		return;
	}

	// Commit the move as one undo action across all dragged brushes.
	// (Rotate never reaches here - it has its own ring-gizmo drag path.)
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(mode == MODE_SCALE ? TTR("Scale Brush") : TTR("Move Brush Element"));
	bool any_moved = false;
	for (const KeyValue<LevelBrush *, PackedVector3Array> &E : gizmo_drag_brush_verts) {
		LevelBrush *target = E.key;
		PackedVector3Array new_verts = target->get_vertices_data();
		if (new_verts == E.value) {
			continue; // Nothing actually moved in this brush.
		}
		any_moved = true;
		_add_brush_undo_pair(undo_redo, target, E.value, target->get_faces_data(), target->get_face_materials_data());
	}
	if (any_moved) {
		undo_redo->add_do_method(current_map, "refresh");
		undo_redo->add_undo_method(current_map, "refresh");
		undo_redo->commit_action(false);
	}

	gizmo_drag_brush_verts.clear();
}

void LevelEditorScreen::_draw_gizmo(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (mode == MODE_BLOCK || mode == MODE_CLIP || mode == MODE_MIRROR || mode == MODE_ROTATE || !_has_selection()) {
		return; // No arrow gizmo in Block/Clip/Mirror/Rotate mode.
	}
	Vector3 origin = _get_gizmo_origin();
	Vector2 so;
	if (!p_vp->project(origin, so)) {
		return;
	}

	Camera3D *cam = p_vp->get_camera();
	Vector3 axes[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };
	Color axis_col[3] = { LevelEditorColors::GIZMO_AXIS_X, LevelEditorColors::GIZMO_AXIS_Y, LevelEditorColors::GIZMO_AXIS_Z };

	const real_t axis_len = 64.0 * EDSCALE;
	Vector2 axis_end[3];
	bool axis_ok[3] = { false, false, false };
	for (int i = 0; i < 3; i++) {
		Vector3 tip = origin + axes[i];
		if (cam->is_position_behind(tip)) {
			continue; // Skip only the occluded axis, not the whole gizmo.
		}
		Vector2 st = cam->unproject_position(tip);
		axis_end[i] = so + (st - so).normalized() * axis_len;
		axis_ok[i] = true;
	}

	// Plane handles (quads at halfway between axes).
	struct PlaneHandle {
		int a, b;
		GizmoPart part;
	};
	static const PlaneHandle planes[3] = {
		{ 0, 1, GIZMO_XY },
		{ 0, 2, GIZMO_XZ },
		{ 1, 2, GIZMO_YZ },
	};
	for (int p = 0; p < 3; p++) {
		if (!axis_ok[planes[p].a] || !axis_ok[planes[p].b]) {
			continue;
		}
		Vector2 pa = so + (axis_end[planes[p].a] - so) * GIZMO_PLANE_EXTENT;
		Vector2 pb = so + (axis_end[planes[p].b] - so) * GIZMO_PLANE_EXTENT;
		PackedVector2Array quad;
		quad.push_back(so);
		quad.push_back(pa);
		quad.push_back(pa + (pb - so));
		quad.push_back(pb);
		Color c = axis_col[planes[p].a].lerp(axis_col[planes[p].b], 0.5);
		c.a = (gizmo_hover == planes[p].part || gizmo_drag_part == planes[p].part) ? 0.55 : 0.22;
		p_canvas->draw_colored_polygon(quad, c);
	}

	// Axis lines; arrowheads in translate modes, cube tips in Scale mode
	// (matches the 3D editor's scale gizmo).
	for (int i = 0; i < 3; i++) {
		if (!axis_ok[i]) {
			continue;
		}
		bool active = (gizmo_hover == (GizmoPart)i || gizmo_drag_part == (GizmoPart)i);
		Color c = active ? LevelEditorColors::hot(axis_col[i]) : axis_col[i];
		p_canvas->draw_line(so, axis_end[i], c, (active ? 3.0 : 2.0) * EDSCALE);
		if (mode == MODE_SCALE) {
			real_t hs = 5.0 * EDSCALE;
			p_canvas->draw_rect(Rect2(axis_end[i] - Vector2(hs, hs), Size2(hs * 2, hs * 2)), c);
		} else {
			// Arrowhead.
			Vector2 dir = (axis_end[i] - so).normalized();
			Vector2 perp(-dir.y, dir.x);
			real_t arrow_len = 10.0 * EDSCALE;
			real_t arrow_w = 4.0 * EDSCALE;
			p_canvas->draw_line(axis_end[i], axis_end[i] - dir * arrow_len + perp * arrow_w, c, 2.0 * EDSCALE);
			p_canvas->draw_line(axis_end[i], axis_end[i] - dir * arrow_len - perp * arrow_w, c, 2.0 * EDSCALE);
		}
	}

	// Center square.
	real_t cs = 4.0 * EDSCALE;
	p_canvas->draw_rect(Rect2(so - Vector2(cs, cs), Size2(cs * 2, cs * 2)), LevelEditorColors::GIZMO_CENTER);
}

// ---------------------------------------------------------------------------
// Input handlers (dispatched from LevelEditorScreen::forward_input).
// ---------------------------------------------------------------------------

bool LevelEditorScreen::_rotate_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	// Rotate-mode ring gizmo.
	if (mode != MODE_ROTATE || !selected_brush) {
		return false;
	}
	Ref<InputEventMouseButton> mb = p_event;
	Ref<InputEventMouseMotion> mm = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			int axis = _pick_rotate_ring(p_vp, mb->get_position());
			if (axis < 0 && p_vp->get_view_type() != LevelEditorViewport::VIEW_PERSPECTIVE) {
				// Ortho views: click anywhere to rotate around the view axis.
				switch (p_vp->get_view_type()) {
					case LevelEditorViewport::VIEW_TOP:
						axis = 1;
						break;
					case LevelEditorViewport::VIEW_FRONT:
						axis = 2;
						break;
					case LevelEditorViewport::VIEW_SIDE:
						axis = 0;
						break;
					default:
						break;
				}
			}
			if (axis >= 0) {
				rotate_drag_axis = axis;
				rotate_drag_viewport = p_vp;
				rotate_drag_start_angle = _rotate_screen_angle(p_vp, mb->get_position(), axis);
				gizmo_drag_original_verts = selected_brush->get_vertices_data();
				return true;
			}
		} else if (rotate_drag_axis >= 0) {
			_rotate_end_drag();
			return true;
		}
	} else if (mm.is_valid()) {
		if (rotate_drag_axis >= 0 && rotate_drag_viewport == p_vp) {
			real_t cur = _rotate_screen_angle(p_vp, mm->get_position(), rotate_drag_axis);
			real_t delta = cur - rotate_drag_start_angle;
			// Snap to 15 degrees.
			delta = Math::snapped(delta, Math::deg_to_rad(15.0));
			_apply_gizmo_rotate(rotate_drag_axis, delta);
			_update_overlays();
			return true;
		}
		int prev = rotate_hover_axis;
		rotate_hover_axis = _pick_rotate_ring(p_vp, mm->get_position());
		if (prev != rotate_hover_axis) {
			_update_overlays();
		}
	}
	return false;
}

bool LevelEditorScreen::_gizmo_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	// Translate/scale gizmo (Select and element modes).
	if (mode == MODE_BLOCK || mode == MODE_CLIP || mode == MODE_MIRROR || mode == MODE_ROTATE || !_has_selection()) {
		return false;
	}
	Ref<InputEventMouseButton> mb = p_event;
	Ref<InputEventMouseMotion> mm = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			int part = _pick_gizmo(p_camera, mb->get_position());
			if (part != GIZMO_NONE) {
				gizmo_drag_uniform_scale = false;
				gizmo_drag_part = (GizmoPart)part;
				gizmo_extrude_drag = (mode == MODE_FACE && mb->is_shift_pressed());
				_gizmo_begin_drag(p_vp, mb->get_position());
				return true; // Consumed by gizmo.
			}
			if (mode == MODE_SCALE) {
				// Off-gizmo click in Scale mode: drag anywhere to scale
				// uniformly via mouse X.
				gizmo_drag_uniform_scale = true;
				gizmo_drag_part = GIZMO_NONE;
				_gizmo_begin_drag(p_vp, mb->get_position());
				return true;
			}
		} else if (gizmo_dragging) {
			_gizmo_end_drag();
			return true;
		}
	} else if (mm.is_valid()) {
		if (gizmo_dragging) {
			_gizmo_drag_to(p_vp, mm->get_position());
			return true;
		}
		GizmoPart prev = gizmo_hover;
		gizmo_hover = (GizmoPart)_pick_gizmo(p_camera, mm->get_position());
		if (prev != gizmo_hover) {
			_update_overlays();
		}
	}
	return false;
}
