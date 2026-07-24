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

// Topology modifiers for LevelBrush (clip, split, extrude, subdivide, weld,
// collapse, bridge, move). Implementations split out of level_brush.cpp.

#include "level_brush.h"

#include "scene/resources/material.h"

void LevelBrush::clip(const Plane &p_plane, bool p_add_cap) {
	// Solid clip: keep only the front side, capping the cut.
	const real_t eps = 0.0005;

	LocalVector<LocalVector<int>> new_faces;
	LocalVector<Ref<Material>> new_mats;
	Vector<int> cap_verts;

	auto weld = [&](const Vector3 &p) -> int {
		for (int idx : cap_verts) {
			if (verts[idx].distance_to(p) < eps * 4.0) {
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
				out.push_back(ia);
			}
			if (in_a != in_b) {
				real_t t = da / (da - db);
				Vector3 hit = a + (b - a) * t;
				out.push_back(weld(hit));
			}
		}
		if (out.size() >= 3) {
			new_faces.push_back(LocalVector<int>(out));
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
	_notify_map_changed();
}

void LevelBrush::delete_faces(const Vector<int> &p_faces) {
	Vector<int> sorted = p_faces;
	sorted.sort();
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
	const real_t eps = 0.0005;
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
				if (verts[vi].distance_to(verts[loop[i]]) < eps * 4.0) {
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
	_notify_map_changed();
}

void LevelBrush::split_faces(const Plane &p_plane) {
	// Split each face polygon along the plane. Faces fully on one side are
	// untouched; crossing faces become two faces sharing the cut edge. The
	// solid is not clipped and no caps are created - both halves remain part
	// of this brush.
	const real_t eps = 0.0005;

	LocalVector<LocalVector<int>> new_faces;
	LocalVector<Ref<Material>> new_mats;

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
					// Weld against existing intersection verts on this edge-cross.
					int found = -1;
					for (uint32_t vi = 0; vi < verts.size(); vi++) {
						if (verts[vi].distance_to(hit) < eps * 4.0) {
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
		for (uint32_t i = 0; i < 4; i++) {
			uint32_t j = (i + 1) % 4;
			mid[i] = (int)verts.size();
			verts.push_back((verts[src[i]] + verts[src[j]]) * 0.5);
		}

		LocalVector<int> q0 = { src[0], mid[0], ci, mid[3] };
		faces[p_face] = q0;
		faces.push_back({ src[1], mid[1], ci, mid[0] });
		faces.push_back({ src[2], mid[2], ci, mid[1] });
		faces.push_back({ src[3], mid[3], ci, mid[2] });
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
