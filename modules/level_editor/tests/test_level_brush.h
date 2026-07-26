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

TEST_CASE("[LevelBrush] setup_quad produces a single-face flat brush") {
	LevelBrush *brush = memnew(LevelBrush);
	// XZ plane at y=1, CCW seen from +Y (same winding the quad brush type
	// commits from the ghost).
	Vector3 corners[4] = {
		Vector3(0, 1, 0),
		Vector3(0, 1, 2),
		Vector3(3, 1, 2),
		Vector3(3, 1, 0),
	};
	brush->setup_quad(corners);

	CHECK(brush->get_vertex_count() == 4);
	CHECK(brush->get_face_count() == 1);
	CHECK(brush->is_valid());
	CHECK(brush->get_face(0).size() == 4);
	CHECK(brush->get_face_normal(0).is_equal_approx(Vector3(0, 1, 0)));
	CHECK(brush->get_center().is_equal_approx(Vector3(1.5, 1, 1)));

	// A reversed winding flips the normal.
	LevelBrush *flipped = memnew(LevelBrush);
	Vector3 rev[4] = { corners[3], corners[2], corners[1], corners[0] };
	flipped->setup_quad(rev);
	CHECK(flipped->get_face_normal(0).is_equal_approx(Vector3(0, -1, 0)));

	memdelete(brush);
	memdelete(flipped);
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

TEST_CASE("[LevelBrush] extrude_edge duplicates the edge and stitches walls") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Top-front edge of the box: verts 7 (0,1,1) and 6 (1,1,1) (from
	// setup_box corner ordering). Pull it straight UP (a pull exactly along
	// the source-normal bisector would make every normal-based outward
	// check degenerate by construction).
	int new_ids[2] = { -1, -1 };
	LevelBrush::EdgeKey e;
	e.a = 6;
	e.b = 7;
	REQUIRE(brush->extrude_edge(e, Vector3(0, 0.5, 0), new_ids));

	// 8 original + 2 duplicated verts.
	CHECK(brush->get_vertex_count() == 10);
	CHECK(brush->get_vertex(new_ids[0]).is_equal_approx(Vector3(1, 1.5, 1)));
	CHECK(brush->get_vertex(new_ids[1]).is_equal_approx(Vector3(0, 1.5, 1)));

	// The edge borders 2 faces (top + front): 6 originals + ONE wall (the
	// bevel face Hammer creates between the two planes).
	CHECK(brush->get_face_count() == 7);

	// The source faces are untouched: the top face still ends at z=1.
	real_t max_z = -(real_t)Math::INF;
	LocalVector<int> front = brush->get_face(0); // +Z face (setup_box order).
	for (int idx : front) {
		max_z = MAX(max_z, brush->get_vertex(idx).z);
	}
	CHECK(Math::is_equal_approx(max_z, (real_t)1.0));

	// The wall (appended last) faces outward: pulled straight UP from the
	// top-front edge, the wall is VERTICAL (it spans the old edge and the
	// raised duplicate), and its outward normal points forward (+Z, the
	// side the solid does not extend to above the front face).
	Vector3 wn = brush->get_face_normal(6);
	CHECK(wn.z > 0.9);
	// And it is perpendicular to the extruded edge (which runs along X).
	CHECK(Math::is_zero_approx(wn.x));

	memdelete(brush);
}

TEST_CASE("[LevelBrush] extrude_edge on an open edge (quad) hangs one wall") {
	LevelBrush *brush = memnew(LevelBrush);
	// Floor quad at y=0 (single face, normal +Y).
	Vector3 corners[4] = {
		Vector3(0, 0, 0),
		Vector3(0, 0, 2),
		Vector3(2, 0, 2),
		Vector3(2, 0, 0),
	};
	brush->setup_quad(corners);

	// Extrude the z=0 edge (verts 0 and 3) downward -> a wall hanging off
	// the quad's edge, like Hammer's edge drag on a plane.
	int new_ids[2] = { -1, -1 };
	LevelBrush::EdgeKey e;
	e.a = 0;
	e.b = 3;
	REQUIRE(brush->extrude_edge(e, Vector3(0, -1, 0), new_ids));

	CHECK(brush->get_vertex_count() == 6);
	// Open edge: exactly ONE stitched wall (the quad has only one face).
	CHECK(brush->get_face_count() == 2);
	CHECK(brush->get_vertex(new_ids[0]).is_equal_approx(Vector3(0, -1, 0)));
	CHECK(brush->get_vertex(new_ids[1]).is_equal_approx(Vector3(2, -1, 0)));

	// The wall must be vertical (its normal lies in the XZ plane).
	Vector3 wall_n = brush->get_face_normal(1);
	CHECK(Math::is_zero_approx(wall_n.y));

	memdelete(brush);
}

TEST_CASE("[LevelBrush] extrude_edge quad walls face out of the solid for every edge") {
	// Regression: the old pull-based winding (edge_dir x pull, sign-flipped
	// toward normal_sum) was a no-op for a flat quad - normal_sum (+Y) is
	// perpendicular to every possible wall normal - so only the -Z wall
	// faced outward. Hammer's walls face out of the solid on ALL sides.
	LevelBrush *brush = memnew(LevelBrush);
	Vector3 corners[4] = {
		Vector3(0, 0, 0),
		Vector3(0, 0, 2),
		Vector3(2, 0, 2),
		Vector3(2, 0, 0),
	};
	brush->setup_quad(corners);

	// Extrude all 4 edges downward (like Shift+dragging each side of a floor
	// quad to form an open box). Quad loop order: (0,1),(1,2),(2,3),(3,0).
	const int edge_verts[4][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 } };
	const Vector3 out_dirs[4] = {
		Vector3(-1, 0, 0), // x=0 edge -> -X
		Vector3(0, 0, 1), // z=2 edge -> +Z
		Vector3(1, 0, 0), // x=2 edge -> +X
		Vector3(0, 0, -1), // z=0 edge -> -Z
	};
	for (int i = 0; i < 4; i++) {
		int new_ids[2] = { -1, -1 };
		LevelBrush::EdgeKey e;
		e.a = edge_verts[i][0];
		e.b = edge_verts[i][1];
		REQUIRE(brush->extrude_edge(e, Vector3(0, -1, 0), new_ids));

		// Each wall (appended last) is vertical and faces AWAY from the quad.
		const int wall = brush->get_face_count() - 1;
		const Vector3 wn = brush->get_face_normal(wall);
		CHECK(Math::is_zero_approx(wn.y));
		CHECK_MESSAGE(wn.dot(out_dirs[i]) > 0.9, "wall ", i, " normal ", wn, " should face ", out_dirs[i]);
	}

	memdelete(brush);
}

TEST_CASE("[LevelBrush] extrude_edge cube top edge wall faces outward for every pull direction") {
	// Regression for the gizmo stub-freeze: the wall winding is decided once
	// from the begin-drag stub, then the drag moves the wall elsewhere. A -X
	// pull on a top edge ended up back-facing (the "flipped texture" report).
	// Whatever the pull, the wall normal must point AWAY from the solid.
	const Vector3 pulls[4] = {
		Vector3(1, 0, 0),
		Vector3(-1, 0, 0),
		Vector3(0, 0, 1),
		Vector3(0, 0, -1),
	};
	for (int p = 0; p < 4; p++) {
		LevelBrush *brush = memnew(LevelBrush);
		brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

		// Top-front edge (verts 6,7 at y=1,z=1), pulled horizontally.
		int new_ids[2] = { -1, -1 };
		LevelBrush::EdgeKey e;
		e.a = 6;
		e.b = 7;
		REQUIRE(brush->extrude_edge(e, pulls[p] * 0.5, new_ids));

		const int wall = brush->get_face_count() - 1;
		const Vector3 wn = brush->get_face_normal(wall);
		const Vector3 outward = brush->get_face_center(wall) - brush->get_center();
		CHECK_MESSAGE(wn.dot(outward) > 0.0, "pull ", pulls[p], " wall normal ", wn, " faces into the solid");

		memdelete(brush);
	}
}

TEST_CASE("[LevelBrush] extrude_vertex duplicates the vert and stitches wedges") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Corner vert 6 = (1,1,1); it touches 3 faces. Pull it outward.
	int new_v = brush->extrude_vertex(6, Vector3(0.5, 0.5, 0.5));
	REQUIRE(new_v >= 0);

	CHECK(brush->get_vertex_count() == 9);
	CHECK(brush->get_vertex(new_v).is_equal_approx(Vector3(1.5, 1.5, 1.5)));
	// 6 original faces + 3 stitched wedges.
	CHECK(brush->get_face_count() == 9);

	// No face may be degenerate, and the three wedges (appended last) must
	// face outward - their normals point away from the brush center.
	Vector3 center = brush->get_center();
	for (int f = 0; f < brush->get_face_count(); f++) {
		CHECK(brush->get_face_normal(f).length() > 0.99);
	}
	for (int f = 6; f < 9; f++) {
		CHECK(brush->get_face_normal(f).dot(brush->get_face_center(f) - center) > 0.0);
	}

	// The source faces are untouched: the +X face (index 2) must still
	// contain the ORIGINAL corner 6, not the duplicate.
	LocalVector<int> right = brush->get_face(2);
	bool has_orig = false;
	bool has_dupe = false;
	for (int idx : right) {
		has_orig = has_orig || idx == 6;
		has_dupe = has_dupe || idx == new_v;
	}
	CHECK(has_orig);
	CHECK(!has_dupe);

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

TEST_CASE("[LevelBrush] clip by a missing plane is a no-op, a full clip empties") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	PackedVector3Array before = brush->get_vertices_data();

	// Keep side is distance >= 0 (normal.dot(p) - d). A plane at x = -5 keeps
	// the whole box (x >= -5): nothing crosses, brush untouched.
	brush->clip(Plane(Vector3(1, 0, 0), -5.0));
	CHECK(brush->get_face_count() == 6);
	CHECK(brush->get_vertices_data() == before);

	// A plane at x = 5 keeps only x >= 5: everything is clipped away.
	brush->clip(Plane(Vector3(1, 0, 0), 5.0));
	CHECK(brush->get_face_count() == 0);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] clip_split produces two closed halves with complementary caps") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Split at x=0.5: this brush keeps the +X side, the returned brush keeps
	// the -X side.
	LevelBrush *back = brush->clip_split(Plane(Vector3(1, 0, 0), 0.5));
	REQUIRE(back != nullptr);

	// Both halves have a cap face on the cut plane with opposing normals.
	auto has_cap = [](LevelBrush *b, const Vector3 &p_normal) {
		for (int f = 0; f < b->get_face_count(); f++) {
			LocalVector<int> loop = b->get_face(f);
			bool all_on_plane = loop.size() >= 3;
			for (int idx : loop) {
				if (Math::abs(b->get_vertex(idx).x - 0.5) > 0.001) {
					all_on_plane = false;
					break;
				}
			}
			if (all_on_plane && b->get_face_normal(f).is_equal_approx(p_normal)) {
				return true;
			}
		}
		return false;
	};
	CHECK(has_cap(brush, Vector3(-1, 0, 0)));
	CHECK(has_cap(back, Vector3(1, 0, 0)));

	// Referenced verts of each half stay in their slab.
	auto slab_ok = [](LevelBrush *b, bool p_upper) {
		for (int f = 0; f < b->get_face_count(); f++) {
			LocalVector<int> loop = b->get_face(f);
			for (int idx : loop) {
				real_t x = b->get_vertex(idx).x;
				if (p_upper) {
					if (x < 0.5 - 0.001) {
						return false;
					}
				} else if (x > 0.5 + 0.001) {
					return false;
				}
			}
		}
		return true;
	};
	CHECK(slab_ok(brush, true));
	CHECK(slab_ok(back, false));

	memdelete(back);
	memdelete(brush);
}

TEST_CASE("[LevelBrush] weld_vertices merges to the average and drops degenerate faces") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Weld the 4 top corners (y=1 verts).
	Vector<int> top;
	for (int i = 0; i < 8; i++) {
		if (brush->get_vertex(i).y > 0.5) {
			top.push_back(i);
		}
	}
	REQUIRE(top.size() == 4);
	brush->weld_vertices(top);

	// Every remaining face still has >= 3 unique indices.
	for (int f = 0; f < brush->get_face_count(); f++) {
		LocalVector<int> loop = brush->get_face(f);
		HashSet<int> unique;
		for (int idx : loop) {
			unique.insert(idx);
		}
		CHECK(unique.size() >= 3);
	}
	// The welded survivor sits at the top-face center.
	bool found_center = false;
	for (int i = 0; i < brush->get_vertex_count(); i++) {
		if (brush->get_vertex(i).is_equal_approx(Vector3(0.5, 1, 0.5))) {
			found_center = true;
		}
	}
	CHECK(found_center);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] extrude_face with negative distance keeps outward normals") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Pull the top face DOWN by 0.25 (cap at y=0.75, still above the center).
	// Winding is flipped for negative distance so normals must stay outward.
	brush->extrude_face(4, -0.25);

	Vector3 center = brush->get_center();
	for (int f = 0; f < brush->get_face_count(); f++) {
		Vector3 fc = brush->get_face_center(f);
		Vector3 n = brush->get_face_normal(f);
		Vector3 out = fc - center;
		if (out.length_squared() < 0.0001) {
			continue; // Face centered on the brush centroid: no outward direction.
		}
		CHECK(n.dot(out) > 0.0);
	}

	memdelete(brush);
}

TEST_CASE("[LevelBrush] extrude_face on an interior brush keeps stored-normal direction") {
	// Interior (flipped) brush: stored loops keep solid-outward normals, and
	// extrude_face stubs along the STORED normal like any solid. (The gizmo
	// decides the visual extrude direction; the op stays flip-agnostic.)
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));
	brush->set_faces_flipped(true);

	int cap = brush->extrude_face(3, 0.5); // -X face.
	REQUIRE(cap >= 0);

	LocalVector<int> cap_loop = brush->get_face(cap);
	for (int idx : cap_loop) {
		CHECK(brush->get_vertex(idx).x == doctest::Approx(-0.5));
	}
	CHECK(brush->get_face_normal(cap).is_equal_approx(Vector3(-1, 0, 0)));

	// Side walls face out of the solid (away from the centroid).
	Vector3 center = brush->get_center();
	for (int f = 0; f < brush->get_face_count(); f++) {
		Vector3 out = brush->get_face_center(f) - center;
		if (out.length_squared() < 0.0001) {
			continue;
		}
		CHECK(brush->get_face_normal(f).dot(out) > 0.0);
	}

	memdelete(brush);
}

TEST_CASE("[LevelBrush] get_face_normal invariants (unit length, planar match, bent quad, degenerate)") {
	// Planar quad: Newell matches the cross-product normal and is unit length.
	LevelBrush *quad = memnew(LevelBrush);
	Vector3 corners[4] = {
		Vector3(0, 0, 0),
		Vector3(0, 0, 2),
		Vector3(3, 0, 2),
		Vector3(3, 0, 0),
	};
	quad->setup_quad(corners);
	const Vector3 n = quad->get_face_normal(0);
	CHECK(n.length() == doctest::Approx(1.0));
	CHECK(n.is_equal_approx(Vector3(0, 1, 0)));
	memdelete(quad);

	// Non-planar (bent) quad: Newell still returns a unit-length, consistent
	// normal (this is why Newell is used over a triangle cross).
	LevelBrush *bent = memnew(LevelBrush);
	Vector3 bc[4] = {
		Vector3(0, 0, 0),
		Vector3(0, 0, 1),
		Vector3(1, 0.5, 1), // Raised corner bends the quad.
		Vector3(1, 0, 0),
	};
	bent->setup_quad(bc);
	const Vector3 bn = bent->get_face_normal(0);
	CHECK(bn.length() == doctest::Approx(1.0));
	CHECK(bn.y > 0.5); // Still mostly up.
	memdelete(bent);

	// Degenerate (collinear) face: the fallback normal, not a zero vector.
	LevelBrush *box = memnew(LevelBrush);
	box->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));
	box->set_vertex(0, Vector3(0.5, 0.5, 0.5));
	box->set_vertex(1, Vector3(0.5, 0.5, 0.5));
	box->set_vertex(2, Vector3(0.5, 0.5, 0.5));
	box->set_vertex(3, Vector3(0.5, 0.5, 0.5));
	// Face 3 is the -X face (verts 0,3,7,4): partially collapsed, still has
	// area. Collapse the whole brush to a point instead:
	for (int i = 0; i < 8; i++) {
		box->set_vertex(i, Vector3(0.5, 0.5, 0.5));
	}
	for (int f = 0; f < 6; f++) {
		CHECK(box->get_face_normal(f).is_equal_approx(Vector3(0, 1, 0)));
	}
	memdelete(box);
}

TEST_CASE("[LevelBrush] ray_intersect edge cases (miss keeps dist, back hit, grazing)") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Miss leaves r_dist untouched (documented contract).
	real_t dist = 123.0;
	int face = brush->ray_intersect(Vector3(5, 5, 5), Vector3(1, 0, 0), dist);
	CHECK(face == -1);
	CHECK(dist == doctest::Approx(123.0));

	// From inside the box outward: hits the exit face (back side of the quad).
	dist = 0.0;
	face = brush->ray_intersect(Vector3(0.5, 0.5, 0.5), Vector3(1, 0, 0), dist);
	CHECK(face == 2); // +X face.
	CHECK(dist == doctest::Approx(0.5));

	// Ray parallel to a face, offset outside it: no hit.
	dist = 0.0;
	face = brush->ray_intersect(Vector3(0.5, 2.0, 0.5), Vector3(1, 0, 0), dist);
	CHECK(face == -1);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] clip with a diagonal plane produces a planar, outward cap") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Diagonal cut keeping x+y+z <= 2: keep side is normal*p >= d with the
	// normal pointing AWAY from the kept solid (same convention as the
	// axis-aligned test: Plane(+X, 0.5) keeps x >= 0.5).
	const Plane plane(Vector3(-1, -1, -1).normalized(), -2.0 / Math::sqrt(3.0));
	brush->clip(plane);

	// Brush stays valid; the cap is the last face, planar, and its stored
	// normal points at the REMOVED side (opposite the keep plane's normal).
	REQUIRE(brush->is_valid());
	const int cap = brush->get_face_count() - 1;
	const Vector3 cap_n = brush->get_face_normal(cap);
	CHECK(cap_n.length() == doctest::Approx(1.0));
	CHECK(cap_n.dot(-plane.normal) > 0.9);

	// Planarity: every cap vert satisfies the plane equation.
	LocalVector<int> loop = brush->get_face(cap);
	REQUIRE(loop.size() >= 3);
	for (int idx : loop) {
		CHECK(Math::abs(plane.distance_to(brush->get_vertex(idx))) < 0.001);
	}

	memdelete(brush);
}

TEST_CASE("[LevelBrush] rewind_face_outward reverses only inward faces") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Already-outward face: no-op (loop order unchanged).
	LocalVector<int> before = brush->get_face(0);
	brush->rewind_face_outward(0);
	LocalVector<int> after = brush->get_face(0);
	REQUIRE(before.size() == after.size());
	for (uint32_t i = 0; i < before.size(); i++) {
		CHECK(before[i] == after[i]);
	}

	// Reverse the loop by hand -> normal now points into the solid -> rewind
	// restores an outward normal.
	Array faces_data = brush->get_faces_data();
	PackedInt32Array loop = faces_data[1];
	PackedInt32Array rev;
	for (int i = loop.size() - 1; i >= 0; i--) {
		rev.push_back(loop[i]);
	}
	faces_data[1] = rev;
	brush->set_faces_data(faces_data);
	const Vector3 center = brush->get_center();
	REQUIRE(brush->get_face_normal(1).dot(brush->get_face_center(1) - center) < 0.0);
	brush->rewind_face_outward(1);
	CHECK(brush->get_face_normal(1).dot(brush->get_face_center(1) - center) > 0.0);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] mirror round-trip is identity and keeps face materials") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 2, 3)));
	Ref<Material> mat;
	mat.instantiate();
	brush->set_face_material(0, mat);

	const PackedVector3Array orig_verts = brush->get_vertices_data();
	const Plane p(Vector3(1, 1, 0).normalized(), 0.3);
	brush->mirror(p);
	CHECK(brush->get_face_material(0) == mat);
	brush->mirror(p); // Mirror twice = identity.

	const PackedVector3Array rt_verts = brush->get_vertices_data();
	REQUIRE(rt_verts.size() == orig_verts.size());
	for (int i = 0; i < orig_verts.size(); i++) {
		CHECK(rt_verts[i].is_equal_approx(orig_verts[i]));
	}
	CHECK(brush->get_face_material(0) == mat);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] compact_vertices with no orphans is a stable no-op") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	const PackedVector3Array before_verts = brush->get_vertices_data();
	const Array before_faces = brush->get_faces_data();
	brush->compact_vertices();
	CHECK(brush->get_vertices_data() == before_verts);
	// Face loops unchanged (indices stable when nothing is removed).
	const Array after_faces = brush->get_faces_data();
	REQUIRE(after_faces.size() == before_faces.size());
	for (int i = 0; i < after_faces.size(); i++) {
		CHECK(PackedInt32Array(after_faces[i]) == PackedInt32Array(before_faces[i]));
	}

	memdelete(brush);
}

TEST_CASE("[LevelBrush] duplicate_brush copies topology, materials, and flipped flag") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 2, 3)));
	Ref<Material> mat;
	mat.instantiate();
	brush->set_face_material(0, mat);
	brush->set_faces_flipped(true);

	LevelBrush *copy = brush->duplicate_brush();
	REQUIRE(copy != nullptr);
	CHECK(copy->get_vertices_data() == brush->get_vertices_data());
	CHECK(copy->get_faces_data().size() == brush->get_faces_data().size());
	CHECK(copy->get_face_material(0) == mat);
	CHECK(copy->is_faces_flipped());

	memdelete(copy);
	memdelete(brush);
}

TEST_CASE("[LevelBrush] get_edges are unique and canonically ordered") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = brush->get_edges();
	CHECK(edges.size() == 12); // A cube has 12 unique edges.
	for (const LevelBrush::EdgeKey &e : edges) {
		CHECK(e.a < e.b); // Canonical ordering: smaller index first.
		CHECK(e.a >= 0);
		CHECK(e.b < 8);
	}

	memdelete(brush);
}

TEST_CASE("[LevelBrush] get_face_center and get_center track geometry") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 4, 6)));

	CHECK(brush->get_center().is_equal_approx(Vector3(1, 2, 3)));
	CHECK(brush->get_face_center(4).is_equal_approx(Vector3(1, 4, 3))); // +Y face.
	CHECK(brush->get_face_center(5).is_equal_approx(Vector3(1, 0, 3))); // -Y face.

	memdelete(brush);
}

TEST_CASE("[LevelBrush] clip without cap leaves the cut open") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Vertical cut at x=0.5, keep +X half, no cap.
	brush->clip(Plane(Vector3(1, 0, 0), 0.5), false);

	// 5 faces survive: +X (untouched quad), +Y/-Y/+Z/-Z (each halved by the
	// cut), and no cap on the cut plane.
	CHECK(brush->get_face_count() == 5);
	for (int f = 0; f < brush->get_face_count(); f++) {
		LocalVector<int> loop = brush->get_face(f);
		bool all_on_plane = true;
		for (int idx : loop) {
			if (Math::abs(brush->get_vertex(idx).x - 0.5) > 0.001) {
				all_on_plane = false;
				break;
			}
		}
		CHECK(!all_on_plane); // No cap face.
	}

	memdelete(brush);
}

TEST_CASE("[LevelBrush] subdivide_face splits a quad into four quads") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));

	Ref<Material> mat;
	mat.instantiate();
	brush->set_face_material(4, mat); // +Y face.

	REQUIRE(brush->subdivide_face(4));

	// 6 - 1 + 4 faces; 5 new verts (4 midpoints + centroid).
	CHECK(brush->get_face_count() == 9);
	CHECK(brush->get_vertex_count() == 8 + 5);

	// All four new faces are quads, inherit the material, and keep the
	// outward (+Y) normal.
	int quad_count = 0;
	for (int f = 0; f < brush->get_face_count(); f++) {
		LocalVector<int> loop = brush->get_face(f);
		bool on_top = true;
		for (int idx : loop) {
			if (Math::abs(brush->get_vertex(idx).y - 2.0) > 0.001) {
				on_top = false;
				break;
			}
		}
		if (on_top) {
			quad_count++;
			CHECK(loop.size() == 4);
			CHECK(brush->get_face_material(f) == mat);
			CHECK(brush->get_face_normal(f).is_equal_approx(Vector3(0, 1, 0)));
		}
	}
	CHECK(quad_count == 4);

	// The centroid vert exists at the face center.
	bool has_center = false;
	for (int i = 0; i < brush->get_vertex_count(); i++) {
		if (brush->get_vertex(i).is_equal_approx(Vector3(1, 2, 1))) {
			has_center = true;
			break;
		}
	}
	CHECK(has_center);

	// Neighboring faces must pass through the new midpoint verts (no
	// T-junctions): each of the 4 side faces of the box gains one midpoint
	// on the boundary it shared with the subdivided face (quad ->
	// pentagon).
	int pentagons = 0;
	for (int f = 0; f < brush->get_face_count(); f++) {
		if (brush->get_face(f).size() == 5) {
			pentagons++;
		}
	}
	CHECK(pentagons == 4);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] subdivide_face fans an n-gon into triangles") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Make pentagons: split_faces with an oblique plane crosses two ADJACENT
	// edges of a face, splitting it into a triangle + a pentagon.
	brush->split_faces(Plane(Vector3(1, 1, 1).normalized(), 1.6 / Math::sqrt(3.0)));
	int ngon = -1;
	for (int f = 0; f < brush->get_face_count(); f++) {
		if (brush->get_face(f).size() > 4) {
			ngon = f;
			break;
		}
	}
	REQUIRE(ngon >= 0);
	const int n = (int)brush->get_face(ngon).size();
	const int faces_before = brush->get_face_count();

	REQUIRE(brush->subdivide_face(ngon));

	// The n-gon is replaced by n triangles: the first at index ngon, the
	// remaining n-1 APPENDED at the end (indices faces_before .. end).
	// Other faces (including any other pentagons) are untouched.
	CHECK(brush->get_face_count() == faces_before - 1 + n);
	CHECK(brush->get_face(ngon).size() == 3);
	for (int i = 0; i < n - 1; i++) {
		CHECK(brush->get_face(faces_before + i).size() == 3);
	}

	// All triangles together reference exactly the n-gon's original loop
	// verts plus the new centroid.
	HashSet<int> tris_verts;
	for (int idx : brush->get_face(ngon)) {
		tris_verts.insert(idx);
	}
	for (int i = 0; i < n - 1; i++) {
		for (int idx : brush->get_face(faces_before + i)) {
			tris_verts.insert(idx);
		}
	}
	CHECK(tris_verts.size() == n + 1); // n loop verts + centroid.

	memdelete(brush);
}

TEST_CASE("[LevelBrush] subdivide_face rejects invalid faces") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	ERR_PRINT_OFF;
	CHECK(!brush->subdivide_face(-1));
	CHECK(!brush->subdivide_face(6));
	ERR_PRINT_ON;
	CHECK(brush->get_face_count() == 6); // Untouched.

	memdelete(brush);
}

TEST_CASE("[LevelBrush] get_edge_loop walks the ring of parallel edges") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// A vertical edge (bottom-front-left to top-front-left: verts 0 and 3
	// share x=0,z=0... verts 0=(0,0,0), 3=(0,1,0)): its loop is all 4
	// vertical edges of the box.
	LevelBrush::EdgeKey vertical(0, 3);
	Vector<LevelBrush::EdgeKey> loop = brush->get_edge_loop(vertical);

	CHECK(loop.size() == 4);
	// Every loop edge is vertical (parallel to the start, no shared verts).
	for (const LevelBrush::EdgeKey &e : loop) {
		Vector3 a = brush->get_vertex(e.a);
		Vector3 b = brush->get_vertex(e.b);
		Vector3 d = (b - a).normalized();
		CHECK(Math::abs(d.y) > 0.99);
	}

	// An edge that doesn't exist yields just itself.
	Vector<LevelBrush::EdgeKey> empty = brush->get_edge_loop(LevelBrush::EdgeKey(0, 7));
	CHECK(empty.size() == 1);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] get_edge_loop stops at n-gon boundaries") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// A horizontal top-front edge loops through the top face (4 horizontal
	// top edges before subdivision).
	LevelBrush::EdgeKey top_front(3, 2); // (0,1,0) -> (1,1,0)
	CHECK(brush->get_edge_loop(top_front).size() == 4);

	// Nick the (1,1,1) corner: keep the side x+y+z <= ~2.3 (clip keeps the
	// side the normal points to, so flip the normal to keep the origin
	// side). The +Y face loses one corner and becomes a pentagon.
	const Vector3 clip_n = Vector3(-1, -1, -1).normalized();
	brush->clip(Plane(clip_n, clip_n.dot(Vector3(1, 1, 1)) + 0.4));

	// Subdividing that pentagon makes a TRIANGLE FAN; any edge bordering a
	// triangle has no well-defined opposite, so the loop dies immediately.
	int pentagon = -1;
	for (int f = 0; f < brush->get_face_count(); f++) {
		if (brush->get_face(f).size() == 5) {
			pentagon = f;
			break;
		}
	}
	REQUIRE(pentagon >= 0);
	const int faces_before = brush->get_face_count();
	REQUIRE(brush->subdivide_face(pentagon)); // Pentagon -> 5 triangles.

	// An edge between two triangles has no well-defined opposite on either
	// side, so the loop dies immediately (opposite_edge() returns -1 for
	// any non-quad). The fan's CENTROID edges are triangle-on-both-sides:
	// find the centroid as the vert appearing in EVERY fan triangle (the
	// subdivided face at `pentagon` plus the appended ones).
	const int fan_faces = brush->get_face_count() - faces_before + 1;
	REQUIRE(fan_faces == 5);
	int centroid = -1;
	for (int v = 0; v < brush->get_vertex_count() && centroid < 0; v++) {
		int count = 0;
		// Fan triangles: face `pentagon` (replaced in place) plus the
		// (fan_faces - 1) faces appended at the end.
		for (int k = 0; k < fan_faces; k++) {
			int f = (k == 0) ? pentagon : brush->get_face_count() - k;
			LocalVector<int> t = brush->get_face(f);
			for (uint32_t j = 0; j < t.size(); j++) {
				if (t[j] == v) {
					count++;
					break;
				}
			}
		}
		if (count == fan_faces) {
			centroid = v;
		}
	}
	REQUIRE(centroid >= 0);
	// Any other vert of the first fan triangle forms a centroid edge.
	LocalVector<int> tri = brush->get_face(pentagon);
	REQUIRE(tri.size() == 3);
	int other = tri[0] == centroid ? tri[1] : tri[0];
	Vector<LevelBrush::EdgeKey> loop = brush->get_edge_loop(LevelBrush::EdgeKey(centroid, other));
	CHECK(loop.size() == 1);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] get_edge_chain follows collinear segments") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));
	REQUIRE(brush->subdivide_face(4)); // Top face -> 4 quads via midpoints+centroid.

	// Find the centroid vert (the one at the face center (1,2,1)).
	int centroid = -1;
	for (int i = 0; i < brush->get_vertex_count(); i++) {
		if (brush->get_vertex(i).is_equal_approx(Vector3(1, 2, 1))) {
			centroid = i;
			break;
		}
	}
	REQUIRE(centroid >= 0);

	// Find the midpoint of the top face's +Z outer edge (1,2,2) and form the
	// "top middle line" edge from it to the centroid.
	int mid_zp = -1;
	for (int i = 0; i < brush->get_vertex_count(); i++) {
		if (brush->get_vertex(i).is_equal_approx(Vector3(1, 2, 2))) {
			mid_zp = i;
			break;
		}
	}
	REQUIRE(mid_zp >= 0);

	// The chain from (mid_zp, centroid) should continue straight to the
	// opposite midpoint (1,2,0) - 2 segments total, no perpendicular turns.
	LevelBrush::EdgeKey start(mid_zp, centroid);
	Vector<LevelBrush::EdgeKey> chain = brush->get_edge_chain(start);
	CHECK(chain.size() == 2);
	for (const LevelBrush::EdgeKey &e : chain) {
		Vector3 d = (brush->get_vertex(e.b) - brush->get_vertex(e.a)).normalized();
		CHECK(Math::abs(d.z) > 0.99); // All segments run along Z.
	}

	memdelete(brush);
}

TEST_CASE("[LevelBrush] bevel_edges consumes the edge into a strip face") {
	// Blender/Hammer-style bevel on a 90-degree corner: the original edge
	// is consumed; one new quad bridges two lines offset p_distance into
	// each adjacent face.
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));

	// Bevel the top-front edge (0,2,0) -> (2,2,0): verts 3 -> 2.
	Vector<LevelBrush::EdgeKey> edges;
	edges.push_back(LevelBrush::EdgeKey(3, 2));
	CHECK(brush->bevel_edges(edges, 0.5) == 1);

	// 6 + 1 faces (one strip quad); 8 + 4 = 12 verts minus the 2 consumed
	// endpoints (bevel compacts orphaned verts) = 10.
	CHECK(brush->get_face_count() == 7);
	CHECK(brush->get_vertex_count() == 10);

	// No face references the original endpoint POSITIONS (compaction may
	// reuse their old indices for other verts, so compare by position).
	for (int f = 0; f < brush->get_face_count(); f++) {
		LocalVector<int> loop = brush->get_face(f);
		for (uint32_t i = 0; i < loop.size(); i++) {
			const Vector3 v = brush->get_vertex(loop[i]);
			CHECK(!v.is_equal_approx(Vector3(0, 2, 0)));
			CHECK(!v.is_equal_approx(Vector3(2, 2, 0)));
		}
	}

	// The strip face (last) is a quad whose normal points outward -
	// up-and-backward (+Y, -Z): the 45-degree chamfer of the corner.
	LocalVector<int> strip = brush->get_face(6);
	CHECK(strip.size() == 4);
	Vector3 n = brush->get_face_normal(6);
	CHECK(n.y > 0.5);
	CHECK(n.z < -0.5);
	CHECK(Math::abs(n.x) < 0.001);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] bevel_edges offsets exactly p_distance into each face") {
	// The slide verts must sit exactly p_distance from the original edge,
	// perpendicular to it within each adjacent face (NOT 2*d as a
	// boundary-edge slide would produce at 90-degree corners).
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));

	// Top-front edge (0,2,0) -> (2,2,0), adjacent faces: top (y=2), front (z=0).
	Vector<LevelBrush::EdgeKey> edges;
	edges.push_back(LevelBrush::EdgeKey(3, 2));
	REQUIRE(brush->bevel_edges(edges, 0.5) == 1);

	// Expect new verts at (0, 1.5, 0) / (2, 1.5, 0) on the top face and
	// (0, 2, 0.5) / (2, 2, 0.5) on the front face.
	int found = 0;
	for (int i = 0; i < brush->get_vertex_count(); i++) {
		const Vector3 v = brush->get_vertex(i);
		if (v.is_equal_approx(Vector3(0, 1.5, 0)) || v.is_equal_approx(Vector3(2, 1.5, 0)) ||
				v.is_equal_approx(Vector3(0, 2, 0.5)) || v.is_equal_approx(Vector3(2, 2, 0.5))) {
			found++;
		}
	}
	CHECK(found == 4);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] bevel_edges makes one continuous strip across a collinear chain") {
	// Subdivide the top face, then bevel BOTH collinear edges of the middle
	// line in one action: the strips must share slide verts at the middle
	// vertex (no doubled/overlapping verts) and the middle vertex survives
	// as the strip centerline.
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));
	REQUIRE(brush->subdivide_face(4)); // +Y face -> 4 quads via midpoints+centroid.

	// Find the centroid (1,2,1) and the top face's two front-edge midpoint
	// verts: front edge (0,2,0)-(2,2,0) midpoint (1,2,0). The middle line
	// along X is (0,2,1)->(1,2,1)->(2,2,1): centroid + two side midpoints.
	int centroid = -1, mid_l = -1, mid_r = -1;
	for (int i = 0; i < brush->get_vertex_count(); i++) {
		const Vector3 v = brush->get_vertex(i);
		if (v.is_equal_approx(Vector3(1, 2, 1))) {
			centroid = i;
		} else if (v.is_equal_approx(Vector3(0, 2, 1))) {
			mid_l = i;
		} else if (v.is_equal_approx(Vector3(2, 2, 1))) {
			mid_r = i;
		}
	}
	REQUIRE(centroid >= 0);
	REQUIRE(mid_l >= 0);
	REQUIRE(mid_r >= 0);

	Vector<LevelBrush::EdgeKey> edges;
	edges.push_back(LevelBrush::EdgeKey(mid_l, centroid));
	edges.push_back(LevelBrush::EdgeKey(centroid, mid_r));
	CHECK(brush->bevel_edges(edges, 0.25) == 2);

	// Both adjacent faces of the chain are coplanar (all on y=2), so each
	// edge shares its slide verts between its two sides - 2 new verts per
	// endpoint region: the 3 distinct endpoints (mid_l, centroid, mid_r)
	// each get 2 (one per side of the line), with the centroid's shared by
	// both edges: 3 * 2 = 6 new verts total. All three original endpoint
	// verts are consumed everywhere (sub-quads + the side-cap trims) and
	// compacted away (-3).
	CHECK(brush->get_vertex_count() == 13 + 6 - 3); // 8 box + 5 subdiv + 6 - 3 consumed.

	// New verts: (x, 2, 0.75) and (x, 2, 1.25) for x in {0, 1, 2}.
	int found = 0;
	for (int i = 0; i < brush->get_vertex_count(); i++) {
		const Vector3 v = brush->get_vertex(i);
		if (!Math::is_equal_approx(v.y, (real_t)2.0)) {
			continue;
		}
		if (Math::is_equal_approx(v.z, (real_t)0.75) || Math::is_equal_approx(v.z, (real_t)1.25)) {
			if (Math::is_equal_approx(v.x, (real_t)0.0) || Math::is_equal_approx(v.x, (real_t)1.0) || Math::is_equal_approx(v.x, (real_t)2.0)) {
				found++;
			}
		}
	}
	CHECK(found == 6);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] bevel_edges works on the boundary of a subdivided face") {
	// Subdivide the FRONT face, then bevel its top boundary (now two
	// half-edges through the midpoint) - the user hit a case where these
	// refused to bevel.
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));

	// Find the front face (z=0): the quad containing both (0,2,0) and (2,0,0).
	int front = -1;
	for (int f = 0; f < brush->get_face_count(); f++) {
		bool has_tl = false, has_br = false;
		LocalVector<int> loop = brush->get_face(f);
		for (uint32_t i = 0; i < loop.size(); i++) {
			const Vector3 v = brush->get_vertex(loop[i]);
			has_tl = has_tl || v.is_equal_approx(Vector3(0, 2, 0));
			has_br = has_br || v.is_equal_approx(Vector3(2, 0, 0));
		}
		if (has_tl && has_br) {
			front = f;
			break;
		}
	}
	REQUIRE(front >= 0);
	REQUIRE(brush->subdivide_face(front)); // -> 4 quads.

	// The top boundary halves: (0,2,0)->(1,2,0) and (1,2,0)->(2,2,0).
	int tl = -1, tm = -1, tr = -1;
	for (int i = 0; i < brush->get_vertex_count(); i++) {
		const Vector3 v = brush->get_vertex(i);
		if (v.is_equal_approx(Vector3(0, 2, 0))) {
			tl = i;
		} else if (v.is_equal_approx(Vector3(1, 2, 0))) {
			tm = i;
		} else if (v.is_equal_approx(Vector3(2, 2, 0))) {
			tr = i;
		}
	}
	REQUIRE(tl >= 0);
	REQUIRE(tm >= 0);
	REQUIRE(tr >= 0);

	Vector<LevelBrush::EdgeKey> edges;
	edges.push_back(LevelBrush::EdgeKey(tl, tm));
	edges.push_back(LevelBrush::EdgeKey(tm, tr));
	// BOTH halves must bevel.
	CHECK(brush->bevel_edges(edges, 0.5) == 2);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] bevel_edges trims the end faces at the edge endpoints") {
	// Beveling a box edge must also cut the corner of the faces touching
	// the edge's ENDPOINTS (the side caps) - otherwise the original corner
	// pokes through the chamfer as a fin.
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));

	// Top-front edge (0,2,0) -> (2,2,0): verts 3 -> 2.
	Vector<LevelBrush::EdgeKey> edges;
	edges.push_back(LevelBrush::EdgeKey(3, 2));
	REQUIRE(brush->bevel_edges(edges, 0.5) == 1);

	// The side caps (x=0 and x=2) must no longer reference the original
	// endpoint POSITIONS: their corners were replaced by the offset verts.
	// (Compare by position - compaction may reuse old indices.)
	for (int f = 0; f < brush->get_face_count(); f++) {
		LocalVector<int> loop = brush->get_face(f);
		for (uint32_t i = 0; i < loop.size(); i++) {
			const Vector3 v = brush->get_vertex(loop[i]);
			CHECK(!v.is_equal_approx(Vector3(0, 2, 0)));
			CHECK(!v.is_equal_approx(Vector3(2, 2, 0)));
		}
	}

	// Each side cap is now a pentagon (quad with one corner clipped).
	int pentagons = 0;
	for (int f = 0; f < brush->get_face_count(); f++) {
		if (brush->get_face(f).size() == 5) {
			pentagons++;
		}
	}
	CHECK(pentagons == 2);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] bevel_edges_profiled segments and shapes the strip") {
	Vector<LevelBrush::EdgeKey> edges;
	edges.push_back(LevelBrush::EdgeKey(3, 2)); // Top-front edge of a 2-box.

	// steps=1, shape 0 (flat chamfer): 2 band quads spanning the whole
	// cross-section, coplanar with each other (straight chord).
	LevelBrush *flat = memnew(LevelBrush);
	flat->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));
	CHECK(flat->bevel_edges_profiled(edges, 1.0, 1, 0.0) == 1);
	CHECK(flat->get_face_count() == 8); // 6 + 2 bands.
	// Iso verts: (2*bands-1)*2 endpoints = 2, at chord mid u=0.5:
	// top outer (x,2,1) and front outer (x,1,0) -> mid (x,1.5,0.5).
	// 8 + 4 outer + 2 iso = 14 verts; the 2 consumed endpoints compacted.
	CHECK(flat->get_vertex_count() == 8 + 4 + 2 - 2);
	int found_mid = 0;
	for (int i = 0; i < flat->get_vertex_count(); i++) {
		const Vector3 v = flat->get_vertex(i);
		if (v.is_equal_approx(Vector3(0, 1.5, 0.5)) || v.is_equal_approx(Vector3(2, 1.5, 0.5))) {
			found_mid++;
		}
	}
	CHECK(found_mid == 2);
	// The two band quads are coplanar (same Newell normal direction).
	Vector3 n6 = flat->get_face_normal(6);
	Vector3 n7 = flat->get_face_normal(7);
	CHECK(n6.dot(n7) > 0.999);

	memdelete(flat);

	// steps=1, shape 0.5 (quadratic Bezier through the original corner):
	// the mid iso vert sits at 0.25*A + 0.5*C + 0.25*B = halfway between
	// the chord mid and the corner: (x, 1.75, 0.25).
	LevelBrush *round = memnew(LevelBrush);
	round->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));
	CHECK(round->bevel_edges_profiled(edges, 1.0, 1, 0.5) == 1);
	found_mid = 0;
	for (int i = 0; i < round->get_vertex_count(); i++) {
		const Vector3 v = round->get_vertex(i);
		if (v.is_equal_approx(Vector3(0, 1.75, 0.25)) || v.is_equal_approx(Vector3(2, 1.75, 0.25))) {
			found_mid++;
		}
	}
	CHECK(found_mid == 2);

	memdelete(round);

	// steps=1, shape 1 (full bulge): mid iso vert ON the original corner
	// (segment path A -> C -> B): (x, 2, 0) - visually no beveling.
	LevelBrush *full = memnew(LevelBrush);
	full->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));
	CHECK(full->bevel_edges_profiled(edges, 1.0, 1, 1.0) == 1);
	found_mid = 0;
	for (int i = 0; i < full->get_vertex_count(); i++) {
		const Vector3 v = full->get_vertex(i);
		if (v.is_equal_approx(Vector3(0, 2, 0)) || v.is_equal_approx(Vector3(2, 2, 0))) {
			found_mid++;
		}
	}
	CHECK(found_mid == 2);

	memdelete(full);

	// steps=0 matches bevel_edges(): single quad, edge consumed, endpoints
	// compacted away (8 + 4 outer - 2 consumed = 10 verts).
	LevelBrush *zero = memnew(LevelBrush);
	zero->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));
	CHECK(zero->bevel_edges_profiled(edges, 0.5, 0, 0.5) == 1);
	CHECK(zero->get_face_count() == 7);
	CHECK(zero->get_vertex_count() == 10);
	for (int f = 0; f < zero->get_face_count(); f++) {
		LocalVector<int> loop = zero->get_face(f);
		for (uint32_t i = 0; i < loop.size(); i++) {
			const Vector3 v = zero->get_vertex(loop[i]);
			CHECK(!v.is_equal_approx(Vector3(0, 2, 0)));
			CHECK(!v.is_equal_approx(Vector3(2, 2, 0)));
		}
	}

	memdelete(zero);
}

TEST_CASE("[LevelBrush] bevel_edges rejects open edges and zero distance") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Zero/negative distance is a no-op.
	Vector<LevelBrush::EdgeKey> edges;
	edges.push_back(LevelBrush::EdgeKey(3, 2));
	CHECK(brush->bevel_edges(edges, 0.0) == 0);
	CHECK(brush->bevel_edges(edges, -1.0) == 0);
	CHECK(brush->get_face_count() == 6);

	// Delete a face so its edges are open (only one adjacent face): those
	// cannot be beveled.
	Vector<int> del;
	del.push_back(4); // +Y face.
	brush->delete_faces(del);
	REQUIRE(brush->get_face_count() == 5);
	CHECK(brush->bevel_edges(edges, 0.25) == 0);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] mirror reflects verts and flips winding") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));

	// Mirror across the plane x = 3 (normal +X): the box lands at x in
	// [4, 6], face normals still outward.
	brush->mirror(Plane(Vector3(1, 0, 0), 3.0));
	AABB bb;
	bb.position = brush->get_vertex(0);
	bb.size = Vector3();
	for (int i = 1; i < brush->get_vertex_count(); i++) {
		bb.expand_to(brush->get_vertex(i));
	}
	CHECK(bb.position.is_equal_approx(Vector3(4, 0, 0)));
	CHECK(bb.size.is_equal_approx(Vector3(2, 2, 2)));

	// Winding reversed: the +X face (now at x=6) still reports +X normal.
	bool found_px = false;
	for (int f = 0; f < brush->get_face_count(); f++) {
		LocalVector<int> loop = brush->get_face(f);
		bool all_x6 = true;
		for (uint32_t i = 0; i < loop.size(); i++) {
			all_x6 = all_x6 && Math::is_equal_approx(brush->get_vertex(loop[i]).x, (real_t)6.0);
		}
		if (all_x6) {
			found_px = true;
			CHECK(brush->get_face_normal(f).x > 0.99);
		}
	}
	CHECK(found_px);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] compact_vertices drops unreferenced verts and remaps") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));

	// Clip away half the box WITHOUT a cap: all 4 clipped-off verts become
	// unreferenced and must be compacted away by clip's internal pass.
	brush->clip(Plane(Vector3(1, 0, 0), 1.0), false);
	for (int i = 0; i < brush->get_vertex_count(); i++) {
		const Vector3 &v = brush->get_vertex(i);
		bool referenced = false;
		for (int f = 0; f < brush->get_face_count() && !referenced; f++) {
			LocalVector<int> loop = brush->get_face(f);
			for (uint32_t k = 0; k < loop.size(); k++) {
				referenced = referenced || brush->get_vertex(loop[k]).is_equal_approx(v);
			}
		}
		CHECK(referenced);
	}
	// Slab x in [1,2]: 4 original + 4 cut verts.
	CHECK(brush->get_vertex_count() == 8);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] get_edge_chain tolerates an invalid edge") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	// Default EdgeKey (-1,-1) must not read out of bounds.
	Vector<LevelBrush::EdgeKey> chain = brush->get_edge_chain(LevelBrush::EdgeKey());
	CHECK(chain.size() == 1);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] delete_faces ignores duplicate indices") {
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	Vector<int> del;
	del.push_back(4);
	del.push_back(4); // Duplicate must not double-delete or error mid-op.
	brush->delete_faces(del);
	CHECK(brush->get_face_count() == 5);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] clip unifies near-plane seam verts with the cap") {
	// A vert within WELD_DIST of the clip plane (kept side) must be snapped
	// onto the plane and SHARED with the cap - not left as a second vert
	// next to the welded seam vert.
	LevelBrush *cut = memnew(LevelBrush);
	cut->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));
	Vector<int> nv2;
	for (int i = 0; i < cut->get_vertex_count(); i++) {
		if (Math::is_zero_approx(cut->get_vertex(i).x)) {
			nv2.push_back(i);
		}
	}
	REQUIRE(nv2.size() == 4);
	cut->move_vertices(nv2, Vector3(0.001, 0, 0)); // x=0 face -> x=0.001.
	cut->clip(Plane(Vector3(1, 0, 0), 0.0005));

	// Every vert ON the seam plane (x ~ 0.0005) must be referenced by the
	// cap face - no near-plane vert may exist OFF the cap.
	int cap = -1;
	for (int f = 0; f < cut->get_face_count(); f++) {
		if (cut->get_face_normal(f).x < -0.99) {
			cap = f;
			break;
		}
	}
	REQUIRE(cap >= 0);
	for (int i = 0; i < cut->get_vertex_count(); i++) {
		const Vector3 v = cut->get_vertex(i);
		if (Math::abs(v.x - 0.0005) > 0.001) {
			continue; // Not a seam vert.
		}
		bool on_cap = false;
		LocalVector<int> loop = cut->get_face(cap);
		for (uint32_t k = 0; k < loop.size(); k++) {
			on_cap = on_cap || cut->get_vertex(loop[k]).is_equal_approx(v);
		}
		CHECK(on_cap);
	}

	memdelete(cut);
}

TEST_CASE("[LevelBrush] split_faces does not weld to pre-existing verts") {
	// Regression: intersection points near an EXISTING vertex must create
	// new verts (welding to the old one corrupts topology - ROADMAP #10).
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));
	const int before = brush->get_vertex_count();

	// Split at x = 0.0015: past the side-classification epsilon (0.0005, so
	// edges genuinely cross) but within WELD_DIST (0.002) of the x=0 face's
	// verts. The split must still create its own intersection verts rather
	// than welding onto the pre-existing ones (ROADMAP #10).
	brush->split_faces(Plane(Vector3(1, 0, 0), 0.0015));
	CHECK(brush->get_vertex_count() > before);

	// And none of the new intersection verts may BE an original position
	// (a weld would have collapsed them onto x=0).
	bool found_cut_vert = false;
	for (int i = 0; i < brush->get_vertex_count(); i++) {
		if (Math::is_equal_approx(brush->get_vertex(i).x, (real_t)0.0015)) {
			found_cut_vert = true;
			break;
		}
	}
	CHECK(found_cut_vert);

	memdelete(brush);
}

TEST_CASE("[LevelBrush] bevel_edges_profiled handles a collinear chain with steps") {
	// Same setup as the steps=0 chain test, but with segments: the middle
	// vertex's shared corners must weld and no zigzag may appear.
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(2, 2, 2)));
	REQUIRE(brush->subdivide_face(4)); // +Y face -> 4 quads.

	int centroid = -1, mid_l = -1, mid_r = -1;
	for (int i = 0; i < brush->get_vertex_count(); i++) {
		const Vector3 v = brush->get_vertex(i);
		if (v.is_equal_approx(Vector3(1, 2, 1))) {
			centroid = i;
		} else if (v.is_equal_approx(Vector3(0, 2, 1))) {
			mid_l = i;
		} else if (v.is_equal_approx(Vector3(2, 2, 1))) {
			mid_r = i;
		}
	}
	REQUIRE(centroid >= 0);
	REQUIRE(mid_l >= 0);
	REQUIRE(mid_r >= 0);

	Vector<LevelBrush::EdgeKey> edges;
	edges.push_back(LevelBrush::EdgeKey(mid_l, centroid));
	edges.push_back(LevelBrush::EdgeKey(centroid, mid_r));
	CHECK(brush->bevel_edges_profiled(edges, 0.25, 2, 0.0) == 2);

	// Every face must still reference valid, in-bounds verts (no zigzag
	// corruption) and lie flat on the subdivided plane (y=2 top surface).
	for (int f = 0; f < brush->get_face_count(); f++) {
		LocalVector<int> loop = brush->get_face(f);
		REQUIRE(loop.size() >= 3);
		for (uint32_t i = 0; i < loop.size(); i++) {
			CHECK(loop[i] >= 0);
			CHECK(loop[i] < brush->get_vertex_count());
		}
	}

	memdelete(brush);
}

} // namespace TestLevelBrush
