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
#include "editor/inspector/editor_resource_picker.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/separator.h"

void LevelEditorDock::_bind_methods() {
}

void LevelEditorDock::_material_changed(const Ref<Resource> &p_resource) {
	if (screen) {
		screen->_material_changed(p_resource);
	}
}

void LevelEditorDock::_apply_material_pressed() {
	apply_material_button->release_focus();
	if (screen) {
		screen->apply_material_from_dock();
	}
}

LevelEditorDock::LevelEditorDock() {
	set_name(TTRC("Level"));

	Label *mat_label = memnew(Label);
	mat_label->set_text(TTRC("Active Material:"));
	add_child(mat_label);

	material_picker = memnew(EditorResourcePicker);
	material_picker->set_base_type("Material");
	material_picker->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	material_picker->connect("resource_changed", callable_mp(this, &LevelEditorDock::_material_changed));
	add_child(material_picker);

	apply_material_button = memnew(Button);
	apply_material_button->set_text(TTRC("Apply to Face"));
	apply_material_button->set_tooltip_text(TTRC("Apply the active material to the selected faces (or the whole selected brush)."));
	apply_material_button->connect("pressed", callable_mp(this, &LevelEditorDock::_apply_material_pressed));
	add_child(apply_material_button);

	add_child(memnew(HSeparator));
}
