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
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/inspector/editor_resource_picker.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/gui/button.h"
#include "scene/gui/control.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/resources/environment.h"

// ---------------------------------------------------------------------------
// LevelEditorViewport
// ---------------------------------------------------------------------------

LevelEditorViewport::LevelEditorViewport() {
	set_stretch(true);
	set_focus_mode(FOCUS_ALL);
	set_clip_contents(true);

	subviewport = memnew(SubViewport);
	subviewport->set_disable_input(true);
	subviewport->set_handle_input_locally(false);
	add_child(subviewport);

	camera = memnew(Camera3D);
	camera->set_current(true);
	camera->set_far(4000.0);
	subviewport->add_child(camera);

	light = memnew(DirectionalLight3D);
	// Aim the sun so it shines downward and slightly from the side: tops lit,
	// bottoms in shade. (DirectionalLight3D shines along its -Z axis.)
	light->look_at_from_position(Vector3(10, 20, 10), Vector3(0, 0, 0), Vector3(0, 1, 0));
	subviewport->add_child(light);

	// Fill light from below/opposite so undersides aren't pitch black.
	DirectionalLight3D *fill = memnew(DirectionalLight3D);
	fill->look_at_from_position(Vector3(-10, -5, -10), Vector3(0, 0, 0), Vector3(0, 1, 0));
	subviewport->add_child(fill);

	world_env = memnew(WorldEnvironment);
	Ref<Environment> env;
	env.instantiate();
	env->set_background(Environment::BG_COLOR);
	env->set_bg_color(Color(0.16, 0.16, 0.18));
	env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
	env->set_ambient_light_color(Color(0.45, 0.45, 0.45));
	env->set_ambient_light_energy(1.0);
	world_env->set_environment(env);
	subviewport->add_child(world_env);

	overlay = memnew(Overlay);
	overlay->viewport = this;
	overlay->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	overlay->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	add_child(overlay);

	view_controller.instantiate();
	// Match the 3D editor's navigation settings.
	view_controller->set_pan_mouse_button((View3DController::NavigationMouseButton)(int)EDITOR_GET("editors/3d/navigation/pan_mouse_button"));
	view_controller->set_orbit_sensitivity(EDITOR_GET("editors/3d/navigation_feel/orbit_sensitivity"));
	view_controller->set_orbit_inertia(EDITOR_GET("editors/3d/navigation_feel/orbit_inertia"));
	view_controller->set_orbit_mouse_button((View3DController::NavigationMouseButton)(int)EDITOR_GET("editors/3d/navigation/orbit_mouse_button"));
	view_controller->set_zoom_style((View3DController::ZoomStyle)(int)EDITOR_GET("editors/3d/navigation/zoom_style"));
	view_controller->set_zoom_inertia(EDITOR_GET("editors/3d/navigation_feel/zoom_inertia"));
	view_controller->set_zoom_mouse_button((View3DController::NavigationMouseButton)(int)EDITOR_GET("editors/3d/navigation/zoom_mouse_button"));
	view_controller->set_freelook_scheme((View3DController::FreelookScheme)(int)EDITOR_GET("editors/3d/freelook/freelook_navigation_scheme"));
	view_controller->set_freelook_base_speed(EDITOR_GET("editors/3d/freelook/freelook_base_speed"));
	view_controller->set_freelook_sensitivity(EDITOR_GET("editors/3d/freelook/freelook_sensitivity"));
	view_controller->set_freelook_inertia(EDITOR_GET("editors/3d/freelook/freelook_inertia"));
	view_controller->set_freelook_speed_zoom_link(EDITOR_GET("editors/3d/freelook/freelook_speed_zoom_link"));
	view_controller->set_freelook_invert_y_axis(EDITOR_GET("editors/3d/freelook/freelook_invert_y_axis"));
	view_controller->set_translation_sensitivity(EDITOR_GET("editors/3d/navigation_feel/translation_sensitivity"));
	view_controller->set_translation_inertia(EDITOR_GET("editors/3d/navigation_feel/translation_inertia"));
	view_controller->set_z_near(camera->get_near());
	view_controller->set_z_far(camera->get_far());

	// Freelook movement keys (WASD/QE + modifiers) - reuse the 3D editor's
	// shortcuts so user remaps apply here too.
	view_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_FORWARD, ED_GET_SHORTCUT("spatial_editor/freelook_forward"));
	view_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_BACKWARDS, ED_GET_SHORTCUT("spatial_editor/freelook_backwards"));
	view_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_LEFT, ED_GET_SHORTCUT("spatial_editor/freelook_left"));
	view_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_RIGHT, ED_GET_SHORTCUT("spatial_editor/freelook_right"));
	view_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_UP, ED_GET_SHORTCUT("spatial_editor/freelook_up"));
	view_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_DOWN, ED_GET_SHORTCUT("spatial_editor/freelook_down"));
	view_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_SPEED_MOD, ED_GET_SHORTCUT("spatial_editor/freelook_speed_modifier"));
	view_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_SLOW_MOD, ED_GET_SHORTCUT("spatial_editor/freelook_slow_modifier"));

	set_process(true);
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
	pivot = Vector3();
	switch (view_type) {
		case VIEW_PERSPECTIVE:
			camera->set_projection(Camera3D::PROJECTION_PERSPECTIVE);
			view_controller->cursor = View3DController::Cursor();
			view_controller->cursor.distance = 20.0;
			view_controller->set_view_type(View3DController::VIEW_TYPE_USER);
			break;
		case VIEW_TOP:
			camera->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
			distance = 40.0;
			break;
		case VIEW_FRONT:
			camera->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
			distance = 40.0;
			break;
		case VIEW_SIDE:
			camera->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
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
		if (view_controller.is_valid()) {
			view_controller->update_camera();
			camera->set_global_transform(view_controller->to_camera_transform());
		}
	} else {
		camera->set_size(distance);
		real_t yaw = 0.0, pitch = 0.0;
		switch (view_type) {
			case VIEW_TOP:
				pitch = Math::deg_to_rad(-90.0);
				break;
			case VIEW_FRONT:
				break;
			case VIEW_SIDE:
				yaw = Math::deg_to_rad(-90.0);
				break;
			default:
				break;
		}
		Basis rot(Vector3(0, 1, 0), yaw);
		rot.rotate(Vector3(1, 0, 0), pitch);
		Vector3 fwd = rot.xform(Vector3(0, 0, -1));
		Vector3 eye = pivot - fwd * 500.0;
		// The basis already points the camera at the pivot; using looking_at()
		// here would break the top view (view dir colinear with the up axis).
		camera->set_transform(Transform3D(rot, eye));
	}
	if (overlay) {
		overlay->update();
	}
}

void LevelEditorViewport::_process_freelook(double p_delta) {
	if (view_type != VIEW_PERSPECTIVE || !view_controller.is_valid()) {
		return;
	}
	if (view_controller->is_freelook_enabled()) {
		view_controller->update_freelook((float)p_delta);
		camera->set_global_transform(view_controller->to_camera_transform());
		if (overlay) {
			overlay->update();
		}
	}
}

void LevelEditorViewport::get_ray(const Vector2 &p_screen, Vector3 &r_origin, Vector3 &r_dir) const {
	r_origin = camera->project_ray_origin(p_screen);
	r_dir = camera->project_ray_normal(p_screen).normalized();
}

bool LevelEditorViewport::ray_to_view_plane(const Vector2 &p_screen, const Vector3 &p_point, Vector3 &r_hit) const {
	Vector3 ro, rd;
	get_ray(p_screen, ro, rd);
	Plane pl;
	switch (view_type) {
		case VIEW_TOP:
		case VIEW_PERSPECTIVE:
			pl = Plane(Vector3(0, 1, 0), p_point.y);
			break;
		case VIEW_FRONT:
			pl = Plane(Vector3(0, 0, 1), p_point.z);
			break;
		case VIEW_SIDE:
			pl = Plane(Vector3(1, 0, 0), p_point.x);
			break;
		default:
			return false;
	}
	return pl.intersects_ray(ro, rd, &r_hit);
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

void LevelEditorViewport::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_RESIZED: {
			if (overlay) {
				overlay->update();
			}
		} break;
		case NOTIFICATION_PROCESS: {
			_process_freelook(get_process_delta_time());
		} break;
		case NOTIFICATION_WM_WINDOW_FOCUS_OUT: {
			if (view_controller.is_valid()) {
				view_controller->set_freelook_enabled(false);
			}
		} break;
	}
}

void LevelEditorViewport::_draw_grid() {
	if (!overlay) {
		return;
	}
	const real_t gs = (screen ? screen->get_grid_size() : 1.0);
	if (gs <= 0) {
		return;
	}

	Color minor(1, 1, 1, 0.06);
	Color major(1, 1, 1, 0.15);
	Color axis_col(0.55, 0.75, 1.0, 0.5);

	if (view_type == VIEW_PERSPECTIVE) {
		// Ground plane (Y = 0) grid over a fixed extent. Segments are clipped
		// against the camera near plane first so they don't vanish when one
		// end goes behind the camera (flying low over the grid).
		const int extent = 64; // Grid lines span -extent..extent world units.
		const real_t near_z = -(real_t)camera->get_near() - 0.01;

		Transform3D cam_inv = camera->get_global_transform().affine_inverse();

		int start = (int)Math::floor(-extent / gs);
		int end = (int)Math::ceil(extent / gs);
		for (int i = start; i <= end; i++) {
			real_t a = i * gs;
			bool is_axis = Math::is_zero_approx(a);
			bool is_major = (i % 8) == 0;
			Color col = is_axis ? axis_col : (is_major ? major : minor);
			real_t width = is_axis ? 2.0 : 1.0;

			for (int seg = 0; seg < 2; seg++) {
				Vector3 p1 = (seg == 0) ? Vector3(a, 0, -extent) : Vector3(-extent, 0, a);
				Vector3 p2 = (seg == 0) ? Vector3(a, 0, extent) : Vector3(extent, 0, a);

				// Clip against the near plane in camera space (z <= near_z is visible).
				Vector3 c1 = cam_inv.xform(p1);
				Vector3 c2 = cam_inv.xform(p2);
				bool b1 = c1.z > near_z;
				bool b2 = c2.z > near_z;
				if (b1 && b2) {
					continue; // Fully behind the near plane.
				}
				if (b1 != b2) {
					real_t t = (near_z - c1.z) / (c2.z - c1.z);
					if (b1) {
						c1 = c1.lerp(c2, t);
					} else {
						c2 = c1.lerp(c2, t);
					}
				}

				Vector2 s1, s2;
				if (project(camera->get_global_transform().xform(c1), s1) && project(camera->get_global_transform().xform(c2), s2)) {
					overlay->draw_line(s1, s2, col, width);
				}
			}
		}
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

void LevelEditorViewport::shortcut_input(const Ref<InputEvent> &p_event) {
	// Swallow keys we handle so editor-level shortcuts (e.g. scene-tree
	// Delete) don't also fire while the Level screen is active.
	Ref<InputEventKey> k = p_event;
	if (k.is_valid() && k->is_pressed()) {
		Key code = k->get_keycode();
		if (code == Key::KEY_DELETE || code == Key::BRACKETLEFT || code == Key::BRACKETRIGHT ||
				code == Key::ENTER || code == Key::KP_ENTER || code == Key::ESCAPE) {
			accept_event();
		}
	}
}

void LevelEditorViewport::gui_input(const Ref<InputEvent> &p_event) {
	if (!screen) {
		return;
	}
	// Keep keyboard focus on the screen so editor-level shortcuts (like the
	// scene tree's Delete) don't fire while working here.
	Ref<InputEventMouseButton> focus_mb = p_event;
	if (focus_mb.is_valid() && focus_mb->is_pressed() && !screen->has_focus()) {
		screen->grab_focus();
	}
	screen->forward_input(camera, p_event);

	if (view_type == VIEW_PERSPECTIVE) {
		// RMB hold -> freelook (same as the 3D editor viewport).
		Ref<InputEventMouseButton> rmb = p_event;
		if (rmb.is_valid() && rmb->get_button_index() == MouseButton::RIGHT) {
			view_controller->set_freelook_enabled(rmb->is_pressed());
			if (rmb->is_pressed()) {
				grab_focus();
			}
		}

		view_controller->gui_input(p_event, get_global_rect());
		// Always resync - wheel zoom and other non-"navigating" inputs still
		// change the controller's cursor.
		_update_camera_transform();
		// Camera may have moved (orbit/pan/zoom) - redraw the grid overlay.
		if (overlay) {
			overlay->update();
		}
		return;
	}

	// Ortho views: MMB pan, wheel zoom.
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid()) {
		if (mb->get_button_index() == MouseButton::MIDDLE) {
			panning = mb->is_pressed();
			last_mouse = mb->get_position();
			accept_event();
		} else if ((mb->get_button_index() == MouseButton::WHEEL_UP || mb->get_button_index() == MouseButton::WHEEL_DOWN) && mb->is_pressed()) {
			// Zoom centered on the mouse: keep the world point under the cursor
			// fixed on screen while the ortho size changes.
			Vector3 before;
			if (intersect_ortho_plane(mb->get_position(), before)) {
				distance = (mb->get_button_index() == MouseButton::WHEEL_UP) ? MAX(0.5, distance * 0.9) : MIN(2000.0, distance * 1.1);
				_update_camera_transform();
				Vector3 after;
				if (intersect_ortho_plane(mb->get_position(), after)) {
					pivot += before - after;
				}
				_update_camera_transform();
			} else {
				distance = (mb->get_button_index() == MouseButton::WHEEL_UP) ? MAX(0.5, distance * 0.9) : MIN(2000.0, distance * 1.1);
				_update_camera_transform();
			}
			accept_event();
		}
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		Vector2 rel = mm->get_position() - last_mouse;
		last_mouse = mm->get_position();

		if (panning) {
			// Pan 1:1 with the mouse: convert pixels to world units using the
			// ortho projection (size = world height of the viewport). The camera
			// up-axis points in world -Z in the top view, so dragging up must
			// move the view "down" in world space to track the cursor - match
			// the perspective viewport by using the screen-aligned direction.
			Basis b = camera->get_global_transform().basis;
			real_t world_per_pixel = distance / MAX(1.0, get_size().y);
			// Content should track the cursor. Some ortho views have camera
			// axes that read as inverted versus the freelook viewport's pan
			// (top: up is world -Z; side: right is world -Z), so flip those.
			Vector3 right = (view_type == VIEW_SIDE) ? -b[0] : b[0];
			Vector3 up = (view_type == VIEW_TOP) ? -b[1] : b[1];
			pivot += (-right * rel.x + up * rel.y) * world_per_pixel;
			_update_camera_transform();
		}
	}
}

// ---------------------------------------------------------------------------
// LevelEditorScreen
// ---------------------------------------------------------------------------

// Shared box-drawing helpers (used by ghost, select handles, and dim labels).
namespace {

void aabb_corners(const AABB &p_aabb, Vector3 r_corners[8]) {
	const Vector3 c = p_aabb.get_center();
	const Vector3 hs = p_aabb.size * 0.5;
	for (int i = 0; i < 8; i++) {
		r_corners[i] = c + Vector3((i & 1) ? hs.x : -hs.x, (i & 2) ? hs.y : -hs.y, (i & 4) ? hs.z : -hs.z);
	}
}

// The 12 edges of a box as index pairs into aabb_corners().
const int AABB_EDGE_IDX[12][2] = {
	{ 0, 1 },
	{ 1, 3 },
	{ 3, 2 },
	{ 2, 0 },
	{ 4, 5 },
	{ 5, 7 },
	{ 7, 6 },
	{ 6, 4 },
	{ 0, 4 },
	{ 1, 5 },
	{ 2, 6 },
	{ 3, 7 },
};

// Outward direction per face handle index (0..5: -x, +x, -y, +y, -z, +z).
const Vector3 AABB_FACE_DIRS[6] = {
	Vector3(-1, 0, 0),
	Vector3(1, 0, 0),
	Vector3(0, -1, 0),
	Vector3(0, 1, 0),
	Vector3(0, 0, -1),
	Vector3(0, 0, 1),
};

Vector3 aabb_face_center(const AABB &p_aabb, int p_face) {
	const Vector3 c = p_aabb.get_center();
	const Vector3 hs = p_aabb.size * 0.5;
	return c + AABB_FACE_DIRS[p_face] * Vector3(hs.x, hs.y, hs.z);
}

} // namespace

void LevelEditorScreen::_bind_methods() {
}

LevelEditorScreen::LevelEditorScreen() {
	set_name("Level");
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_process(true);
	set_process_input(true);
	set_focus_mode(FOCUS_ALL);

	MarginContainer *toolbar_margin = memnew(MarginContainer);
	toolbar_margin->add_theme_constant_override("margin_top", 1 * EDSCALE);
	toolbar_margin->add_theme_constant_override("margin_bottom", 1 * EDSCALE);
	toolbar_margin->set_custom_maximum_size(Size2(-1, 36 * EDSCALE));
	toolbar_margin->set_theme_type_variation("MainToolBarMargin");
	add_child(toolbar_margin);

	toolbar = memnew(HBoxContainer);
	toolbar_margin->add_child(toolbar);

	// Tool modes in button-group panels (Select, Rotate, Scale) / (Block, Clip)...
	PanelContainer *tool_panel = memnew(PanelContainer);
	tool_panel->set_theme_type_variation("PanelContainerButtonGroup");
	toolbar->add_child(tool_panel);
	HBoxContainer *tool_hbox = memnew(HBoxContainer);
	tool_panel->add_child(tool_hbox);

	for (int i = MODE_SELECT; i <= MODE_SCALE; i++) {
		Button *b = memnew(Button);
		b->set_toggle_mode(true);
		b->set_pressed(i == 0);
		b->connect("pressed", callable_mp(this, &LevelEditorScreen::_mode_changed).bind(i));
		b->set_theme_type_variation(SceneStringName(FlatButton));
		tool_hbox->add_child(b);
		mode_buttons[i] = b;
	}

	toolbar->add_child(memnew(VSeparator));

	PanelContainer *draw_panel = memnew(PanelContainer);
	draw_panel->set_theme_type_variation("PanelContainerButtonGroup");
	toolbar->add_child(draw_panel);
	HBoxContainer *draw_hbox = memnew(HBoxContainer);
	draw_panel->add_child(draw_hbox);

	for (int i = MODE_BLOCK; i <= MODE_CLIP; i++) {
		Button *b = memnew(Button);
		b->set_toggle_mode(true);
		b->set_pressed(false);
		b->connect("pressed", callable_mp(this, &LevelEditorScreen::_mode_changed).bind(i));
		b->set_theme_type_variation(SceneStringName(FlatButton));
		draw_hbox->add_child(b);
		mode_buttons[i] = b;
	}

	toolbar->add_child(memnew(VSeparator));

	// ...and element modes in a second panel (Vertex, Edge, Face).
	PanelContainer *element_panel = memnew(PanelContainer);
	element_panel->set_theme_type_variation("PanelContainerButtonGroup");
	toolbar->add_child(element_panel);
	HBoxContainer *element_hbox = memnew(HBoxContainer);
	element_panel->add_child(element_hbox);

	for (int i = MODE_VERTEX; i <= MODE_FACE; i++) {
		Button *b = memnew(Button);
		b->set_toggle_mode(true);
		b->set_pressed(false);
		b->connect("pressed", callable_mp(this, &LevelEditorScreen::_mode_changed).bind(i));
		b->set_theme_type_variation(SceneStringName(FlatButton));
		element_hbox->add_child(b);
		mode_buttons[i] = b;
	}

	// Icons are (re)assigned in NOTIFICATION_THEME_CHANGED. Text labels are
	// fallbacks for buttons without icons.
	mode_buttons[MODE_BLOCK]->set_tooltip_text(TTRC("Block"));
	mode_buttons[MODE_CLIP]->set_tooltip_text(TTRC("Clip"));
	mode_buttons[MODE_VERTEX]->set_tooltip_text(TTRC("Vertex"));
	mode_buttons[MODE_EDGE]->set_tooltip_text(TTRC("Edge"));
	mode_buttons[MODE_FACE]->set_tooltip_text(TTRC("Face"));

	// Set shortcuts for buttons
	mode_buttons[MODE_SELECT]->set_shortcut(ED_SHORTCUT("spatial_editor/tool_transform", TTRC("Select / Move Mode"), Key::Q, true));
	mode_buttons[MODE_ROTATE]->set_shortcut(ED_SHORTCUT("spatial_editor/tool_rotate", TTRC("Rotate Mode"), Key::E, true));
	mode_buttons[MODE_SCALE]->set_shortcut(ED_SHORTCUT("spatial_editor/tool_scale", TTRC("Scale Mode"), Key::R, true));

	toolbar->add_child(memnew(VSeparator));

	Label *grid_label = memnew(Label);
	grid_label->set_text(TTRC("Grid:"));
	toolbar->add_child(grid_label);

	grid_size_spin = memnew(SpinBox);
	grid_size_spin->set_min(0.015625); // 1/64
	grid_size_spin->set_max(64);
	grid_size_spin->set_step(0.015625);
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

	flip_faces_button = memnew(Button);
	flip_faces_button->set_text(TTRC("Flip Faces"));
	flip_faces_button->set_tooltip_text(TTRC("Invert the selected brush's face normals - turns a solid block into an interior room shell."));
	flip_faces_button->connect("pressed", callable_mp(this, &LevelEditorScreen::_flip_faces_pressed));
	toolbar->add_child(flip_faces_button);

	bake_button = memnew(Button);
	bake_button->set_text(TTRC("Bake Level"));
	bake_button->set_tooltip_text(TTRC("Bake brushes to a MeshInstance3D with trimesh collision and an occluder."));
	bake_button->connect("pressed", callable_mp(this, &LevelEditorScreen::_bake_pressed));
	toolbar->add_child(bake_button);

	toolbar->add_child(memnew(VSeparator));

	tools_menu = memnew(MenuButton);
	tools_menu->set_text(TTRC("Tools"));
	tools_menu->set_flat(false);
	PopupMenu *tools_popup = tools_menu->get_popup();
	tools_popup->add_item(TTRC("Bridge Edge"), 0);
	tools_popup->connect("id_pressed", callable_mp(this, &LevelEditorScreen::_tools_menu_selected));
	toolbar->add_child(tools_menu);

	// Quad viewports: main vertical split with two horizontal splits inside,
	// all with nested dragger intersections enabled - grabbing the center
	// intersection drags both axes at once (like the 3D editor's quad view).
	rows_split = memnew(SplitContainer);
	rows_split->set_vertical(true);
	rows_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	rows_split->set_drag_nested_intersections(true);
	add_child(rows_split);

	top_split = memnew(SplitContainer);
	top_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	top_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	top_split->set_drag_nested_intersections(true);
	bottom_split = memnew(SplitContainer);
	bottom_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	bottom_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	bottom_split->set_drag_nested_intersections(true);
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

	// Shown instead of the quad viewports when the edited scene has no
	// LevelMap yet.
	no_map_panel = memnew(MarginContainer);
	no_map_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	no_map_panel->hide();
	add_child(no_map_panel);

	VBoxContainer *no_map_vbox = memnew(VBoxContainer);
	no_map_vbox->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	no_map_panel->add_child(no_map_vbox);

	no_map_label = memnew(Label);
	no_map_label->set_text(TTRC("This scene does not contain a LevelMap node. Create one to begin editing."));
	no_map_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	no_map_vbox->add_child(no_map_label);

	create_map_button = memnew(Button);
	create_map_button->set_text(TTRC("Create LevelMap"));
	create_map_button->set_h_size_flags(SIZE_SHRINK_CENTER);
	create_map_button->connect("pressed", callable_mp(this, &LevelEditorScreen::_create_map_pressed));
	no_map_vbox->add_child(create_map_button);

	top_split->add_child(viewports[0]);
	top_split->add_child(viewports[1]);
	bottom_split->add_child(viewports[2]);
	bottom_split->add_child(viewports[3]);
}

void LevelEditorScreen::input(const Ref<InputEvent> &p_event) {
	// The _vp_input phase runs BEFORE gui/shortcut dispatch (and before the
	// scene dock's no-context Delete shortcut), so swallow our keys here
	// when the Level screen is visible.
	if (!is_visible_in_tree()) {
		return;
	}
	Ref<InputEventKey> k = p_event;
	if (!k.is_valid() || !k->is_pressed() || k->is_echo()) {
		return;
	}
	switch (k->get_keycode()) {
		case Key::KEY_DELETE: {
			_delete_selection();
			get_viewport()->set_input_as_handled();
		} break;
		case Key::BRACKETLEFT:
		case Key::BRACKETRIGHT: {
			static const real_t steps[] = {
				0.015625,
				0.03125,
				0.0625,
				0.125,
				0.25,
				0.5,
				1.0,
				2.0,
				4.0,
				8.0,
				16.0,
				32.0,
				64.0,
			};
			const int step_count = (int)(sizeof(steps) / sizeof(steps[0]));
			int idx = 0;
			for (int i = 0; i < step_count; i++) {
				if (steps[i] <= grid_size) {
					idx = i;
				}
			}
			idx += (k->get_keycode() == Key::BRACKETRIGHT) ? 1 : -1;
			idx = CLAMP(idx, 0, step_count - 1);
			if (steps[idx] != grid_size) {
				grid_size_spin->set_value(steps[idx]);
			}
			get_viewport()->set_input_as_handled();
		} break;
		case Key::ENTER:
		case Key::KP_ENTER: {
			bool handled = false;
			if (ghost_active) {
				_ghost_commit();
				handled = true;
			} else if (clip_active) {
				_clip_apply();
				handled = true;
			}
			if (handled) {
				get_viewport()->set_input_as_handled();
			}
		} break;
		case Key::ESCAPE: {
			bool handled = false;
			if (ghost_active) {
				_ghost_cancel();
				handled = true;
			} else if (clip_active) {
				_clip_cancel();
				handled = true;
			} else if (dragging) {
				dragging = false;
				drag_active = false;
				drag_viewport = nullptr;
				_update_overlays();
				handled = true;
			}
			if (handled) {
				get_viewport()->set_input_as_handled();
			}
		} break;
		default:
			break;
	}
}

void LevelEditorScreen::shortcut_input(const Ref<InputEvent> &p_event) {
	// Keys handled by the level editor are consumed here so no-context
	// editor shortcuts (like the scene tree's Delete) never see them.
	// Actual handling happens in input() (the earlier _vp_input phase).
	Ref<InputEventKey> k = p_event;
	if (!k.is_valid() || !k->is_pressed()) {
		return;
	}
	Key code = k->get_keycode();
	if (code == Key::KEY_DELETE || code == Key::BRACKETLEFT || code == Key::BRACKETRIGHT ||
			code == Key::ENTER || code == Key::KP_ENTER || code == Key::ESCAPE) {
		accept_event();
	}
}

void LevelEditorScreen::_edit_brush_node(LevelBrush *p_brush) {
	// Show the brush in the inspector, but keep keyboard focus on the level
	// screen so editor shortcuts (e.g. scene-tree Delete) don't hijack keys.
	EditorInterface::get_singleton()->edit_node(p_brush);
	call_deferred("grab_focus");
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
		_update_map_ui();
	}
	_update_overlays();
}

void LevelEditorScreen::make_visible(bool p_visible) {
	if (p_visible) {
		_update_map_ui();
		_update_overlays();
		grab_focus();
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

void LevelEditorScreen::on_scene_changed() {
	current_map = nullptr;
	_clear_selection();
	_update_map_ui();
	_update_overlays();
}

void LevelEditorScreen::_update_warning_color() {
	if (no_map_label && is_inside_tree()) {
		no_map_label->add_theme_color_override(SceneStringName(font_color), no_map_label->get_theme_color(SNAME("warning_color"), EditorStringName(Editor)));
	}
}

void LevelEditorScreen::_update_map_ui() {
	if (!current_map) {
		// Fresh scene: only adopt a map found in the edited scene tree - never
		// auto-create one (the user must press "Create LevelMap").
		LevelMap *found = nullptr;
		Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
		if (root) {
			List<Node *> stack;
			stack.push_back(root);
			while (!stack.is_empty()) {
				Node *n = stack.front()->get();
				stack.pop_front();
				LevelMap *lm = Object::cast_to<LevelMap>(n);
				if (lm) {
					found = lm;
					break;
				}
				for (int i = 0; i < n->get_child_count(); i++) {
					stack.push_back(n->get_child(i));
				}
			}
		}
		if (found) {
			current_map = found;
			current_map->refresh();
		}
	}

	bool has_map = current_map != nullptr;
	if (rows_split) {
		rows_split->set_visible(has_map);
	}
	if (no_map_panel) {
		no_map_panel->set_visible(!has_map);
	}
}

void LevelEditorScreen::_create_map_pressed() {
	create_map_button->release_focus();
	_get_or_create_map();
	_update_map_ui();
	_update_overlays();
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

void LevelEditorScreen::_update_mode_icons() {
	if (mode_buttons[MODE_SELECT]) {
		mode_buttons[MODE_SELECT]->set_button_icon(get_editor_theme_icon(SNAME("ToolSelect")));
		mode_buttons[MODE_ROTATE]->set_button_icon(get_editor_theme_icon(SNAME("ToolRotate")));
		mode_buttons[MODE_SCALE]->set_button_icon(get_editor_theme_icon(SNAME("ToolScale")));
		mode_buttons[MODE_BLOCK]->set_button_icon(get_editor_theme_icon(SNAME("Object")));
		mode_buttons[MODE_CLIP]->set_button_icon(get_editor_theme_icon(SNAME("EditAddRemove")));
		mode_buttons[MODE_VERTEX]->set_button_icon(get_editor_theme_icon(SNAME("ControlAlignCenterLeft")));
		mode_buttons[MODE_EDGE]->set_button_icon(get_editor_theme_icon(SNAME("ControlAlignRightWide")));
		mode_buttons[MODE_FACE]->set_button_icon(get_editor_theme_icon(SNAME("ControlAlignFullRect")));
	}
}

void LevelEditorScreen::_mode_changed(int p_mode) {
	// Clicking the Clip button again while a clip is active cycles
	// keep-left / keep-right / keep-both (like Hammer's clip tool).
	if ((Mode)p_mode == MODE_CLIP && mode == MODE_CLIP && clip_active) {
		_clip_cycle_side();
		mode_buttons[MODE_CLIP]->set_pressed(true);
	} else {
		_set_mode((Mode)p_mode);
	}
	// Don't leave keyboard focus on the toolbar buttons, so Enter/Esc/etc.
	// go to the viewports instead of re-triggering the button.
	for (int i = 0; i < MODE_MAX; i++) {
		mode_buttons[i]->release_focus();
	}
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
	if (mode != MODE_BLOCK && ghost_active) {
		_ghost_cancel();
	}
	if (mode != MODE_CLIP && clip_active) {
		_clip_cancel();
	}
	_update_overlays();
}

void LevelEditorScreen::_grid_size_changed(double p_value) {
	grid_size = MAX(0.015625, (real_t)p_value);
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

void LevelEditorScreen::_delete_selection() {
	if (!current_map || !selected_brush) {
		return;
	}

	switch (mode) {
		case MODE_SELECT: {
			// Delete the whole brush node.
			LevelBrush *target = selected_brush;
			LevelMap *map = current_map;
			_clear_selection();

			EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
			undo_redo->create_action(TTR("Delete Brush"));
			undo_redo->add_do_method(map, "remove_child", target);
			undo_redo->add_do_method(map, "refresh");
			undo_redo->add_undo_method(map, "add_child", target);
			undo_redo->add_undo_method(map, "refresh");
			undo_redo->add_undo_reference(target);
			undo_redo->commit_action();
			_refresh_map();
		} break;
		case MODE_FACE: {
			if (selected_faces.is_empty()) {
				return;
			}
			LevelBrush *target = selected_brush;
			PackedVector3Array old_verts = target->get_vertices_data();
			Array old_faces = target->get_faces_data();
			Array old_mats = target->get_face_materials_data();

			Vector<int> faces;
			for (int f : selected_faces) {
				faces.push_back(f);
			}
			target->delete_faces(faces);
			selected_faces.clear();
			_commit_brush_undo(TTR("Delete Faces"), target, old_verts, old_faces, old_mats);
			_refresh_map();
		} break;
		case MODE_EDGE: {
			if (selected_edges.is_empty()) {
				return;
			}
			// Collapse the edges' vertices (merges each edge to a point).
			LevelBrush *target = selected_brush;
			PackedVector3Array old_verts = target->get_vertices_data();
			Array old_faces = target->get_faces_data();
			Array old_mats = target->get_face_materials_data();

			Vector<int> verts;
			HashSet<int> seen;
			for (const LevelBrush::EdgeKey &e : selected_edges) {
				if (!seen.has(e.a)) {
					verts.push_back(e.a);
					seen.insert(e.a);
				}
				if (!seen.has(e.b)) {
					verts.push_back(e.b);
					seen.insert(e.b);
				}
			}
			target->collapse_vertices(verts);
			selected_edges.clear();
			_commit_brush_undo(TTR("Delete Edges"), target, old_verts, old_faces, old_mats);
			_refresh_map();
		} break;
		case MODE_VERTEX: {
			if (selected_vertices.is_empty()) {
				return;
			}
			LevelBrush *target = selected_brush;
			PackedVector3Array old_verts = target->get_vertices_data();
			Array old_faces = target->get_faces_data();
			Array old_mats = target->get_face_materials_data();

			Vector<int> verts;
			for (int v : selected_vertices) {
				verts.push_back(v);
			}
			target->collapse_vertices(verts);
			selected_vertices.clear();
			_commit_brush_undo(TTR("Delete Vertices"), target, old_verts, old_faces, old_mats);
			_refresh_map();
		} break;
		default:
			break;
	}
	_update_overlays();
}

void LevelEditorScreen::_commit_brush_undo(const String &p_action, LevelBrush *p_brush, const PackedVector3Array &p_old_verts, const Array &p_old_faces, const Array &p_old_mats, bool p_execute) {
	PackedVector3Array new_verts = p_brush->get_vertices_data();
	Array new_faces = p_brush->get_faces_data();
	Array new_mats = p_brush->get_face_materials_data();

	LevelMap *map = current_map;
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(p_action);
	undo_redo->add_do_property(p_brush, "vertices", new_verts);
	undo_redo->add_do_property(p_brush, "faces", new_faces);
	undo_redo->add_do_property(p_brush, "face_materials", new_mats);
	undo_redo->add_do_method(map, "refresh");
	undo_redo->add_undo_property(p_brush, "vertices", p_old_verts);
	undo_redo->add_undo_property(p_brush, "faces", p_old_faces);
	undo_redo->add_undo_property(p_brush, "face_materials", p_old_mats);
	undo_redo->add_undo_method(map, "refresh");
	undo_redo->commit_action(p_execute);
}

void LevelEditorScreen::_clear_selection() {
	selected_brush = nullptr;
	select_handle_hover = GHOST_NONE;
	select_handle_drag = GHOST_NONE;
	select_moving = false;
	select_move_viewport = nullptr;
	rotate_hover_axis = -1;
	rotate_drag_axis = -1;
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

bool LevelEditorScreen::_pick_vertex(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, int &r_vertex) const {
	if (!current_map || !selected_brush) {
		return false;
	}
	Transform3D gt = selected_brush->get_global_transform();

	real_t best = 16.0; // pixels.
	bool found = false;
	int best_v = -1;
	for (int i = 0; i < selected_brush->get_vertex_count(); i++) {
		Vector3 w = gt.xform(selected_brush->get_vertex(i));
		if (p_camera->is_position_behind(w)) {
			continue;
		}
		Vector2 sp = p_camera->unproject_position(w);
		real_t d = sp.distance_to(p_screen);
		if (d < best) {
			best = d;
			best_v = i;
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
		Vector3 wa = gt.xform(selected_brush->get_vertex(e.a));
		Vector3 wb = gt.xform(selected_brush->get_vertex(e.b));
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
			has_hover_vertex = _pick_vertex(cam, p_mouse, hover_brush, hover_vertex);
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
	if (!current_map) {
		return; // No map yet - tool input is disabled.
	}
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

	// Delete/brackets are handled by LevelEditorScreen::shortcut_input (so
	// the scene dock can't hijack them); skip them here to avoid double-
	// handling when this viewport has focus.
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() &&
			(key->get_keycode() == Key::KEY_DELETE || key->get_keycode() == Key::BRACKETLEFT || key->get_keycode() == Key::BRACKETRIGHT)) {
		return;
	}

	// --- Select-mode box handles take priority over the move gizmo ---
	if (mode == MODE_SELECT && selected_brush) {
		if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
			if (mb->is_pressed()) {
				int h = _pick_select_handle(vp, mb->get_position());
				if (h != GHOST_NONE) {
					select_handle_drag = h;
					select_drag_viewport = vp;
					select_drag_original_aabb = _get_brush_local_aabb(selected_brush);
					return;
				}
			} else {
				if (select_handle_drag != GHOST_NONE) {
					_select_handle_end_drag();
					return;
				}
			}
		} else if (mm.is_valid()) {
			if (select_handle_drag != GHOST_NONE && select_drag_viewport == vp) {
				_select_handle_drag_to(vp, mm->get_position());
				return;
			} else {
				int prev = select_handle_hover;
				select_handle_hover = _pick_select_handle(vp, mm->get_position());
				if (prev != select_handle_hover) {
					_update_overlays();
				}
			}
		}
	}

	// --- Rotate gizmo interaction (Rotate mode) ---
	if (mode == MODE_ROTATE && selected_brush) {
		if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
			if (mb->is_pressed()) {
				int axis = _pick_rotate_ring(vp, mb->get_position());
				if (axis < 0 && vp->get_view_type() != LevelEditorViewport::VIEW_PERSPECTIVE) {
					// Ortho views: click anywhere to rotate around the view axis.
					switch (vp->get_view_type()) {
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
					rotate_drag_viewport = vp;
					rotate_drag_start_angle = _rotate_screen_angle(vp, mb->get_position(), axis);
					gizmo_drag_original_verts = selected_brush->get_vertices_data();
					return;
				}
			} else {
				if (rotate_drag_axis >= 0) {
					_rotate_end_drag();
					return;
				}
			}
		} else if (mm.is_valid()) {
			if (rotate_drag_axis >= 0 && rotate_drag_viewport == vp) {
				real_t cur = _rotate_screen_angle(vp, mm->get_position(), rotate_drag_axis);
				real_t delta = cur - rotate_drag_start_angle;
				// Snap to 15 degrees.
				delta = Math::snapped(delta, Math::deg_to_rad(15.0));
				_apply_gizmo_rotate(rotate_drag_axis, delta);
				_update_overlays();
				return;
			} else {
				int prev = rotate_hover_axis;
				rotate_hover_axis = _pick_rotate_ring(vp, mm->get_position());
				if (prev != rotate_hover_axis) {
					_update_overlays();
				}
			}
		}
	}

	// --- Gizmo interaction takes priority (Select and element modes) ---
	if (mode != MODE_BLOCK && mode != MODE_CLIP && mode != MODE_ROTATE && _has_selection()) {
		if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
			if (mb->is_pressed()) {
				int part = _pick_gizmo(p_camera, mb->get_position());
				if (part != GIZMO_NONE) {
					gizmo_drag_uniform_scale = false;
					gizmo_drag_part = (GizmoPart)part;
					_gizmo_begin_drag(vp, mb->get_position());
					return; // Consumed by gizmo.
				} else if (mode == MODE_SCALE) {
					// Off-gizmo click in Scale mode: drag anywhere to scale
					// uniformly via mouse X.
					gizmo_drag_uniform_scale = true;
					gizmo_drag_part = GIZMO_NONE;
					_gizmo_begin_drag(vp, mb->get_position());
					return;
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
		// --- Stage 2: ghost box with resize handles ---
		if (ghost_active) {
			if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
				if (mb->is_pressed()) {
					int h = _pick_ghost_handle(vp, mb->get_position());
					if (h != GHOST_NONE) {
						ghost_handle_drag = h;
						ghost_drag_viewport = vp;
					} else if (_ghost_hit_test(vp, mb->get_position())) {
						// Clicked inside the ghost: drag the whole box.
						ghost_moving = true;
						ghost_drag_viewport = vp;
						Vector3 hit;
						if (_ghost_ray_to_edit_plane(vp, mb->get_position(), hit)) {
							ghost_move_offset = hit - ghost_aabb.position;
						} else {
							ghost_move_offset = Vector3();
						}
					}
				} else {
					ghost_handle_drag = GHOST_NONE;
					ghost_moving = false;
					ghost_drag_viewport = nullptr;
				}
				return;
			}
			if (mm.is_valid()) {
				if (ghost_moving && ghost_drag_viewport == vp) {
					Vector3 hit;
					if (_ghost_ray_to_edit_plane(vp, mm->get_position(), hit)) {
						Vector3 new_pos = _snap(hit - ghost_move_offset);
						ghost_aabb.position = new_pos;
						_update_overlays();
					}
				} else if (ghost_handle_drag != GHOST_NONE && ghost_drag_viewport == vp) {
					_ghost_handle_drag_to(vp, mm->get_position());
				} else {
					int prev = ghost_handle_hover;
					ghost_handle_hover = _pick_ghost_handle(vp, mm->get_position());
					if (prev != ghost_handle_hover) {
						_update_overlays();
					}
				}
				return;
			}
			Ref<InputEventKey> k = p_event;
			if (k.is_valid() && k->is_pressed()) {
				if (k->get_keycode() == Key::ENTER || k->get_keycode() == Key::KP_ENTER) {
					_ghost_commit();
					return;
				}
				if (k->get_keycode() == Key::ESCAPE) {
					_ghost_cancel();
					return;
				}
			}
			return;
		}

		// --- Stage 1: initial drag ---
		// Esc cancels an in-progress drag.
		Ref<InputEventKey> k = p_event;
		if (k.is_valid() && k->is_pressed() && k->get_keycode() == Key::ESCAPE && dragging) {
			dragging = false;
			drag_active = false;
			drag_viewport = nullptr;
			_update_overlays();
			return;
		}
		if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
			if (mb->is_pressed()) {
				Vector3 hit;
				if (vp->ray_to_view_plane(mb->get_position(), Vector3(), hit)) {
					drag_start = _snap(hit);
					dragging = true;
					drag_active = false;
					drag_viewport = vp;
					drag_current = drag_start;
				}
			} else {
				if (dragging && drag_viewport == vp) {
					if (drag_active) {
						// Enter ghost state instead of committing immediately.
						Vector3 mins, maxs;
						_compute_drag_aabb(mins, maxs);
						ghost_aabb = AABB(mins, maxs - mins);
						ghost_active = true;
						ghost_handle_hover = GHOST_NONE;
						ghost_handle_drag = GHOST_NONE;
					}
					dragging = false;
					drag_viewport = nullptr;
					_update_overlays();
				}
			}
		} else if (mm.is_valid() && dragging && drag_viewport == vp) {
			Vector3 hit;
			if (vp->ray_to_view_plane(mm->get_position(), Vector3(), hit)) {
				drag_current = _snap(hit);
				drag_active = (drag_current - drag_start).length() > grid_size * 0.5;
				_update_overlays();
			}
		}
		return;
	}

	// --- Clip mode ---
	if (mode == MODE_CLIP) {
		// Keys: Enter applies, Esc cancels. (Side cycling is on the Clip
		// toolbar button - Tab is eaten by GUI focus navigation.)
		Ref<InputEventKey> k = p_event;
		if (k.is_valid() && k->is_pressed() && clip_active) {
			if (k->get_keycode() == Key::ENTER || k->get_keycode() == Key::KP_ENTER) {
				_clip_apply();
				return;
			}
			if (k->get_keycode() == Key::ESCAPE) {
				_clip_cancel();
				return;
			}
		}

		if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
			if (mb->is_pressed()) {
				if (clip_active && !clip_drawing) {
					// Grab a clip point to adjust.
					int pi = _pick_clip_point(vp, mb->get_position());
					if (pi >= 0) {
						clip_drag_point = pi;
						clip_viewport = vp;
						return;
					}
				}
				// Otherwise start a new clip on the clicked brush.
				Vector3 hit;
				LevelBrush *brush = nullptr;
				int f;
				if (_pick_face(p_camera, mb->get_position(), brush, f, hit)) {
					_clip_begin(brush, hit, vp);
				} else if (vp->get_view_type() != LevelEditorViewport::VIEW_PERSPECTIVE && current_map) {
					// Ortho views: click anywhere - use the selected brush (or the
					// most recent one) and place the point on the edit plane.
					LevelBrush *target = selected_brush;
					if (!target) {
						Vector<LevelBrush *> brushes = current_map->get_brushes();
						if (!brushes.is_empty()) {
							target = brushes[brushes.size() - 1];
						}
					}
					if (target) {
						// Place the point on the edit plane at the brush's depth.
						Vector3 center = target->get_global_transform().xform(target->get_center());
						if (vp->ray_to_view_plane(mb->get_position(), center, hit)) {
							_clip_begin(target, hit, vp);
						}
					}
				}
			} else {
				if (clip_drawing && clip_viewport == vp) {
					clip_drawing = false;
					clip_drag_point = -1;
				} else if (clip_drag_point >= 0 && clip_viewport == vp) {
					clip_drag_point = -1;
				}
			}
			return;
		}
		if (mm.is_valid()) {
			if ((clip_drawing || clip_drag_point >= 0) && clip_viewport == vp) {
				// Move the active point on the edit plane THROUGH THE FIRST clip
				// point, so both points stay coplanar (same Y in top view, etc).
				Vector3 hit;
				if (vp->ray_to_view_plane(mm->get_position(), clip_points[0], hit)) {
					if (clip_drawing) {
						_clip_update_second(hit);
					} else if (clip_drag_point >= 0) {
						clip_points[clip_drag_point] = _snap(hit);
						_update_overlays();
					}
				}
			}
			return;
		}
		return;
	}

	// Select + element modes (skip while the gizmo is active).
	if (gizmo_dragging) {
		return;
	}

	// Whole-brush drag in Select mode.
	if (select_moving) {
		if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT && !mb->is_pressed()) {
			// Release: commit undo.
			Vector3 new_pos = selected_brush ? selected_brush->get_position() : select_move_original_position;
			if (selected_brush && !new_pos.is_equal_approx(select_move_original_position)) {
				LevelBrush *target = selected_brush;
				Vector3 old_pos = select_move_original_position;
				EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
				undo_redo->create_action(TTR("Move Brush"));
				undo_redo->add_do_property(target, "position", new_pos);
				undo_redo->add_undo_property(target, "position", old_pos);
				undo_redo->commit_action(false);
			}
			select_moving = false;
			select_move_viewport = nullptr;
			return;
		}
		if (mm.is_valid() && select_move_viewport == vp && selected_brush) {
			Vector3 grab;
			if (_select_ray_to_edit_plane(vp, mm->get_position(), grab)) {
				Vector3 new_world = _snap(grab - select_move_offset);
				Node3D *parent = Object::cast_to<Node3D>(selected_brush->get_parent());
				if (parent) {
					selected_brush->set_position(parent->get_global_transform().affine_inverse().xform(new_world));
				} else {
					selected_brush->set_position(new_world);
				}
				_refresh_map();
				_update_overlays();
			}
			return;
		}
	}
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT && mb->is_pressed()) {
		bool add = mb->is_shift_pressed();
		switch (mode) {
			case MODE_SELECT: {
				// Click a brush to select it; re-clicking the already-selected
				// brush starts a whole-brush drag (like the ghost move).
				Vector3 hit;
				LevelBrush *brush = nullptr;
				int f;
				if (_pick_face(p_camera, mb->get_position(), brush, f, hit)) {
					if (brush == selected_brush) {
						// Begin drag on the edit plane at the grab depth.
						select_moving = true;
						select_move_viewport = vp;
						select_move_original_position = selected_brush->get_position();
						Vector3 grab;
						if (_select_ray_to_edit_plane(vp, mb->get_position(), grab)) {
							select_move_offset = grab - selected_brush->get_global_position();
						} else {
							select_move_offset = Vector3();
						}
					} else {
						selected_brush = brush;
						_edit_brush_node(brush);
					}
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
						_edit_brush_node(brush);
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
						_edit_brush_node(brush);
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
				int v;
				if (_pick_vertex(p_camera, mb->get_position(), brush, v)) {
					if (brush != selected_brush) {
						selected_brush = brush;
						selected_vertices.clear();
						_edit_brush_node(brush);
					}
					if (selected_vertices.has(v) && add) {
						selected_vertices.erase(v);
					} else {
						if (!add) {
							selected_vertices.clear();
						}
						selected_vertices.insert(v);
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

bool LevelEditorScreen::_select_ray_to_edit_plane(LevelEditorViewport *p_vp, const Vector2 &p_screen, Vector3 &r_hit) const {
	Vector3 pos = selected_brush ? selected_brush->get_global_position() : Vector3();
	return p_vp->ray_to_view_plane(p_screen, pos, r_hit);
}

// ---- Ghost block (stage 2) -------------------------------------------------

// Shared box-handle picking: corners (high priority) then face centers.
// Positions are computed in world space via p_xform.
int LevelEditorScreen::_pick_box_handle(LevelEditorViewport *p_vp, const Vector2 &p_screen, const AABB &p_aabb, const Transform3D &p_xform) const {
	const real_t face_tol = 10.0 * EDSCALE;
	const real_t corner_tol = 8.0 * EDSCALE;

	Vector3 corners[8];
	aabb_corners(p_aabb, corners);
	for (int i = 0; i < 8; i++) {
		Vector2 sp;
		if (p_vp->project(p_xform.xform(corners[i]), sp) && sp.distance_to(p_screen) < corner_tol) {
			return GHOST_CORNER_0 + i;
		}
	}

	int best = GHOST_NONE;
	real_t best_d = face_tol;
	for (int i = 0; i < 6; i++) {
		Vector2 sp;
		if (p_vp->project(p_xform.xform(aabb_face_center(p_aabb, i)), sp)) {
			real_t d = sp.distance_to(p_screen);
			if (d < best_d) {
				best_d = d;
				best = GHOST_FACE_XN + i;
			}
		}
	}
	return best;
}

int LevelEditorScreen::_pick_ghost_handle(LevelEditorViewport *p_vp, const Vector2 &p_screen) const {
	return _pick_box_handle(p_vp, p_screen, ghost_aabb, Transform3D());
}

bool LevelEditorScreen::_ghost_hit_test(LevelEditorViewport *p_vp, const Vector2 &p_screen) const {
	// Screen-space point-in-polygon test against the ghost's projected faces.
	Vector3 corners[8];
	aabb_corners(ghost_aabb, corners);
	static const int face_idx[6][4] = {
		{ 4, 5, 7, 6 },
		{ 1, 0, 2, 3 }, // +Z, -Z
		{ 5, 1, 3, 7 },
		{ 0, 4, 6, 2 }, // +X, -X
		{ 7, 6, 2, 3 },
		{ 0, 1, 5, 4 }, // +Y, -Y
	};
	for (auto &f : face_idx) {
		Vector2 quad[4];
		bool ok = true;
		for (int i = 0; i < 4; i++) {
			if (!p_vp->project(corners[f[i]], quad[i])) {
				ok = false;
				break;
			}
		}
		if (!ok) {
			continue;
		}
		// Ray-casting point-in-quad test.
		int crossings = 0;
		for (int i = 0; i < 4; i++) {
			Vector2 a = quad[i];
			Vector2 b = quad[(i + 1) % 4];
			if ((a.y > p_screen.y) != (b.y > p_screen.y)) {
				real_t x = a.x + (p_screen.y - a.y) * (b.x - a.x) / (b.y - a.y);
				if (p_screen.x < x) {
					crossings++;
				}
			}
		}
		if (crossings % 2 == 1) {
			return true;
		}
	}
	return false;
}

bool LevelEditorScreen::_ghost_ray_to_edit_plane(LevelEditorViewport *p_vp, const Vector2 &p_screen, Vector3 &r_hit) const {
	return p_vp->ray_to_view_plane(p_screen, ghost_aabb.get_center(), r_hit);
}

void LevelEditorScreen::_ghost_handle_drag_to(LevelEditorViewport *p_vp, const Vector2 &p_mouse) {
	// Drag the handle along its axis (face) or freely (corner), snapped.
	Vector3 c = ghost_aabb.get_center();
	Vector3 mins = ghost_aabb.position;
	Vector3 maxs = ghost_aabb.position + ghost_aabb.size;

	int h = ghost_handle_drag;
	if (h >= GHOST_CORNER_0) {
		int ci = h - GHOST_CORNER_0;
		Vector3 corner = c + Vector3((ci & 1) ? ghost_aabb.size.x * 0.5 : -ghost_aabb.size.x * 0.5, (ci & 2) ? ghost_aabb.size.y * 0.5 : -ghost_aabb.size.y * 0.5, (ci & 4) ? ghost_aabb.size.z * 0.5 : -ghost_aabb.size.z * 0.5);

		Vector3 hit;
		if (!p_vp->ray_to_view_plane(p_mouse, corner, hit)) {
			return;
		}
		hit = _snap(hit);
		if (ci & 1) {
			maxs.x = MAX(hit.x, mins.x + CMP_EPSILON);
		} else {
			mins.x = MIN(hit.x, maxs.x - CMP_EPSILON);
		}
		if (ci & 2) {
			maxs.y = MAX(hit.y, mins.y + CMP_EPSILON);
		} else {
			mins.y = MIN(hit.y, maxs.y - CMP_EPSILON);
		}
		if (ci & 4) {
			maxs.z = MAX(hit.z, mins.z + CMP_EPSILON);
		} else {
			mins.z = MIN(hit.z, maxs.z - CMP_EPSILON);
		}
	} else {
		// Face handle: slide that face along its own axis. Intersect the
		// mouse ray with the viewport's edit plane (ortho rays are parallel,
		// so a face-aligned plane would never be hit), then take only the
		// handle axis component.
		int axis = (h - GHOST_FACE_XN) / 2; // 0=x, 1=y, 2=z
		bool is_max = ((h - GHOST_FACE_XN) % 2) == 1;

		Vector3 hit;
		if (!p_vp->ray_to_view_plane(p_mouse, c, hit)) {
			return;
		}
		real_t v = _snap(hit[axis]);
		if (is_max) {
			maxs[axis] = MAX(v, mins[axis] + CMP_EPSILON);
		} else {
			mins[axis] = MIN(v, maxs[axis] - CMP_EPSILON);
		}
	}

	ghost_aabb = AABB(mins, maxs - mins);
	_update_overlays();
}

void LevelEditorScreen::_ghost_commit() {
	ghost_active = false;
	ghost_handle_hover = GHOST_NONE;
	ghost_handle_drag = GHOST_NONE;
	ghost_moving = false;

	LevelMap *map = _get_or_create_map();
	ERR_FAIL_NULL(map);

	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	ERR_FAIL_NULL(root);

	Transform3D map_inv = map->get_global_transform().affine_inverse();

	LevelBrush *brush = memnew(LevelBrush);
	brush->set_name("Brush");
	brush->setup_box(map_inv.xform(ghost_aabb));
	if (current_material.is_valid()) {
		for (int f = 0; f < brush->get_face_count(); f++) {
			brush->set_face_material(f, current_material);
		}
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Add Level Brush"));
	undo_redo->add_do_method(map, "add_child", brush);
	undo_redo->add_do_method(brush, "set_owner", root);
	undo_redo->add_do_method(map, "refresh");
	undo_redo->add_undo_method(map, "remove_child", brush);
	undo_redo->add_undo_method(map, "refresh");
	undo_redo->commit_action();

	selected_brush = brush;
	_edit_brush_node(brush);
	_refresh_map();
}

void LevelEditorScreen::_ghost_cancel() {
	ghost_active = false;
	ghost_handle_hover = GHOST_NONE;
	ghost_handle_drag = GHOST_NONE;
	ghost_moving = false;
	_update_overlays();
}

void LevelEditorScreen::_draw_ghost(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (!ghost_active) {
		return;
	}

	Vector3 corners[8];
	aabb_corners(ghost_aabb, corners);

	// Box edges.
	Color col(0.2, 0.9, 0.4, 0.9);
	for (auto &e : AABB_EDGE_IDX) {
		Vector2 a, b;
		if (p_vp->project(corners[e[0]], a) && p_vp->project(corners[e[1]], b)) {
			p_canvas->draw_line(a, b, col, 2.0);
		}
	}

	// Face handles: squares at face centers.
	for (int i = 0; i < 6; i++) {
		Vector3 fc = aabb_face_center(ghost_aabb, i);
		Vector2 sp;
		if (p_vp->project(fc, sp)) {
			bool hot = (ghost_handle_hover == GHOST_FACE_XN + i || ghost_handle_drag == GHOST_FACE_XN + i);
			Color hc = hot ? Color(0.6, 1.0, 0.75, 0.95) : Color(0.2, 0.9, 0.4, 0.7);
			real_t hs_px = 4.0 * EDSCALE;
			p_canvas->draw_rect(Rect2(sp - Vector2(hs_px, hs_px), Size2(hs_px * 2, hs_px * 2)), hc);
		}
	}

	// Corner handles.
	for (int i = 0; i < 8; i++) {
		Vector2 sp;
		if (p_vp->project(corners[i], sp)) {
			bool hot = (ghost_handle_hover == GHOST_CORNER_0 + i || ghost_handle_drag == GHOST_CORNER_0 + i);
			Color hc = hot ? Color(0.6, 1.0, 0.75, 0.95) : Color(0.2, 0.9, 0.4, 0.7);
			real_t hs_px = 3.0 * EDSCALE;
			p_canvas->draw_rect(Rect2(sp - Vector2(hs_px, hs_px), Size2(hs_px * 2, hs_px * 2)), hc);
		}
	}

	_draw_dim_labels(p_vp, p_canvas, ghost_aabb);
}

void LevelEditorScreen::_draw_dim_labels(LevelEditorViewport *p_vp, Control *p_canvas, const AABB &p_aabb) {
	Vector3 corners[8];
	aabb_corners(p_aabb, corners);

	Ref<Font> font = get_theme_font(SNAME("font"), SNAME("Label"));
	const int font_size = get_theme_font_size(SNAME("font_size"), SNAME("Label"));
	Color text_col(1, 1, 1, 0.9);

	struct DimLabel {
		int edge_a, edge_b;
		int axis;
	};
	static const DimLabel dim_labels[3] = {
		{ 2, 3, 0 }, // X edge along the top -> width
		{ 6, 7, 2 }, // Z edge along the top -> depth
		{ 0, 2, 1 }, // Y edge -> height
	};
	for (const DimLabel &dl : dim_labels) {
		// Ortho views show only the two axes visible in that view; the
		// perspective view shows all three.
		switch (p_vp->get_view_type()) {
			case LevelEditorViewport::VIEW_TOP: // X and Z
				if (dl.axis == 1) {
					continue;
				}
				break;
			case LevelEditorViewport::VIEW_FRONT: // X and Y
				if (dl.axis == 2) {
					continue;
				}
				break;
			case LevelEditorViewport::VIEW_SIDE: // Z and Y
				if (dl.axis == 0) {
					continue;
				}
				break;
			default:
				break;
		}
		Vector3 mid = (corners[dl.edge_a] + corners[dl.edge_b]) * 0.5;
		Vector2 sp;
		if (!p_vp->project(mid, sp)) {
			continue;
		}
		String text = String::num(p_aabb.size[dl.axis], 2);
		Vector2 text_size = font->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
		p_canvas->draw_string(font, sp - text_size * 0.5 + Vector2(0, text_size.y * 0.35), text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, text_col);
	}
}

// ---- Clip tool ---------------------------------------------------------------

void LevelEditorScreen::_clip_begin(LevelBrush *p_brush, const Vector3 &p_point, LevelEditorViewport *p_vp) {
	_clip_cancel();
	clip_brush = p_brush;
	clip_active = true;
	clip_drawing = true;
	clip_drag_point = 1;
	clip_viewport = p_vp;
	clip_side = CLIP_KEEP_FRONT;
	clip_points[0] = _snap(p_point);
	clip_points[1] = clip_points[0];
	clip_view_dir = -p_vp->get_camera()->get_global_transform().basis[2];
	_update_overlays();
}

void LevelEditorScreen::_clip_update_second(const Vector3 &p_point) {
	clip_points[1] = _snap(p_point);
	_update_overlays();
}

int LevelEditorScreen::_pick_clip_point(LevelEditorViewport *p_vp, const Vector2 &p_screen) const {
	for (int i = 0; i < 2; i++) {
		Vector2 sp;
		if (p_vp->project(clip_points[i], sp) && sp.distance_to(p_screen) < 10.0) {
			return i;
		}
	}
	return -1;
}

Plane LevelEditorScreen::_clip_plane() const {
	// Plane through both clip points, containing the view direction. The
	// normal points to the LEFT of the line as drawn on screen (verified:
	// along x view_dir), so "keep left/right" matches Hammer's clip tool.
	Vector3 along = clip_points[1] - clip_points[0];
	if (along.length() < CMP_EPSILON) {
		return Plane();
	}
	Vector3 n = along.cross(clip_view_dir);
	if (n.length_squared() < CMP_EPSILON) {
		// Line parallel to view dir - degenerate.
		return Plane();
	}
	n.normalize();

	// World plane -> brush-local plane.
	Plane world_plane(n, n.dot(clip_points[0]));
	Transform3D gt = clip_brush->get_global_transform();
	Transform3D inv = gt.affine_inverse();
	Vector3 local_n = inv.basis.xform(world_plane.normal).normalized();
	Vector3 local_point = inv.xform(clip_points[0]);
	return Plane(local_n, local_n.dot(local_point));
}

void LevelEditorScreen::_clip_apply() {
	if (!clip_active || !clip_brush) {
		return;
	}
	Plane plane = _clip_plane();
	if (plane.normal.is_zero_approx()) {
		_clip_cancel();
		return;
	}
	if (clip_side == CLIP_KEEP_BACK) {
		plane = -plane;
	}

	if (clip_side == CLIP_KEEP_BOTH) {
		// Keep-both: subdivide faces along the line in-place - no caps, no
		// seam, no new brush node. The brush stays one solid.
		LevelBrush *target = clip_brush;
		PackedVector3Array old_verts = target->get_vertices_data();
		Array old_faces = target->get_faces_data();

		target->split_faces(plane);
		_commit_brush_undo(TTR("Split Brush Faces"), target, old_verts, old_faces, Array());
	} else {
		LevelBrush *target = clip_brush;
		PackedVector3Array old_verts = target->get_vertices_data();
		Array old_faces = target->get_faces_data();

		target->clip(plane);
		_commit_brush_undo(TTR("Clip Brush"), target, old_verts, old_faces, Array());
	}

	clip_active = false;
	clip_brush = nullptr;
	_refresh_map();
}

void LevelEditorScreen::_clip_cancel() {
	clip_active = false;
	clip_drawing = false;
	clip_drag_point = -1;
	clip_brush = nullptr;
	clip_viewport = nullptr;
	_update_overlays();
}

void LevelEditorScreen::_clip_cycle_side() {
	clip_side = (ClipSide)(((int)clip_side + 1) % 3);
	_update_overlays();
}

void LevelEditorScreen::_draw_clip(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (mode != MODE_CLIP || !clip_active || !clip_brush) {
		return;
	}

	// Clip points + line.
	Color pt_col(0.2, 0.9, 1.0, 1.0);
	Color line_col(0.2, 0.9, 1.0, 0.9);
	Vector2 s0, s1;
	bool has0 = p_vp->project(clip_points[0], s0);
	bool has1 = p_vp->project(clip_points[1], s1);
	if (has0 && has1) {
		p_canvas->draw_line(s0, s1, line_col, 2.0);
	}
	for (int i = 0; i < 2; i++) {
		Vector2 sp;
		if (p_vp->project(clip_points[i], sp)) {
			bool hot = (clip_drag_point == i);
			real_t hs_px = 4.0 * EDSCALE;
			p_canvas->draw_rect(Rect2(sp - Vector2(hs_px, hs_px), Size2(hs_px * 2, hs_px * 2)), hot ? Color(0.6, 1.0, 1.0, 1.0) : pt_col);
		}
	}

	// Preview the cut by coloring each brush EDGE: the segment on the kept
	// side of the plane is green, the discarded side red - so the cut reads
	// exactly where the line passes through the brush.
	Plane plane = _clip_plane();
	if (plane.normal.is_zero_approx()) {
		return;
	}
	Transform3D gt = clip_brush->get_global_transform();

	Color kept_col(0.2, 0.9, 0.4, 0.95);
	Color cut_col(0.95, 0.25, 0.2, 0.95);

	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = clip_brush->get_edges();
	for (const LevelBrush::EdgeKey &e : edges) {
		Vector3 a_local = clip_brush->get_vertex(e.a);
		Vector3 b_local = clip_brush->get_vertex(e.b);
		real_t da = plane.distance_to(a_local);
		real_t db = plane.distance_to(b_local);

		// Split the edge at the plane if it crosses.
		Vector3 mid_local;
		bool crosses = (da > 0.0) != (db > 0.0);
		if (crosses) {
			real_t t = da / (da - db);
			mid_local = a_local + (b_local - a_local) * t;
		}

		// For keep-both, show the two halves in green/blue instead of green/red.
		Color col_a, col_b;
		switch (clip_side) {
			case CLIP_KEEP_FRONT:
				col_a = (da >= 0.0) ? kept_col : cut_col;
				col_b = (db >= 0.0) ? kept_col : cut_col;
				break;
			case CLIP_KEEP_BACK:
				col_a = (da < 0.0) ? kept_col : cut_col;
				col_b = (db < 0.0) ? kept_col : cut_col;
				break;
			case CLIP_KEEP_BOTH:
			default:
				col_a = (da >= 0.0) ? kept_col : Color(0.4, 0.6, 1.0, 0.95);
				col_b = (db >= 0.0) ? kept_col : Color(0.4, 0.6, 1.0, 0.95);
				break;
		}

		auto draw_seg = [&](const Vector3 &p1, const Vector3 &p2, const Color &c) {
			Vector2 s1, s2;
			if (p_vp->project(gt.xform(p1), s1) && p_vp->project(gt.xform(p2), s2)) {
				p_canvas->draw_line(s1, s2, c, 2.5);
			}
		};

		if (crosses) {
			draw_seg(a_local, mid_local, col_a);
			draw_seg(mid_local, b_local, col_b);
			// Mark the cut point.
			Vector2 sm;
			if (p_vp->project(gt.xform(mid_local), sm)) {
				p_canvas->draw_rect(Rect2(sm - Vector2(3, 3), Size2(6, 6)), Color(1, 1, 1, 0.9));
			}
		} else {
			draw_seg(a_local, b_local, col_a);
		}
	}

	// Mode hint text in the corner.
	String side_text;
	switch (clip_side) {
		case CLIP_KEEP_FRONT:
			side_text = TTR("Clip: keep LEFT of line (click Clip to cycle, Enter to apply, Esc to cancel)");
			break;
		case CLIP_KEEP_BACK:
			side_text = TTR("Clip: keep RIGHT of line (click Clip to cycle, Enter to apply, Esc to cancel)");
			break;
		case CLIP_KEEP_BOTH:
			side_text = TTR("Clip: split faces along line (click Clip to cycle, Enter to apply, Esc to cancel)");
			break;
	}
	p_canvas->draw_string(get_theme_font(SNAME("font"), SNAME("Label")), Vector2(8, 18), side_text, HORIZONTAL_ALIGNMENT_LEFT, -1, 13, Color(1, 1, 1, 0.8));
}

// ---- Select-mode box handles ------------------------------------------------

AABB LevelEditorScreen::_get_brush_local_aabb(LevelBrush *p_brush) const {
	AABB bb;
	for (int i = 0; i < p_brush->get_vertex_count(); i++) {
		if (i == 0) {
			bb.position = p_brush->get_vertex(0);
		} else {
			bb.expand_to(p_brush->get_vertex(i));
		}
	}
	return bb;
}

void LevelEditorScreen::_apply_brush_aabb(LevelBrush *p_brush, const AABB &p_aabb) {
	// Scale current vertices from the current AABB into the target AABB.
	AABB cur = _get_brush_local_aabb(p_brush);
	for (int i = 0; i < p_brush->get_vertex_count(); i++) {
		Vector3 v = p_brush->get_vertex(i);
		for (int axis = 0; axis < 3; axis++) {
			real_t cur_size = cur.size[axis];
			real_t t = (cur_size > CMP_EPSILON) ? (v[axis] - cur.position[axis]) / cur_size : 0.0;
			v[axis] = p_aabb.position[axis] + t * p_aabb.size[axis];
		}
		p_brush->set_vertex(i, v);
	}
}

int LevelEditorScreen::_pick_select_handle(LevelEditorViewport *p_vp, const Vector2 &p_screen) const {
	if (!selected_brush) {
		return GHOST_NONE;
	}
	return _pick_box_handle(p_vp, p_screen, _get_brush_local_aabb(selected_brush), selected_brush->get_global_transform());
}

void LevelEditorScreen::_select_handle_drag_to(LevelEditorViewport *p_vp, const Vector2 &p_mouse) {
	if (!selected_brush) {
		return;
	}

	// Restore original geometry, then apply the resize from scratch (absolute).
	_apply_brush_aabb(selected_brush, select_drag_original_aabb);

	Transform3D gt = selected_brush->get_global_transform();
	Transform3D inv = gt.affine_inverse();

	AABB bb = select_drag_original_aabb;
	Vector3 mins = bb.position;
	Vector3 maxs = bb.position + bb.size;

	int h = select_handle_drag;

	// Intersect with the viewport edit plane in world space, then convert to
	// brush-local.
	Vector3 hit;
	Vector3 wc = gt.xform(bb.get_center());
	if (!p_vp->ray_to_view_plane(p_mouse, wc, hit)) {
		return;
	}

	Vector3 local_hit = _snap(inv.xform(hit));

	if (h >= GHOST_CORNER_0) {
		int ci = h - GHOST_CORNER_0;
		if (ci & 1) {
			maxs.x = MAX(local_hit.x, mins.x + grid_size);
		} else {
			mins.x = MIN(local_hit.x, maxs.x - grid_size);
		}
		if (ci & 2) {
			maxs.y = MAX(local_hit.y, mins.y + grid_size);
		} else {
			mins.y = MIN(local_hit.y, maxs.y - grid_size);
		}
		if (ci & 4) {
			maxs.z = MAX(local_hit.z, mins.z + grid_size);
		} else {
			mins.z = MIN(local_hit.z, maxs.z - grid_size);
		}
	} else {
		int axis = (h - GHOST_FACE_XN) / 2;
		bool is_max = ((h - GHOST_FACE_XN) % 2) == 1;
		if (is_max) {
			maxs[axis] = MAX(local_hit[axis], mins[axis] + grid_size);
		} else {
			mins[axis] = MIN(local_hit[axis], maxs[axis] - grid_size);
		}
	}

	_apply_brush_aabb(selected_brush, AABB(mins, maxs - mins));
	_refresh_map();
	_update_overlays();
}

void LevelEditorScreen::_select_handle_end_drag() {
	if (select_handle_drag == GHOST_NONE || !selected_brush) {
		select_handle_drag = GHOST_NONE;
		return;
	}
	select_handle_drag = GHOST_NONE;

	// Commit as undo: original AABB vs current geometry.
	LevelBrush *target = selected_brush;

	// Snapshot "before" by applying the original AABB to a temp copy.
	LevelBrush *before = target->duplicate_brush();
	_apply_brush_aabb(before, select_drag_original_aabb);
	PackedVector3Array old_verts = before->get_vertices_data();
	memdelete(before);

	PackedVector3Array new_verts = target->get_vertices_data();
	if (new_verts == old_verts) {
		return;
	}

	Array cur_faces = target->get_faces_data();
	Array cur_mats = target->get_face_materials_data();
	_commit_brush_undo(TTR("Resize Brush"), target, old_verts, cur_faces, cur_mats);
}

void LevelEditorScreen::_draw_select_handles(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (mode != MODE_SELECT || !selected_brush) {
		return;
	}

	AABB bb = _get_brush_local_aabb(selected_brush);

	Transform3D gt = selected_brush->get_global_transform();
	Color box_col(1.0, 0.6, 0.1, 0.9);

	// Bounding box edges.
	Vector3 corners[8];
	aabb_corners(bb, corners);
	for (auto &e : AABB_EDGE_IDX) {
		Vector2 a, b;
		if (p_vp->project(gt.xform(corners[e[0]]), a) && p_vp->project(gt.xform(corners[e[1]]), b)) {
			p_canvas->draw_line(a, b, box_col, 1.5);
		}
	}

	// Face handles.
	for (int i = 0; i < 6; i++) {
		Vector3 fc = gt.xform(aabb_face_center(bb, i));
		Vector2 sp;
		if (p_vp->project(fc, sp)) {
			bool hot = (select_handle_hover == GHOST_FACE_XN + i || select_handle_drag == GHOST_FACE_XN + i);
			Color hc = hot ? Color(1.0, 0.8, 0.5, 0.95) : Color(1.0, 0.6, 0.1, 0.8);
			real_t hs_px = 4.0 * EDSCALE;
			p_canvas->draw_rect(Rect2(sp - Vector2(hs_px, hs_px), Size2(hs_px * 2, hs_px * 2)), hc);
		}
	}

	// Corner handles.
	for (int i = 0; i < 8; i++) {
		Vector2 sp;
		if (p_vp->project(gt.xform(corners[i]), sp)) {
			bool hot = (select_handle_hover == GHOST_CORNER_0 + i || select_handle_drag == GHOST_CORNER_0 + i);
			Color hc = hot ? Color(1.0, 0.8, 0.5, 0.95) : Color(1.0, 0.6, 0.1, 0.8);
			real_t hs_px = 3.0 * EDSCALE;
			p_canvas->draw_rect(Rect2(sp - Vector2(hs_px, hs_px), Size2(hs_px * 2, hs_px * 2)), hc);
		}
	}
}

void LevelEditorScreen::_compute_drag_aabb(Vector3 &r_mins, Vector3 &r_maxs) const {
	r_mins = drag_start.min(drag_current);
	r_maxs = drag_start.max(drag_current);

	LevelEditorViewport::ViewType vt = drag_viewport->get_view_type();
	real_t thickness = grid_size;

	// Reuse the last brush's Y height if there is one, so walls of uniform
	// height are quick to lay out (edits to the previous block carry over).
	const LevelBrush *ref_brush = selected_brush;
	Vector<LevelBrush *> map_brushes;
	if (!ref_brush && current_map) {
		map_brushes = current_map->get_brushes();
		if (!map_brushes.is_empty()) {
			ref_brush = map_brushes[map_brushes.size() - 1];
		}
	}
	real_t ref_height = -1.0;
	if (ref_brush) {
		real_t min_y = (real_t)Math::INF, max_y = -(real_t)Math::INF;
		for (int i = 0; i < ref_brush->get_vertex_count(); i++) {
			Vector3 w = ref_brush->get_global_transform().xform(ref_brush->get_vertex(i));
			min_y = MIN(min_y, w.y);
			max_y = MAX(max_y, w.y);
		}
		if (max_y - min_y > CMP_EPSILON) {
			ref_height = max_y - min_y;
		}
	}

	switch (vt) {
		case LevelEditorViewport::VIEW_TOP:
		case LevelEditorViewport::VIEW_PERSPECTIVE:
			// Drag plane is the ground; block rises in Y.
			if (ref_height > 0.0) {
				thickness = ref_height;
			}
			r_mins.y = _snap(drag_start.y);
			r_maxs.y = r_mins.y + thickness;
			break;
		case LevelEditorViewport::VIEW_FRONT:
			r_mins.z = _snap(drag_start.z);
			r_maxs.z = r_mins.z + thickness;
			break;
		case LevelEditorViewport::VIEW_SIDE:
			r_mins.x = _snap(drag_start.x);
			r_maxs.x = r_mins.x + thickness;
			break;
	}

	for (int i = 0; i < 3; i++) {
		if (r_maxs[i] - r_mins[i] < CMP_EPSILON) {
			r_maxs[i] = r_mins[i] + thickness;
		}
	}
}

void LevelEditorScreen::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			_update_mode_icons();
			_update_warning_color();
		} break;
		case NOTIFICATION_PROCESS: {
			// Drop dangling selection if the brush was deleted externally.
			if (selected_brush && !selected_brush->is_inside_tree()) {
				_clear_selection();
				_update_overlays();
			}
			if (current_map && !current_map->is_inside_tree()) {
				current_map = nullptr;
				_clear_selection();
				_update_map_ui();
			}
		} break;
	}
}

// ---- Manipulation gizmo ---------------------------------------------------

bool LevelEditorScreen::_has_selection() const {
	if (!selected_brush) {
		return false;
	}
	switch (mode) {
		case MODE_SELECT:
		case MODE_ROTATE:
		case MODE_SCALE:
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

	if (mode == MODE_SELECT || mode == MODE_ROTATE || mode == MODE_SCALE) {
		// Gizmo at the brush's geometry center, in world space.
		return gt.xform(selected_brush->get_center());
	}

	Vector3 sum;
	int count = 0;

	switch (mode) {
		case MODE_FACE: {
			for (int f : selected_faces) {
				LocalVector<int> loop = selected_brush->get_face(f);
				for (int idx : loop) {
					sum += gt.xform(selected_brush->get_vertex(idx));
					count++;
				}
			}
		} break;
		case MODE_EDGE: {
			for (const LevelBrush::EdgeKey &e : selected_edges) {
				sum += gt.xform(selected_brush->get_vertex(e.a));
				sum += gt.xform(selected_brush->get_vertex(e.b));
				count += 2;
			}
		} break;
		case MODE_VERTEX: {
			for (int v : selected_vertices) {
				sum += gt.xform(selected_brush->get_vertex(v));
				count++;
			}
		} break;
		default:
			break;
	}
	return count > 0 ? sum / count : gt.get_origin();
}

Vector<int> LevelEditorScreen::_get_gizmo_vertex_indices() const {
	Vector<int> out;
	if (!selected_brush) {
		return out;
	}
	HashSet<int> seen;
	auto add = [&](int idx) {
		if (!seen.has(idx)) {
			seen.insert(idx);
			out.push_back(idx);
		}
	};
	switch (mode) {
		case MODE_FACE:
			for (int f : selected_faces) {
				LocalVector<int> loop = selected_brush->get_face(f);
				for (int idx : loop) {
					add(idx);
				}
			}
			break;
		case MODE_EDGE:
			for (const LevelBrush::EdgeKey &e : selected_edges) {
				add(e.a);
				add(e.b);
			}
			break;
		case MODE_VERTEX:
			for (int v : selected_vertices) {
				add(v);
			}
			break;
		default:
			break;
	}
	return out;
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

	// Axis length in pixels (fixed screen size, scaled by editor scale).
	const real_t axis_len = 64.0 * EDSCALE;
	const real_t axis_tol = 9.0 * EDSCALE;
	const real_t plane_tol = 12.0 * EDSCALE;
	const real_t center_tol = 7.0 * EDSCALE;

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

	// Snapshot brush vertices for absolute drags + undo.
	gizmo_drag_original_verts = selected_brush->get_vertices_data();
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

	// Scale mode: axis drag distance -> scale factor along that axis.
	if (mode == MODE_SCALE) {
		if (gizmo_drag_uniform_scale) {
			// Click-anywhere uniform drag (started off-gizmo): use mouse X.
			real_t factor = 1.0 + (p_mouse.x - gizmo_drag_mouse_start.x) * 0.01;
			_apply_gizmo_scale_uniform(factor);
		} else {
			_apply_gizmo_scale(delta);
		}
		_update_overlays();
		return;
	}

	_apply_gizmo_delta(delta);
	_update_overlays();
}

// ---- Rotate gizmo ------------------------------------------------------------

// Radius in pixels of the rotate rings on screen (before EDSCALE).
static const real_t ROTATE_RING_PX = 64.0 * EDSCALE;

int LevelEditorScreen::_pick_rotate_ring(LevelEditorViewport *p_vp, const Vector2 &p_screen) const {
	if (!selected_brush) {
		return -1;
	}
	Vector3 origin = _get_gizmo_origin();
	Vector2 center;
	if (!p_vp->project(origin, center)) {
		return -1;
	}

	// The ring plane normal axes; ring radius matches the drawn circle.
	const real_t tol = 8.0 * EDSCALE;
	int best_axis = -1;
	real_t best_dist = tol;

	// In ortho views, only the ring perpendicular to the view plane is usable.
	int allowed_axis = -1;
	switch (p_vp->get_view_type()) {
		case LevelEditorViewport::VIEW_TOP:
			allowed_axis = 1;
			break;
		case LevelEditorViewport::VIEW_FRONT:
			allowed_axis = 2;
			break;
		case LevelEditorViewport::VIEW_SIDE:
			allowed_axis = 0;
			break;
		default:
			break;
	}

	for (int axis = 0; axis < 3; axis++) {
		if (allowed_axis >= 0 && axis != allowed_axis) {
			continue; // Ring disabled in this ortho view.
		}
		// Sample the ring in 3D and project; measure min distance to the mouse.
		const int SEGMENTS = 48;
		real_t min_d = (real_t)Math::INF;
		bool any_front = false;
		for (int s = 0; s < SEGMENTS; s++) {
			real_t a = (real_t)s / SEGMENTS * Math::TAU;
			Vector3 p;
			// Ring in the plane perpendicular to the axis.
			int u = (axis + 1) % 3, v = (axis + 2) % 3;
			p[u] = Math::cos(a);
			p[v] = Math::sin(a);
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
	if (mode != MODE_ROTATE || !selected_brush) {
		return;
	}
	Vector3 origin = _get_gizmo_origin();
	Vector2 center;
	if (!p_vp->project(origin, center)) {
		return;
	}

	Camera3D *cam = p_vp->get_camera();
	Color axis_col[3] = { Color(0.95, 0.3, 0.3), Color(0.4, 0.9, 0.4), Color(0.3, 0.6, 1.0) };

	// World-space radius so the ring projects to ~ROTATE_RING_PX pixels.
	// Estimate via one test point like the move gizmo does.
	real_t world_radius = 1.0;
	{
		Vector3 test = origin + Vector3(1, 0, 0);
		if (!cam->is_position_behind(test)) {
			real_t px = cam->unproject_position(test).distance_to(center);
			if (px > 0.001) {
				world_radius = ROTATE_RING_PX / px;
			}
		}
	}

	// In ortho views, only the ring perpendicular to the view plane is usable.
	int allowed_axis = -1;
	switch (p_vp->get_view_type()) {
		case LevelEditorViewport::VIEW_TOP:
			allowed_axis = 1;
			break;
		case LevelEditorViewport::VIEW_FRONT:
			allowed_axis = 2;
			break;
		case LevelEditorViewport::VIEW_SIDE:
			allowed_axis = 0;
			break;
		default:
			break;
	}

	const int SEGMENTS = 48;
	for (int axis = 0; axis < 3; axis++) {
		if (allowed_axis >= 0 && axis != allowed_axis) {
			continue; // Ring disabled in this ortho view.
		}
		bool hot = (rotate_hover_axis == axis || rotate_drag_axis == axis);
		Color col = hot ? axis_col[axis].lerp(Color(1, 1, 1), 0.5) : axis_col[axis];
		real_t width = (hot ? 3.0 : 2.0) * EDSCALE;

		int u = (axis + 1) % 3, v = (axis + 2) % 3;
		Vector2 prev;
		bool has_prev = false;
		for (int s = 0; s <= SEGMENTS; s++) {
			real_t a = (real_t)s / SEGMENTS * Math::TAU;
			Vector3 p;
			p[u] = Math::cos(a) * world_radius;
			p[v] = Math::sin(a) * world_radius;
			Vector2 sp;
			if (p_vp->project(origin + p, sp)) {
				if (has_prev) {
					p_canvas->draw_line(prev, sp, col, width);
				}
				prev = sp;
				has_prev = true;
			} else {
				has_prev = false;
			}
		}
	}

	// Center dot.
	p_canvas->draw_circle(center, 3.0 * EDSCALE, Color(1, 1, 1, 0.9));
}

void LevelEditorScreen::_rotate_end_drag() {
	if (rotate_drag_axis < 0 || !selected_brush) {
		rotate_drag_axis = -1;
		return;
	}
	rotate_drag_axis = -1;

	// Commit as undo: vertices before vs after.
	LevelBrush *target = selected_brush;

	PackedVector3Array new_verts = target->get_vertices_data();
	if (new_verts == gizmo_drag_original_verts) {
		return;
	}

	Array cur_faces = target->get_faces_data();
	Array cur_mats = target->get_face_materials_data();
	_commit_brush_undo(TTR("Rotate Brush"), target, gizmo_drag_original_verts, cur_faces, cur_mats);

	gizmo_drag_original_verts.clear();
}

void LevelEditorScreen::_apply_gizmo_rotate(int p_axis, real_t p_angle) {
	if (!selected_brush) {
		return;
	}
	// Rotate original vertices around the brush center, absolute per drag.
	selected_brush->set_vertices_data(gizmo_drag_original_verts);

	Vector3 center = selected_brush->get_center();
	Vector3 axis;
	axis[p_axis] = 1.0;
	Basis rot(axis, p_angle);

	for (int i = 0; i < selected_brush->get_vertex_count(); i++) {
		Vector3 v = selected_brush->get_vertex(i);
		v = center + rot.xform(v - center);
		selected_brush->set_vertex(i, v);
	}
	_refresh_map();
}

void LevelEditorScreen::_apply_gizmo_scale_uniform(real_t p_factor) {
	if (!selected_brush) {
		return;
	}
	selected_brush->set_vertices_data(gizmo_drag_original_verts);

	AABB bb;
	for (int i = 0; i < selected_brush->get_vertex_count(); i++) {
		if (i == 0) {
			bb.position = selected_brush->get_vertex(0);
		} else {
			bb.expand_to(selected_brush->get_vertex(i));
		}
	}
	Vector3 center = bb.get_center();
	real_t f = MAX(p_factor, 0.01);
	for (int i = 0; i < selected_brush->get_vertex_count(); i++) {
		Vector3 v = selected_brush->get_vertex(i);
		selected_brush->set_vertex(i, center + (v - center) * f);
	}
	_refresh_map();
}

void LevelEditorScreen::_apply_gizmo_scale(const Vector3 &p_world_delta) {
	if (!selected_brush) {
		return;
	}
	// Restore, then scale original vertices from the brush-local AABB min.
	selected_brush->set_vertices_data(gizmo_drag_original_verts);

	// Per-axis scale factor: 1 unit of drag = +100%.
	Transform3D inv = selected_brush->get_global_transform().affine_inverse();
	Vector3 local_delta = inv.basis.xform(p_world_delta);

	AABB bb;
	for (int i = 0; i < selected_brush->get_vertex_count(); i++) {
		if (i == 0) {
			bb.position = selected_brush->get_vertex(0);
		} else {
			bb.expand_to(selected_brush->get_vertex(i));
		}
	}

	Vector3 factors(1, 1, 1);
	if (gizmo_drag_part == GIZMO_XY || gizmo_drag_part == GIZMO_XZ || gizmo_drag_part == GIZMO_YZ) {
		// Center/plane drag: uniform scale by the largest dragged component.
		real_t f = 1.0 + MAX(local_delta.x, MAX(local_delta.y, local_delta.z));
		factors = Vector3(f, f, f);
	} else if (gizmo_drag_part >= GIZMO_X && gizmo_drag_part <= GIZMO_Z) {
		factors[gizmo_drag_part] = 1.0 + local_delta[gizmo_drag_part];
	}
	factors.x = MAX(factors.x, 0.01);
	factors.y = MAX(factors.y, 0.01);
	factors.z = MAX(factors.z, 0.01);

	for (int i = 0; i < selected_brush->get_vertex_count(); i++) {
		Vector3 v = selected_brush->get_vertex(i);
		v = bb.position + (v - bb.position) * factors;
		selected_brush->set_vertex(i, v);
	}
	_refresh_map();
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

	// Restore original vertices, then apply the new delta -> absolute drags.
	selected_brush->set_vertices_data(gizmo_drag_original_verts);

	// World delta -> brush-local delta (direction only).
	Transform3D inv = selected_brush->get_global_transform().affine_inverse();
	Vector3 local_delta = inv.basis.xform(p_world_delta);

	// Move only the selected vertices; faces deform to fit (Blender-style).
	Vector<int> indices = _get_gizmo_vertex_indices();
	selected_brush->move_vertices(indices, local_delta);
	_refresh_map();
}

void LevelEditorScreen::_gizmo_end_drag() {
	if (!gizmo_dragging) {
		return;
	}
	gizmo_dragging = false;
	gizmo_drag_uniform_scale = false;

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

	if (gizmo_drag_original_verts.is_empty()) {
		return;
	}

	// Commit the move as an undo action using the serialized vertices property.
	LevelBrush *target = selected_brush;

	PackedVector3Array new_data = target->get_vertices_data();
	if (new_data == gizmo_drag_original_verts) {
		return; // Nothing actually moved.
	}

	Array cur_faces = target->get_faces_data();
	Array cur_mats = target->get_face_materials_data();
	_commit_brush_undo(mode == MODE_ROTATE ? TTR("Rotate Brush") : (mode == MODE_SCALE ? TTR("Scale Brush") : TTR("Move Brush Element")), target, gizmo_drag_original_verts, cur_faces, cur_mats);

	gizmo_drag_original_verts.clear();
}

void LevelEditorScreen::_draw_gizmo(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (mode == MODE_BLOCK || mode == MODE_CLIP || mode == MODE_ROTATE || !_has_selection()) {
		return; // No arrow gizmo in Block/Clip/Rotate mode.
	}
	Vector3 origin = _get_gizmo_origin();
	Vector2 so;
	if (!p_vp->project(origin, so)) {
		return;
	}

	Camera3D *cam = p_vp->get_camera();
	Vector3 axes[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };
	Color axis_col[3] = { Color(0.95, 0.3, 0.3), Color(0.4, 0.9, 0.4), Color(0.3, 0.6, 1.0) };

	const real_t axis_len = 64.0 * EDSCALE;
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
		Color c = active ? axis_col[i].lerp(Color(1, 1, 1), 0.5) : axis_col[i];
		p_canvas->draw_line(so, axis_end[i], c, (active ? 3.0 : 2.0) * EDSCALE);
		// Arrowhead.
		Vector2 dir = (axis_end[i] - so).normalized();
		Vector2 perp(-dir.y, dir.x);
		real_t arrow_len = 10.0 * EDSCALE;
		real_t arrow_w = 4.0 * EDSCALE;
		p_canvas->draw_line(axis_end[i], axis_end[i] - dir * arrow_len + perp * arrow_w, c, 2.0 * EDSCALE);
		p_canvas->draw_line(axis_end[i], axis_end[i] - dir * arrow_len - perp * arrow_w, c, 2.0 * EDSCALE);
	}

	// Center square.
	real_t cs = 4.0 * EDSCALE;
	p_canvas->draw_rect(Rect2(so - Vector2(cs, cs), Size2(cs * 2, cs * 2)), Color(1, 1, 1, 0.9));
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
			if (p_vp->project(gt.xform(hover_brush->get_vertex(e.a)), a) && p_vp->project(gt.xform(hover_brush->get_vertex(e.b)), b)) {
				p_canvas->draw_line(a, b, Color(1, 1, 1, 0.5), 1.0);
			}
		}
	}
	_draw_drag_feedback(p_vp, p_canvas);
	_draw_ghost(p_vp, p_canvas);
	_draw_selection(p_vp, p_canvas);
	_draw_select_handles(p_vp, p_canvas);
	_draw_gizmo(p_vp, p_canvas);
	_draw_rotate_gizmo(p_vp, p_canvas);
	_draw_clip(p_vp, p_canvas);
}

void LevelEditorScreen::_draw_brush_outline(LevelEditorViewport *p_vp, Control *p_canvas, LevelBrush *p_brush, bool p_selected) {
	Transform3D gt = p_brush->get_global_transform();

	Color col = p_selected ? Color(1.0, 0.6, 0.1, 0.9) : Color(0.9, 0.9, 0.9, 0.35);
	real_t width = p_selected ? 2.0 : 1.0;

	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = p_brush->get_edges();
	for (const LevelBrush::EdgeKey &e : edges) {
		Vector2 a, b;
		if (p_vp->project(gt.xform(p_brush->get_vertex(e.a)), a) && p_vp->project(gt.xform(p_brush->get_vertex(e.b)), b)) {
			p_canvas->draw_line(a, b, col, width);
		}
	}
}

void LevelEditorScreen::_draw_drag_feedback(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (!dragging || !drag_active || !drag_viewport || ghost_active) {
		return;
	}

	Vector3 mins, maxs;
	_compute_drag_aabb(mins, maxs);

	LevelBrush *preview = memnew(LevelBrush);
	preview->setup_box(AABB(mins, maxs - mins));
	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = preview->get_edges();

	Color col(0.2, 0.9, 0.4, 0.9);
	for (const LevelBrush::EdgeKey &e : edges) {
		Vector2 a, b;
		if (p_vp->project(preview->get_vertex(e.a), a) && p_vp->project(preview->get_vertex(e.b), b)) {
			p_canvas->draw_line(a, b, col, 2.0);
		}
	}
	memdelete(preview);

	// Show the in-progress box's dimensions too.
	_draw_dim_labels(p_vp, p_canvas, AABB(mins, maxs - mins));

	if (p_vp == drag_viewport && p_vp->get_view_type() != LevelEditorViewport::VIEW_PERSPECTIVE) {
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
		LocalVector<int> poly = selected_brush->get_face(f);
		if (poly.size() < 3) {
			continue;
		}
		PackedVector2Array pts;
		bool all_front = true;
		for (int idx : poly) {
			Vector2 sp;
			if (!p_vp->project(gt.xform(selected_brush->get_vertex(idx)), sp)) {
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
		if (p_vp->project(gt.xform(selected_brush->get_vertex(e.a)), a) && p_vp->project(gt.xform(selected_brush->get_vertex(e.b)), b)) {
			p_canvas->draw_line(a, b, edge_col, 3.0);
		}
	}

	// Vertices.
	Color vert_col(1.0, 0.9, 0.2, 1.0);
	for (int v : selected_vertices) {
		Vector2 sp;
		if (p_vp->project(gt.xform(selected_brush->get_vertex(v)), sp)) {
			p_canvas->draw_rect(Rect2(sp - Vector2(4, 4), Size2(8, 8)), vert_col);
		}
	}

	// Hover highlight.
	Color hover_col(1, 1, 1, 0.5);
	switch (mode) {
		case MODE_FACE: {
			if (hover_brush && hover_face >= 0) {
				Transform3D hgt = hover_brush->get_global_transform();
				LocalVector<int> poly = hover_brush->get_face(hover_face);
				PackedVector2Array pts;
				bool ok = true;
				for (int idx : poly) {
					Vector2 sp;
					if (!p_vp->project(hgt.xform(hover_brush->get_vertex(idx)), sp)) {
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
				if (p_vp->project(gt.xform(selected_brush->get_vertex(hover_edge.a)), a) && p_vp->project(gt.xform(selected_brush->get_vertex(hover_edge.b)), b)) {
					p_canvas->draw_line(a, b, hover_col, 1.0);
				}
			}
		} break;
		case MODE_VERTEX: {
			if (has_hover_vertex && hover_brush == selected_brush) {
				Vector2 sp;
				if (p_vp->project(gt.xform(selected_brush->get_vertex(hover_vertex)), sp)) {
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
	return EditorInterface::get_singleton()->get_base_control()->get_theme_icon(SNAME("Subdivision"), SNAME("EditorIcons"));
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

void LevelEditorPlugin::edited_scene_changed() {
	screen->on_scene_changed();
}

void LevelEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		screen->show();
		screen->make_visible(true);
	} else {
		screen->hide();
	}
}
