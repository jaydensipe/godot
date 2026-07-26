/**************************************************************************/
/*  level_editor_tools.cpp                                                */
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

// Tool actions for the level editor: extrude, material assignment, face
// flipping, bridging, and baking. These are LevelEditorScreen member
// functions, split out of level_editor_screen.cpp for organization.

#include "../level_editor_screen.h"

#include "editor/editor_interface.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/settings/editor_settings.h"
#include "scene/gui/button.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"

void LevelEditorScreen::_action_extrude_faces() {
	if (!current_map || selected_faces.is_empty()) {
		return;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Extrude Brush"));

	for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
		LevelBrush *target = E.key;
		PackedVector3Array old_verts = target->get_vertices_data();
		Array old_faces = target->get_faces_data();
		Array old_mats = target->get_face_materials_data();

		LevelBrush *working = target->duplicate_brush();
		// Extrude each selected face into new geometry (cap + side walls).
		// Process from highest index down so later indices stay valid.
		Vector<int> sorted;
		for (int f : E.value) {
			sorted.push_back(f);
		}
		sorted.sort();
		for (int i = sorted.size() - 1; i >= 0; i--) {
			working->extrude_face(sorted[i], extrude_amount);
		}

		undo_redo->add_do_property(target, "vertices", working->get_vertices_data());
		undo_redo->add_do_property(target, "faces", working->get_faces_data());
		undo_redo->add_do_property(target, "face_materials", working->get_face_materials_data());
		undo_redo->add_undo_property(target, "vertices", old_verts);
		undo_redo->add_undo_property(target, "faces", old_faces);
		undo_redo->add_undo_property(target, "face_materials", old_mats);
		memdelete(working);
	}

	undo_redo->add_do_method(current_map, "refresh");
	undo_redo->add_undo_method(current_map, "refresh");
	undo_redo->commit_action();

	_refresh_map();
}

void LevelEditorScreen::_action_extrude_edges() {
	if (!current_map || selected_edges.is_empty()) {
		return;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Extrude Brush"));

	// Move the edges' vertices along Y (a directional "pull").
	for (const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
		LevelBrush *target = E.key;
		PackedVector3Array old_verts = target->get_vertices_data();

		LevelBrush *working = target->duplicate_brush();
		Vector<int> verts;
		for (const LevelBrush::EdgeKey &e : E.value) {
			verts.push_back(e.a);
			verts.push_back(e.b);
		}
		working->move_vertices(verts, Vector3(0, extrude_amount, 0));

		undo_redo->add_do_property(target, "vertices", working->get_vertices_data());
		undo_redo->add_undo_property(target, "vertices", old_verts);
		memdelete(working);
	}

	undo_redo->add_do_method(current_map, "refresh");
	undo_redo->add_undo_method(current_map, "refresh");
	undo_redo->commit_action();

	_refresh_map();
}

void LevelEditorScreen::_action_extrude_vertices() {
	if (!current_map || selected_vertices.is_empty()) {
		return;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Extrude Brush"));

	for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_vertices) {
		LevelBrush *target = E.key;
		PackedVector3Array old_verts = target->get_vertices_data();

		LevelBrush *working = target->duplicate_brush();
		Vector<int> verts;
		for (int v : E.value) {
			verts.push_back(v);
		}
		working->move_vertices(verts, Vector3(0, extrude_amount, 0));

		undo_redo->add_do_property(target, "vertices", working->get_vertices_data());
		undo_redo->add_undo_property(target, "vertices", old_verts);
		memdelete(working);
	}

	undo_redo->add_do_method(current_map, "refresh");
	undo_redo->add_undo_method(current_map, "refresh");
	undo_redo->commit_action();

	_refresh_map();
}

void LevelEditorScreen::_action_apply_material() {
	if (!current_map || current_material.is_null()) {
		return;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Apply Brush Material"));
	bool did = false;

	if (!selected_faces.is_empty()) {
		// Only the selected faces across brushes.
		for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
			LevelBrush *target = E.key;
			for (int f : E.value) {
				undo_redo->add_do_method(target, "set_face_material", f, current_material);
				undo_redo->add_undo_method(target, "set_face_material", f, target->get_face_material(f));
			}
			did = true;
		}
	} else if (selected_brush) {
		// Whole selected brush.
		for (int f = 0; f < selected_brush->get_face_count(); f++) {
			undo_redo->add_do_method(selected_brush, "set_face_material", f, current_material);
			undo_redo->add_undo_method(selected_brush, "set_face_material", f, selected_brush->get_face_material(f));
		}
		did = true;
	}

	if (!did) {
		return;
	}

	undo_redo->add_do_method(current_map, "refresh");
	undo_redo->add_undo_method(current_map, "refresh");
	undo_redo->commit_action();

	_refresh_map();
}

void LevelEditorScreen::_action_flip_faces() {
	if (!current_map || !selected_brush) {
		return;
	}

	bool old_flipped = selected_brush->is_faces_flipped();
	bool new_flipped = !old_flipped;

	selected_brush->set_faces_flipped(new_flipped);

	LevelBrush *target = selected_brush;
	LevelMap *map = current_map;

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Flip Brush Faces"));
	undo_redo->add_do_property(target, "faces_flipped", new_flipped);
	undo_redo->add_do_method(map, "refresh");
	undo_redo->add_undo_property(target, "faces_flipped", old_flipped);
	undo_redo->add_undo_method(map, "refresh");
	undo_redo->commit_action(false);

	_refresh_map();
}

void LevelEditorScreen::_vertex_menu_selected(int p_id) {
	vertex_menu->release_focus();
	switch (p_id) {
		case 0: // Extrude
			_action_extrude_vertices();
			break;
		case 1: // Collapse (merge at neighbors' average, same as Delete in vertex mode)
			_action_collapse_vertices();
			break;
	}
}

void LevelEditorScreen::_action_bevel_edges() {
	if (!current_map || selected_edges.is_empty()) {
		return;
	}

	// Armed via the Edge menu: first click arms (dock shows Width/Steps/
	// Shape); Enter applies. Direct calls (apply path) run with the armed
	// values.
	if (armed_action != ACTION_BEVEL_EDGES) {
		_arm_action(ACTION_BEVEL_EDGES);
		return;
	}

	const real_t width = get_armed_value(StringName("width"), grid_size);
	const int steps = (int)get_armed_value(StringName("steps"), 0.0);
	const real_t shape = get_armed_value(StringName("shape"), 0.5);

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Bevel Edges"));
	bool did = false;
	for (const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
		LevelBrush *target = E.key;
		PackedVector3Array old_verts = target->get_vertices_data();
		Array old_faces = target->get_faces_data();
		Array old_mats = target->get_face_materials_data();

		Vector<LevelBrush::EdgeKey> edges;
		for (const LevelBrush::EdgeKey &e : E.value) {
			edges.push_back(e);
		}

		LevelBrush *working = target->duplicate_brush();
		if (working->bevel_edges_profiled(edges, width, steps, shape) > 0) {
			undo_redo->add_do_property(target, "vertices", working->get_vertices_data());
			undo_redo->add_do_property(target, "faces", working->get_faces_data());
			undo_redo->add_do_property(target, "face_materials", working->get_face_materials_data());
			undo_redo->add_undo_property(target, "vertices", old_verts);
			undo_redo->add_undo_property(target, "faces", old_faces);
			undo_redo->add_undo_property(target, "face_materials", old_mats);
			did = true;
		}
		memdelete(working);
	}
	if (!did) {
		return;
	}
	undo_redo->add_do_method(current_map, "refresh");
	undo_redo->add_undo_method(current_map, "refresh");
	undo_redo->commit_action();

	// compact_vertices() remaps vert indices, so the EdgeKey selection is
	// stale - clear it (same rule as subdivide/delete, GOTCHAS #14).
	selected_edges.clear();
	_refresh_map();
}

void LevelEditorScreen::_edge_menu_selected(int p_id) {
	edge_menu->release_focus();
	switch (p_id) {
		case 0: // Extrude
			_action_extrude_edges();
			break;
		case 1: // Bridge
			_action_bridge_edges();
			break;
		case 2: // Collapse
			_action_collapse_edges();
			break;
		case 3: // Bevel
			_action_bevel_edges();
			break;
	}
}

void LevelEditorScreen::_action_subdivide_faces() {
	if (!current_map || selected_faces.is_empty()) {
		return;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Subdivide Faces"));
	for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
		LevelBrush *target = E.key;
		PackedVector3Array old_verts = target->get_vertices_data();
		Array old_faces = target->get_faces_data();
		Array old_mats = target->get_face_materials_data();

		// Subdivide on a copy (highest index first so earlier indices stay
		// valid as faces are appended), then set the result as do-properties.
		LevelBrush *working = target->duplicate_brush();
		Vector<int> sorted;
		for (int f : E.value) {
			sorted.push_back(f);
		}
		sorted.sort();
		for (int i = sorted.size() - 1; i >= 0; i--) {
			working->subdivide_face(sorted[i]);
		}

		undo_redo->add_do_property(target, "vertices", working->get_vertices_data());
		undo_redo->add_do_property(target, "faces", working->get_faces_data());
		undo_redo->add_do_property(target, "face_materials", working->get_face_materials_data());
		undo_redo->add_undo_property(target, "vertices", old_verts);
		undo_redo->add_undo_property(target, "faces", old_faces);
		undo_redo->add_undo_property(target, "face_materials", old_mats);
		memdelete(working);
	}
	undo_redo->add_do_method(current_map, "refresh");
	undo_redo->add_undo_method(current_map, "refresh");
	undo_redo->commit_action();

	selected_faces.clear();
	_refresh_map();
}

void LevelEditorScreen::_face_menu_selected(int p_id) {
	face_menu->release_focus();
	switch (p_id) {
		case 0: // Extrude
			_action_extrude_faces();
			break;
		case 1: // Apply Material
			_action_apply_material();
			break;
		case 2: // Delete
			_action_delete_faces();
			break;
		case 4: // Subdivide
			_action_subdivide_faces();
			break;
		case 3: // Flip Faces (works on the selected brush, any mode)
			_action_flip_faces();
			break;
	}
}

void LevelEditorScreen::_tools_menu_selected(int p_id) {
	tools_menu->release_focus();
	switch (p_id) {
		case 0:
			_action_bridge_edges();
			break;
	}
}

void LevelEditorScreen::_view_grid_toggled(int p_id) {
	const int base = 4 * LevelEditorViewport::DISPLAY_MAX;
	PopupMenu *popup = view_menu->get_popup();
	int idx = popup->get_item_index(p_id);
	bool checked = !popup->is_item_checked(idx);
	popup->set_item_checked(idx, checked);

	if (p_id == base) {
		grid_2d_enabled = checked;
		EditorSettings::get_singleton()->set_project_metadata("level_editor", "grid_2d_enabled", checked);
	} else if (p_id == base + 1) {
		grid_3d_enabled = checked;
		EditorSettings::get_singleton()->set_project_metadata("level_editor", "grid_3d_enabled", checked);
		for (int i = 0; i < 4; i++) {
			viewports[i]->set_grid_3d_visible(checked);
		}
	}
	view_menu->release_focus();
	_update_overlays();
}

void LevelEditorScreen::_view_display_selected(int p_id) {
	view_menu->release_focus();
	const int vp = p_id / LevelEditorViewport::DISPLAY_MAX;
	const int disp_mode = p_id % LevelEditorViewport::DISPLAY_MAX;
	ERR_FAIL_INDEX(vp, 4);

	viewports[vp]->set_display_mode((LevelEditorViewport::DisplayMode)disp_mode);

	// Keep the radio state in sync within that viewport's submenu.
	PopupMenu *sub = view_submenus[vp];
	if (sub) {
		for (int i = 0; i < sub->get_item_count(); i++) {
			sub->set_item_checked(i, (sub->get_item_id(i) % LevelEditorViewport::DISPLAY_MAX) == disp_mode);
		}
	}

	// Persist all four modes for this project.
	Array modes;
	modes.resize(4);
	for (int i = 0; i < 4; i++) {
		modes[i] = (int)viewports[i]->get_display_mode();
	}
	EditorSettings::get_singleton()->set_project_metadata("level_editor", "viewport_display_modes", modes);
}

void LevelEditorScreen::_action_bridge_edges() {
	if (!current_map || selected_edges.size() != 1) {
		return; // Bridge needs exactly 2 edges on the SAME brush.
	}
	const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E = *selected_edges.begin();
	if (E.value.size() != 2) {
		return;
	}

	Vector<LevelBrush::EdgeKey> edges;
	for (const LevelBrush::EdgeKey &e : E.value) {
		edges.push_back(e);
	}

	LevelBrush *target = E.key;
	PackedVector3Array old_verts = target->get_vertices_data();
	Array old_faces = target->get_faces_data();
	Array old_mats = target->get_face_materials_data();

	// Bridge the two edges with a new quad face.
	int new_face = target->bridge_edges(edges[0], edges[1], current_material);
	if (new_face < 0) {
		return; // Shared vertex or same edge - can't bridge.
	}
	selected_edges.clear();

	_commit_brush_undo(TTR("Bridge Edges"), target, old_verts, old_faces, old_mats);
	_refresh_map();
}

void LevelEditorScreen::_bake_pressed() {
	bake_button->release_focus();
	if (!current_map) {
		return;
	}
	Node3D *baked = current_map->bake();
	ERR_FAIL_NULL(baked);

	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	if (!root) {
		memdelete(baked);
		return;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Bake Level"));
	undo_redo->add_do_method(root, "add_child", baked);
	undo_redo->add_do_method(baked, "set_owner", root);
	// Also give the baked children an owner so they persist.
	{
		List<Node *> stack;
		stack.push_back(baked);
		while (!stack.is_empty()) {
			Node *n = stack.front()->get();
			stack.pop_front();
			for (int i = 0; i < n->get_child_count(); i++) {
				undo_redo->add_do_method(n->get_child(i), "set_owner", root);
				stack.push_back(n->get_child(i));
			}
		}
	}
	undo_redo->add_undo_method(root, "remove_child", baked);
	undo_redo->add_do_reference(baked); // Keep the node alive across undo.
	undo_redo->commit_action();

	EditorInterface::get_singleton()->edit_node(baked);
}

void LevelEditorScreen::_action_delete_faces() {
	if (!current_map || selected_faces.is_empty()) {
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Delete Faces"));
	for (KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
		LevelBrush *target = E.key;
		PackedVector3Array old_verts = target->get_vertices_data();
		Array old_faces = target->get_faces_data();
		Array old_mats = target->get_face_materials_data();

		Vector<int> faces;
		for (int f : E.value) {
			faces.push_back(f);
		}
		target->delete_faces(faces);
		_add_brush_undo_pair(undo_redo, target, old_verts, old_faces, old_mats);
	}
	undo_redo->add_do_method(current_map, "refresh");
	undo_redo->add_undo_method(current_map, "refresh");
	undo_redo->commit_action(false);
	selected_faces.clear();
	_refresh_map();
}

void LevelEditorScreen::_action_collapse_edges() {
	if (!current_map || selected_edges.is_empty()) {
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Delete Edges"));
	for (KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
		LevelBrush *target = E.key;
		PackedVector3Array old_verts = target->get_vertices_data();
		Array old_faces = target->get_faces_data();
		Array old_mats = target->get_face_materials_data();

		// Collapse the edges' vertices (merges each edge to a point).
		Vector<int> verts;
		HashSet<int> seen;
		for (const LevelBrush::EdgeKey &e : E.value) {
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
		_add_brush_undo_pair(undo_redo, target, old_verts, old_faces, old_mats);
	}
	undo_redo->add_do_method(current_map, "refresh");
	undo_redo->add_undo_method(current_map, "refresh");
	undo_redo->commit_action(false);
	selected_edges.clear();
	_refresh_map();
}

void LevelEditorScreen::_action_collapse_vertices() {
	if (!current_map || selected_vertices.is_empty()) {
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Delete Vertices"));
	for (KeyValue<LevelBrush *, HashSet<int>> &E : selected_vertices) {
		LevelBrush *target = E.key;
		PackedVector3Array old_verts = target->get_vertices_data();
		Array old_faces = target->get_faces_data();
		Array old_mats = target->get_face_materials_data();

		Vector<int> verts;
		for (int v : E.value) {
			verts.push_back(v);
		}
		target->collapse_vertices(verts);
		_add_brush_undo_pair(undo_redo, target, old_verts, old_faces, old_mats);
	}
	undo_redo->add_do_method(current_map, "refresh");
	undo_redo->add_undo_method(current_map, "refresh");
	undo_redo->commit_action(false);
	selected_vertices.clear();
	_refresh_map();
}

// ---------------------------------------------------------------------------
// Input handlers (dispatched from LevelEditorScreen::forward_input).
// Each returns true when it consumed the event.
// ---------------------------------------------------------------------------

bool LevelEditorScreen::_select_handles_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	// Select-tool box handles take priority over the move gizmo (Mesh target
	// only - element targets transform via the gizmo).
	if (tool != TOOL_SELECT || selection_target != TARGET_MESH || !selected_brush) {
		return false;
	}
	Ref<InputEventMouseButton> mb = p_event;
	Ref<InputEventMouseMotion> mm = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			int h = _pick_select_handle(p_vp, mb->get_position());
			if (h != GHOST_NONE) {
				select_handle_drag = h;
				select_drag_viewport = p_vp;
				select_drag_original_aabb = _get_brush_local_aabb(selected_brush);
				select_drag_original_verts = selected_brush->get_vertices_data();
				return true;
			}
		} else if (select_handle_drag != GHOST_NONE) {
			_select_handle_end_drag();
			return true;
		}
	} else if (mm.is_valid()) {
		if (select_handle_drag != GHOST_NONE && select_drag_viewport == p_vp) {
			_select_handle_drag_to(p_vp, mm->get_position());
			return true;
		}
		int prev = select_handle_hover;
		select_handle_hover = _pick_select_handle(p_vp, mm->get_position());
		if (prev != select_handle_hover) {
			_update_overlays();
		}
	}
	return false;
}

bool LevelEditorScreen::_selection_input(LevelEditorViewport *p_vp, Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	// Transform tools only (the drawing tools consume their own input), and
	// never while the gizmo owns the drag.
	if (_is_drawing_tool() || gizmo_dragging) {
		return false;
	}
	Ref<InputEventMouseButton> mb = p_event;
	Ref<InputEventMouseMotion> mm = p_event;

	// Whole-brush drag in Select mode (moves ALL selected brushes).
	if (select_moving) {
		if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT && !mb->is_pressed()) {
			// Release: commit one undo action covering every moved brush.
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
			select_moving = false;
			select_move_viewport = nullptr;
			select_move_original_positions.clear();
			return true;
		}
		if (mm.is_valid() && select_move_viewport == p_vp && selected_brush) {
			Vector3 grab;
			if (_select_ray_to_edit_plane(p_vp, mm->get_position(), grab)) {
				const Vector3 new_world = _snap(grab - select_move_offset);
				Node3D *parent = Object::cast_to<Node3D>(selected_brush->get_parent());
				Vector3 new_primary = new_world;
				if (parent) {
					new_primary = parent->get_global_transform().affine_inverse().xform(new_world);
				}
				const Vector3 delta = new_primary - select_move_original_position;
				for (const KeyValue<LevelBrush *, Vector3> &E : select_move_original_positions) {
					if (E.key->is_inside_tree()) {
						E.key->set_position(E.value + delta);
					}
				}
				_refresh_map();
				_update_overlays();
			}
			return true;
		}
	}

	// Paint selection drag (vertex/edge/face modes).
	if (paint_select_active) {
		if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT && !mb->is_pressed()) {
			paint_select_active = false;
			paint_select_viewport = nullptr;
			return true;
		}
		if (mm.is_valid() && paint_select_viewport == p_vp) {
			_paint_select_at(p_camera, mm->get_position());
			_update_overlays();
			return true;
		}
	}

	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT && mb->is_pressed()) {
		bool add = mb->is_shift_pressed();
		switch (selection_target) {
			case TARGET_MESH: {
				// Click a brush to select it (Shift toggles it in/out of the
				// multi-selection); re-clicking the already-selected primary in
				// the Select tool starts a whole-brush drag of ALL selected
				// brushes (like the ghost move).
				Vector3 hit;
				LevelBrush *brush = nullptr;
				int f;
				if (_pick_face(p_camera, mb->get_position(), brush, f, hit)) {
					if (add) {
						_mesh_selection_toggle(brush);
					} else if (brush == selected_brush && tool == TOOL_MOVE && _mesh_selection_has(brush)) {
						// Re-clicking the selected primary in the Move tool begins a
						// whole-brush drag of ALL selected brushes (like the ghost).
						select_moving = true;
						select_move_viewport = p_vp;
						select_move_original_position = selected_brush->get_position();
						select_move_original_positions.clear();
						for (LevelBrush *b : selected_brushes) {
							select_move_original_positions[b] = b->get_position();
						}
						Vector3 grab;
						if (_select_ray_to_edit_plane(p_vp, mb->get_position(), grab)) {
							select_move_offset = grab - selected_brush->get_global_position();
						} else {
							select_move_offset = Vector3();
						}
					} else {
						_mesh_selection_set(brush);
					}
				} else if (!add) {
					_clear_selection();
				}
			} break;
			case TARGET_FACE: {
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
						if (!_mesh_selection_has(brush)) {
							selected_brushes.push_back(brush);
						}
						_edit_brush_node(brush);
					}
					paint_select_active = true;
					paint_select_viewport = p_vp;
				} else if (!add) {
					_clear_selection();
				}
			} break;
			case TARGET_EDGE: {
				LevelBrush *brush = nullptr;
				LevelBrush::EdgeKey e;
				if (_pick_edge(p_camera, mb->get_position(), brush, e)) {
					if (mb->is_double_click()) {
						if (mb->is_alt_pressed()) {
							// Alt+double-click: edge loop (Blender alt-click) - opposite
							// edges across each face, both directions.
							_select_edge_loop(brush, e);
						} else {
							// Double-click: collinear chain (straight run of segments,
							// e.g. consecutive pieces of a subdivided edge).
							_select_edge_chain(brush, e);
						}
					} else {
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
					}
					if (brush != selected_brush) {
						selected_brush = brush;
						if (!_mesh_selection_has(brush)) {
							selected_brushes.push_back(brush);
						}
						_edit_brush_node(brush);
					}
					paint_select_active = true;
					paint_select_viewport = p_vp;
				} else {
					// Missed any edge: still arm the paint drag so a drag started
					// over empty space adds edges as it crosses them.
					paint_select_active = true;
					paint_select_viewport = p_vp;
					if (!add) {
						_clear_selection();
					}
				}
			} break;
			case TARGET_VERTEX: {
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
						if (!_mesh_selection_has(brush)) {
							selected_brushes.push_back(brush);
						}
						_edit_brush_node(brush);
					}
					paint_select_active = true;
					paint_select_viewport = p_vp;
				} else if (!add) {
					_clear_selection();
				}
			} break;
			default:
				break;
		}
		_update_overlays();
		return true;
	}
	if (mm.is_valid()) {
		_update_hover(p_vp, mm->get_position());
		return true;
	}
	return false;
}
