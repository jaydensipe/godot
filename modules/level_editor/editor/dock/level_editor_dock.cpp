/**************************************************************************/
/*  level_editor_dock.cpp                                                 */
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

#include "level_editor_dock.h"

#include "../level_editor_screen.h"

#include "core/object/callable_mp.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"

// Setting descriptors per armed action. To configure a new action, add its
// list here and handle it in LevelEditorScreen::_action_apply_armed().
static Vector<LevelActionSetting> get_action_settings(LevelEditorScreen::ArmedAction p_action, real_t p_grid_size) {
	Vector<LevelActionSetting> settings;
	switch (p_action) {
		case LevelEditorScreen::ACTION_BEVEL_EDGES: {
			{
				LevelActionSetting s;
				s.id = StringName("width");
				s.label = TTRC("Width");
				s.min = 0.001;
				s.max = 1024.0;
				s.step = p_grid_size; // Grid-relative stepping; any value allowed.
				s.value = p_grid_size;
				settings.push_back(s);
			}
			{
				LevelActionSetting s;
				s.id = StringName("steps");
				s.label = TTRC("Steps");
				s.min = 0.0;
				s.max = 16.0;
				s.step = 1.0;
				s.value = 0.0;
				s.rounded = true;
				settings.push_back(s);
			}
			{
				LevelActionSetting s;
				s.id = StringName("shape");
				s.label = TTRC("Shape");
				s.min = 0.0;
				s.max = 1.0;
				s.step = 0.05;
				s.value = 0.5;
				settings.push_back(s);
			}
		} break;
		default:
			break;
	}
	return settings;
}

void LevelEditorDock::_bind_methods() {
}

void LevelEditorDock::_setting_changed(double p_value, const StringName &p_id) {
	if (screen) {
		screen->set_armed_value(p_id, p_value);
	}
}

void LevelEditorDock::refresh() {
	// Rebuild the form from scratch (armed actions are infrequent and the
	// forms are tiny - full rebuild keeps this dead simple).
	if (form) {
		form->queue_free();
		form = nullptr;
	}
	form = memnew(VBoxContainer);
	add_child(form);

	// Persistent tool settings, per active tool.
	if (screen && screen->get_tool() == LevelEditorScreen::TOOL_BLOCK) {
		Label *label = memnew(Label);
		label->set_text(TTRC("Brush Type"));
		form->add_child(label);

		OptionButton *type = memnew(OptionButton);
		type->add_item(TTRC("Block"), 0);
		type->add_item(TTRC("Quad"), 1);
		type->select(screen->get_brush_type());
		type->connect("item_selected", callable_mp(this, &LevelEditorDock::_brush_type_selected));
		form->add_child(type);

		form->add_child(memnew(HSeparator));
	}

	if (!screen || screen->get_armed_action() == LevelEditorScreen::ACTION_NONE) {
		Label *hint = memnew(Label);
		hint->set_text(TTRC("No active tool settings."));
		hint->add_theme_color_override("font_color", get_theme_color(SNAME("font_disabled_color"), SNAME("Editor")));
		form->add_child(hint);
		return;
	}

	const Vector<LevelActionSetting> settings = get_action_settings(screen->get_armed_action(), screen->get_grid_size());
	for (const LevelActionSetting &s : settings) {
		Label *label = memnew(Label);
		label->set_text(s.label);
		form->add_child(label);

		SpinBox *spin = memnew(SpinBox);
		spin->set_min(s.min);
		spin->set_max(s.max);
		spin->set_step(s.step);
		spin->set_allow_greater(true);
		spin->set_allow_lesser(s.min <= 0.0);
		spin->set_use_rounded_values(s.rounded);
		// Persisted value wins over the descriptor default (re-arm / refresh).
		spin->set_value(screen->get_armed_value(s.id, s.value));
		spin->connect("value_changed", callable_mp(this, &LevelEditorDock::_setting_changed).bind(s.id));
		form->add_child(spin);
		// Seed the screen state so apply works even if the user never edits.
		screen->set_armed_value(s.id, spin->get_value());
	}

	form->add_child(memnew(HSeparator));

	Label *hint = memnew(Label);
	hint->set_text(TTRC("Enter: Apply   Esc: Cancel"));
	hint->add_theme_color_override("font_color", get_theme_color(SNAME("font_disabled_color"), SNAME("Editor")));
	form->add_child(hint);

	Button *cancel = memnew(Button);
	cancel->set_text(TTRC("Cancel"));
	cancel->connect("pressed", callable_mp(this, &LevelEditorDock::_cancel_pressed));
	form->add_child(cancel);
}

void LevelEditorDock::_brush_type_selected(int p_index) {
	if (screen) {
		screen->set_brush_type(p_index);
	}
}

void LevelEditorDock::_cancel_pressed() {
	if (screen) {
		screen->cancel_armed_action();
	}
}

LevelEditorDock::LevelEditorDock() {
	set_name(TTRC("Level"));
	refresh();
}
