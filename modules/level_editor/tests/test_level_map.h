/**************************************************************************/
/*  test_level_map.h                                                      */
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

#include "../level_map.h"

#include "scene/3d/occluder_instance_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "scene/resources/material.h"
#include "tests/test_macros.h"

namespace TestLevelMap {

TEST_CASE("[SceneTree][LevelMap] bake produces mesh, collision, and occluder") {
	LevelMap *map = memnew(LevelMap);
	// bake() needs global transforms; a detached node's global == local.
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));
	map->add_child(brush);

	CHECK(map->get_brush_count() == 1);

	Node3D *baked = map->bake();
	REQUIRE(baked != nullptr);

	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(baked);
	REQUIRE(mi != nullptr);
	REQUIRE(mi->get_mesh().is_valid());

	// One material (default) -> one surface, 12 triangles.
	CHECK(mi->get_mesh()->get_surface_count() == 1);
	CHECK(mi->get_mesh()->surface_get_primitive_type(0) == Mesh::PRIMITIVE_TRIANGLES);

	// StaticBody3D with a concave shape.
	StaticBody3D *body = nullptr;
	OccluderInstance3D *occluder = nullptr;
	for (int i = 0; i < baked->get_child_count(); i++) {
		if (!body) {
			body = Object::cast_to<StaticBody3D>(baked->get_child(i));
		}
		if (!occluder) {
			occluder = Object::cast_to<OccluderInstance3D>(baked->get_child(i));
		}
	}
	REQUIRE(body != nullptr);
	REQUIRE(occluder != nullptr);
	REQUIRE(occluder->get_occluder().is_valid());

	CollisionShape3D *shape_node = Object::cast_to<CollisionShape3D>(body->get_child(0));
	REQUIRE(shape_node != nullptr);
	Ref<ConcavePolygonShape3D> concave = shape_node->get_shape();
	REQUIRE(concave.is_valid());
	CHECK(concave->get_faces().size() == 36);

	memdelete(baked);
	memdelete(map);
}

TEST_CASE("[SceneTree][LevelMap] bake groups faces per material") {
	LevelMap *map = memnew(LevelMap);
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));

	Ref<StandardMaterial3D> mat_a;
	mat_a.instantiate();
	Ref<StandardMaterial3D> mat_b;
	mat_b.instantiate();
	brush->set_face_material(0, mat_a);
	brush->set_face_material(1, mat_b);
	// Faces 2-5 fall back to the map default material.
	map->add_child(brush);

	Node3D *baked = map->bake();
	REQUIRE(baked != nullptr);
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(baked);
	REQUIRE(mi != nullptr);

	// mat_a, mat_b, default = 3 surfaces.
	CHECK(mi->get_mesh()->get_surface_count() == 3);

	bool has_a = false, has_b = false, has_default = false;
	for (int s = 0; s < mi->get_mesh()->get_surface_count(); s++) {
		Ref<Material> m = mi->get_mesh()->surface_get_material(s);
		has_a = has_a || (m == Ref<Material>(mat_a));
		has_b = has_b || (m == Ref<Material>(mat_b));
		has_default = has_default || (m == map->get_default_material());
	}
	CHECK(has_a);
	CHECK(has_b);
	CHECK(has_default);

	memdelete(baked);
	memdelete(map);
}

TEST_CASE("[SceneTree][LevelMap] brush transform is applied at bake time") {
	LevelMap *map = memnew(LevelMap);
	LevelBrush *brush = memnew(LevelBrush);
	brush->setup_box(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)));
	brush->set_position(Vector3(10, 0, 0));
	map->add_child(brush);

	Node3D *baked = map->bake();
	REQUIRE(baked != nullptr);
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(baked);
	REQUIRE(mi != nullptr);

	// The mesh's AABB is in map space and includes the brush offset.
	AABB bb = mi->get_mesh()->get_aabb();
	CHECK(bb.position.x == doctest::Approx(10.0));
	CHECK(bb.size.x == doctest::Approx(1.0));

	memdelete(baked);
	memdelete(map);
}

TEST_CASE("[SceneTree][LevelMap] empty map bakes to nothing") {
	LevelMap *map = memnew(LevelMap);
	CHECK(map->get_brush_count() == 0);
	CHECK(map->bake() == nullptr);
	memdelete(map);
}

} // namespace TestLevelMap
