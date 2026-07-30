/**************************************************************************/
/*  test_level_helpers.h                                                  */
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

#include "../level_helpers.h"

#include "tests/test_macros.h"

namespace TestLevelHelpers {

TEST_CASE("[LevelHelpers] axis_drag_plane contains the axis and the point") {
	// Camera off to the side of the Y axis (top-view-like setup).
	Plane pl = LevelHelpers::axis_drag_plane(Vector3(1, 2, 3), 1, Vector3(10, 2, 3));

	// The plane contains the drag point and is parallel to the Y axis.
	CHECK(Math::is_zero_approx(pl.distance_to(Vector3(1, 2, 3))));
	CHECK(Math::is_zero_approx(pl.distance_to(Vector3(1, 5, 3))));
	CHECK(Math::is_zero_approx(pl.normal.dot(Vector3(0, 1, 0))));

	// It faces the camera as directly as possible: the normal is the negated
	// perpendicular component of the to-camera direction (axis x (axis x v)).
	// Either sign describes the same plane, so check abs alignment.
	Vector3 expected = Vector3(1, 0, 0); // (10,2,3)-(1,2,3) minus its Y part.
	CHECK(pl.normal.abs().is_equal_approx(expected.abs()));
}

TEST_CASE("[LevelHelpers] axis_drag_plane falls back when camera is on the axis") {
	// Camera looks straight down the X axis: to_cam is parallel, double-cross
	// degenerates, and any perpendicular normal is acceptable.
	Plane pl = LevelHelpers::axis_drag_plane(Vector3(0, 0, 0), 0, Vector3(5, 0, 0));
	CHECK(Math::is_zero_approx(pl.normal.dot(Vector3(1, 0, 0))));
	CHECK(pl.normal.length() > 0.9);

	// Same for the Y axis, where the X-based fallback is itself degenerate
	// and the Y-based one must kick in.
	Plane pl_y = LevelHelpers::axis_drag_plane(Vector3(0, 0, 0), 1, Vector3(0, 5, 0));
	CHECK(Math::is_zero_approx(pl_y.normal.dot(Vector3(0, 1, 0))));
	CHECK(pl_y.normal.length() > 0.9);
}

TEST_CASE("[LevelHelpers] axis_drag_plane enables vertical drags in the top view") {
	// The vertical-drag bug this solves: the top view's edit plane is XZ, so
	// a ray pointing mostly down can never move a handle along Y. The axis
	// plane for Y must be intersectable by that ray (mouse rays in practice
	// always have a small horizontal component).
	Plane pl = LevelHelpers::axis_drag_plane(Vector3(0, 1, 0), 1, Vector3(4, 8, 0));

	Vector3 hit;
	// Ray from above, pointing down and slightly sideways (top ortho view).
	REQUIRE(pl.intersects_ray(Vector3(2, 10, 0), Vector3(-0.2, -1, 0).normalized(), &hit));
	// The hit's Y is what the drag reads - it must be free to vary.
	CHECK(hit.y > 0.0);
	CHECK(Math::is_zero_approx(pl.distance_to(hit)));
}

TEST_CASE("[LevelHelpers] closest_point_on_line_to_ray solves the axis drag") {
	// Y axis line through the origin; mouse ray from the side aimed at y=3.
	Vector3 point;
	REQUIRE(LevelHelpers::closest_point_on_line_to_ray(
			Vector3(0, 0, 0), Vector3(0, 1, 0),
			Vector3(5, 3, 0), Vector3(-1, 0, 0), point));
	CHECK(point.is_equal_approx(Vector3(0, 3, 0)));
}

TEST_CASE("[LevelHelpers] closest_point_on_line_to_ray works looking down the axis") {
	// Camera roughly above, ray near-parallel to the Y axis - the solver must
	// still return the closest approach instead of failing.
	Vector3 point;
	REQUIRE(LevelHelpers::closest_point_on_line_to_ray(
			Vector3(1, 0, 1), Vector3(0, 1, 0),
			Vector3(2, 10, 1), Vector3(-0.05, -1, 0).normalized(), point));
	CHECK(Math::abs(point.x - 1.0) < 0.01);
	CHECK(Math::abs(point.z - 1.0) < 0.01);
	// The closest approach may sit behind the line origin (negative
	// parameter) - only the x/z projection matters for the drag.
}

TEST_CASE("[LevelHelpers] closest_point_on_line_to_ray rejects parallel lines") {
	Vector3 point;
	CHECK(!LevelHelpers::closest_point_on_line_to_ray(
			Vector3(0, 0, 0), Vector3(0, 1, 0),
			Vector3(3, 0, 0), Vector3(0, 1, 0), point));
}

TEST_CASE("[LevelHelpers] closest_point_on_segment_2d clamps to the segment") {
	// Projection inside the segment.
	CHECK(LevelHelpers::closest_point_on_segment_2d(Vector2(0, 0), Vector2(10, 0), Vector2(4, 3)).is_equal_approx(Vector2(4, 0)));
	// Beyond each endpoint clamps to the endpoint.
	CHECK(LevelHelpers::closest_point_on_segment_2d(Vector2(0, 0), Vector2(10, 0), Vector2(-5, 3)).is_equal_approx(Vector2(0, 0)));
	CHECK(LevelHelpers::closest_point_on_segment_2d(Vector2(0, 0), Vector2(10, 0), Vector2(99, -2)).is_equal_approx(Vector2(10, 0)));
	// Degenerate segment resolves to the point itself.
	CHECK(LevelHelpers::closest_point_on_segment_2d(Vector2(2, 2), Vector2(2, 2), Vector2(7, 7)).is_equal_approx(Vector2(2, 2)));
}

TEST_CASE("[LevelHelpers] clip_segment_to_rect clips against each slab") {
	real_t t0 = 0, t1 = 0;
	const Rect2 rect(0, 0, 100, 100);

	// Fully inside: span is the whole segment.
	REQUIRE(LevelHelpers::clip_segment_to_rect(Vector2(10, 10), Vector2(90, 90), rect, t0, t1));
	CHECK(Math::is_zero_approx(t0));
	CHECK(Math::is_equal_approx(t1, (Vector2(90, 90) - Vector2(10, 10)).length()));

	// Crossing horizontally: clipped to x in [0, 100].
	REQUIRE(LevelHelpers::clip_segment_to_rect(Vector2(-50, 50), Vector2(150, 50), rect, t0, t1));
	CHECK(Math::is_equal_approx(t0, (real_t)50.0));
	CHECK(Math::is_equal_approx(t1, (real_t)150.0));

	// Diagonal entering at the bottom-left corner region: direction (200,-200),
	// enters at (0,100) and exits at (100,0) - both at param distance
	// 100*sqrt(2) and 200*sqrt(2) along the segment.
	REQUIRE(LevelHelpers::clip_segment_to_rect(Vector2(-100, 200), Vector2(100, 0), rect, t0, t1));
	CHECK(Math::is_equal_approx(t0, (real_t)(100.0 * Math::SQRT2)));
	CHECK(Math::is_equal_approx(t1, (real_t)(200.0 * Math::SQRT2)));

	// Fully outside (parallel to a slab): rejected.
	CHECK(!LevelHelpers::clip_segment_to_rect(Vector2(-10, 200), Vector2(50, 200), rect, t0, t1));
	// Passing below-left of the corner (x slab and y slab never overlap):
	// from (-150,-100) to (150,-50) stays below y=0 while crossing x: rejected.
	CHECK(!LevelHelpers::clip_segment_to_rect(Vector2(-150, -100), Vector2(150, -50), rect, t0, t1));
	// Degenerate segment: rejected.
	CHECK(!LevelHelpers::clip_segment_to_rect(Vector2(50, 50), Vector2(50, 50), rect, t0, t1));
}

TEST_CASE("[LevelHelpers] aabb_corners uses the x|y|z bitmask layout") {
	Vector3 c[8];
	LevelHelpers::aabb_corners(AABB(Vector3(0, 0, 0), Vector3(2, 4, 6)), c);
	CHECK(c[0].is_equal_approx(Vector3(0, 0, 0)));
	CHECK(c[1].is_equal_approx(Vector3(2, 0, 0)));
	CHECK(c[2].is_equal_approx(Vector3(0, 4, 0)));
	CHECK(c[3].is_equal_approx(Vector3(2, 4, 0)));
	CHECK(c[7].is_equal_approx(Vector3(2, 4, 6)));
}

TEST_CASE("[LevelHelpers] AABB_EDGE_IDX is a valid box edge table") {
	Vector3 c[8];
	LevelHelpers::aabb_corners(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)), c);
	for (int i = 0; i < 12; i++) {
		// Every edge differs in exactly one axis (unit edge length).
		CHECK(Math::is_equal_approx(c[LevelHelpers::AABB_EDGE_IDX[i][0]].distance_to(c[LevelHelpers::AABB_EDGE_IDX[i][1]]), (real_t)1.0));
	}
	// Every corner index appears in exactly 3 edges.
	int counts[8] = { 0 };
	for (int i = 0; i < 12; i++) {
		counts[LevelHelpers::AABB_EDGE_IDX[i][0]]++;
		counts[LevelHelpers::AABB_EDGE_IDX[i][1]]++;
	}
	for (int i = 0; i < 8; i++) {
		CHECK(counts[i] == 3);
	}
}

TEST_CASE("[LevelHelpers] aabb_face_center matches AABB_FACE_DIRS") {
	const AABB bb(Vector3(0, 0, 0), Vector3(2, 4, 6));
	// Face 1 = +x, face 2 = -y, face 5 = +z.
	CHECK(LevelHelpers::aabb_face_center(bb, 1).is_equal_approx(Vector3(2, 2, 3)));
	CHECK(LevelHelpers::aabb_face_center(bb, 2).is_equal_approx(Vector3(1, 0, 3)));
	CHECK(LevelHelpers::aabb_face_center(bb, 5).is_equal_approx(Vector3(1, 2, 6)));
	// Opposite faces are opposite directions.
	for (int i = 0; i < 3; i++) {
		CHECK(LevelHelpers::AABB_FACE_DIRS[i * 2].is_equal_approx(-LevelHelpers::AABB_FACE_DIRS[i * 2 + 1]));
	}
}

TEST_CASE("[LevelHelpers] aabb_from_points encloses all points") {
	PackedVector3Array pts;
	pts.push_back(Vector3(1, 2, 3));
	pts.push_back(Vector3(-1, 5, 0));
	pts.push_back(Vector3(0, 0, 7));
	AABB bb = LevelHelpers::aabb_from_points(pts);
	CHECK(bb.position.is_equal_approx(Vector3(-1, 0, 0)));
	CHECK(bb.size.is_equal_approx(Vector3(2, 5, 7)));

	// Empty input yields an empty (zero-size) box.
	AABB empty = LevelHelpers::aabb_from_points(PackedVector3Array());
	CHECK(empty.size.is_equal_approx(Vector3()));
}

TEST_CASE("[LevelHelpers] ortho_view_axis maps ortho views to world axes") {
	// LevelEditorViewport::ViewType: PERSPECTIVE=0, TOP=1, FRONT=2, SIDE=3.
	CHECK(LevelHelpers::ortho_view_axis(1) == 1); // TOP looks down +Y.
	CHECK(LevelHelpers::ortho_view_axis(2) == 2); // FRONT looks down +Z.
	CHECK(LevelHelpers::ortho_view_axis(3) == 0); // SIDE looks down +X.
	CHECK(LevelHelpers::ortho_view_axis(0) == -1); // PERSPECTIVE: no fixed axis.
	// Unknown values fall through to -1 (same as perspective).
	CHECK(LevelHelpers::ortho_view_axis(99) == -1);
}

} // namespace TestLevelHelpers
