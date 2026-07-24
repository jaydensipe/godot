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

#include "dock/level_editor_dock.h"
#include "level_constants.h"
#include "level_helpers.h"

using namespace LevelHelpers;
using LevelEditorColors::GIZMO_PLANE_EXTENT;

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
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/gui/button.h"
#include "scene/gui/control.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/separator.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"

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
	env->set_bg_color(LevelEditorColors::VIEWPORT_BG);
	env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
	env->set_ambient_light_color(LevelEditorColors::VIEWPORT_AMBIENT);
	env->set_ambient_light_energy(1.0);
	world_env->set_environment(env);
	subviewport->add_child(world_env);

	// 3D grid (perspective view): rendered as line geometry so brushes
	// occlude it correctly via the depth buffer.
	grid_mesh.instantiate();
	grid_mesh_instance = memnew(MeshInstance3D);
	grid_mesh_instance->set_mesh(grid_mesh);
	grid_mesh_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	Ref<StandardMaterial3D> grid_mat;
	grid_mat.instantiate();
	grid_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	grid_mat->set_flag(BaseMaterial3D::FLAG_USE_POINT_SIZE, false);
	grid_mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	grid_mat->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, false);
	grid_mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	grid_mesh_instance->set_material_override(grid_mat);
	// All SubViewports share the scene's World3D, so visibility alone can't
	// keep this out of the ortho panes - put it on a layer only the
	// perspective camera renders (set in set_view_type).
	grid_mesh_instance->set_layer_mask(1 << 19); // Layer 20 (of 20).
	// Sit a hair below Y=0 so brushes resting on the ground plane don't
	// z-fight the grid lines.
	grid_mesh_instance->set_position(Vector3(0, -0.002, 0));
	subviewport->add_child(grid_mesh_instance);

	overlay = memnew(Overlay);
	overlay->viewport = this;
	overlay->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	overlay->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	add_child(overlay);

	set_display_mode(DISPLAY_UNSHADED);

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

void LevelEditorViewport::set_grid_mesh_size(real_t p_grid_size) {
	if (p_grid_size == grid_mesh_size) {
		return;
	}
	grid_mesh_size = p_grid_size;
	// The 3D grid mesh only belongs to the perspective view - ortho views use
	// the infinite 2D overlay grid.
	if (grid_mesh_instance) {
		grid_mesh_instance->set_visible(view_type == VIEW_PERSPECTIVE && (!screen || screen->is_grid_3d_enabled()));
	}
	if (view_type == VIEW_PERSPECTIVE) {
		_rebuild_grid_mesh(p_grid_size);
	}
}

void LevelEditorViewport::set_grid_3d_visible(bool p_visible) {
	if (grid_mesh_instance) {
		grid_mesh_instance->set_visible(view_type == VIEW_PERSPECTIVE && p_visible);
	}
}

void LevelEditorViewport::_rebuild_grid_mesh(real_t p_grid_size) {
	if (!grid_mesh.is_valid() || p_grid_size <= 0) {
		return;
	}
	grid_mesh->clear_surfaces();
	grid_mesh->surface_begin(Mesh::PRIMITIVE_LINES);

	// Camera-centered (ground-projected), fixed extent around it - the grid
	// "follows" the camera like the 3D editor's.
	const real_t extent = LevelEditorGrid::GRID_3D_EXTENT;
	Vector3 cam_pos = camera ? camera->get_global_position() : Vector3();
	grid_mesh_center = Vector3(Math::snapped(cam_pos.x, p_grid_size), 0, Math::snapped(cam_pos.z, p_grid_size));

	int start = (int)Math::floor(-extent / p_grid_size);
	int end = (int)Math::ceil(extent / p_grid_size);
	for (int i = start; i <= end; i++) {
		real_t a = i * p_grid_size;
		bool is_major = (i % 8) == 0;

		// Line parallel to Z at world x = center.x + a: axis-blue only when
		// that x is exactly 0. Lines parallel to X get the same test on z.
		Color col_x = Math::is_zero_approx(grid_mesh_center.x + a) ? LevelEditorColors::GRID_AXIS : (is_major ? LevelEditorColors::GRID_MAJOR : LevelEditorColors::GRID_MINOR);
		Color col_z = Math::is_zero_approx(grid_mesh_center.z + a) ? LevelEditorColors::GRID_AXIS : (is_major ? LevelEditorColors::GRID_MAJOR : LevelEditorColors::GRID_MINOR);

		grid_mesh->surface_set_color(col_x);
		grid_mesh->surface_add_vertex(grid_mesh_center + Vector3(a, 0, -extent));
		grid_mesh->surface_add_vertex(grid_mesh_center + Vector3(a, 0, extent));
		grid_mesh->surface_set_color(col_z);
		grid_mesh->surface_add_vertex(grid_mesh_center + Vector3(-extent, 0, a));
		grid_mesh->surface_add_vertex(grid_mesh_center + Vector3(extent, 0, a));
	}
	grid_mesh->surface_end();
}

void LevelEditorViewport::_update_grid_tracking() {
	if (view_type != VIEW_PERSPECTIVE || grid_mesh_size <= 0 || !camera) {
		return;
	}
	// Rebuild when the camera has moved far enough that the fixed-extent grid
	// would go stale (half the extent).
	Vector3 cam_pos = camera->get_global_position();
	if (Vector2(cam_pos.x - grid_mesh_center.x, cam_pos.z - grid_mesh_center.z).length() > LevelEditorGrid::GRID_3D_REBUILD_DIST) {
		_rebuild_grid_mesh(grid_mesh_size);
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
		case VIEW_FRONT:
		case VIEW_SIDE:
			camera->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
			distance = 40.0;
			break;
	}
	_update_camera_transform();
	// The 3D grid lives on render layer 20; only the perspective camera culls
	// it in (the shared World3D makes plain visibility useless for this).
	if (camera) {
		camera->set_cull_mask(view_type == VIEW_PERSPECTIVE ? 0xFFFFF : 0x7FFFF);
	}
	if (grid_mesh_instance) {
		grid_mesh_instance->set_visible(view_type == VIEW_PERSPECTIVE);
		if (view_type == VIEW_PERSPECTIVE && grid_mesh_size > 0) {
			_rebuild_grid_mesh(grid_mesh_size);
		}
	}
}

void LevelEditorViewport::set_display_mode(DisplayMode p_mode) {
	display_mode = p_mode;
	if (!subviewport) {
		return;
	}
	static const Viewport::DebugDraw modes[DISPLAY_MAX] = {
		Viewport::DEBUG_DRAW_DISABLED, // DISPLAY_NORMAL
		Viewport::DEBUG_DRAW_WIREFRAME,
		Viewport::DEBUG_DRAW_OVERDRAW,
		Viewport::DEBUG_DRAW_LIGHTING,
		Viewport::DEBUG_DRAW_UNSHADED,
	};
	subviewport->set_debug_draw(modes[p_mode]);
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
			_update_grid_tracking();
		} break;
		case NOTIFICATION_WM_WINDOW_FOCUS_OUT: {
			if (view_controller.is_valid()) {
				view_controller->set_freelook_enabled(false);
			}
		} break;
	}
}

void LevelEditorViewport::_draw_grid() {
	if (!overlay || view_type == VIEW_PERSPECTIVE || (screen && !screen->is_grid_2d_enabled())) {
		return; // Perspective grid is a 3D mesh (depth-tested), not overlay.
	}
	const real_t gs = (screen ? screen->get_grid_size() : 1.0);
	if (gs <= 0) {
		return;
	}

	Color minor = LevelEditorColors::GRID_MINOR;
	Color major = LevelEditorColors::GRID_MAJOR;
	Color axis_col = LevelEditorColors::GRID_AXIS;

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
	mode_buttons[MODE_FACE]->set_tooltip_text(TTRC("Shift: Hold while dragging to extrude."));

	// Set shortcuts for buttons
	mode_buttons[MODE_SELECT]->set_shortcut(ED_SHORTCUT("spatial_editor/tool_transform", TTRC("Select / Move Mode"), Key::Q, true));
	mode_buttons[MODE_ROTATE]->set_shortcut(ED_SHORTCUT("spatial_editor/tool_rotate", TTRC("Rotate Mode"), Key::E, true));
	mode_buttons[MODE_SCALE]->set_shortcut(ED_SHORTCUT("spatial_editor/tool_scale", TTRC("Scale Mode"), Key::R, true));

	mode_buttons[MODE_BLOCK]->set_shortcut(ED_SHORTCUT("level_editor/tool_block", TTRC("Block Mode"), Key::B, true));
	mode_buttons[MODE_CLIP]->set_shortcut(ED_SHORTCUT("level_editor/tool_clip", TTRC("Clip Mode"), Key::C, true));

	mode_buttons[MODE_VERTEX]->set_shortcut(ED_SHORTCUT("level_editor/tool_vertex", TTRC("Vertex Mode"), Key::KEY_1, true));
	mode_buttons[MODE_EDGE]->set_shortcut(ED_SHORTCUT("level_editor/tool_edge", TTRC("Edge Mode"), Key::KEY_2, true));
	mode_buttons[MODE_FACE]->set_shortcut(ED_SHORTCUT("level_editor/tool_face", TTRC("Face Mode"), Key::KEY_3, true));

	toolbar->add_child(memnew(VSeparator));

	// View menu: per-viewport render display mode. IDs encode the viewport:
	// id = viewport * DISPLAY_MAX + mode.
	view_menu = memnew(MenuButton);
	view_menu->set_text(TTRC("View"));
	view_menu->set_flat(false);
	view_menu->set_theme_type_variation("FlatMenuButton");
	PopupMenu *view_popup = view_menu->get_popup();
	static const char *vp_names[4] = { "Perspective", "Top", "Front", "Side" };
	static const char *mode_names[LevelEditorViewport::DISPLAY_MAX] = { "Normal", "Wireframe", "Overdraw", "Lighting", "Unshaded" };
	for (int vp = 0; vp < 4; vp++) {
		PopupMenu *sub = memnew(PopupMenu);
		view_submenus[vp] = sub;
		sub->set_hide_on_checkable_item_selection(false);
		for (int m = 0; m < LevelEditorViewport::DISPLAY_MAX; m++) {
			sub->add_radio_check_item(TTRC(mode_names[m]), vp * LevelEditorViewport::DISPLAY_MAX + m);
		}
		sub->set_item_checked(LevelEditorViewport::DISPLAY_UNSHADED, true);
		sub->connect("id_pressed", callable_mp(this, &LevelEditorScreen::_view_display_selected));
		view_popup->add_submenu_node_item(TTRC(vp_names[vp]), sub);
	}
	view_popup->add_separator();
	// Grid toggles (global, not per-viewport). IDs past the display range.
	// Restored from project metadata below.
	grid_2d_enabled = EditorSettings::get_singleton()->get_project_metadata("level_editor", "grid_2d_enabled", true);
	grid_3d_enabled = EditorSettings::get_singleton()->get_project_metadata("level_editor", "grid_3d_enabled", true);
	view_popup->add_check_item(TTRC("Show 2D Grid"), 4 * LevelEditorViewport::DISPLAY_MAX);
	view_popup->add_check_item(TTRC("Show 3D Grid"), 4 * LevelEditorViewport::DISPLAY_MAX + 1);
	view_popup->set_item_checked(view_popup->get_item_index(4 * LevelEditorViewport::DISPLAY_MAX), grid_2d_enabled);
	view_popup->set_item_checked(view_popup->get_item_index(4 * LevelEditorViewport::DISPLAY_MAX + 1), grid_3d_enabled);
	view_popup->connect("id_pressed", callable_mp(this, &LevelEditorScreen::_view_grid_toggled));
	toolbar->add_child(view_menu);

	tools_menu = memnew(MenuButton);
	tools_menu->set_text(TTRC("Tools"));
	tools_menu->set_flat(false);
	tools_menu->set_theme_type_variation("FlatMenuButton");
	PopupMenu *tools_popup = tools_menu->get_popup();
	tools_popup->add_item(TTRC("Bridge Edge"), 0);
	tools_popup->connect("id_pressed", callable_mp(this, &LevelEditorScreen::_tools_menu_selected));
	toolbar->add_child(tools_menu);

	toolbar->add_child(memnew(VSeparator));

	vertex_menu = memnew(MenuButton);
	vertex_menu->set_text(TTRC("Vertex"));
	vertex_menu->set_flat(false);
	vertex_menu->set_theme_type_variation("FlatMenuButton");
	PopupMenu *vertex_popup = vertex_menu->get_popup();
	vertex_popup->add_item(TTRC("Extrude"), 0);
	vertex_popup->add_item(TTRC("Collapse"), 1);
	vertex_popup->connect("id_pressed", callable_mp(this, &LevelEditorScreen::_vertex_menu_selected));
	toolbar->add_child(vertex_menu);

	edge_menu = memnew(MenuButton);
	edge_menu->set_text(TTRC("Edge"));
	edge_menu->set_flat(false);
	edge_menu->set_theme_type_variation("FlatMenuButton");
	PopupMenu *edge_popup = edge_menu->get_popup();
	edge_popup->add_item(TTRC("Extrude"), 0);
	edge_popup->add_item(TTRC("Bridge"), 1);
	edge_popup->add_item(TTRC("Collapse"), 2);
	edge_popup->connect("id_pressed", callable_mp(this, &LevelEditorScreen::_edge_menu_selected));
	toolbar->add_child(edge_menu);

	face_menu = memnew(MenuButton);
	face_menu->set_text(TTRC("Face"));
	face_menu->set_flat(false);
	face_menu->set_theme_type_variation("FlatMenuButton");
	PopupMenu *face_popup = face_menu->get_popup();
	face_popup->add_item(TTRC("Extrude"), 0);
	face_popup->add_item(TTRC("Apply Material"), 1);
	face_popup->add_item(TTRC("Delete"), 2);
	face_popup->add_item(TTRC("Subdivide"), 4);
	face_popup->add_separator();
	face_popup->add_item(TTRC("Flip Faces"), 3);
	face_popup->connect("id_pressed", callable_mp(this, &LevelEditorScreen::_face_menu_selected));
	toolbar->add_child(face_menu);

	Control *toolbar_spring = memnew(Control);
	toolbar_spring->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	toolbar_spring->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	toolbar->add_child(toolbar_spring);

	Label *grid_label = memnew(Label);
	grid_label->set_text(TTRC("Grid:"));
	toolbar->add_child(grid_label);

	grid_size_option = memnew(OptionButton);
	grid_size_option->set_clip_text(true);
	grid_size_option->set_custom_minimum_size(Size2(115, 0));
	for (int i = 0; i < LevelEditorGrid::STEP_COUNT; i++) {
		// Plain decimals: integers without a trailing .0, fractions as-is.
		real_t step = LevelEditorGrid::STEPS[i];
		String label = (step >= 1.0) ? String::num_int64((int64_t)step) : String::num(step);
		grid_size_option->add_item(label);
	}
	grid_size_option->set_fit_to_longest_item(false);
	grid_size_option->select(_grid_step_index());
	grid_size_option->get_popup()->connect("index_pressed", callable_mp(this, &LevelEditorScreen::_grid_size_selected));
	toolbar->add_child(grid_size_option);

	toolbar->add_child(memnew(VSeparator));

	bake_button = memnew(Button);
	bake_button->set_text(TTRC("Bake Level"));
	bake_button->set_tooltip_text(TTRC("Bake brushes to a MeshInstance3D with trimesh collision and an occluder."));
	bake_button->connect("pressed", callable_mp(this, &LevelEditorScreen::_bake_pressed));
	toolbar->add_child(bake_button);

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

	// Ortho views default to overdraw (engine renders its BG black); perspective
	// stays unshaded. Saved modes override below.
	for (int vp = 1; vp < 4; vp++) {
		viewports[vp]->set_display_mode(LevelEditorViewport::DISPLAY_OVERDRAW);
		for (int i = 0; i < view_submenus[vp]->get_item_count(); i++) {
			view_submenus[vp]->set_item_checked(i, (view_submenus[vp]->get_item_id(i) % LevelEditorViewport::DISPLAY_MAX) == LevelEditorViewport::DISPLAY_OVERDRAW);
		}
	}

	// Restore per-viewport display modes saved for this project (default is
	// Unshaded, set in the viewport constructor).
	{
		Array saved = EditorSettings::get_singleton()->get_project_metadata("level_editor", "viewport_display_modes", Array());
		for (int vp = 0; vp < 4 && vp < saved.size(); vp++) {
			int m = (int)saved[vp];
			if (m < 0 || m >= LevelEditorViewport::DISPLAY_MAX) {
				continue;
			}
			viewports[vp]->set_display_mode((LevelEditorViewport::DisplayMode)m);
			for (int i = 0; i < view_submenus[vp]->get_item_count(); i++) {
				view_submenus[vp]->set_item_checked(i, (view_submenus[vp]->get_item_id(i) % LevelEditorViewport::DISPLAY_MAX) == m);
			}
		}
	}

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
			int idx = _grid_step_index();
			idx += (k->get_keycode() == Key::BRACKETRIGHT) ? 1 : -1;
			idx = CLAMP(idx, 0, LevelEditorGrid::STEP_COUNT - 1);
			if (LevelEditorGrid::STEPS[idx] != grid_size) {
				grid_size = LevelEditorGrid::STEPS[idx];
				grid_size_option->select(idx);
				_update_overlays();
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

// Finds the first LevelMap in the edited scene (DFS), or nullptr.
LevelMap *LevelEditorScreen::_find_map_in_scene() const {
	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	if (!root) {
		return nullptr;
	}
	List<Node *> stack;
	stack.push_back(root);
	while (!stack.is_empty()) {
		Node *n = stack.front()->get();
		stack.pop_front();
		LevelMap *lm = Object::cast_to<LevelMap>(n);
		if (lm) {
			return lm;
		}
		for (int i = 0; i < n->get_child_count(); i++) {
			stack.push_back(n->get_child(i));
		}
	}
	return nullptr;
}

void LevelEditorScreen::_resolve_map() {
	if (current_map) {
		return;
	}
	current_map = _find_map_in_scene();
	if (current_map) {
		current_map->refresh();
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
		current_map = _find_map_in_scene();
		if (current_map) {
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
	// Drop element selection whenever the mode changes - each tool owns its
	// own selection type.
	_clear_element_selection();
	if (mode != MODE_BLOCK && ghost_active) {
		_ghost_cancel();
	}
	if (mode != MODE_CLIP && clip_active) {
		_clip_cancel();
	}
	_update_overlays();
}

int LevelEditorScreen::_grid_step_index() const {
	int idx = 0;
	for (int i = 0; i < LevelEditorGrid::STEP_COUNT; i++) {
		if (LevelEditorGrid::STEPS[i] <= grid_size) {
			idx = i;
		}
	}
	return idx;
}

void LevelEditorScreen::_grid_size_selected(int p_index) {
	grid_size = LevelEditorGrid::STEPS[CLAMP(p_index, 0, LevelEditorGrid::STEP_COUNT - 1)];
	grid_size_option->select(CLAMP(p_index, 0, LevelEditorGrid::STEP_COUNT - 1));
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

// Records one brush's current topology as the do-state against the given
// snapshot, into an already-created undo action. Shared by _commit_brush_undo
// (single brush) and the multi-brush delete/tool paths (one action spanning
// several brushes).
void LevelEditorScreen::_add_brush_undo_pair(EditorUndoRedoManager *p_undo_redo, LevelBrush *p_brush, const PackedVector3Array &p_old_verts, const Array &p_old_faces, const Array &p_old_mats) {
	p_undo_redo->add_do_property(p_brush, "vertices", p_brush->get_vertices_data());
	p_undo_redo->add_do_property(p_brush, "faces", p_brush->get_faces_data());
	p_undo_redo->add_do_property(p_brush, "face_materials", p_brush->get_face_materials_data());
	p_undo_redo->add_undo_property(p_brush, "vertices", p_old_verts);
	p_undo_redo->add_undo_property(p_brush, "faces", p_old_faces);
	p_undo_redo->add_undo_property(p_brush, "face_materials", p_old_mats);
}

void LevelEditorScreen::_delete_selection() {
	if (!current_map) {
		return;
	}

	switch (mode) {
		case MODE_SELECT: {
			if (!selected_brush) {
				return;
			}
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
		case MODE_FACE:
			_action_delete_faces();
			break;
		case MODE_EDGE:
			_action_collapse_edges();
			break;
		case MODE_VERTEX:
			_action_collapse_vertices();
			break;
		default:
			break;
	}
	_update_overlays();
}

void LevelEditorScreen::_commit_brush_undo(const String &p_action, LevelBrush *p_brush, const PackedVector3Array &p_old_verts, const Array &p_old_faces, const Array &p_old_mats, bool p_execute) {
	LevelMap *map = current_map;
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(p_action);
	_add_brush_undo_pair(undo_redo, p_brush, p_old_verts, p_old_faces, p_old_mats);
	undo_redo->add_do_method(map, "refresh");
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
	_clear_element_selection();
	hover_brush = nullptr;
	hover_face = -1;
	has_hover_edge = false;
	has_hover_vertex = false;
}

void LevelEditorScreen::_clear_element_selection() {
	selected_faces.clear();
	selected_edges.clear();
	selected_vertices.clear();
}

void LevelEditorScreen::_update_overlays() {
	for (int i = 0; i < 4; i++) {
		viewports[i]->set_grid_mesh_size(grid_size);
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
	if (!current_map) {
		return false;
	}

	real_t best = 16.0; // pixels.
	bool found = false;
	int best_v = -1;
	LevelBrush *best_brush = nullptr;

	Vector<LevelBrush *> brushes = current_map->get_brushes();
	for (LevelBrush *brush : brushes) {
		Transform3D gt = brush->get_global_transform();
		for (int i = 0; i < brush->get_vertex_count(); i++) {
			Vector3 w = gt.xform(brush->get_vertex(i));
			if (p_camera->is_position_behind(w)) {
				continue;
			}
			Vector2 sp = p_camera->unproject_position(w);
			real_t d = sp.distance_to(p_screen);
			if (d < best) {
				best = d;
				best_v = i;
				best_brush = brush;
				found = true;
			}
		}
	}
	if (found) {
		r_brush = best_brush;
		r_vertex = best_v;
	}
	return found;
}

bool LevelEditorScreen::_pick_edge(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, LevelBrush::EdgeKey &r_edge) const {
	if (!current_map) {
		return false;
	}

	real_t best = 12.0; // pixels.
	bool found = false;
	LevelBrush::EdgeKey best_edge;
	LevelBrush *best_brush = nullptr;

	Vector<LevelBrush *> brushes = current_map->get_brushes();
	for (LevelBrush *brush : brushes) {
		Transform3D gt = brush->get_global_transform();
		HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = brush->get_edges();
		for (const LevelBrush::EdgeKey &e : edges) {
			Vector3 wa = gt.xform(brush->get_vertex(e.a));
			Vector3 wb = gt.xform(brush->get_vertex(e.b));
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
				best_brush = brush;
				found = true;
			}
		}
	}
	if (found) {
		r_brush = best_brush;
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
			// Also resolve which brush is under the cursor (face pick) so all of
			// its vertices can be shown even when not directly over one.
			if (!has_hover_vertex) {
				Vector3 hit;
				int f;
				LevelBrush *b = nullptr;
				if (_pick_face(cam, p_mouse, b, f, hit)) {
					hover_brush = b;
				}
			}
		} break;
		case MODE_EDGE: {
			has_hover_edge = _pick_edge(cam, p_mouse, hover_brush, hover_edge);
			if (!has_hover_edge) {
				Vector3 hit;
				int f;
				LevelBrush *b = nullptr;
				if (_pick_face(cam, p_mouse, b, f, hit)) {
					hover_brush = b;
				}
			}
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
					select_drag_original_verts = selected_brush->get_vertices_data();
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
					gizmo_extrude_drag = (mode == MODE_FACE && mb->is_shift_pressed());
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
				} else if (vp->get_view_type() != LevelEditorViewport::VIEW_PERSPECTIVE) {
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
					if (!add) {
						selected_faces.clear();
					}
					HashSet<int> &set = _face_set(brush);
					if (set.has(f) && add) {
						set.erase(f);
						if (set.is_empty()) {
							selected_faces.erase(brush);
						}
					} else {
						set.insert(f);
					}
					if (brush != selected_brush) {
						selected_brush = brush;
						_edit_brush_node(brush);
					}
				} else if (!add) {
					_clear_selection();
				}
			} break;
			case MODE_EDGE: {
				LevelBrush *brush = nullptr;
				LevelBrush::EdgeKey e;
				if (_pick_edge(p_camera, mb->get_position(), brush, e)) {
					if (!add) {
						selected_edges.clear();
					}
					HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> &set = _edge_set(brush);
					if (set.has(e) && add) {
						set.erase(e);
						if (set.is_empty()) {
							selected_edges.erase(brush);
						}
					} else {
						set.insert(e);
					}
					if (brush != selected_brush) {
						selected_brush = brush;
						_edit_brush_node(brush);
					}
				} else if (!add) {
					_clear_selection();
				}
			} break;
			case MODE_VERTEX: {
				LevelBrush *brush = nullptr;
				int v;
				if (_pick_vertex(p_camera, mb->get_position(), brush, v)) {
					if (!add) {
						selected_vertices.clear();
					}
					HashSet<int> &set = _vertex_set(brush);
					if (set.has(v) && add) {
						set.erase(v);
						if (set.is_empty()) {
							selected_vertices.erase(brush);
						}
					} else {
						set.insert(v);
					}
					if (brush != selected_brush) {
						selected_brush = brush;
						_edit_brush_node(brush);
					}
				} else if (!add) {
					_clear_selection();
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
	// Use the vertex snapshot - remapping into the original AABB would bake in
	// any data lost by a degenerate intermediate drag.
	selected_brush->set_vertices_data(select_drag_original_verts);

	Transform3D gt = selected_brush->get_global_transform();
	Transform3D inv = gt.affine_inverse();

	AABB bb = select_drag_original_aabb;
	Vector3 mins = bb.position;
	Vector3 maxs = bb.position + bb.size;

	int h = select_handle_drag;

	// Intersect with a camera-facing plane that contains the dragged axis in
	// world space (the view plane is parallel to the Y axis in top view, so it
	// can never move handles up/down), then convert to brush-local.
	if (h >= GHOST_CORNER_0) {
		int ci = h - GHOST_CORNER_0;
		Vector3 corners[8];
		aabb_corners(bb, corners);
		Vector3 wc = gt.xform(corners[ci]);
		for (int axis = 0; axis < 3; axis++) {
			Vector3 hit;
			if (!_ray_to_axis_plane(p_vp, p_mouse, wc, axis, hit)) {
				continue;
			}
			real_t v = _snap(inv.xform(hit)[axis]);
			if (ci & (1 << axis)) {
				maxs[axis] = MAX(v, mins[axis] + grid_size);
			} else {
				mins[axis] = MIN(v, maxs[axis] - grid_size);
			}
		}
	} else {
		int axis = (h - GHOST_FACE_XN) / 2;
		bool is_max = ((h - GHOST_FACE_XN) % 2) == 1;
		Vector3 hit;
		Vector3 wc = gt.xform(bb.get_center());
		if (!_ray_to_axis_plane(p_vp, p_mouse, wc, axis, hit)) {
			return;
		}
		real_t v = _snap(inv.xform(hit)[axis]);
		if (is_max) {
			maxs[axis] = MAX(v, mins[axis] + grid_size);
		} else {
			mins[axis] = MIN(v, maxs[axis] - grid_size);
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

	// Commit as undo: original vertices vs current geometry.
	LevelBrush *target = selected_brush;
	PackedVector3Array old_verts = select_drag_original_verts;
	select_drag_original_verts.clear();

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
	Color box_col = LevelEditorColors::BRUSH_OUTLINE_SELECTED;

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
			Color hc = hot ? LevelEditorColors::SELECT_HANDLE_HOT : LevelEditorColors::SELECT_HANDLE;
			real_t hs_px = 4.0 * EDSCALE;
			p_canvas->draw_rect(Rect2(sp - Vector2(hs_px, hs_px), Size2(hs_px * 2, hs_px * 2)), hc);
		}
	}

	// Corner handles.
	for (int i = 0; i < 8; i++) {
		Vector2 sp;
		if (p_vp->project(gt.xform(corners[i]), sp)) {
			bool hot = (select_handle_hover == GHOST_CORNER_0 + i || select_handle_drag == GHOST_CORNER_0 + i);
			Color hc = hot ? LevelEditorColors::SELECT_HANDLE_HOT : LevelEditorColors::SELECT_HANDLE;
			real_t hs_px = 3.0 * EDSCALE;
			p_canvas->draw_rect(Rect2(sp - Vector2(hs_px, hs_px), Size2(hs_px * 2, hs_px * 2)), hc);
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
			bool pruned = false;
			if (selected_brush && !selected_brush->is_inside_tree()) {
				selected_brush = nullptr;
				pruned = true;
			}
			// Prune element selections whose brush was deleted.
			pruned = _prune_dead_brushes(selected_faces) || pruned;
			pruned = _prune_dead_brushes(selected_edges) || pruned;
			pruned = _prune_dead_brushes(selected_vertices) || pruned;
			if (pruned) {
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
				p_canvas->draw_line(a, b, LevelEditorColors::BRUSH_OUTLINE_HOVER, 1.0);
			}
		}
	}
	// Element modes: the hovered brush - and any brush with selected elements
	// of the current mode - gets a light-blue outline and green vertices.
	if (mode == MODE_VERTEX || mode == MODE_EDGE || mode == MODE_FACE) {
		// Collect the brushes to highlight: hovered + those with a selection.
		LocalVector<LevelBrush *> highlight;
		if (hover_brush) {
			highlight.push_back(hover_brush);
		}
		auto add_selected_brushes = [&](auto &p_map) {
			for (const auto &E : p_map) {
				bool dup = false;
				for (LevelBrush *b : highlight) {
					if (b == E.key) {
						dup = true;
						break;
					}
				}
				if (!dup) {
					highlight.push_back(E.key);
				}
			}
		};
		switch (mode) {
			case MODE_VERTEX:
				add_selected_brushes(selected_vertices);
				break;
			case MODE_FACE:
				add_selected_brushes(selected_faces);
				break;
			case MODE_EDGE:
				add_selected_brushes(selected_edges);
				break;
			default:
				break;
		}

		const real_t vs = 3.0 * EDSCALE; // half-size, normal.
		const real_t vs_hot = 4.5 * EDSCALE; // half-size, hovered.
		for (LevelBrush *brush : highlight) {
			Transform3D gt = brush->get_global_transform();
			HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = brush->get_edges();
			for (const LevelBrush::EdgeKey &e : edges) {
				Vector2 a, b;
				if (!p_vp->project(gt.xform(brush->get_vertex(e.a)), a) || !p_vp->project(gt.xform(brush->get_vertex(e.b)), b)) {
					continue;
				}
				if (mode == MODE_EDGE) {
					// Edges stay light-blue; only the hovered edge turns green.
					bool hot = (brush == hover_brush && has_hover_edge && e == hover_edge);
					p_canvas->draw_line(a, b, hot ? LevelEditorColors::HOVER_ELEMENT : LevelEditorColors::HOVER_BRUSH_OUTLINE, hot ? 2.5 : 1.5);
				} else {
					// Vertex/Face modes: light-blue outline only.
					p_canvas->draw_line(a, b, LevelEditorColors::HOVER_BRUSH_OUTLINE, 1.5);
				}
			}
			if (mode == MODE_FACE && brush == hover_brush && hover_face >= 0) {
				// Hovered face: green fill + outline.
				LocalVector<int> poly = brush->get_face(hover_face);
				if (poly.size() >= 3) {
					PackedVector2Array pts;
					bool ok = true;
					for (int idx : poly) {
						Vector2 sp;
						if (!p_vp->project(gt.xform(brush->get_vertex(idx)), sp)) {
							ok = false;
							break;
						}
						pts.push_back(sp);
					}
					if (ok) {
						p_canvas->draw_colored_polygon(pts, LevelEditorColors::HOVER_FACE_FILL);
						for (int i = 0; i < pts.size(); i++) {
							p_canvas->draw_line(pts[i], pts[(i + 1) % pts.size()], LevelEditorColors::HOVER_ELEMENT, 2.0);
						}
					}
				}
			}
			if (mode != MODE_VERTEX) {
				continue; // Only vertex mode shows vertex markers.
			}
			// All vertices in bright green; the vertex under the cursor is
			// slightly larger.
			Color vert_col = LevelEditorColors::HOVER_ELEMENT;
			for (int i = 0; i < brush->get_vertex_count(); i++) {
				Vector2 sp;
				if (p_vp->project(gt.xform(brush->get_vertex(i)), sp)) {
					bool hot = (brush == hover_brush && has_hover_vertex && i == hover_vertex);
					real_t hs = hot ? vs_hot : vs;
					// 1px black outline behind the fill.
					p_canvas->draw_rect(Rect2(sp - Vector2(hs + 1, hs + 1), Size2((hs + 1) * 2, (hs + 1) * 2)), LevelEditorColors::VERTEX_OUTLINE);
					p_canvas->draw_rect(Rect2(sp - Vector2(hs, hs), Size2(hs * 2, hs * 2)), vert_col);
				}
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

	// In element modes there is no whole-brush selection - draw all brushes
	// with the plain outline (hovered brush gets its own highlight).
	bool element_mode = (mode == MODE_VERTEX || mode == MODE_EDGE || mode == MODE_FACE);
	Color col = (p_selected && !element_mode) ? LevelEditorColors::BRUSH_OUTLINE_SELECTED : LevelEditorColors::BRUSH_OUTLINE;
	real_t width = (p_selected && !element_mode) ? 2.0 : 1.0;

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

	Color col = LevelEditorColors::GHOST;
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
			p_canvas->draw_rect(r, LevelEditorColors::DRAG_RECT, false, 1.0);
		}
	}
}

void LevelEditorScreen::_draw_selection(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (!current_map) {
		return;
	}

	// Faces.
	Color face_col = LevelEditorColors::SELECTED_FACE_FILL;
	Color face_outline = LevelEditorColors::SELECTED_ELEMENT;
	for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
		Transform3D gt = E.key->get_global_transform();
		for (int f : E.value) {
			LocalVector<int> poly = E.key->get_face(f);
			if (poly.size() < 3) {
				continue;
			}
			PackedVector2Array pts;
			bool all_front = true;
			for (int idx : poly) {
				Vector2 sp;
				if (!p_vp->project(gt.xform(E.key->get_vertex(idx)), sp)) {
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
	}

	// Edges (selected: same orange as selected-face outlines).
	Color edge_col = LevelEditorColors::SELECTED_ELEMENT;
	for (const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
		Transform3D gt = E.key->get_global_transform();
		for (const LevelBrush::EdgeKey &e : E.value) {
			Vector2 a, b;
			if (p_vp->project(gt.xform(E.key->get_vertex(e.a)), a) && p_vp->project(gt.xform(E.key->get_vertex(e.b)), b)) {
				p_canvas->draw_line(a, b, edge_col, 3.0);
			}
		}
	}

	// Vertices (selected: same orange as selected-face outlines).
	Color vert_col = LevelEditorColors::SELECTED_ELEMENT;
	const real_t sel_vs = 4.5 * EDSCALE;
	for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_vertices) {
		Transform3D gt = E.key->get_global_transform();
		for (int v : E.value) {
			Vector2 sp;
			if (p_vp->project(gt.xform(E.key->get_vertex(v)), sp)) {
				p_canvas->draw_rect(Rect2(sp - Vector2(sel_vs + 1, sel_vs + 1), Size2((sel_vs + 1) * 2, (sel_vs + 1) * 2)), LevelEditorColors::VERTEX_OUTLINE);
				p_canvas->draw_rect(Rect2(sp - Vector2(sel_vs, sel_vs), Size2(sel_vs * 2, sel_vs * 2)), vert_col);
			}
		}
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

	dock = memnew(LevelEditorDock);
	dock->set_screen(screen);
	add_control_to_dock(DOCK_SLOT_RIGHT_BL, dock);
	// Tab icon is set lazily in make_visible() - theme icons aren't
	// registered yet when plugins construct.

	EditorInterface::get_singleton()->get_selection()->connect("selection_changed", callable_mp(this, &LevelEditorPlugin::_editor_selection_changed));
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
		// Theme icons are guaranteed registered by now.
		set_dock_tab_icon(dock, get_plugin_icon());
		screen->show();
		screen->make_visible(true);
	} else {
		screen->hide();
	}
}
