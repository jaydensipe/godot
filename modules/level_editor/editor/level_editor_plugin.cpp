/**************************************************************************/
/*  level_editor_plugin.cpp                                               */
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

// The main-screen editor plugin ("Level" tab): owns the LevelEditorScreen
// and the LevelEditorDock, and mirrors the editor's node selection into the
// screen. Split out of level_editor_screen.cpp.

#include "level_editor_plugin.h"

#include "dock/level_editor_dock.h"
#include "level_editor_screen.h"

#include "core/object/callable_mp.h"
#include "editor/editor_interface.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"

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
	if (screen->is_applying_editor_selection() || !screen->is_visible_in_tree()) {
		return; // Echo of our own _sync_editor_selection, or the tab is hidden.
	}
	// Mirror the editor's node selection into the level editor exactly
	// (replace vs. accumulate semantics match the scene tree).
	screen->apply_editor_selection(EditorInterface::get_singleton()->get_selection()->get_selected_nodes());
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
		// Leaving the Level tab: drop the mirrored selection so the scene tree
		// doesn't keep stale brush selections.
		screen->clear_selection();
		screen->hide();
	}
}
