/**************************************************************************/
/*  level_editor_screen.h                                                 */
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

#include "../level_map.h"

#include "editor/plugins/editor_plugin.h"
#include "scene/debugger/view_3d_controller.h"
#include "scene/gui/box_container.h"
#include "scene/gui/split_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/viewport.h"

class Button;
class Camera3D;
class DirectionalLight3D;
class EditorResourcePicker;
class SpinBox;
class WorldEnvironment;

class LevelEditorScreen;

// One 3D pane inside the quad view. Self-contained camera + input.
class LevelEditorViewport : public SubViewportContainer {
	GDCLASS(LevelEditorViewport, SubViewportContainer);

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

	ViewType view_type = VIEW_PERSPECTIVE;

	// Perspective navigation is handled by the same controller the 3D editor
	// viewport uses (orbit/pan/zoom + RMB freelook with editor settings).
	Ref<View3DController> view_controller;

	// Ortho views keep a simple pan/zoom model.
	Vector3 pivot;
	real_t distance = 20.0;
	bool panning = false;
	Vector2 last_mouse;

	void _update_camera_transform();
	void _draw_grid();

	void _process_freelook(double p_delta);

	// Overlay draw hook (called from Overlay::_notification).
	void _overlay_draw();

protected:
	void _notification(int p_what);
	virtual void gui_input(const Ref<InputEvent> &p_event) override;

public:
	LevelEditorScreen *screen = nullptr;

	void set_view_type(ViewType p_type);
	ViewType get_view_type() const { return view_type; }

	Camera3D *get_camera() const { return camera; }

	void get_ray(const Vector2 &p_screen, Vector3 &r_origin, Vector3 &r_dir) const;
	bool intersect_ortho_plane(const Vector2 &p_screen, Vector3 &r_hit) const;

	void queue_overlay_redraw();
	bool project(const Vector3 &p_world, Vector2 &r_screen) const;

	void focus_on(const AABB &p_aabb);

	LevelEditorViewport();
};

class LevelEditorScreen : public VBoxContainer {
	GDCLASS(LevelEditorScreen, VBoxContainer);

public:
	enum Mode {
		MODE_SELECT,
		MODE_ROTATE,
		MODE_SCALE,
		MODE_BLOCK,
		MODE_CLIP,
		MODE_VERTEX,
		MODE_EDGE,
		MODE_FACE,
		MODE_MAX
	};

private:
	EditorPlugin *plugin = nullptr;

	LevelEditorViewport *viewports[4] = {};
	HSplitContainer *top_split = nullptr;
	HSplitContainer *bottom_split = nullptr;
	VSplitContainer *rows_split = nullptr;

	HBoxContainer *toolbar = nullptr;
	Button *mode_buttons[MODE_MAX] = {};
	SpinBox *grid_size_spin = nullptr;
	SpinBox *extrude_spin = nullptr;
	Button *extrude_button = nullptr;
	Button *apply_material_button = nullptr;
	Button *flip_faces_button = nullptr;
	Button *bake_button = nullptr;
	EditorResourcePicker *material_picker = nullptr;

	real_t grid_size = 1.0;
	real_t extrude_amount = 1.0;

	Mode mode = MODE_SELECT;

	LevelMap *current_map = nullptr;

	// Block-drag state (stage 1: drawing the initial box).
	bool dragging = false;
	LevelEditorViewport *drag_viewport = nullptr;
	Vector3 drag_start;
	Vector3 drag_current;
	bool drag_active = false;

	// Ghost state (stage 2: box drawn, resize handles active until Enter/Esc).
	bool ghost_active = false;
	AABB ghost_aabb; // World space.

	// Ghost handle being dragged (or hovered): 6 face handles + 8 corners.
	enum GhostHandle {
		GHOST_NONE = -1,
		GHOST_FACE_XN = 0,
		GHOST_FACE_XP = 1,
		GHOST_FACE_YN = 2,
		GHOST_FACE_YP = 3,
		GHOST_FACE_ZN = 4,
		GHOST_FACE_ZP = 5,
		GHOST_CORNER_0 = 6, // 6..13: corner index = 6 + bitmask(x|y|z)
	};
	int ghost_handle_hover = GHOST_NONE;
	int ghost_handle_drag = GHOST_NONE;
	LevelEditorViewport *ghost_drag_viewport = nullptr;
	bool ghost_moving = false; // Dragging the whole ghost box.
	Vector3 ghost_move_offset; // Grab point minus ghost AABB position.

	bool _ghost_hit_test(LevelEditorViewport *p_vp, const Vector2 &p_screen) const;
	bool _ghost_ray_to_edit_plane(LevelEditorViewport *p_vp, const Vector2 &p_screen, Vector3 &r_hit) const;

	int _pick_ghost_handle(LevelEditorViewport *p_vp, const Vector2 &p_screen) const;
	void _ghost_handle_drag_to(LevelEditorViewport *p_vp, const Vector2 &p_mouse);
	void _ghost_commit();
	void _ghost_cancel();
	void _draw_ghost(LevelEditorViewport *p_vp, Control *p_canvas);
	void _draw_dim_labels(LevelEditorViewport *p_vp, Control *p_canvas, const AABB &p_aabb);

	// --- Clip tool state ---
	enum ClipSide {
		CLIP_KEEP_FRONT, // Keep the side the clip normal points to.
		CLIP_KEEP_BACK,
		CLIP_KEEP_BOTH,
	};

	LevelBrush *clip_brush = nullptr;
	bool clip_active = false;
	bool clip_drawing = false; // True while placing the 2nd point.
	int clip_drag_point = -1; // 0/1 while dragging a clip point; -1 = none.
	Vector3 clip_points[2]; // World space.
	Vector3 clip_view_dir; // Camera forward when the line was drawn (world).
	ClipSide clip_side = CLIP_KEEP_FRONT;
	LevelEditorViewport *clip_viewport = nullptr;

	void _clip_begin(LevelBrush *p_brush, const Vector3 &p_point, LevelEditorViewport *p_vp);
	void _clip_update_second(const Vector3 &p_point);
	int _pick_clip_point(LevelEditorViewport *p_vp, const Vector2 &p_screen) const;
	Plane _clip_plane() const; // In clip_brush local space.
	void _clip_apply();
	void _clip_cancel();
	void _clip_cycle_side();
	void _draw_clip(LevelEditorViewport *p_vp, Control *p_canvas);

	// Select-mode box handles: resize the selected brush's local AABB.
	// Shares the GhostHandle enum (0..5 faces, 6..13 corners).
	int select_handle_hover = GHOST_NONE;
	int select_handle_drag = GHOST_NONE;
	LevelEditorViewport *select_drag_viewport = nullptr;
	AABB select_drag_original_aabb; // Brush-local AABB at drag start.

	// Select-mode whole-brush drag (click selected brush, move like ghost).
	bool select_moving = false;
	Vector3 select_move_offset; // Grab point minus brush world position.
	LevelEditorViewport *select_move_viewport = nullptr;
	Vector3 select_move_original_position;

	bool _select_ray_to_edit_plane(LevelEditorViewport *p_vp, const Vector2 &p_screen, Vector3 &r_hit) const;

	// Rotate gizmo: 3 axis rings around the selection pivot.
	int rotate_hover_axis = -1; // 0/1/2 or -1
	int rotate_drag_axis = -1;
	real_t rotate_drag_start_angle = 0.0; // Angle of grab point around the axis.
	LevelEditorViewport *rotate_drag_viewport = nullptr;

	int _pick_rotate_ring(LevelEditorViewport *p_vp, const Vector2 &p_screen) const;
	real_t _rotate_screen_angle(LevelEditorViewport *p_vp, const Vector2 &p_screen, int p_axis) const;
	void _draw_rotate_gizmo(LevelEditorViewport *p_vp, Control *p_canvas);
	void _rotate_end_drag();

	AABB _get_brush_local_aabb(LevelBrush *p_brush) const;
	void _apply_brush_aabb(LevelBrush *p_brush, const AABB &p_aabb);
	int _pick_select_handle(LevelEditorViewport *p_vp, const Vector2 &p_screen) const;
	void _select_handle_drag_to(LevelEditorViewport *p_vp, const Vector2 &p_mouse);
	void _select_handle_end_drag();
	void _draw_select_handles(LevelEditorViewport *p_vp, Control *p_canvas);

	// Selection state (all indices refer to the selected brush's topology).
	LevelBrush *selected_brush = nullptr;
	HashSet<int> selected_faces;
	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> selected_edges;
	HashSet<int> selected_vertices;

	// Hover feedback.
	LevelBrush *hover_brush = nullptr;
	int hover_face = -1;
	LevelBrush::EdgeKey hover_edge;
	bool has_hover_edge = false;
	int hover_vertex = -1;
	bool has_hover_vertex = false;

	Ref<Material> current_material;

	// --- Manipulation gizmo ---
	enum GizmoPart {
		GIZMO_NONE = -1,
		GIZMO_X = 0,
		GIZMO_Y = 1,
		GIZMO_Z = 2,
		GIZMO_XY = 3,
		GIZMO_XZ = 4,
		GIZMO_YZ = 5,
	};

	GizmoPart gizmo_hover = GIZMO_NONE;
	GizmoPart gizmo_drag_part = GIZMO_NONE;
	bool gizmo_dragging = false;
	Vector3 gizmo_drag_start_origin; // World-space gizmo origin at drag start.
	Vector2 gizmo_drag_mouse_start;
	LevelEditorViewport *gizmo_drag_viewport = nullptr;
	Vector3 gizmo_drag_plane_normal;
	Vector3 gizmo_drag_plane_point;
	PackedVector3Array gizmo_drag_original_verts; // Brush vertices at drag start.
	Vector3 gizmo_drag_original_position; // Brush node position at drag start (Select mode).
	real_t gizmo_drag_original_rotation = 0.0; // Y rotation at drag start (Rotate mode).
	bool gizmo_drag_uniform_scale = false; // Scale drag started off-gizmo (mouse-X uniform).

	Vector3 _get_gizmo_origin() const; // World-space pivot of current selection.
	bool _has_selection() const;
	int _pick_gizmo(Camera3D *p_camera, const Vector2 &p_screen) const;
	void _gizmo_begin_drag(LevelEditorViewport *p_vp, const Vector2 &p_mouse);
	void _gizmo_drag_to(LevelEditorViewport *p_vp, const Vector2 &p_mouse);
	void _gizmo_end_drag();
	void _apply_gizmo_delta(const Vector3 &p_world_delta);
	void _apply_gizmo_rotate(int p_axis, real_t p_angle);
	void _apply_gizmo_scale_uniform(real_t p_factor);
	void _apply_gizmo_scale(const Vector3 &p_world_delta);
	void _draw_gizmo(LevelEditorViewport *p_vp, Control *p_canvas);

	LevelMap *_get_or_create_map();
	void _resolve_map();

	void _mode_changed(int p_mode);
	void _set_mode(Mode p_mode);
	void _update_mode_icons();

	void _extrude_pressed();
	void _apply_material_pressed();
	void _flip_faces_pressed();
	void _bake_pressed();
	void _material_changed(const Ref<Resource> &p_resource);
	void _grid_size_changed(double p_value);
	void _extrude_amount_changed(double p_value);

	Vector3 _snap(const Vector3 &p_v) const;
	real_t _snap(real_t p_v) const;

	bool _pick_face(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, int &r_face, Vector3 &r_hit) const;
	bool _pick_vertex(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, int &r_vertex) const;
	bool _pick_edge(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, LevelBrush::EdgeKey &r_edge) const;
	Vector<int> _get_gizmo_vertex_indices() const; // Vertex indices the gizmo moves (per mode).

	void _update_hover(LevelEditorViewport *p_vp, const Vector2 &p_mouse);
	void _clear_selection();
	void _update_overlays();
	void _refresh_map();

	void _compute_drag_aabb(Vector3 &r_mins, Vector3 &r_maxs) const;

	// Draw helpers used by the viewport overlay.
	friend class LevelEditorViewport;
	void _draw_viewport_overlay(LevelEditorViewport *p_vp, Control *p_canvas);
	void _draw_brush_outline(LevelEditorViewport *p_vp, Control *p_canvas, LevelBrush *p_brush, bool p_selected);
	void _draw_drag_feedback(LevelEditorViewport *p_vp, Control *p_canvas);
	void _draw_selection(LevelEditorViewport *p_vp, Control *p_canvas);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_plugin(EditorPlugin *p_plugin);
	LevelMap *get_map() const { return current_map; }
	real_t get_grid_size() const { return grid_size; }

	// Sync the level-editor brush selection with the editor's node selection.
	void set_selected_brush_from_editor(LevelBrush *p_brush);

	void make_visible(bool p_visible);
	void forward_input(Camera3D *p_camera, const Ref<InputEvent> &p_event);

	LevelEditorScreen();
};

// The main-screen plugin.
class LevelEditorPlugin : public EditorPlugin {
	GDCLASS(LevelEditorPlugin, EditorPlugin);

	LevelEditorScreen *screen = nullptr;

	void _editor_selection_changed();

protected:
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return TTRC("Level"); }
	virtual const Ref<Texture2D> get_plugin_icon() const override;
	virtual bool has_main_screen() const override { return true; }
	virtual void make_visible(bool p_visible) override;

	LevelEditorPlugin();
};
