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
#include "scene/gui/button.h"
#include "scene/gui/menu_button.h"

void LevelEditorScreen::_extrude_pressed() {
	extrude_button->release_focus();
	if (!current_map || !selected_brush) {
		return;
	}

	// Snapshot serialized topology so undo restores the whole brush in one go.
	PackedVector3Array old_verts = selected_brush->get_vertices_data();
	Array old_faces = selected_brush->get_faces_data();

	LevelBrush *working = selected_brush->duplicate_brush();
	bool did = false;
	switch (mode) {
		case MODE_FACE: {
			if (selected_faces.is_empty()) {
				break;
			}
			// Extrude each selected face into new geometry (cap + side walls).
			// Process from highest index down so removals don't shift pending
			// indices.
			Vector<int> sorted;
			for (int f : selected_faces) {
				sorted.push_back(f);
			}
			sorted.sort();
			for (int i = sorted.size() - 1; i >= 0; i--) {
				working->extrude_face(sorted[i], extrude_amount);
			}
			did = true;
		} break;
		case MODE_EDGE: {
			if (selected_edges.is_empty()) {
				break;
			}
			// Move the edge's vertices along the average of their adjacent face
			// normals (a directional "pull" rather than new geometry).
			Vector<int> verts;
			for (const LevelBrush::EdgeKey &e : selected_edges) {
				verts.push_back(e.a);
				verts.push_back(e.b);
			}
			working->move_vertices(verts, Vector3(0, extrude_amount, 0));
			did = true;
		} break;
		case MODE_VERTEX: {
			if (selected_vertices.is_empty()) {
				break;
			}
			Vector<int> verts;
			for (int v : selected_vertices) {
				verts.push_back(v);
			}
			working->move_vertices(verts, Vector3(0, extrude_amount, 0));
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

	PackedVector3Array new_verts = working->get_vertices_data();
	Array new_faces = working->get_faces_data();

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Extrude Brush"));
	undo_redo->add_do_property(target, "vertices", new_verts);
	undo_redo->add_do_property(target, "faces", new_faces);
	undo_redo->add_do_method(map, "refresh");
	undo_redo->add_undo_property(target, "vertices", old_verts);
	undo_redo->add_undo_property(target, "faces", old_faces);
	undo_redo->add_undo_method(map, "refresh");
	undo_redo->commit_action();
	memdelete(working);

	_refresh_map();
}

void LevelEditorScreen::_apply_material_pressed() {
	apply_material_button->release_focus();
	if (!current_map || !selected_brush || current_material.is_null()) {
		return;
	}

	LevelBrush *target = selected_brush;
	LevelMap *map = current_map;

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Apply Brush Material"));

	if (mode == MODE_FACE && !selected_faces.is_empty()) {
		for (int f : selected_faces) {
			undo_redo->add_do_method(target, "set_face_material", f, current_material);
			undo_redo->add_undo_method(target, "set_face_material", f, target->get_face_material(f));
		}
	} else {
		for (int f = 0; f < target->get_face_count(); f++) {
			undo_redo->add_do_method(target, "set_face_material", f, current_material);
			undo_redo->add_undo_method(target, "set_face_material", f, target->get_face_material(f));
		}
	}
	undo_redo->add_do_method(map, "refresh");
	undo_redo->add_undo_method(map, "refresh");
	undo_redo->commit_action();

	_refresh_map();
}

void LevelEditorScreen::_flip_faces_pressed() {
	flip_faces_button->release_focus();
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

void LevelEditorScreen::_tools_menu_selected(int p_id) {
	tools_menu->release_focus();
	switch (p_id) {
		case 0:
			_join_edges();
			break;
	}
}

void LevelEditorScreen::_join_edges() {
	if (!current_map || !selected_brush || selected_edges.size() != 2) {
		return;
	}

	Vector<LevelBrush::EdgeKey> edges;
	for (const LevelBrush::EdgeKey &e : selected_edges) {
		edges.push_back(e);
	}

	LevelBrush *target = selected_brush;
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
	ERR_FAIL_NULL(root);

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
	undo_redo->commit_action();

	EditorInterface::get_singleton()->edit_node(baked);
}
