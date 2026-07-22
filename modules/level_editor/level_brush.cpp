/**************************************************************************/
/*  level_brush.cpp                                                       */
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

#include "level_brush.h"

#include "core/object/class_db.h"
#include "scene/resources/material.h"

void LevelBrush::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_vertex_count"), &LevelBrush::get_vertex_count);
	ClassDB::bind_method(D_METHOD("get_vertex", "index"), &LevelBrush::get_vertex);
	ClassDB::bind_method(D_METHOD("set_vertex", "index", "pos"), &LevelBrush::set_vertex);
	ClassDB::bind_method(D_METHOD("get_face_count"), &LevelBrush::get_face_count);
	ClassDB::bind_method(D_METHOD("get_face_material", "face"), &LevelBrush::get_face_material);
	ClassDB::bind_method(D_METHOD("set_face_material", "face", "material"), &LevelBrush::set_face_material);
	ClassDB::bind_method(D_METHOD("setup_box", "aabb"), &LevelBrush::setup_box);
	ClassDB::bind_method(D_METHOD("flip_faces"), &LevelBrush::flip_faces);
	ClassDB::bind_method(D_METHOD("set_faces_flipped", "flipped"), &LevelBrush::set_faces_flipped);
	ClassDB::bind_method(D_METHOD("is_faces_flipped"), &LevelBrush::is_faces_flipped);

	ClassDB::bind_method(D_METHOD("set_vertices_data", "vertices"), &LevelBrush::set_vertices_data);
	ClassDB::bind_method(D_METHOD("get_vertices_data"), &LevelBrush::get_vertices_data);
	ClassDB::bind_method(D_METHOD("set_faces_data", "faces"), &LevelBrush::set_faces_data);
	ClassDB::bind_method(D_METHOD("get_faces_data"), &LevelBrush::get_faces_data);
	ClassDB::bind_method(D_METHOD("set_face_materials_data", "materials"), &LevelBrush::set_face_materials_data);
	ClassDB::bind_method(D_METHOD("get_face_materials_data"), &LevelBrush::get_face_materials_data);

	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR3_ARRAY, "vertices", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR), "set_vertices_data", "get_vertices_data");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "faces", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_faces_data", "get_faces_data");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "face_materials", PROPERTY_HINT_ARRAY_TYPE, "Material", PROPERTY_USAGE_STORAGE), "set_face_materials_data", "get_face_materials_data");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "faces_flipped"), "set_faces_flipped", "is_faces_flipped");
}

void LevelBrush::_update_face_count_storage() {
	if ((int)face_materials.size() != (int)faces.size()) {
		face_materials.resize(faces.size());
	}
}

Vector3 LevelBrush::get_vertex(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, (int)verts.size(), Vector3());
	return verts[p_index];
}

void LevelBrush::set_vertex(int p_index, const Vector3 &p_pos) {
	ERR_FAIL_INDEX(p_index, (int)verts.size());
	verts[p_index] = p_pos;
}

LocalVector<int> LevelBrush::get_face(int p_face) const {
	ERR_FAIL_INDEX_V(p_face, (int)faces.size(), LocalVector<int>());
	LocalVector<int> out;
	out = faces[p_face];
	return out;
}

Vector3 LevelBrush::get_face_normal(int p_face) const {
	ERR_FAIL_INDEX_V(p_face, (int)faces.size(), Vector3());
	const LocalVector<int> &f = faces[p_face];
	// Newell's method - robust for non-planar n-gons.
	Vector3 n;
	for (uint32_t i = 0; i < f.size(); i++) {
		const Vector3 &a = verts[f[i]];
		const Vector3 &b = verts[f[(i + 1) % f.size()]];
		n.x += (a.y - b.y) * (a.z + b.z);
		n.y += (a.z - b.z) * (a.x + b.x);
		n.z += (a.x - b.x) * (a.y + b.y);
	}
	if (n.length_squared() < CMP_EPSILON) {
		return Vector3(0, 1, 0);
	}
	return n.normalized();
}

Vector3 LevelBrush::get_face_center(int p_face) const {
	ERR_FAIL_INDEX_V(p_face, (int)faces.size(), Vector3());
	const LocalVector<int> &f = faces[p_face];
	Vector3 c;
	for (int idx : f) {
		c += verts[idx];
	}
	if (!f.is_empty()) {
		c /= (real_t)f.size();
	}
	return c;
}

void LevelBrush::set_face_material(int p_face, const Ref<Material> &p_material) {
	ERR_FAIL_INDEX(p_face, (int)face_materials.size());
	face_materials[p_face] = p_material;
}

Ref<Material> LevelBrush::get_face_material(int p_face) const {
	ERR_FAIL_INDEX_V(p_face, (int)face_materials.size(), Ref<Material>());
	return face_materials[p_face];
}

HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> LevelBrush::get_edges() const {
	HashSet<EdgeKey, EdgeKeyHasher> edges;
	for (uint32_t f = 0; f < faces.size(); f++) {
		const LocalVector<int> &loop = faces[f];
		for (uint32_t i = 0; i < loop.size(); i++) {
			edges.insert(EdgeKey(loop[i], loop[(i + 1) % loop.size()]));
		}
	}
	return edges;
}

Vector3 LevelBrush::get_center() const {
	Vector3 c;
	for (const Vector3 &v : verts) {
		c += v;
	}
	if (!verts.is_empty()) {
		c /= (real_t)verts.size();
	}
	return c;
}

int LevelBrush::ray_intersect(const Vector3 &p_origin, const Vector3 &p_dir, real_t &r_dist) const {
	real_t best = (real_t)Math::INF;
	int best_face = -1;
	for (uint32_t f = 0; f < faces.size(); f++) {
		const LocalVector<int> &loop = faces[f];
		if (loop.size() < 3) {
			continue;
		}
		for (uint32_t i = 0; i + 2 < loop.size(); i++) {
			Vector3 tri[3] = { verts[loop[0]], verts[loop[i + 1]], verts[loop[i + 2]] };
			// Moller-Trumbore.
			Vector3 e1 = tri[1] - tri[0];
			Vector3 e2 = tri[2] - tri[0];
			Vector3 p = p_dir.cross(e2);
			real_t det = e1.dot(p);
			if (Math::abs(det) < CMP_EPSILON) {
				continue;
			}
			real_t inv_det = 1.0 / det;
			Vector3 t = p_origin - tri[0];
			real_t u = t.dot(p) * inv_det;
			if (u < -CMP_EPSILON || u > 1.0 + CMP_EPSILON) {
				continue;
			}
			Vector3 q = t.cross(e1);
			real_t v = p_dir.dot(q) * inv_det;
			if (v < -CMP_EPSILON || u + v > 1.0 + CMP_EPSILON) {
				continue;
			}
			real_t dist = e2.dot(q) * inv_det;
			if (dist > CMP_EPSILON && dist < best) {
				best = dist;
				best_face = (int)f;
			}
		}
	}
	if (best_face != -1) {
		r_dist = best;
	}
	return best_face;
}

void LevelBrush::setup_box(const AABB &p_aabb) {
	verts.clear();
	faces.clear();
	face_materials.clear();

	Vector3 mn = p_aabb.position;
	Vector3 mx = p_aabb.position + p_aabb.size;

	// 8 corners.
	verts.push_back(Vector3(mn.x, mn.y, mn.z)); // 0
	verts.push_back(Vector3(mx.x, mn.y, mn.z)); // 1
	verts.push_back(Vector3(mx.x, mx.y, mn.z)); // 2
	verts.push_back(Vector3(mn.x, mx.y, mn.z)); // 3
	verts.push_back(Vector3(mn.x, mn.y, mx.z)); // 4
	verts.push_back(Vector3(mx.x, mn.y, mx.z)); // 5
	verts.push_back(Vector3(mx.x, mx.y, mx.z)); // 6
	verts.push_back(Vector3(mn.x, mx.y, mx.z)); // 7

	// 6 quad faces, wound outward (CCW seen from outside).
	faces.push_back({ 4, 5, 6, 7 }); // +Z (front)
	faces.push_back({ 1, 0, 3, 2 }); // -Z (back)
	faces.push_back({ 5, 1, 2, 6 }); // +X (right)
	faces.push_back({ 0, 4, 7, 3 }); // -X (left)
	faces.push_back({ 7, 6, 2, 3 }); // +Y (top)
	faces.push_back({ 0, 1, 5, 4 }); // -Y (bottom)

	_update_face_count_storage();
}

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
}

LevelBrush *LevelBrush::clip_split(const Plane &p_plane) {
	LevelBrush *back = duplicate_brush();
	back->clip(-p_plane);
	clip(p_plane);
	return back;
}

void LevelBrush::flip_faces() {
	set_faces_flipped(!faces_flipped);
}

void LevelBrush::set_faces_flipped(bool p_flipped) {
	if (faces_flipped == p_flipped) {
		return;
	}
	faces_flipped = p_flipped;
	// Ask the parent map to rebuild its preview.
	Node *parent = get_parent();
	if (parent && parent->has_method("refresh")) {
		parent->call_deferred("refresh");
	}
}

void LevelBrush::move_vertices(const Vector<int> &p_vertices, const Vector3 &p_delta) {
	if (p_delta.is_zero_approx()) {
		return;
	}
	for (int idx : p_vertices) {
		ERR_FAIL_INDEX(idx, (int)verts.size());
		verts[idx] += p_delta;
	}
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

	// Replace the source face with the cap (flipped when extruding inward).
	LocalVector<int> new_cap;
	if (p_distance > 0) {
		new_cap = cap;
	} else {
		for (uint32_t i = 0; i < n; i++) {
			new_cap.push_back(cap[n - 1 - i]);
		}
	}
	faces[p_face] = new_cap;

	// Append side quads stitching the source loop to the cap (wound outward).
	for (uint32_t i = 0; i < n; i++) {
		uint32_t j = (i + 1) % n;
		if (p_distance > 0) {
			faces.push_back({ src[i], src[j], (int)(base + j), (int)(base + i) });
		} else {
			faces.push_back({ src[j], src[i], (int)(base + i), (int)(base + j) });
		}
	}

	// Materials: cap keeps the source material; sides get default (null).
	_update_face_count_storage();
	face_materials[p_face] = cap_mat;

	return p_face;
}

void LevelBrush::get_bake_surface_data(int p_face, Vector<Vector3> &r_vertices, Vector<Vector3> &r_normals, Vector<Vector2> &r_uvs) const {
	ERR_FAIL_INDEX(p_face, (int)faces.size());
	const LocalVector<int> &loop = faces[p_face];
	if (loop.size() < 3) {
		return;
	}

	const Vector3 n = faces_flipped ? -get_face_normal(p_face) : get_face_normal(p_face);

	// Planar UV projection on the face's dominant axes.
	Vector3 abs_n = n.abs();
	Vector3 axis_u, axis_v;
	if (abs_n.x >= abs_n.y && abs_n.x >= abs_n.z) {
		axis_u = Vector3(0, 0, 1);
		axis_v = Vector3(0, 1, 0);
	} else if (abs_n.y >= abs_n.x && abs_n.y >= abs_n.z) {
		axis_u = Vector3(1, 0, 0);
		axis_v = Vector3(0, 0, 1);
	} else {
		axis_u = Vector3(1, 0, 0);
		axis_v = Vector3(0, 1, 0);
	}

	const real_t uv_scale = 0.25;

	for (uint32_t i = 0; i + 2 < loop.size(); i++) {
		int tri[3];
		if (faces_flipped) {
			tri[0] = loop[0];
			tri[1] = loop[i + 2];
			tri[2] = loop[i + 1];
		} else {
			tri[0] = loop[0];
			tri[1] = loop[i + 1];
			tri[2] = loop[i + 2];
		}
		for (int t = 0; t < 3; t++) {
			const Vector3 &p = verts[tri[t]];
			r_vertices.push_back(p);
			r_normals.push_back(n);
			r_uvs.push_back(Vector2(p.dot(axis_u), p.dot(axis_v)) * uv_scale);
		}
	}
}

void LevelBrush::get_collision_faces(Vector<Vector3> &r_faces) const {
	for (uint32_t f = 0; f < faces.size(); f++) {
		const LocalVector<int> &loop = faces[f];
		for (uint32_t i = 0; i + 2 < loop.size(); i++) {
			if (faces_flipped) {
				r_faces.push_back(verts[loop[0]]);
				r_faces.push_back(verts[loop[i + 2]]);
				r_faces.push_back(verts[loop[i + 1]]);
			} else {
				r_faces.push_back(verts[loop[0]]);
				r_faces.push_back(verts[loop[i + 1]]);
				r_faces.push_back(verts[loop[i + 2]]);
			}
		}
	}
}

LevelBrush *LevelBrush::duplicate_brush() const {
	LevelBrush *copy = memnew(LevelBrush);
	copy->verts = verts;
	copy->faces = faces;
	copy->face_materials = face_materials;
	copy->faces_flipped = faces_flipped;
	return copy;
}

void LevelBrush::set_vertices_data(const PackedVector3Array &p_verts) {
	verts.clear();
	verts.resize(p_verts.size());
	for (int i = 0; i < p_verts.size(); i++) {
		verts[i] = p_verts[i];
	}
}

PackedVector3Array LevelBrush::get_vertices_data() const {
	PackedVector3Array out;
	out.resize(verts.size());
	for (uint32_t i = 0; i < verts.size(); i++) {
		out.set(i, verts[i]);
	}
	return out;
}

void LevelBrush::set_faces_data(const Array &p_faces) {
	faces.clear();
	faces.resize(p_faces.size());
	for (int i = 0; i < p_faces.size(); i++) {
		PackedInt32Array loop = p_faces[i];
		faces[i].resize(loop.size());
		for (int j = 0; j < loop.size(); j++) {
			faces[i][j] = loop[j];
		}
	}
	_update_face_count_storage();
}

Array LevelBrush::get_faces_data() const {
	Array out;
	out.resize(faces.size());
	for (uint32_t i = 0; i < faces.size(); i++) {
		PackedInt32Array loop;
		loop.resize(faces[i].size());
		for (uint32_t j = 0; j < faces[i].size(); j++) {
			loop.set(j, faces[i][j]);
		}
		out[i] = loop;
	}
	return out;
}

void LevelBrush::set_face_materials_data(const Array &p_materials) {
	face_materials.clear();
	face_materials.resize(p_materials.size());
	for (int i = 0; i < p_materials.size(); i++) {
		Ref<Material> m = p_materials[i];
		face_materials[i] = m;
	}
	_update_face_count_storage();
}

Array LevelBrush::get_face_materials_data() const {
	Array out;
	out.resize(face_materials.size());
	for (uint32_t i = 0; i < face_materials.size(); i++) {
		out[i] = face_materials[i];
	}
	return out;
}
