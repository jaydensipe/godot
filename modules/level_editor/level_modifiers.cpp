/**************************************************************************/
/*  level_modifiers.cpp                                                   */
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

// Topology modifiers for LevelBrush: clip, split, extrude, subdivide,
// weld, collapse, bridge, mirror, compact, bevel, edge loop/chain walks,
// delete. Implementations split out of level_brush.cpp.

#include "level_brush.h"
#include "level_constants.h"

#include "core/math/math_defs.h"
#include "core/templates/hash_map.h"
#include "scene/resources/material.h"

Vector<LevelBrush::EdgeKey> LevelBrush::get_edge_loop(const EdgeKey &p_edge) const {
	Vector<EdgeKey> loop;

	// Faces using each vertex (adjacency for finding the face on the other
	// side of an edge).
	auto faces_with_edge = [&](const EdgeKey &e, int &r_a, int &r_b) {
		r_a = -1;
		r_b = -1;
		for (uint32_t f = 0; f < faces.size(); f++) {
			const LocalVector<int> &l = faces[f];
			const uint32_t n = l.size();
			for (uint32_t i = 0; i < n; i++) {
				int ia = l[i];
				int ib = l[(i + 1) % n];
				if ((ia == e.a && ib == e.b) || (ia == e.b && ib == e.a)) {
					if (r_a < 0) {
						r_a = (int)f;
					} else {
						r_b = (int)f;
						return;
					}
				}
			}
		}
	};

	// Opposite edge of p_edge within face p_face (quads only; -1 verts =
	// no well-defined opposite -> loop stops at n-gons).
	auto opposite_edge = [&](int p_face, const EdgeKey &e) -> EdgeKey {
		const LocalVector<int> &l = faces[p_face];
		if (l.size() != 4) {
			return EdgeKey(-1, -1);
		}
		for (uint32_t i = 0; i < 4; i++) {
			int ia = l[i];
			int ib = l[(i + 1) % 4];
			if ((ia == e.a && ib == e.b) || (ia == e.b && ib == e.a)) {
				int oa = l[(i + 2) % 4];
				int ob = l[(i + 3) % 4];
				return EdgeKey(oa, ob);
			}
		}
		return EdgeKey(-1, -1);
	};

	// Walk one direction: from p_edge across p_start_face, following opposite
	// edges until returning to p_edge, dead-ending, or hitting an edge the
	// other direction already collected (closed rings meet in the middle).
	auto walk = [&](const EdgeKey &p_start_edge, int p_start_face, HashSet<EdgeKey, EdgeKeyHasher> &r_visited, Vector<EdgeKey> &r_out) {
		EdgeKey cur = p_start_edge;
		int face = p_start_face;
		while (face >= 0) {
			EdgeKey next = opposite_edge(face, cur);
			if (next.a < 0 || next == p_start_edge || r_visited.has(next)) {
				break;
			}
			r_out.push_back(next);
			r_visited.insert(next);
			// Continue across the face on the OTHER side of `next`.
			int fa, fb;
			faces_with_edge(next, fa, fb);
			face = (fa == face) ? fb : fa;
			cur = next;
		}
	};

	int fa, fb;
	faces_with_edge(p_edge, fa, fb);
	HashSet<EdgeKey, EdgeKeyHasher> visited;
	visited.insert(p_edge);
	Vector<EdgeKey> forward;
	walk(p_edge, fa, visited, forward);
	Vector<EdgeKey> backward;
	walk(p_edge, fb, visited, backward);

	// Assemble: backward (reversed) + start + forward.
	for (int i = backward.size() - 1; i >= 0; i--) {
		loop.push_back(backward[i]);
	}
	loop.push_back(p_edge);
	for (const EdgeKey &e : forward) {
		loop.push_back(e);
	}
	return loop;
}

Vector<LevelBrush::EdgeKey> LevelBrush::get_edge_chain(const EdgeKey &p_edge) const {
	Vector<EdgeKey> chain;
	if (p_edge.a < 0 || p_edge.a >= (int)verts.size() || p_edge.b < 0 || p_edge.b >= (int)verts.size()) {
		chain.push_back(p_edge);
		return chain;
	}
	chain.push_back(p_edge);

	const Vector3 dir = (verts[p_edge.b] - verts[p_edge.a]).normalized();
	const real_t PARALLEL_DOT = LevelBrushConstants::PARALLEL_DOT;

	// All edges touching a vertex.
	auto edges_at = [&](int p_vert, Vector<EdgeKey> &r_out) {
		for (uint32_t f = 0; f < faces.size(); f++) {
			const LocalVector<int> &l = faces[f];
			const uint32_t n = l.size();
			for (uint32_t i = 0; i < n; i++) {
				int ia = l[i];
				int ib = l[(i + 1) % n];
				if (ia == p_vert || ib == p_vert) {
					EdgeKey e(ia, ib);
					bool seen = false;
					for (const EdgeKey &x : r_out) {
						if (x == e) {
							seen = true;
							break;
						}
					}
					if (!seen) {
						r_out.push_back(e);
					}
				}
			}
		}
	};

	HashSet<EdgeKey, EdgeKeyHasher> visited;
	visited.insert(p_edge);

	// Walk one endpoint direction at a time.
	for (int pass = 0; pass < 2; pass++) {
		int tip = (pass == 0) ? p_edge.a : p_edge.b;
		Vector<EdgeKey> side;
		while (true) {
			Vector<EdgeKey> touching;
			edges_at(tip, touching);
			EdgeKey best(-1, -1);
			for (const EdgeKey &cand : touching) {
				if (visited.has(cand)) {
					continue;
				}
				Vector3 cd = (verts[cand.b] - verts[cand.a]).normalized();
				if (Math::abs(cd.dot(dir)) > PARALLEL_DOT) {
					best = cand;
					break; // At most one collinear continuation at a clean vertex.
				}
			}
			if (best.a < 0) {
				break;
			}
			side.push_back(best);
			visited.insert(best);
			tip = (best.a == tip) ? best.b : best.a;
		}
		if (pass == 0) {
			// Prepend the a-side in reverse walk order.
			for (int i = side.size() - 1; i >= 0; i--) {
				chain.push_back(side[i]);
			}
		} else {
			for (const EdgeKey &e : side) {
				chain.push_back(e);
			}
		}
	}
	return chain;
}

void LevelBrush::mirror(const Plane &p_plane) {
	for (uint32_t i = 0; i < verts.size(); i++) {
		// Reflect: p' = p - 2 * distance_to(p) * normal.
		verts[i] -= p_plane.normal * (2.0 * p_plane.distance_to(verts[i]));
	}
	for (uint32_t f = 0; f < faces.size(); f++) {
		LocalVector<int> &l = faces[f];
		LocalVector<int> rev;
		for (int i = (int)l.size() - 1; i >= 0; i--) {
			rev.push_back(l[i]);
		}
		l = rev;
	}
	_notify_map_changed();
}

void LevelBrush::compact_vertices() {
	LocalVector<int> remap;
	remap.resize(verts.size());
	for (uint32_t i = 0; i < remap.size(); i++) {
		remap[i] = -1;
	}
	for (uint32_t f = 0; f < faces.size(); f++) {
		LocalVector<int> &l = faces[f];
		for (uint32_t i = 0; i < l.size(); i++) {
			remap[l[i]] = 0; // Mark referenced.
		}
	}
	int next = 0;
	LocalVector<Vector3> new_verts;
	for (uint32_t i = 0; i < verts.size(); i++) {
		if (remap[i] >= 0) {
			remap[i] = next++;
			new_verts.push_back(verts[i]);
		}
	}
	if (next == (int)verts.size()) {
		return; // Nothing orphaned.
	}
	for (uint32_t f = 0; f < faces.size(); f++) {
		LocalVector<int> &l = faces[f];
		for (uint32_t i = 0; i < l.size(); i++) {
			l[i] = remap[l[i]];
		}
	}
	verts = new_verts;
}

void LevelBrush::clip(const Plane &p_plane, bool p_add_cap) {
	// Solid clip: keep only the front side, capping the cut.
	const real_t eps = LevelBrushConstants::PLANE_EPSILON;

	LocalVector<LocalVector<int>> new_faces;
	LocalVector<Ref<Material>> new_mats;
	Vector<int> cap_verts;

	auto weld = [&](const Vector3 &p) -> int {
		for (int idx : cap_verts) {
			if (verts[idx].distance_to(p) < LevelBrushConstants::WELD_DIST) {
				return idx;
			}
		}
		verts.push_back(p);
		int idx = (int)verts.size() - 1;
		cap_verts.push_back(idx);
		return idx;
	};

	for (uint32_t f = 0; f < faces.size(); f++) {
		const LocalVector<int> &loop = faces[f];
		LocalVector<int> out;
		const uint32_t n = loop.size();
		for (uint32_t i = 0; i < n; i++) {
			int ia = loop[i];
			int ib = loop[(i + 1) % n];
			const Vector3 &a = verts[ia];
			const Vector3 &b = verts[ib];
			real_t da = p_plane.distance_to(a);
			real_t db = p_plane.distance_to(b);
			bool in_a = da >= -eps;
			bool in_b = db >= -eps;

			if (in_a) {
				if (da < LevelBrushConstants::WELD_DIST) {
					// On/near the plane: snap onto it and share the seam vert
					// with the cap - otherwise this original vert and the
					// welded intersection vert from a crossing edge coexist
					// within epsilon as two distinct seam verts.
					out.push_back(weld(a - p_plane.normal * da));
				} else {
					out.push_back(ia);
				}
			}
			if (in_a != in_b) {
				real_t t = da / (da - db);
				Vector3 hit = a + (b - a) * t;
				out.push_back(weld(hit));
			}
		}
		// Drop consecutive duplicates (two near-plane corners can snap to
		// the same seam vert) and the wrap-around duplicate.
		LocalVector<int> clean;
		for (uint32_t i = 0; i < out.size(); i++) {
			if (clean.is_empty() || clean[clean.size() - 1] != out[i]) {
				clean.push_back(out[i]);
			}
		}
		if (clean.size() > 1 && clean[0] == clean[clean.size() - 1]) {
			clean.remove_at(clean.size() - 1);
		}
		if (clean.size() >= 3) {
			new_faces.push_back(LocalVector<int>(clean));
			new_mats.push_back(f < face_materials.size() ? face_materials[f] : Ref<Material>());
		}
	}

	faces = new_faces;
	face_materials = new_mats;

	if (p_add_cap && cap_verts.size() >= 3) {
		Vector3 center;
		for (int idx : cap_verts) {
			center += verts[idx];
		}
		center /= (real_t)cap_verts.size();

		Vector3 n = -p_plane.normal;
		Vector3 axis_u = (verts[cap_verts[0]] - center).normalized();
		Vector3 axis_v = n.cross(axis_u).normalized();

		struct Item {
			real_t angle;
			int idx;
			bool operator<(const Item &o) const { return angle < o.angle; }
		};
		Vector<Item> items;
		for (int idx : cap_verts) {
			Vector3 d = verts[idx] - center;
			items.push_back({ Math::atan2(d.dot(axis_v), d.dot(axis_u)), idx });
		}
		items.sort();

		LocalVector<int> cap;
		for (const Item &it : items) {
			cap.push_back(it.idx);
		}
		faces.push_back(LocalVector<int>(cap));
		face_materials.push_back(Ref<Material>());
	}
	compact_vertices();
	_notify_map_changed();
}

void LevelBrush::delete_faces(const Vector<int> &p_faces) {
	Vector<int> sorted = p_faces;
	sorted.sort();
	// Dedup: a repeated index would shift after the first removal and
	// either delete the wrong face or trip the bounds check mid-op.
	for (int i = sorted.size() - 2; i >= 0; i--) {
		if (sorted[i] == sorted[i + 1]) {
			sorted.remove_at(i + 1);
		}
	}
	for (int i = sorted.size() - 1; i >= 0; i--) {
		int f = sorted[i];
		ERR_FAIL_INDEX(f, (int)faces.size());
		faces.remove_at(f);
		if (f < (int)face_materials.size()) {
			face_materials.remove_at(f);
		}
	}
	_notify_map_changed();
}

void LevelBrush::weld_vertices(const Vector<int> &p_vertices) {
	if (p_vertices.size() < 2) {
		return;
	}
	for (int v : p_vertices) {
		ERR_FAIL_INDEX(v, (int)verts.size());
	}

	// Average position of all listed vertices.
	Vector3 avg;
	for (int v : p_vertices) {
		avg += verts[v];
	}
	avg /= (real_t)p_vertices.size();

	int keep = p_vertices[0];
	verts[keep] = avg;

	// Remap all other indices to the kept one in every face.
	HashSet<int> merged;
	for (int i = 1; i < p_vertices.size(); i++) {
		merged.insert(p_vertices[i]);
	}
	for (uint32_t f = 0; f < faces.size(); f++) {
		LocalVector<int> &loop = faces[f];
		for (uint32_t i = 0; i < loop.size(); i++) {
			if (merged.has(loop[i])) {
				loop[i] = keep;
			}
		}
	}

	// Remove consecutive duplicates and degenerate faces.
	for (int f = (int)faces.size() - 1; f >= 0; f--) {
		LocalVector<int> &loop = faces[f];
		LocalVector<int> clean;
		for (uint32_t i = 0; i < loop.size(); i++) {
			if (clean.is_empty() || clean[clean.size() - 1] != loop[i]) {
				clean.push_back(loop[i]);
			}
		}
		// Also check wrap-around dup (first == last).
		if (clean.size() > 1 && clean[0] == clean[clean.size() - 1]) {
			clean.remove_at(clean.size() - 1);
		}
		HashSet<int> unique;
		for (int idx : clean) {
			unique.insert(idx);
		}
		if (unique.size() < 3) {
			faces.remove_at(f);
			if (f < (int)face_materials.size()) {
				face_materials.remove_at(f);
			}
		} else {
			loop = clean;
		}
	}
	compact_vertices();
	_notify_map_changed();
}

int LevelBrush::bridge_edges(const EdgeKey &p_edge_a, const EdgeKey &p_edge_b, const Ref<Material> &p_material) {
	// Need two distinct edges with 4 unique endpoints.
	if (p_edge_a == p_edge_b) {
		return -1;
	}
	if (p_edge_a.a == p_edge_b.a || p_edge_a.a == p_edge_b.b || p_edge_a.b == p_edge_b.a || p_edge_a.b == p_edge_b.b) {
		return -1; // Shared vertex - can't form a clean quad.
	}

	int quad[4] = { p_edge_a.a, p_edge_a.b, p_edge_b.b, p_edge_b.a };

	// Check winding against the brush: the new face's normal should point
	// away from the brush centroid.
	Vector3 centroid = get_center();
	Vector3 quad_center;
	for (int i = 0; i < 4; i++) {
		quad_center += verts[quad[i]];
	}
	quad_center *= 0.25;

	Vector3 e1 = verts[quad[1]] - verts[quad[0]];
	Vector3 e2 = verts[quad[3]] - verts[quad[0]];
	Vector3 n = e1.cross(e2);
	if (n.dot(quad_center - centroid) < 0) {
		// Flip winding to face outward.
		SWAP(quad[1], quad[3]);
	}

	LocalVector<int> face;
	for (int i = 0; i < 4; i++) {
		face.push_back(quad[i]);
	}
	faces.push_back(LocalVector<int>(face));
	face_materials.push_back(p_material);
	_notify_map_changed();
	return (int)faces.size() - 1;
}

void LevelBrush::collapse_vertices(const Vector<int> &p_vertices) {
	if (p_vertices.is_empty()) {
		return;
	}

	// Move each target vertex to the average of its edge-connected neighbors
	// (excluding neighbors that are also being collapsed).
	HashSet<int> targets;
	for (int v : p_vertices) {
		targets.insert(v);
	}

	for (int v : p_vertices) {
		ERR_FAIL_INDEX(v, (int)verts.size());
		Vector3 sum;
		int count = 0;
		for (uint32_t f = 0; f < faces.size(); f++) {
			const LocalVector<int> &loop = faces[f];
			for (uint32_t i = 0; i < loop.size(); i++) {
				if (loop[i] == v) {
					int prev = loop[(i + loop.size() - 1) % loop.size()];
					int next = loop[(i + 1) % loop.size()];
					if (!targets.has(prev)) {
						sum += verts[prev];
						count++;
					}
					if (!targets.has(next)) {
						sum += verts[next];
						count++;
					}
				}
			}
		}
		if (count > 0) {
			verts[v] = sum / (real_t)count;
		}
	}

	// Weld duplicates: remap face indices of collapsed verts to their
	// position-matching survivors.
	for (uint32_t f = 0; f < faces.size(); f++) {
		LocalVector<int> &loop = faces[f];
		for (uint32_t i = 0; i < loop.size(); i++) {
			if (!targets.has(loop[i])) {
				continue;
			}
			// Find a non-target vertex at the same position.
			for (uint32_t vi = 0; vi < verts.size(); vi++) {
				if (targets.has((int)vi) || (int)vi == loop[i]) {
					continue;
				}
				if (verts[vi].distance_to(verts[loop[i]]) < LevelBrushConstants::WELD_DIST) {
					loop[i] = (int)vi;
					break;
				}
			}
		}
	}

	// Remove degenerate faces (fewer than 3 unique vertices).
	for (int f = (int)faces.size() - 1; f >= 0; f--) {
		HashSet<int> unique;
		for (int idx : faces[f]) {
			unique.insert(idx);
		}
		if (unique.size() < 3) {
			faces.remove_at(f);
			if (f < (int)face_materials.size()) {
				face_materials.remove_at(f);
			}
		}
	}
	compact_vertices();
	_notify_map_changed();
}

void LevelBrush::split_faces(const Plane &p_plane) {
	// Split each face polygon along the plane. Faces fully on one side are
	// untouched; crossing faces become two faces sharing the cut edge. The
	// solid is not clipped and no caps are created - both halves remain part
	// of this brush.
	const real_t eps = LevelBrushConstants::PLANE_EPSILON;

	LocalVector<LocalVector<int>> new_faces;
	LocalVector<Ref<Material>> new_mats;

	// Intersection verts created by THIS split; welding is only valid against
	// these (welding to a pre-existing nearby vertex would corrupt topology).
	const uint32_t first_new_vert = verts.size();

	for (uint32_t f = 0; f < faces.size(); f++) {
		const LocalVector<int> &loop = faces[f];
		const uint32_t n = loop.size();
		if (n < 3) {
			continue;
		}

		// Classify vertices.
		LocalVector<real_t> dist;
		dist.resize(n);
		int front = 0, back = 0;
		for (uint32_t i = 0; i < n; i++) {
			dist[i] = p_plane.distance_to(verts[loop[i]]);
			if (dist[i] > eps) {
				front++;
			} else if (dist[i] < -eps) {
				back++;
			}
		}

		Ref<Material> mat = f < face_materials.size() ? face_materials[f] : Ref<Material>();

		if (front == 0 || back == 0) {
			// Entirely on one side (or on the plane): keep as-is.
			new_faces.push_back(LocalVector<int>(loop));
			new_mats.push_back(mat);
			continue;
		}

		// Split into front and back polygons (Sutherland-Hodgman both ways).
		auto clip_side = [&](bool keep_front) -> LocalVector<int> {
			LocalVector<int> out;
			for (uint32_t i = 0; i < n; i++) {
				uint32_t i2 = (i + 1) % n;
				int ia = loop[i];
				int ib = loop[i2];
				// dist is indexed by loop position, not vertex index.
				real_t da = keep_front ? dist[i] : -dist[i];
				real_t db = keep_front ? dist[i2] : -dist[i2];
				bool in_a = da >= -eps;
				bool in_b = db >= -eps;
				if (in_a) {
					out.push_back(ia);
				}
				if (in_a != in_b) {
					real_t t = da / (da - db);
					Vector3 hit = verts[ia] + (verts[ib] - verts[ia]) * t;
					// Weld against intersection verts created by this split.
					int found = -1;
					for (uint32_t vi = first_new_vert; vi < verts.size(); vi++) {
						if (verts[vi].distance_to(hit) < LevelBrushConstants::WELD_DIST) {
							found = (int)vi;
							break;
						}
					}
					if (found == -1) {
						verts.push_back(hit);
						found = (int)verts.size() - 1;
					}
					out.push_back(found);
				}
			}
			return out;
		};

		LocalVector<int> front_poly = clip_side(true);
		LocalVector<int> back_poly = clip_side(false);

		if (front_poly.size() >= 3) {
			new_faces.push_back(LocalVector<int>(front_poly));
			new_mats.push_back(mat);
		}
		if (back_poly.size() >= 3) {
			new_faces.push_back(LocalVector<int>(back_poly));
			new_mats.push_back(mat);
		}
	}

	faces = new_faces;
	face_materials = new_mats;
	_notify_map_changed();
}

LevelBrush *LevelBrush::clip_split(const Plane &p_plane) {
	LevelBrush *back = duplicate_brush();
	back->clip(-p_plane);
	clip(p_plane);
	return back;
}

void LevelBrush::move_vertices(const Vector<int> &p_vertices, const Vector3 &p_delta) {
	if (p_delta.is_zero_approx()) {
		return;
	}
	for (int idx : p_vertices) {
		ERR_FAIL_INDEX(idx, (int)verts.size());
		verts[idx] += p_delta;
	}
	_notify_map_changed();
}

int LevelBrush::extrude_face(int p_face, real_t p_distance) {
	ERR_FAIL_INDEX_V(p_face, (int)faces.size(), -1);
	if (Math::is_zero_approx(p_distance)) {
		return -1;
	}

	LocalVector<int> src;
	src = faces[p_face];
	const uint32_t n = src.size();
	if (n < 3) {
		return -1;
	}

	Ref<Material> cap_mat;
	if (p_face < (int)face_materials.size()) {
		cap_mat = face_materials[p_face];
	}

	const Vector3 normal = get_face_normal(p_face);
	const Vector3 offset = normal * p_distance;

	// Duplicate the loop, offset along the normal.
	LocalVector<int> cap;
	cap.resize(n);
	const uint32_t base = verts.size();
	for (uint32_t i = 0; i < n; i++) {
		verts.push_back(verts[src[i]] + offset);
		cap[i] = (int)(base + i);
	}

	// Replace the source face with the cap. The cap keeps the source winding
	// regardless of extrude direction - it remains the brush's outer boundary
	// on that side either way.
	faces[p_face] = cap;

	// Append side quads stitching the source loop to the cap (wound outward).
	// For negative (inward) extrudes the wall order must swap to keep normals
	// pointing out of the solid.
	for (uint32_t i = 0; i < n; i++) {
		uint32_t j = (i + 1) % n;
		if (p_distance > 0) {
			faces.push_back({ src[i], src[j], (int)(base + j), (int)(base + i) });
		} else {
			faces.push_back({ src[i], (int)(base + i), (int)(base + j), src[j] });
		}
	}

	// Materials: cap keeps the source material; sides get default (null).
	_update_face_count_storage();
	face_materials[p_face] = cap_mat;
	_notify_map_changed();

	return p_face;
}

bool LevelBrush::subdivide_face(int p_face) {
	ERR_FAIL_INDEX_V(p_face, (int)faces.size(), false);

	LocalVector<int> src;
	src = faces[p_face];
	const uint32_t n = src.size();
	if (n < 3) {
		return false;
	}

	Ref<Material> mat;
	if (p_face < (int)face_materials.size()) {
		mat = face_materials[p_face];
	}

	// New vertex at the face's centroid (shared by both split styles).
	Vector3 center;
	for (uint32_t i = 0; i < n; i++) {
		center += verts[src[i]];
	}
	center /= (real_t)n;
	const int ci = (int)verts.size();
	verts.push_back(center);

	if (n == 4) {
		// Hammer-style quad grid: edge midpoints + centroid, 4 quads keeping
		// the source winding (corner, next midpoint, centroid, prev midpoint).
		int mid[4];
		for (int i = 0; i < 4; i++) {
			uint32_t j = (i + 1) % 4;
			mid[i] = (int)verts.size();

			verts.push_back((verts[src[i]] + verts[src[j]]) * 0.5);
		}

		LocalVector<int> q0 = { src[0], mid[0], ci, mid[3] };
		faces[p_face] = q0;
		faces.push_back({ src[1], mid[1], ci, mid[0] });
		faces.push_back({ src[2], mid[2], ci, mid[1] });
		faces.push_back({ src[3], mid[3], ci, mid[2] });

		// Split the shared boundary edges in the NEIGHBORING faces too:
		// any other face whose loop contains src[i] -> src[j] (or the
		// reverse) consecutively must pass through mid[i] - otherwise the
		// midpoint is a T-junction (render cracks, broken edge adjacency
		// for bevel/loop walks).
		const uint32_t first_new = (uint32_t)faces.size() - 3; // New sub-quads
		// (q0 replaced p_face in place; the other 3 were appended).
		for (uint32_t f = 0; f < faces.size(); f++) {
			if ((int)f == p_face || f >= first_new) {
				continue; // Sub-quads already reference the midpoints.
			}
			LocalVector<int> &l = faces[f];
			for (int i = 0; i < 4; i++) {
				const int a = src[i];
				const int b = src[(i + 1) % 4];
				for (uint32_t k = 0; k < l.size(); k++) {
					const int ia = l[k];
					const int ib = l[(k + 1) % l.size()];
					if ((ia == a && ib == b) || (ia == b && ib == a)) {
						l.insert(k + 1, mid[i]);
						break; // One insertion per neighbor edge run.
					}
				}
			}
		}
	} else {
		// N-gon fallback: one triangle per edge, fanning from the centroid.
		LocalVector<int> t0 = { src[0], src[1], ci };
		faces[p_face] = t0;
		for (uint32_t i = 1; i < n; i++) {
			uint32_t j = (i + 1) % n;
			faces.push_back({ src[i], src[j], ci });
		}
	}

	_update_face_count_storage();
	// All new faces inherit the source material.
	for (uint32_t i = p_face; i < faces.size(); i++) {
		face_materials[i] = mat;
	}
	_notify_map_changed();

	return true;
}

int LevelBrush::bevel_edges(const Vector<EdgeKey> &p_edges, real_t p_distance) {
	return bevel_edges_profiled(p_edges, p_distance, 0, 0.5);
}

int LevelBrush::bevel_edges_profiled(const Vector<EdgeKey> &p_edges, real_t p_width, int p_steps, real_t p_shape) {
	if (p_width <= CMP_EPSILON) {
		return 0;
	}
	const bool single_cut = (p_steps <= 0);
	const int bands = MAX(p_steps, 1); // Band quads per side (steps >= 1).

	// Blender-style bevel (width = p_width):
	//  - The edge is CONSUMED; every adjacent face gets an offset line at
	//    distance p_width (measured along the boundary edges).
	//  - p_steps == 0 (single cut): ONE strip quad bridges the offset lines.
	//  - p_steps >= 1: the strip cross-section (outer line A -> outer line
	//    B) is subdivided into 2*p_steps band quads whose iso verts follow
	//    the p_shape profile: 0 = straight chord (flat chamfer), 0.5 =
	//    quadratic Bezier through the original corner (round-over; apex
	//    approaches but never reaches the corner), 1 = the original face
	//    segments (full bulge, visually no beveling).
	//  - Faces touching an endpoint are trimmed along the same profile
	//    polyline (polyline inserted into the end cap's loop).
	//  - Where several beveled edges meet at a vertex, their outer offset
	//    lines in a shared face are intersected (mitred) into ONE corner
	//    point, so strips join cleanly instead of crossing. Collinear
	//    continuations share the point naturally.
	//
	// Pass 1 gathers topology (all reads on the ORIGINAL loops), pass 2
	// creates corner verts, pass 3 rewrites loops and emits strips. All
	// corner positions are computed from pre-mutation data, so multi-edge
	// selections can't poison each other.
	const real_t shape_t = CLAMP(p_shape, 0.0, 1.0);

	struct EdgeInfo {
		int adj[2] = { -1, -1 };
	};
	struct CornerInfo {
		Vector3 pos;
		int new_vert = -1;
	};

	auto corner_key = [](int p_vert, int p_face) {
		return ((int64_t)p_vert << 32) | (uint32_t)p_face;
	};

	// --- Pass 1: gather.
	LocalVector<EdgeInfo> einfo;
	einfo.resize(p_edges.size());
	// corner key -> list of beveled-edge directions (unit, along each edge,
	// arbitrary sign) incident at that (vertex, face) corner.
	HashMap<int64_t, LocalVector<Vector3>> corner_dirs;
	HashMap<int64_t, bool> corner_ok; // false = unbevelable corner seen.

	for (int ei = 0; ei < p_edges.size(); ei++) {
		const EdgeKey &edge = p_edges[ei];
		int adj_count = 0;
		for (uint32_t f = 0; f < faces.size() && adj_count < 2; f++) {
			const LocalVector<int> &l = faces[f];
			const uint32_t n = l.size();
			for (uint32_t i = 0; i < n; i++) {
				int ia = l[i];
				int ib = l[(i + 1) % n];
				if ((ia == edge.a && ib == edge.b) || (ia == edge.b && ib == edge.a)) {
					einfo[ei].adj[adj_count++] = (int)f;
					break;
				}
			}
		}
		if (adj_count != 2) {
			continue; // Open boundary or degenerate - can't bevel.
		}
		const Vector3 dir = (verts[edge.b] - verts[edge.a]).normalized();
		for (int s = 0; s < 2; s++) {
			corner_dirs[corner_key(edge.a, einfo[ei].adj[s])].push_back(dir);
			corner_dirs[corner_key(edge.b, einfo[ei].adj[s])].push_back(dir);
		}
	}

	// --- Pass 2: resolve one offset position per corner.
	HashMap<int64_t, CornerInfo> corners;
	for (KeyValue<int64_t, LocalVector<Vector3>> &kv : corner_dirs) {
		const int vert = (int)(kv.key >> 32);
		const int face = (int)(uint32_t)(kv.key & 0xFFFFFFFF);
		const LocalVector<int> &l = faces[face];
		const uint32_t n = l.size();
		// Locate the vertex in the loop (first occurrence is fine: a face
		// visits a boundary vertex once in sane topology).
		int pos = -1;
		for (uint32_t i = 0; i < n; i++) {
			if (l[i] == vert) {
				pos = (int)i;
				break;
			}
		}
		if (pos < 0) {
			continue;
		}
		const Vector3 &vp = verts[vert];
		const Vector3 to_prev = (verts[l[(pos + n - 1) % n]] - vp).normalized();
		const Vector3 to_next = (verts[l[(pos + 1) % n]] - vp).normalized();
		const LocalVector<Vector3> &dirs = kv.value;

		// Which of the corner's two boundary runs are beveled? A run is
		// beveled if its direction is parallel to any beveled-edge direction
		// recorded at this corner.
		auto run_beveled = [&](const Vector3 &p_run) {
			for (uint32_t k = 0; k < dirs.size(); k++) {
				if (Math::abs(p_run.dot(dirs[k])) > LevelBrushConstants::PARALLEL_DOT) {
					return true;
				}
			}
			return false;
		};
		const bool prev_beveled = run_beveled(to_prev);
		const bool next_beveled = run_beveled(to_next);

		Vector3 offset;
		if (prev_beveled && next_beveled) {
			// BOTH boundary runs beveled (two beveled edges meet here, or a
			// collinear chain passing through): the corner point is the
			// intersection of the two offset lines - on the angle bisector at
			// distance p_width from each edge line. t = w / sin(angle
			// between bisector and edge).
			Vector3 bisector = to_prev + to_next;
			if (bisector.length_squared() < CMP_EPSILON) {
				// Collinear runs (flat 180-degree corner, e.g. a chain
				// midpoint in a subdivided face): no geometric bisector, but
				// both offset lines are the SAME line - slide perpendicular
				// to the edge within the face plane (face normal x edge).
				bisector = get_face_normal(face).cross(dirs[0]);
				if (bisector.length_squared() < CMP_EPSILON) {
					corner_ok[corner_key(vert, face)] = false;
					continue;
				}
				bisector.normalize();
				// Point INTO the face (toward the face centroid).
				Vector3 centroid;
				for (uint32_t i = 0; i < n; i++) {
					centroid += verts[l[i]];
				}
				centroid /= (real_t)n;
				if (bisector.dot(centroid - vp) < 0.0) {
					bisector = -bisector;
				}
				offset = bisector * p_width;
			} else {
				bisector.normalize();
				real_t along = Math::abs(bisector.dot(dirs[0]));
				real_t denom = Math::sqrt(MAX(1.0 - along * along, 0.0));
				if (denom < 0.05) {
					corner_ok[corner_key(vert, face)] = false; // Nearly parallel.
					continue;
				}
				offset = bisector * (p_width / denom);
			}
		} else {
			// ONE boundary run beveled: the new corner point sits ON the
			// other (unbeveled) boundary ray at exactly p_width from the
			// vertex (Blender measures the offset along the boundary edge).
			const Vector3 &ray = prev_beveled ? to_next : to_prev;
			offset = ray * p_width;
		}
		CornerInfo ci;
		ci.pos = vp + offset;
		corners[corner_key(vert, face)] = ci;
	}

	// Weld corners that resolved to the SAME position: coplanar adjacent
	// faces of a beveled edge (e.g. sub-quads of a subdivided face sharing
	// a chain-midpoint corner) each compute their own corner key, but the
	// strip should share ONE vert there - otherwise coincident duplicates
	// pile up.
	{
		LocalVector<int64_t> keys;
		for (KeyValue<int64_t, CornerInfo> &kv : corners) {
			keys.push_back(kv.key);
		}
		for (uint32_t i = 0; i < keys.size(); i++) {
			CornerInfo *a = corners.getptr(keys[i]);
			if (!a) {
				continue;
			}
			for (uint32_t j = i + 1; j < keys.size(); j++) {
				CornerInfo *b = corners.getptr(keys[j]);
				if (!b) {
					continue;
				}
				if (a->pos.is_equal_approx(b->pos)) {
					// Alias b's key to a's entry: mark b as aliased by giving
					// it a's (future) vert via a shared canonical pointer -
					// simplest: pre-assign the vert now on first weld group.
					if (a->new_vert < 0) {
						a->new_vert = (int)verts.size();
						verts.push_back(a->pos);
					}
					b->new_vert = a->new_vert;
				}
			}
		}
	}

	// --- Pass 3: apply.
	int beveled = 0;
	for (int ei = 0; ei < p_edges.size(); ei++) {
		const EdgeKey &edge = p_edges[ei];
		if (einfo[ei].adj[1] < 0) {
			continue; // Not bevelable.
		}
		// Resolve/create the 4 corner verts.
		int nv[2][2]; // [face slot][endpoint 0=a,1=b]
		bool ok = true;
		for (int s = 0; s < 2 && ok; s++) {
			const int orig[2] = { edge.a, edge.b };
			for (int e = 0; e < 2; e++) {
				int64_t key = corner_key(orig[e], einfo[ei].adj[s]);
				const bool *cok = corner_ok.getptr(key);
				if (cok && !*cok) {
					ok = false;
					break;
				}
				CornerInfo *ci = corners.getptr(key);
				if (!ci) {
					ok = false;
					break;
				}
				if (ci->new_vert < 0) {
					ci->new_vert = (int)verts.size();
					verts.push_back(ci->pos);
				}
				nv[s][e] = ci->new_vert;
			}
		}
		if (!ok) {
			continue;
		}

		// Cut the edge out of each adjacent face: ...prev, a, b, next...
		// becomes ...prev, a_outer, b_outer, next... . If a corner vert is
		// shared with an already-processed edge, the loop may already
		// contain it - skip duplicates.
		Ref<Material> bevel_mat;
		for (int s = 0; s < 2; s++) {
			LocalVector<int> &l = faces[einfo[ei].adj[s]];
			const uint32_t n = l.size();
			LocalVector<int> nl;
			for (uint32_t i = 0; i < n; i++) {
				if (l[i] == edge.a) {
					if (nl.is_empty() || nl[nl.size() - 1] != nv[s][0]) {
						nl.push_back(nv[s][0]);
					}
				} else if (l[i] == edge.b) {
					if (nl.is_empty() || nl[nl.size() - 1] != nv[s][1]) {
						nl.push_back(nv[s][1]);
					}
				} else {
					nl.push_back(l[i]);
				}
			}
			l = nl;
			if (einfo[ei].adj[s] < (int)face_materials.size() && bevel_mat.is_null()) {
				bevel_mat = face_materials[einfo[ei].adj[s]];
			}
		}

		// Inner iso verts across the strip cross-section: iso k (1..2*bands-1)
		// at u = k/(2*bands), blended per shape between the chord (flat),
		// the quadratic Bezier through the original corner (round), and the
		// original face segments (full bulge). iso_verts[k-1][endpoint].
		// Created BEFORE the end-face trim, which inserts this polyline.
		LocalVector<LocalVector<int>> iso_verts;
		if (!single_cut) {
			iso_verts.resize(2 * bands - 1);
			const real_t round_w = MIN(shape_t * 2.0, 1.0); // chord -> bezier.
			const real_t bulge_w = MAX(shape_t * 2.0 - 1.0, 0.0); // bezier -> segments.
			for (int k = 1; k < 2 * bands; k++) {
				const real_t u = (real_t)k / (real_t)(2 * bands);
				iso_verts[k - 1].resize(2);
				for (int e = 0; e < 2; e++) {
					const Vector3 &A = verts[nv[0][e]];
					const Vector3 &B = verts[nv[1][e]];
					const Vector3 &C = verts[e == 0 ? edge.a : edge.b]; // Original corner.
					const Vector3 chord = A.lerp(B, u);
					const Vector3 bez = A * ((1.0 - u) * (1.0 - u)) + C * (2.0 * u * (1.0 - u)) + B * (u * u);
					const Vector3 seg = (u <= 0.5) ? A.lerp(C, 2.0 * u) : C.lerp(B, 2.0 * u - 1.0);
					Vector3 pos = chord.lerp(bez, round_w);
					if (bulge_w > 0.0) {
						pos = pos.lerp(seg, bulge_w);
					}
					iso_verts[k - 1][e] = (int)verts.size();
					verts.push_back(pos);
				}
			}
		}

		// Trim the END faces: every non-adjacent face touching an endpoint
		// of the beveled edge must also lose that corner - otherwise its
		// original corner vert sticks through the chamfer as a fin. The
		// corner is replaced by the strip's end polyline (outer vert, iso
		// verts, outer vert - one per profile point), ordered to match the
		// end face's winding.
		for (int e = 0; e < 2; e++) {
			const int orig[2] = { edge.a, edge.b };
			const int v = orig[e];
			for (uint32_t f = 0; f < faces.size(); f++) {
				if ((int)f == einfo[ei].adj[0] || (int)f == einfo[ei].adj[1]) {
					continue; // Adjacent faces already handled.
				}
				LocalVector<int> &l = faces[f];
				const uint32_t n = l.size();
				// Find v in this loop, and determine on which side of the
				// beveled edge this face lies (does its corner at v run along
				// the edge direction or away from it?).
				int pos = -1;
				for (uint32_t i = 0; i < n; i++) {
					if (l[i] == v) {
						pos = (int)i;
						break;
					}
				}
				if (pos < 0) {
					continue;
				}
				// Already trimmed by an earlier edge of the same bevel action
				// (e.g. the middle of a collinear chain with steps >= 1): the
				// offset verts are already in the loop.
				bool already = false;
				for (uint32_t i = 0; i < n; i++) {
					if (l[i] == nv[0][e] || l[i] == nv[1][e]) {
						already = true;
						break;
					}
				}
				if (already) {
					continue;
				}
				// Skip faces that border ANOTHER selected edge at this same
				// vertex: that edge's own trim (and the corner weld) handles
				// this face - trimming here would insert a zigzag across the
				// shared vertex (the bowtie X on collinear chains).
				bool touches_other_selected = false;
				for (int oi = 0; oi < p_edges.size(); oi++) {
					if (oi == ei || einfo[oi].adj[1] < 0) {
						continue;
					}
					if (p_edges[oi].a != v && p_edges[oi].b != v) {
						continue; // Not incident at this vertex.
					}
					// Does face f border edge oi at all?
					for (uint32_t i = 0; i < n; i++) {
						int ia = l[i];
						int ib = l[(i + 1) % n];
						if ((ia == p_edges[oi].a && ib == p_edges[oi].b) || (ia == p_edges[oi].b && ib == p_edges[oi].a)) {
							touches_other_selected = true;
							break;
						}
					}
					if (touches_other_selected) {
						break;
					}
				}
				if (touches_other_selected) {
					continue;
				}
				// Insert the strip's end polyline where v was: outerA, iso
				// verts (in cross-section order), outerB - so the cap follows
				// the profile curve. Order outerA/outerB to match the winding:
				// the vert closer to the PREV boundary ray goes first.
				const Vector3 prev_dir = (verts[l[(pos + n - 1) % n]] - verts[v]).normalized();
				const Vector3 off0 = verts[nv[0][e]] - verts[v];
				const Vector3 off1 = verts[nv[1][e]] - verts[v];
				const bool first0 = off0.normalized().dot(prev_dir) >= off1.normalized().dot(prev_dir);
				LocalVector<int> nl;
				for (uint32_t i = 0; i < n; i++) {
					if ((int)i == pos) {
						nl.push_back(first0 ? nv[0][e] : nv[1][e]);
						// Iso verts run A -> B; append forward or reversed.
						for (int k = 0; k < (int)iso_verts.size(); k++) {
							const int idx = first0 ? k : (int)iso_verts.size() - 1 - k;
							nl.push_back(iso_verts[idx][e]);
						}
						nl.push_back(first0 ? nv[1][e] : nv[0][e]);
					} else {
						nl.push_back(l[i]);
					}
				}
				l = nl;
			}
		}

		// Strip faces, wound to continue the original surface (normal
		// matched against face slot 0's Newell normal).
		//
		// p_steps == 0: ONE quad bridging the two outer offset lines (the
		// classic single-cut bevel; the original edge is fully consumed).
		//
		// p_steps >= 1: 2*p_steps band quads across the whole cross-section
		// (iso 0 = outer line A, iso 2*bands = outer line B); the original
		// edge is consumed from the adjacent faces - the profile CURVE is
		// what approaches or reaches the corner, not a retained centerline.
		const Vector3 src_normal = get_face_normal(einfo[ei].adj[0]);

		LocalVector<LocalVector<int>> strip_faces;
		if (single_cut) {
			LocalVector<int> strip;
			strip.push_back(nv[0][0]);
			strip.push_back(nv[0][1]);
			strip.push_back(nv[1][1]);
			strip.push_back(nv[1][0]);
			strip_faces.push_back(LocalVector<int>(strip));
		} else {
			auto iso_vert = [&](int p_k, int p_end) -> int {
				if (p_k == 0) {
					return nv[0][p_end];
				}
				if (p_k == 2 * bands) {
					return nv[1][p_end];
				}
				return iso_verts[p_k - 1][p_end];
			};
			for (int k = 0; k < 2 * bands; k++) {
				LocalVector<int> band;
				band.push_back(iso_vert(k, 0));
				band.push_back(iso_vert(k, 1));
				band.push_back(iso_vert(k + 1, 1));
				band.push_back(iso_vert(k + 1, 0));
				strip_faces.push_back(LocalVector<int>(band));
			}
		}

		for (uint32_t bi = 0; bi < strip_faces.size(); bi++) {
			LocalVector<int> strip;
			strip = strip_faces[bi];
			// Degenerate guard: fully-shared corners (closed ring bevel)
			// can collapse a strip to < 3 distinct verts.
			int distinct = 1;
			for (uint32_t i = 1; i < strip.size(); i++) {
				bool seen = false;
				for (uint32_t j = 0; j < i; j++) {
					seen = seen || strip[j] == strip[i];
				}
				distinct += seen ? 0 : 1;
			}
			if (distinct < 3) {
				continue;
			}
			Vector3 e1 = verts[strip[1]] - verts[strip[0]];
			Vector3 e2 = verts[strip[strip.size() - 1]] - verts[strip[0]];
			if (e1.cross(e2).dot(src_normal) < 0.0) {
				LocalVector<int> rev;
				for (int i = (int)strip.size() - 1; i >= 0; i--) {
					rev.push_back(strip[i]);
				}
				strip = rev;
			}
			faces.push_back(LocalVector<int>(strip));
			face_materials.push_back(bevel_mat);
		}

		beveled++;
	}

	if (beveled > 0) {
		compact_vertices();
		_notify_map_changed();
	}
	return beveled;
}
