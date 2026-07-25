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
#include "scene/gui/menu_button.h"
#include "scene/gui/split_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/viewport.h"
#include "scene/resources/immediate_mesh.h"

class Button;
class Camera3D;
class DirectionalLight3D;
class LevelEditorDock;
class OptionButton;
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

	// Overlay draw hook (called from Overlay::_notification).
	void _overlay_draw();

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

	void get_ray(const Vector2 &p_screen, Vector3 &r_origin, Vector3 &r_dir) const;
	bool intersect_ortho_plane(const Vector2 &p_screen, Vector3 &r_hit) const;
	// Ray -> this viewport's natural edit plane (through p_point). Works for
	// all view types (perspective uses the horizontal plane at p_point.y).
	bool ray_to_view_plane(const Vector2 &p_screen, const Vector3 &p_point, Vector3 &r_hit) const;

	void queue_overlay_redraw();
	bool project(const Vector3 &p_world, Vector2 &r_screen) const;

	void set_grid_mesh_size(real_t p_grid_size);
	void set_grid_3d_visible(bool p_visible);

	LevelEditorViewport();
};

class LevelEditorScreen : public VBoxContainer {
	GDCLASS(LevelEditorScreen, VBoxContainer);

public:
	// Two orthogonal state axes (Hammer 2 style):
	// - Tool: what the left mouse button does (move/rotate/scale gizmo, or a
	//   modal drawing tool). BLOCK/CLIP/MIRROR suspend the transform tool and
	//   restore it (and the selection target) afterwards.
	// - Target: what gets selected/transformed - the whole brush ("Mesh") or
	//   its vertices/edges/faces.
	enum Tool {
		TOOL_SELECT,
		TOOL_ROTATE,
		TOOL_SCALE,
		TOOL_BLOCK,
		TOOL_CLIP,
		TOOL_MIRROR,
		TOOL_MAX
	};

	enum SelectionTarget {
		TARGET_VERTEX,
		TARGET_EDGE,
		TARGET_FACE,
		TARGET_MESH,
		TARGET_MAX
	};

private:
	LevelEditorViewport *viewports[4] = {};
	SplitContainer *top_split = nullptr;
	SplitContainer *bottom_split = nullptr;
	SplitContainer *rows_split = nullptr;

	HBoxContainer *toolbar = nullptr;
	Control *no_map_panel = nullptr;
	Label *no_map_label = nullptr;
	Button *create_map_button = nullptr;
	Button *tool_buttons[TOOL_MAX] = {};
	Button *target_buttons[TARGET_MAX] = {};
	OptionButton *grid_size_option = nullptr;
	Button *bake_button = nullptr;
	MenuButton *tools_menu = nullptr;
	LevelEditorDock *dock = nullptr; // Owned by the plugin; set after ctor.
	MenuButton *vertex_menu = nullptr;
	MenuButton *edge_menu = nullptr;
	MenuButton *face_menu = nullptr;
	MenuButton *view_menu = nullptr;
	PopupMenu *view_submenus[4] = {};

	bool grid_2d_enabled = true;
	bool grid_3d_enabled = true;

	real_t grid_size = 1.0;
	real_t extrude_amount = 1.0;

	// Block tool setting (edited from the dock): what shape the drag commits.
	enum BrushType {
		BRUSH_BLOCK, // Solid box.
		BRUSH_QUAD, // Flat plane (one grid unit thick).
		BRUSH_TYPE_MAX
	};
	BrushType brush_type = BRUSH_BLOCK;

	Tool tool = TOOL_SELECT;
	Tool last_transform_tool = TOOL_SELECT; // Restored after BLOCK/CLIP/MIRROR.
	SelectionTarget selection_target = TARGET_MESH;
	SelectionTarget last_target = TARGET_MESH; // Restored after BLOCK/CLIP/MIRROR.

	// True when the active tool is one of the modal drawing tools.
	bool _is_drawing_tool() const { return tool == TOOL_BLOCK || tool == TOOL_CLIP || tool == TOOL_MIRROR; }
	// True when selection/transformation acts on brush elements.
	bool _is_element_target() const { return selection_target != TARGET_MESH; }

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
	mutable int ghost_flat_axis = -1; // Quad brush: the view axis the ghost is flat on (-1 = box). Written by the const _compute_drag_aabb.

	bool _ghost_hit_test(LevelEditorViewport *p_vp, const Vector2 &p_screen) const;
	bool _ghost_ray_to_edit_plane(LevelEditorViewport *p_vp, const Vector2 &p_screen, Vector3 &r_hit) const;

	int _pick_box_handle(LevelEditorViewport *p_vp, const Vector2 &p_screen, const AABB &p_aabb, const Transform3D &p_xform) const;
	int _pick_ghost_handle(LevelEditorViewport *p_vp, const Vector2 &p_screen) const;
	bool _ray_to_axis_plane(LevelEditorViewport *p_vp, const Vector2 &p_screen, const Vector3 &p_point, int p_axis, Vector3 &r_hit) const;
	void _ghost_handle_drag_to(LevelEditorViewport *p_vp, const Vector2 &p_mouse);
	void _ghost_commit();
	void _ghost_cancel();
	void _draw_ghost(LevelEditorViewport *p_vp, Control *p_canvas);
	void _draw_dim_labels(LevelEditorViewport *p_vp, Control *p_canvas, const AABB &p_aabb);
	// One predicate for all quad-ghost handle rules (thickness handles never
	// exist; edge-on views keep only the two endpoint handles + no corners).
	bool _ghost_handle_usable(LevelEditorViewport *p_vp, int p_handle) const;

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

	// Shared by clip + mirror: plane through two world points containing
	// the captured view direction, converted to p_brush local space.
	static Plane _two_point_plane(const Vector3 p_points[2], const Vector3 &p_view_dir, const LevelBrush *p_brush);

	// --- Mirror tool state (clip-style 2-point plane, previews the ---
	// mirrored copy; Enter creates it as a NEW brush node).
	LevelBrush *mirror_brush = nullptr;
	bool mirror_active = false;
	bool mirror_drawing = false;
	int mirror_drag_point = -1;
	Vector3 mirror_points[2]; // World space.
	Vector3 mirror_view_dir;
	LevelEditorViewport *mirror_viewport = nullptr;

	void _mirror_begin(LevelBrush *p_brush, const Vector3 &p_point, LevelEditorViewport *p_vp);
	Plane _mirror_plane() const; // In mirror_brush local space.
	void _mirror_apply();
	void _mirror_cancel();
	void _draw_mirror(LevelEditorViewport *p_vp, Control *p_canvas);

	// --- Armed action state (dock-configured actions, Enter applies) ---
public:
	// An action with dock settings is "armed" by its menu item (instead of
	// applying immediately); the dock edits armed_values; Enter runs
	// _action_apply_armed(), Esc _action_cancel_armed().
	enum ArmedAction {
		ACTION_NONE,
		ACTION_BEVEL_EDGES,
	};

private:
	ArmedAction armed_action = ACTION_NONE;
	HashMap<StringName, double> armed_values;

	void _arm_action(ArmedAction p_action);
	void _action_apply_armed();
	void _action_cancel_armed();

	// --- Tool previews (overlay line sets that follow tool state) ---
	// Generic container: line segments in a brush's local space, drawn in a
	// color. Each tool owns a PRODUCER that (re)builds the lines when its
	// relevant inputs change; the cache key is just a hash of those inputs.
	// To add a preview: one PreviewId, a color in _draw_tool_preview, and a
	// _*_preview_rebuild() that fills the struct and sets cache_hash.
	enum PreviewId {
		PREVIEW_NONE,
		PREVIEW_BEVEL,
	};
	struct ToolPreview {
		PreviewId id = PREVIEW_NONE;
		LevelBrush *brush = nullptr; // Brush whose transform the lines are in.
		LocalVector<Vector3> lines; // Brush-local segments (pairs of points).
		uint32_t cache_hash = 0; // 0 = invalid/needs rebuild.
	};
	ToolPreview tool_preview;

	void _bevel_preview_rebuild();
	void _draw_tool_preview(LevelEditorViewport *p_vp, Control *p_canvas);

public:
	// Dock hooks (LevelEditorDock reads/writes these).
	ArmedAction get_armed_action() const { return armed_action; }
	double get_armed_value(const StringName &p_id, double p_fallback) const;
	void set_armed_value(const StringName &p_id, double p_value);

	// Brush-tool dock settings.
	Tool get_tool() const { return tool; }
	int get_brush_type() const { return (int)brush_type; }
	void set_brush_type(int p_type) { brush_type = (BrushType)p_type; }
	void cancel_armed_action() { _action_cancel_armed(); }

private:
	// Select-mode box handles: resize the selected brush's local AABB.
	// Shares the GhostHandle enum (0..5 faces, 6..13 corners).
	int select_handle_hover = GHOST_NONE;
	int select_handle_drag = GHOST_NONE;
	LevelEditorViewport *select_drag_viewport = nullptr;
	AABB select_drag_original_aabb; // Brush-local AABB at drag start.
	PackedVector3Array select_drag_original_verts; // Brush vertices at drag start.

	// Select-mode whole-brush drag (click selected brush, move like ghost).
	bool select_moving = false;
	Vector3 select_move_offset; // Grab point minus brush world position.
	LevelEditorViewport *select_move_viewport = nullptr;
	Vector3 select_move_original_position;

	// Paint selection: holding LMB and dragging in an element mode selects
	// each element the cursor passes over (no toggle-off while painting).
	bool paint_select_active = false;
	LevelEditorViewport *paint_select_viewport = nullptr;
	void _paint_select_at(Camera3D *p_camera, const Vector2 &p_screen);

	bool _select_ray_to_edit_plane(LevelEditorViewport *p_vp, const Vector2 &p_screen, Vector3 &r_hit) const;

	// Rotate gizmo: 3 axis rings around the selection pivot.
	int rotate_hover_axis = -1; // 0/1/2 or -1
	int rotate_drag_axis = -1;
	real_t rotate_drag_start_angle = 0.0; // Angle of grab point around the axis.
	LevelEditorViewport *rotate_drag_viewport = nullptr;

	int _pick_rotate_ring(LevelEditorViewport *p_vp, const Vector2 &p_screen) const;
	real_t _rotate_screen_angle(LevelEditorViewport *p_vp, const Vector2 &p_screen, int p_axis) const;
	real_t _rotate_world_radius(LevelEditorViewport *p_vp, const Vector3 &p_origin, const Vector2 &p_center) const;
	int _rotate_allowed_axis(LevelEditorViewport::ViewType p_type) const;
	void _draw_rotate_gizmo(LevelEditorViewport *p_vp, Control *p_canvas);
	void _rotate_end_drag();

	AABB _get_brush_local_aabb(LevelBrush *p_brush) const;
	void _apply_brush_aabb(LevelBrush *p_brush, const AABB &p_aabb);
	int _pick_select_handle(LevelEditorViewport *p_vp, const Vector2 &p_screen) const;
	void _select_handle_drag_to(LevelEditorViewport *p_vp, const Vector2 &p_mouse);
	void _select_handle_end_drag();
	void _draw_select_handles(LevelEditorViewport *p_vp, Control *p_canvas);

	// Per-tool input handlers, called by forward_input in priority order;
	// each returns true when the event was consumed.
	bool _select_handles_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event);
	bool _rotate_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event);
	bool _gizmo_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event);
	bool _brush_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event);
	bool _clip_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event);
	bool _mirror_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event);
	bool _selection_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event);

	// Selection state. Whole-brush: selected_brush. Element modes select
	// across brushes: each set is keyed by the owning brush.
	LevelBrush *selected_brush = nullptr;
	HashMap<LevelBrush *, HashSet<int>> selected_faces;
	HashMap<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> selected_edges;
	HashMap<LevelBrush *, HashSet<int>> selected_vertices;

	// Hover feedback.
	LevelBrush *hover_brush = nullptr;
	int hover_face = -1;
	LevelBrush::EdgeKey hover_edge;
	bool has_hover_edge = false;
	int hover_vertex = -1;
	bool has_hover_vertex = false;

	Ref<Material> current_material;

	// Per-brush snapshot at gizmo drag start (multi-brush element moves).
	HashMap<LevelBrush *, PackedVector3Array> gizmo_drag_brush_verts;

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
	Vector3 gizmo_drag_grab_offset; // Closest point on the drag axis/plane to the grab click, minus the origin (kills first-frame jumps).
	Vector2 gizmo_drag_mouse_start;
	LevelEditorViewport *gizmo_drag_viewport = nullptr;
	Vector3 gizmo_drag_plane_normal;
	Vector3 gizmo_drag_plane_point;
	PackedVector3Array gizmo_drag_original_verts; // Brush vertices at drag start.
	Vector3 gizmo_drag_original_position; // Brush node position at drag start (Select mode).
	bool gizmo_drag_uniform_scale = false; // Scale drag started off-gizmo (mouse-X uniform).
	bool gizmo_extrude_drag = false; // Shift+drag in Face mode: extrude instead of move.
	// Face-mode extrude-drag snapshots (indices shift after extruding, so the
	// generic vertex snapshot can't restore them).
	HashMap<LevelBrush *, PackedVector3Array> gizmo_extrude_orig_verts;
	HashMap<LevelBrush *, Array> gizmo_extrude_orig_faces;
	HashMap<LevelBrush *, Array> gizmo_extrude_orig_mats;
	HashMap<LevelBrush *, Vector<int>> gizmo_extrude_cap_faces; // Cap face per brush, in extrude order.
	Vector<Vector3> gizmo_extrude_normals; // Brush-local cap normals, same order.
	HashMap<LevelBrush *, PackedVector3Array> gizmo_extrude_moved_verts; // Post-extrude topology at drag start.

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
	LevelMap *_find_map_in_scene() const;

	// Element-selection helpers (per-brush keyed sets).
	void _clear_element_selection();
	void _select_edge_loop(LevelBrush *p_brush, const LevelBrush::EdgeKey &p_edge);
	void _select_edge_chain(LevelBrush *p_brush, const LevelBrush::EdgeKey &p_edge);
	HashSet<int> &_face_set(LevelBrush *p_brush) { return selected_faces[p_brush]; }
	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> &_edge_set(LevelBrush *p_brush) { return selected_edges[p_brush]; }
	HashSet<int> &_vertex_set(LevelBrush *p_brush) { return selected_vertices[p_brush]; }

	// Erase map entries whose brush was deleted from the scene. Returns true
	// if anything was pruned.
	template <typename T>
	static bool _prune_dead_brushes(HashMap<LevelBrush *, T> &p_map) {
		List<LevelBrush *> dead;
		for (const KeyValue<LevelBrush *, T> &E : p_map) {
			if (!E.key->is_inside_tree()) {
				dead.push_back(E.key);
			}
		}
		for (LevelBrush *b : dead) {
			p_map.erase(b);
		}
		return !dead.is_empty();
	}

	void _tool_changed(int p_tool);
	void _target_changed(int p_target);
	void _set_tool(Tool p_tool);
	void _set_target(SelectionTarget p_target);
	void _update_mode_icons();
	void _edit_brush_node(LevelBrush *p_brush);

	void _action_extrude_faces();
	void _action_extrude_edges();
	void _action_extrude_vertices();
	void _action_apply_material();
	void _action_flip_faces();
	void _action_subdivide_faces();
	void _bake_pressed();
	void _tools_menu_selected(int p_id);
	void _vertex_menu_selected(int p_id);
	void _edge_menu_selected(int p_id);
	void _face_menu_selected(int p_id);
	void _view_display_selected(int p_id);
	void _view_grid_toggled(int p_id);
	void _action_bridge_edges();
	void _action_bevel_edges();
	void _grid_size_selected(int p_index);

	int _grid_step_index() const;

	Vector3 _snap(const Vector3 &p_v) const;
	real_t _snap(real_t p_v) const;

	bool _pick_face(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, int &r_face, Vector3 &r_hit) const;
	bool _pick_vertex(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, int &r_vertex) const;
	bool _pick_edge(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, LevelBrush::EdgeKey &r_edge) const;
	Vector<int> _get_gizmo_vertex_indices(LevelBrush *p_brush) const; // Vertex indices the gizmo moves (per mode).

	void _update_hover(LevelEditorViewport *p_vp, const Vector2 &p_mouse);
	void _clear_selection();
	void _delete_selection();
	void _action_delete_faces();
	void _action_collapse_edges();
	void _action_collapse_vertices();

	// Undo helper: records the brush's current vertices/faces/materials as the
	// "do" state against previously-snapshotted data, in one action.
	void _commit_brush_undo(const String &p_action, LevelBrush *p_brush, const PackedVector3Array &p_old_verts, const Array &p_old_faces, const Array &p_old_mats, bool p_execute = false);
	// Records one brush's current topology as the do-state against the given
	// snapshot, into an already-created undo action. Shared by tool actions
	// that span multiple brushes in one undo action.
	void _add_brush_undo_pair(EditorUndoRedoManager *p_undo_redo, LevelBrush *p_brush, const PackedVector3Array &p_old_verts, const Array &p_old_faces, const Array &p_old_mats);
	void _update_overlays();
	void _refresh_map();
	void _update_map_ui();
	void _create_map_pressed();
	void _update_warning_color();

	void _compute_drag_aabb(Vector3 &r_mins, Vector3 &r_maxs) const;

	// Draw helpers used by the viewport overlay.
	friend class LevelEditorViewport;
	friend class LevelEditorDock;
	void _draw_viewport_overlay(LevelEditorViewport *p_vp, Control *p_canvas);
	void _draw_brush_outline(LevelEditorViewport *p_vp, Control *p_canvas, LevelBrush *p_brush, bool p_selected);
	void _draw_drag_feedback(LevelEditorViewport *p_vp, Control *p_canvas);
	void _draw_selection(LevelEditorViewport *p_vp, Control *p_canvas);

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void shortcut_input(const Ref<InputEvent> &p_event) override;
	virtual void input(const Ref<InputEvent> &p_event) override;

public:
	LevelMap *get_map() const { return current_map; }
	real_t get_grid_size() const { return grid_size; }
	bool is_grid_2d_enabled() const { return grid_2d_enabled; }
	bool is_grid_3d_enabled() const { return grid_3d_enabled; }

	// Dock hooks (LevelEditorDock forwards its UI events here).
	void _material_changed(const Ref<Resource> &p_resource);
	void apply_material_from_dock() { _action_apply_material(); }

	// Sync the level-editor brush selection with the editor's node selection.
	void set_selected_brush_from_editor(LevelBrush *p_brush);

	void set_dock(LevelEditorDock *p_dock) { dock = p_dock; }

	void make_visible(bool p_visible);
	void forward_input(Camera3D *p_camera, const Ref<InputEvent> &p_event);

	// Called by the plugin when the edited scene changes (new/opened scene).
	void on_scene_changed();

	LevelEditorScreen();
};

// The main-screen plugin.
class LevelEditorPlugin : public EditorPlugin {
	GDCLASS(LevelEditorPlugin, EditorPlugin);

	LevelEditorScreen *screen = nullptr;
	LevelEditorDock *dock = nullptr;

	void _editor_selection_changed();

public:
	virtual String get_plugin_name() const override { return TTRC("Level"); }
	virtual const Ref<Texture2D> get_plugin_icon() const override;
	virtual bool has_main_screen() const override { return true; }
	virtual void make_visible(bool p_visible) override;
	virtual void edited_scene_changed() override;

	LevelEditorPlugin();
};
