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
	bool faces_flipped = false;

	// Cached edge sets (get_edges/get_open_edges). Rebuilding them scans every
	// face loop; the overlay does that per brush per viewport per repaint,
	// which tanks FPS during gizmo drags on dense brushes (e.g. a 64-side
	// sphere = ~6000 edges x 4 viewports per mouse-motion). Topology only
	// changes through _notify_map_changed(), so the cache is invalidated there.
	mutable bool edges_dirty = true;
	mutable HashSet<EdgeKey, EdgeKeyHasher> edges_cache;
	mutable HashSet<EdgeKey, EdgeKeyHasher> open_edges_cache;
	void _rebuild_edges_cache() const;

	void _update_face_count_storage();
	void _notify_map_changed();

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
	// Assign one material to every face.
	void set_all_face_materials(const Ref<Material> &p_material);

	// All unique edges as vertex-index pairs. Returns the cached set (rebuilt
	// lazily on geometry change) - do not hold across mutations.
	const HashSet<EdgeKey, EdgeKeyHasher> &get_edges() const;

	// Edges used by only one face (boundary of the brush surface - not
	// shared with an adjacent face). Same caching as get_edges().
	const HashSet<EdgeKey, EdgeKeyHasher> &get_open_edges() const;

	Vector3 get_center() const;
	bool is_valid() const { return verts.size() >= 4 && !faces.is_empty(); }

	// Ray test against fan-triangulated faces. Returns face index or -1.
	// Ray is in local space.
	int ray_intersect(const Vector3 &p_origin, const Vector3 &p_dir, real_t &r_dist) const;

	// Initialize as an axis-aligned box (8 verts, 6 quads) with the given
	// local-space AABB.
	void setup_box(const AABB &p_aabb);

	// Initialize as a single flat quad (4 verts, 1 face). The corners must be
	// wound CCW around the intended outward normal (same convention as
	// setup_box faces).
	void setup_quad(const Vector3 p_corners[4]);

	// Initialize as a convex sphere approximation inscribed in the given AABB:
	// p_sides vertices around the equator, half that many latitude rings, all
	// planar faces (top/bottom cap fans + quad bands), wound outward.
	// p_sides is clamped to [4, 64].
	void setup_sphere(const AABB &p_aabb, int p_sides = 16);

	// Move vertices directly (Blender-style). Only the given vertices move.
	void move_vertices(const Vector<int> &p_vertices, const Vector3 &p_delta);

	// Reverse every face's winding (normals point inward) - turns a solid
	// block into an interior room shell.
	void flip_faces();

	// Exported property: when true, faces are treated as flipped (interior).
	void set_faces_flipped(bool p_flipped);
	bool is_faces_flipped() const { return faces_flipped; }

	// Extrude a face: duplicates the face loop, offsets it along the face
	// normal, and stitches side quads. Returns the new cap face index (or -1).
	int extrude_face(int p_face, real_t p_distance);

	// Extrude an edge: duplicates the edge's two verts (offset by p_offset),
	// rewires every face using them to the duplicates, and stitches one new
	// quad per rewired face spanning old->new (the "pull" wall). Returns the
	// duplicated verts in r_new (same order: a, b), false if the edge is
	// invalid or unused.
	bool extrude_edge(const EdgeKey &p_edge, const Vector3 &p_offset, int r_new[2]);

	// Extrude a vertex: duplicates the vert (offset by p_offset), rewires
	// every face using it to the duplicate, and stitches one new triangle fan
	// wedge per rewired face (old neighbor verts + old vert + new vert).
	// Returns the new vertex index, or -1 if invalid/unused.
	int extrude_vertex(int p_vertex, const Vector3 &p_offset);

	// Re-orient a face loop so its normal points away from the brush
	// centroid (out of the solid). Used to keep extruded walls correctly
	// wound as a gizmo drag moves them (their winding is decided at
	// extrude time from a stub offset and can go stale - GOTCHAS #30).
	void rewind_face_outward(int p_face);

	// Subdivide a face: quads split into 4 quads via edge midpoints + centroid
	// (Hammer-style); n-gons fall back to a triangle fan from the centroid.
	// New faces inherit the face's material. Returns true on success.
	bool subdivide_face(int p_face);

	// Clip the brush with a plane (local space). Keeps the side the plane
	// normal points to. Adds a cap face on the cut unless p_add_cap is false
	// (used when splitting, where the two halves share the seam).
	void clip(const Plane &p_plane, bool p_add_cap = true);

	// Delete faces by index (any order; sorted internally).
	void delete_faces(const Vector<int> &p_faces);

	// Mirror the brush across a plane (local space): reflects all verts and
	// reverses every face's winding (reflection flips chirality - without
	// the reversal normals point inward). Materials unchanged.
	void mirror(const Plane &p_plane);

	// Drop verts not referenced by any face and remap face indices.
	// Geometry ops that replace verts in loops (clip, bevel, weld,
	// collapse) leave orphans; compacting keeps the serialized array from
	// growing monotonically and dead verts out of vertex picking.
	void compact_vertices();

	// Join (weld) vertices: all listed vertices are moved to their average
	// and merged into the first listed vertex. Degenerate faces are removed.
	void weld_vertices(const Vector<int> &p_vertices);

	// Bridge two edges with a new face. Returns the new face index, or -1 if
	// the edges share a vertex (can't bridge).
	int bridge_edges(const EdgeKey &p_edge_a, const EdgeKey &p_edge_b, const Ref<Material> &p_material);

	// Edge loop selection (Blender alt-click): starting from p_edge, walk both
	// directions across each face to the opposite edge, until returning to the
	// start or hitting a dead end (n-gon boundary / open topology). Returns
	// all edges in the loop, including p_edge.
	Vector<EdgeKey> get_edge_loop(const EdgeKey &p_edge) const;

	// Collinear edge chain: from p_edge, walk both directions through shared
	// vertices following only edges parallel to it (e.g. consecutive segments
	// of a subdivided straight edge). Returns all chain edges, incl. p_edge.
	Vector<EdgeKey> get_edge_chain(const EdgeKey &p_edge) const;

	// Bevel the given edges (Blender-style, width = p_distance, 1 segment):
	// each edge is consumed and replaced by one strip face bridging two
	// lines offset p_distance into each adjacent face (measured along the
	// boundary edges at the corners). Where multiple beveled edges meet at
	// a vertex, their offset lines are mitred into one shared corner point
	// per face; collinear chains (e.g. both halves of a subdivided edge)
	// produce one continuous strip. Returns the number of edges beveled.
	int bevel_edges(const Vector<EdgeKey> &p_edges, real_t p_distance);

	// Full bevel with segments + profile (dock Bevel action). p_steps is
	// the number of SEGMENTS MINUS ONE: 0 consumes the edge into one flat
	// strip quad; N >= 1 subdivides the strip cross-section into 2N band
	// quads following the p_shape profile: 0 = flat chamfer, 0.5 =
	// quadratic-Bezier round-over (apex approaches but never reaches the
	// original corner), 1 = full bulge back to the original corner
	// (visually no beveling).
	int bevel_edges_profiled(const Vector<EdgeKey> &p_edges, real_t p_width, int p_steps, real_t p_shape);

	// Collapse vertices: moves each to the average position of its edge
	// neighbors and welds duplicates. Degenerate faces (< 3 unique verts)
	// are removed.
	void collapse_vertices(const Vector<int> &p_vertices);

	// Split faces along the plane without clipping the solid: crossing faces
	// become two faces sharing the cut edge. No caps, no seam - the brush
	// stays whole, just with faces subdivided along the line.
	void split_faces(const Plane &p_plane);

	// Clip keeping both sides: returns the back-side brush (caller owns it),
	// this brush keeps the front side.
	LevelBrush *clip_split(const Plane &p_plane);

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
	void _notification(int p_what);
};
