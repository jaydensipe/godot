/**************************************************************************/
/*  test_level_brush.h                                                    */
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

#pragma once

#include "../level_brush.h"

#include "scene/resources/material.h"
#include "tests/test_macros.h"

namespace TestLevelBrush {

// Expected outward normals of a fresh unit box (face order from setup_box).
static const Vector3 BOX_NORMALS[6] = {
	Vector3(0, 0, 1), // +Z (front)
	Vector3(0, 0, -1), // -Z (back)
	Vector3(1, 0, 0), // +X (right)
	Vector3(-1, 0, 0), // -X (left)
	Vector3(0, 1, 0), // +Y (top)
	Vector3(0, -1, 0), // -Y (bottom)
};

TEST_CASE("[LevelBrush] setup_box produces a valid cube") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	CHECK(brush->get_vertex_count() == 8);
	CHECK(brush->get_face_count() == 6);
	CHECK(brush->is_valid());

	// Every face has 4 verts and the expected outward normal.
	for (int f = 0; f < 6; f++) {
		CHECK(brush->get_face(f).size() == 4);
		CHECK(brush->get_face_normal(f).is_equal_approx(BOX_NORMALS[f]));
	}

	// 12 unique edges on a cube.
	CHECK(brush->get_edges().size() == 12);

	CHECK(brush->get_center().is_equal_approx(Vector3(0.5, 0.5, 0.5)));

	memdelete(brush);
}

TEST_CASE("[LevelBrush] move_vertices only affects the given vertices") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Grab the positions of every other vertex first.
	const int moved = 6; // (1,1,1) corner.
	Vector3 before[8];
	for (int i = 0; i < 8; i++) {
		before[i] = brush->get_vertex(i);
	}
	REQUIRE(before[moved].is_equal_approx(Vector3(1, 1, 1)));

	Vector<int> verts;
	verts.push_back(moved);
	brush->move_vertices(verts, Vector3(0.25, 0.5, -0.75));

	// The moved vertex tracks exactly; every other vertex is untouched.
	CHECK(brush->get_vertex(moved).is_equal_approx(before[moved] + Vector3(0.25, 0.5, -0.75)));
	for (int i = 0; i < 8; i++) {
		if (i != moved) {
			CHECK(brush->get_vertex(i).is_equal_approx(before[i]));
		}
	}

	// The three incident faces have tilted normals; the opposite faces are
	// unchanged.
	CHECK(brush->get_face_normal(4).is_equal_approx(BOX_NORMALS[4]) == false);
	CHECK(brush->get_face_normal(5).is_equal_approx(BOX_NORMALS[5]));

	memdelete(brush);
}

TEST_CASE("[LevelBrush] ray_intersect hits the entry face") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	real_t dist = 0.0;
	// Straight down onto the top face (+Y is face 4).
	int face = brush->ray_intersect(Vector3(0.5, 5.0, 0.5), Vector3(0, -1, 0), dist);
	CHECK(face == 4);
	CHECK(dist == doctest::Approx(4.0));

	// From +Z toward -Z: hits front (face 0).
	face = brush->ray_intersect(Vector3(0.5, 0.5, 5.0), Vector3(0, 0, -1), dist);
	CHECK(face == 0);

	// Ray missing the box entirely.
	face = brush->ray_intersect(Vector3(5.0, 5.0, 5.0), Vector3(1, 0, 0), dist);
	CHECK(face == -1);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] extrude_face creates cap and side walls") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	int cap = brush->extrude_face(4, 1.0); // Top face, up by 1.
	REQUIRE(cap >= 0);

	// 8 original verts + 4 cap verts.
	CHECK(brush->get_vertex_count() == 12);
	// Cap + 5 remaining original faces + 4 side walls.
	CHECK(brush->get_face_count() == 10);

	// The cap sits at y=2 with a +Y normal.
	CHECK(brush->get_face_normal(cap).is_equal_approx(Vector3(0, 1, 0)));
	LocalVector<int> cap_loop = brush->get_face(cap);
	for (int idx : cap_loop) {
		CHECK(brush->get_vertex(idx).y == doctest::Approx(2.0));
	}

	// Brush is still closed: the new side walls face outward.
	// Check one side wall's normal roughly (wall between old top edge and cap).
	bool found_wall = false;
	for (int f = 0; f < brush->get_face_count(); f++) {
		if (f == cap) {
			continue;
		}
		LocalVector<int> loop = brush->get_face(f);
		bool has_low = false, has_high = false;
		for (int idx : loop) {
			if (brush->get_vertex(idx).y == doctest::Approx(1.0)) {
				has_low = true;
			}
			if (brush->get_vertex(idx).y == doctest::Approx(2.0)) {
				has_high = true;
			}
		}
		if (has_low && has_high) {
			found_wall = true;
		}
	}
	CHECK(found_wall);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] clip keeps the front side and caps the cut") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Vertical cut at x=0.5, keep the +X half.
	brush->clip(Plane(Vector3(1, 0, 0), 0.5));

	// All verts REFERENCED by faces satisfy x >= 0.5 (the verts array may
	// keep now-orphaned entries - that's expected).
	HashSet<int> referenced;
	for (int f = 0; f < brush->get_face_count(); f++) {
		LocalVector<int> loop = brush->get_face(f);
		for (int idx : loop) {
			referenced.insert(idx);
		}
	}
	for (int idx : referenced) {
		CHECK(brush->get_vertex(idx).x >= 0.5 - 0.001);
	}
	// A cap face on the cut plane exists (all its verts at x=0.5).
	bool has_cap = false;
	for (int f = 0; f < brush->get_face_count(); f++) {
		LocalVector<int> loop = brush->get_face(f);
		bool all_on_plane = loop.size() >= 3;
		for (int idx : loop) {
			if (Math::abs(brush->get_vertex(idx).x - 0.5) > 0.001) {
				all_on_plane = false;
				break;
			}
		}
		if (all_on_plane) {
			has_cap = true;
			// Cap faces away from the kept half.
			CHECK(brush->get_face_normal(f).is_equal_approx(Vector3(-1, 0, 0)));
		}
	}
	CHECK(has_cap);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] split_faces subdivides without clipping") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	brush->split_faces(Plane(Vector3(1, 0, 0), 0.5));

	// No verts were removed and none moved: the brush is the same solid.
	for (int i = 0; i < 8; i++) {
		const Vector3 &v = brush->get_vertex(i);
		CHECK(v.x >= 0.0 - 0.001);
		CHECK(v.x <= 1.0 + 0.001);
	}

	// The 4 faces parallel to the cut (+Z, -Z, +Y, -Y) split in two; the two
	// faces on the cut normal (+X, -X) are untouched: 4 + 2*... wait, the two
	// axis-aligned side faces (+X/-X) don't cross; the other 4 do.
	CHECK(brush->get_face_count() == 6 + 4);

	// New intersection verts lie on the cut plane.
	int on_plane = 0;
	for (int i = 0; i < brush->get_vertex_count(); i++) {
		if (Math::abs(brush->get_vertex(i).x - 0.5) < 0.001) {
			on_plane++;
		}
	}
	CHECK(on_plane == 4);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] flip_faces inverts every normal") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	REQUIRE(!brush->is_faces_flipped());
	brush->flip_faces();
	CHECK(brush->is_faces_flipped());

	// Geometry (face loops) is unchanged; flipping is a render/bake-time flag.
	CHECK(brush->get_face_count() == 6);
	CHECK(brush->get_face_normal(4).is_equal_approx(Vector3(0, 1, 0)));

	// Bake data is inverted: normals flipped, winding reversed.
	Vector<Vector3> verts, normals;
	Vector<Vector2> uvs;
	brush->get_bake_surface_data(4, verts, normals, uvs);
	REQUIRE(normals.size() == 6); // Quad = 2 triangles.
	for (const Vector3 &n : normals) {
		CHECK(n.is_equal_approx(Vector3(0, -1, 0)));
	}

	brush->flip_faces();
	CHECK(!brush->is_faces_flipped());

	memdelete(brush);
}

TEST_CASE("[LevelBrush] bridge_edges spans a new quad between two edges") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Two parallel top edges: (2-6) is the back top edge... pick edges on
	// opposite sides of the top face: edge (3,2) on -Z top and edge (7,6) on
	// +Z top.
	LevelBrush::EdgeKey a(3, 2);
	LevelBrush::EdgeKey b(7, 6);
	int face = brush->bridge_edges(a, b, Ref<Material>());
	REQUIRE(face >= 0);

	CHECK(brush->get_face(face).size() == 4);
	// New face's normal points outward (up).
	CHECK(brush->get_face_normal(face).y > 0.5);

	// Same edge or shared-vertex edges are rejected.
	CHECK(brush->bridge_edges(a, a, Ref<Material>()) == -1);
	CHECK(brush->bridge_edges(LevelBrush::EdgeKey(0, 1), LevelBrush::EdgeKey(1, 5), Ref<Material>()) == -1);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] delete_faces removes faces and materials stay aligned") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	Ref<Material> mat;
	mat.instantiate();
	brush->set_face_material(5, mat); // Tag the bottom face.

	Vector<int> del;
	del.push_back(4); // Top.
	brush->delete_faces(del);

	CHECK(brush->get_face_count() == 5);
	// The tagged bottom face kept its material (indices shifted by removal).
	bool found = false;
	for (int f = 0; f < brush->get_face_count(); f++) {
		if (brush->get_face_material(f) == mat) {
			found = true;
		}
	}
	CHECK(found);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] collapse_vertices welds to neighbor average") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	Vector<int> verts;
	verts.push_back(6); // (1,1,1)
	brush->collapse_vertices(verts);

	// Vertex 6 moved to the average of its 3 neighbors: (1,1,0),(1,0,1),(0,1,1)
	// -> (2/3, 2/3, 2/3).
	Vector3 expected(2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0);
	bool found_avg = false;
	for (int i = 0; i < brush->get_vertex_count(); i++) {
		if (brush->get_vertex(i).is_equal_approx(expected)) {
			found_avg = true;
		}
	}
	CHECK(found_avg);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] serialization round-trips topology") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 3, 4)));
	brush->extrude_face(4, 1.0);

	PackedVector3Array saved_verts = brush->get_vertices_data();
	Array saved_faces = brush->get_faces_data();
	Array saved_mats = brush->get_face_materials_data();

	LevelBrush *copy = memnew(LevelBrush);
	copy->set_vertices_data(saved_verts);
	copy->set_faces_data(saved_faces);
	copy->set_face_materials_data(saved_mats);

	CHECK(copy->get_vertex_count() == brush->get_vertex_count());
	CHECK(copy->get_face_count() == brush->get_face_count());
	for (int i = 0; i < brush->get_vertex_count(); i++) {
		CHECK(copy->get_vertex(i).is_equal_approx(brush->get_vertex(i)));
	}
	for (int f = 0; f < brush->get_face_count(); f++) {
		CHECK(copy->get_face_normal(f).is_equal_approx(brush->get_face_normal(f)));
	}

	memdelete(copy);
	memdelete(brush);
}

TEST_CASE("[LevelBrush] collision and bake triangle counts match topology") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	Vector<Vector3> tris;
	brush->get_collision_faces(tris);
	// 6 quads * 2 triangles * 3 verts.
	CHECK(tris.size() == 36);

	// Per-face bake data: quad -> 2 tris with consistent normals/uvs.
	for (int f = 0; f < 6; f++) {
		Vector<Vector3> verts, normals;
		Vector<Vector2> uvs;
		brush->get_bake_surface_data(f, verts, normals, uvs);
		CHECK(verts.size() == 6);
		CHECK(normals.size() == 6);
		CHECK(uvs.size() == 6);
		for (const Vector3 &n : normals) {
			CHECK(n.is_equal_approx(BOX_NORMALS[f]));
		}
	}

	memdelete(brush);
}

} // namespace TestLevelBrush
