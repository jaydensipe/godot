/**************************************************************************/
/*  level_editor_gizmos.cpp                                               */
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

// Manipulation gizmos for the level editor: the translate/scale arrow gizmo
// and the rotate ring gizmo (picking, dragging, drawing, undo commits, and
// the Face-mode Shift+drag extrude). These are LevelEditorScreen member
// functions, split out of level_editor_screen.cpp for organization.

#include "../../level_constants.h"
#include "../level_editor_screen.h"
#include "../level_helpers.h"

#include "core/math/geometry_3d.h"
#include "editor/editor_interface.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/surface_tool.h"

// ---- Shared gizmo geometry --------------------------------------------------

// Axis length in pixels (fixed screen size, scaled by editor scale). Pick and
// draw MUST use one computation so they always agree (GOTCHAS #25).
static const real_t GIZMO_AXIS_LEN = 64.0;

// Plane-handle axis pairs; GizmoPart = GIZMO_XY + index.
static const int GIZMO_PLANE_AXES[3][2] = { { 0, 1 }, { 0, 2 }, { 1, 2 } };

// ---- 3D gizmo (Godot 3D-editor style) ---------------------------------------
// Real geometry rendered with no-depth-test materials. A view-parallel axis
// foreshortens into a small blob (correct) instead of warping (2D projection)
// or popping in/out (angle cutoffs). Sizes are the gizmo's world-unit
// proportions at scale 1 (axis length ~1.0); _gizmo_3d_world_scale() maps
// that to ~GIZMO_AXIS_LEN pixels on screen.
namespace LevelGizmo3D {
static const real_t AXIS_LEN = 1.0;
static const real_t SHAFT_RADIUS = 0.03;
static const real_t HEAD_LEN = 0.22;
static const real_t HEAD_RADIUS = 0.09;
static const real_t PLANE_EXTENT = 0.4; // Handle offset along each axis.
static const real_t PLANE_SIZE = 0.28;
static const real_t CENTER_SIZE = 0.12;
static const real_t TIP_CUBE_SIZE = 0.14; // Scale-tool tip (Godot's scale gizmo look).
// Pick radii (world units at scale 1) - generous like the 3D editor's sphere
// grabs; the mesh visuals stay thin.
static const real_t AXIS_PICK_RADIUS = 0.14;
static const real_t CENTER_PICK_RADIUS = 0.16;
} // namespace LevelGizmo3D

// Lathed arrow (shaft + head) pointing down +Y, Godot-style. Origin at the
// gizmo center, arrow from y=0 to y=AXIS_LEN.
static Ref<ArrayMesh> _build_gizmo_arrow_mesh() {
	using namespace LevelGizmo3D;
	Ref<SurfaceTool> st;
	st.instantiate();
	st->begin(Mesh::PRIMITIVE_TRIANGLES);
	// Profile: (radius, y) pairs revolved around +Y.
	const Vector2 profile[] = {
		Vector2(0, 0),
		Vector2(SHAFT_RADIUS, 0),
		Vector2(SHAFT_RADIUS, AXIS_LEN - HEAD_LEN),
		Vector2(HEAD_RADIUS, AXIS_LEN - HEAD_LEN),
		Vector2(0, AXIS_LEN),
	};
	const int profile_points = (int)(sizeof(profile) / sizeof(profile[0]));
	const int sides = 16;
	const real_t step = Math::TAU / sides;
	for (int k = 0; k < sides; k++) {
		Basis ma(Vector3(0, 1, 0), k * step);
		Basis mb(Vector3(0, 1, 0), (k + 1) * step);
		for (int j = 0; j < profile_points - 1; j++) {
			Vector3 pa = Vector3(profile[j].x, profile[j].y, 0);
			Vector3 pb = Vector3(profile[j + 1].x, profile[j + 1].y, 0);
			Vector3 quad[4] = {
				ma.xform(pa),
				mb.xform(pa),
				mb.xform(pb),
				ma.xform(pb),
			};
			st->add_vertex(quad[0]);
			st->add_vertex(quad[2]);
			st->add_vertex(quad[1]);
			st->add_vertex(quad[0]);
			st->add_vertex(quad[3]);
			st->add_vertex(quad[2]);
		}
	}
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	st->commit(mesh);
	return mesh;
}

// Shaft with a cube tip (Scale tool; matches the 3D editor's scale gizmo).
// Shaft along +Y like the arrow mesh, cube centered at y=AXIS_LEN.
static Ref<ArrayMesh> _build_gizmo_scale_mesh() {
	using namespace LevelGizmo3D;
	Ref<SurfaceTool> st;
	st.instantiate();
	st->begin(Mesh::PRIMITIVE_TRIANGLES);
	// Shaft (no head): revolve a 2-point profile.
	const real_t shaft_end = AXIS_LEN - TIP_CUBE_SIZE * 0.5;
	const Vector2 profile[] = {
		Vector2(0, 0),
		Vector2(SHAFT_RADIUS, 0),
		Vector2(SHAFT_RADIUS, shaft_end),
		Vector2(0, shaft_end),
	};
	const int profile_points = (int)(sizeof(profile) / sizeof(profile[0]));
	const int sides = 12;
	const real_t step = Math::TAU / sides;
	for (int k = 0; k < sides; k++) {
		Basis ma(Vector3(0, 1, 0), k * step);
		Basis mb(Vector3(0, 1, 0), (k + 1) * step);
		for (int j = 0; j < profile_points - 1; j++) {
			Vector3 pa = Vector3(profile[j].x, profile[j].y, 0);
			Vector3 pb = Vector3(profile[j + 1].x, profile[j + 1].y, 0);
			Vector3 quad[4] = {
				ma.xform(pa),
				mb.xform(pa),
				mb.xform(pb),
				ma.xform(pb),
			};
			st->add_vertex(quad[0]);
			st->add_vertex(quad[2]);
			st->add_vertex(quad[1]);
			st->add_vertex(quad[0]);
			st->add_vertex(quad[3]);
			st->add_vertex(quad[2]);
		}
	}
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	st->commit(mesh);

	// Cube tip as a second surface (same material slot look via override on 0?
	// No - override materials are per-surface; apply the axis material to both
	// surfaces in _ensure_gizmo_3d).
	Ref<SurfaceTool> ct;
	ct.instantiate();
	ct->begin(Mesh::PRIMITIVE_TRIANGLES);
	const real_t h = TIP_CUBE_SIZE * 0.5;
	const real_t cy = AXIS_LEN;
	// 6 faces, 2 triangles each, outward winding (material is cull-disabled
	// anyway).
	const Vector3 c[8] = {
		Vector3(-h, cy - h, -h),
		Vector3(h, cy - h, -h),
		Vector3(h, cy - h, h),
		Vector3(-h, cy - h, h),
		Vector3(-h, cy + h, -h),
		Vector3(h, cy + h, -h),
		Vector3(h, cy + h, h),
		Vector3(-h, cy + h, h),
	};
	static const int FACES[6][4] = {
		{ 0, 1, 2, 3 }, // -Y
		{ 7, 6, 5, 4 }, // +Y
		{ 4, 5, 1, 0 }, // -Z
		{ 6, 7, 3, 2 }, // +Z
		{ 5, 6, 2, 1 }, // +X
		{ 7, 4, 0, 3 }, // -X
	};
	for (const int (&f)[4] : FACES) {
		ct->add_vertex(c[f[0]]);
		ct->add_vertex(c[f[1]]);
		ct->add_vertex(c[f[2]]);
		ct->add_vertex(c[f[0]]);
		ct->add_vertex(c[f[2]]);
		ct->add_vertex(c[f[3]]);
	}
	ct->commit(mesh);
	return mesh;
}

// Handle quad in the positive quadrant of the p_a0/p_a1 axis pair - same
// placement as _pick_gizmo's quad, so pick and draw always agree.
static Ref<ArrayMesh> _build_gizmo_plane_mesh(int p_a0, int p_a1) {
	using namespace LevelGizmo3D;
	const real_t e = PLANE_EXTENT;
	const real_t s = PLANE_SIZE;
	static const Vector3 AXES[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };
	Ref<SurfaceTool> st;
	st.instantiate();
	st->begin(Mesh::PRIMITIVE_TRIANGLES);
	const Vector3 quad[4] = {
		AXES[p_a0] * e + AXES[p_a1] * e,
		AXES[p_a0] * (e + s) + AXES[p_a1] * e,
		AXES[p_a0] * (e + s) + AXES[p_a1] * (e + s),
		AXES[p_a0] * e + AXES[p_a1] * (e + s),
	};
	// Both windings (drawn double-sided anyway via CULL_DISABLED, but keep the
	// mesh valid for any material).
	st->add_vertex(quad[0]);
	st->add_vertex(quad[1]);
	st->add_vertex(quad[2]);
	st->add_vertex(quad[0]);
	st->add_vertex(quad[2]);
	st->add_vertex(quad[3]);
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	st->commit(mesh);
	return mesh;
}

// Unshaded, no-depth-test material (draws on top like the 3D editor's gizmo).
static Ref<StandardMaterial3D> _make_gizmo_material(const Color &p_color) {
	Ref<StandardMaterial3D> mat;
	mat.instantiate();
	mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
	mat->set_flag(StandardMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
	mat->set_flag(StandardMaterial3D::FLAG_DISABLE_FOG, true);
	mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	mat->set_albedo(p_color);
	if (p_color.a < 1.0) {
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	}
	return mat;
}

// One gizmo node tree: axes + plane handles + center cube. One per viewport,
// living in the viewport's gizmo overlay SubViewport (own World3D, immune to
// the scene viewport's debug draw modes); all share meshes/materials.
void LevelEditorScreen::_build_gizmo_instance(Gizmo3DInstance &r_inst) {
	using namespace LevelGizmo3D;
	r_inst.root = memnew(Node3D);
	for (int i = 0; i < 3; i++) {
		MeshInstance3D *axis = memnew(MeshInstance3D);
		axis->set_mesh(gizmo_3d_arrow_mesh);
		axis->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		axis->set_surface_override_material(0, gizmo_3d_axis_mat[i]);
		// NB: surface 1 (the scale mesh's cube tip) only exists while the scale
		// mesh is active - its override is set in _update_gizmo_3d on swap.
		Basis b;
		switch (i) {
			case 0:
				b.rotate(Vector3(0, 0, 1), -Math::PI / 2.0); // +Y -> +X
				break;
			case 2:
				b.rotate(Vector3(1, 0, 0), Math::PI / 2.0); // +Y -> +Z
				break;
			default:
				break;
		}
		axis->set_transform(Transform3D(b, Vector3()));
		r_inst.root->add_child(axis);
		r_inst.axes[i] = axis;

		MeshInstance3D *plane = memnew(MeshInstance3D);
		plane->set_mesh(gizmo_3d_plane_mesh[i]); // Built per-pair, already placed.
		plane->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		plane->set_surface_override_material(0, gizmo_3d_plane_mat[i]);
		r_inst.root->add_child(plane);
		r_inst.planes[i] = plane;
	}

	Ref<BoxMesh> center_box;
	center_box.instantiate();
	center_box->set_size(Vector3(CENTER_SIZE, CENTER_SIZE, CENTER_SIZE));
	MeshInstance3D *center = memnew(MeshInstance3D);
	center->set_mesh(center_box);
	center->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	center->set_surface_override_material(0, gizmo_3d_center_mat);
	r_inst.root->add_child(center);
	r_inst.center = center;
}

void LevelEditorScreen::_ensure_gizmo_3d() {
	if (gizmo_3d_built) {
		return;
	}
	gizmo_3d_built = true;

	const Color axis_col[3] = { LevelEditorColors::GIZMO_AXIS_X, LevelEditorColors::GIZMO_AXIS_Y, LevelEditorColors::GIZMO_AXIS_Z };
	gizmo_3d_arrow_mesh = _build_gizmo_arrow_mesh();
	gizmo_3d_scale_mesh = _build_gizmo_scale_mesh();
	for (int i = 0; i < 3; i++) {
		gizmo_3d_plane_mesh[i] = _build_gizmo_plane_mesh(GIZMO_PLANE_AXES[i][0], GIZMO_PLANE_AXES[i][1]);
	}

	for (int i = 0; i < 3; i++) {
		gizmo_3d_axis_mat[i] = _make_gizmo_material(axis_col[i]);
		gizmo_3d_axis_mat_hot[i] = _make_gizmo_material(LevelEditorColors::hot(axis_col[i]));
		Color pc = axis_col[GIZMO_PLANE_AXES[i][0]].lerp(axis_col[GIZMO_PLANE_AXES[i][1]], 0.5);
		pc.a = 0.8;
		gizmo_3d_plane_mat[i] = _make_gizmo_material(pc);
		Color pc_hot = LevelEditorColors::hot(pc);
		pc_hot.a = 0.95;
		gizmo_3d_plane_mat_hot[i] = _make_gizmo_material(pc_hot);
	}
	gizmo_3d_center_mat = _make_gizmo_material(LevelEditorColors::GIZMO_CENTER);

	for (int v = 0; v < 4; v++) {
		_build_gizmo_instance(gizmo_3d[v]);
		gizmo_3d[v].root->set_visible(false);
		viewports[v]->set_gizmo_root(gizmo_3d[v].root);
	}
}

// World-space size that projects to p_pixels screen pixels at p_origin's
// camera depth (Godot's method: pixels-per-world-unit at that distance).
real_t LevelEditorScreen::_pixels_to_world_at(LevelEditorViewport *p_vp, const Vector3 &p_origin, real_t p_pixels) const {
	Camera3D *cam = p_vp->get_camera();
	const Transform3D cam_xform = cam->get_global_transform();
	const Vector3 camz = -cam_xform.basis.get_column(2).normalized();
	const Vector3 camy = -cam_xform.basis.get_column(1).normalized();
	const Plane p(camz, cam_xform.origin);
	const real_t gizmo_d = MAX(Math::abs(p.distance_to(p_origin)), (real_t)CMP_EPSILON);
	const real_t d0 = cam->unproject_position(cam_xform.origin + camz * gizmo_d).y;
	const real_t d1 = cam->unproject_position(cam_xform.origin + camz * gizmo_d + camy).y;
	const real_t dd = MAX(Math::abs(d0 - d1), (real_t)CMP_EPSILON);
	return (p_pixels * EDSCALE) / dd;
}

// World scale that makes the gizmo read at ~GIZMO_AXIS_LEN pixels on screen.
real_t LevelEditorScreen::_gizmo_3d_world_scale(LevelEditorViewport *p_vp, const Vector3 &p_origin) const {
	return _pixels_to_world_at(p_vp, p_origin, GIZMO_AXIS_LEN);
}

// ---- 3D brush outlines ------------------------------------------------------
// One line mesh per brush per viewport in the gizmo overlay world (no depth
// test, debug-draw-immune). Rebuilt ONLY when the brush's geometry version
// changes - the old 2D overlay re-projected every edge of every brush every
// frame (the interactive-drag hotspot). Open edges are pre-dashed into the
// mesh; selection/hover is a per-instance material swap.

void LevelEditorScreen::_ensure_outline_mats() {
	if (outline_built) {
		return;
	}
	outline_built = true;
	auto make = [](const Color &p_color) {
		Ref<StandardMaterial3D> mat;
		mat.instantiate();
		mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
		mat->set_flag(StandardMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
		mat->set_flag(StandardMaterial3D::FLAG_DISABLE_FOG, true);
		mat->set_albedo(p_color);
		if (p_color.a < 1.0) {
			mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		}
		return mat;
	};
	outline_mat = make(LevelEditorColors::BRUSH_OUTLINE);
	outline_mat_selected = make(LevelEditorColors::BRUSH_OUTLINE_SELECTED);
	outline_mat_highlight = make(LevelEditorColors::HOVER_BRUSH_OUTLINE);
	outline_mat_hover = make(LevelEditorColors::BRUSH_OUTLINE_HOVER);
}

// Tessellates in BRUSH-LOCAL space; the instance carries the brush's global
// transform, so moving a brush costs zero rebuilds (only geometry edits do).
void LevelEditorScreen::_rebuild_outline_mesh(LevelBrush *p_brush, ImmediateMesh *r_mesh) const {
	const HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> &edges = p_brush->get_edges();
	const HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> &open_edges = p_brush->get_open_edges();

	r_mesh->clear_surfaces();
	r_mesh->surface_begin(Mesh::PRIMITIVE_LINES);
	for (const LevelBrush::EdgeKey &e : edges) {
		const Vector3 a = p_brush->get_vertex(e.a);
		const Vector3 b = p_brush->get_vertex(e.b);
		if (!open_edges.has(e)) {
			r_mesh->surface_add_vertex(a);
			r_mesh->surface_add_vertex(b);
			continue;
		}
		// Open edge: pre-dash into segments (in world units approximated from
		// the brush scale - dashes are cosmetic).
		const real_t dash = LevelEditorHandles::OPEN_EDGE_DASH_WORLD; // ~Hammer grid dash.
		const real_t len = a.distance_to(b);
		if (len < dash * 1.5) {
			r_mesh->surface_add_vertex(a);
			r_mesh->surface_add_vertex(b);
			continue;
		}
		const int dashes = MAX(1, (int)Math::floor(len / (dash * 2.0)));
		for (int d = 0; d < dashes; d++) {
			const real_t t0 = (real_t)d / dashes;
			const real_t t1 = t0 + 0.5 / dashes;
			r_mesh->surface_add_vertex(a.lerp(b, t0));
			r_mesh->surface_add_vertex(a.lerp(b, t1));
		}
	}
	r_mesh->surface_end();
}

bool LevelEditorScreen::_update_outlines() {
	_ensure_outline_mats();
	if (!current_map) {
		return false;
	}
	bool any_geometry_changed = false;

	Vector<LevelBrush *> brushes = current_map->get_brushes();
	HashSet<LevelBrush *> live;
	for (LevelBrush *b : brushes) {
		live.insert(b);
	}

	// Per viewport: sync instances with the brush set, rebuild dirty meshes,
	// and apply the selection/hover material.
	for (int v = 0; v < 4; v++) {
		HashMap<LevelBrush *, MeshInstance3D *> &inst = viewports[v]->get_outline_instances();

		// Prune instances for brushes that left the map.
		List<LevelBrush *> dead;
		for (KeyValue<LevelBrush *, MeshInstance3D *> &E : inst) {
			if (!live.has(E.key)) {
				dead.push_back(E.key);
			}
		}
		for (LevelBrush *b : dead) {
			inst[b]->queue_free();
			inst.erase(b);
		}

		for (LevelBrush *b : brushes) {
			MeshInstance3D **mi_p = inst.getptr(b);
			MeshInstance3D *mi = mi_p ? *mi_p : nullptr;
			if (!mi) {
				mi = memnew(MeshInstance3D);
				mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				Ref<ImmediateMesh> mesh;
				mesh.instantiate();
				mi->set_mesh(mesh);
				viewports[v]->get_gizmo_subviewport()->add_child(mi);
				inst[b] = mi;
				outline_versions[v].erase(b); // Force a build below.
			}

			// Rebuild the line mesh only when the brush geometry changed; the
			// transform is applied per frame (moves don't trigger rebuilds).
			// NB: the version map is PER VIEWPORT - each pane owns a separate
			// ImmediateMesh, so each pane must independently detect the change
			// (a shared map let the first pane bump the version and the other
			// three skip their rebuild - stale outline after rotate/scale).
			const uint64_t built = outline_versions[v].has(b) ? outline_versions[v][b] : UINT64_MAX;
			if (built != b->get_geometry_version()) {
				ImmediateMesh *mesh = Object::cast_to<ImmediateMesh>(mi->get_mesh().ptr());
				if (mesh) {
					_rebuild_outline_mesh(b, mesh);
				}
				outline_versions[v][b] = b->get_geometry_version();
				any_geometry_changed = true;
			}
			mi->set_transform(b->get_global_transform());

			// Material from selection/hover state (same rules as the old 2D
			// overlay). Skip redundant re-assignments - set_material_override
			// touches the renderer even for the same RID.
			Ref<Material> mat = outline_mat;
			if (_is_element_target()) {
				// Light blue for hovered brush or any brush with selected elements.
				bool highlighted = (b == hover_brush) || selected_faces.has(b) || selected_edges.has(b) || selected_vertices.has(b);
				if (highlighted) {
					mat = outline_mat_highlight;
				}
			} else {
				if (_mesh_selection_has(b)) {
					mat = outline_mat_selected;
				} else if (b == hover_brush && !_is_drawing_tool()) {
					mat = outline_mat_hover;
				}
			}
			if (mi->get_material_override() != mat) {
				mi->set_material_override(mat);
			}
		}
	}

	// Prune version bookkeeping for dead brushes.
	for (int v = 0; v < 4; v++) {
		List<LevelBrush *> dead_versions;
		for (KeyValue<LevelBrush *, uint64_t> &E : outline_versions[v]) {
			if (!live.has(E.key)) {
				dead_versions.push_back(E.key);
			}
		}
		for (LevelBrush *b : dead_versions) {
			outline_versions[v].erase(b);
		}
	}
	return any_geometry_changed;
}

void LevelEditorScreen::_update_gizmo_3d() {
	_ensure_gizmo_3d();

	const bool want_visible = !_is_drawing_tool() && tool != TOOL_SELECT && tool != TOOL_ROTATE && _has_selection();
	const Vector3 origin = want_visible ? _get_gizmo_origin() : Vector3();

	// Highlight from hover/drag (material swap, Godot-style) and cube tips in
	// the Scale tool. Materials/meshes are shared by all four instances, so
	// update them once via instance 0's nodes.
	if (want_visible) {
		const Ref<ArrayMesh> &tip_mesh = (tool == TOOL_SCALE) ? gizmo_3d_scale_mesh : gizmo_3d_arrow_mesh;
		for (int i = 0; i < 3; i++) {
			if (gizmo_3d[0].axes[i]->get_mesh() != tip_mesh) {
				for (int v = 0; v < 4; v++) {
					gizmo_3d[v].axes[i]->set_mesh(tip_mesh);
				}
			}
			const bool axis_active = (gizmo_hover == (GizmoPart)i || gizmo_drag_part == (GizmoPart)i);
			const Ref<Material> &am = axis_active ? gizmo_3d_axis_mat_hot[i] : gizmo_3d_axis_mat[i];
			const bool plane_active = (gizmo_hover == (GizmoPart)(GIZMO_XY + i) || gizmo_drag_part == (GizmoPart)(GIZMO_XY + i));
			const Ref<Material> &pm = plane_active ? gizmo_3d_plane_mat_hot[i] : gizmo_3d_plane_mat[i];
			for (int v = 0; v < 4; v++) {
				gizmo_3d[v].axes[i]->set_surface_override_material(0, am);
				if (tool == TOOL_SCALE) {
					// Scale mesh has a second surface (cube tip).
					gizmo_3d[v].axes[i]->set_surface_override_material(1, am);
				}
				gizmo_3d[v].planes[i]->set_surface_override_material(0, pm);
			}
		}
	}

	// Per viewport: transform + visibility (screen-size compensation is per
	// camera, and ortho views get the 3D gizmo too).
	for (int v = 0; v < 4; v++) {
		if (!want_visible) {
			gizmo_3d[v].root->set_visible(false);
			continue;
		}
		const real_t s = _gizmo_3d_world_scale(viewports[v], origin);
		gizmo_3d[v].root->set_transform(Transform3D(Basis().scaled(Vector3(s, s, s)), origin));
		gizmo_3d[v].root->set_visible(true);
	}
}

// ---- Manipulation gizmo ---------------------------------------------------

bool LevelEditorScreen::_has_selection() const {
	switch (selection_target) {
		case TARGET_MESH:
			return !selected_brushes.is_empty(); // Whole brushes are the selection.
		case TARGET_FACE:
			return !selected_faces.is_empty();
		case TARGET_EDGE:
			return !selected_edges.is_empty();
		case TARGET_VERTEX:
			return !selected_vertices.is_empty();
		default:
			return false;
	}
}

Vector3 LevelEditorScreen::_get_gizmo_origin() const {
	if (selection_target == TARGET_MESH) {
		if (selected_brushes.is_empty()) {
			return Vector3();
		}
		// Gizmo at the combined world-space center of all selected brushes.
		Vector3 sum;
		for (LevelBrush *b : selected_brushes) {
			sum += b->get_global_transform().xform(b->get_center());
		}
		return sum / (real_t)selected_brushes.size();
	}

	Vector3 sum;
	int count = 0;

	switch (selection_target) {
		case TARGET_FACE: {
			for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
				Transform3D gt = E.key->get_global_transform();
				for (int f : E.value) {
					LocalVector<int> loop = E.key->get_face(f);
					for (int idx : loop) {
						sum += gt.xform(E.key->get_vertex(idx));
						count++;
					}
				}
			}
		} break;
		case TARGET_EDGE: {
			for (const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
				Transform3D gt = E.key->get_global_transform();
				for (const LevelBrush::EdgeKey &e : E.value) {
					sum += gt.xform(E.key->get_vertex(e.a));
					sum += gt.xform(E.key->get_vertex(e.b));
					count += 2;
				}
			}
		} break;
		case TARGET_VERTEX: {
			for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_vertices) {
				Transform3D gt = E.key->get_global_transform();
				for (int v : E.value) {
					sum += gt.xform(E.key->get_vertex(v));
					count++;
				}
			}
		} break;
		default:
			break;
	}
	return count > 0 ? sum / count : Vector3();
}

Vector<int> LevelEditorScreen::_get_gizmo_vertex_indices(LevelBrush *p_brush) const {
	Vector<int> out;
	if (!p_brush) {
		return out;
	}
	HashSet<int> seen;
	auto add = [&](int idx) {
		if (!seen.has(idx)) {
			seen.insert(idx);
			out.push_back(idx);
		}
	};
	switch (selection_target) {
		case TARGET_FACE: {
			const HashSet<int> *set = selected_faces.getptr(p_brush);
			if (set) {
				for (int f : *set) {
					LocalVector<int> loop = p_brush->get_face(f);
					for (int idx : loop) {
						add(idx);
					}
				}
			}
		} break;
		case TARGET_EDGE: {
			const HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> *set = selected_edges.getptr(p_brush);
			if (set) {
				for (const LevelBrush::EdgeKey &e : *set) {
					add(e.a);
					add(e.b);
				}
			}
		} break;
		case TARGET_VERTEX: {
			const HashSet<int> *set = selected_vertices.getptr(p_brush);
			if (set) {
				for (int v : *set) {
					add(v);
				}
			}
		} break;
		default:
			break;
	}
	return out;
}

int LevelEditorScreen::_pick_gizmo(LevelEditorViewport *p_vp, Camera3D *p_camera, const Vector2 &p_screen) const {
	if (!_has_selection()) {
		return GIZMO_NONE;
	}
	const Vector3 origin = _get_gizmo_origin();

	// 3D picking against the gizmo geometry (Godot's sphere-grab approach),
	// used by ALL view types: pick volumes match the 3D mesh visuals exactly,
	// so foreshortened axes stay grabbable at their drawn size.
	using namespace LevelGizmo3D;
	const real_t scale = _gizmo_3d_world_scale(p_vp, origin);
	Vector3 ro, rd;
	p_vp->get_ray(p_screen, ro, rd);
	static const Vector3 AXES[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };

	// Center cube first (smallest, most central target): free move in the
	// camera plane.
	if (Geometry3D::segment_intersects_sphere(ro, ro + rd * LevelEditorHandles::PICK_RAY_LEN, origin, CENTER_PICK_RADIUS * scale)) {
		Vector3 cam_fwd = -p_camera->get_global_transform().basis[2];
		Vector3 ac = cam_fwd.abs();
		if (ac.z >= ac.x && ac.z >= ac.y) {
			return GIZMO_XY;
		} else if (ac.y >= ac.x) {
			return GIZMO_XZ;
		}
		return GIZMO_YZ;
	}

	// Plane handles (ray vs handle quad; handles are smaller targets than the
	// axis lines, so they win first).
	for (int p = 0; p < 3; p++) {
		const int a0 = GIZMO_PLANE_AXES[p][0];
		const int a1 = GIZMO_PLANE_AXES[p][1];
		const real_t e = PLANE_EXTENT * scale;
		const real_t s2 = PLANE_SIZE * scale;
		Vector3 quad[4] = {
			origin + AXES[a0] * e + AXES[a1] * e,
			origin + AXES[a0] * (e + s2) + AXES[a1] * e,
			origin + AXES[a0] * (e + s2) + AXES[a1] * (e + s2),
			origin + AXES[a0] * e + AXES[a1] * (e + s2),
		};
		Vector3 hit;
		if (Geometry3D::segment_intersects_triangle(ro, ro + rd * LevelEditorHandles::PICK_RAY_LEN, quad[0], quad[1], quad[2], &hit) ||
				Geometry3D::segment_intersects_triangle(ro, ro + rd * LevelEditorHandles::PICK_RAY_LEN, quad[0], quad[2], quad[3], &hit)) {
			return GIZMO_XY + p;
		}
	}

	// Axis arrows: closest point between the pick ray and the axis segment must
	// come within the pick radius (segment-vs-segment distance).
	int best = GIZMO_NONE;
	real_t best_d = AXIS_PICK_RADIUS * scale;
	for (int i = 0; i < 3; i++) {
		const Vector3 tip = origin + AXES[i] * (AXIS_LEN * scale);
		Vector3 r1, r2;
		Geometry3D::get_closest_points_between_segments(origin, tip, ro, ro + rd * LevelEditorHandles::PICK_RAY_LEN, r1, r2);
		const real_t d = r1.distance_to(r2);
		if (d < best_d) {
			best_d = d;
			best = i;
		}
	}
	return best;
}

// Shift+drag extrude stub offset: the average normal of the faces using the
// element (edge p_a->p_b, or vertex p_a when p_b < 0), times EXTRUDE_STUB so
// the provisional wall/wedge is never degenerate; the drag replaces it.
static Vector3 _element_stub_dir(LevelBrush *p_brush, int p_a, int p_b) {
	Vector3 dir;
	for (int f = 0; f < p_brush->get_face_count(); f++) {
		LocalVector<int> loop = p_brush->get_face(f);
		bool has_a = false, has_b = (p_b < 0);
		for (int idx : loop) {
			has_a = has_a || idx == p_a;
			has_b = has_b || idx == p_b;
			if (has_a && has_b) {
				break;
			}
		}
		if (has_a && has_b) {
			dir += p_brush->get_face_normal(f);
		}
	}
	if (dir.is_zero_approx()) {
		dir = Vector3(0, 1, 0);
	}
	return dir.normalized() * LevelEditorHandles::EXTRUDE_STUB;
}

void LevelEditorScreen::_gizmo_begin_drag(LevelEditorViewport *p_vp, const Vector2 &p_mouse) {
	gizmo_dragging = true;
	gizmo_drag_viewport = p_vp;
	gizmo_drag_start_origin = _get_gizmo_origin();

	// Snapshot brush vertices for absolute drags + undo.
	gizmo_drag_brush_verts.clear();
	gizmo_scale_last_factors = Vector3(1, 1, 1);
	if (selection_target == TARGET_MESH) {
		gizmo_drag_original_positions.clear();
		gizmo_dup_sources.clear();
		if (gizmo_duplicate_drag) {
			// Shift+drag: duplicate every selected brush into a new sibling node
			// and drag the copies; the originals stay put.
			Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
			LevelBrush *old_primary = selected_brush;
			Vector<LevelBrush *> dupes;
			for (LevelBrush *b : selected_brushes) {
				LevelBrush *copy = b->duplicate_brush();
				copy->set_name(b->get_name());
				copy->set_transform(b->get_transform());
				b->get_parent()->add_child(copy);
				if (root) {
					copy->set_owner(root);
				}
				gizmo_dup_sources[copy] = b;
				dupes.push_back(copy);
				if (b == old_primary) {
					selected_brush = copy;
				}
			}
			selected_brushes = dupes;
			gizmo_drag_start_origin = _get_gizmo_origin(); // Same spot, but derived from the copies.
			_sync_editor_selection();
			_refresh_map();
		}
		for (LevelBrush *b : selected_brushes) {
			gizmo_drag_brush_verts[b] = b->get_vertices_data();
			gizmo_drag_original_positions[b] = b->get_position();
		}
	} else {
		// Element targets: one snapshot per selected brush.
		HashSet<LevelBrush *> brushes;
		switch (selection_target) {
			case TARGET_FACE:
				for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
					brushes.insert(E.key);
				}
				break;
			case TARGET_EDGE:
				for (const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
					brushes.insert(E.key);
				}
				break;
			case TARGET_VERTEX:
				for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_vertices) {
					brushes.insert(E.key);
				}
				break;
			default:
				break;
		}
		for (LevelBrush *b : brushes) {
			gizmo_drag_brush_verts[b] = b->get_vertices_data();
		}
	}

	// Shift+drag in an element target: extrude the selected elements once,
	// then the drag moves the duplicated geometry (Hammer-style pull).
	gizmo_extrude_cap_faces.clear();
	gizmo_extrude_cap_normals.clear();
	gizmo_extrude_elem_verts.clear();
	gizmo_extrude_wall_edges.clear();
	gizmo_extrude_orig_verts.clear();
	gizmo_extrude_orig_faces.clear();
	gizmo_extrude_orig_mats.clear();
	gizmo_extrude_moved_verts.clear();
	if (gizmo_extrude_drag) {
		switch (selection_target) {
			case TARGET_FACE: {
				for (KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
					LevelBrush *b = E.key;
					gizmo_extrude_orig_verts[b] = b->get_vertices_data();
					gizmo_extrude_orig_faces[b] = b->get_faces_data();
					gizmo_extrude_orig_mats[b] = b->get_face_materials_data();

					// Highest index first so earlier indices stay valid as faces append.
					Vector<int> sorted;
					for (int f : E.value) {
						sorted.push_back(f);
					}
					sorted.sort();
					Vector<int> caps;
					Vector<Vector3> cap_normals;
					for (int i = sorted.size() - 1; i >= 0; i--) {
						// Slide direction = the cap's stored normal (same direction
						// extrude_face stubs along).
						const Vector3 n = b->get_face_normal(sorted[i]);
						if (b->extrude_face(sorted[i], LevelEditorHandles::EXTRUDE_STUB) < 0) {
							continue; // Degenerate face: no extrude, skip its cap.
						}
						caps.push_back(sorted[i]); // extrude_face replaces src with the cap in place.
						cap_normals.push_back(n);
					}
					if (caps.is_empty()) {
						gizmo_extrude_orig_verts.erase(b);
						gizmo_extrude_orig_faces.erase(b);
						gizmo_extrude_orig_mats.erase(b);
						continue;
					}
					gizmo_extrude_cap_faces[b] = caps;
					gizmo_extrude_cap_normals[b] = cap_normals;
					gizmo_extrude_moved_verts[b] = b->get_vertices_data();

					// Update the selection to the caps so overlays track the extrusion.
					E.value.clear();
					for (int c : caps) {
						E.value.insert(c);
					}
				}
			} break;
			case TARGET_EDGE: {
				for (KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
					LevelBrush *b = E.key;
					gizmo_extrude_orig_verts[b] = b->get_vertices_data();
					gizmo_extrude_orig_faces[b] = b->get_faces_data();
					gizmo_extrude_orig_mats[b] = b->get_face_materials_data();

					Vector<int> new_verts;
					Vector<LevelBrush::EdgeKey> wall_edges;
					HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> new_edges;
					for (const LevelBrush::EdgeKey &e : E.value) {
						// Stub offset along the average normal of the edge's faces,
						// so the stub wall is never degenerate; the drag replaces it.
						const Vector3 stub = _element_stub_dir(b, e.a, e.b);
						int ids[2];
						if (b->extrude_edge(e, stub, ids)) {
							new_verts.push_back(ids[0]);
							new_verts.push_back(ids[1]);
							wall_edges.push_back(e); // Original seam edge, same order as walls.
							LevelBrush::EdgeKey ne;
							ne.a = ids[0];
							ne.b = ids[1];
							new_edges.insert(ne);
						}
					}
					gizmo_extrude_elem_verts[b] = new_verts;
					gizmo_extrude_wall_edges[b] = wall_edges;
					gizmo_extrude_moved_verts[b] = b->get_vertices_data();
					E.value = new_edges; // Selection tracks the duplicated edges.
				}
			} break;
			case TARGET_VERTEX: {
				for (KeyValue<LevelBrush *, HashSet<int>> &E : selected_vertices) {
					LevelBrush *b = E.key;
					gizmo_extrude_orig_verts[b] = b->get_vertices_data();
					gizmo_extrude_orig_faces[b] = b->get_faces_data();
					gizmo_extrude_orig_mats[b] = b->get_face_materials_data();

					Vector<int> new_verts;
					HashSet<int> new_sel;
					for (int v : E.value) {
						// Stub along the average normal of the vertex's faces.
						int nv = b->extrude_vertex(v, _element_stub_dir(b, v, -1));
						if (nv >= 0) {
							new_verts.push_back(nv);
							new_sel.insert(nv);
						}
					}
					gizmo_extrude_elem_verts[b] = new_verts;
					gizmo_extrude_moved_verts[b] = b->get_vertices_data();
					E.value = new_sel; // Selection tracks the duplicated verts.
				}
			} break;
			default:
				break;
		}
		_refresh_map();
	}

	// Build the constraint plane: passes through the gizmo origin, faces the camera.
	Camera3D *cam = p_vp->get_camera();
	Vector3 cam_pos = cam->get_global_position();
	Vector3 n;
	switch (gizmo_drag_part) {
		case GIZMO_X:
		case GIZMO_Y:
		case GIZMO_Z: {
			// Plane contains the axis and faces the camera.
			Vector3 axis = Vector3(gizmo_drag_part == GIZMO_X ? 1 : 0, gizmo_drag_part == GIZMO_Y ? 1 : 0, gizmo_drag_part == GIZMO_Z ? 1 : 0);
			Vector3 to_cam = (cam_pos - gizmo_drag_start_origin).normalized();
			n = axis.cross(to_cam).cross(axis).normalized();
			if (n.is_zero_approx()) {
				n = axis.get_any_perpendicular();
			}
		} break;
		case GIZMO_XY:
			n = Vector3(0, 0, 1);
			break;
		case GIZMO_XZ:
			n = Vector3(0, 1, 0);
			break;
		case GIZMO_YZ:
			n = Vector3(1, 0, 0);
			break;
		default:
			n = Vector3(0, 1, 0);
			break;
	}
	gizmo_drag_plane_normal = n;
	gizmo_drag_plane_point = gizmo_drag_start_origin;

	// Grab offset: where on the drag axis/plane the user actually grabbed,
	// relative to the gizmo origin. Subtracting this from drag hits keeps the
	// first mouse move from jumping (the click is never exactly at the origin).
	gizmo_drag_grab_offset = Vector3();
	Vector3 gro, grd;
	p_vp->get_ray(p_mouse, gro, grd);
	if (gizmo_drag_part == GIZMO_X || gizmo_drag_part == GIZMO_Y || gizmo_drag_part == GIZMO_Z) {
		Vector3 axis;
		axis[gizmo_drag_part] = 1.0;
		Vector3 grab;
		if (LevelHelpers::closest_point_on_line_to_ray(gizmo_drag_start_origin, axis, gro, grd, grab)) {
			gizmo_drag_grab_offset = grab - gizmo_drag_start_origin;
		}
	} else {
		Plane plane(gizmo_drag_plane_normal, gizmo_drag_plane_normal.dot(gizmo_drag_plane_point));
		Vector3 grab;
		if (plane.intersects_ray(gro, grd, &grab)) {
			gizmo_drag_grab_offset = grab - gizmo_drag_start_origin;
		}
	}
}

void LevelEditorScreen::_gizmo_drag_to(LevelEditorViewport *p_vp, const Vector2 &p_mouse) {
	Vector3 ro, rd;
	p_vp->get_ray(p_mouse, ro, rd);

	Vector3 delta;

	if (gizmo_drag_part == GIZMO_X || gizmo_drag_part == GIZMO_Y || gizmo_drag_part == GIZMO_Z) {
		// Axis drag: find the closest point on the axis line to the mouse ray.
		// Works for any view angle, including looking straight down the axis.
		Vector3 axis;
		axis[gizmo_drag_part] = 1.0;

		Vector3 axis_point;
		if (!LevelHelpers::closest_point_on_line_to_ray(gizmo_drag_start_origin, axis, ro, rd, axis_point)) {
			return; // Mouse ray parallel to the axis.
		}
		// Scale mode wants the raw (unsnapped) delta; it snaps the resulting
		// brush SIZE to the grid instead, which avoids start-of-drag jitter.
		if (tool == TOOL_SCALE) {
			delta = axis_point - gizmo_drag_start_origin - gizmo_drag_grab_offset;
		} else {
			delta = _snap(axis_point - gizmo_drag_grab_offset) - _snap(gizmo_drag_start_origin);
		}
		// Keep only the axis component.
		Vector3 constrained;
		constrained[gizmo_drag_part] = delta[gizmo_drag_part];
		delta = constrained;
	} else {
		// Plane drag: intersect the mouse ray with the constraint plane.
		Plane plane(gizmo_drag_plane_normal, gizmo_drag_plane_normal.dot(gizmo_drag_plane_point));
		Vector3 hit;
		if (!plane.intersects_ray(ro, rd, &hit)) {
			return;
		}
		if (tool == TOOL_SCALE) {
			delta = hit - gizmo_drag_start_origin - gizmo_drag_grab_offset;
		} else {
			delta = _snap(hit - gizmo_drag_grab_offset) - _snap(gizmo_drag_start_origin);
		}

		switch (gizmo_drag_part) {
			case GIZMO_XY:
				delta.z = 0;
				break;
			case GIZMO_XZ:
				delta.y = 0;
				break;
			case GIZMO_YZ:
				delta.x = 0;
				break;
			default:
				break;
		}
	}

	// Scale mode: axis drag distance -> scale factor along that axis.
	if (tool == TOOL_SCALE) {
		if (gizmo_extrude_drag) {
			// Shift+drag extrude in the Scale tool: extrude once, then SCALE the
			// new geometry per element (each cap around its own center) instead
			// of translating it. Must not reuse _apply_gizmo_scale: its snapshot
			// restore would shrink the brush below the extruded faces' vert
			// indices (bake crash, GOTCHAS #32).
			_apply_gizmo_scale_extrude(delta);
		} else {
			_apply_gizmo_scale(delta);
		}
		_update_overlays();
		return;
	}

	_apply_gizmo_delta(delta);
	_update_overlays();
}

// Per-axis scale factors from a drag delta, for the current gizmo drag part
// (plane/center drags scale uniformly by the largest component), clamped to
// a small positive minimum so a brush can't be scaled inside-out.
Vector3 LevelEditorScreen::_scale_factors_from_drag(const Vector3 &p_world_delta) const {
	const real_t SCALE_RATE = LevelEditorHandles::SCALE_DRAG_RATE; // 4 world units of drag = 2x scale.
	Vector3 factors(1, 1, 1);
	if (gizmo_drag_part == GIZMO_XY || gizmo_drag_part == GIZMO_XZ || gizmo_drag_part == GIZMO_YZ) {
		const real_t f = 1.0 + MAX(p_world_delta.x, MAX(p_world_delta.y, p_world_delta.z)) * SCALE_RATE;
		factors = Vector3(f, f, f);
	} else if (gizmo_drag_part >= GIZMO_X && gizmo_drag_part <= GIZMO_Z) {
		factors[gizmo_drag_part] = 1.0 + p_world_delta[gizmo_drag_part] * SCALE_RATE;
	}
	factors.x = MAX(factors.x, 0.01);
	factors.y = MAX(factors.y, 0.01);
	factors.z = MAX(factors.z, 0.01);
	return factors;
}

// Extrude-drag scaling (Scale tool, element targets): per-axis factors from
// the drag delta, applied to the extruded geometry on top of the POST-extrude
// snapshot. Face caps scale around their OWN centers (each extrusion grows
// independently); edge/vertex extrusions scale their duplicated-vert cluster
// around the cluster center.
void LevelEditorScreen::_apply_gizmo_scale_extrude(const Vector3 &p_world_delta) {
	const Vector3 factors = _scale_factors_from_drag(p_world_delta);

	auto scale_verts_around = [&](LevelBrush *p_brush, const Vector<int> &p_indices, const Vector3 &p_pivot_world) {
		Transform3D gt = p_brush->get_global_transform();
		Transform3D inv = gt.affine_inverse();
		for (int idx : p_indices) {
			Vector3 v = gt.xform(p_brush->get_vertex(idx));
			Vector3 rel = v - p_pivot_world;
			v = p_pivot_world + Vector3(rel.x * factors.x, rel.y * factors.y, rel.z * factors.z);
			p_brush->set_vertex(idx, inv.xform(v));
		}
	};

	if (selection_target == TARGET_FACE) {
		for (KeyValue<LevelBrush *, Vector<int>> &E : gizmo_extrude_cap_faces) {
			LevelBrush *brush = E.key;
			brush->set_vertices_data(gizmo_extrude_moved_verts[brush]); // Post-extrude snapshot.
			Transform3D gt = brush->get_global_transform();
			for (int cap : E.value) {
				Vector<int> loop_verts;
				LocalVector<int> loop = brush->get_face(cap);
				for (int idx : loop) {
					loop_verts.push_back(idx);
				}
				// Cap center in world space.
				Vector3 center;
				for (int idx : loop_verts) {
					center += gt.xform(brush->get_vertex(idx));
				}
				if (!loop_verts.is_empty()) {
					center /= loop_verts.size();
				}
				scale_verts_around(brush, loop_verts, center);
			}
			// Walls keep the seam-winding set at extrude time (GOTCHAS #30 is
			// superseded by manifold seam winding): the drag only moves the
			// duplicated verts rigidly, so no re-wind is needed.
		}
	} else {
		for (KeyValue<LevelBrush *, Vector<int>> &E : gizmo_extrude_elem_verts) {
			LevelBrush *brush = E.key;
			brush->set_vertices_data(gizmo_extrude_moved_verts[brush]);
			Transform3D gt = brush->get_global_transform();
			Vector3 center;
			for (int idx : E.value) {
				center += gt.xform(brush->get_vertex(idx));
			}
			if (!E.value.is_empty()) {
				center /= E.value.size();
			}
			scale_verts_around(brush, E.value, center);
			if (gizmo_extrude_wall_edges.has(brush)) {
				const Vector<LevelBrush::EdgeKey> &wedges = gizmo_extrude_wall_edges[brush];
				const int first_wall = brush->get_face_count() - wedges.size();
				for (int i = 0; i < wedges.size(); i++) {
					brush->rewind_edge_wall(first_wall + i, wedges[i].a, wedges[i].b);
				}
			}
		}
	}
	_refresh_map();
}

// ---- Rotate gizmo ------------------------------------------------------------

// Radius in pixels of the rotate rings on screen (before EDSCALE; applied
// per use - EDSCALE isn't safe to call at static-init time).
static const real_t ROTATE_RING_PX = 64.0;
// Segments used to sample the ring (shared by pick + draw so they agree).
static const int ROTATE_RING_SEGMENTS = 48;

// World-space ring radius that projects to ROTATE_RING_PX pixels at the
// gizmo origin. Shared by pick + draw so they always agree (GOTCHAS #25).
real_t LevelEditorScreen::_rotate_world_radius(LevelEditorViewport *p_vp, const Vector3 &p_origin) const {
	return _pixels_to_world_at(p_vp, p_origin, ROTATE_RING_PX);
}

// The only usable rotate axis per ortho view (-1 = all, perspective).
int LevelEditorScreen::_rotate_allowed_axis(LevelEditorViewport::ViewType p_type) const {
	return LevelHelpers::ortho_view_axis((int)p_type);
}

int LevelEditorScreen::_pick_rotate_ring(LevelEditorViewport *p_vp, const Vector2 &p_screen) const {
	if (!_has_selection()) {
		return -1;
	}
	Vector3 origin = _get_gizmo_origin();
	Vector2 center;
	if (!p_vp->project(origin, center)) {
		return -1;
	}

	// The ring plane normal axes; ring radius matches the drawn circle.
	const real_t tol = LevelEditorHandles::ROTATE_RING_PICK_TOL * EDSCALE;
	int best_axis = -1;
	real_t best_dist = tol;

	// In ortho views, only the ring perpendicular to the view plane is usable.
	const int allowed_axis = _rotate_allowed_axis(p_vp->get_view_type());
	const real_t world_radius = _rotate_world_radius(p_vp, origin);

	for (int axis = 0; axis < 3; axis++) {
		if (allowed_axis >= 0 && axis != allowed_axis) {
			continue; // Ring disabled in this ortho view.
		}
		// Sample the ring in 3D and project; measure min distance to the mouse.
		real_t min_d = (real_t)Math::INF;
		bool any_front = false;
		for (int s = 0; s < ROTATE_RING_SEGMENTS; s++) {
			real_t a = (real_t)s / ROTATE_RING_SEGMENTS * Math::TAU;
			Vector3 p;
			// Ring in the plane perpendicular to the axis.
			int u = (axis + 1) % 3, v = (axis + 2) % 3;
			p[u] = Math::cos(a) * world_radius;
			p[v] = Math::sin(a) * world_radius;
			Vector3 world = origin + p;
			Vector2 sp;
			if (!p_vp->project(world, sp)) {
				continue;
			}
			any_front = true;
			min_d = MIN(min_d, sp.distance_to(p_screen));
		}
		if (!any_front) {
			continue;
		}
		// Closest projected ring point wins.
		if (min_d < best_dist) {
			best_dist = min_d;
			best_axis = axis;
		}
	}
	return best_axis;
}

real_t LevelEditorScreen::_rotate_screen_angle(LevelEditorViewport *p_vp, const Vector2 &p_screen, int p_axis) const {
	// Intersect the mouse ray with the plane perpendicular to the axis
	// through the gizmo origin; return the angle around the axis from the
	// view-right reference.
	Vector3 origin = _get_gizmo_origin();
	Vector3 ro, rd;
	p_vp->get_ray(p_screen, ro, rd);
	Vector3 normal;
	normal[p_axis] = 1.0;
	Plane pl(normal, normal.dot(origin));
	Vector3 hit;
	if (!pl.intersects_ray(ro, rd, &hit)) {
		return 0.0;
	}
	Vector3 rel = hit - origin;
	int u = (p_axis + 1) % 3, v = (p_axis + 2) % 3;
	return Math::atan2(rel[v], rel[u]);
}

void LevelEditorScreen::_draw_rotate_gizmo(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (tool != TOOL_ROTATE || !_has_selection()) {
		return;
	}
	Vector3 origin = _get_gizmo_origin();
	Vector2 center;
	if (!p_vp->project(origin, center)) {
		return;
	}

	Color axis_col[3] = { LevelEditorColors::GIZMO_AXIS_X, LevelEditorColors::GIZMO_AXIS_Y, LevelEditorColors::GIZMO_AXIS_Z };

	// World-space radius so the ring projects to ~ROTATE_RING_PX pixels.
	const real_t world_radius = _rotate_world_radius(p_vp, origin);

	// In ortho views, only the ring perpendicular to the view plane is usable.
	const int allowed_axis = _rotate_allowed_axis(p_vp->get_view_type());

	for (int axis = 0; axis < 3; axis++) {
		if (allowed_axis >= 0 && axis != allowed_axis) {
			continue; // Ring disabled in this ortho view.
		}
		bool hot = (rotate_hover_axis == axis || rotate_drag_axis == axis);
		Color col = hot ? LevelEditorColors::hot(axis_col[axis]) : axis_col[axis];
		real_t width = (hot ? 3.0 : 2.0) * EDSCALE;

		int u = (axis + 1) % 3, v = (axis + 2) % 3;
		Vector3 prev_w;
		bool has_prev = false;
		for (int s = 0; s <= ROTATE_RING_SEGMENTS; s++) {
			real_t a = (real_t)s / ROTATE_RING_SEGMENTS * Math::TAU;
			Vector3 p;
			p[u] = Math::cos(a) * world_radius;
			p[v] = Math::sin(a) * world_radius;
			const Vector3 w = origin + p;
			if (has_prev) {
				// Near-plane-safe: clips segments crossing behind the camera
				// instead of dropping them (GOTCHAS #22).
				Vector2 sa, sb;
				if (p_vp->project_segment(prev_w, w, sa, sb)) {
					p_canvas->draw_line(sa, sb, col, width);
				}
			}
			prev_w = w;
			has_prev = true;
		}
	}

	// Center dot.
	p_canvas->draw_circle(center, 3.0 * EDSCALE, LevelEditorColors::GIZMO_CENTER);
}

void LevelEditorScreen::_rotate_end_drag() {
	if (rotate_drag_axis < 0) {
		return;
	}
	const int axis = rotate_drag_axis;
	rotate_drag_axis = -1;

	_commit_brush_verts_undo(selection_target == TARGET_MESH ? TTR("Rotate Brush") : TTR("Rotate Brush Elements"), gizmo_drag_brush_verts);
	// Remember for Replay Action (Shift+G): mesh-target rotations repeat
	// around each brush's own center (same rule as the drag).
	if (selection_target == TARGET_MESH && !Math::is_zero_approx(rotate_drag_last_angle)) {
		_record_replay_action(ReplayAction::KIND_ROTATE, Vector3(), axis, rotate_drag_last_angle);
	}
	rotate_drag_last_angle = 0.0;
	gizmo_drag_brush_verts.clear();
}

void LevelEditorScreen::_apply_gizmo_rotate(int p_axis, real_t p_angle) {
	Vector3 axis;
	axis[p_axis] = 1.0;

	if (selection_target == TARGET_MESH) {
		// Each selected brush rotates around its OWN center (individual
		// origins), absolute per drag - restore the snapshot, then rotate.
		Basis rot(axis, p_angle);
		for (KeyValue<LevelBrush *, PackedVector3Array> &E : gizmo_drag_brush_verts) {
			LevelBrush *brush = E.key;
			brush->set_vertices_data(E.value);
			_brush_transform_verts(brush, brush->get_center(), rot);
		}
		_refresh_map();
		return;
	}

	// Element targets: rotate each selected brush's vertex subset around the
	// shared selection pivot (world axis through the gizmo origin).
	Vector3 pivot = gizmo_drag_start_origin;
	Basis world_rot(axis, p_angle);
	for (KeyValue<LevelBrush *, PackedVector3Array> &E : gizmo_drag_brush_verts) {
		LevelBrush *brush = E.key;
		Transform3D gt = brush->get_global_transform();
		Transform3D inv = gt.affine_inverse();

		brush->set_vertices_data(E.value);
		Vector<int> indices = _get_gizmo_vertex_indices(brush);
		for (int idx : indices) {
			Vector3 v = gt.xform(brush->get_vertex(idx));
			v = pivot + world_rot.xform(v - pivot);
			brush->set_vertex(idx, inv.xform(v));
		}
	}
	_refresh_map();
}

void LevelEditorScreen::_apply_gizmo_scale(const Vector3 &p_world_delta) {
	if (selection_target != TARGET_MESH) {
		// Element targets: per-axis scale of the selected vertices around the
		// drag-start selection pivot, snapped to the world grid (same rule as
		// whole-brush scale).
		const Vector3 factors = _scale_factors_from_drag(p_world_delta);

		Vector3 pivot = gizmo_drag_start_origin;
		for (KeyValue<LevelBrush *, PackedVector3Array> &E : gizmo_drag_brush_verts) {
			LevelBrush *brush = E.key;
			Transform3D gt = brush->get_global_transform();
			Transform3D inv = gt.affine_inverse();
			brush->set_vertices_data(E.value);
			Vector<int> indices = _get_gizmo_vertex_indices(brush);
			for (int idx : indices) {
				Vector3 v = gt.xform(brush->get_vertex(idx));
				Vector3 rel = v - pivot;
				v = _snap(pivot + Vector3(rel.x * factors.x, rel.y * factors.y, rel.z * factors.z));
				brush->set_vertex(idx, inv.xform(v));
			}
		}
		_refresh_map();
		return;
	}
	// Mesh target: ONE per-axis factor set for all selected brushes, snapped
	// against the PRIMARY brush's size (Hammer-style group scale). Each
	// brush scales around its OWN AABB center; only the primary's scaled
	// edges land exactly on grid-size multiples.
	const Vector3 factors = _compute_mesh_scale_factors(p_world_delta);
	gizmo_scale_last_factors = factors; // Remember for Replay Action recording.
	_apply_mesh_scale_factors(factors);
}

Vector3 LevelEditorScreen::_compute_mesh_scale_factors(const Vector3 &p_world_delta) const {
	if (!selected_brush || !gizmo_drag_brush_verts.has(selected_brush)) {
		return Vector3(1, 1, 1);
	}
	const PackedVector3Array &primary_orig = gizmo_drag_brush_verts[selected_brush];
	const AABB pbb = LevelHelpers::aabb_from_points(primary_orig);
	const Transform3D primary_inv = selected_brush->get_global_transform().affine_inverse();
	const Vector3 primary_delta = primary_inv.basis.xform(p_world_delta);

	const real_t SCALE_RATE = 0.25; // 4 world units of drag = 2x scale.
	Vector3 factors(1, 1, 1);
	if (gizmo_drag_part == GIZMO_XY || gizmo_drag_part == GIZMO_XZ || gizmo_drag_part == GIZMO_YZ) {
		// Center/plane drag: uniform scale by the largest dragged component,
		// snapped against the primary's largest extent.
		real_t f = 1.0 + MAX(primary_delta.x, MAX(primary_delta.y, primary_delta.z)) * SCALE_RATE;
		f = MAX(f, 0.01);
		int ref_axis = 0;
		for (int axis = 1; axis < 3; axis++) {
			if (pbb.size[axis] > pbb.size[ref_axis]) {
				ref_axis = axis;
			}
		}
		if (pbb.size[ref_axis] > CMP_EPSILON) {
			f = MAX(_snap(pbb.size[ref_axis] * f), grid_size) / pbb.size[ref_axis];
		}
		factors = Vector3(f, f, f);
	} else if (gizmo_drag_part >= GIZMO_X && gizmo_drag_part <= GIZMO_Z) {
		const int axis = gizmo_drag_part;
		real_t f = MAX(1.0 + primary_delta[axis] * SCALE_RATE, 0.01);
		if (pbb.size[axis] > CMP_EPSILON) {
			f = MAX(_snap(pbb.size[axis] * f), grid_size) / pbb.size[axis];
		}
		factors[axis] = f;
	}
	return factors;
}

void LevelEditorScreen::_apply_mesh_scale_factors(const Vector3 &p_factors) {
	for (KeyValue<LevelBrush *, PackedVector3Array> &E : gizmo_drag_brush_verts) {
		LevelBrush *brush = E.key;
		brush->set_vertices_data(E.value);

		const AABB bb = LevelHelpers::aabb_from_points(E.value);
		const Vector3 center = bb.get_center();
		_brush_transform_verts(brush, center, Basis::from_scale(p_factors));
	}
	_refresh_map();
}

void LevelEditorScreen::_brush_transform_verts(LevelBrush *p_brush, const Vector3 &p_center, const Basis &p_basis) {
	for (int i = 0; i < p_brush->get_vertex_count(); i++) {
		Vector3 v = p_brush->get_vertex(i);
		v = p_center + p_basis.xform(v - p_center);
		p_brush->set_vertex(i, v);
	}
}

void LevelEditorScreen::_brushes_transform_own_center(const Basis &p_basis) {
	// Mesh-target rule: each selected brush transforms around its OWN center.
	for (LevelBrush *b : selected_brushes) {
		if (b->is_inside_tree()) {
			_brush_transform_verts(b, b->get_center(), p_basis);
		}
	}
	_refresh_map();
}

void LevelEditorScreen::_apply_gizmo_delta(const Vector3 &p_world_delta) {
	if (!_has_selection()) {
		return;
	}

	if (selection_target == TARGET_MESH && tool == TOOL_MOVE) {
		// Move ALL selected brush nodes by the same delta, converted into
		// parent (map) space and applied on top of the drag-start positions.
		for (const KeyValue<LevelBrush *, Vector3> &E : gizmo_drag_original_positions) {
			LevelBrush *b = E.key;
			if (!b->is_inside_tree()) {
				continue;
			}
			Vector3 local = p_world_delta;
			Node3D *parent = Object::cast_to<Node3D>(b->get_parent());
			if (parent) {
				local = parent->get_global_transform().affine_inverse().basis.xform(p_world_delta);
			}
			b->set_position(E.value + local);
		}
		_refresh_map();
		return;
	}

	if (gizmo_extrude_drag) {
		// Extrude drag: reset to the post-extrude topology, then offset the
		// duplicated geometry by the delta (face caps slide along their own
		// normals; edges/verts move freely).
		if (selection_target == TARGET_FACE) {
			for (KeyValue<LevelBrush *, Vector<int>> &E : gizmo_extrude_cap_faces) {
				LevelBrush *brush = E.key;
				brush->set_vertices_data(gizmo_extrude_moved_verts[brush]);

				Transform3D inv = brush->get_global_transform().affine_inverse();
				Vector3 local_delta = inv.basis.xform(p_world_delta);

				// All caps share ONE signed amount: the drag projected on a
				// reference normal (the last cap = lowest face index, the +axis
				// face for box topology). Each cap then slides along its OWN
				// normal by that amount - opposing faces both move outward
				// together instead of one going inward (per-face projection
				// made an opposing pair slide the same world direction).
				// The amount snaps to the grid (Hammer extrudes in grid steps).
				const Vector<Vector3> &normals = gizmo_extrude_cap_normals[brush];
				ERR_CONTINUE(normals.is_empty()); // Invariant: written together with cap_faces at begin-drag.
				const real_t amount = _snap(local_delta.dot(normals[normals.size() - 1]));
				for (int ci = 0; ci < E.value.size(); ci++) {
					const Vector3 &n = normals[ci];
					Vector<int> loop_verts;
					LocalVector<int> loop = brush->get_face(E.value[ci]);
					for (int idx : loop) {
						loop_verts.push_back(idx);
					}
					brush->move_vertices(loop_verts, n * amount);
				}
				// Walls keep the seam-winding set at extrude time; no re-wind.
			}
		} else {
			for (KeyValue<LevelBrush *, Vector<int>> &E : gizmo_extrude_elem_verts) {
				LevelBrush *brush = E.key;
				brush->set_vertices_data(gizmo_extrude_moved_verts[brush]);

				Transform3D inv = brush->get_global_transform().affine_inverse();
				Vector3 local_delta = _snap(inv.basis.xform(p_world_delta));
				brush->move_vertices(E.value, local_delta);

				// Edge extrusions: re-wind each wall against the CURRENT geometry.
				// Begin-drag wound them from a 0.001 stub whose direction ties between
				// the two using faces, so the initial winding is unstable; as the drag
				// rotates each wall to its real plane, align it with the source face it
				// now continues (GOTCHAS #30). Walls are the last wall_edges faces.
				if (gizmo_extrude_wall_edges.has(brush)) {
					const Vector<LevelBrush::EdgeKey> &wedges = gizmo_extrude_wall_edges[brush];
					const int first_wall = brush->get_face_count() - wedges.size();
					for (int i = 0; i < wedges.size(); i++) {
						brush->rewind_edge_wall(first_wall + i, wedges[i].a, wedges[i].b);
					}
				}
			}
		}
		_refresh_map();
		return;
	}

	// Restore original vertices, then apply the new delta -> absolute drags.
	// Multi-brush: each selected brush's own vertex subset moves.
	for (KeyValue<LevelBrush *, PackedVector3Array> &E : gizmo_drag_brush_verts) {
		LevelBrush *brush = E.key;
		brush->set_vertices_data(E.value);

		// World delta -> brush-local delta (direction only).
		Transform3D inv = brush->get_global_transform().affine_inverse();
		Vector3 local_delta = inv.basis.xform(p_world_delta);

		// Move only the selected vertices; faces deform to fit (Blender-style).
		Vector<int> indices = _get_gizmo_vertex_indices(brush);
		brush->move_vertices(indices, local_delta);
	}
	_refresh_map();
}

void LevelEditorScreen::_gizmo_end_drag() {
	if (!gizmo_dragging) {
		return;
	}
	gizmo_dragging = false;
	gizmo_drag_part = GIZMO_NONE; // Clear the active highlight (draw treats drag_part as hot).
	_update_overlays();

	if (!_has_selection()) {
		return;
	}

	// Move tool + Mesh target + Shift: the drag moved live duplicates - commit
	// their creation (undo removes them and reselects the sources).
	if (selection_target == TARGET_MESH && tool == TOOL_MOVE && gizmo_duplicate_drag) {
		gizmo_duplicate_drag = false;
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
		undo_redo->create_action(TTR("Duplicate Brush"));
		TypedArray<Node> sources;
		TypedArray<Node> copies;
		for (const KeyValue<LevelBrush *, LevelBrush *> &E : gizmo_dup_sources) {
			LevelBrush *copy = E.key;
			Node *parent = E.value->get_parent(); // Source's parent = copy's intended parent.
			if (!parent) {
				continue;
			}
			sources.push_back(E.value);
			copies.push_back(copy);
			undo_redo->add_do_method(parent, "add_child", copy);
			if (root) {
				undo_redo->add_do_method(copy, "set_owner", root);
			}
			undo_redo->add_undo_method(parent, "remove_child", copy);
			undo_redo->add_do_reference(copy); // Keep the node alive across undo.
		}
		undo_redo->add_do_method(current_map, "refresh");
		undo_redo->add_undo_method(current_map, "refresh");
		undo_redo->add_undo_method(this, "set_brush_selection", sources);
		undo_redo->add_do_method(this, "set_brush_selection", copies);
		undo_redo->commit_action(false);

		// Remember for Replay Action (Shift+G): total world offset of the drag.
		_record_replay_action(ReplayAction::KIND_DUPLICATE_DRAG, _get_gizmo_origin() - gizmo_drag_start_origin);

		gizmo_dup_sources.clear();
		gizmo_drag_original_positions.clear();
		gizmo_drag_brush_verts.clear();
		return;
	}

	// Move tool + Mesh target: one undo action across all moved brushes.
	if (selection_target == TARGET_MESH && tool == TOOL_MOVE) {
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		bool created = false;
		for (const KeyValue<LevelBrush *, Vector3> &E : gizmo_drag_original_positions) {
			LevelBrush *b = E.key;
			if (!b->is_inside_tree()) {
				continue;
			}
			const Vector3 new_pos = b->get_position();
			if (!new_pos.is_equal_approx(E.value)) {
				if (!created) {
					undo_redo->create_action(TTR("Move Brush"));
					created = true;
				}
				undo_redo->add_do_property(b, "position", new_pos);
				undo_redo->add_undo_property(b, "position", E.value);
			}
		}
		if (created) {
			undo_redo->commit_action(false);
			// Remember for Replay Action (Shift+G): total world offset of the drag.
			_record_replay_action(ReplayAction::KIND_MOVE, _get_gizmo_origin() - gizmo_drag_start_origin);
		}
		gizmo_drag_original_positions.clear();
		return;
	}

	// Scale tool + Mesh target: one undo action across all scaled brushes.
	if (tool == TOOL_SCALE && selection_target == TARGET_MESH) {
		_commit_brush_verts_undo(TTR("Scale Brush"), gizmo_drag_brush_verts);
		// Remember for Replay Action (Shift+G): factors captured during the drag
		// (gizmo_drag_part is already NONE here, so recompute would be identity).
		if (!gizmo_scale_last_factors.is_equal_approx(Vector3(1, 1, 1))) {
			_record_replay_action(ReplayAction::KIND_SCALE, Vector3(), 0, 0.0, gizmo_scale_last_factors);
		}
		gizmo_drag_brush_verts.clear();
		return;
	}

	if (gizmo_extrude_drag) {
		gizmo_extrude_drag = false;
		// Commit the extrusion (topology change at drag start + cap pull) as a
		// single undo action, recorded against the pre-extrude snapshots.
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action(selection_target == TARGET_FACE ? TTR("Extrude Faces") : (selection_target == TARGET_EDGE ? TTR("Extrude Edges") : TTR("Extrude Vertices")));
		bool any = false;
		for (KeyValue<LevelBrush *, PackedVector3Array> &E : gizmo_extrude_orig_verts) {
			any = true;
			_add_brush_undo_pair(undo_redo, E.key, E.value, gizmo_extrude_orig_faces[E.key], gizmo_extrude_orig_mats[E.key]);
		}
		if (any) {
			undo_redo->add_do_method(current_map, "refresh");
			undo_redo->add_undo_method(current_map, "refresh");
			// The selection was remapped to the NEW geometry during the drag
			// (indices that don't exist in the pre-extrude snapshot). Undoing
			// would leave those stale indices in the selection (out-of-bounds
			// in every draw/pick) - clear it on undo.
			undo_redo->add_undo_method(this, "clear_selection");
			undo_redo->commit_action(false);
		}
		gizmo_extrude_orig_verts.clear();
		gizmo_extrude_orig_faces.clear();
		gizmo_extrude_orig_mats.clear();
		gizmo_extrude_cap_faces.clear();
		gizmo_extrude_cap_normals.clear();
		gizmo_extrude_elem_verts.clear();
		gizmo_extrude_wall_edges.clear();
		gizmo_extrude_moved_verts.clear();
		gizmo_drag_brush_verts.clear();
		return;
	}

	if (gizmo_drag_brush_verts.is_empty()) {
		return;
	}

	// Commit the element transform as one undo action across all dragged
	// brushes. (Rotate has its own ring-gizmo drag path and never reaches here.)
	_commit_brush_verts_undo(tool == TOOL_SCALE ? TTR("Scale Brush Elements") : TTR("Move Brush Element"), gizmo_drag_brush_verts);
	gizmo_drag_brush_verts.clear();
}

// ---------------------------------------------------------------------------
// Input handlers (dispatched from LevelEditorScreen::forward_input).
// ---------------------------------------------------------------------------

bool LevelEditorScreen::_rotate_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	// Rotate-mode ring gizmo.
	if (tool != TOOL_ROTATE || !_has_selection()) {
		return false;
	}
	Ref<InputEventMouseButton> mb = p_event;
	Ref<InputEventMouseMotion> mm = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			int axis = _pick_rotate_ring(p_vp, mb->get_position());
			if (axis < 0 && p_vp->get_view_type() != LevelEditorViewport::VIEW_PERSPECTIVE) {
				// Ortho views: click anywhere to rotate around the view axis.
				switch (p_vp->get_view_type()) {
					case LevelEditorViewport::VIEW_TOP:
						axis = 1;
						break;
					case LevelEditorViewport::VIEW_FRONT:
						axis = 2;
						break;
					case LevelEditorViewport::VIEW_SIDE:
						axis = 0;
						break;
					default:
						break;
				}
			}
			if (axis >= 0) {
				rotate_drag_axis = axis;
				rotate_drag_viewport = p_vp;
				rotate_drag_start_angle = _rotate_screen_angle(p_vp, mb->get_position(), axis);
				if (selection_target == TARGET_MESH) {
					gizmo_drag_brush_verts.clear();
					for (LevelBrush *b : selected_brushes) {
						gizmo_drag_brush_verts[b] = b->get_vertices_data();
					}
				} else {
					// Element targets: snapshot every selected brush, and the drag
					// pivot (selection center) for world-axis rotation.
					gizmo_drag_start_origin = _get_gizmo_origin();
					gizmo_drag_brush_verts.clear();
					HashSet<LevelBrush *> brushes;
					switch (selection_target) {
						case TARGET_FACE:
							for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
								brushes.insert(E.key);
							}
							break;
						case TARGET_EDGE:
							for (const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
								brushes.insert(E.key);
							}
							break;
						case TARGET_VERTEX:
							for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_vertices) {
								brushes.insert(E.key);
							}
							break;
						default:
							break;
					}
					for (LevelBrush *b : brushes) {
						gizmo_drag_brush_verts[b] = b->get_vertices_data();
					}
				}
				return true;
			}
		} else if (rotate_drag_axis >= 0) {
			_rotate_end_drag();
			return true;
		}
	} else if (mm.is_valid()) {
		if (rotate_drag_axis >= 0 && rotate_drag_viewport == p_vp) {
			real_t cur = _rotate_screen_angle(p_vp, mm->get_position(), rotate_drag_axis);
				real_t delta = cur - rotate_drag_start_angle;
				// Snap to the rotate grid step.
				delta = Math::snapped(delta, Math::deg_to_rad(LevelEditorGrid::ROTATE_SNAP_DEGREES));
			rotate_drag_last_angle = delta;
			_apply_gizmo_rotate(rotate_drag_axis, delta);
			_update_overlays();
			return true;
		}
		int prev = rotate_hover_axis;
		rotate_hover_axis = _pick_rotate_ring(p_vp, mm->get_position());
		if (prev != rotate_hover_axis) {
			_update_overlays();
		}
	}
	return false;
}

bool LevelEditorScreen::_gizmo_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	// Translate/scale gizmo (Move/Scale tools, any selection target).
	if (_is_drawing_tool() || tool == TOOL_SELECT || tool == TOOL_ROTATE || !_has_selection()) {
		return false;
	}
	Ref<InputEventMouseButton> mb = p_event;
	Ref<InputEventMouseMotion> mm = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			int part = _pick_gizmo(p_vp, p_camera, mb->get_position());
			if (part != GIZMO_NONE) {
				gizmo_drag_part = (GizmoPart)part;
				gizmo_extrude_drag = (_is_element_target() && mb->is_shift_pressed());
				// Mesh target: Shift+drag duplicates the brushes and drags the copies.
				gizmo_duplicate_drag = (selection_target == TARGET_MESH && tool == TOOL_MOVE && mb->is_shift_pressed());
				_gizmo_begin_drag(p_vp, mb->get_position());
				return true; // Consumed by gizmo.
			}
			// Off-gizmo clicks fall through to _selection_input in EVERY tool -
			// the Scale tool's old "click anywhere to uniform-scale" swallow
			// made already-selected brushes un-reselectable (GOTCHAS #31).
		} else if (gizmo_dragging) {
			_gizmo_end_drag();
			return true;
		}
	} else if (mm.is_valid()) {
		if (gizmo_dragging) {
			_gizmo_drag_to(p_vp, mm->get_position());
			return true;
		}
		GizmoPart prev = gizmo_hover;
		gizmo_hover = (GizmoPart)_pick_gizmo(p_vp, p_camera, mm->get_position());
		if (prev != gizmo_hover) {
			_update_overlays();
		}
	}
	return false;
}
