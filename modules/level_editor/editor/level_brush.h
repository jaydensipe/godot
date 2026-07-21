/**************************************************************************/
/*  level_brush.h                                                         */
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

#include "core/math/plane.h"
#include "core/templates/hash_set.h"
#include "scene/3d/node_3d.h"

class Material;

// A convex solid defined by a set of planes, Hammer-style.
// All brush geometry (polygons, edges, vertices) is derived from plane
// intersections, in the brush's local space.
class LevelBrush : public Node3D {
	GDCLASS(LevelBrush, Node3D);

	Vector<Plane> planes;
	Vector<Ref<Material>> face_materials;

	static Vector3 _snap(const Vector3 &p_v);

public:
	static constexpr real_t SNAP_EPSILON = CMP_EPSILON;

	struct EdgeKey {
		Vector3 a;
		Vector3 b;

		EdgeKey() {}
		EdgeKey(const Vector3 &p_a, const Vector3 &p_b);

		bool operator==(const EdgeKey &p_other) const {
			return a == p_other.a && b == p_other.b;
		}
	};

	struct EdgeKeyHasher {
		static uint32_t hash(const EdgeKey &p_key);
	};

	int get_face_count() const { return planes.size(); }

	void set_face_plane(int p_face, const Plane &p_plane);
	Plane get_face_plane(int p_face) const;

	void set_face_material(int p_face, const Ref<Material> &p_material);
	Ref<Material> get_face_material(int p_face) const;

	// Serialized form.
	void set_planes_data(const PackedVector4Array &p_planes);
	PackedVector4Array get_planes_data() const;
	void set_face_materials_data(const Array &p_materials);
	Array get_face_materials_data() const;

	// Returns the local-space polygon for face p_face (deduplicated, wound
	// so it faces away from the solid). Empty if degenerate.
	Vector<Vector3> get_face_polygon(int p_face) const;

	// All unique edges and vertices of the convex solid (local space).
	HashSet<EdgeKey, EdgeKeyHasher> get_edges() const;
	Vector<Vector3> get_vertices() const;

	Vector3 get_center() const;
	bool is_valid() const { return !get_vertices().is_empty(); }

	// Plane-based ray test in local space; returns entry face or -1.
	int ray_intersect(const Vector3 &p_origin, const Vector3 &p_dir, real_t &r_dist) const;

	// Initialize as an axis-aligned box with the given local-space AABB.
	void setup_box(const AABB &p_aabb);

	// Deep copy (not added to any tree). Caller owns the result.
	LevelBrush *duplicate_brush() const;

	// Move the given faces along p_delta (any direction, local space).
	void translate_faces(const Vector<int> &p_faces, const Vector3 &p_delta);

	// Deformation move: recompute the planes of faces incident to each moved
	// element so the vertices/edges themselves move (normals tilt, Hammer-style).
	// p_deltas maps original snapped vertex position -> new local-space position.
	void move_elements(const Vector<Vector3> &p_vertices, const Vector<EdgeKey> &p_edges, const Vector<int> &p_faces, const Vector3 &p_delta);

	// Extrude faces outward along their normals (positive = expand).
	void extrude_faces(const Vector<int> &p_faces, real_t p_distance);
	void extrude_edge(const EdgeKey &p_edge, real_t p_distance);
	void extrude_vertex(const Vector3 &p_vertex, real_t p_distance);

	// Merge faces that ended up coplanar. Keeps the material of the lowest
	// merged face index.
	void merge_coplanar_faces();

	// Bake helpers. Local space; the caller applies transforms.
	void get_bake_surface_data(int p_face, Vector<Vector3> &r_vertices, Vector<Vector3> &r_normals, Vector<Vector2> &r_uvs) const;
	void get_collision_faces(Vector<Vector3> &r_faces) const;

	LevelBrush() {}
	virtual ~LevelBrush() {}

protected:
	static void _bind_methods();
};
