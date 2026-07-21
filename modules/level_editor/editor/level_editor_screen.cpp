/**************************************************************************/
/*  level_editor_screen.cpp                                               */
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

#include "level_editor_screen.h"

#include "core/object/callable_mp.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/inspector/editor_resource_picker.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/resources/environment.h"

// ---------------------------------------------------------------------------
// LevelEditorViewport
// ---------------------------------------------------------------------------

LevelEditorViewport::LevelEditorViewport() {
	set_stretch(true);
	set_focus_mode(FOCUS_ALL);

	subviewport = memnew(SubViewport);
	subviewport->set_disable_input(true);
	subviewport->set_handle_input_locally(false);
	add_child(subviewport);

	camera = memnew(Camera3D);
	camera->set_current(true);
	camera->set_far(4000.0);
	subviewport->add_child(camera);

	light = memnew(DirectionalLight3D);
	light->set_rotation(Vector3(Math::deg_to_rad(-50.0), Math::deg_to_rad(-30.0), 0));
	subviewport->add_child(light);

	world_env = memnew(WorldEnvironment);
	Ref<Environment> env;
	env.instantiate();
	env->set_background(Environment::BG_COLOR);
	env->set_bg_color(Color(0.16, 0.16, 0.18));
	env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
	env->set_ambient_light_color(Color(0.45, 0.45, 0.48));
	env->set_ambient_light_energy(1.0);
	world_env->set_environment(env);
	subviewport->add_child(world_env);

	overlay = memnew(Overlay);
	overlay->viewport = this;
	overlay->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	overlay->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	add_child(overlay);

	_update_camera_transform();
}

void LevelEditorViewport::Overlay::_notification(int p_what) {
	if (p_what == NOTIFICATION_DRAW && viewport) {
		viewport->_overlay_draw();
	}
}

void LevelEditorViewport::_overlay_draw() {
	_draw_grid();
	if (screen) {
		screen->_draw_viewport_overlay(this, overlay);
	}
}

void LevelEditorViewport::set_view_type(ViewType p_type) {
	view_type = p_type;
	switch (view_type) {
		case VIEW_PERSPECTIVE:
			camera->set_projection(Camera3D::PROJECTION_PERSPECTIVE);
			pivot = Vector3();
			yaw = Math::deg_to_rad(-45.0);
			pitch = Math::deg_to_rad(-30.0);
			distance = 20.0;
			break;
		case VIEW_TOP:
			camera->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
			pivot = Vector3();
			yaw = 0;
			pitch = Math::deg_to_rad(-90.0);
			distance = 40.0;
			break;
		case VIEW_FRONT:
			camera->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
			pivot = Vector3();
			yaw = 0;
			pitch = 0;
			distance = 40.0;
			break;
		case VIEW_SIDE:
			camera->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
			pivot = Vector3();
			yaw = Math::deg_to_rad(-90.0);
			pitch = 0;
			distance = 40.0;
			break;
	}
	_update_camera_transform();
}

void LevelEditorViewport::_update_camera_transform() {
	if (!camera) {
		return;
	}
	if (view_type == VIEW_PERSPECTIVE) {
		Basis rot(Vector3(0, 1, 0), yaw);
		rot.rotate(Vector3(1, 0, 0), pitch);
		Vector3 eye = pivot + rot.xform(Vector3(0, 0, distance));
		camera->look_at_from_position(eye, pivot, Vector3(0, 1, 0));
	} else {
		camera->set_size(distance);
		Basis rot(Vector3(0, 1, 0), yaw);
		rot.rotate(Vector3(1, 0, 0), pitch);
		Vector3 fwd = rot.xform(Vector3(0, 0, -1));
		Vector3 eye = pivot - fwd * 500.0;
		camera->set_transform(Transform3D(rot, eye).looking_at(pivot, Vector3(0, 1, 0)));
	}
	if (overlay) {
		overlay->update();
	}
}

void LevelEditorViewport::get_ray(const Vector2 &p_screen, Vector3 &r_origin, Vector3 &r_dir) const {
	r_origin = camera->project_ray_origin(p_screen);
	r_dir = camera->project_ray_normal(p_screen).normalized();
}

bool LevelEditorViewport::intersect_ortho_plane(const Vector2 &p_screen, Vector3 &r_hit) const {
	Vector3 ro, rd;
	get_ray(p_screen, ro, rd);
	Plane plane;
	switch (view_type) {
		case VIEW_TOP:
			plane = Plane(Vector3(0, 1, 0), 0);
			break;
		case VIEW_FRONT:
			plane = Plane(Vector3(0, 0, 1), 0);
			break;
		case VIEW_SIDE:
			plane = Plane(Vector3(1, 0, 0), 0);
			break;
		default:
			return false;
	}
	return plane.intersects_ray(ro, rd, &r_hit);
}

bool LevelEditorViewport::project(const Vector3 &p_world, Vector2 &r_screen) const {
	if (camera->is_position_behind(p_world)) {
		return false;
	}
	r_screen = camera->unproject_position(p_world);
	return true;
}

void LevelEditorViewport::queue_overlay_redraw() {
	if (overlay) {
		overlay->update();
	}
}

void LevelEditorViewport::focus_on(const AABB &p_aabb) {
	pivot = p_aabb.get_center();
	distance = MAX(p_aabb.get_longest_axis_size() * 2.0, 4.0);
	_update_camera_transform();
}

void LevelEditorViewport::_notification(int p_what) {
	if (p_what == NOTIFICATION_RESIZED) {
		if (overlay) {
			overlay->update();
		}
	}
}

void LevelEditorViewport::_draw_grid() {
	if (!overlay || view_type == VIEW_PERSPECTIVE) {
		return;
	}
	const real_t gs = (screen ? screen->get_grid_size() : 1.0);
	if (gs <= 0) {
		return;
	}

	Size2 sz = overlay->get_size();

	Vector3 w[4];
	Vector2 corners[4] = { Vector2(0, 0), Vector2(sz.x, 0), Vector2(sz.x, sz.y), Vector2(0, sz.y) };
	for (int i = 0; i < 4; i++) {
		if (!intersect_ortho_plane(corners[i], w[i])) {
			return;
		}
	}

	real_t min_a = (real_t)Math::INF, max_a = -(real_t)Math::INF;
	real_t min_b = (real_t)Math::INF, max_b = -(real_t)Math::INF;
	int axis_a = 0, axis_b = 1;
	switch (view_type) {
		case VIEW_TOP:
			axis_a = 0;
			axis_b = 2;
			break;
		case VIEW_FRONT:
			axis_a = 0;
			axis_b = 1;
			break;
		case VIEW_SIDE:
			axis_a = 2;
			axis_b = 1;
			break;
		default:
			break;
	}
	for (int i = 0; i < 4; i++) {
		min_a = MIN(min_a, w[i][axis_a]);
		max_a = MAX(max_a, w[i][axis_a]);
		min_b = MIN(min_b, w[i][axis_b]);
		max_b = MAX(max_b, w[i][axis_b]);
	}

	Color minor(1, 1, 1, 0.06);
	Color major(1, 1, 1, 0.15);
	Color axis_col(0.55, 0.75, 1.0, 0.5);

	int start_a = (int)Math::floor(min_a / gs);
	int end_a = (int)Math::ceil(max_a / gs);
	for (int i = start_a; i <= end_a; i++) {
		real_t a = i * gs;
		Vector3 p1, p2;
		p1[axis_a] = a;
		p2[axis_a] = a;
		p1[axis_b] = min_b;
		p2[axis_b] = max_b;
		Vector2 s1, s2;
		if (project(p1, s1) && project(p2, s2)) {
			bool is_axis = Math::is_zero_approx(a);
			bool is_major = (i % 8) == 0;
			overlay->draw_line(s1, s2, is_axis ? axis_col : (is_major ? major : minor), is_axis ? 2.0 : 1.0);
		}
	}

	int start_b = (int)Math::floor(min_b / gs);
	int end_b = (int)Math::ceil(max_b / gs);
	for (int i = start_b; i <= end_b; i++) {
		real_t b = i * gs;
		Vector3 p1, p2;
		p1[axis_b] = b;
		p2[axis_b] = b;
		p1[axis_a] = min_a;
		p2[axis_a] = max_a;
		Vector2 s1, s2;
		if (project(p1, s1) && project(p2, s2)) {
			bool is_axis = Math::is_zero_approx(b);
			bool is_major = (i % 8) == 0;
			overlay->draw_line(s1, s2, is_axis ? axis_col : (is_major ? major : minor), is_axis ? 2.0 : 1.0);
		}
	}
}

void LevelEditorViewport::gui_input(const Ref<InputEvent> &p_event) {
	if (!screen) {
		return;
	}
	screen->forward_input(camera, p_event);

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid()) {
		if (mb->get_button_index() == MouseButton::MIDDLE) {
			panning = mb->is_pressed();
			last_mouse = mb->get_position();
			accept_event();
		} else if (mb->get_button_index() == MouseButton::RIGHT) {
			orbiting = mb->is_pressed() && view_type == VIEW_PERSPECTIVE;
			last_mouse = mb->get_position();
			accept_event();
		} else if (mb->get_button_index() == MouseButton::WHEEL_UP && mb->is_pressed()) {
			distance = MAX(0.5, distance * 0.9);
			_update_camera_transform();
			accept_event();
		} else if (mb->get_button_index() == MouseButton::WHEEL_DOWN && mb->is_pressed()) {
			distance = MIN(2000.0, distance * 1.1);
			_update_camera_transform();
			accept_event();
		}
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		Vector2 rel = mm->get_position() - last_mouse;
		last_mouse = mm->get_position();

		if (panning) {
			Basis b = camera->get_global_transform().basis;
			real_t scale = distance * 0.0015;
			pivot += (-b[0] * rel.x + b[1] * rel.y) * scale;
			_update_camera_transform();
		} else if (orbiting) {
			yaw -= rel.x * 0.01;
			pitch = CLAMP(pitch - rel.y * 0.01, -Math::PI * 0.49, Math::PI * 0.49);
			_update_camera_transform();
		}
	}
}

// ---------------------------------------------------------------------------
// LevelEditorScreen
// ---------------------------------------------------------------------------

void LevelEditorScreen::_bind_methods() {
}

LevelEditorScreen::LevelEditorScreen() {
	set_name("Level");
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_process(true);

	toolbar = memnew(HBoxContainer);
	add_child(toolbar);

	static const char *mode_names[MODE_MAX] = { "Select", "Block", "Vertex", "Edge", "Face" };
	for (int i = 0; i < MODE_MAX; i++) {
		Button *b = memnew(Button);
		b->set_text(mode_names[i]);
		b->set_toggle_mode(true);
		b->set_pressed(i == 0);
		b->connect("pressed", callable_mp(this, &LevelEditorScreen::_mode_changed).bind(i));
		toolbar->add_child(b);
		mode_buttons[i] = b;
	}

	toolbar->add_child(memnew(VSeparator));

	Label *grid_label = memnew(Label);
	grid_label->set_text(TTRC("Grid:"));
	toolbar->add_child(grid_label);

	grid_size_spin = memnew(SpinBox);
	grid_size_spin->set_min(0.01);
	grid_size_spin->set_max(1000);
	grid_size_spin->set_step(0.25);
	grid_size_spin->set_value(1.0);
	grid_size_spin->set_custom_minimum_size(Size2(80, 0));
	grid_size_spin->connect("value_changed", callable_mp(this, &LevelEditorScreen::_grid_size_changed));
	toolbar->add_child(grid_size_spin);

	toolbar->add_child(memnew(VSeparator));

	Label *ext_label = memnew(Label);
	ext_label->set_text(TTRC("Extrude:"));
	toolbar->add_child(ext_label);

	extrude_spin = memnew(SpinBox);
	extrude_spin->set_min(-1000);
	extrude_spin->set_max(1000);
	extrude_spin->set_step(0.25);
	extrude_spin->set_value(1.0);
	extrude_spin->set_custom_minimum_size(Size2(80, 0));
	extrude_spin->connect("value_changed", callable_mp(this, &LevelEditorScreen::_extrude_amount_changed));
	toolbar->add_child(extrude_spin);

	extrude_button = memnew(Button);
	extrude_button->set_text(TTRC("Extrude"));
	extrude_button->connect("pressed", callable_mp(this, &LevelEditorScreen::_extrude_pressed));
	toolbar->add_child(extrude_button);

	toolbar->add_child(memnew(VSeparator));

	Label *mat_label = memnew(Label);
	mat_label->set_text(TTRC("Material:"));
	toolbar->add_child(mat_label);

	material_picker = memnew(EditorResourcePicker);
	material_picker->set_base_type("Material");
	material_picker->set_custom_minimum_size(Size2(160, 0));
	material_picker->connect("resource_changed", callable_mp(this, &LevelEditorScreen::_material_changed));
	toolbar->add_child(material_picker);

	apply_material_button = memnew(Button);
	apply_material_button->set_text(TTRC("Apply to Face"));
	apply_material_button->connect("pressed", callable_mp(this, &LevelEditorScreen::_apply_material_pressed));
	toolbar->add_child(apply_material_button);

	toolbar->add_child(memnew(VSeparator));

	bake_button = memnew(Button);
	bake_button->set_text(TTRC("Bake Level"));
	bake_button->set_tooltip_text(TTRC("Bake brushes to a MeshInstance3D with trimesh collision and an occluder."));
	bake_button->connect("pressed", callable_mp(this, &LevelEditorScreen::_bake_pressed));
	toolbar->add_child(bake_button);

	// Quad viewports.
	rows_split = memnew(VSplitContainer);
	rows_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(rows_split);

	top_split = memnew(HSplitContainer);
	bottom_split = memnew(HSplitContainer);
	rows_split->add_child(top_split);
	rows_split->add_child(bottom_split);

	for (int i = 0; i < 4; i++) {
		LevelEditorViewport *vp = memnew(LevelEditorViewport);
		vp->screen = this;
		vp->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		vp->set_custom_minimum_size(Size2(0, 160));
		viewports[i] = vp;
	}
	viewports[0]->set_view_type(LevelEditorViewport::VIEW_PERSPECTIVE);
	viewports[1]->set_view_type(LevelEditorViewport::VIEW_TOP);
	viewports[2]->set_view_type(LevelEditorViewport::VIEW_FRONT);
	viewports[3]->set_view_type(LevelEditorViewport::VIEW_SIDE);

	top_split->add_child(viewports[0]);
	top_split->add_child(viewports[1]);
	bottom_split->add_child(viewports[2]);
	bottom_split->add_child(viewports[3]);
}

void LevelEditorScreen::set_plugin(EditorPlugin *p_plugin) {
	plugin = p_plugin;
}

void LevelEditorScreen::set_selected_brush_from_editor(LevelBrush *p_brush) {
	if (!p_brush || p_brush == selected_brush) {
		return;
	}
	// Adopt the brush and, if it belongs to a map, adopt that map too.
	selected_brush = p_brush;
	LevelMap *map = Object::cast_to<LevelMap>(p_brush->get_parent());
	if (map && map != current_map) {
		current_map = map;
	}
	_update_overlays();
}

void LevelEditorScreen::make_visible(bool p_visible) {
	if (p_visible) {
		_resolve_map();
		_update_overlays();
	}
}

void LevelEditorScreen::_resolve_map() {
	if (current_map) {
		return;
	}
	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	if (!root) {
		return;
	}
	List<Node *> stack;
	stack.push_back(root);
	while (!stack.is_empty()) {
		Node *n = stack.front()->get();
		stack.pop_front();
		LevelMap *lm = Object::cast_to<LevelMap>(n);
		if (lm) {
			current_map = lm;
			current_map->refresh();
			return;
		}
		for (int i = 0; i < n->get_child_count(); i++) {
			stack.push_back(n->get_child(i));
		}
	}
}

LevelMap *LevelEditorScreen::_get_or_create_map() {
	_resolve_map();
	if (current_map) {
		return current_map;
	}
	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	ERR_FAIL_NULL_V(root, nullptr);

	current_map = memnew(LevelMap);
	current_map->set_name("LevelMap");
	root->add_child(current_map);
	current_map->set_owner(root);
	return current_map;
}

void LevelEditorScreen::_mode_changed(int p_mode) {
	_set_mode((Mode)p_mode);
}

void LevelEditorScreen::_set_mode(Mode p_mode) {
	mode = p_mode;
	for (int i = 0; i < MODE_MAX; i++) {
		mode_buttons[i]->set_pressed(i == (int)mode);
	}
	// Keep the brush; drop element selection when leaving element modes.
	if (mode == MODE_SELECT || mode == MODE_BLOCK) {
		selected_faces.clear();
		selected_edges.clear();
		selected_vertices.clear();
	}
	_update_overlays();
}

void LevelEditorScreen::_grid_size_changed(double p_value) {
	grid_size = MAX(0.01, (real_t)p_value);
	_update_overlays();
}

void LevelEditorScreen::_extrude_amount_changed(double p_value) {
	extrude_amount = (real_t)p_value;
}

void LevelEditorScreen::_material_changed(const Ref<Resource> &p_resource) {
	current_material = p_resource;
}

Vector3 LevelEditorScreen::_snap(const Vector3 &p_v) const {
	return Vector3(_snap(p_v.x), _snap(p_v.y), _snap(p_v.z));
}

real_t LevelEditorScreen::_snap(real_t p_v) const {
	return Math::snapped(p_v, grid_size);
}

void LevelEditorScreen::_clear_selection() {
	selected_brush = nullptr;
	selected_faces.clear();
	selected_edges.clear();
	selected_vertices.clear();
	hover_brush = nullptr;
	hover_face = -1;
	has_hover_edge = false;
	has_hover_vertex = false;
}

void LevelEditorScreen::_update_overlays() {
	for (int i = 0; i < 4; i++) {
		viewports[i]->queue_overlay_redraw();
	}
}

void LevelEditorScreen::_refresh_map() {
	if (current_map) {
		current_map->refresh();
	}
	_update_overlays();
}

bool LevelEditorScreen::_pick_face(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, int &r_face, Vector3 &r_hit) const {
	if (!current_map) {
		return false;
	}
	Vector3 ro = p_camera->project_ray_origin(p_screen);
	Vector3 rd = p_camera->project_ray_normal(p_screen).normalized();

	real_t best = (real_t)Math::INF;
	LevelBrush *best_brush = nullptr;
	int best_face = -1;
	Vector3 best_hit;

	Vector<LevelBrush *> brushes = current_map->get_brushes();
	for (LevelBrush *brush : brushes) {
		// Ray in brush-local space.
		Transform3D inv = brush->get_global_transform().affine_inverse();
		Vector3 lro = inv.xform(ro);
		Vector3 lrd = inv.basis.xform(rd).normalized();

		real_t d;
		int f = brush->ray_intersect(lro, lrd, d);
		if (f >= 0) {
			// Distance in world space: recompute from the world hit point.
			Vector3 world_hit = brush->get_global_transform().xform(lro + lrd * d);
			real_t wd = (world_hit - ro).length();
			if (wd < best) {
				best = wd;
				best_brush = brush;
				best_face = f;
				best_hit = world_hit;
			}
		}
	}
	if (best_brush) {
		r_brush = best_brush;
		r_face = best_face;
		r_hit = best_hit;
		return true;
	}
	return false;
}

bool LevelEditorScreen::_pick_vertex(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, Vector3 &r_vertex) const {
	if (!current_map || !selected_brush) {
		return false;
	}
	Transform3D gt = selected_brush->get_global_transform();
	Vector<Vector3> verts = selected_brush->get_vertices();

	real_t best = 16.0; // pixels.
	bool found = false;
	Vector3 best_v;
	for (const Vector3 &v : verts) {
		Vector3 w = gt.xform(v);
		if (p_camera->is_position_behind(w)) {
			continue;
		}
		Vector2 sp = p_camera->unproject_position(w);
		real_t d = sp.distance_to(p_screen);
		if (d < best) {
			best = d;
			best_v = v;
			found = true;
		}
	}
	if (found) {
		r_brush = selected_brush;
		r_vertex = best_v;
	}
	return found;
}

bool LevelEditorScreen::_pick_edge(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, LevelBrush::EdgeKey &r_edge) const {
	if (!current_map || !selected_brush) {
		return false;
	}
	Transform3D gt = selected_brush->get_global_transform();

	real_t best = 12.0; // pixels.
	bool found = false;
	LevelBrush::EdgeKey best_edge;

	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = selected_brush->get_edges();
	for (const LevelBrush::EdgeKey &e : edges) {
		Vector3 wa = gt.xform(e.a);
		Vector3 wb = gt.xform(e.b);
		if (p_camera->is_position_behind(wa) || p_camera->is_position_behind(wb)) {
			continue;
		}
		Vector2 sa = p_camera->unproject_position(wa);
		Vector2 sb = p_camera->unproject_position(wb);

		Vector2 ab = sb - sa;
		real_t len2 = ab.length_squared();
		real_t t = (len2 > 0) ? CLAMP((p_screen - sa).dot(ab) / len2, 0.0, 1.0) : 0.0;
		real_t d = (sa + ab * t).distance_to(p_screen);
		if (d < best) {
			best = d;
			best_edge = e;
			found = true;
		}
	}
	if (found) {
		r_brush = selected_brush;
		r_edge = best_edge;
	}
	return found;
}

void LevelEditorScreen::_update_hover(LevelEditorViewport *p_vp, const Vector2 &p_mouse) {
	hover_brush = nullptr;
	hover_face = -1;
	has_hover_edge = false;
	has_hover_vertex = false;

	Camera3D *cam = p_vp->get_camera();
	switch (mode) {
		case MODE_SELECT: {
			Vector3 hit;
			int f;
			_pick_face(cam, p_mouse, hover_brush, f, hit);
		} break;
		case MODE_FACE: {
			Vector3 hit;
			_pick_face(cam, p_mouse, hover_brush, hover_face, hit);
		} break;
		case MODE_VERTEX: {
			Vector3 v;
			has_hover_vertex = _pick_vertex(cam, p_mouse, hover_brush, v);
			if (has_hover_vertex) {
				hover_vertex = v;
			}
		} break;
		case MODE_EDGE: {
			has_hover_edge = _pick_edge(cam, p_mouse, hover_brush, hover_edge);
		} break;
		default:
			break;
	}
	_update_overlays();
}

void LevelEditorScreen::forward_input(Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	LevelEditorViewport *vp = nullptr;
	for (int i = 0; i < 4; i++) {
		if (viewports[i]->get_camera() == p_camera) {
			vp = viewports[i];
			break;
		}
	}
	if (!vp) {
		return;
	}

	Ref<InputEventMouseButton> mb = p_event;
	Ref<InputEventMouseMotion> mm = p_event;

	// --- Gizmo interaction takes priority (Select and element modes) ---
	if (mode != MODE_BLOCK && _has_selection()) {
		if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
			if (mb->is_pressed()) {
				int part = _pick_gizmo(p_camera, mb->get_position());
				if (part != GIZMO_NONE) {
					gizmo_drag_part = (GizmoPart)part;
					_gizmo_begin_drag(vp, mb->get_position());
					return; // Consumed by gizmo.
				}
			} else {
				if (gizmo_dragging) {
					_gizmo_end_drag();
					return;
				}
			}
		} else if (mm.is_valid()) {
			if (gizmo_dragging) {
				_gizmo_drag_to(vp, mm->get_position());
				return;
			} else {
				GizmoPart prev = gizmo_hover;
				gizmo_hover = (GizmoPart)_pick_gizmo(p_camera, mm->get_position());
				if (prev != gizmo_hover) {
					_update_overlays();
				}
			}
		}
	}

	if (mode == MODE_BLOCK) {
		if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
			if (mb->is_pressed()) {
				Vector3 hit;
				bool ok = false;
				if (vp->get_view_type() == LevelEditorViewport::VIEW_PERSPECTIVE) {
					Vector3 ro, rd;
					vp->get_ray(mb->get_position(), ro, rd);
					Plane ground(Vector3(0, 1, 0), 0);
					ok = ground.intersects_ray(ro, rd, &hit);
				} else {
					ok = vp->intersect_ortho_plane(mb->get_position(), hit);
				}
				if (ok) {
					drag_start = _snap(hit);
					dragging = true;
					drag_active = false;
					drag_viewport = vp;
					drag_current = drag_start;
				}
			} else {
				if (dragging && drag_viewport == vp) {
					if (drag_active) {
						_commit_drag();
					}
					dragging = false;
					drag_viewport = nullptr;
				}
			}
		} else if (mm.is_valid() && dragging && drag_viewport == vp) {
			Vector3 hit;
			bool ok = false;
			if (vp->get_view_type() == LevelEditorViewport::VIEW_PERSPECTIVE) {
				Vector3 ro, rd;
				vp->get_ray(mm->get_position(), ro, rd);
				Plane ground(Vector3(0, 1, 0), 0);
				ok = ground.intersects_ray(ro, rd, &hit);
			} else {
				ok = vp->intersect_ortho_plane(mm->get_position(), hit);
			}
			if (ok) {
				drag_current = _snap(hit);
				drag_active = (drag_current - drag_start).length() > grid_size * 0.5;
				_update_overlays();
			}
		}
		return;
	}

	// Select + element modes (skip while the gizmo is active).
	if (gizmo_dragging) {
		return;
	}
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT && mb->is_pressed()) {
		bool add = mb->is_shift_pressed();
		switch (mode) {
			case MODE_SELECT: {
				// Click a brush to select the whole node (gizmo moves it).
				Vector3 hit;
				LevelBrush *brush = nullptr;
				int f;
				if (_pick_face(p_camera, mb->get_position(), brush, f, hit)) {
					selected_brush = brush;
					EditorInterface::get_singleton()->edit_node(brush);
				} else if (!add) {
					_clear_selection();
				}
			} break;
			case MODE_FACE: {
				Vector3 hit;
				LevelBrush *brush = nullptr;
				int f;
				if (_pick_face(p_camera, mb->get_position(), brush, f, hit)) {
					if (brush != selected_brush) {
						selected_brush = brush;
						selected_faces.clear();
						EditorInterface::get_singleton()->edit_node(brush);
					}
					if (selected_faces.has(f) && add) {
						selected_faces.erase(f);
					} else {
						if (!add) {
							selected_faces.clear();
						}
						selected_faces.insert(f);
					}
				} else if (!add) {
					_clear_selection();
				}
			} break;
			case MODE_EDGE: {
				LevelBrush *brush = nullptr;
				LevelBrush::EdgeKey e;
				if (_pick_edge(p_camera, mb->get_position(), brush, e)) {
					if (brush != selected_brush) {
						selected_brush = brush;
						selected_edges.clear();
						EditorInterface::get_singleton()->edit_node(brush);
					}
					if (selected_edges.has(e) && add) {
						selected_edges.erase(e);
					} else {
						if (!add) {
							selected_edges.clear();
						}
						selected_edges.insert(e);
					}
				} else if (!add) {
					selected_edges.clear();
				}
			} break;
			case MODE_VERTEX: {
				LevelBrush *brush = nullptr;
				Vector3 v;
				if (_pick_vertex(p_camera, mb->get_position(), brush, v)) {
					if (brush != selected_brush) {
						selected_brush = brush;
						selected_vertices.clear();
						EditorInterface::get_singleton()->edit_node(brush);
					}
					bool already = false;
					for (int i = 0; i < selected_vertices.size(); i++) {
						if (LevelBrush::EdgeKey(selected_vertices[i], selected_vertices[i]) == LevelBrush::EdgeKey(v, v)) {
							already = true;
							if (add) {
								selected_vertices.remove_at(i);
							}
							break;
						}
					}
					if (!already) {
						if (!add) {
							selected_vertices.clear();
						}
						selected_vertices.push_back(v);
					}
				} else if (!add) {
					selected_vertices.clear();
				}
			} break;
			default:
				break;
		}
		_update_overlays();
	} else if (mm.is_valid()) {
		_update_hover(vp, mm->get_position());
	}
}

void LevelEditorScreen::_commit_drag() {
	if (!drag_active) {
		return;
	}

	Vector3 mins = drag_start.min(drag_current);
	Vector3 maxs = drag_start.max(drag_current);

	LevelEditorViewport::ViewType vt = drag_viewport->get_view_type();
	real_t thickness = grid_size;
	switch (vt) {
		case LevelEditorViewport::VIEW_TOP:
		case LevelEditorViewport::VIEW_PERSPECTIVE:
			mins.y = _snap(drag_start.y);
			maxs.y = mins.y + thickness;
			break;
		case LevelEditorViewport::VIEW_FRONT:
			mins.z = _snap(drag_start.z);
			maxs.z = mins.z + thickness;
			break;
		case LevelEditorViewport::VIEW_SIDE:
			mins.x = _snap(drag_start.x);
			maxs.x = mins.x + thickness;
			break;
	}

	for (int i = 0; i < 3; i++) {
		if (maxs[i] - mins[i] < CMP_EPSILON) {
			maxs[i] = mins[i] + thickness;
		}
	}

	LevelMap *map = _get_or_create_map();
	ERR_FAIL_NULL(map);

	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	ERR_FAIL_NULL(root);

	// Build the brush node in map-local space so it's positioned under the
	// cursor even if the map itself is transformed.
	Transform3D map_inv = map->get_global_transform().affine_inverse();
	AABB world_aabb(mins, maxs - mins);

	LevelBrush *brush = memnew(LevelBrush);
	brush->set_name("Brush");
	brush->setup_box(map_inv.xform(world_aabb));
	if (current_material.is_valid()) {
		for (int f = 0; f < brush->get_face_count(); f++) {
			brush->set_face_material(f, current_material);
		}
	}

	EditorUndoRedoManager::get_singleton()->create_action(TTR("Add Level Brush"));
	EditorUndoRedoManager::get_singleton()->add_do_method(map, "add_child", brush);
	EditorUndoRedoManager::get_singleton()->add_do_method(brush, "set_owner", root);
	EditorUndoRedoManager::get_singleton()->add_do_method(map, "refresh");
	EditorUndoRedoManager::get_singleton()->add_undo_method(map, "remove_child", brush);
	EditorUndoRedoManager::get_singleton()->add_undo_method(map, "refresh");
	EditorUndoRedoManager::get_singleton()->commit_action();

	selected_brush = brush;
	EditorInterface::get_singleton()->edit_node(brush);
	_refresh_map();
}

void LevelEditorScreen::_extrude_pressed() {
	if (!current_map || !selected_brush) {
		return;
	}

	// Snapshot the serialized plane data so undo can restore the previous solid
	// in one property write (safe even if the face count changes).
	PackedVector4Array old_data = selected_brush->get_planes_data();

	LevelBrush *working = selected_brush->duplicate_brush();
	bool did = false;
	switch (mode) {
		case MODE_FACE: {
			if (selected_faces.is_empty()) {
				break;
			}
			Vector<int> faces;
			for (int f : selected_faces) {
				faces.push_back(f);
			}
			working->extrude_faces(faces, extrude_amount);
			working->merge_coplanar_faces();
			did = true;
		} break;
		case MODE_EDGE: {
			if (selected_edges.is_empty()) {
				break;
			}
			for (const LevelBrush::EdgeKey &e : selected_edges) {
				working->extrude_edge(e, extrude_amount);
			}
			working->merge_coplanar_faces();
			did = true;
		} break;
		case MODE_VERTEX: {
			if (selected_vertices.is_empty()) {
				break;
			}
			for (const Vector3 &v : selected_vertices) {
				working->extrude_vertex(v, extrude_amount);
			}
			working->merge_coplanar_faces();
			did = true;
		} break;
		default:
			break;
	}

	if (!did) {
		memdelete(working);
		return;
	}

	LevelBrush *target = selected_brush;
	LevelMap *map = current_map;

	PackedVector4Array new_data = working->get_planes_data();

	EditorUndoRedoManager::get_singleton()->create_action(TTR("Extrude Brush"));
	EditorUndoRedoManager::get_singleton()->add_do_property(target, "planes", new_data);
	EditorUndoRedoManager::get_singleton()->add_do_method(map, "refresh");
	EditorUndoRedoManager::get_singleton()->add_undo_property(target, "planes", old_data);
	EditorUndoRedoManager::get_singleton()->add_undo_method(map, "refresh");
	EditorUndoRedoManager::get_singleton()->commit_action();
	memdelete(working);

	_refresh_map();
}

void LevelEditorScreen::_apply_material_pressed() {
	if (!current_map || !selected_brush || current_material.is_null()) {
		return;
	}

	LevelBrush *target = selected_brush;
	LevelMap *map = current_map;

	EditorUndoRedoManager::get_singleton()->create_action(TTR("Apply Brush Material"));

	if (mode == MODE_FACE && !selected_faces.is_empty()) {
		for (int f : selected_faces) {
			EditorUndoRedoManager::get_singleton()->add_do_method(target, "set_face_material", f, current_material);
			EditorUndoRedoManager::get_singleton()->add_undo_method(target, "set_face_material", f, target->get_face_material(f));
		}
	} else {
		for (int f = 0; f < target->get_face_count(); f++) {
			EditorUndoRedoManager::get_singleton()->add_do_method(target, "set_face_material", f, current_material);
			EditorUndoRedoManager::get_singleton()->add_undo_method(target, "set_face_material", f, target->get_face_material(f));
		}
	}
	EditorUndoRedoManager::get_singleton()->add_do_method(map, "refresh");
	EditorUndoRedoManager::get_singleton()->add_undo_method(map, "refresh");
	EditorUndoRedoManager::get_singleton()->commit_action();

	_refresh_map();
}

void LevelEditorScreen::_bake_pressed() {
	if (!current_map) {
		return;
	}
	Node3D *baked = current_map->bake();
	ERR_FAIL_NULL(baked);

	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	ERR_FAIL_NULL(root);

	EditorUndoRedoManager::get_singleton()->create_action(TTR("Bake Level"));
	EditorUndoRedoManager::get_singleton()->add_do_method(root, "add_child", baked);
	EditorUndoRedoManager::get_singleton()->add_do_method(baked, "set_owner", root);
	// Also give the baked children an owner so they persist.
	{
		List<Node *> stack;
		stack.push_back(baked);
		while (!stack.is_empty()) {
			Node *n = stack.front()->get();
			stack.pop_front();
			for (int i = 0; i < n->get_child_count(); i++) {
				EditorUndoRedoManager::get_singleton()->add_do_method(n->get_child(i), "set_owner", root);
				stack.push_back(n->get_child(i));
			}
		}
	}
	EditorUndoRedoManager::get_singleton()->add_undo_method(root, "remove_child", baked);
	EditorUndoRedoManager::get_singleton()->commit_action();

	EditorInterface::get_singleton()->edit_node(baked);
}

void LevelEditorScreen::_notification(int p_what) {
	if (p_what == NOTIFICATION_PROCESS) {
		// Drop dangling selection if the brush was deleted externally.
		if (selected_brush && !selected_brush->is_inside_tree()) {
			_clear_selection();
			_update_overlays();
		}
		if (current_map && !current_map->is_inside_tree()) {
			current_map = nullptr;
			_clear_selection();
		}
	}
}

// ---- Manipulation gizmo ---------------------------------------------------

bool LevelEditorScreen::_has_selection() const {
	if (!selected_brush) {
		return false;
	}
	switch (mode) {
		case MODE_SELECT:
			return true; // Whole brush is the selection.
		case MODE_FACE:
			return !selected_faces.is_empty();
		case MODE_EDGE:
			return !selected_edges.is_empty();
		case MODE_VERTEX:
			return !selected_vertices.is_empty();
		default:
			return false;
	}
}

Vector3 LevelEditorScreen::_get_gizmo_origin() const {
	if (!selected_brush) {
		return Vector3();
	}
	Transform3D gt = selected_brush->get_global_transform();

	if (mode == MODE_SELECT) {
		// Gizmo at the brush's geometry center, in world space.
		return gt.xform(selected_brush->get_center());
	}

	Vector3 sum;
	int count = 0;

	switch (mode) {
		case MODE_FACE: {
			for (int f : selected_faces) {
				Vector<Vector3> poly = selected_brush->get_face_polygon(f);
				for (const Vector3 &v : poly) {
					sum += gt.xform(v);
					count++;
				}
			}
		} break;
		case MODE_EDGE: {
			for (const LevelBrush::EdgeKey &e : selected_edges) {
				sum += gt.xform(e.a);
				sum += gt.xform(e.b);
				count += 2;
			}
		} break;
		case MODE_VERTEX: {
			for (const Vector3 &v : selected_vertices) {
				sum += gt.xform(v);
				count++;
			}
		} break;
		default:
			break;
	}
	return count > 0 ? sum / count : gt.get_origin();
}

int LevelEditorScreen::_pick_gizmo(Camera3D *p_camera, const Vector2 &p_screen) const {
	if (!_has_selection()) {
		return GIZMO_NONE;
	}
	Vector3 origin = _get_gizmo_origin();
	if (p_camera->is_position_behind(origin)) {
		return GIZMO_NONE;
	}
	Vector2 so = p_camera->unproject_position(origin);

	// Axis length in pixels (fixed screen size).
	const real_t axis_len = 64.0;
	const real_t axis_tol = 9.0;
	const real_t plane_tol = 12.0;
	const real_t center_tol = 7.0;

	// World-space scale so the axis projects to axis_len pixels.
	// Approximate: project origin + unit axis and measure pixel length.
	Vector3 axes[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };
	Vector2 axis_end[3];
	for (int i = 0; i < 3; i++) {
		Vector3 tip = origin + axes[i];
		if (p_camera->is_position_behind(tip)) {
			return GIZMO_NONE;
		}
		Vector2 st = p_camera->unproject_position(tip);
		axis_end[i] = so + (st - so).normalized() * axis_len;
	}

	// Check plane handles first (smaller targets).
	struct PlaneHandle {
		int a, b;
		GizmoPart part;
	};
	static const PlaneHandle planes[3] = {
		{ 0, 1, GIZMO_XY },
		{ 0, 2, GIZMO_XZ },
		{ 1, 2, GIZMO_YZ },
	};
	for (int p = 0; p < 3; p++) {
		Vector2 pa = so + (axis_end[planes[p].a] - so) * 0.5;
		Vector2 pb = so + (axis_end[planes[p].b] - so) * 0.5;
		Vector2 center = (so + pa + pb) / 3.0;
		if (p_screen.distance_to(center) < plane_tol) {
			return planes[p].part;
		}
	}

	// Axis lines.
	for (int i = 0; i < 3; i++) {
		Vector2 a = so;
		Vector2 b = axis_end[i];
		Vector2 ab = b - a;
		real_t len2 = ab.length_squared();
		real_t t = (len2 > 0) ? CLAMP((p_screen - a).dot(ab) / len2, 0.0, 1.0) : 0.0;
		real_t d = (a + ab * t).distance_to(p_screen);
		if (d < axis_tol) {
			return i;
		}
	}

	// Center: free move in the camera plane.
	if (p_screen.distance_to(so) < center_tol) {
		// Pick the plane most facing the camera for a natural drag.
		Vector3 cam_fwd = -p_camera->get_global_transform().basis[2];
		Vector3 ac = cam_fwd.abs();
		if (ac.z >= ac.x && ac.z >= ac.y) {
			return GIZMO_XY;
		} else if (ac.y >= ac.x) {
			return GIZMO_XZ;
		}
		return GIZMO_YZ;
	}
	return GIZMO_NONE;
}

void LevelEditorScreen::_gizmo_begin_drag(LevelEditorViewport *p_vp, const Vector2 &p_mouse) {
	gizmo_dragging = true;
	gizmo_drag_viewport = p_vp;
	gizmo_drag_mouse_start = p_mouse;
	gizmo_drag_start_origin = _get_gizmo_origin();

	// Snapshot brush planes for undo.
	gizmo_drag_original_planes.clear();
	for (int f = 0; f < selected_brush->get_face_count(); f++) {
		gizmo_drag_original_planes.push_back(selected_brush->get_face_plane(f));
	}
	gizmo_drag_original_position = selected_brush->get_position();

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
		const Vector3 &p1 = gizmo_drag_start_origin; // Point on axis line.
		const Vector3 d1 = axis;
		const Vector3 &p2 = ro;
		const Vector3 d2 = rd;

		Vector3 r = p1 - p2;
		real_t a = d1.dot(d1);
		real_t b = d1.dot(d2);
		real_t c = d2.dot(d2);
		real_t d = d1.dot(r);
		real_t e = d2.dot(r);
		real_t denom = a * c - b * b;
		if (Math::is_zero_approx(denom)) {
			return;
		}
		real_t t = (b * e - c * d) / denom; // Parameter along the axis line.
		Vector3 axis_point = p1 + d1 * t;
		delta = _snap(axis_point) - _snap(gizmo_drag_start_origin);
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
		Vector3 snapped_hit = _snap(hit);
		Vector3 snapped_start = _snap(gizmo_drag_start_origin);
		delta = snapped_hit - snapped_start;

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

	_apply_gizmo_delta(delta);
	_update_overlays();
}

void LevelEditorScreen::_apply_gizmo_delta(const Vector3 &p_world_delta) {
	if (!selected_brush) {
		return;
	}

	if (mode == MODE_SELECT) {
		// Move the whole brush node. Delta is world; convert into the parent
		// (map) space and apply on top of the drag-start position.
		Node3D *parent = Object::cast_to<Node3D>(selected_brush->get_parent());
		Vector3 local = p_world_delta;
		if (parent) {
			local = parent->get_global_transform().affine_inverse().basis.xform(p_world_delta);
		}
		selected_brush->set_position(gizmo_drag_original_position + local);
		_refresh_map();
		return;
	}

	// Restore the original planes first, then apply the new delta. This makes
	// dragging absolute (not accumulative).
	for (int f = 0; f < gizmo_drag_original_planes.size(); f++) {
		selected_brush->set_face_plane(f, gizmo_drag_original_planes[f]);
	}

	// World delta -> brush-local delta (direction only).
	Transform3D inv = selected_brush->get_global_transform().affine_inverse();
	Vector3 local_delta = inv.basis.xform(p_world_delta);

	// Deformation move: the selected elements themselves move; planes tilt.
	Vector<Vector3> verts;
	Vector<LevelBrush::EdgeKey> edges;
	Vector<int> faces;
	switch (mode) {
		case MODE_FACE:
			for (int f : selected_faces) {
				faces.push_back(f);
			}
			break;
		case MODE_EDGE:
			for (const LevelBrush::EdgeKey &e : selected_edges) {
				edges.push_back(e);
			}
			break;
		case MODE_VERTEX:
			for (const Vector3 &v : selected_vertices) {
				verts.push_back(v);
			}
			break;
		default:
			break;
	}
	selected_brush->move_elements(verts, edges, faces, local_delta);
	_refresh_map();
}

void LevelEditorScreen::_gizmo_end_drag() {
	if (!gizmo_dragging) {
		return;
	}
	gizmo_dragging = false;

	if (!selected_brush) {
		return;
	}

	// Select mode: undoable node move.
	if (mode == MODE_SELECT) {
		Vector3 new_pos = selected_brush->get_position();
		if (!new_pos.is_equal_approx(gizmo_drag_original_position)) {
			LevelBrush *target = selected_brush;
			Vector3 old_pos = gizmo_drag_original_position;
			EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
			undo_redo->create_action(TTR("Move Brush"));
			undo_redo->add_do_property(target, "position", new_pos);
			undo_redo->add_undo_property(target, "position", old_pos);
			undo_redo->commit_action(false);
		}
		return;
	}

	if (gizmo_drag_original_planes.is_empty()) {
		return;
	}

	// Commit the move as an undo action using the serialized planes property:
	// one do/undo pair covers the whole solid, and survives face-count changes.
	LevelBrush *target = selected_brush;
	LevelMap *map = current_map;

	PackedVector4Array new_data = target->get_planes_data();
	PackedVector4Array old_data;
	old_data.resize(gizmo_drag_original_planes.size());
	for (int i = 0; i < gizmo_drag_original_planes.size(); i++) {
		const Plane &p = gizmo_drag_original_planes[i];
		old_data.set(i, Vector4(p.normal.x, p.normal.y, p.normal.z, p.d));
	}

	if (new_data == old_data) {
		return; // Nothing actually moved.
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Move Brush Element"));
	undo_redo->add_do_property(target, "planes", new_data);
	undo_redo->add_do_method(map, "refresh");
	undo_redo->add_undo_property(target, "planes", old_data);
	undo_redo->add_undo_method(map, "refresh");
	// commit_action with execute=false because the brush is already in the "do" state.
	undo_redo->commit_action(false);

	gizmo_drag_original_planes.clear();
}

void LevelEditorScreen::_draw_gizmo(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (mode == MODE_BLOCK || !_has_selection()) {
		return; // No gizmo in Block mode (draw-only tool).
	}
	Vector3 origin = _get_gizmo_origin();
	Vector2 so;
	if (!p_vp->project(origin, so)) {
		return;
	}

	Camera3D *cam = p_vp->get_camera();
	Vector3 axes[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };
	Color axis_col[3] = { Color(0.95, 0.3, 0.3), Color(0.4, 0.9, 0.4), Color(0.3, 0.6, 1.0) };

	const real_t axis_len = 64.0;
	Vector2 axis_end[3];
	for (int i = 0; i < 3; i++) {
		Vector3 tip = origin + axes[i];
		if (cam->is_position_behind(tip)) {
			return;
		}
		Vector2 st = cam->unproject_position(tip);
		axis_end[i] = so + (st - so).normalized() * axis_len;
	}

	// Plane handles (quads at halfway between axes).
	struct PlaneHandle {
		int a, b;
		GizmoPart part;
	};
	static const PlaneHandle planes[3] = {
		{ 0, 1, GIZMO_XY },
		{ 0, 2, GIZMO_XZ },
		{ 1, 2, GIZMO_YZ },
	};
	for (int p = 0; p < 3; p++) {
		Vector2 pa = so + (axis_end[planes[p].a] - so) * 0.45;
		Vector2 pb = so + (axis_end[planes[p].b] - so) * 0.45;
		PackedVector2Array quad;
		quad.push_back(so);
		quad.push_back(pa);
		quad.push_back(pa + (pb - so));
		quad.push_back(pb);
		Color c = axis_col[planes[p].a].lerp(axis_col[planes[p].b], 0.5);
		c.a = (gizmo_hover == planes[p].part || gizmo_drag_part == planes[p].part) ? 0.55 : 0.22;
		p_canvas->draw_colored_polygon(quad, c);
	}

	// Axis lines with arrowheads.
	for (int i = 0; i < 3; i++) {
		bool active = (gizmo_hover == (GizmoPart)i || gizmo_drag_part == (GizmoPart)i);
		Color c = active ? Color(1, 1, 1) : axis_col[i];
		p_canvas->draw_line(so, axis_end[i], c, active ? 3.0 : 2.0);
		// Arrowhead.
		Vector2 dir = (axis_end[i] - so).normalized();
		Vector2 perp(-dir.y, dir.x);
		p_canvas->draw_line(axis_end[i], axis_end[i] - dir * 10 + perp * 4, c, 2.0);
		p_canvas->draw_line(axis_end[i], axis_end[i] - dir * 10 - perp * 4, c, 2.0);
	}

	// Center square.
	p_canvas->draw_rect(Rect2(so - Vector2(4, 4), Size2(8, 8)), Color(1, 1, 1, 0.9));
}

// ---- Drawing --------------------------------------------------------------

void LevelEditorScreen::_draw_viewport_overlay(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (!current_map) {
		return;
	}

	Vector<LevelBrush *> brushes = current_map->get_brushes();
	for (LevelBrush *b : brushes) {
		_draw_brush_outline(p_vp, p_canvas, b, b == selected_brush);
	}
	if (mode == MODE_SELECT && hover_brush && hover_brush != selected_brush) {
		// Hover highlight (thin white).
		Transform3D gt = hover_brush->get_global_transform();
		HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = hover_brush->get_edges();
		for (const LevelBrush::EdgeKey &e : edges) {
			Vector2 a, b;
			if (p_vp->project(gt.xform(e.a), a) && p_vp->project(gt.xform(e.b), b)) {
				p_canvas->draw_line(a, b, Color(1, 1, 1, 0.5), 1.0);
			}
		}
	}
	_draw_drag_feedback(p_vp, p_canvas);
	_draw_selection(p_vp, p_canvas);
	_draw_gizmo(p_vp, p_canvas);
}

void LevelEditorScreen::_draw_brush_outline(LevelEditorViewport *p_vp, Control *p_canvas, LevelBrush *p_brush, bool p_selected) {
	Transform3D gt = p_brush->get_global_transform();

	Color col = p_selected ? Color(1.0, 0.6, 0.1, 0.9) : Color(0.9, 0.9, 0.9, 0.35);
	real_t width = p_selected ? 2.0 : 1.0;

	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = p_brush->get_edges();
	for (const LevelBrush::EdgeKey &e : edges) {
		Vector2 a, b;
		if (p_vp->project(gt.xform(e.a), a) && p_vp->project(gt.xform(e.b), b)) {
			p_canvas->draw_line(a, b, col, width);
		}
	}
}

void LevelEditorScreen::_draw_drag_feedback(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (!dragging || !drag_active || !drag_viewport) {
		return;
	}

	Vector3 mins = drag_start.min(drag_current);
	Vector3 maxs = drag_start.max(drag_current);
	LevelEditorViewport::ViewType vt = drag_viewport->get_view_type();
	real_t thickness = grid_size;
	switch (vt) {
		case LevelEditorViewport::VIEW_TOP:
		case LevelEditorViewport::VIEW_PERSPECTIVE:
			mins.y = drag_start.y;
			maxs.y = mins.y + thickness;
			break;
		case LevelEditorViewport::VIEW_FRONT:
			mins.z = drag_start.z;
			maxs.z = mins.z + thickness;
			break;
		case LevelEditorViewport::VIEW_SIDE:
			mins.x = drag_start.x;
			maxs.x = mins.x + thickness;
			break;
	}

	LevelBrush *preview = memnew(LevelBrush);
	preview->setup_box(AABB(mins, maxs - mins));
	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = preview->get_edges();
	memdelete(preview);

	Color col(0.2, 0.9, 0.4, 0.9);
	for (const LevelBrush::EdgeKey &e : edges) {
		Vector2 a, b;
		if (p_vp->project(e.a, a) && p_vp->project(e.b, b)) {
			p_canvas->draw_line(a, b, col, 2.0);
		}
	}

	if (p_vp == drag_viewport) {
		Vector2 s0, s1;
		if (p_vp->project(drag_start, s0) && p_vp->project(drag_current, s1)) {
			Rect2 r(s0, s1 - s0);
			r = r.abs();
			p_canvas->draw_rect(r, Color(0.2, 0.9, 0.4, 0.6), false, 1.0);
		}
	}
}

void LevelEditorScreen::_draw_selection(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (!current_map || !selected_brush) {
		return;
	}

	Transform3D gt = selected_brush->get_global_transform();

	// Faces.
	Color face_col(1.0, 0.45, 0.1, 0.22);
	Color face_outline(1.0, 0.6, 0.1, 0.95);
	for (int f : selected_faces) {
		Vector<Vector3> poly = selected_brush->get_face_polygon(f);
		if (poly.size() < 3) {
			continue;
		}
		PackedVector2Array pts;
		bool all_front = true;
		for (const Vector3 &v : poly) {
			Vector2 sp;
			if (!p_vp->project(gt.xform(v), sp)) {
				all_front = false;
				break;
			}
			pts.push_back(sp);
		}
		if (all_front) {
			p_canvas->draw_colored_polygon(pts, face_col);
			for (int i = 0; i < pts.size(); i++) {
				p_canvas->draw_line(pts[i], pts[(i + 1) % pts.size()], face_outline, 2.0);
			}
		}
	}

	// Edges.
	Color edge_col(0.2, 0.8, 1.0, 0.95);
	for (const LevelBrush::EdgeKey &e : selected_edges) {
		Vector2 a, b;
		if (p_vp->project(gt.xform(e.a), a) && p_vp->project(gt.xform(e.b), b)) {
			p_canvas->draw_line(a, b, edge_col, 3.0);
		}
	}

	// Vertices.
	Color vert_col(1.0, 0.9, 0.2, 1.0);
	for (const Vector3 &v : selected_vertices) {
		Vector2 sp;
		if (p_vp->project(gt.xform(v), sp)) {
			p_canvas->draw_rect(Rect2(sp - Vector2(4, 4), Size2(8, 8)), vert_col);
		}
	}

	// Hover highlight.
	Color hover_col(1, 1, 1, 0.5);
	switch (mode) {
		case MODE_FACE: {
			if (hover_brush && hover_face >= 0) {
				Transform3D hgt = hover_brush->get_global_transform();
				Vector<Vector3> poly = hover_brush->get_face_polygon(hover_face);
				PackedVector2Array pts;
				bool ok = true;
				for (const Vector3 &v : poly) {
					Vector2 sp;
					if (!p_vp->project(hgt.xform(v), sp)) {
						ok = false;
						break;
					}
					pts.push_back(sp);
				}
				if (ok && pts.size() >= 3) {
					for (int i = 0; i < pts.size(); i++) {
						p_canvas->draw_line(pts[i], pts[(i + 1) % pts.size()], hover_col, 1.0);
					}
				}
			}
		} break;
		case MODE_EDGE: {
			if (has_hover_edge && hover_brush == selected_brush) {
				Vector2 a, b;
				if (p_vp->project(gt.xform(hover_edge.a), a) && p_vp->project(gt.xform(hover_edge.b), b)) {
					p_canvas->draw_line(a, b, hover_col, 1.0);
				}
			}
		} break;
		case MODE_VERTEX: {
			if (has_hover_vertex && hover_brush == selected_brush) {
				Vector2 sp;
				if (p_vp->project(gt.xform(hover_vertex), sp)) {
					p_canvas->draw_rect(Rect2(sp - Vector2(4, 4), Size2(8, 8)), hover_col, false, 1.0);
				}
			}
		} break;
		default:
			break;
	}
}

// ---------------------------------------------------------------------------
// LevelEditorPlugin
// ---------------------------------------------------------------------------

const Ref<Texture2D> LevelEditorPlugin::get_plugin_icon() const {
	return EditorInterface::get_singleton()->get_base_control()->get_theme_icon(SNAME("Editor3DHandle"), SNAME("EditorIcons"));
}

LevelEditorPlugin::LevelEditorPlugin() {
	screen = memnew(LevelEditorScreen);
	screen->set_plugin(this);
	screen->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	EditorNode::get_singleton()->get_editor_main_screen()->get_control()->add_child(screen);
	screen->hide();

	EditorInterface::get_singleton()->get_selection()->connect("selection_changed", callable_mp(this, &LevelEditorPlugin::_editor_selection_changed));
}

void LevelEditorPlugin::_notification(int p_what) {
}

void LevelEditorPlugin::_editor_selection_changed() {
	// Mirror the editor's node selection into the level editor: if a
	// LevelBrush node gets selected (e.g. clicked in the 3D tab or scene
	// tree), make it the active brush here too.
	TypedArray<Node> sel = EditorInterface::get_singleton()->get_selection()->get_selected_nodes();
	for (int i = 0; i < sel.size(); i++) {
		LevelBrush *b = Object::cast_to<LevelBrush>(sel[i]);
		if (b) {
			screen->set_selected_brush_from_editor(b);
			return;
		}
	}
}

void LevelEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		screen->show();
		screen->make_visible(true);
	} else {
		screen->hide();
	}
}
