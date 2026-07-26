/**************************************************************************/
/*  level_helpers.h                                                       */
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

#pragma once

#include "core/math/aabb.h"
#include "core/math/plane.h"
#include "core/variant/variant.h"

// Shared box helpers for the level editor (ghost block, select handles,
// drag feedback). Corner indexing is a bitmask: x|y|z (bit 0/1/2).
namespace LevelHelpers {

inline void aabb_corners(const AABB &p_aabb, Vector3 r_corners[8]) {
	const Vector3 c = p_aabb.get_center();
	const Vector3 hs = p_aabb.size * 0.5;
	for (int i = 0; i < 8; i++) {
		r_corners[i] = c + Vector3((i & 1) ? hs.x : -hs.x, (i & 2) ? hs.y : -hs.y, (i & 4) ? hs.z : -hs.z);
	}
}

// The 12 edges of a box as index pairs into aabb_corners().
inline constexpr int AABB_EDGE_IDX[12][2] = {
	{ 0, 1 },
	{ 1, 3 },
	{ 3, 2 },
	{ 2, 0 },
	{ 4, 5 },
	{ 5, 7 },
	{ 7, 6 },
	{ 6, 4 },
	{ 0, 4 },
	{ 1, 5 },
	{ 2, 6 },
	{ 3, 7 },
};

// Outward direction per face handle index (0..5: -x, +x, -y, +y, -z, +z).
inline const Vector3 AABB_FACE_DIRS[6] = {
	Vector3(-1, 0, 0),
	Vector3(1, 0, 0),
	Vector3(0, -1, 0),
	Vector3(0, 1, 0),
	Vector3(0, 0, -1),
	Vector3(0, 0, 1),
};

inline Vector3 aabb_face_center(const AABB &p_aabb, int p_face) {
	const Vector3 c = p_aabb.get_center();
	const Vector3 hs = p_aabb.size * 0.5;
	return c + AABB_FACE_DIRS[p_face] * Vector3(hs.x, hs.y, hs.z);
}

// AABB enclosing a vertex array (empty for 0 points).
inline AABB aabb_from_points(const PackedVector3Array &p_points) {
	AABB bb;
	for (int i = 0; i < p_points.size(); i++) {
		if (i == 0) {
			bb.position = p_points[0];
		} else {
			bb.expand_to(p_points[i]);
		}
	}
	return bb;
}

// Plane containing p_axis through p_point, oriented to face p_cam_pos as
// directly as possible (most stable ray picking). Used by box-handle and
// gizmo drags so movement works along axes parallel to the view plane (e.g.
// dragging up/down in the top view). Falls back to any perpendicular normal
// when the camera looks straight down the axis.
inline Plane axis_drag_plane(const Vector3 &p_point, int p_axis, const Vector3 &p_cam_pos) {
	Vector3 axis;
	axis[p_axis] = 1.0;

	Vector3 to_cam = p_cam_pos - p_point;
	Vector3 n = axis.cross(axis.cross(to_cam));
	if (n.length_squared() < CMP_EPSILON) {
		n = axis.cross(Vector3(1, 0, 0));
		if (n.length_squared() < CMP_EPSILON) {
			n = axis.cross(Vector3(0, 1, 0));
		}
	}
	n.normalize();
	return Plane(n, n.dot(p_point));
}

// Closest point on the line (p_line_point, p_line_dir) to the ray
// (p_ray_origin, p_ray_dir) - the gizmo axis-drag solver. Returns false when
// the two are parallel (degenerate, no unique closest point).
inline bool closest_point_on_line_to_ray(const Vector3 &p_line_point, const Vector3 &p_line_dir, const Vector3 &p_ray_origin, const Vector3 &p_ray_dir, Vector3 &r_point) {
	const Vector3 &d1 = p_line_dir;
	const Vector3 &d2 = p_ray_dir;
	Vector3 r = p_line_point - p_ray_origin;
	real_t a = d1.dot(d1);
	real_t b = d1.dot(d2);
	real_t c = d2.dot(d2);
	real_t d = d1.dot(r);
	real_t e = d2.dot(r);
	real_t denom = a * c - b * b;
	if (Math::is_zero_approx(denom)) {
		return false;
	}
	real_t t = (b * e - c * d) / denom;
	r_point = p_line_point + d1 * t;
	return true;
}

} // namespace LevelHelpers
