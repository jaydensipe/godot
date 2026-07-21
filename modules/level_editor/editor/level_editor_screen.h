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

#include "level_map.h"

#include "editor/plugins/editor_plugin.h"
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

	// Orbit state.
	Vector3 pivot;
	real_t yaw = Math::deg_to_rad(-45.0);
	real_t pitch = Math::deg_to_rad(-30.0);
	real_t distance = 20.0;

	bool panning = false;
	bool orbiting = false;
	Vector2 last_mouse;

	void _update_camera_transform();
	void _draw_grid();

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
		MODE_BLOCK,
		MODE_VERTEX,
		MODE_EDGE,
		MODE_FACE,
		MODE_MAX,
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
	Button *bake_button = nullptr;
	EditorResourcePicker *material_picker = nullptr;

	real_t grid_size = 1.0;
	real_t extrude_amount = 1.0;

	Mode mode = MODE_SELECT;

	LevelMap *current_map = nullptr;

	// Block-drag state.
	bool dragging = false;
	LevelEditorViewport *drag_viewport = nullptr;
	Vector3 drag_start;
	Vector3 drag_current;
	bool drag_active = false;

	// Selection state.
	LevelBrush *selected_brush = nullptr;
	HashSet<int> selected_faces;
	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> selected_edges;
	Vector<Vector3> selected_vertices;

	// Hover feedback.
	LevelBrush *hover_brush = nullptr;
	int hover_face = -1;
	LevelBrush::EdgeKey hover_edge;
	bool has_hover_edge = false;
	Vector3 hover_vertex;
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
	Vector<Plane> gizmo_drag_original_planes; // Brush planes at drag start.
	Vector3 gizmo_drag_original_position; // Brush node position at drag start (Select mode).

	Vector3 _get_gizmo_origin() const; // World-space pivot of current selection.
	bool _has_selection() const;
	int _pick_gizmo(Camera3D *p_camera, const Vector2 &p_screen) const;
	void _gizmo_begin_drag(LevelEditorViewport *p_vp, const Vector2 &p_mouse);
	void _gizmo_drag_to(LevelEditorViewport *p_vp, const Vector2 &p_mouse);
	void _gizmo_end_drag();
	void _apply_gizmo_delta(const Vector3 &p_world_delta);
	void _draw_gizmo(LevelEditorViewport *p_vp, Control *p_canvas);

	LevelMap *_get_or_create_map();
	void _resolve_map();

	void _mode_changed(int p_mode);
	void _set_mode(Mode p_mode);

	void _extrude_pressed();
	void _apply_material_pressed();
	void _bake_pressed();
	void _material_changed(const Ref<Resource> &p_resource);
	void _grid_size_changed(double p_value);
	void _extrude_amount_changed(double p_value);

	Vector3 _snap(const Vector3 &p_v) const;
	real_t _snap(real_t p_v) const;

	bool _pick_face(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, int &r_face, Vector3 &r_hit) const;
	bool _pick_vertex(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, Vector3 &r_vertex) const;
	bool _pick_edge(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, LevelBrush::EdgeKey &r_edge) const;

	void _update_hover(LevelEditorViewport *p_vp, const Vector2 &p_mouse);
	void _clear_selection();
	void _update_overlays();
	void _refresh_map();

	void _commit_drag();

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
