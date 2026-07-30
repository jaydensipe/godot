/**************************************************************************/
/*  level_editor_viewport.h                                               */
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

#pragma once

#include "scene/debugger/view_3d_controller.h"
#include "scene/gui/subviewport_container.h"
#include "scene/resources/gradient.h"
#include "scene/resources/immediate_mesh.h"
#include "servers/display/display_server.h"

class Camera3D;
class DirectionalLight3D;
class Label;
class LevelBrush;
class LevelEditorScreen;
class MeshInstance3D;
class Node3D;
class PanelContainer;
class SubViewport;
class WorldEnvironment;

// One 3D pane inside the quad view. Self-contained camera + input.
class LevelEditorViewport : public SubViewportContainer {
	GDCLASS(LevelEditorViewport, SubViewportContainer);

	// The screen reads the drop state for the overlay highlight.
	friend class LevelEditorScreen;

public:
	enum ViewType {
		VIEW_PERSPECTIVE,
		VIEW_TOP,
		VIEW_FRONT,
		VIEW_SIDE,
	};

private:
	SubViewport *subviewport = nullptr;
	Camera3D *camera = nullptr;
	DirectionalLight3D *light = nullptr;
	WorldEnvironment *world_env = nullptr;

	// Gizmo-only overlay pass: a transparent SubViewport stacked above the
	// scene viewport, with its own World3D and a camera synced to the scene
	// camera each frame. Keeps the 3D gizmo immune to the viewport's debug
	// draw modes (wireframe/overdraw would restyle it) in ALL view types.
	SubViewport *gizmo_subviewport = nullptr;
	Camera3D *gizmo_camera = nullptr;
	Node3D *gizmo_root = nullptr; // Screen-owned 3D gizmo, one per viewport.

	// 3D brush outlines: one line mesh per brush in the overlay world (no
	// depth test, debug-draw-immune), rebuilt ONLY when the brush's geometry
	// changes - the 2D overlay re-projected every edge of every brush every
	// frame, which was the interactive-drag hotspot. Screen-owned.
	HashMap<LevelBrush *, MeshInstance3D *> outline_instances;

	class Overlay : public Control {
		GDCLASS(Overlay, Control);

	public:
		LevelEditorViewport *viewport = nullptr;

	protected:
		void _notification(int p_what);

	public:
		void update() { queue_redraw(); }
	};

	Overlay *overlay = nullptr;

	// Dedicated overlay for ANIMATED/cheap-frequently-redrawn content
	// (material-drop highlight, tool previews like the bevel marching-ants).
	// Anything that animates or redraws per-frame MUST draw here, NOT on the
	// main overlay - the main overlay repaints every brush outline/gizmo/
	// hover, so a per-frame redraw of it tanks FPS (GOTCHAS #33).
	class PreviewOverlay : public Control {
		GDCLASS(PreviewOverlay, Control);

	public:
		LevelEditorViewport *viewport = nullptr;

	protected:
		void _notification(int p_what);
	};

	PreviewOverlay *preview_overlay = nullptr;

	// View Information / View Frame Time HUDs (same as the 3D editor's View
	// menu). Toggled per viewport from the screen's View menu; updated in
	// NOTIFICATION_PROCESS.
	PanelContainer *info_panel = nullptr;
	Label *info_label = nullptr;
	PanelContainer *frame_time_panel = nullptr;
	Label *cpu_time_label = nullptr;
	Label *gpu_time_label = nullptr;
	Label *fps_label = nullptr;
	Ref<Gradient> frame_time_gradient;
	static constexpr int FRAME_TIME_HISTORY = 20;
	double cpu_time_history[FRAME_TIME_HISTORY] = {};
	double gpu_time_history[FRAME_TIME_HISTORY] = {};
	int cpu_time_history_index = 0;
	int gpu_time_history_index = 0;
	bool show_info = false;
	bool show_frame_time = false;
	void _update_info_hud();
	void _update_frame_time_hud();

	// Material drop from the FileSystem dock: while a droppable file is
	// dragged over this viewport, the drop target is highlighted with a
	// marching-ants dashed outline (drop_phase scrolls the dashes).
	bool drop_active = false;
	LevelBrush *drop_brush = nullptr;
	int drop_face = -1;
	double drop_phase = 0.0;

	// Drag throttling: the payload type is validated once per drag session
	// (gui_get_drag_data() is invariant for the whole drag), and the face
	// ray-pick only re-runs when the cursor has moved a few pixels.
	bool drop_payload_checked = false;
	bool drop_payload_ok = false;
	bool drop_cursor_set = false;
	Vector2 drop_last_probe = Vector2(Math::INF, Math::INF);

	ViewType view_type = VIEW_PERSPECTIVE;

	// OS cursor the tool code asked for this frame (resize cursors over box
	// handles). Viewports are SubViewportContainers, which the engine's
	// cursor update skips - so the tool sets/restores the cursor itself via
	// set_hover_cursor().
	DisplayServerEnums::CursorShape hover_cursor = DisplayServerEnums::CURSOR_ARROW;

	// Perspective navigation is handled by the same controller the 3D editor
	// viewport uses (orbit/pan/zoom + RMB freelook with editor settings).
	Ref<View3DController> view_controller;

	// Ortho views keep a simple pan/zoom model.
	Vector3 pivot;
	real_t distance = 20.0;
	bool panning = false;
	Vector2 last_mouse;

	// Perspective-view 3D grid (depth-tested against brush geometry). Recenters
	// on the camera as it moves (like the 3D editor's grid).
	MeshInstance3D *grid_mesh_instance = nullptr;
	Ref<ImmediateMesh> grid_mesh;
	real_t grid_mesh_size = -1.0; // Grid size the mesh was built for (-1 = none).
	Vector3 grid_mesh_center = Vector3(1e10, 1e10, 1e10); // Forces first build.

	void _update_camera_transform();
	void _draw_grid();
	void _rebuild_grid_mesh(real_t p_grid_size);
	void _update_grid_tracking();

	void _process_freelook(double p_delta);

	// Overlay draw hooks (called from the overlays' _notification).
	void _overlay_draw();
	void _preview_overlay_draw();

protected:
	void _notification(int p_what);
	virtual void gui_input(const Ref<InputEvent> &p_event) override;
	virtual void shortcut_input(const Ref<InputEvent> &p_event) override;

public:
	LevelEditorScreen *screen = nullptr;

	void set_view_type(ViewType p_type);
	ViewType get_view_type() const { return view_type; }

	// Render display mode (maps to Viewport::DebugDraw). Per viewport.
	enum DisplayMode {
		DISPLAY_NORMAL,
		DISPLAY_WIREFRAME,
		DISPLAY_OVERDRAW,
		DISPLAY_LIGHTING,
		DISPLAY_UNSHADED,
		DISPLAY_MAX
	};

private:
	DisplayMode display_mode = DISPLAY_NORMAL;

public:
	void set_display_mode(DisplayMode p_mode);
	DisplayMode get_display_mode() const { return display_mode; }

	Camera3D *get_camera() const { return camera; }
	SubViewport *get_subviewport() const { return subviewport; }
	SubViewport *get_gizmo_subviewport() const { return gizmo_subviewport; }
	void set_gizmo_root(Node3D *p_root);
	HashMap<LevelBrush *, MeshInstance3D *> &get_outline_instances() { return outline_instances; }
	// Sync the gizmo overlay camera to the scene camera (called per frame).
	void sync_gizmo_camera();
	bool is_freelook_active() const { return view_controller.is_valid() && view_controller->is_freelook_enabled(); }
	// Any camera navigation in progress (perspective freelook or ortho MMB
	// pan) - tool input/hover picks are skipped while the camera moves.
	bool is_navigating() const { return is_freelook_active() || panning; }

	void get_ray(const Vector2 &p_screen, Vector3 &r_origin, Vector3 &r_dir) const;
	bool intersect_ortho_plane(const Vector2 &p_screen, Vector3 &r_hit) const;
	// Ray -> this viewport's natural edit plane (through p_point). Works for
	// all view types (perspective uses the horizontal plane at p_point.y).
	bool ray_to_view_plane(const Vector2 &p_screen, const Vector3 &p_point, Vector3 &r_hit) const;

	void queue_overlay_redraw();
	void _queue_preview_redraw();
	void set_hover_cursor(DisplayServerEnums::CursorShape p_shape);
	void clear_drop_state();
	bool can_drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from);
	void drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from);
	bool project(const Vector3 &p_world, Vector2 &r_screen) const;
	// Near-plane-safe projection: clips the world segment against the camera
	// near plane in camera space before unprojecting, so overlay lines don't
	// vanish when ONE endpoint goes behind the camera (GOTCHAS #22).
	bool project_segment(const Vector3 &p_a, const Vector3 &p_b, Vector2 &r_a, Vector2 &r_b) const;
	// Same for a polygon loop: clips each edge against the near plane and
	// rebuilds the loop (a vert behind the camera inserts the two crossing
	// points instead of dropping the whole face).
	bool project_polygon(const Vector<Vector3> &p_world, PackedVector2Array &r_screen) const;

	void set_grid_mesh_size(real_t p_grid_size);
	void set_grid_3d_visible(bool p_visible);
	void set_info_visible(bool p_visible);
	void set_frame_time_visible(bool p_visible);

	LevelEditorViewport();
};
