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
	{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
	{ 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
	{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
};

// Outward direction per face handle index (0..5: -x, +x, -y, +y, -z, +z).
inline const Vector3 AABB_FACE_DIRS[6] = {
	Vector3(-1, 0, 0), Vector3(1, 0, 0),
	Vector3(0, -1, 0), Vector3(0, 1, 0),
	Vector3(0, 0, -1), Vector3(0, 0, 1),
};

inline Vector3 aabb_face_center(const AABB &p_aabb, int p_face) {
	const Vector3 c = p_aabb.get_center();
	const Vector3 hs = p_aabb.size * 0.5;
	return c + AABB_FACE_DIRS[p_face] * Vector3(hs.x, hs.y, hs.z);
}

} // namespace LevelHelpers
