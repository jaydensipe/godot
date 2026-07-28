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

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_node.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/editor_quick_open_dialog.h"
#include "editor/inspector/editor_resource_preview.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/texture_rect.h"

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

void LevelEditorDock::_notification(int p_what) {
	if (p_what == NOTIFICATION_THEME_CHANGED && material_panel) {
		// Framed content-box style (same as the editor's Tree panels).
		material_panel->set_theme_type_variation("PanelContainerButtonGroup");
		material_save->set_button_icon(get_editor_theme_icon(SNAME("Save")));
	}
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
	ScrollContainer *scroll = Object::cast_to<ScrollContainer>(get_child(0));
	ERR_FAIL_NULL(scroll);
	form = memnew(VBoxContainer);
	form->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->add_child(form);

	// Persistent tool settings, per active tool.
	if (screen && screen->get_tool() == LevelEditorScreen::TOOL_BLOCK) {
		Label *label = memnew(Label);
		label->set_text(TTRC("Brush Type"));
		form->add_child(label);

		OptionButton *type = memnew(OptionButton);
		type->add_item(TTRC("Block"), 0);
		type->add_item(TTRC("Quad"), 1);
		type->add_item(TTRC("Sphere"), 2);
		type->select(screen->get_brush_type());
		type->connect("item_selected", callable_mp(this, &LevelEditorDock::_brush_type_selected));
		form->add_child(type);

		// Sphere sides (only meaningful for the Sphere type).
		if (screen->get_brush_type() == 2) { // BRUSH_SPHERE
			Label *sides_label = memnew(Label);
			sides_label->set_text(TTRC("Sides"));
			form->add_child(sides_label);

			SpinBox *sides = memnew(SpinBox);
			sides->set_min(4);
			sides->set_max(64);
			sides->set_step(1);
			sides->set_use_rounded_values(true);
			sides->set_value(screen->get_brush_sphere_sides());
			sides->connect("value_changed", callable_mp(this, &LevelEditorDock::_sphere_sides_changed));
			form->add_child(sides);
		}

		form->add_child(memnew(HSeparator));
	}

	if (!screen || screen->get_armed_action() == LevelEditorScreen::ACTION_NONE) {
		Label *hint = memnew(Label);
		hint->set_text(TTRC("No active tool settings."));
		hint->set_v_size_flags(SIZE_EXPAND_FILL);
		hint->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		hint->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
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

// The material shown in the sticky panel: the screen's active material, or
// the LevelMap's default material when no override is set (r_is_override is
// true only in the first case).
static Ref<Material> _get_displayed_material(LevelEditorScreen *p_screen, bool &r_is_override) {
	r_is_override = false;
	if (!p_screen) {
		return Ref<Material>();
	}
	Ref<Material> mat = p_screen->get_active_material();
	if (mat.is_valid()) {
		r_is_override = true;
		return mat;
	}
	LevelMap *map = p_screen->get_map();
	if (map) {
		mat = map->get_default_material();
	}
	return mat;
}

void LevelEditorDock::refresh_material() {
	ERR_FAIL_NULL(material_preview);
	ERR_FAIL_NULL(material_name);

	bool is_override = false;
	Ref<Material> mat = _get_displayed_material(screen, is_override);
	if (material_save) {
		material_save->set_disabled(mat.is_null());
	}
	if (mat.is_null()) {
		material_name->set_text(TTRC("(No Material)"));
		material_preview->set_texture(Ref<Texture2D>());
		material_preview->set_tooltip_text("");
		return;
	}

	if (is_override) {
		String name = mat->get_name();
		if (name.is_empty() && !mat->get_path().is_empty()) {
			name = mat->get_path().get_file();
		}
		if (name.is_empty()) {
			name = TTRC("(Unnamed Material)");
		}
		material_name->set_text(name);
	} else {
		// Showing the map's default material (no active override).
		material_name->set_text(TTRC("(Map Default)"));
	}
	material_preview->set_tooltip_text(mat->get_path());

	// Rendered preview via the editor's resource preview queue (renders
	// materials on a lit sphere off-thread; cached on later calls). The
	// instance id round-trips so a stale preview can't clobber a newer pick.
	EditorResourcePreview::get_singleton()->queue_edited_resource_preview(mat, callable_mp(this, &LevelEditorDock::_material_preview_ready).bind(mat->get_instance_id()));
}

void LevelEditorDock::_material_preview_ready(const String &p_path, const Ref<Texture2D> &p_preview, const Ref<Texture2D> &p_small_preview, ObjectID p_for) {
	bool is_override = false;
	Ref<Material> mat = _get_displayed_material(screen, is_override);
	if (mat.is_null() || mat->get_instance_id() != p_for) {
		return;
	}
	if (p_preview.is_valid()) {
		material_preview->set_texture(p_preview);
	}
}

void LevelEditorDock::_brush_type_selected(int p_index) {
	if (screen) {
		screen->set_brush_type(p_index);
		// Rebuild the form so the Sphere Sides field appears/disappears.
		refresh();
	}
}

void LevelEditorDock::_sphere_sides_changed(double p_value) {
	if (screen) {
		screen->set_brush_sphere_sides((int)p_value);
	}
}

void LevelEditorDock::_browse_pressed() {
	// Same dialog as the inspector's "Quick Load": fuzzy search over all
	// project resources of the given base types. Textures are accepted too
	// and get wrapped in a StandardMaterial3D on selection.
	Vector<StringName> base_types;
	base_types.push_back(StringName("Material"));
	base_types.push_back(StringName("Texture2D"));
	EditorNode::get_singleton()->get_quick_open_dialog()->popup_dialog(base_types, callable_mp(this, &LevelEditorDock::_browse_selected));
}

void LevelEditorDock::_browse_selected(const String &p_path) {
	const String res_type = ResourceLoader::get_resource_type(p_path);
	Ref<Material> mat;
	if (ClassDB::is_parent_class(res_type, "Material")) {
		mat = ResourceLoader::load(p_path);
		ERR_FAIL_COND_MSG(mat.is_null(), "Cannot load material from path '" + p_path + "'.");
	} else if (ClassDB::is_parent_class(res_type, "Texture2D")) {
		// Wrapped via the shared cache so repeated picks of the same file
		// share one generated material.
		ERR_FAIL_NULL(screen);
		mat = screen->get_texture_material_cache().get_or_create(p_path);
		ERR_FAIL_COND(mat.is_null());
	} else {
		ERR_FAIL_MSG("Resource '" + p_path + "' is neither a Material nor a Texture2D.");
	}

	if (screen) {
		screen->set_active_material(mat);
	}
	refresh_material();
}

void LevelEditorDock::_save_pressed() {
	// Promote the displayed material to a standalone resource file (shared,
	// deduped on disk instead of embedded per-brush).
	bool is_override = false;
	Ref<Material> mat = _get_displayed_material(screen, is_override);
	ERR_FAIL_COND(mat.is_null());

	material_save_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	material_save_dialog->set_title(TTRC("Save Material"));
	if (mat->get_path().is_resource_file()) {
		material_save_dialog->set_current_path(mat->get_path());
	} else {
		String base = mat->get_name();
		if (base.is_empty()) {
			base = "material";
		}
		material_save_dialog->set_current_file(base + ".material");
	}
	material_save_dialog->popup_centered_ratio();
}

void LevelEditorDock::_save_selected(const String &p_path) {
	bool is_override = false;
	Ref<Material> mat = _get_displayed_material(screen, is_override);
	ERR_FAIL_COND(mat.is_null());

	const Error err = ResourceSaver::save(mat, p_path);
	if (err != OK) {
		ERR_FAIL_MSG(vformat("Cannot save material to path '%s' (error %d).", p_path, (int)err));
	}
	refresh_material(); // Name/tooltip now reflect the saved path.
}

Variant LevelEditorDock::_material_drag_data(const Point2 &p_point, Control *p_from) {
	// Dragging the active material onto a viewport face/brush - same payload
	// convention as EditorResourcePicker ("resource" type), so any control
	// that accepts resource drags can also take it.
	Ref<Material> mat = screen ? screen->get_active_material() : Ref<Material>();
	if (mat.is_null()) {
		return Variant();
	}
	return EditorNode::get_singleton()->drag_resource(mat, p_from);
}

void LevelEditorDock::_cancel_pressed() {
	if (screen) {
		screen->cancel_armed_action();
	}
}

LevelEditorDock::LevelEditorDock() {
	set_name(TTRC("Level"));

	// Scrollable settings form (top, expands).
	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	add_child(scroll);

	// Sticky active-material panel (bottom, never scrolls), framed like the
	// content boxes used elsewhere in the editor (style applied in
	// NOTIFICATION_THEME_CHANGED, once the theme is available).
	material_panel = memnew(PanelContainer);
	add_child(material_panel);

	VBoxContainer *material_vbox = memnew(VBoxContainer);
	material_panel->add_child(material_vbox);

	Label *title = memnew(Label);
	title->set_text(TTRC("Active Material"));
	material_vbox->add_child(title);

	material_preview = memnew(TextureRect);
	material_preview->set_custom_minimum_size(Size2(96, 96) * EDSCALE);
	material_preview->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	material_preview->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	material_preview->set_drag_forwarding(callable_mp(this, &LevelEditorDock::_material_drag_data).bind(material_preview), Callable(), Callable());
	material_vbox->add_child(material_preview);

	material_name = memnew(Label);
	material_name->set_text(TTRC("(Map Default)"));
	material_name->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	material_name->set_clip_text(true);
	material_name->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	material_vbox->add_child(material_name);

	HBoxContainer *material_buttons = memnew(HBoxContainer);
	material_vbox->add_child(material_buttons);

	material_browse = memnew(Button);
	material_browse->set_text(TTRC("Browse..."));
	material_browse->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	material_browse->connect("pressed", callable_mp(this, &LevelEditorDock::_browse_pressed));
	material_buttons->add_child(material_browse);

	material_buttons->add_child(memnew(VSeparator));

	material_save = memnew(Button);
	material_save->set_theme_type_variation("FlatMenuButton");
	material_save->set_tooltip_text(TTRC("Save the displayed material as a resource file."));
	material_save->connect("pressed", callable_mp(this, &LevelEditorDock::_save_pressed));
	material_buttons->add_child(material_save);

	material_save_dialog = memnew(EditorFileDialog);
	material_save_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
	material_save_dialog->add_filter("*.material", TTRC("Material"));
	material_save_dialog->add_filter("*.res", TTRC("Binary Resource"));
	material_save_dialog->add_filter("*.tres", TTRC("Text Resource"));
	material_save_dialog->connect("file_selected", callable_mp(this, &LevelEditorDock::_save_selected));
	add_child(material_save_dialog);

	refresh();
}
