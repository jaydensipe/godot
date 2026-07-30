/**************************************************************************/
/*  level_editor_viewport.cpp                                             */
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

// One 3D pane of the level editor's quad view: scene SubViewport + camera +
// lights + world environment, the gizmo overlay SubViewport (own World3D,
// debug-draw-immune), the 2D/3D grids, the View Information / Frame Time
// HUDs, perspective freelook (View3DController) and ortho pan/zoom, ray and
// near-plane-safe projection helpers, and material drag-and-drop forwarding.
// Split out of level_editor_screen.cpp (the class used to live there).

#include "level_editor_viewport.h"

#include "../level_constants.h"
#include "level_editor_screen.h"
#include "materials/level_editor_materials.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/gui/box_container.h"
#include "scene/gui/label.h"
#include "scene/gui/panel_container.h"
#include "scene/main/viewport.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"
#include "servers/rendering/rendering_server.h"

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

	// Gizmo overlay pass: transparent SubViewport with its OWN World3D so the
	// viewport's debug draw mode (wireframe/overdraw/...) can't restyle the
	// gizmo. Stacked above the scene viewport; its camera is synced to the
	// scene camera per frame (sync_gizmo_camera).
	gizmo_subviewport = memnew(SubViewport);
	gizmo_subviewport->set_disable_input(true);
	gizmo_subviewport->set_handle_input_locally(false);
	gizmo_subviewport->set_transparent_background(true);
	gizmo_subviewport->set_use_own_world_3d(true);
	gizmo_subviewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
	gizmo_camera = memnew(Camera3D);
	gizmo_camera->set_current(true);
	gizmo_subviewport->add_child(gizmo_camera);
	add_child(gizmo_subviewport);

	overlay = memnew(Overlay);
	overlay->viewport = this;
	overlay->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	overlay->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	add_child(overlay);

	// The drop highlight + tool previews get their own overlay so per-frame
	// animation redraws stay cheap (the main overlay repaints every brush
	// outline/gizmo/preview).
	preview_overlay = memnew(PreviewOverlay);
	preview_overlay->viewport = this;
	preview_overlay->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	preview_overlay->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	add_child(preview_overlay);

	// View Information HUD (bottom-right; same content as the 3D editor's
	// "View Information": camera pos, viewport size, render stats).
	info_panel = memnew(PanelContainer);
	info_panel->set_anchor_and_offset(SIDE_LEFT, ANCHOR_END, -90 * EDSCALE);
	info_panel->set_anchor_and_offset(SIDE_TOP, ANCHOR_END, -90 * EDSCALE);
	info_panel->set_anchor_and_offset(SIDE_RIGHT, ANCHOR_END, -10 * EDSCALE);
	info_panel->set_anchor_and_offset(SIDE_BOTTOM, ANCHOR_END, -10 * EDSCALE);
	info_panel->set_h_grow_direction(GROW_DIRECTION_BEGIN);
	info_panel->set_v_grow_direction(GROW_DIRECTION_BEGIN);
	info_panel->set_mouse_filter(MOUSE_FILTER_IGNORE);
	add_child(info_panel);
	info_panel->hide();
	info_label = memnew(Label);
	info_panel->add_child(info_label);

	// View Frame Time HUD (top-right; CPU/GPU ms + FPS like the 3D editor).
	frame_time_gradient.instantiate();
	// Default ctor already has points at offsets 0 and 1; add the midpoint.
	// Colors are set on theme change (0=success, 1=warning, 2=error).
	frame_time_gradient->add_point(0.5, Color());
	frame_time_panel = memnew(PanelContainer);
	frame_time_panel->set_anchor_and_offset(SIDE_LEFT, ANCHOR_END, -90 * EDSCALE);
	frame_time_panel->set_anchor_and_offset(SIDE_TOP, ANCHOR_BEGIN, 10 * EDSCALE);
	// Same 10px right padding as the Information panel (anchoring both sides
	// keeps the panel pinned to the right edge as its width changes).
	frame_time_panel->set_anchor_and_offset(SIDE_RIGHT, ANCHOR_END, -10 * EDSCALE);
	frame_time_panel->set_h_grow_direction(GROW_DIRECTION_BEGIN);
	frame_time_panel->set_mouse_filter(MOUSE_FILTER_IGNORE);
	add_child(frame_time_panel);
	frame_time_panel->hide();
	VBoxContainer *frame_time_vbox = memnew(VBoxContainer);
	frame_time_panel->add_child(frame_time_vbox);
	// Individual labels so each can be colored by performance level.
	cpu_time_label = memnew(Label);
	frame_time_vbox->add_child(cpu_time_label);
	gpu_time_label = memnew(Label);
	frame_time_vbox->add_child(gpu_time_label);
	fps_label = memnew(Label);
	frame_time_vbox->add_child(fps_label);

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

void LevelEditorViewport::PreviewOverlay::_notification(int p_what) {
	if (p_what == NOTIFICATION_DRAW && viewport) {
		viewport->_preview_overlay_draw();
	}
}

void LevelEditorViewport::_overlay_draw() {
	_draw_grid();
	if (screen) {
		screen->_draw_viewport_overlay(this, overlay);
	}
}

void LevelEditorViewport::_preview_overlay_draw() {
	if (screen) {
		screen->_draw_material_drop(this, preview_overlay);
		// Animated tool previews (bevel marching-ants) live here too so their
		// per-frame phase steps don't repaint the expensive main overlay.
		screen->_draw_tool_preview(this, preview_overlay);
	}
}

void LevelEditorViewport::set_gizmo_root(Node3D *p_root) {
	if (gizmo_root == p_root) {
		return;
	}
	if (gizmo_root && gizmo_root->get_parent() == gizmo_subviewport) {
		gizmo_subviewport->remove_child(gizmo_root);
	}
	gizmo_root = p_root;
	if (gizmo_root) {
		gizmo_subviewport->add_child(gizmo_root);
	}
}

void LevelEditorViewport::sync_gizmo_camera() {
	if (!gizmo_camera || !camera) {
		return;
	}
	gizmo_camera->set_global_transform(camera->get_global_transform());
	gizmo_camera->set_projection(camera->get_projection());
	gizmo_camera->set_fov(camera->get_fov());
	gizmo_camera->set_size(camera->get_size());
	gizmo_camera->set_near(camera->get_near());
	gizmo_camera->set_far(camera->get_far());
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

void LevelEditorViewport::set_info_visible(bool p_visible) {
	show_info = p_visible;
	if (info_panel) {
		info_panel->set_visible(p_visible);
	}
}

void LevelEditorViewport::set_frame_time_visible(bool p_visible) {
	if (show_frame_time == p_visible) {
		return;
	}
	show_frame_time = p_visible;
	if (frame_time_panel) {
		frame_time_panel->set_visible(p_visible);
	}
	// Measuring render time has a small cost - only enable it while shown.
	RS::get_singleton()->viewport_set_measure_render_time(subviewport->get_viewport_rid(), p_visible);
	if (p_visible) {
		// Initialize to 120 FPS so the initial average is reasonable.
		for (int i = 0; i < FRAME_TIME_HISTORY; i++) {
			cpu_time_history[i] = 8.333333;
			gpu_time_history[i] = 8.333333;
		}
		cpu_time_history_index = 0;
		gpu_time_history_index = 0;
	}
}

void LevelEditorViewport::_update_info_hud() {
	if (!show_info || !info_panel || !camera) {
		return;
	}
	// Mirrors the 3D editor's "View Information" panel.
	const String viewport_size = vformat(U"%d \u00d7 %d", subviewport->get_size().x, subviewport->get_size().y);
	String text;
	text += vformat(TTR("X: %s"), rtos(camera->get_position().x).pad_decimals(1)) + "\n";
	text += vformat(TTR("Y: %s"), rtos(camera->get_position().y).pad_decimals(1)) + "\n";
	text += vformat(TTR("Z: %s"), rtos(camera->get_position().z).pad_decimals(1)) + "\n";
	text += "\n";
	text += vformat(TTR("Size: %s (%.1fMP)") + "\n", viewport_size, subviewport->get_size().x * subviewport->get_size().y * 0.000001);
	text += "\n";
	text += vformat(TTR("Objects: %d"), subviewport->get_render_info(Viewport::RENDER_INFO_TYPE_VISIBLE, Viewport::RENDER_INFO_OBJECTS_IN_FRAME)) + "\n";
	text += vformat(TTR("Primitives: %d"), subviewport->get_render_info(Viewport::RENDER_INFO_TYPE_VISIBLE, Viewport::RENDER_INFO_PRIMITIVES_IN_FRAME)) + "\n";
	text += vformat(TTR("Draw Calls: %d"), subviewport->get_render_info(Viewport::RENDER_INFO_TYPE_VISIBLE, Viewport::RENDER_INFO_DRAW_CALLS_IN_FRAME));
	info_label->set_text(text);
}

void LevelEditorViewport::_update_frame_time_hud() {
	if (!show_frame_time || !frame_time_panel) {
		return;
	}
	// Mirrors the 3D editor's "View Frame Time" panel: 20-frame rolling
	// average of measured CPU/GPU render time, colored by a green->red
	// gradient (midpoint 15 ms).
	cpu_time_history[cpu_time_history_index] = RS::get_singleton()->viewport_get_measured_render_time_cpu(subviewport->get_viewport_rid());
	cpu_time_history_index = (cpu_time_history_index + 1) % FRAME_TIME_HISTORY;
	double cpu_time = 0.0;
	for (int i = 0; i < FRAME_TIME_HISTORY; i++) {
		cpu_time += cpu_time_history[i];
	}
	cpu_time = MAX(0.01, cpu_time / FRAME_TIME_HISTORY);

	gpu_time_history[gpu_time_history_index] = RS::get_singleton()->viewport_get_measured_render_time_gpu(subviewport->get_viewport_rid());
	gpu_time_history_index = (gpu_time_history_index + 1) % FRAME_TIME_HISTORY;
	double gpu_time = 0.0;
	for (int i = 0; i < FRAME_TIME_HISTORY; i++) {
		gpu_time += gpu_time_history[i];
	}
	gpu_time = MAX(0.01, gpu_time / FRAME_TIME_HISTORY);

	cpu_time_label->set_text(vformat(TTR("CPU Time: %s ms"), rtos(cpu_time).pad_decimals(2)));
	cpu_time_label->add_theme_color_override(SceneStringName(font_color), frame_time_gradient->get_color_at_offset(Math::remap(cpu_time, 0.0, 30.0, 0.0, 1.0)));
	gpu_time_label->set_text(vformat(TTR("GPU Time: %s ms"), rtos(gpu_time).pad_decimals(2)));
	gpu_time_label->add_theme_color_override(SceneStringName(font_color), frame_time_gradient->get_color_at_offset(Math::remap(gpu_time, 0.0, 30.0, 0.0, 1.0)));
	fps_label->set_text(vformat(TTR("Editor FPS: %d"), (int)Engine::get_singleton()->get_frames_per_second()));
	fps_label->add_theme_color_override(SceneStringName(font_color), frame_time_gradient->get_color_at_offset(Math::remap(1000.0 / MAX(1.0, Engine::get_singleton()->get_frames_per_second()), 0.0, 30.0, 0.0, 1.0)));
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
		bool is_major = (i % LevelEditorGrid::GRID_MAJOR_INTERVAL) == 0;

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

// Ortho wheel zoom: distance limits (world units) and per-notch factors.
static const real_t ORTHO_ZOOM_MIN = 0.5;
static const real_t ORTHO_ZOOM_MAX = 2000.0;
static const real_t ORTHO_ZOOM_IN = 0.9;
static const real_t ORTHO_ZOOM_OUT = 1.1;

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
		// Sync the interpolated cursor so that ending freelook doesn't
		// revert to a stale position (set_freelook_enabled(false) does
		// cursor = cursor_interp). Use delta=0 to skip inertia smoothing
		// — we want cursor_interp == cursor exactly, otherwise the
		// inertia-lagged interpolant causes a jerk-back on RMB release.
		view_controller->update_camera(0);
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

void LevelEditorViewport::set_hover_cursor(DisplayServerEnums::CursorShape p_shape) {
	if (p_shape == hover_cursor) {
		return;
	}
	hover_cursor = p_shape;
	if (DisplayServer::get_singleton()->has_feature(DisplayServerEnums::FEATURE_CURSOR_SHAPE)) {
		DisplayServer::get_singleton()->cursor_set_shape(p_shape);
	}
}

void LevelEditorViewport::queue_overlay_redraw() {
	if (overlay) {
		overlay->update();
	}
}

void LevelEditorViewport::_queue_preview_redraw() {
	if (preview_overlay) {
		preview_overlay->queue_redraw();
	}
}

void LevelEditorViewport::clear_drop_state() {
	drop_payload_checked = false;
	drop_payload_ok = false;
	drop_last_probe = Vector2(Math::INF, Math::INF);
	if (drop_cursor_set) {
		drop_cursor_set = false;
		DisplayServer::get_singleton()->cursor_set_shape(DisplayServerEnums::CURSOR_ARROW);
	}
	if (drop_active) {
		drop_active = false;
		drop_brush = nullptr;
		drop_face = -1;
		drop_phase = 0.0;
		_queue_preview_redraw();
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
	if (drop_last_probe.x == Math::INF || drop_last_probe.distance_squared_to(p_point) > LevelEditorHandles::DROP_REPROBE_DIST_SQ) {
		drop_last_probe = p_point;
		LevelBrush *brush = nullptr;
		int face = -1;
		ok = screen->_material_drop_pick(camera, p_point, brush, face);
		if (ok != drop_active || brush != drop_brush || face != drop_face) {
			drop_active = ok;
			drop_brush = brush;
			drop_face = face;
			_queue_preview_redraw();
		}
	}

	// OS cursor feedback for the drag: the Viewport cursor update skips
	// SubViewportContainers, so we must set it ourselves (and restore the
	// arrow in clear_drop_state).
	if (DisplayServer::get_singleton()->has_feature(DisplayServerEnums::FEATURE_CURSOR_SHAPE)) {
		drop_cursor_set = true;
		DisplayServer::get_singleton()->cursor_set_shape(ok ? DisplayServerEnums::CURSOR_CAN_DROP : DisplayServerEnums::CURSOR_FORBIDDEN);
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
		case NOTIFICATION_MOUSE_ENTER: {
			// Viewports are SubViewportContainers, which the Viewport cursor
			// update deliberately SKIPS (scene/main/viewport.cpp: the OS cursor
			// is only set when NOT over one). So a resize cursor grabbed over an
			// adjacent SplitContainer dragger would otherwise leak and stay
			// stuck over this viewport. Re-assert the arrow on mouse enter; the
			// splitter re-sets its own cursor when hovered.
			hover_cursor = DisplayServerEnums::CURSOR_ARROW;
			if (DisplayServer::get_singleton()->has_feature(DisplayServerEnums::FEATURE_CURSOR_SHAPE)) {
				DisplayServer::get_singleton()->cursor_set_shape(DisplayServerEnums::CURSOR_ARROW);
			}
		} break;
		case NOTIFICATION_RESIZED: {
			if (overlay) {
				overlay->update();
			}
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			// Same styling as the 3D editor's information HUDs.
			Control *gui_base = EditorNode::get_singleton()->get_gui_base();
			const Ref<StyleBox> &sb = gui_base->get_theme_stylebox(SNAME("Information3dViewport"), EditorStringName(EditorStyles));
			info_panel->add_theme_style_override(SceneStringName(panel), sb);
			frame_time_panel->add_theme_style_override(SceneStringName(panel), sb);
			frame_time_gradient->set_color(0, get_theme_color(SNAME("success_color_dark_background"), EditorStringName(Editor)));
			frame_time_gradient->set_color(1, get_theme_color(SNAME("warning_color_dark_background"), EditorStringName(Editor)));
			frame_time_gradient->set_color(2, get_theme_color(SNAME("error_color_dark_background"), EditorStringName(Editor)));
		} break;
		case NOTIFICATION_PROCESS: {
			_process_freelook(get_process_delta_time());
			_update_grid_tracking();
			sync_gizmo_camera();
			_update_info_hud();
			_update_frame_time_hud();
			if (drop_active) {
				// Marching-ants drop highlight on the dedicated PreviewOverlay:
				// cheap enough to redraw at full speed (1px phase steps).
				const double new_phase = Math::fposmod(drop_phase + get_process_delta_time() * LevelEditorHandles::ANTS_SPEED, (double)LevelEditorHandles::ANTS_PERIOD);
				if (Math::floor(new_phase) != Math::floor(drop_phase)) {
					_queue_preview_redraw();
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
			bool is_major = (i % LevelEditorGrid::GRID_MAJOR_INTERVAL) == 0;
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
			bool is_major = (i % LevelEditorGrid::GRID_MAJOR_INTERVAL) == 0;
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
			const bool has_pivot_point = intersect_ortho_plane(mb->get_position(), before);
			distance = (mb->get_button_index() == MouseButton::WHEEL_UP) ? MAX(ORTHO_ZOOM_MIN, distance * ORTHO_ZOOM_IN) : MIN(ORTHO_ZOOM_MAX, distance * ORTHO_ZOOM_OUT);
			_update_camera_transform();
			if (has_pivot_point) {
				Vector3 after;
				if (intersect_ortho_plane(mb->get_position(), after)) {
					pivot += before - after;
				}
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
