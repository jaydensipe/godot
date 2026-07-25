/**************************************************************************/
/*  level_editor_dock.h                                                   */
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

#include "scene/gui/box_container.h"

class LevelEditorScreen;
class SpinBox;

// Right-side dock for the Level editor: settings panels for ARMED actions.
//
// An action with configurable options (bevel, ...) is "armed" by its toolbar
// menu item instead of applying immediately. The dock then builds a settings
// form from the action's LevelActionSetting descriptor list, edits write back
// into LevelEditorScreen's armed-action state, and Enter applies / Esc
// cancels (same pattern as the clip tool).
//
// To give a new action settings:
//   1. Add an ActionId entry in level_editor_screen.h.
//   2. Add its LevelActionSetting list to get_action_settings() (dock .cpp).
//   3. Implement its apply in the screen's _action_apply_armed().

// One configurable value of an armed action.
struct LevelActionSetting {
	StringName id; // Key written back to the screen's armed-action values.
	String label;
	double min = 0.0;
	double max = 1.0;
	double step = 0.01;
	double value = 0.0;
	bool rounded = false; // Whole-number SpinBox (e.g. segment counts).
};

class LevelEditorDock : public VBoxContainer {
	GDCLASS(LevelEditorDock, VBoxContainer);

	LevelEditorScreen *screen = nullptr;

	VBoxContainer *form = nullptr; // Rebuilt per armed action.

	void _setting_changed(double p_value, const StringName &p_id);
	void _cancel_pressed();

protected:
	static void _bind_methods();

public:
	void set_screen(LevelEditorScreen *p_screen) { screen = p_screen; }

	// Rebuilds the form for the armed action (or shows the idle hint).
	void refresh();

	LevelEditorDock();
};
