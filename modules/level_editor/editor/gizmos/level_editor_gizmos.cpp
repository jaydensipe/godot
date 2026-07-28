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

// Manipulation gizmos for the level editor: the translate/scale arrow gizmo
// and the rotate ring gizmo (picking, dragging, drawing, undo commits, and
// the Face-mode Shift+drag extrude). These are LevelEditorScreen member
// functions, split out of level_editor_screen.cpp for organization.

#include "../../level_constants.h"
#include "../level_editor_screen.h"
#include "../level_helpers.h"

#include "core/math/geometry_2d.h"
#include "editor/editor_interface.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/themes/editor_scale.h"

using LevelEditorColors::GIZMO_PLANE_EXTENT;

// ---- Shared gizmo geometry --------------------------------------------------

// Axis length in pixels (fixed screen size, scaled by editor scale). Pick and
// draw MUST use one computation so they always agree (GOTCHAS #25).
static const real_t GIZMO_AXIS_LEN = 64.0;

// Plane-handle axis pairs; GizmoPart = GIZMO_XY + index.
static const int GIZMO_PLANE_AXES[3][2] = { { 0, 1 }, { 0, 2 }, { 1, 2 } };

// Screen-space axis endpoints for the gizmo at p_origin: each axis projects
// to GIZMO_AXIS_LEN pixels; axes whose tip is behind the camera are skipped
// individually. Returns false if the origin itself is behind the camera.
static bool compute_gizmo_axes(Camera3D *p_camera, const Vector3 &p_origin, Vector2 &r_origin_2d, Vector2 r_axis_end[3], bool r_axis_ok[3]) {
	if (p_camera->is_position_behind(p_origin)) {
		return false;
	}
	r_origin_2d = p_camera->unproject_position(p_origin);
	static const Vector3 AXES[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };
	const real_t axis_len = GIZMO_AXIS_LEN * EDSCALE;
	for (int i = 0; i < 3; i++) {
		r_axis_ok[i] = false;
		const Vector3 tip = p_origin + AXES[i];
		if (p_camera->is_position_behind(tip)) {
			continue; // Only this axis is unusable - keep the rest of the gizmo.
		}
		const Vector2 st = p_camera->unproject_position(tip);
		r_axis_end[i] = r_origin_2d + (st - r_origin_2d).normalized() * axis_len;
		r_axis_ok[i] = true;
	}
	return true;
}

// ---- Manipulation gizmo ---------------------------------------------------

bool LevelEditorScreen::_has_selection() const {
	switch (selection_target) {
		case TARGET_MESH:
			return !selected_brushes.is_empty(); // Whole brushes are the selection.
		case TARGET_FACE:
			return !selected_faces.is_empty();
		case TARGET_EDGE:
			return !selected_edges.is_empty();
		case TARGET_VERTEX:
			return !selected_vertices.is_empty();
		default:
			return false;
	}
}

Vector3 LevelEditorScreen::_get_gizmo_origin() const {
	if (selection_target == TARGET_MESH) {
		if (selected_brushes.is_empty()) {
			return Vector3();
		}
		// Gizmo at the combined world-space center of all selected brushes.
		Vector3 sum;
		for (LevelBrush *b : selected_brushes) {
			sum += b->get_global_transform().xform(b->get_center());
		}
		return sum / (real_t)selected_brushes.size();
	}

	Vector3 sum;
	int count = 0;

	switch (selection_target) {
		case TARGET_FACE: {
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
		case TARGET_EDGE: {
			for (const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
				Transform3D gt = E.key->get_global_transform();
				for (const LevelBrush::EdgeKey &e : E.value) {
					sum += gt.xform(E.key->get_vertex(e.a));
					sum += gt.xform(E.key->get_vertex(e.b));
					count += 2;
				}
			}
		} break;
		case TARGET_VERTEX: {
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
	switch (selection_target) {
		case TARGET_FACE: {
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
		case TARGET_EDGE: {
			const HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> *set = selected_edges.getptr(p_brush);
			if (set) {
				for (const LevelBrush::EdgeKey &e : *set) {
					add(e.a);
					add(e.b);
				}
			}
		} break;
		case TARGET_VERTEX: {
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
	const Vector3 origin = _get_gizmo_origin();
	Vector2 so;
	Vector2 axis_end[3];
	bool axis_ok[3];
	if (!compute_gizmo_axes(p_camera, origin, so, axis_end, axis_ok)) {
		return GIZMO_NONE;
	}

	const real_t axis_tol = 9.0 * EDSCALE;
	const real_t plane_tol = 4.0 * EDSCALE; // Grow margin around the drawn quad.
	const real_t center_tol = 7.0 * EDSCALE;

	// Check plane handles first (smaller targets), against the same quad that
	// _draw_gizmo draws - the previous centroid-circle test left the quad's
	// corners dead and missed wide/skewed quads entirely. The quad is grown
	// along both diagonals by plane_tol so thin quads stay grabbable.
	for (int p = 0; p < 3; p++) {
		if (!axis_ok[GIZMO_PLANE_AXES[p][0]] || !axis_ok[GIZMO_PLANE_AXES[p][1]]) {
			continue;
		}
		Vector2 pa = so + (axis_end[GIZMO_PLANE_AXES[p][0]] - so) * GIZMO_PLANE_EXTENT;
		Vector2 pb = so + (axis_end[GIZMO_PLANE_AXES[p][1]] - so) * GIZMO_PLANE_EXTENT;
		Vector2 corner = pa + (pb - so);
		Vector<Vector2> quad;
		quad.push_back(so);
		quad.push_back(pa);
		quad.push_back(corner);
		quad.push_back(pb);
		if (Geometry2D::is_point_in_polygon(p_screen, quad)) {
			return GIZMO_XY + p;
		}
		// Grown quad: push each corner out along its diagonal direction.
		Vector2 center = (so + corner) * 0.5;
		Vector<Vector2> grown;
		for (int i = 0; i < 4; i++) {
			Vector2 d = quad[i] - center;
			real_t len = d.length();
			grown.push_back((len > 0) ? quad[i] + d / len * plane_tol : quad[i]);
		}
		if (Geometry2D::is_point_in_polygon(p_screen, grown)) {
			return GIZMO_XY + p;
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
	gizmo_scale_last_factors = Vector3(1, 1, 1);
	if (selection_target == TARGET_MESH) {
		gizmo_drag_original_positions.clear();
		gizmo_dup_sources.clear();
		if (gizmo_duplicate_drag) {
			// Shift+drag: duplicate every selected brush into a new sibling node
			// and drag the copies; the originals stay put.
			Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
			LevelBrush *old_primary = selected_brush;
			Vector<LevelBrush *> dupes;
			for (LevelBrush *b : selected_brushes) {
				LevelBrush *copy = b->duplicate_brush();
				copy->set_name(b->get_name());
				copy->set_transform(b->get_transform());
				b->get_parent()->add_child(copy);
				if (root) {
					copy->set_owner(root);
				}
				gizmo_dup_sources[copy] = b;
				dupes.push_back(copy);
				if (b == old_primary) {
					selected_brush = copy;
				}
			}
			selected_brushes = dupes;
			gizmo_drag_start_origin = _get_gizmo_origin(); // Same spot, but derived from the copies.
			_sync_editor_selection();
			_refresh_map();
		}
		for (LevelBrush *b : selected_brushes) {
			gizmo_drag_brush_verts[b] = b->get_vertices_data();
			gizmo_drag_original_positions[b] = b->get_position();
		}
	} else {
		// Element targets: one snapshot per selected brush.
		HashSet<LevelBrush *> brushes;
		switch (selection_target) {
			case TARGET_FACE:
				for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
					brushes.insert(E.key);
				}
				break;
			case TARGET_EDGE:
				for (const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
					brushes.insert(E.key);
				}
				break;
			case TARGET_VERTEX:
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

	// Shift+drag in an element target: extrude the selected elements once,
	// then the drag moves the duplicated geometry (Hammer-style pull).
	gizmo_extrude_cap_faces.clear();
	gizmo_extrude_cap_normals.clear();
	gizmo_extrude_elem_verts.clear();
	gizmo_extrude_orig_verts.clear();
	gizmo_extrude_orig_faces.clear();
	gizmo_extrude_orig_mats.clear();
	gizmo_extrude_moved_verts.clear();
	if (gizmo_extrude_drag) {
		switch (selection_target) {
			case TARGET_FACE: {
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
					Vector<Vector3> cap_normals;
					for (int i = sorted.size() - 1; i >= 0; i--) {
						// Slide direction = the cap's stored normal (same direction
						// extrude_face stubs along).
						const Vector3 n = b->get_face_normal(sorted[i]);
						if (b->extrude_face(sorted[i], 0.001) < 0) {
							continue; // Degenerate face: no extrude, skip its cap.
						}
						caps.push_back(sorted[i]); // extrude_face replaces src with the cap in place.
						cap_normals.push_back(n);
					}
					if (caps.is_empty()) {
						gizmo_extrude_orig_verts.erase(b);
						gizmo_extrude_orig_faces.erase(b);
						gizmo_extrude_orig_mats.erase(b);
						continue;
					}
					gizmo_extrude_cap_faces[b] = caps;
					gizmo_extrude_cap_normals[b] = cap_normals;
					gizmo_extrude_moved_verts[b] = b->get_vertices_data();

					// Update the selection to the caps so overlays track the extrusion.
					E.value.clear();
					for (int c : caps) {
						E.value.insert(c);
					}
				}
			} break;
			case TARGET_EDGE: {
				for (KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
					LevelBrush *b = E.key;
					gizmo_extrude_orig_verts[b] = b->get_vertices_data();
					gizmo_extrude_orig_faces[b] = b->get_faces_data();
					gizmo_extrude_orig_mats[b] = b->get_face_materials_data();

					Vector<int> new_verts;
					HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> new_edges;
					for (const LevelBrush::EdgeKey &e : E.value) {
						// Stub offset along the average normal of the edge's faces,
						// so the stub wall is never degenerate; the drag replaces it.
						Vector3 dir;
						for (int f = 0; f < b->get_face_count(); f++) {
							LocalVector<int> loop = b->get_face(f);
							bool has_a = false, has_bv = false;
							for (int idx : loop) {
								has_a = has_a || idx == e.a;
								has_bv = has_bv || idx == e.b;
							}
							if (has_a && has_bv) {
								dir += b->get_face_normal(f);
							}
						}
						if (dir.is_zero_approx()) {
							dir = Vector3(0, 1, 0);
						}
						int ids[2];
						if (b->extrude_edge(e, dir.normalized() * 0.001, ids)) {
							new_verts.push_back(ids[0]);
							new_verts.push_back(ids[1]);
							LevelBrush::EdgeKey ne;
							ne.a = ids[0];
							ne.b = ids[1];
							new_edges.insert(ne);
						}
					}
					gizmo_extrude_elem_verts[b] = new_verts;
					gizmo_extrude_moved_verts[b] = b->get_vertices_data();
					E.value = new_edges; // Selection tracks the duplicated edges.
				}
			} break;
			case TARGET_VERTEX: {
				for (KeyValue<LevelBrush *, HashSet<int>> &E : selected_vertices) {
					LevelBrush *b = E.key;
					gizmo_extrude_orig_verts[b] = b->get_vertices_data();
					gizmo_extrude_orig_faces[b] = b->get_faces_data();
					gizmo_extrude_orig_mats[b] = b->get_face_materials_data();

					Vector<int> new_verts;
					HashSet<int> new_sel;
					for (int v : E.value) {
						// Stub along the average normal of the vertex's faces.
						Vector3 dir;
						for (int f = 0; f < b->get_face_count(); f++) {
							LocalVector<int> loop = b->get_face(f);
							for (int idx : loop) {
								if (idx == v) {
									dir += b->get_face_normal(f);
									break;
								}
							}
						}
						if (dir.is_zero_approx()) {
							dir = Vector3(0, 1, 0);
						}
						int nv = b->extrude_vertex(v, dir.normalized() * 0.001);
						if (nv >= 0) {
							new_verts.push_back(nv);
							new_sel.insert(nv);
						}
					}
					gizmo_extrude_elem_verts[b] = new_verts;
					gizmo_extrude_moved_verts[b] = b->get_vertices_data();
					E.value = new_sel; // Selection tracks the duplicated verts.
				}
			} break;
			default:
				break;
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
		if (tool == TOOL_SCALE) {
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
		if (tool == TOOL_SCALE) {
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
	// EXTRUDE drags (Shift+drag) always go through _apply_gizmo_delta: the
	// scale paths restore the PRE-extrude vertex snapshot, which shrinks the
	// brush below the extruded faces' vert indices (bake crash, GOTCHAS #32).
	if (tool == TOOL_SCALE && !gizmo_extrude_drag) {
		_apply_gizmo_scale(delta);
		_update_overlays();
		return;
	}

	_apply_gizmo_delta(delta);
	_update_overlays();
}

// ---- Rotate gizmo ------------------------------------------------------------

// Radius in pixels of the rotate rings on screen (before EDSCALE; applied
// per use - EDSCALE isn't safe to call at static-init time).
static const real_t ROTATE_RING_PX = 64.0;

// World-space ring radius that projects to ROTATE_RING_PX pixels at the
// gizmo origin. Shared by pick + draw so they always agree.
real_t LevelEditorScreen::_rotate_world_radius(LevelEditorViewport *p_vp, const Vector3 &p_origin, const Vector2 &p_center) const {
	Camera3D *cam = p_vp->get_camera();
	Vector3 test = p_origin + Vector3(1, 0, 0);
	if (!cam->is_position_behind(test)) {
		real_t px = cam->unproject_position(test).distance_to(p_center);
		if (px > 0.001) {
			return ROTATE_RING_PX * EDSCALE / px;
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
	if (!_has_selection()) {
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
	if (tool != TOOL_ROTATE || !_has_selection()) {
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
	if (rotate_drag_axis < 0) {
		return;
	}
	const int axis = rotate_drag_axis;
	rotate_drag_axis = -1;

	_commit_brush_verts_undo(selection_target == TARGET_MESH ? TTR("Rotate Brush") : TTR("Rotate Brush Elements"), gizmo_drag_brush_verts);
	// Remember for Replay Action (Shift+G): mesh-target rotations repeat
	// around each brush's own center (same rule as the drag).
	if (selection_target == TARGET_MESH && !Math::is_zero_approx(rotate_drag_last_angle)) {
		_record_replay_action(ReplayAction::KIND_ROTATE, Vector3(), axis, rotate_drag_last_angle);
	}
	rotate_drag_last_angle = 0.0;
	gizmo_drag_brush_verts.clear();
}

void LevelEditorScreen::_apply_gizmo_rotate(int p_axis, real_t p_angle) {
	Vector3 axis;
	axis[p_axis] = 1.0;

	if (selection_target == TARGET_MESH) {
		// Each selected brush rotates around its OWN center (individual
		// origins), absolute per drag - restore the snapshot, then rotate.
		Basis rot(axis, p_angle);
		for (KeyValue<LevelBrush *, PackedVector3Array> &E : gizmo_drag_brush_verts) {
			LevelBrush *brush = E.key;
			brush->set_vertices_data(E.value);
			_brush_transform_verts(brush, brush->get_center(), rot);
		}
		_refresh_map();
		return;
	}

	// Element targets: rotate each selected brush's vertex subset around the
	// shared selection pivot (world axis through the gizmo origin).
	Vector3 pivot = gizmo_drag_start_origin;
	Basis world_rot(axis, p_angle);
	for (KeyValue<LevelBrush *, PackedVector3Array> &E : gizmo_drag_brush_verts) {
		LevelBrush *brush = E.key;
		Transform3D gt = brush->get_global_transform();
		Transform3D inv = gt.affine_inverse();

		brush->set_vertices_data(E.value);
		Vector<int> indices = _get_gizmo_vertex_indices(brush);
		for (int idx : indices) {
			Vector3 v = gt.xform(brush->get_vertex(idx));
			v = pivot + world_rot.xform(v - pivot);
			brush->set_vertex(idx, inv.xform(v));
		}
	}
	_refresh_map();
}

void LevelEditorScreen::_apply_gizmo_scale(const Vector3 &p_world_delta) {
	if (selection_target != TARGET_MESH) {
		// Element targets: per-axis scale of the selected vertices around the
		// drag-start selection pivot, snapped to the world grid (same rule as
		// whole-brush scale).
		const real_t SCALE_RATE = 0.25; // 4 world units of drag = 2x scale.
		Vector3 factors(1, 1, 1);
		if (gizmo_drag_part == GIZMO_XY || gizmo_drag_part == GIZMO_XZ || gizmo_drag_part == GIZMO_YZ) {
			// Center/plane drag: uniform scale by the largest dragged component.
			real_t f = 1.0 + MAX(p_world_delta.x, MAX(p_world_delta.y, p_world_delta.z)) * SCALE_RATE;
			factors = Vector3(f, f, f);
		} else if (gizmo_drag_part >= GIZMO_X && gizmo_drag_part <= GIZMO_Z) {
			factors[gizmo_drag_part] = 1.0 + p_world_delta[gizmo_drag_part] * SCALE_RATE;
		}
		factors.x = MAX(factors.x, 0.01);
		factors.y = MAX(factors.y, 0.01);
		factors.z = MAX(factors.z, 0.01);

		Vector3 pivot = gizmo_drag_start_origin;
		for (KeyValue<LevelBrush *, PackedVector3Array> &E : gizmo_drag_brush_verts) {
			LevelBrush *brush = E.key;
			Transform3D gt = brush->get_global_transform();
			Transform3D inv = gt.affine_inverse();
			brush->set_vertices_data(E.value);
			Vector<int> indices = _get_gizmo_vertex_indices(brush);
			for (int idx : indices) {
				Vector3 v = gt.xform(brush->get_vertex(idx));
				Vector3 rel = v - pivot;
				v = _snap(pivot + Vector3(rel.x * factors.x, rel.y * factors.y, rel.z * factors.z));
				brush->set_vertex(idx, inv.xform(v));
			}
		}
		_refresh_map();
		return;
	}
	// Mesh target: ONE per-axis factor set for all selected brushes, snapped
	// against the PRIMARY brush's size (Hammer-style group scale). Each
	// brush scales around its OWN AABB center; only the primary's scaled
	// edges land exactly on grid-size multiples.
	const Vector3 factors = _compute_mesh_scale_factors(p_world_delta);
	gizmo_scale_last_factors = factors; // Remember for Replay Action recording.
	_apply_mesh_scale_factors(factors);
}

Vector3 LevelEditorScreen::_compute_mesh_scale_factors(const Vector3 &p_world_delta) const {
	if (!selected_brush || !gizmo_drag_brush_verts.has(selected_brush)) {
		return Vector3(1, 1, 1);
	}
	const PackedVector3Array &primary_orig = gizmo_drag_brush_verts[selected_brush];
	const AABB pbb = LevelHelpers::aabb_from_points(primary_orig);
	const Transform3D primary_inv = selected_brush->get_global_transform().affine_inverse();
	const Vector3 primary_delta = primary_inv.basis.xform(p_world_delta);

	const real_t SCALE_RATE = 0.25; // 4 world units of drag = 2x scale.
	Vector3 factors(1, 1, 1);
	if (gizmo_drag_part == GIZMO_XY || gizmo_drag_part == GIZMO_XZ || gizmo_drag_part == GIZMO_YZ) {
		// Center/plane drag: uniform scale by the largest dragged component,
		// snapped against the primary's largest extent.
		real_t f = 1.0 + MAX(primary_delta.x, MAX(primary_delta.y, primary_delta.z)) * SCALE_RATE;
		f = MAX(f, 0.01);
		int ref_axis = 0;
		for (int axis = 1; axis < 3; axis++) {
			if (pbb.size[axis] > pbb.size[ref_axis]) {
				ref_axis = axis;
			}
		}
		if (pbb.size[ref_axis] > CMP_EPSILON) {
			f = MAX(_snap(pbb.size[ref_axis] * f), grid_size) / pbb.size[ref_axis];
		}
		factors = Vector3(f, f, f);
	} else if (gizmo_drag_part >= GIZMO_X && gizmo_drag_part <= GIZMO_Z) {
		const int axis = gizmo_drag_part;
		real_t f = MAX(1.0 + primary_delta[axis] * SCALE_RATE, 0.01);
		if (pbb.size[axis] > CMP_EPSILON) {
			f = MAX(_snap(pbb.size[axis] * f), grid_size) / pbb.size[axis];
		}
		factors[axis] = f;
	}
	return factors;
}

void LevelEditorScreen::_apply_mesh_scale_factors(const Vector3 &p_factors) {
	for (KeyValue<LevelBrush *, PackedVector3Array> &E : gizmo_drag_brush_verts) {
		LevelBrush *brush = E.key;
		brush->set_vertices_data(E.value);

		const AABB bb = LevelHelpers::aabb_from_points(E.value);
		const Vector3 center = bb.get_center();
		_brush_transform_verts(brush, center, Basis::from_scale(p_factors));
	}
	_refresh_map();
}

void LevelEditorScreen::_brush_transform_verts(LevelBrush *p_brush, const Vector3 &p_center, const Basis &p_basis) {
	for (int i = 0; i < p_brush->get_vertex_count(); i++) {
		Vector3 v = p_brush->get_vertex(i);
		v = p_center + p_basis.xform(v - p_center);
		p_brush->set_vertex(i, v);
	}
}

void LevelEditorScreen::_brushes_transform_own_center(const Basis &p_basis) {
	// Mesh-target rule: each selected brush transforms around its OWN center.
	for (LevelBrush *b : selected_brushes) {
		if (b->is_inside_tree()) {
			_brush_transform_verts(b, b->get_center(), p_basis);
		}
	}
	_refresh_map();
}

void LevelEditorScreen::_apply_gizmo_delta(const Vector3 &p_world_delta) {
	if (!_has_selection()) {
		return;
	}

	if (selection_target == TARGET_MESH && tool == TOOL_MOVE) {
		// Move ALL selected brush nodes by the same delta, converted into
		// parent (map) space and applied on top of the drag-start positions.
		for (const KeyValue<LevelBrush *, Vector3> &E : gizmo_drag_original_positions) {
			LevelBrush *b = E.key;
			if (!b->is_inside_tree()) {
				continue;
			}
			Vector3 local = p_world_delta;
			Node3D *parent = Object::cast_to<Node3D>(b->get_parent());
			if (parent) {
				local = parent->get_global_transform().affine_inverse().basis.xform(p_world_delta);
			}
			b->set_position(E.value + local);
		}
		_refresh_map();
		return;
	}

	if (gizmo_extrude_drag) {
		// Extrude drag: reset to the post-extrude topology, then offset the
		// duplicated geometry by the delta (face caps slide along their own
		// normals; edges/verts move freely).
		if (selection_target == TARGET_FACE) {
			for (KeyValue<LevelBrush *, Vector<int>> &E : gizmo_extrude_cap_faces) {
				LevelBrush *brush = E.key;
				brush->set_vertices_data(gizmo_extrude_moved_verts[brush]);

				Transform3D inv = brush->get_global_transform().affine_inverse();
				Vector3 local_delta = inv.basis.xform(p_world_delta);

				// All caps share ONE signed amount: the drag projected on a
				// reference normal (the last cap = lowest face index, the +axis
				// face for box topology). Each cap then slides along its OWN
				// normal by that amount - opposing faces both move outward
				// together instead of one going inward (per-face projection
				// made an opposing pair slide the same world direction).
				// The amount snaps to the grid (Hammer extrudes in grid steps).
				const Vector<Vector3> &normals = gizmo_extrude_cap_normals[brush];
				const real_t amount = _snap(local_delta.dot(normals[normals.size() - 1]));
				for (int ci = 0; ci < E.value.size(); ci++) {
					const Vector3 &n = normals[ci];
					Vector<int> loop_verts;
					LocalVector<int> loop = brush->get_face(E.value[ci]);
					for (int idx : loop) {
						loop_verts.push_back(idx);
					}
					brush->move_vertices(loop_verts, n * amount);
				}

				// The side walls were wound at begin-drag from the 0.001 stub;
				// the drag can move the cap to the opposite side of the source
				// plane, flipping which way the walls face (GOTCHAS #30). Re-wind
				// them against the current geometry. Walls are the faces appended
				// after the pre-extrude topology.
				const int first_wall = gizmo_extrude_orig_faces[brush].size();
				for (int f = first_wall; f < brush->get_face_count(); f++) {
					brush->rewind_face_outward(f);
				}
			}
		} else {
			for (KeyValue<LevelBrush *, Vector<int>> &E : gizmo_extrude_elem_verts) {
				LevelBrush *brush = E.key;
				brush->set_vertices_data(gizmo_extrude_moved_verts[brush]);

				Transform3D inv = brush->get_global_transform().affine_inverse();
				Vector3 local_delta = _snap(inv.basis.xform(p_world_delta));
				brush->move_vertices(E.value, local_delta);

				// The extruded walls were wound at begin-drag from a stub offset;
				// the drag rotates their planes, which can flip which side faces
				// out (GOTCHAS #30). Re-wind them against the current geometry.
				// Walls are the faces appended after the pre-extrude topology.
				const int first_wall = gizmo_extrude_orig_faces[brush].size();
				for (int f = first_wall; f < brush->get_face_count(); f++) {
					brush->rewind_face_outward(f);
				}
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
	gizmo_drag_part = GIZMO_NONE; // Clear the active highlight (draw treats drag_part as hot).
	_update_overlays();

	if (!_has_selection()) {
		return;
	}

	// Move tool + Mesh target + Shift: the drag moved live duplicates - commit
	// their creation (undo removes them and reselects the sources).
	if (selection_target == TARGET_MESH && tool == TOOL_MOVE && gizmo_duplicate_drag) {
		gizmo_duplicate_drag = false;
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
		undo_redo->create_action(TTR("Duplicate Brush"));
		TypedArray<Node> sources;
		TypedArray<Node> copies;
		for (const KeyValue<LevelBrush *, LevelBrush *> &E : gizmo_dup_sources) {
			LevelBrush *copy = E.key;
			Node *parent = E.value->get_parent(); // Source's parent = copy's intended parent.
			if (!parent) {
				continue;
			}
			sources.push_back(E.value);
			copies.push_back(copy);
			undo_redo->add_do_method(parent, "add_child", copy);
			if (root) {
				undo_redo->add_do_method(copy, "set_owner", root);
			}
			undo_redo->add_undo_method(parent, "remove_child", copy);
			undo_redo->add_do_reference(copy); // Keep the node alive across undo.
		}
		undo_redo->add_do_method(current_map, "refresh");
		undo_redo->add_undo_method(current_map, "refresh");
		undo_redo->add_undo_method(this, "set_brush_selection", sources);
		undo_redo->add_do_method(this, "set_brush_selection", copies);
		undo_redo->commit_action(false);

		// Remember for Replay Action (Shift+G): total world offset of the drag.
		_record_replay_action(ReplayAction::KIND_DUPLICATE_DRAG, _get_gizmo_origin() - gizmo_drag_start_origin);

		gizmo_dup_sources.clear();
		gizmo_drag_original_positions.clear();
		gizmo_drag_brush_verts.clear();
		return;
	}

	// Move tool + Mesh target: one undo action across all moved brushes.
	if (selection_target == TARGET_MESH && tool == TOOL_MOVE) {
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		bool created = false;
		for (const KeyValue<LevelBrush *, Vector3> &E : gizmo_drag_original_positions) {
			LevelBrush *b = E.key;
			if (!b->is_inside_tree()) {
				continue;
			}
			const Vector3 new_pos = b->get_position();
			if (!new_pos.is_equal_approx(E.value)) {
				if (!created) {
					undo_redo->create_action(TTR("Move Brush"));
					created = true;
				}
				undo_redo->add_do_property(b, "position", new_pos);
				undo_redo->add_undo_property(b, "position", E.value);
			}
		}
		if (created) {
			undo_redo->commit_action(false);
			// Remember for Replay Action (Shift+G): total world offset of the drag.
			_record_replay_action(ReplayAction::KIND_MOVE, _get_gizmo_origin() - gizmo_drag_start_origin);
		}
		gizmo_drag_original_positions.clear();
		return;
	}

	// Scale tool + Mesh target: one undo action across all scaled brushes.
	if (tool == TOOL_SCALE && selection_target == TARGET_MESH) {
		_commit_brush_verts_undo(TTR("Scale Brush"), gizmo_drag_brush_verts);
		// Remember for Replay Action (Shift+G): factors captured during the drag
		// (gizmo_drag_part is already NONE here, so recompute would be identity).
		if (!gizmo_scale_last_factors.is_equal_approx(Vector3(1, 1, 1))) {
			_record_replay_action(ReplayAction::KIND_SCALE, Vector3(), 0, 0.0, gizmo_scale_last_factors);
		}
		gizmo_drag_brush_verts.clear();
		return;
	}

	if (gizmo_extrude_drag) {
		gizmo_extrude_drag = false;
		// Commit the extrusion (topology change at drag start + cap pull) as a
		// single undo action, recorded against the pre-extrude snapshots.
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action(selection_target == TARGET_FACE ? TTR("Extrude Faces") : (selection_target == TARGET_EDGE ? TTR("Extrude Edges") : TTR("Extrude Vertices")));
		bool any = false;
		for (KeyValue<LevelBrush *, PackedVector3Array> &E : gizmo_extrude_orig_verts) {
			any = true;
			_add_brush_undo_pair(undo_redo, E.key, E.value, gizmo_extrude_orig_faces[E.key], gizmo_extrude_orig_mats[E.key]);
		}
		if (any) {
			undo_redo->add_do_method(current_map, "refresh");
			undo_redo->add_undo_method(current_map, "refresh");
			// The selection was remapped to the NEW geometry during the drag
			// (indices that don't exist in the pre-extrude snapshot). Undoing
			// would leave those stale indices in the selection (out-of-bounds
			// in every draw/pick) - clear it on undo.
			undo_redo->add_undo_method(this, "clear_selection");
			undo_redo->commit_action(false);
		}
		gizmo_extrude_orig_verts.clear();
		gizmo_extrude_orig_faces.clear();
		gizmo_extrude_orig_mats.clear();
		gizmo_extrude_cap_faces.clear();
		gizmo_extrude_cap_normals.clear();
		gizmo_extrude_elem_verts.clear();
		gizmo_extrude_moved_verts.clear();
		gizmo_drag_brush_verts.clear();
		return;
	}

	if (gizmo_drag_brush_verts.is_empty()) {
		return;
	}

	// Commit the element transform as one undo action across all dragged
	// brushes. (Rotate has its own ring-gizmo drag path and never reaches here.)
	_commit_brush_verts_undo(tool == TOOL_SCALE ? TTR("Scale Brush Elements") : TTR("Move Brush Element"), gizmo_drag_brush_verts);
	gizmo_drag_brush_verts.clear();
}

void LevelEditorScreen::_draw_gizmo(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (_is_drawing_tool() || tool == TOOL_SELECT || tool == TOOL_ROTATE || !_has_selection()) {
		return; // No arrow gizmo in the drawing tools, Select, or Rotate tool.
	}
	const Vector3 origin = _get_gizmo_origin();
	Camera3D *cam = p_vp->get_camera();
	Vector2 so;
	Vector2 axis_end[3];
	bool axis_ok[3];
	if (!compute_gizmo_axes(cam, origin, so, axis_end, axis_ok)) {
		return;
	}

	Color axis_col[3] = { LevelEditorColors::GIZMO_AXIS_X, LevelEditorColors::GIZMO_AXIS_Y, LevelEditorColors::GIZMO_AXIS_Z };

	// Plane handles (quads at halfway between axes).
	for (int p = 0; p < 3; p++) {
		if (!axis_ok[GIZMO_PLANE_AXES[p][0]] || !axis_ok[GIZMO_PLANE_AXES[p][1]]) {
			continue;
		}
		Vector2 pa = so + (axis_end[GIZMO_PLANE_AXES[p][0]] - so) * GIZMO_PLANE_EXTENT;
		Vector2 pb = so + (axis_end[GIZMO_PLANE_AXES[p][1]] - so) * GIZMO_PLANE_EXTENT;
		PackedVector2Array quad;
		quad.push_back(so);
		quad.push_back(pa);
		quad.push_back(pa + (pb - so));
		quad.push_back(pb);
		Color c = axis_col[GIZMO_PLANE_AXES[p][0]].lerp(axis_col[GIZMO_PLANE_AXES[p][1]], 0.5);
		c.a = (gizmo_hover == (GizmoPart)(GIZMO_XY + p) || gizmo_drag_part == (GizmoPart)(GIZMO_XY + p)) ? 0.55 : 0.22;
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
		if (tool == TOOL_SCALE) {
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
	if (tool != TOOL_ROTATE || !_has_selection()) {
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
				if (selection_target == TARGET_MESH) {
					gizmo_drag_brush_verts.clear();
					for (LevelBrush *b : selected_brushes) {
						gizmo_drag_brush_verts[b] = b->get_vertices_data();
					}
				} else {
					// Element targets: snapshot every selected brush, and the drag
					// pivot (selection center) for world-axis rotation.
					gizmo_drag_start_origin = _get_gizmo_origin();
					gizmo_drag_brush_verts.clear();
					HashSet<LevelBrush *> brushes;
					switch (selection_target) {
						case TARGET_FACE:
							for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
								brushes.insert(E.key);
							}
							break;
						case TARGET_EDGE:
							for (const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
								brushes.insert(E.key);
							}
							break;
						case TARGET_VERTEX:
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
			rotate_drag_last_angle = delta;
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
	// Translate/scale gizmo (Move/Scale tools, any selection target).
	if (_is_drawing_tool() || tool == TOOL_SELECT || tool == TOOL_ROTATE || !_has_selection()) {
		return false;
	}
	Ref<InputEventMouseButton> mb = p_event;
	Ref<InputEventMouseMotion> mm = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			int part = _pick_gizmo(p_camera, mb->get_position());
			if (part != GIZMO_NONE) {
				gizmo_drag_part = (GizmoPart)part;
				gizmo_extrude_drag = (_is_element_target() && mb->is_shift_pressed());
				// Mesh target: Shift+drag duplicates the brushes and drags the copies.
				gizmo_duplicate_drag = (selection_target == TARGET_MESH && tool == TOOL_MOVE && mb->is_shift_pressed());
				_gizmo_begin_drag(p_vp, mb->get_position());
				return true; // Consumed by gizmo.
			}
			// Off-gizmo clicks fall through to _selection_input in EVERY tool -
			// the Scale tool's old "click anywhere to uniform-scale" swallow
			// made already-selected brushes un-reselectable (GOTCHAS #31).
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
