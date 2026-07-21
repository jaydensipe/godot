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

#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"

class Material;

// A brush solid stored as explicit mesh topology (vertices + polygon faces)
// in local space. Faces are n-gons (wound outward) and may be non-planar;
// they are fan-triangulated for rendering, collision, and occluders.
//
// This is the Hammer/Blender-style data model: editing a vertex moves only
// that vertex; faces deform to fit.
class LevelBrush : public Node3D {
	GDCLASS(LevelBrush, Node3D);

public:
	struct EdgeKey {
		int a = -1;
		int b = -1;

		EdgeKey() {}
		EdgeKey(int p_a, int p_b) {
			if (p_a < p_b) {
				a = p_a;
				b = p_b;
			} else {
				a = p_b;
				b = p_a;
			}
		}

		bool operator==(const EdgeKey &p_other) const {
			return a == p_other.a && b == p_other.b;
		}
	};

	struct EdgeKeyHasher {
		static _FORCE_INLINE_ uint32_t hash(const EdgeKey &p_key) {
			uint32_t h = hash_murmur3_one_32((uint32_t)p_key.a);
			return hash_murmur3_one_32((uint32_t)p_key.b, h);
		}
	};

private:
	LocalVector<Vector3> verts;
	// Faces as index loops into verts, wound so they face outward.
	LocalVector<LocalVector<int>> faces;
	LocalVector<Ref<Material>> face_materials;

	void _update_face_count_storage();

public:
	int get_vertex_count() const { return (int)verts.size(); }
	Vector3 get_vertex(int p_index) const;
	void set_vertex(int p_index, const Vector3 &p_pos);

	int get_face_count() const { return (int)faces.size(); }
	LocalVector<int> get_face(int p_face) const;
	Vector3 get_face_normal(int p_face) const; // Newell's method, robust for n-gons.
	Vector3 get_face_center(int p_face) const;

	void set_face_material(int p_face, const Ref<Material> &p_material);
	Ref<Material> get_face_material(int p_face) const;

	// All unique edges as vertex-index pairs.
	HashSet<EdgeKey, EdgeKeyHasher> get_edges() const;

	Vector3 get_center() const;
	bool is_valid() const { return verts.size() >= 4 && !faces.is_empty(); }

	// Ray test against fan-triangulated faces. Returns face index or -1.
	// Ray is in local space.
	int ray_intersect(const Vector3 &p_origin, const Vector3 &p_dir, real_t &r_dist) const;

	// Initialize as an axis-aligned box (8 verts, 6 quads) with the given
	// local-space AABB.
	void setup_box(const AABB &p_aabb);

	// Move vertices directly (Blender-style). Only the given vertices move.
	void move_vertices(const Vector<int> &p_vertices, const Vector3 &p_delta);

	// Extrude a face: duplicates the face loop, offsets it along the face
	// normal, and stitches side quads. Returns the new cap face index (or -1).
	int extrude_face(int p_face, real_t p_distance);

	// Bake helpers (local space; caller applies transforms).
	void get_bake_surface_data(int p_face, Vector<Vector3> &r_vertices, Vector<Vector3> &r_normals, Vector<Vector2> &r_uvs) const;
	void get_collision_faces(Vector<Vector3> &r_faces) const;

	// Deep copy (not added to any tree). Caller owns the result.
	LevelBrush *duplicate_brush() const;

	// Serialized form.
	void set_vertices_data(const PackedVector3Array &p_verts);
	PackedVector3Array get_vertices_data() const;
	void set_faces_data(const Array &p_faces); // Array of PackedInt32Array.
	Array get_faces_data() const;
	void set_face_materials_data(const Array &p_materials);
	Array get_face_materials_data() const;

	LevelBrush() {}
	virtual ~LevelBrush() {}

protected:
	static void _bind_methods();
};
