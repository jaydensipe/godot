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

Vector3 LevelBrush::_snap(const Vector3 &p_v) {
	return Vector3(Math::snapped(p_v.x, SNAP_EPSILON), Math::snapped(p_v.y, SNAP_EPSILON), Math::snapped(p_v.z, SNAP_EPSILON));
}

LevelBrush::EdgeKey::EdgeKey(const Vector3 &p_a, const Vector3 &p_b) {
	Vector3 sa = LevelBrush::_snap(p_a);
	Vector3 sb = LevelBrush::_snap(p_b);
	// Canonical ordering so (a,b) == (b,a).
	if (sa == sb) {
		a = sa;
		b = sb;
	} else if (sa < sb) {
		a = sa;
		b = sb;
	} else {
		a = sb;
		b = sa;
	}
}

uint32_t LevelBrush::EdgeKeyHasher::hash(const EdgeKey &p_key) {
	uint32_t h = hash_murmur3_one_32(*(const uint32_t *)&p_key.a.x);
	h = hash_murmur3_one_32(*(const uint32_t *)&p_key.a.y, h);
	h = hash_murmur3_one_32(*(const uint32_t *)&p_key.a.z, h);
	h = hash_murmur3_one_32(*(const uint32_t *)&p_key.b.x, h);
	h = hash_murmur3_one_32(*(const uint32_t *)&p_key.b.y, h);
	h = hash_murmur3_one_32(*(const uint32_t *)&p_key.b.z, h);
	return h;
}

void LevelBrush::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_face_count"), &LevelBrush::get_face_count);
	ClassDB::bind_method(D_METHOD("get_face_plane", "face"), &LevelBrush::get_face_plane);
	ClassDB::bind_method(D_METHOD("set_face_plane", "face", "plane"), &LevelBrush::set_face_plane);
	ClassDB::bind_method(D_METHOD("get_face_material", "face"), &LevelBrush::get_face_material);
	ClassDB::bind_method(D_METHOD("set_face_material", "face", "material"), &LevelBrush::set_face_material);
	ClassDB::bind_method(D_METHOD("setup_box", "aabb"), &LevelBrush::setup_box);

	ClassDB::bind_method(D_METHOD("set_planes_data", "planes"), &LevelBrush::set_planes_data);
	ClassDB::bind_method(D_METHOD("get_planes_data"), &LevelBrush::get_planes_data);
	ClassDB::bind_method(D_METHOD("set_face_materials_data", "materials"), &LevelBrush::set_face_materials_data);
	ClassDB::bind_method(D_METHOD("get_face_materials_data"), &LevelBrush::get_face_materials_data);

	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR4_ARRAY, "planes", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR), "set_planes_data", "get_planes_data");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "face_materials", PROPERTY_HINT_ARRAY_TYPE, "Material", PROPERTY_USAGE_STORAGE), "set_face_materials_data", "get_face_materials_data");
}

void LevelBrush::set_planes_data(const PackedVector4Array &p_planes) {
	planes.clear();
	planes.resize(p_planes.size());
	for (int i = 0; i < p_planes.size(); i++) {
		Vector4 v = p_planes[i];
		planes.write[i] = Plane(Vector3(v.x, v.y, v.z), v.w);
	}
	// Keep materials array in sync size-wise (don't drop existing refs).
	if (face_materials.size() != planes.size()) {
		face_materials.resize(planes.size());
	}
}

PackedVector4Array LevelBrush::get_planes_data() const {
	PackedVector4Array out;
	out.resize(planes.size());
	for (int i = 0; i < planes.size(); i++) {
		const Plane &p = planes[i];
		out.set(i, Vector4(p.normal.x, p.normal.y, p.normal.z, p.d));
	}
	return out;
}

void LevelBrush::set_face_materials_data(const Array &p_materials) {
	face_materials.clear();
	face_materials.resize(p_materials.size());
	for (int i = 0; i < p_materials.size(); i++) {
		Ref<Material> m = p_materials[i];
		face_materials.write[i] = m;
	}
}

Array LevelBrush::get_face_materials_data() const {
	Array out;
	out.resize(face_materials.size());
	for (int i = 0; i < face_materials.size(); i++) {
		out[i] = face_materials[i];
	}
	return out;
}

void LevelBrush::set_face_plane(int p_face, const Plane &p_plane) {
	ERR_FAIL_INDEX(p_face, planes.size());
	planes.write[p_face] = p_plane;
}

Plane LevelBrush::get_face_plane(int p_face) const {
	ERR_FAIL_INDEX_V(p_face, planes.size(), Plane());
	return planes[p_face];
}

void LevelBrush::set_face_material(int p_face, const Ref<Material> &p_material) {
	ERR_FAIL_INDEX(p_face, face_materials.size());
	face_materials.write[p_face] = p_material;
}

Ref<Material> LevelBrush::get_face_material(int p_face) const {
	ERR_FAIL_INDEX_V(p_face, face_materials.size(), Ref<Material>());
	return face_materials[p_face];
}

Vector<Vector3> LevelBrush::get_face_polygon(int p_face) const {
	ERR_FAIL_INDEX_V(p_face, planes.size(), Vector<Vector3>());

	const Plane &fp = planes[p_face];
	const int count = planes.size();
	Vector<Vector3> poly;

	for (int i = 0; i < count; i++) {
		if (i == p_face) {
			continue;
		}
		for (int j = i + 1; j < count; j++) {
			if (j == p_face) {
				continue;
			}
			Vector3 v;
			if (fp.intersect_3(planes[i], planes[j], &v)) {
				bool inside = true;
				for (int k = 0; k < count; k++) {
					if (k == p_face || k == i || k == j) {
						continue;
					}
					if (planes[k].is_point_over(v)) {
						inside = false;
						break;
					}
				}
				if (inside) {
					bool found = false;
					for (const Vector3 &p : poly) {
						if (_snap(p) == _snap(v)) {
							found = true;
							break;
						}
					}
					if (!found) {
						poly.push_back(v);
					}
				}
			}
		}
	}

	if (poly.size() < 3) {
		return Vector<Vector3>();
	}

	// Sort around the face normal (fan winding).
	Vector3 center;
	for (const Vector3 &p : poly) {
		center += p;
	}
	center /= poly.size();

	Vector3 n = fp.normal;
	Vector3 axis_u = (poly[0] - center).normalized();
	Vector3 axis_v = n.cross(axis_u).normalized();

	struct SortItem {
		real_t angle;
		Vector3 v;
		bool operator<(const SortItem &p_other) const { return angle < p_other.angle; }
	};

	Vector<SortItem> items;
	items.resize(poly.size());
	for (int i = 0; i < poly.size(); i++) {
		Vector3 d = poly[i] - center;
		items.write[i].angle = Math::atan2(d.dot(axis_v), d.dot(axis_u));
		items.write[i].v = poly[i];
	}
	items.sort();

	Vector<Vector3> out;
	out.resize(items.size());
	for (int i = 0; i < items.size(); i++) {
		out.write[i] = items[i].v;
	}
	return out;
}

HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> LevelBrush::get_edges() const {
	HashSet<EdgeKey, EdgeKeyHasher> edges;
	for (int i = 0; i < planes.size(); i++) {
		Vector<Vector3> poly = get_face_polygon(i);
		for (int j = 0; j < poly.size(); j++) {
			edges.insert(EdgeKey(poly[j], poly[(j + 1) % poly.size()]));
		}
	}
	return edges;
}

Vector<Vector3> LevelBrush::get_vertices() const {
	Vector<Vector3> verts;
	HashSet<EdgeKey, EdgeKeyHasher> seen;
	for (int i = 0; i < planes.size(); i++) {
		Vector<Vector3> poly = get_face_polygon(i);
		for (int j = 0; j < poly.size(); j++) {
			EdgeKey key(poly[j], poly[j]);
			if (!seen.has(key)) {
				seen.insert(key);
				verts.push_back(poly[j]);
			}
		}
	}
	return verts;
}

Vector3 LevelBrush::get_center() const {
	Vector<Vector3> verts = get_vertices();
	Vector3 c;
	for (const Vector3 &v : verts) {
		c += v;
	}
	if (!verts.is_empty()) {
		c /= verts.size();
	}
	return c;
}

int LevelBrush::ray_intersect(const Vector3 &p_origin, const Vector3 &p_dir, real_t &r_dist) const {
	real_t best = (real_t)Math::INF;
	int best_face = -1;
	for (int i = 0; i < planes.size(); i++) {
		Vector3 hit;
		if (planes[i].intersects_ray(p_origin, p_dir, &hit)) {
			real_t d = (hit - p_origin).length();
			if (d < best) {
				bool inside = true;
				for (int j = 0; j < planes.size(); j++) {
					if (j != i && planes[j].is_point_over(hit)) {
						inside = false;
						break;
					}
				}
				if (inside) {
					best = d;
					best_face = i;
				}
			}
		}
	}
	if (best_face != -1) {
		r_dist = best;
	}
	return best_face;
}

void LevelBrush::setup_box(const AABB &p_aabb) {
	planes.clear();

	Vector3 mins = p_aabb.position;
	Vector3 maxs = p_aabb.position + p_aabb.size;

	// Outward-facing planes.
	planes.push_back(Plane(Vector3(1, 0, 0), maxs.x));
	planes.push_back(Plane(Vector3(-1, 0, 0), -mins.x));
	planes.push_back(Plane(Vector3(0, 1, 0), maxs.y));
	planes.push_back(Plane(Vector3(0, -1, 0), -mins.y));
	planes.push_back(Plane(Vector3(0, 0, 1), maxs.z));
	planes.push_back(Plane(Vector3(0, 0, -1), -mins.z));

	face_materials.resize(6);
}

LevelBrush *LevelBrush::duplicate_brush() const {
	LevelBrush *copy = memnew(LevelBrush);
	copy->planes = planes;
	copy->face_materials = face_materials;
	return copy;
}

void LevelBrush::translate_faces(const Vector<int> &p_faces, const Vector3 &p_delta) {
	if (p_delta.is_zero_approx()) {
		return;
	}
	for (int f : p_faces) {
		ERR_FAIL_INDEX(f, planes.size());
		planes.write[f].d += planes[f].normal.dot(p_delta);
	}
}

void LevelBrush::extrude_faces(const Vector<int> &p_faces, real_t p_distance) {
	if (p_distance == 0.0) {
		return;
	}
	for (int f : p_faces) {
		ERR_FAIL_INDEX(f, planes.size());
		planes.write[f].d += p_distance;
	}
}

void LevelBrush::extrude_edge(const EdgeKey &p_edge, real_t p_distance) {
	Vector<int> touching;
	for (int i = 0; i < planes.size(); i++) {
		Vector<Vector3> poly = get_face_polygon(i);
		for (int j = 0; j < poly.size(); j++) {
			EdgeKey ek(poly[j], poly[(j + 1) % poly.size()]);
			if (ek == p_edge) {
				touching.push_back(i);
				break;
			}
		}
	}
	if (touching.size() != 2) {
		return;
	}
	extrude_faces(touching, p_distance);
}

void LevelBrush::extrude_vertex(const Vector3 &p_vertex, real_t p_distance) {
	Vector3 snapped = _snap(p_vertex);
	Vector<int> touching;
	for (int i = 0; i < planes.size(); i++) {
		Vector<Vector3> poly = get_face_polygon(i);
		for (const Vector3 &v : poly) {
			if (_snap(v) == snapped) {
				touching.push_back(i);
				break;
			}
		}
	}
	if (touching.is_empty()) {
		return;
	}
	extrude_faces(touching, p_distance);
}

void LevelBrush::move_elements(const Vector<Vector3> &p_vertices, const Vector<EdgeKey> &p_edges, const Vector<int> &p_faces, const Vector3 &p_delta) {
	if (p_delta.is_zero_approx()) {
		return;
	}

	// Faces slide parallel to themselves: keep the normal, shift the plane
	// offset. Neighboring faces are left alone; the convex clip stretches
	// them automatically. This is the Hammer "move face" behavior.
	if (!p_faces.is_empty()) {
		translate_faces(p_faces, p_delta);
		return;
	}

	// Collect the set of directly-moved vertices (snapped, local space).
	HashSet<EdgeKey, EdgeKeyHasher> moved_verts; // EdgeKey(v,v) acts as a point key.
	for (const Vector3 &v : p_vertices) {
		moved_verts.insert(EdgeKey(v, v));
	}
	for (const EdgeKey &e : p_edges) {
		moved_verts.insert(EdgeKey(e.a, e.a));
		moved_verts.insert(EdgeKey(e.b, e.b));
	}

	auto new_pos = [&](const Vector3 &p_v) -> Vector3 {
		return p_v + p_delta;
	};

	// Recompute each affected face's plane from its (partially moved) polygon.
	// For each face that has at least one moved vertex, pick anchors: prefer
	// unmoved polygon corners (which stay fixed), fill with moved ones.
	HashSet<int> affected_faces;
	for (int i = 0; i < planes.size(); i++) {
		Vector<Vector3> poly = get_face_polygon(i);
		bool has_moved = false;
		for (const Vector3 &v : poly) {
			if (moved_verts.has(EdgeKey(v, v))) {
				has_moved = true;
				break;
			}
		}
		if (has_moved) {
			affected_faces.insert(i);
		}
	}

	for (int f : affected_faces) {
		Vector<Vector3> poly = get_face_polygon(f);
		if (poly.size() < 3) {
			continue;
		}

		// Apply delta to moved vertices of this polygon.
		Vector<Vector3> moved_poly;
		moved_poly.resize(poly.size());
		for (int i = 0; i < poly.size(); i++) {
			moved_poly.write[i] = moved_verts.has(EdgeKey(poly[i], poly[i])) ? new_pos(poly[i]) : poly[i];
		}

		// The new plane MUST pass through the moved vertices, otherwise the
		// move is a no-op (unmoved anchors would rebuild the old plane).
		// Strategy: sort anchors so moved points come first; then pick the
		// first non-collinear triple. For a quad with one moved corner this
		// yields: moved corner + the two adjacent corners... which is
		// degenerate (they were collinear on the old plane edge) only if the
		// move is along the edge; the loop then tries the next triple.
		Vector<int> order;
		for (int i = 0; i < moved_poly.size(); i++) {
			if (moved_verts.has(EdgeKey(poly[i], poly[i]))) {
				order.push_back(i);
			}
		}
		for (int i = 0; i < moved_poly.size(); i++) {
			if (!moved_verts.has(EdgeKey(poly[i], poly[i]))) {
				order.push_back(i);
			}
		}

		Plane new_plane;
		bool found = false;
		for (int a = 0; a < order.size() && !found; a++) {
			for (int b = a + 1; b < order.size() && !found; b++) {
				for (int c = b + 1; c < order.size() && !found; c++) {
					const Vector3 &p1 = moved_poly[order[a]];
					const Vector3 &p2 = moved_poly[order[b]];
					const Vector3 &p3 = moved_poly[order[c]];
					Vector3 n = (p1 - p2).cross(p1 - p3);
					if (n.length_squared() > CMP_EPSILON) {
						n.normalize();
						// Keep the plane facing outward (same side as before).
						if (n.dot(planes[f].normal) < 0) {
							n = -n;
						}
						new_plane = Plane(n, n.dot(p1));
						found = true;
					}
				}
			}
		}
		if (found) {
			planes.write[f] = new_plane;
		}
	}
}

void LevelBrush::merge_coplanar_faces() {
	for (int i = 0; i < planes.size(); i++) {
		for (int j = planes.size() - 1; j > i; j--) {
			if (planes[i].is_equal_approx(planes[j])) {
				planes.remove_at(j);
				face_materials.remove_at(j);
			}
		}
	}
}

void LevelBrush::get_bake_surface_data(int p_face, Vector<Vector3> &r_vertices, Vector<Vector3> &r_normals, Vector<Vector2> &r_uvs) const {
	Vector<Vector3> poly = get_face_polygon(p_face);
	if (poly.size() < 3) {
		return;
	}

	const Plane &plane = planes[p_face];
	Vector3 n = plane.normal;

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

	const real_t uv_scale = 0.25; // 4 texels per unit, Hammer-ish.

	for (int i = 0; i < poly.size() - 2; i++) {
		Vector3 tri[3] = { poly[0], poly[i + 1], poly[i + 2] };
		for (int t = 0; t < 3; t++) {
			r_vertices.push_back(tri[t]);
			r_normals.push_back(n);
			r_uvs.push_back(Vector2(tri[t].dot(axis_u), tri[t].dot(axis_v)) * uv_scale);
		}
	}
}

void LevelBrush::get_collision_faces(Vector<Vector3> &r_faces) const {
	for (int i = 0; i < planes.size(); i++) {
		Vector<Vector3> poly = get_face_polygon(i);
		for (int j = 0; j < poly.size() - 2; j++) {
			r_faces.push_back(poly[0]);
			r_faces.push_back(poly[j + 1]);
			r_faces.push_back(poly[j + 2]);
		}
	}
}
