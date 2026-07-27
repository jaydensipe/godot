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

#include "../level_constants.h"
#include "dock/level_editor_dock.h"
#include "level_helpers.h"

using namespace LevelHelpers;
using LevelEditorColors::GIZMO_PLANE_EXTENT;

#include "core/math/geometry_2d.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
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

	// The drop highlight gets its own overlay so its animation redraws stay
	// cheap (the main overlay repaints every brush outline/gizmo/preview).
	drop_overlay = memnew(DropOverlay);
	drop_overlay->viewport = this;
	drop_overlay->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	drop_overlay->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	add_child(drop_overlay);

	// Material drag-and-drop from the FileSystem dock (bound with this control
	// as the p_from argument - same as SET_DRAG_FORWARDING_CD).
	set_drag_forwarding(Callable(), callable_mp(this, &LevelEditorViewport::can_drop_data_fw).bind(this), callable_mp(this, &LevelEditorViewport::drop_data_fw).bind(this));

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

void LevelEditorViewport::DropOverlay::_notification(int p_what) {
	if (p_what == NOTIFICATION_DRAW && viewport) {
		viewport->_drop_overlay_draw();
	}
}

void LevelEditorViewport::_overlay_draw() {
	_draw_grid();
	if (screen) {
		screen->_draw_viewport_overlay(this, overlay);
	}
}

void LevelEditorViewport::_drop_overlay_draw() {
	if (screen) {
		screen->_draw_material_drop(this, drop_overlay);
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

bool LevelEditorViewport::project_segment(const Vector3 &p_a, const Vector3 &p_b, Vector2 &r_a, Vector2 &r_b) const {
	// Ortho cameras have no near-plane crossing issue (parallel rays).
	if (camera->get_projection() != Camera3D::PROJECTION_PERSPECTIVE) {
		return project(p_a, r_a) && project(p_b, r_b);
	}
	// Clip in camera space: forward is -Z, so a point is visible when
	// z <= -near. Move behind-camera endpoints to the near plane along the
	// segment (GOTCHAS #22: projecting a behind-camera point mirrors it).
	const Transform3D cam = camera->get_global_transform();
	const real_t nz = -camera->get_near();
	Vector3 a = cam.affine_inverse().xform(p_a);
	Vector3 b = cam.affine_inverse().xform(p_b);
	const bool a_vis = a.z <= nz;
	const bool b_vis = b.z <= nz;
	if (!a_vis && !b_vis) {
		return false;
	}
	if (a_vis != b_vis) {
		const real_t t = (nz - a.z) / (b.z - a.z);
		const Vector3 cross = a + (b - a) * t;
		if (!a_vis) {
			a = cross;
		} else {
			b = cross;
		}
	}
	r_a = camera->unproject_position(cam.xform(a));
	r_b = camera->unproject_position(cam.xform(b));
	return true;
}

bool LevelEditorViewport::project_polygon(const Vector<Vector3> &p_world, PackedVector2Array &r_screen) const {
	r_screen.clear();
	if (p_world.size() < 3) {
		return false;
	}
	if (camera->get_projection() != Camera3D::PROJECTION_PERSPECTIVE) {
		for (const Vector3 &w : p_world) {
			Vector2 sp;
			if (!project(w, sp)) {
				return false;
			}
			r_screen.push_back(sp);
		}
		return true;
	}
	const Transform3D cam = camera->get_global_transform();
	const real_t nz = -camera->get_near();
	const Transform3D inv = cam.affine_inverse();
	// Sutherland-Hodgman against the single near plane (z <= -near).
	for (int i = 0; i < p_world.size(); i++) {
		const Vector3 cur = inv.xform(p_world[i]);
		const Vector3 prev = inv.xform(p_world[(i + p_world.size() - 1) % p_world.size()]);
		const bool cur_vis = cur.z <= nz;
		const bool prev_vis = prev.z <= nz;
		if (cur_vis != prev_vis) {
			const real_t t = (nz - prev.z) / (cur.z - prev.z);
			const Vector3 cross = prev + (cur - prev) * t;
			r_screen.push_back(camera->unproject_position(cam.xform(cross)));
		}
		if (cur_vis) {
			r_screen.push_back(camera->unproject_position(p_world[i]));
		}
	}
	return r_screen.size() >= 3;
}

void LevelEditorViewport::queue_overlay_redraw() {
	if (overlay) {
		overlay->update();
	}
}

void LevelEditorViewport::_queue_drop_redraw() {
	if (drop_overlay) {
		drop_overlay->queue_redraw();
	}
}

void LevelEditorViewport::clear_drop_state() {
	drop_payload_checked = false;
	drop_payload_ok = false;
	drop_last_probe = Vector2(Math::INF, Math::INF);
	if (drop_active) {
		drop_active = false;
		drop_brush = nullptr;
		drop_face = -1;
		drop_phase = 0.0;
		_queue_drop_redraw();
	}
}

bool LevelEditorViewport::can_drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) {
	// Called continuously while a drag hovers this viewport; (INF, INF)
	// signals the drag left without dropping.
	if (p_point == Vector2(Math::INF, Math::INF)) {
		clear_drop_state();
		return false;
	}
	if (!screen) {
		return false;
	}

	// (C) The payload is invariant for the whole drag session, so validate
	// its type once instead of on every motion event.
	if (!drop_payload_checked) {
		drop_payload_checked = true;
		drop_payload_ok = LevelEditorMaterials::drag_data_is_material(p_data);
	}
	if (!drop_payload_ok) {
		return false;
	}

	// (B) The face ray-pick sweeps every brush triangle - only re-run it
	// when the cursor has moved enough to change the pick (~4px).
	bool ok = drop_active;
	if (drop_last_probe.x == Math::INF || drop_last_probe.distance_squared_to(p_point) > 16.0) {
		drop_last_probe = p_point;
		LevelBrush *brush = nullptr;
		int face = -1;
		ok = screen->_material_drop_pick(camera, p_point, brush, face);
		if (ok != drop_active || brush != drop_brush || face != drop_face) {
			drop_active = ok;
			drop_brush = brush;
			drop_face = face;
			_queue_drop_redraw();
		}
	}
	return ok;
}

void LevelEditorViewport::drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) {
	LevelBrush *brush = nullptr;
	int face = -1;
	if (screen && screen->_material_drop_probe(camera, p_point, p_data, brush, face)) {
		screen->_apply_material_drop(brush, face, p_data);
	}
	clear_drop_state();
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
			if (drop_active) {
				// Marching-ants drop highlight on the dedicated DropOverlay:
				// cheap enough to redraw at full speed (1px phase steps).
				const double new_phase = Math::fposmod(drop_phase + get_process_delta_time() * 60.0, 16.0);
				if (Math::floor(new_phase) != Math::floor(drop_phase)) {
					_queue_drop_redraw();
				}
				drop_phase = new_phase;
			}
		} break;
		case NOTIFICATION_DRAG_END: {
			// Esc-cancel (or any non-drop drag end) never calls can_drop_data
			// with the INF sentinel, so clear the highlight here instead.
			clear_drop_state();
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
	ClassDB::bind_method(D_METHOD("clear_selection"), &LevelEditorScreen::clear_selection);
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

	// Tool modes in button-group panels (Select, Move, Rotate, Scale) / (Block, Clip, Mirror)...
	PanelContainer *tool_panel = memnew(PanelContainer);
	tool_panel->set_theme_type_variation("PanelContainerButtonGroup");
	toolbar->add_child(tool_panel);
	HBoxContainer *tool_hbox = memnew(HBoxContainer);
	tool_panel->add_child(tool_hbox);

	for (int i = TOOL_SELECT; i <= TOOL_SCALE; i++) {
		Button *b = memnew(Button);
		b->set_toggle_mode(true);
		b->set_pressed(i == 0);
		b->connect("pressed", callable_mp(this, &LevelEditorScreen::_tool_changed).bind(i));
		b->set_theme_type_variation(SceneStringName(FlatButton));
		tool_hbox->add_child(b);
		tool_buttons[i] = b;
	}

	toolbar->add_child(memnew(VSeparator));

	PanelContainer *draw_panel = memnew(PanelContainer);
	draw_panel->set_theme_type_variation("PanelContainerButtonGroup");
	toolbar->add_child(draw_panel);

	HBoxContainer *draw_hbox = memnew(HBoxContainer);
	draw_panel->add_child(draw_hbox);

	for (int i = TOOL_BLOCK; i <= TOOL_MIRROR; i++) {
		Button *b = memnew(Button);
		b->set_toggle_mode(true);
		b->set_pressed(false);
		b->connect("pressed", callable_mp(this, &LevelEditorScreen::_tool_changed).bind(i));
		b->set_theme_type_variation(SceneStringName(FlatButton));
		draw_hbox->add_child(b);
		tool_buttons[i] = b;
	}

	toolbar->add_child(memnew(VSeparator));

	// ...and the selection target in a second panel (Mesh, Vertex, Edge, Face).
	// Orthogonal to the tool: any transform tool can act on any target.
	PanelContainer *element_panel = memnew(PanelContainer);
	element_panel->set_theme_type_variation("PanelContainerButtonGroup");
	toolbar->add_child(element_panel);

	HBoxContainer *element_hbox = memnew(HBoxContainer);
	element_panel->add_child(element_hbox);

	for (int i = 0; i < TARGET_MAX; i++) {
		Button *b = memnew(Button);
		b->set_toggle_mode(true);
		b->set_pressed(i == TARGET_MESH);
		b->connect("pressed", callable_mp(this, &LevelEditorScreen::_target_changed).bind(i));
		b->set_theme_type_variation(SceneStringName(FlatButton));
		element_hbox->add_child(b);
		target_buttons[i] = b;
	}

	// Icons are (re)assigned in NOTIFICATION_THEME_CHANGED. Text labels are
	// fallbacks for buttons without icons.
	tool_buttons[TOOL_SELECT]->set_tooltip_text(TTRC("Select (resize handles on a single selected brush)"));
	tool_buttons[TOOL_MOVE]->set_tooltip_text(TTRC("Move (translate gizmo)"));
	tool_buttons[TOOL_ROTATE]->set_tooltip_text(TTRC("Rotate"));
	tool_buttons[TOOL_SCALE]->set_tooltip_text(TTRC("Scale"));
	tool_buttons[TOOL_BLOCK]->set_tooltip_text(TTRC("Block"));
	tool_buttons[TOOL_CLIP]->set_tooltip_text(TTRC("Clip"));
	tool_buttons[TOOL_MIRROR]->set_tooltip_text(TTRC("Mirror (draw a plane to duplicate the brush reflected across it)"));
	target_buttons[TARGET_VERTEX]->set_tooltip_text(TTRC("Vertex"));
	target_buttons[TARGET_EDGE]->set_tooltip_text(TTRC("Edge (double-click: select straight chain, Alt+double-click: select loop)"));
	target_buttons[TARGET_FACE]->set_tooltip_text(TTRC("Face (Shift: hold while dragging to extrude)"));
	target_buttons[TARGET_MESH]->set_tooltip_text(TTRC("Mesh (whole-brush selection)"));

	// Set shortcuts for buttons
	tool_buttons[TOOL_SELECT]->set_shortcut(ED_SHORTCUT("spatial_editor/tool_transform", TTRC("Select Mode"), Key::Q, true));
	tool_buttons[TOOL_MOVE]->set_shortcut(ED_SHORTCUT("spatial_editor/tool_move", TTRC("Move Mode"), Key::W, true));
	tool_buttons[TOOL_ROTATE]->set_shortcut(ED_SHORTCUT("spatial_editor/tool_rotate", TTRC("Rotate Mode"), Key::E, true));
	tool_buttons[TOOL_SCALE]->set_shortcut(ED_SHORTCUT("spatial_editor/tool_scale", TTRC("Scale Mode"), Key::R, true));

	tool_buttons[TOOL_BLOCK]->set_shortcut(ED_SHORTCUT("level_editor/tool_block", TTRC("Block Tool"), Key::B, true));
	tool_buttons[TOOL_CLIP]->set_shortcut(ED_SHORTCUT("level_editor/tool_clip", TTRC("Clip Tool"), Key::C, true));
	tool_buttons[TOOL_MIRROR]->set_shortcut(ED_SHORTCUT("level_editor/tool_mirror", TTRC("Mirror Tool"), Key::M, true));

	target_buttons[TARGET_VERTEX]->set_shortcut(ED_SHORTCUT("level_editor/tool_vertex", TTRC("Vertex Selection"), Key::KEY_1, true));
	target_buttons[TARGET_EDGE]->set_shortcut(ED_SHORTCUT("level_editor/tool_edge", TTRC("Edge Selection"), Key::KEY_2, true));
	target_buttons[TARGET_FACE]->set_shortcut(ED_SHORTCUT("level_editor/tool_face", TTRC("Face Selection"), Key::KEY_3, true));
	target_buttons[TARGET_MESH]->set_shortcut(ED_SHORTCUT("level_editor/target_mesh", TTRC("Mesh Selection"), Key::KEY_4, true));

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
	edge_popup->add_item(TTRC("Bevel"), 3);
	edge_popup->connect("id_pressed", callable_mp(this, &LevelEditorScreen::_edge_menu_selected));
	toolbar->add_child(edge_menu);

	face_menu = memnew(MenuButton);
	face_menu->set_text(TTRC("Face"));
	face_menu->set_flat(false);
	face_menu->set_theme_type_variation("FlatMenuButton");
	PopupMenu *face_popup = face_menu->get_popup();
	face_popup->add_item(TTRC("Extrude"), 0);
	face_popup->add_item(TTRC("Delete"), 2);
	face_popup->add_shortcut(ED_SHORTCUT("level_editor/subdivide_face", TTRC("Subdivide"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::D, true), 4);
	face_popup->add_separator();
	face_popup->add_shortcut(ED_SHORTCUT("level_editor/flip_faces", TTRC("Flip Faces"), Key::F, true), 3);
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

	// Initial enabled/disabled state of the element menu items (no map, no
	// selection: everything starts disabled).
	_update_menu_states();
}

void LevelEditorScreen::input(const Ref<InputEvent> &p_event) {
	// The _vp_input phase runs BEFORE gui/shortcut dispatch (and before the
	// scene dock's no-context Delete shortcut), so swallow our keys here
	// when the Level screen is visible.
	if (!is_visible_in_tree()) {
		return;
	}
	Ref<InputEventKey> k = p_event;
	if (!k.is_valid() || !k->is_pressed()) {
		return;
	}

	// Freelook (RMB-hold in the perspective viewport) flies with WASD/QE -
	// swallow the tool-switch keys (Q/W/E/R, echoes included) so flying
	// doesn't change tools.
	bool freelook = false;
	for (int i = 0; i < 4; i++) {
		if (viewports[i]->is_freelook_active()) {
			freelook = true;
			break;
		}
	}
	if (freelook) {
		switch (k->get_keycode()) {
			case Key::Q:
			case Key::W:
			case Key::E:
			case Key::R:
				get_viewport()->set_input_as_handled();
				return;
			default:
				break;
		}
	}
	if (k->is_echo()) {
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
			} else if (mirror_active) {
				_mirror_apply();
				handled = true;
			} else if (armed_action != ACTION_NONE) {
				_action_apply_armed();
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
			} else if (mirror_active) {
				_mirror_cancel();
				handled = true;
			} else if (armed_action != ACTION_NONE) {
				_action_cancel_armed();
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

bool LevelEditorScreen::_mesh_selection_has(LevelBrush *p_brush) const {
	return selected_brushes.has(p_brush);
}

void LevelEditorScreen::_mesh_selection_set(LevelBrush *p_brush) {
	selected_brushes.clear();
	selected_brushes.push_back(p_brush);
	if (selected_brush != p_brush) {
		selected_brush = p_brush;
		_edit_brush_node(p_brush);
	}
}

void LevelEditorScreen::_mesh_selection_toggle(LevelBrush *p_brush) {
	const int at = selected_brushes.find(p_brush);
	if (at >= 0) {
		selected_brushes.remove_at(at);
		// Keep the primary valid: fall back to the last remaining brush.
		if (selected_brush == p_brush) {
			selected_brush = selected_brushes.is_empty() ? nullptr : selected_brushes[selected_brushes.size() - 1];
			if (selected_brush) {
				_edit_brush_node(selected_brush);
			}
		}
	} else {
		selected_brushes.push_back(p_brush);
		selected_brush = p_brush;
		_edit_brush_node(p_brush);
	}
}

void LevelEditorScreen::set_selected_brush_from_editor(LevelBrush *p_brush) {
	if (!p_brush || p_brush == selected_brush) {
		return;
	}
	// Adopt the brush and, if it belongs to a map, adopt that map too.
	_mesh_selection_set(p_brush);
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
	if (ghost_active) {
		_ghost_cancel();
	}
	if (clip_active) {
		_clip_cancel();
	}
	if (mirror_active) {
		_mirror_cancel();
	}
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
	// The material panel falls back to the map's default material.
	if (dock) {
		dock->refresh_material();
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
	if (tool_buttons[TOOL_SELECT]) {
		tool_buttons[TOOL_SELECT]->set_button_icon(get_editor_theme_icon(SNAME("ToolSelect")));
		tool_buttons[TOOL_MOVE]->set_button_icon(get_editor_theme_icon(SNAME("ToolMove")));
		tool_buttons[TOOL_ROTATE]->set_button_icon(get_editor_theme_icon(SNAME("ToolRotate")));
		tool_buttons[TOOL_SCALE]->set_button_icon(get_editor_theme_icon(SNAME("ToolScale")));
		tool_buttons[TOOL_BLOCK]->set_button_icon(get_editor_theme_icon(SNAME("Brush")));
		tool_buttons[TOOL_CLIP]->set_button_icon(get_editor_theme_icon(SNAME("Clip")));
		tool_buttons[TOOL_MIRROR]->set_button_icon(get_editor_theme_icon(SNAME("Mirror")));
		target_buttons[TARGET_VERTEX]->set_button_icon(get_editor_theme_icon(SNAME("ControlAlignCenterLeft")));
		target_buttons[TARGET_EDGE]->set_button_icon(get_editor_theme_icon(SNAME("ControlAlignRightWide")));
		target_buttons[TARGET_FACE]->set_button_icon(get_editor_theme_icon(SNAME("ControlAlignFullRect")));
		target_buttons[TARGET_MESH]->set_button_icon(get_editor_theme_icon(SNAME("MeshTool")));
	}
}

void LevelEditorScreen::_tool_changed(int p_tool) {
	// Clicking the Clip button again while a clip is active cycles
	// keep-left / keep-right / keep-both (like Hammer's clip tool).
	if ((Tool)p_tool == TOOL_CLIP && tool == TOOL_CLIP && clip_active) {
		_clip_cycle_side();
		tool_buttons[TOOL_CLIP]->set_pressed(true);
	} else {
		_set_tool((Tool)p_tool);
	}
	// Don't leave keyboard focus on the toolbar buttons, so Enter/Esc/etc.
	// go to the viewports instead of re-triggering the button.
	for (int i = 0; i < TOOL_MAX; i++) {
		tool_buttons[i]->release_focus();
	}
}

void LevelEditorScreen::_target_changed(int p_target) {
	_set_target((SelectionTarget)p_target);
	for (int i = 0; i < TARGET_MAX; i++) {
		target_buttons[i]->release_focus();
	}
}

void LevelEditorScreen::_set_tool(Tool p_tool) {
	// Interrupt any in-progress drag first - the drag's undo action commits
	// against the OLD tool's snapshots, so ending it cleanly is required
	// before switching (a dropped extrude drag would be un-undoable).
	if (gizmo_dragging) {
		_gizmo_end_drag();
	}
	if (rotate_drag_axis >= 0) {
		_rotate_end_drag();
	}
	if (select_handle_drag != GHOST_NONE) {
		_select_handle_end_drag();
	}
	if (select_moving) {
		// End the whole-brush move like an LMB release: commit the position
		// undo, or a mid-move shortcut makes the move un-undoable.
		select_moving = false;
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		bool created = false;
		for (const KeyValue<LevelBrush *, Vector3> &E : select_move_original_positions) {
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
		}
		select_move_viewport = nullptr;
		select_move_original_positions.clear();
	}
	paint_select_active = false;
	paint_select_viewport = nullptr;

	const bool was_drawing = _is_drawing_tool();
	const bool to_drawing = (p_tool == TOOL_BLOCK || p_tool == TOOL_CLIP || p_tool == TOOL_MIRROR);

	// Entering a drawing tool suspends the transform tool + selection target;
	// leaving one restores them (Hammer remembers).
	Tool restore_tool = p_tool;
	if (to_drawing && !was_drawing) {
		last_transform_tool = tool;
		last_target = selection_target;
	} else if (!to_drawing && was_drawing) {
		restore_tool = last_transform_tool;
		selection_target = last_target;
	}

	tool = restore_tool;
	if (!to_drawing) {
		last_transform_tool = tool;
	}
	for (int i = 0; i < TOOL_MAX; i++) {
		tool_buttons[i]->set_pressed(i == (int)tool);
	}
	for (int i = 0; i < TARGET_MAX; i++) {
		target_buttons[i]->set_pressed(i == (int)selection_target);
	}
	// The drawing tools don't transform the selection - drop it.
	if (to_drawing) {
		_clear_selection();
	}
	if (tool != TOOL_BLOCK && ghost_active) {
		_ghost_cancel();
	}
	if (tool != TOOL_CLIP && clip_active) {
		_clip_cancel();
	}
	if (tool != TOOL_MIRROR && mirror_active) {
		_mirror_cancel();
	}
	_action_cancel_armed();
	if (dock) {
		dock->refresh();
	}
	_update_overlays();
}

void LevelEditorScreen::_set_target(SelectionTarget p_target) {
	if (p_target == selection_target) {
		return;
	}
	if (gizmo_dragging) {
		_gizmo_end_drag();
	}
	if (rotate_drag_axis >= 0) {
		_rotate_end_drag();
	}
	paint_select_active = false;
	paint_select_viewport = nullptr;

	selection_target = p_target;
	for (int i = 0; i < TARGET_MAX; i++) {
		target_buttons[i]->set_pressed(i == (int)selection_target);
	}
	// Each target owns its own selection type.
	_clear_selection();
	_action_cancel_armed();
	_update_overlays();
}

// --- Armed action plumbing (dock settings, Enter applies) ---

void LevelEditorScreen::_arm_action(ArmedAction p_action) {
	if (armed_action == p_action) {
		return; // Keep values across a re-click (acts as a toggle-off guard).
	}
	armed_action = p_action;
	if (dock) {
		dock->refresh();
	}
	_update_overlays();
}

void LevelEditorScreen::_action_cancel_armed() {
	if (armed_action == ACTION_NONE) {
		return;
	}
	armed_action = ACTION_NONE;
	armed_values.clear();
	tool_preview = ToolPreview();
	if (dock) {
		dock->refresh();
	}
	_update_overlays();
}

double LevelEditorScreen::get_armed_value(const StringName &p_id, double p_fallback) const {
	const double *v = armed_values.getptr(p_id);
	return v ? *v : p_fallback;
}

void LevelEditorScreen::set_armed_value(const StringName &p_id, double p_value) {
	armed_values[p_id] = p_value;
	_update_overlays(); // Live previews follow dock edits.
}

// --- Tool previews ---

void LevelEditorScreen::_bevel_preview_rebuild() {
	if (armed_action != ACTION_BEVEL_EDGES || selected_edges.size() != 1) {
		if (tool_preview.id == PREVIEW_BEVEL) {
			tool_preview = ToolPreview();
		}
		return;
	}

	const real_t width = get_armed_value(StringName("width"), grid_size);
	const int steps = (int)get_armed_value(StringName("steps"), 0.0);
	const real_t shape = get_armed_value(StringName("shape"), 0.5);
	const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E = *selected_edges.begin();

	// Cache key: everything the preview depends on (brush, selection,
	// armed values, AND current geometry - gizmo edits while armed must
	// invalidate).
	uint32_t h = hash_murmur3_one_64((uint64_t)E.key);
	h = hash_murmur3_one_32((uint64_t)E.value.size(), h);
	for (const LevelBrush::EdgeKey &e : E.value) {
		h = hash_murmur3_one_32((uint32_t)e.a, h);
		h = hash_murmur3_one_32((uint32_t)e.b, h);
	}
	const PackedVector3Array cur_verts = E.key->get_vertices_data();
	for (const Vector3 &v : cur_verts) {
		h = hash_murmur3_one_real(v.x, h);
		h = hash_murmur3_one_real(v.y, h);
		h = hash_murmur3_one_real(v.z, h);
	}
	h = hash_murmur3_one_real((double)width, h);
	h = hash_murmur3_one_32((uint32_t)steps, h);
	h = hash_murmur3_one_real((double)shape, h);
	if (tool_preview.id == PREVIEW_BEVEL && tool_preview.cache_hash == h) {
		return;
	}

	tool_preview = ToolPreview();

	Vector<LevelBrush::EdgeKey> edges;
	for (const LevelBrush::EdgeKey &e : E.value) {
		edges.push_back(e);
	}

	LevelBrush *working = E.key->duplicate_brush();
	if (working->bevel_edges_profiled(edges, width, steps, shape) > 0) {
		// Draw only edges the bevel CREATED (present in the result but not
		// in the original brush) - the original edges stay drawn by the
		// normal outline pass, and consumed edges (steps=0 centerline) must
		// not ghost back in here.
		HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> orig_edges = E.key->get_edges();
		HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> result_edges = working->get_edges();
		for (const LevelBrush::EdgeKey &e : result_edges) {
			const Vector3 va = working->get_vertex(e.a);
			const Vector3 vb = working->get_vertex(e.b);
			bool existed = false;
			for (const LevelBrush::EdgeKey &oe : orig_edges) {
				if (E.key->get_vertex(oe.a).is_equal_approx(va) && E.key->get_vertex(oe.b).is_equal_approx(vb)) {
					existed = true;
					break;
				}
			}
			if (!existed) {
				tool_preview.lines.push_back(va);
				tool_preview.lines.push_back(vb);
			}
		}
		tool_preview.id = PREVIEW_BEVEL;
		tool_preview.brush = E.key;
		tool_preview.cache_hash = h;
	}
	memdelete(working);
}

void LevelEditorScreen::_draw_tool_preview(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (tool_preview.id == PREVIEW_NONE || !tool_preview.brush || tool_preview.lines.is_empty()) {
		return;
	}
	Color col;
	switch (tool_preview.id) {
		case PREVIEW_BEVEL:
			col = LevelEditorColors::CLIP_LINE;
			break;
		default:
			col = LevelEditorColors::GHOST;
			break;
	}
	const Transform3D gt = tool_preview.brush->get_global_transform();
	for (uint32_t i = 0; i + 1 < tool_preview.lines.size(); i += 2) {
		Vector2 a, b;
		if (p_vp->project(gt.xform(tool_preview.lines[i]), a) && p_vp->project(gt.xform(tool_preview.lines[i + 1]), b)) {
			p_canvas->draw_line(a, b, col, 2.0);
		}
	}
}

void LevelEditorScreen::_action_apply_armed() {
	switch (armed_action) {
		case ACTION_BEVEL_EDGES:
			_action_bevel_edges();
			break;
		default:
			break;
	}
	_action_cancel_armed();
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
	grid_size_option->release_focus();
	grid_size = LevelEditorGrid::STEPS[CLAMP(p_index, 0, LevelEditorGrid::STEP_COUNT - 1)];
	grid_size_option->select(CLAMP(p_index, 0, LevelEditorGrid::STEP_COUNT - 1));
	_update_overlays();
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

	switch (selection_target) {
		case TARGET_MESH: {
			if (selected_brushes.is_empty()) {
				return;
			}
			// Delete all selected brush nodes in one undo action.
			LevelMap *map = current_map;
			Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
			Vector<LevelBrush *> doomed = selected_brushes;
			_clear_selection();

			EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
			undo_redo->create_action(TTR("Delete Brush"));
			for (LevelBrush *target : doomed) {
				undo_redo->add_do_method(map, "remove_child", target);
				undo_redo->add_undo_method(map, "add_child", target);
				undo_redo->add_undo_method(target, "set_owner", root);
				undo_redo->add_undo_reference(target);
			}
			undo_redo->add_do_method(map, "refresh");
			undo_redo->add_undo_method(map, "refresh");
			undo_redo->commit_action();
			_refresh_map();
		} break;
		case TARGET_FACE:
			_action_delete_faces();
			break;
		case TARGET_EDGE:
			_action_collapse_edges();
			break;
		case TARGET_VERTEX:
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

bool LevelEditorScreen::_material_drop_probe(Camera3D *p_camera, const Vector2 &p_screen, const Variant &p_data, LevelBrush *&r_brush, int &r_face) const {
	if (!LevelEditorMaterials::drag_data_is_material(p_data)) {
		r_brush = nullptr;
		r_face = -1;
		return false;
	}
	return _material_drop_pick(p_camera, p_screen, r_brush, r_face);
}

bool LevelEditorScreen::_material_drop_pick(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, int &r_face) const {
	r_brush = nullptr;
	r_face = -1;
	if (!current_map) {
		return false;
	}

	// Face target drops on the hovered face; everything else drops on the
	// whole brush under the cursor (face ray-pick doubles as the brush pick).
	Vector3 hit;
	if (!_pick_face(p_camera, p_screen, r_brush, r_face, hit)) {
		r_brush = nullptr;
		r_face = -1;
		return false;
	}
	if (selection_target != TARGET_FACE) {
		r_face = -1;
	}
	return true;
}

void LevelEditorScreen::_apply_material_drop(LevelBrush *p_brush, int p_face, const Variant &p_data) {
	Ref<Material> mat = LevelEditorMaterials::material_from_drag_data(p_data, texture_material_cache);
	ERR_FAIL_COND(mat.is_null());

	// Snapshot the serialized properties for undo, then apply live.
	PackedVector3Array old_verts = p_brush->get_vertices_data();
	Array old_faces = p_brush->get_faces_data();
	Array old_mats = p_brush->get_face_materials_data();

	if (p_face >= 0) {
		p_brush->set_face_material(p_face, mat);
	} else {
		p_brush->set_all_face_materials(mat);
	}

	_commit_brush_undo(p_face >= 0 ? TTR("Apply Face Material") : TTR("Apply Brush Material"), p_brush, old_verts, old_faces, old_mats, false);
	_update_overlays();
}

void LevelEditorScreen::_commit_brush_verts_undo(const String &p_action, const HashMap<LevelBrush *, PackedVector3Array> &p_old_verts) {
	// One undo action across every brush whose vertices actually changed vs
	// the snapshot. No-op (no action created) when nothing moved.
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	bool created = false;
	for (const KeyValue<LevelBrush *, PackedVector3Array> &E : p_old_verts) {
		LevelBrush *target = E.key;
		if (target->get_vertices_data() == E.value) {
			continue;
		}
		if (!created) {
			undo_redo->create_action(p_action);
			created = true;
		}
		_add_brush_undo_pair(undo_redo, target, E.value, target->get_faces_data(), target->get_face_materials_data());
	}
	if (created) {
		undo_redo->add_do_method(current_map, "refresh");
		undo_redo->add_undo_method(current_map, "refresh");
		undo_redo->commit_action(false);
	}
}

void LevelEditorScreen::_clear_selection() {
	selected_brush = nullptr;
	selected_brushes.clear();
	select_handle_hover = GHOST_NONE;
	select_handle_drag = GHOST_NONE;
	select_moving = false;
	select_move_viewport = nullptr;
	select_move_original_positions.clear();
	paint_select_active = false;
	paint_select_viewport = nullptr;
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

void LevelEditorScreen::_select_edge_loop(LevelBrush *p_brush, const LevelBrush::EdgeKey &p_edge) {
	// Replace the edge selection on this brush with the whole loop.
	Vector<LevelBrush::EdgeKey> loop = p_brush->get_edge_loop(p_edge);
	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> &set = _edge_set(p_brush);
	set.clear();
	for (const LevelBrush::EdgeKey &e : loop) {
		set.insert(e);
	}
	_update_overlays();
}

void LevelEditorScreen::_select_edge_chain(LevelBrush *p_brush, const LevelBrush::EdgeKey &p_edge) {
	// Replace the edge selection on this brush with the collinear chain.
	Vector<LevelBrush::EdgeKey> chain = p_brush->get_edge_chain(p_edge);
	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> &set = _edge_set(p_brush);
	set.clear();
	for (const LevelBrush::EdgeKey &e : chain) {
		set.insert(e);
	}
	_update_overlays();
}

void LevelEditorScreen::_update_overlays() {
	for (int i = 0; i < 4; i++) {
		viewports[i]->set_grid_mesh_size(grid_size);
		viewports[i]->queue_overlay_redraw();
	}
	_update_menu_states();
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
			real_t d = closest_point_on_segment_2d(sa, sb, p_screen).distance_to(p_screen);
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
	switch (selection_target) {
		case TARGET_MESH: {
			Vector3 hit;
			int f;
			_pick_face(cam, p_mouse, hover_brush, f, hit);
		} break;
		case TARGET_FACE: {
			Vector3 hit;
			_pick_face(cam, p_mouse, hover_brush, hover_face, hit);
		} break;
		case TARGET_VERTEX: {
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
		case TARGET_EDGE: {
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

	// Delete/brackets are handled by LevelEditorScreen::shortcut_input (so
	// the scene dock can't hijack them); skip them here to avoid double-
	// handling when this viewport has focus.
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() &&
			(key->get_keycode() == Key::KEY_DELETE || key->get_keycode() == Key::BRACKETLEFT || key->get_keycode() == Key::BRACKETRIGHT)) {
		return;
	}

	// Dispatch to the per-tool input handlers in priority order. Each handler
	// owns one tool's input and returns true when it consumed the event.
	// The order matters: select handles beat the move gizmo, the gizmo beats
	// the creation tools, and selection clicks come last.
	if (_select_handles_input(vp, p_camera, p_event)) {
		return;
	}
	if (_rotate_input(vp, p_camera, p_event)) {
		return;
	}
	if (_gizmo_input(vp, p_camera, p_event)) {
		return;
	}
	if (_brush_input(vp, p_camera, p_event)) {
		return;
	}
	if (_clip_input(vp, p_camera, p_event)) {
		return;
	}
	if (_mirror_input(vp, p_camera, p_event)) {
		return;
	}
	if (_selection_input(vp, p_camera, p_event)) {
		return;
	}
}

void LevelEditorScreen::_paint_select_at(Camera3D *p_camera, const Vector2 &p_screen) {
	// Add (never remove) the element under the cursor, per mode. Called on the
	// initial click and on every mouse-motion while the button is held, so
	// dragging paints a trail of selected elements.
	switch (selection_target) {
		case TARGET_VERTEX: {
			LevelBrush *brush = nullptr;
			int v;
			if (_pick_vertex(p_camera, p_screen, brush, v)) {
				_vertex_set(brush).insert(v);
			}
		} break;
		case TARGET_EDGE: {
			LevelBrush *brush = nullptr;
			LevelBrush::EdgeKey e;
			if (_pick_edge(p_camera, p_screen, brush, e)) {
				_edge_set(brush).insert(e);
			}
		} break;
		case TARGET_FACE: {
			Vector3 hit;
			LevelBrush *brush = nullptr;
			int f;
			if (_pick_face(p_camera, p_screen, brush, f, hit)) {
				_face_set(brush).insert(f);
			}
		} break;
		default:
			break;
	}
}

bool LevelEditorScreen::_select_ray_to_edit_plane(LevelEditorViewport *p_vp, const Vector2 &p_screen, Vector3 &r_hit) const {
	Vector3 pos = selected_brush ? selected_brush->get_global_position() : Vector3();
	return p_vp->ray_to_view_plane(p_screen, pos, r_hit);
}

// ---- Select-mode box handles ------------------------------------------------

AABB LevelEditorScreen::_get_brush_local_aabb(LevelBrush *p_brush) const {
	return LevelHelpers::aabb_from_points(p_brush->get_vertices_data());
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
	if (!selected_brush || selected_brushes.size() != 1) {
		return GHOST_NONE;
	}
	const int h = _pick_box_handle(p_vp, p_screen, _get_brush_local_aabb(selected_brush), selected_brush->get_global_transform());
	return _box_handle_usable(p_vp, h, -1) ? h : GHOST_NONE;
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
	// Resize handles are single-brush only (per-brush local AABB).
	if (tool != TOOL_SELECT || selection_target != TARGET_MESH || !selected_brush || selected_brushes.size() != 1) {
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
		if (!_box_handle_usable(p_vp, GHOST_FACE_XN + i, -1)) {
			continue;
		}
		Vector3 fc = gt.xform(aabb_face_center(bb, i));
		Vector2 sp;
		if (p_vp->project(fc, sp)) {
			bool hot = (select_handle_hover == GHOST_FACE_XN + i || select_handle_drag == GHOST_FACE_XN + i);
			Color hc = hot ? LevelEditorColors::SELECT_HANDLE_HOT : LevelEditorColors::SELECT_HANDLE;
			real_t hs_px = LevelEditorHandles::FACE_SIZE * EDSCALE;
			p_canvas->draw_rect(Rect2(sp - Vector2(hs_px, hs_px), Size2(hs_px * 2, hs_px * 2)), hc);
		}
	}

	// Corner handles.
	for (int i = 0; i < 8; i++) {
		Vector2 sp;
		if (p_vp->project(gt.xform(corners[i]), sp)) {
			bool hot = (select_handle_hover == GHOST_CORNER_0 + i || select_handle_drag == GHOST_CORNER_0 + i);
			Color hc = hot ? LevelEditorColors::SELECT_HANDLE_HOT : LevelEditorColors::SELECT_HANDLE;
			real_t hs_px = LevelEditorHandles::CORNER_SIZE * EDSCALE;
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
			for (int i = selected_brushes.size() - 1; i >= 0; i--) {
				if (!selected_brushes[i]->is_inside_tree()) {
					selected_brushes.remove_at(i);
					pruned = true;
				}
			}
			if (selected_brush && !selected_brush->is_inside_tree()) {
				selected_brush = selected_brushes.is_empty() ? nullptr : selected_brushes[selected_brushes.size() - 1];
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
		_draw_brush_outline(p_vp, p_canvas, b, _mesh_selection_has(b));
	}
	if (selection_target == TARGET_MESH && !_is_drawing_tool() && hover_brush && !_mesh_selection_has(hover_brush)) {
		// Hover highlight (thin white) - in every transform tool, not just
		// Select, so the user can see what a click would pick.
		Transform3D gt = hover_brush->get_global_transform();
		HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> open_edges = hover_brush->get_open_edges();
		HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = hover_brush->get_edges();
		for (const LevelBrush::EdgeKey &e : edges) {
			Vector2 a, b;
			if (p_vp->project_segment(gt.xform(hover_brush->get_vertex(e.a)), gt.xform(hover_brush->get_vertex(e.b)), a, b)) {
				if (open_edges.has(e)) {
					p_canvas->draw_dashed_line(a, b, LevelEditorColors::BRUSH_OUTLINE_HOVER, 1.0, 6.0 * EDSCALE);
				} else {
					p_canvas->draw_line(a, b, LevelEditorColors::BRUSH_OUTLINE_HOVER, 1.0);
				}
			}
		}
	}
	// Element targets: the hovered brush - and any brush with selected elements
	// of the current target - gets a light-blue outline and green vertices.
	if (_is_element_target()) {
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
		switch (selection_target) {
			case TARGET_VERTEX:
				add_selected_brushes(selected_vertices);
				break;
			case TARGET_FACE:
				add_selected_brushes(selected_faces);
				break;
			case TARGET_EDGE:
				add_selected_brushes(selected_edges);
				break;
			default:
				break;
		}

		const real_t vs = 3.0 * EDSCALE; // half-size, normal.
		const real_t vs_hot = 4.5 * EDSCALE; // half-size, hovered.
		for (LevelBrush *brush : highlight) {
			Transform3D gt = brush->get_global_transform();
			HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> open_edges = brush->get_open_edges();
			HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = brush->get_edges();
			for (const LevelBrush::EdgeKey &e : edges) {
				Vector2 a, b;
				if (!p_vp->project_segment(gt.xform(brush->get_vertex(e.a)), gt.xform(brush->get_vertex(e.b)), a, b)) {
					continue;
				}
				Color edge_col;
				real_t edge_width;
				if (selection_target == TARGET_EDGE) {
					// Edges stay light-blue; only the hovered edge turns green.
					bool hot = (brush == hover_brush && has_hover_edge && e == hover_edge);
					edge_col = hot ? LevelEditorColors::HOVER_ELEMENT : LevelEditorColors::HOVER_BRUSH_OUTLINE;
					edge_width = hot ? 2.5 : 1.5;
				} else {
					// Vertex/Face targets: light-blue outline only.
					edge_col = LevelEditorColors::HOVER_BRUSH_OUTLINE;
					edge_width = 1.5;
				}
				if (open_edges.has(e)) {
					p_canvas->draw_dashed_line(a, b, edge_col, edge_width, 6.0 * EDSCALE);
				} else {
					p_canvas->draw_line(a, b, edge_col, edge_width);
				}
			}
			if (selection_target == TARGET_FACE && brush == hover_brush && hover_face >= 0) {
				// Hovered face: green fill + outline.
				LocalVector<int> poly = brush->get_face(hover_face);
				if (poly.size() >= 3) {
					Vector<Vector3> world;
					for (int idx : poly) {
						world.push_back(gt.xform(brush->get_vertex(idx)));
					}
					PackedVector2Array pts;
					if (p_vp->project_polygon(world, pts)) {
						// The projected polygon can be degenerate (face viewed edge-on,
						// or a concave/self-intersecting outline after vertex edits) -
						// pre-flight the same triangulation the renderer does and skip
						// the fill if it fails (the outline still draws).
						if (!Geometry2D::triangulate_polygon(pts).is_empty()) {
							p_canvas->draw_colored_polygon(pts, LevelEditorColors::HOVER_FACE_FILL);
						}
						for (int i = 0; i < pts.size(); i++) {
							p_canvas->draw_line(pts[i], pts[(i + 1) % pts.size()], LevelEditorColors::HOVER_ELEMENT, 2.0);
						}
					}
				}
			}
			if (selection_target != TARGET_VERTEX) {
				continue; // Only the vertex target shows vertex markers.
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
	_draw_mirror(p_vp, p_canvas);
	_bevel_preview_rebuild();
	_draw_tool_preview(p_vp, p_canvas);
}

void LevelEditorScreen::_draw_material_drop(LevelEditorViewport *p_vp, Control *p_canvas) {
	// Material drop target highlight (dragging a material/texture over this
	// viewport): face mode highlights the hovered face, other targets the
	// whole brush outline - both with marching-ants dashes (drop_phase
	// scrolls the pattern along the path). Drawn on the viewport's dedicated
	// DropOverlay so the animation doesn't repaint the main overlay.
	if (!p_vp->drop_active || !p_vp->drop_brush) {
		return;
	}
	Transform3D gt = p_vp->drop_brush->get_global_transform();
	const real_t dash_len = 8.0 * EDSCALE;
	const real_t period = dash_len * 2.0;
	// Near-plane-clipped segments can unproject to endpoints hundreds of
	// thousands of pixels off-screen (asymptotic projection) - generating
	// dashes along the whole segment then costs millions of draw calls.
	// Clip to the overlay rect (with a small margin) before dashing.
	const Rect2 visible_rect(Vector2(-64, -64), p_canvas->get_size() + Vector2(128, 128));
	// Draws one dashed segment with the dash pattern offset by p_phase.
	// Returns the phase at the end of the visible span (for loop continuity).
	auto draw_marching_segment = [&](const Vector2 &p_a, const Vector2 &p_b, real_t p_phase, real_t p_width) -> real_t {
		const real_t len = (p_b - p_a).length();
		real_t t0, t1;
		if (!clip_segment_to_rect(p_a, p_b, visible_rect, t0, t1)) {
			return p_phase; // Fully outside the visible rect (or degenerate).
		}
		const Vector2 dir = (p_b - p_a) / len;
		// Walk the dash pattern along the visible span, starting p_phase
		// before it so the offset slides the dashes along the axis.
		real_t t = t0 - Math::fposmod(t0 + p_phase, period);
		while (t < t1) {
			const real_t dash_start = MAX(t, t0);
			const real_t dash_end = MIN(t + dash_len, t1);
			if (dash_end > dash_start) {
				p_canvas->draw_line(p_a + dir * dash_start, p_a + dir * dash_end, LevelEditorColors::SELECTED_ELEMENT, p_width);
			}
			t += period;
		}
		// Phase continuity for the next edge: advance by the FULL segment
		// length (not the clipped span) so off-screen portions still count.
		return Math::fposmod(p_phase - len, period);
	};
	if (p_vp->drop_face >= 0) {
		LocalVector<int> poly = p_vp->drop_brush->get_face(p_vp->drop_face);
		if (poly.size() >= 3) {
			Vector<Vector3> world;
			for (int idx : poly) {
				world.push_back(gt.xform(p_vp->drop_brush->get_vertex(idx)));
			}
			PackedVector2Array pts;
			if (p_vp->project_polygon(world, pts)) {
				// Skip the fill when near-plane clipping blew the polygon up to
				// astronomic screen coordinates (rasterizing it would scan
				// millions of pixels). The clipped dashes still draw.
				bool fill_ok = true;
				const real_t coord_limit = 32768.0;
				for (const Vector2 &p : pts) {
					if (Math::abs(p.x) > coord_limit || Math::abs(p.y) > coord_limit) {
						fill_ok = false;
						break;
					}
				}
				if (fill_ok && !Geometry2D::triangulate_polygon(pts).is_empty()) {
					p_canvas->draw_colored_polygon(pts, LevelEditorColors::SELECTED_FACE_FILL);
				}
				// The phase runs continuously around the loop so dashes turn
				// corners instead of resetting per edge. Orange + thicker than
				// the green hover highlight underneath so the drop target is
				// distinguishable from a plain hover.
				real_t phase = Math::fposmod((real_t)p_vp->drop_phase * EDSCALE, period);
				for (int i = 0; i < pts.size(); i++) {
					phase = draw_marching_segment(pts[i], pts[(i + 1) % pts.size()], phase, 3.0);
				}
			}
		}
	} else {
		const real_t phase = Math::fposmod((real_t)p_vp->drop_phase * EDSCALE, period);
		HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = p_vp->drop_brush->get_edges();
		for (const LevelBrush::EdgeKey &e : edges) {
			Vector2 a, b;
			if (p_vp->project_segment(gt.xform(p_vp->drop_brush->get_vertex(e.a)), gt.xform(p_vp->drop_brush->get_vertex(e.b)), a, b)) {
				draw_marching_segment(a, b, phase, 3.0);
			}
		}
	}
}

void LevelEditorScreen::_draw_brush_outline(LevelEditorViewport *p_vp, Control *p_canvas, LevelBrush *p_brush, bool p_selected) {
	Transform3D gt = p_brush->get_global_transform();

	// In element targets there is no whole-brush selection - draw all brushes
	// with the plain outline (hovered brush gets its own highlight).
	bool element_mode = _is_element_target();
	Color col = (p_selected && !element_mode) ? LevelEditorColors::BRUSH_OUTLINE_SELECTED : LevelEditorColors::BRUSH_OUTLINE;
	real_t width = (p_selected && !element_mode) ? 2.0 : 1.0;

	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> open_edges = p_brush->get_open_edges();
	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> edges = p_brush->get_edges();
	for (const LevelBrush::EdgeKey &e : edges) {
		Vector2 a, b;
		if (p_vp->project(gt.xform(p_brush->get_vertex(e.a)), a) && p_vp->project(gt.xform(p_brush->get_vertex(e.b)), b)) {
			if (open_edges.has(e)) {
				// Open edge (no adjacent face) - draw hashed.
				p_canvas->draw_dashed_line(a, b, col, width, 6.0 * EDSCALE);
			} else {
				p_canvas->draw_line(a, b, col, width);
			}
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
			Vector<Vector3> world;
			for (int idx : poly) {
				world.push_back(gt.xform(E.key->get_vertex(idx)));
			}
			PackedVector2Array pts;
			if (p_vp->project_polygon(world, pts)) {
				// Same degenerate-projection guard as the hover fill.
				if (!Geometry2D::triangulate_polygon(pts).is_empty()) {
					p_canvas->draw_colored_polygon(pts, face_col);
				}
				for (int i = 0; i < pts.size(); i++) {
					p_canvas->draw_line(pts[i], pts[(i + 1) % pts.size()], face_outline, 2.0);
				}
			}
		}
	}

	// Edges (selected: same orange as selected-face outlines). Hidden while
	// a bevel is armed - the cyan preview shows the bevel target instead.
	if (armed_action != ACTION_BEVEL_EDGES) {
		Color edge_col = LevelEditorColors::SELECTED_ELEMENT;
		for (const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
			Transform3D gt = E.key->get_global_transform();
			HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> open_edges = E.key->get_open_edges();
			for (const LevelBrush::EdgeKey &e : E.value) {
				Vector2 a, b;
				if (p_vp->project_segment(gt.xform(E.key->get_vertex(e.a)), gt.xform(E.key->get_vertex(e.b)), a, b)) {
					if (open_edges.has(e)) {
						p_canvas->draw_dashed_line(a, b, edge_col, 3.0, 6.0 * EDSCALE);
					} else {
						p_canvas->draw_line(a, b, edge_col, 3.0);
					}
				}
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
	screen->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	EditorNode::get_singleton()->get_editor_main_screen()->get_control()->add_child(screen);
	screen->hide();

	dock = memnew(LevelEditorDock);
	dock->set_screen(screen);
	screen->set_dock(dock);
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

LevelEditorPlugin::~LevelEditorPlugin() {
	// Pair the ctor's registrations, or a stale dock/screen survives plugin
	// teardown (shows up as a duplicate "Level" dock on the next session).
	if (dock) {
		remove_control_from_docks(dock);
		memdelete(dock);
		dock = nullptr;
	}
	if (screen) {
		memdelete(screen);
		screen = nullptr;
	}
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
