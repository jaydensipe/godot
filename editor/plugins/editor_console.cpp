/**************************************************************************/
/*  editor_console.cpp												      */
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

#include "editor_console.h"

#include "core/math/expression.h"
#include "core/string/string_builder.h"
#include "core/version_generated.gen.h"
#include "editor/editor_interface.h"

void EditorConsolePlugin::_submit_command(const String &p_expression) {
	if (p_expression.is_empty()) {
		print_line_rich("[color=yellow]>>> [/color]");
		return;
	}

	_initialize_default_aliases();

	String parsed = _parse_aliases(p_expression);

	print_line_rich("[color=yellow]>>> " + parsed + "[/color]");

	Expression expression;
	expression.parse(parsed);
	const Variant return_val = expression.execute(Array(), EditorInterface::get_singleton());

	set_text("");
	grab_focus();
	print_line(return_val);
}

// USE IS_PLAYING_SCENE AND SEND TO SCENE DEBUGGER

void EditorConsolePlugin::_verify_command(const String &p_text) {
	Expression expression;
	if (expression.parse(p_text) == 31) {
		set_right_icon(get_editor_theme_icon(SNAME("ImportCheck")));
	} else {
		set_right_icon(get_editor_theme_icon(SNAME("ImportFail")));
	}
}

void EditorConsolePlugin::_initialize_default_aliases() {
	aliases.set("spawn_light", vformat("get_edited_scene_root().add_child(OmniLight3D.new())"));
}

String EditorConsolePlugin::_parse_aliases(const String &p_expression) {
	Vector<String> tokens = p_expression.split(" ");
	StringBuilder expression;

	for (int i = 0; i < tokens.size(); i++) {
		String token = tokens[i];

		if (aliases.has(token)) {
			tokens.set(i, aliases[token]);
		}

		expression.append(tokens[i] + " ");
	}

	return expression.as_string();
}
EditorConsolePlugin::EditorConsolePlugin() {
}
