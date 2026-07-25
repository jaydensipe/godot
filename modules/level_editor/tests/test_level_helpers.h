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

#include "../editor/level_helpers.h"

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

} // namespace TestLevelHelpers
