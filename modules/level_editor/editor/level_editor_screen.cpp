/**************************************************************************/
/*  level_editor_screen.cpp                                               */
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

#include "level_editor_screen.h"

#include "../level_constants.h"
#include "../level_helpers.h"
#include "dock/level_editor_dock.h"

using namespace LevelHelpers;
using LevelEditorColors::GIZMO_PLANE_EXTENT;

#include "core/math/geometry_2d.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/inspector/multi_node_edit.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/camera_3d.h"
#include "scene/gui/button.h"
#include "scene/gui/control.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/separator.h"

void LevelEditorScreen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("clear_selection"), &LevelEditorScreen::clear_selection);
	ClassDB::bind_method(D_METHOD("set_brush_selection", "brushes"), &LevelEditorScreen::set_brush_selection);
}

void LevelEditorScreen::set_brush_selection(const TypedArray<Node> &p_brushes) {
	selected_brushes.clear();
	for (int i = 0; i < p_brushes.size(); i++) {
		LevelBrush *b = Object::cast_to<LevelBrush>(p_brushes[i]);
		if (b && b->is_inside_tree()) {
			selected_brushes.push_back(b);
		}
	}
	selected_brush = selected_brushes.is_empty() ? nullptr : selected_brushes[selected_brushes.size() - 1];
	_clear_element_selection();
	_sync_editor_selection();
	_update_overlays();
}

LevelEditorScreen::LevelEditorScreen() {
	set_name("Level");
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_process(true);
	set_process_input(true);
	set_focus_mode(FOCUS_ALL);

	MarginContainer *toolbar_margin = memnew(MarginContainer);
	toolbar_margin->add_theme_constant_override("margin_top", 1 * EDSCALE);
	toolbar_margin->add_theme_constant_override("margin_bottom", 1 * EDSCALE);
	toolbar_margin->set_custom_maximum_size(Size2(-1, 36 * EDSCALE));
	toolbar_margin->set_theme_type_variation("MainToolBarMargin");
	add_child(toolbar_margin);

	toolbar = memnew(HBoxContainer);
	toolbar_margin->add_child(toolbar);

	// Tool modes in button-group panels (Select, Move, Rotate, Scale) / (Block, Clip, Mirror)...
	PanelContainer *tool_panel = memnew(PanelContainer);
	tool_panel->set_theme_type_variation("PanelContainerButtonGroup");
	toolbar->add_child(tool_panel);
	HBoxContainer *tool_hbox = memnew(HBoxContainer);
	tool_panel->add_child(tool_hbox);

	for (int i = TOOL_SELECT; i <= TOOL_SCALE; i++) {
		Button *b = memnew(Button);
		b->set_toggle_mode(true);
		b->set_pressed(i == 0);
		b->connect("pressed", callable_mp(this, &LevelEditorScreen::_tool_changed).bind(i));
		b->set_theme_type_variation(SceneStringName(FlatButton));
		tool_hbox->add_child(b);
		tool_buttons[i] = b;
	}

	toolbar->add_child(memnew(VSeparator));

	PanelContainer *draw_panel = memnew(PanelContainer);
	draw_panel->set_theme_type_variation("PanelContainerButtonGroup");
	toolbar->add_child(draw_panel);

	HBoxContainer *draw_hbox = memnew(HBoxContainer);
	draw_panel->add_child(draw_hbox);

	for (int i = TOOL_BLOCK; i <= TOOL_MIRROR; i++) {
		Button *b = memnew(Button);
		b->set_toggle_mode(true);
		b->set_pressed(false);
		b->connect("pressed", callable_mp(this, &LevelEditorScreen::_tool_changed).bind(i));
		b->set_theme_type_variation(SceneStringName(FlatButton));
		draw_hbox->add_child(b);
		tool_buttons[i] = b;
	}

	toolbar->add_child(memnew(VSeparator));

	// ...and the selection target in a second panel (Mesh, Vertex, Edge, Face).
	// Orthogonal to the tool: any transform tool can act on any target.
	PanelContainer *element_panel = memnew(PanelContainer);
	element_panel->set_theme_type_variation("PanelContainerButtonGroup");
	toolbar->add_child(element_panel);

	HBoxContainer *element_hbox = memnew(HBoxContainer);
	element_panel->add_child(element_hbox);

	for (int i = 0; i < TARGET_MAX; i++) {
		Button *b = memnew(Button);
		b->set_toggle_mode(true);
		b->set_pressed(i == TARGET_MESH);
		b->connect("pressed", callable_mp(this, &LevelEditorScreen::_target_changed).bind(i));
		b->set_theme_type_variation(SceneStringName(FlatButton));
		element_hbox->add_child(b);
		target_buttons[i] = b;
	}

	// Icons are (re)assigned in NOTIFICATION_THEME_CHANGED. Text labels are
	// fallbacks for buttons without icons.
	tool_buttons[TOOL_SELECT]->set_tooltip_text(TTRC("Select (resize handles on a single selected brush)"));
	tool_buttons[TOOL_MOVE]->set_tooltip_text(TTRC("Move (translate gizmo)"));
	tool_buttons[TOOL_ROTATE]->set_tooltip_text(TTRC("Rotate"));
	tool_buttons[TOOL_SCALE]->set_tooltip_text(TTRC("Scale"));
	tool_buttons[TOOL_BLOCK]->set_tooltip_text(TTRC("Block"));
	tool_buttons[TOOL_CLIP]->set_tooltip_text(TTRC("Clip"));
	tool_buttons[TOOL_MIRROR]->set_tooltip_text(TTRC("Mirror (draw a plane to duplicate the brush reflected across it)"));
	target_buttons[TARGET_VERTEX]->set_tooltip_text(TTRC("Vertex"));
	target_buttons[TARGET_EDGE]->set_tooltip_text(TTRC("Edge (double-click: select straight chain, Alt+double-click: select loop)"));
	target_buttons[TARGET_FACE]->set_tooltip_text(TTRC("Face (Shift: hold while dragging to extrude)"));
	target_buttons[TARGET_MESH]->set_tooltip_text(TTRC("Mesh (whole-brush selection)"));

	// Set shortcuts for buttons
	tool_buttons[TOOL_SELECT]->set_shortcut(ED_SHORTCUT("spatial_editor/tool_transform", TTRC("Select Mode"), Key::Q, true));
	tool_buttons[TOOL_MOVE]->set_shortcut(ED_SHORTCUT("spatial_editor/tool_move", TTRC("Move Mode"), Key::W, true));
	tool_buttons[TOOL_ROTATE]->set_shortcut(ED_SHORTCUT("spatial_editor/tool_rotate", TTRC("Rotate Mode"), Key::E, true));
	tool_buttons[TOOL_SCALE]->set_shortcut(ED_SHORTCUT("spatial_editor/tool_scale", TTRC("Scale Mode"), Key::R, true));

	tool_buttons[TOOL_BLOCK]->set_shortcut(ED_SHORTCUT("level_editor/tool_block", TTRC("Block Tool"), Key::B, true));
	tool_buttons[TOOL_CLIP]->set_shortcut(ED_SHORTCUT("level_editor/tool_clip", TTRC("Clip Tool"), Key::C, true));
	tool_buttons[TOOL_MIRROR]->set_shortcut(ED_SHORTCUT("level_editor/tool_mirror", TTRC("Mirror Tool"), Key::M, true));

	target_buttons[TARGET_VERTEX]->set_shortcut(ED_SHORTCUT("level_editor/tool_vertex", TTRC("Vertex Selection"), Key::KEY_1, true));
	target_buttons[TARGET_EDGE]->set_shortcut(ED_SHORTCUT("level_editor/tool_edge", TTRC("Edge Selection"), Key::KEY_2, true));
	target_buttons[TARGET_FACE]->set_shortcut(ED_SHORTCUT("level_editor/tool_face", TTRC("Face Selection"), Key::KEY_3, true));
	target_buttons[TARGET_MESH]->set_shortcut(ED_SHORTCUT("level_editor/target_mesh", TTRC("Mesh Selection"), Key::KEY_4, true));

	toolbar->add_child(memnew(VSeparator));

	// Tools menu: replayable actions (Shift+G repeats the last one).
	tools_menu = memnew(MenuButton);
	tools_menu->set_text(TTRC("Tools"));
	tools_menu->set_flat(false);
	tools_menu->set_theme_type_variation("FlatMenuButton");
	PopupMenu *tools_popup = tools_menu->get_popup();
	tools_popup->add_shortcut(ED_SHORTCUT("level_editor/replay_action", TTRC("Replay Action"), KeyModifierMask::SHIFT | Key::G, true), 0);
	tools_popup->connect("id_pressed", callable_mp(this, &LevelEditorScreen::_tools_menu_selected));
	toolbar->add_child(tools_menu);

	toolbar->add_child(memnew(VSeparator));

	// View menu: per-viewport submenus. IDs are per-submenu (0..DISPLAY_MAX-1
	// display modes, DISPLAY_MAX/+1 the HUD toggles); the viewport index is
	// bound to each submenu's handler (encoding vp in the ID collided with
	// the grid-toggle IDs at 4*DISPLAY_MAX).
	view_menu = memnew(MenuButton);
	view_menu->set_text(TTRC("View"));
	view_menu->set_flat(false);
	view_menu->set_theme_type_variation("FlatMenuButton");
	PopupMenu *view_popup = view_menu->get_popup();
	static const char *vp_names[4] = { "Perspective", "Top", "Front", "Side" };
	static const char *mode_names[LevelEditorViewport::DISPLAY_MAX] = { "Normal", "Wireframe", "Overdraw", "Lighting", "Unshaded" };
	for (int vp = 0; vp < 4; vp++) {
		PopupMenu *sub = memnew(PopupMenu);
		view_submenus[vp] = sub;
		sub->set_hide_on_checkable_item_selection(false);
		for (int m = 0; m < LevelEditorViewport::DISPLAY_MAX; m++) {
			sub->add_radio_check_item(TTRC(mode_names[m]), m);
		}
		sub->set_item_checked(LevelEditorViewport::DISPLAY_UNSHADED, true);
		// HUD toggles (same as the 3D editor's View menu), separated from the
		// display modes. IDs sit past the mode range; routed in the handler.
		sub->add_separator();
		sub->add_check_item(TTRC("View Information"), LevelEditorViewport::DISPLAY_MAX);
		sub->add_check_item(TTRC("View Frame Time"), LevelEditorViewport::DISPLAY_MAX + 1);
		sub->connect("id_pressed", callable_mp(this, &LevelEditorScreen::_view_display_selected).bind(vp));
		view_popup->add_submenu_node_item(TTRC(vp_names[vp]), sub);
	}
	view_popup->add_separator();
	// Grid toggles (global, not per-viewport). IDs must avoid the submenu
	// items' auto-assigned IDs (add_submenu_node_item assigns items.size()
	// when no ID is given - the 4 submenu items got 0..3), so use IDs well
	// past the item count.
	grid_2d_enabled = EditorSettings::get_singleton()->get_project_metadata("level_editor", "grid_2d_enabled", true);
	grid_3d_enabled = EditorSettings::get_singleton()->get_project_metadata("level_editor", "grid_3d_enabled", true);
	view_popup->add_check_item(TTRC("Show 2D Grid"), VIEW_MENU_GRID_2D_ID);
	view_popup->add_check_item(TTRC("Show 3D Grid"), VIEW_MENU_GRID_3D_ID);
	view_popup->set_item_checked(view_popup->get_item_index(VIEW_MENU_GRID_2D_ID), grid_2d_enabled);
	view_popup->set_item_checked(view_popup->get_item_index(VIEW_MENU_GRID_3D_ID), grid_3d_enabled);
	view_popup->connect("id_pressed", callable_mp(this, &LevelEditorScreen::_view_grid_toggled));
	toolbar->add_child(view_menu);

	toolbar->add_child(memnew(VSeparator));

	vertex_menu = memnew(MenuButton);
	vertex_menu->set_text(TTRC("Vertex"));
	vertex_menu->set_flat(false);
	vertex_menu->set_theme_type_variation("FlatMenuButton");
	PopupMenu *vertex_popup = vertex_menu->get_popup();
	vertex_popup->add_item(TTRC("Extrude"), 0);
	vertex_popup->add_item(TTRC("Collapse"), 1);
	vertex_popup->connect("id_pressed", callable_mp(this, &LevelEditorScreen::_vertex_menu_selected));
	toolbar->add_child(vertex_menu);

	edge_menu = memnew(MenuButton);
	edge_menu->set_text(TTRC("Edge"));
	edge_menu->set_flat(false);
	edge_menu->set_theme_type_variation("FlatMenuButton");
	PopupMenu *edge_popup = edge_menu->get_popup();
	edge_popup->add_item(TTRC("Extrude"), 0);
	edge_popup->add_item(TTRC("Bridge"), 1);
	edge_popup->add_item(TTRC("Collapse"), 2);
	edge_popup->add_item(TTRC("Bevel"), 3);
	edge_popup->connect("id_pressed", callable_mp(this, &LevelEditorScreen::_edge_menu_selected));
	toolbar->add_child(edge_menu);

	face_menu = memnew(MenuButton);
	face_menu->set_text(TTRC("Face"));
	face_menu->set_flat(false);
	face_menu->set_theme_type_variation("FlatMenuButton");
	PopupMenu *face_popup = face_menu->get_popup();
	face_popup->add_item(TTRC("Extrude"), 0);
	face_popup->add_item(TTRC("Delete"), 2);
	face_popup->add_shortcut(ED_SHORTCUT("level_editor/subdivide_face", TTRC("Subdivide"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::D, true), 4);
	face_popup->add_separator();
	face_popup->add_shortcut(ED_SHORTCUT("level_editor/flip_faces", TTRC("Flip Faces"), Key::F, true), 3);
	face_popup->connect("id_pressed", callable_mp(this, &LevelEditorScreen::_face_menu_selected));
	toolbar->add_child(face_menu);

	Control *toolbar_spring = memnew(Control);
	toolbar_spring->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	toolbar_spring->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	toolbar->add_child(toolbar_spring);

	Label *grid_label = memnew(Label);
	grid_label->set_text(TTRC("Grid:"));
	toolbar->add_child(grid_label);

	grid_size_option = memnew(OptionButton);
	grid_size_option->set_clip_text(true);
	grid_size_option->set_custom_minimum_size(Size2(115, 0));
	for (int i = 0; i < LevelEditorGrid::STEP_COUNT; i++) {
		// Plain decimals: integers without a trailing .0, fractions as-is.
		real_t step = LevelEditorGrid::STEPS[i];
		String label = (step >= 1.0) ? String::num_int64((int64_t)step) : String::num(step);
		grid_size_option->add_item(label);
	}
	grid_size_option->set_fit_to_longest_item(false);
	grid_size_option->select(_grid_step_index());
	grid_size_option->get_popup()->connect("index_pressed", callable_mp(this, &LevelEditorScreen::_grid_size_selected));
	toolbar->add_child(grid_size_option);

	toolbar->add_child(memnew(VSeparator));

	bake_button = memnew(Button);
	bake_button->set_text(TTRC("Bake Level"));
	bake_button->set_tooltip_text(TTRC("Bake brushes to a MeshInstance3D with trimesh collision and an occluder."));
	bake_button->connect("pressed", callable_mp(this, &LevelEditorScreen::_bake_pressed));
	toolbar->add_child(bake_button);

	// Quad viewports: main vertical split with two horizontal splits inside,
	// all with nested dragger intersections enabled - grabbing the center
	// intersection drags both axes at once (like the 3D editor's quad view).
	rows_split = memnew(SplitContainer);
	rows_split->set_vertical(true);
	rows_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	rows_split->set_drag_nested_intersections(true);
	add_child(rows_split);

	top_split = memnew(SplitContainer);
	top_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	top_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	top_split->set_drag_nested_intersections(true);
	bottom_split = memnew(SplitContainer);
	bottom_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	bottom_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	bottom_split->set_drag_nested_intersections(true);
	rows_split->add_child(top_split);
	rows_split->add_child(bottom_split);

	for (int i = 0; i < 4; i++) {
		LevelEditorViewport *vp = memnew(LevelEditorViewport);
		vp->screen = this;
		vp->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		vp->set_custom_minimum_size(Size2(0, 160));
		viewports[i] = vp;
	}
	viewports[0]->set_view_type(LevelEditorViewport::VIEW_PERSPECTIVE);
	viewports[1]->set_view_type(LevelEditorViewport::VIEW_TOP);
	viewports[2]->set_view_type(LevelEditorViewport::VIEW_FRONT);
	viewports[3]->set_view_type(LevelEditorViewport::VIEW_SIDE);

	// Ortho views default to overdraw (engine renders its BG black); perspective
	// stays unshaded. Saved modes override below.
	for (int vp = 1; vp < 4; vp++) {
		viewports[vp]->set_display_mode(LevelEditorViewport::DISPLAY_OVERDRAW);
		_sync_display_submenu(vp, LevelEditorViewport::DISPLAY_OVERDRAW);
	}

	// Restore per-viewport display modes saved for this project (default is
	// Unshaded, set in the viewport constructor).
	{
		Array saved = EditorSettings::get_singleton()->get_project_metadata("level_editor", "viewport_display_modes", Array());
		for (int vp = 0; vp < 4 && vp < saved.size(); vp++) {
			int m = (int)saved[vp];
			if (m < 0 || m >= LevelEditorViewport::DISPLAY_MAX) {
				continue;
			}
			viewports[vp]->set_display_mode((LevelEditorViewport::DisplayMode)m);
			_sync_display_submenu(vp, m);
		}
	}

	// HUD toggles (View Information / View Frame Time), per viewport. The
	// items live in the display-mode submenus past the mode range.
	for (int vp = 0; vp < 4; vp++) {
		bool info = EditorSettings::get_singleton()->get_project_metadata("level_editor", vformat("view_%d_info", vp), false);
		bool ft = EditorSettings::get_singleton()->get_project_metadata("level_editor", vformat("view_%d_frame_time", vp), false);
		viewports[vp]->set_info_visible(info);
		viewports[vp]->set_frame_time_visible(ft);
		view_submenus[vp]->set_item_checked(view_submenus[vp]->get_item_index(LevelEditorViewport::DISPLAY_MAX), info);
		view_submenus[vp]->set_item_checked(view_submenus[vp]->get_item_index(LevelEditorViewport::DISPLAY_MAX + 1), ft);
	}

	// Shown instead of the quad viewports when the edited scene has no
	// LevelMap yet.
	no_map_panel = memnew(MarginContainer);
	no_map_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	no_map_panel->hide();
	add_child(no_map_panel);

	VBoxContainer *no_map_vbox = memnew(VBoxContainer);
	no_map_vbox->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	no_map_panel->add_child(no_map_vbox);

	no_map_label = memnew(Label);
	no_map_label->set_text(TTRC("This scene does not contain a LevelMap node. Create one to begin editing."));
	no_map_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	no_map_vbox->add_child(no_map_label);

	create_map_button = memnew(Button);
	create_map_button->set_text(TTRC("Create LevelMap"));
	create_map_button->set_h_size_flags(SIZE_SHRINK_CENTER);
	create_map_button->connect("pressed", callable_mp(this, &LevelEditorScreen::_create_map_pressed));
	no_map_vbox->add_child(create_map_button);

	top_split->add_child(viewports[0]);
	top_split->add_child(viewports[1]);
	bottom_split->add_child(viewports[2]);
	bottom_split->add_child(viewports[3]);

	// Initial enabled/disabled state of the element menu items (no map, no
	// selection: everything starts disabled).
	_update_menu_states();
}

void LevelEditorScreen::input(const Ref<InputEvent> &p_event) {
	// The _vp_input phase runs BEFORE gui/shortcut dispatch (and before the
	// scene dock's no-context Delete shortcut), so swallow our keys here
	// when the Level screen is visible.
	if (!is_visible_in_tree()) {
		return;
	}
	Ref<InputEventKey> k = p_event;
	if (!k.is_valid() || !k->is_pressed()) {
		return;
	}

	// Freelook (RMB-hold in the perspective viewport) flies with WASD/QE -
	// swallow the tool-switch keys (Q/W/E/R, echoes included) so flying
	// doesn't change tools.
	bool freelook = false;
	for (int i = 0; i < 4; i++) {
		if (viewports[i]->is_freelook_active()) {
			freelook = true;
			break;
		}
	}
	if (freelook) {
		switch (k->get_keycode()) {
			case Key::Q:
			case Key::W:
			case Key::E:
			case Key::R:
				get_viewport()->set_input_as_handled();
				return;
			default:
				break;
		}
	}
	if (k->is_echo()) {
		return;
	}

	switch (k->get_keycode()) {
		case Key::KEY_DELETE: {
			_delete_selection();
			get_viewport()->set_input_as_handled();
		} break;
		case Key::G: {
			// Shift+G replays the last recorded action (e.g. duplicate-drag).
			if (k->is_shift_pressed() && last_action.kind != ReplayAction::KIND_NONE) {
				_replay_last_action();
				get_viewport()->set_input_as_handled();
			}
		} break;
		case Key::F: {
			// Edge mode: quick bevel (grid size, default steps/shape). Face mode
			// leaves F to the Flip Faces button shortcut below.
			if (selection_target == TARGET_EDGE && !k->is_shift_pressed()) {
				_action_bevel_edges(true); // Quick bevel.
				get_viewport()->set_input_as_handled();
			}
		} break;
		case Key::BRACKETLEFT:
		case Key::BRACKETRIGHT: {
			int idx = _grid_step_index();
			idx += (k->get_keycode() == Key::BRACKETRIGHT) ? 1 : -1;
			idx = CLAMP(idx, 0, LevelEditorGrid::STEP_COUNT - 1);
			if (LevelEditorGrid::STEPS[idx] != grid_size) {
				grid_size = LevelEditorGrid::STEPS[idx];
				grid_size_option->select(idx);
				_update_overlays();
			}
			get_viewport()->set_input_as_handled();
		} break;
		case Key::ENTER:
		case Key::KP_ENTER: {
			bool handled = false;
			if (ghost_active) {
				_ghost_commit();
				handled = true;
			} else if (clip_active) {
				_clip_apply();
				handled = true;
			} else if (mirror_active) {
				_mirror_apply();
				handled = true;
			} else if (armed_action != ACTION_NONE) {
				_action_apply_armed();
				handled = true;
			}
			if (handled) {
				get_viewport()->set_input_as_handled();
			}
		} break;
		case Key::ESCAPE: {
			bool handled = false;
			if (ghost_active) {
				_ghost_cancel();
				handled = true;
			} else if (clip_active) {
				_clip_cancel();
				handled = true;
			} else if (mirror_active) {
				_mirror_cancel();
				handled = true;
			} else if (armed_action != ACTION_NONE) {
				_action_cancel_armed();
				handled = true;
			} else if (dragging) {
				dragging = false;
				drag_active = false;
				drag_viewport = nullptr;
				_update_overlays();
				handled = true;
			}
			if (handled) {
				get_viewport()->set_input_as_handled();
			}
		} break;
		default:
			break;
	}
}

void LevelEditorScreen::shortcut_input(const Ref<InputEvent> &p_event) {
	// Keys handled by the level editor are consumed here so no-context
	// editor shortcuts (like the scene tree's Delete) never see them.
	// Actual handling happens in input() (the earlier _vp_input phase).
	Ref<InputEventKey> k = p_event;
	if (!k.is_valid() || !k->is_pressed()) {
		return;
	}
	Key code = k->get_keycode();
	if (code == Key::KEY_DELETE || code == Key::BRACKETLEFT || code == Key::BRACKETRIGHT ||
			code == Key::ENTER || code == Key::KP_ENTER || code == Key::ESCAPE ||
			(code == Key::G && k->is_shift_pressed()) ||
			(code == Key::F && selection_target == TARGET_EDGE && !k->is_shift_pressed())) {
		accept_event();
	}
}

void LevelEditorScreen::_edit_brush_node(LevelBrush *p_brush) {
	// Show the brush in the inspector, but keep keyboard focus on the level
	// screen so editor shortcuts (e.g. scene-tree Delete) don't hijack keys.
	EditorInterface::get_singleton()->edit_node(p_brush);
	call_deferred("grab_focus");
}

void LevelEditorScreen::_sync_editor_selection() {
	if (applying_editor_selection || !is_visible_in_tree()) {
		return; // Don't bounce editor-originated selections back (feedback loop).
	}
	EditorSelection *sel = EditorInterface::get_singleton()->get_selection();
	// Diff against the current editor selection: remove stale brush nodes, add
	// new ones. Non-brush selected nodes are left alone.
	TypedArray<Node> current = sel->get_selected_nodes();
	for (int i = 0; i < current.size(); i++) {
		Node *n = Object::cast_to<Node>(current[i]);
		LevelBrush *b = Object::cast_to<LevelBrush>(n);
		if (b && !selected_brushes.has(b)) {
			sel->remove_node(n);
		}
	}
	for (LevelBrush *b : selected_brushes) {
		if (b->is_inside_tree() && !sel->is_selected(b)) {
			sel->add_node(b);
		}
	}
	sel->update();
	// Inspector: mirror the scene tree dock's behavior - one node shows the
	// node, several show the multi-node editor (MultiNodeEdit). Pushing the
	// single primary here would stomp the multi-edit view.
	if (selected_brushes.size() == 1 && selected_brush) {
		_edit_brush_node(selected_brush);
	} else if (selected_brushes.size() > 1) {
		Node *root = EditorNode::get_singleton()->get_edited_scene();
		if (root) {
			Ref<MultiNodeEdit> mne = memnew(MultiNodeEdit);
			for (LevelBrush *b : selected_brushes) {
				if (b->is_inside_tree()) {
					mne->add_node(root->get_path_to(b));
				}
			}
			EditorNode::get_singleton()->push_item(mne.ptr());
			call_deferred("grab_focus");
		}
	}
}

bool LevelEditorScreen::_mesh_selection_has(LevelBrush *p_brush) const {
	return selected_brushes.has(p_brush);
}

void LevelEditorScreen::_mesh_selection_set(LevelBrush *p_brush) {
	selected_brushes.clear();
	selected_brushes.push_back(p_brush);
	selected_brush = p_brush;
	_sync_editor_selection();
}

void LevelEditorScreen::_mesh_selection_toggle(LevelBrush *p_brush) {
	const int at = selected_brushes.find(p_brush);
	if (at >= 0) {
		selected_brushes.remove_at(at);
		// Keep the primary valid: fall back to the last remaining brush.
		if (selected_brush == p_brush) {
			selected_brush = selected_brushes.is_empty() ? nullptr : selected_brushes[selected_brushes.size() - 1];
		}
	} else {
		selected_brushes.push_back(p_brush);
		selected_brush = p_brush;
	}
	_sync_editor_selection();
}

void LevelEditorScreen::apply_editor_selection(const TypedArray<Node> &p_nodes) {
	applying_editor_selection = true;
	// Exact mirror of the editor selection: drop brushes no longer selected,
	// adopt newly selected ones. This keeps replace (plain click) and
	// accumulate (Shift+click) semantics identical to the scene tree.
	for (int i = selected_brushes.size() - 1; i >= 0; i--) {
		bool still = false;
		for (int j = 0; j < p_nodes.size(); j++) {
			if (Object::cast_to<Node>(p_nodes[j]) == selected_brushes[i]) {
				still = true;
				break;
			}
		}
		if (!still) {
			selected_brushes.remove_at(i);
		}
	}
	LevelBrush *primary = nullptr;
	for (int i = 0; i < p_nodes.size(); i++) {
		LevelBrush *b = Object::cast_to<LevelBrush>(p_nodes[i]);
		if (b) {
			if (!selected_brushes.has(b)) {
				selected_brushes.push_back(b);
			}
			primary = b; // Last brush in the editor selection acts as primary.
		}
	}
	if (primary) {
		selected_brush = primary;
	} else if (!selected_brushes.has(selected_brush)) {
		selected_brush = selected_brushes.is_empty() ? nullptr : selected_brushes[selected_brushes.size() - 1];
	}
	if (selected_brush) {
		LevelMap *map = Object::cast_to<LevelMap>(selected_brush->get_parent());
		if (map && map != current_map) {
			current_map = map;
			_update_map_ui();
		}
	}
	_update_overlays();
	applying_editor_selection = false;
}

void LevelEditorScreen::make_visible(bool p_visible) {
	if (p_visible) {
		_update_map_ui();
		_update_overlays();
		grab_focus();
	}
}

// Finds the first LevelMap in the edited scene (DFS), or nullptr.
LevelMap *LevelEditorScreen::_find_map_in_scene() const {
	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	if (!root) {
		return nullptr;
	}
	List<Node *> stack;
	stack.push_back(root);
	while (!stack.is_empty()) {
		Node *n = stack.front()->get();
		stack.pop_front();
		LevelMap *lm = Object::cast_to<LevelMap>(n);
		if (lm) {
			return lm;
		}
		for (int i = 0; i < n->get_child_count(); i++) {
			stack.push_back(n->get_child(i));
		}
	}
	return nullptr;
}

void LevelEditorScreen::_resolve_map() {
	if (current_map) {
		return;
	}
	current_map = _find_map_in_scene();
	if (current_map) {
		current_map->refresh();
	}
}

void LevelEditorScreen::_abandon_drags() {
	// Scene change: the brushes any drag references belong to the old scene,
	// so nothing here may dereference them (no undo commit, no map refresh).
	dragging = false;
	drag_active = false;
	drag_viewport = nullptr;

	ghost_handle_hover = GHOST_NONE;
	ghost_handle_drag = GHOST_NONE;
	ghost_drag_viewport = nullptr;
	ghost_moving = false;

	clip_drag_point = -1;
	mirror_drag_point = -1;

	gizmo_dragging = false;
	gizmo_drag_part = GIZMO_NONE;
	gizmo_drag_viewport = nullptr;
	gizmo_drag_brush_verts.clear();
	gizmo_drag_original_positions.clear();
	gizmo_dup_sources.clear();
	gizmo_extrude_drag = false;
	gizmo_duplicate_drag = false;
	gizmo_extrude_orig_verts.clear();
	gizmo_extrude_orig_faces.clear();
	gizmo_extrude_orig_mats.clear();
	gizmo_extrude_cap_faces.clear();
	gizmo_extrude_cap_normals.clear();
	gizmo_extrude_elem_verts.clear();
	gizmo_extrude_wall_edges.clear();
	gizmo_extrude_moved_verts.clear();

	rotate_hover_axis = -1;
	rotate_drag_axis = -1;
	rotate_drag_viewport = nullptr;

	select_handle_hover = GHOST_NONE;
	select_handle_drag = GHOST_NONE;
	select_drag_viewport = nullptr;
	select_moving = false;
	select_move_viewport = nullptr;
	select_move_original_positions.clear();

	paint_select_active = false;
	paint_select_viewport = nullptr;
}

void LevelEditorScreen::on_scene_changed() {
	current_map = nullptr;
	_abandon_drags();
	if (ghost_active) {
		_ghost_cancel();
	}
	if (clip_active) {
		_clip_cancel();
	}
	if (mirror_active) {
		_mirror_cancel();
	}
	_clear_selection();
	_update_map_ui();
	_update_overlays();
}

void LevelEditorScreen::_update_warning_color() {
	if (no_map_label && is_inside_tree()) {
		no_map_label->add_theme_color_override(SceneStringName(font_color), no_map_label->get_theme_color(SNAME("warning_color"), EditorStringName(Editor)));
	}
}

void LevelEditorScreen::_update_map_ui() {
	if (!current_map) {
		// Fresh scene: only adopt a map found in the edited scene tree - never
		// auto-create one (the user must press "Create LevelMap").
		current_map = _find_map_in_scene();
		if (current_map) {
			current_map->refresh();
		}
	}

	bool has_map = current_map != nullptr;
	if (rows_split) {
		rows_split->set_visible(has_map);
	}
	if (no_map_panel) {
		no_map_panel->set_visible(!has_map);
	}
	// The material panel falls back to the map's default material.
	if (dock) {
		dock->refresh_material();
	}
}

void LevelEditorScreen::_create_map_pressed() {
	create_map_button->release_focus();
	_get_or_create_map();
	_update_map_ui();
	_update_overlays();
}

LevelMap *LevelEditorScreen::_get_or_create_map() {
	_resolve_map();
	if (current_map) {
		return current_map;
	}
	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	ERR_FAIL_NULL_V(root, nullptr);

	current_map = memnew(LevelMap);
	current_map->set_name("LevelMap");
	root->add_child(current_map);
	current_map->set_owner(root);
	return current_map;
}

void LevelEditorScreen::_update_mode_icons() {
	if (tool_buttons[TOOL_SELECT]) {
		tool_buttons[TOOL_SELECT]->set_button_icon(get_editor_theme_icon(SNAME("ToolSelect")));
		tool_buttons[TOOL_MOVE]->set_button_icon(get_editor_theme_icon(SNAME("ToolMove")));
		tool_buttons[TOOL_ROTATE]->set_button_icon(get_editor_theme_icon(SNAME("ToolRotate")));
		tool_buttons[TOOL_SCALE]->set_button_icon(get_editor_theme_icon(SNAME("ToolScale")));
		tool_buttons[TOOL_BLOCK]->set_button_icon(get_editor_theme_icon(SNAME("Brush")));
		tool_buttons[TOOL_CLIP]->set_button_icon(get_editor_theme_icon(SNAME("Clip")));
		tool_buttons[TOOL_MIRROR]->set_button_icon(get_editor_theme_icon(SNAME("Mirror")));
		target_buttons[TARGET_VERTEX]->set_button_icon(get_editor_theme_icon(SNAME("ControlAlignCenterLeft")));
		target_buttons[TARGET_EDGE]->set_button_icon(get_editor_theme_icon(SNAME("ControlAlignRightWide")));
		target_buttons[TARGET_FACE]->set_button_icon(get_editor_theme_icon(SNAME("ControlAlignFullRect")));
		target_buttons[TARGET_MESH]->set_button_icon(get_editor_theme_icon(SNAME("MeshTool")));
	}
}

void LevelEditorScreen::_tool_changed(int p_tool) {
	// Clicking the Clip button again while a clip is active cycles
	// keep-left / keep-right / keep-both (like Hammer's clip tool).
	if ((Tool)p_tool == TOOL_CLIP && tool == TOOL_CLIP && clip_active) {
		_clip_cycle_side();
		tool_buttons[TOOL_CLIP]->set_pressed(true);
	} else {
		_set_tool((Tool)p_tool);
	}
	// Don't leave keyboard focus on the toolbar buttons, so Enter/Esc/etc.
	// go to the viewports instead of re-triggering the button.
	for (int i = 0; i < TOOL_MAX; i++) {
		tool_buttons[i]->release_focus();
	}
}

void LevelEditorScreen::_target_changed(int p_target) {
	_set_target((SelectionTarget)p_target);
	for (int i = 0; i < TARGET_MAX; i++) {
		target_buttons[i]->release_focus();
	}
}

void LevelEditorScreen::_set_tool(Tool p_tool) {
	// Interrupt any in-progress drag first - the drag's undo action commits
	// against the OLD tool's snapshots, so ending it cleanly is required
	// before switching (a dropped extrude drag would be un-undoable).
	if (gizmo_dragging) {
		_gizmo_end_drag();
	}
	if (rotate_drag_axis >= 0) {
		_rotate_end_drag();
	}
	if (select_handle_drag != GHOST_NONE) {
		_select_handle_end_drag();
	}
	if (select_moving) {
		// End the whole-brush move like an LMB release: commit the position
		// undo, or a mid-move shortcut makes the move un-undoable.
		select_moving = false;
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
		select_move_viewport = nullptr;
		select_move_original_positions.clear();
	}
	paint_select_active = false;
	paint_select_viewport = nullptr;

	const bool was_drawing = _is_drawing_tool();
	const bool to_drawing = (p_tool == TOOL_BLOCK || p_tool == TOOL_CLIP || p_tool == TOOL_MIRROR);

	// Entering a drawing tool suspends the transform tool + selection target;
	// leaving one restores them (Hammer remembers).
	Tool restore_tool = p_tool;
	if (to_drawing && !was_drawing) {
		last_transform_tool = tool;
		last_target = selection_target;
	} else if (!to_drawing && was_drawing) {
		restore_tool = last_transform_tool;
		selection_target = last_target;
	}

	tool = restore_tool;
	if (!to_drawing) {
		last_transform_tool = tool;
	}
	for (int i = 0; i < TOOL_MAX; i++) {
		tool_buttons[i]->set_pressed(i == (int)tool);
	}
	for (int i = 0; i < TARGET_MAX; i++) {
		target_buttons[i]->set_pressed(i == (int)selection_target);
	}
	// The drawing tools don't transform the selection - drop it.
	if (to_drawing) {
		_clear_selection();
	}
	if (tool != TOOL_BLOCK && ghost_active) {
		_ghost_cancel();
	}
	if (tool != TOOL_CLIP && clip_active) {
		_clip_cancel();
	}
	if (tool != TOOL_MIRROR && mirror_active) {
		_mirror_cancel();
	}
	_action_cancel_armed();
	if (dock) {
		dock->refresh();
	}
	_update_overlays();
}

void LevelEditorScreen::_set_target(SelectionTarget p_target) {
	if (p_target == selection_target) {
		return;
	}
	if (gizmo_dragging) {
		_gizmo_end_drag();
	}
	if (rotate_drag_axis >= 0) {
		_rotate_end_drag();
	}
	paint_select_active = false;
	paint_select_viewport = nullptr;

	selection_target = p_target;
	for (int i = 0; i < TARGET_MAX; i++) {
		target_buttons[i]->set_pressed(i == (int)selection_target);
	}
	// Each target owns its own selection type.
	_clear_selection();
	_action_cancel_armed();
	_update_overlays();
}

int LevelEditorScreen::_grid_step_index() const {
	int idx = 0;
	for (int i = 0; i < LevelEditorGrid::STEP_COUNT; i++) {
		if (LevelEditorGrid::STEPS[i] <= grid_size) {
			idx = i;
		}
	}
	return idx;
}

void LevelEditorScreen::_grid_size_selected(int p_index) {
	grid_size_option->release_focus();
	grid_size = LevelEditorGrid::STEPS[CLAMP(p_index, 0, LevelEditorGrid::STEP_COUNT - 1)];
	grid_size_option->select(CLAMP(p_index, 0, LevelEditorGrid::STEP_COUNT - 1));
	_update_overlays();
}

Vector3 LevelEditorScreen::_snap(const Vector3 &p_v) const {
	return Vector3(_snap(p_v.x), _snap(p_v.y), _snap(p_v.z));
}

real_t LevelEditorScreen::_snap(real_t p_v) const {
	return Math::snapped(p_v, grid_size);
}

// Records one brush's current topology as the do-state against the given
// snapshot, into an already-created undo action. Shared by _commit_brush_undo
// (single brush) and the multi-brush delete/tool paths (one action spanning
// several brushes).
void LevelEditorScreen::_add_brush_undo_pair(EditorUndoRedoManager *p_undo_redo, LevelBrush *p_brush, const PackedVector3Array &p_old_verts, const Array &p_old_faces, const Array &p_old_mats) {
	p_undo_redo->add_do_property(p_brush, "vertices", p_brush->get_vertices_data());
	p_undo_redo->add_do_property(p_brush, "faces", p_brush->get_faces_data());
	p_undo_redo->add_do_property(p_brush, "face_materials", p_brush->get_face_materials_data());
	p_undo_redo->add_undo_property(p_brush, "vertices", p_old_verts);
	p_undo_redo->add_undo_property(p_brush, "faces", p_old_faces);
	p_undo_redo->add_undo_property(p_brush, "face_materials", p_old_mats);
}

void LevelEditorScreen::_delete_selection() {
	if (!current_map) {
		return;
	}

	switch (selection_target) {
		case TARGET_MESH: {
			if (selected_brushes.is_empty()) {
				return;
			}
			// Delete all selected brush nodes in one undo action.
			LevelMap *map = current_map;
			Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
			Vector<LevelBrush *> doomed = selected_brushes;
			_clear_selection();

			EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
			undo_redo->create_action(TTR("Delete Brush"));
			for (LevelBrush *target : doomed) {
				undo_redo->add_do_method(map, "remove_child", target);
				undo_redo->add_undo_method(map, "add_child", target);
				undo_redo->add_undo_method(target, "set_owner", root);
				undo_redo->add_undo_reference(target);
			}
			undo_redo->add_do_method(map, "refresh");
			undo_redo->add_undo_method(map, "refresh");
			undo_redo->commit_action();
			_refresh_map();
		} break;
		case TARGET_FACE:
			_action_delete_faces();
			break;
		case TARGET_EDGE:
			_action_collapse_edges();
			break;
		case TARGET_VERTEX:
			_action_collapse_vertices();
			break;
		default:
			break;
	}
	_update_overlays();
}

void LevelEditorScreen::_commit_brush_undo(const String &p_action, LevelBrush *p_brush, const PackedVector3Array &p_old_verts, const Array &p_old_faces, const Array &p_old_mats, bool p_execute) {
	LevelMap *map = current_map;
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(p_action);
	_add_brush_undo_pair(undo_redo, p_brush, p_old_verts, p_old_faces, p_old_mats);
	undo_redo->add_do_method(map, "refresh");
	undo_redo->add_undo_method(map, "refresh");
	undo_redo->commit_action(p_execute);
}

void LevelEditorScreen::_commit_brush_verts_undo(const String &p_action, const HashMap<LevelBrush *, PackedVector3Array> &p_old_verts) {
	// One undo action across every brush whose vertices actually changed vs
	// the snapshot. No-op (no action created) when nothing moved.
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	bool created = false;
	for (const KeyValue<LevelBrush *, PackedVector3Array> &E : p_old_verts) {
		LevelBrush *target = E.key;
		if (target->get_vertices_data() == E.value) {
			continue;
		}
		if (!created) {
			undo_redo->create_action(p_action);
			created = true;
		}
		_add_brush_undo_pair(undo_redo, target, E.value, target->get_faces_data(), target->get_face_materials_data());
	}
	if (created) {
		undo_redo->add_do_method(current_map, "refresh");
		undo_redo->add_undo_method(current_map, "refresh");
		undo_redo->commit_action(false);
	}
}

void LevelEditorScreen::_clear_selection() {
	selected_brush = nullptr;
	selected_brushes.clear();
	select_handle_hover = GHOST_NONE;
	select_handle_drag = GHOST_NONE;
	select_moving = false;
	select_move_viewport = nullptr;
	select_move_original_positions.clear();
	paint_select_active = false;
	paint_select_viewport = nullptr;
	rotate_hover_axis = -1;
	rotate_drag_axis = -1;
	_clear_element_selection();
	hover_brush = nullptr;
	hover_face = -1;
	has_hover_edge = false;
	has_hover_vertex = false;
	// Force the next hover update to re-pick (the throttle's change-check
	// would otherwise compare against this manually-cleared state).
	hover_last_pick = Vector2(Math::INF, Math::INF);
	_sync_editor_selection();
}

void LevelEditorScreen::_clear_element_selection() {
	selected_faces.clear();
	selected_edges.clear();
	selected_vertices.clear();
}

void LevelEditorScreen::_select_edge_loop(LevelBrush *p_brush, const LevelBrush::EdgeKey &p_edge) {
	// Replace the edge selection on this brush with the whole loop.
	Vector<LevelBrush::EdgeKey> loop = p_brush->get_edge_loop(p_edge);
	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> &set = _edge_set(p_brush);
	set.clear();
	for (const LevelBrush::EdgeKey &e : loop) {
		set.insert(e);
	}
	_update_overlays();
}

void LevelEditorScreen::_select_edge_chain(LevelBrush *p_brush, const LevelBrush::EdgeKey &p_edge) {
	// Replace the edge selection on this brush with the collinear chain.
	Vector<LevelBrush::EdgeKey> chain = p_brush->get_edge_chain(p_edge);
	HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> &set = _edge_set(p_brush);
	set.clear();
	for (const LevelBrush::EdgeKey &e : chain) {
		set.insert(e);
	}
	_update_overlays();
}

void LevelEditorScreen::_update_overlays() {
	for (int i = 0; i < 4; i++) {
		viewports[i]->set_grid_mesh_size(grid_size);
		viewports[i]->queue_overlay_redraw();
		// Tool previews (bevel ants) draw on the PreviewOverlay - keep it in sync
		// on state changes (arm/disarm/dock edits). Cheap: it only draws when
		// there's a drop or preview active.
		viewports[i]->_queue_preview_redraw();
	}
	_update_gizmo_3d();
	_update_outlines();
	_update_menu_states();
}

void LevelEditorScreen::_refresh_map() {
	if (current_map) {
		current_map->refresh();
	}
	_update_overlays();
}

bool LevelEditorScreen::_pick_face(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, int &r_face, Vector3 &r_hit) const {
	if (!current_map) {
		return false;
	}
	Vector3 ro = p_camera->project_ray_origin(p_screen);
	Vector3 rd = p_camera->project_ray_normal(p_screen).normalized();

	real_t best = (real_t)Math::INF;
	LevelBrush *best_brush = nullptr;
	int best_face = -1;
	Vector3 best_hit;

	Vector<LevelBrush *> brushes = current_map->get_brushes();
	for (LevelBrush *brush : brushes) {
		// Ray in brush-local space.
		Transform3D inv = brush->get_global_transform().affine_inverse();
		Vector3 lro = inv.xform(ro);
		Vector3 lrd = inv.basis.xform(rd).normalized();

		real_t d;
		int f = brush->ray_intersect(lro, lrd, d);
		if (f >= 0) {
			// Distance in world space: recompute from the world hit point.
			Vector3 world_hit = brush->get_global_transform().xform(lro + lrd * d);
			real_t wd = (world_hit - ro).length();
			if (wd < best) {
				best = wd;
				best_brush = brush;
				best_face = f;
				best_hit = world_hit;
			}
		}
	}
	if (best_brush) {
		r_brush = best_brush;
		r_face = best_face;
		r_hit = best_hit;
		return true;
	}
	return false;
}

bool LevelEditorScreen::_pick_vertex(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, int &r_vertex) const {
	if (!current_map) {
		return false;
	}

	real_t best = LevelEditorHandles::VERTEX_PICK_TOL * EDSCALE; // pixels.
	bool found = false;
	int best_v = -1;
	LevelBrush *best_brush = nullptr;

	Vector<LevelBrush *> brushes = current_map->get_brushes();
	for (LevelBrush *brush : brushes) {
		Transform3D gt = brush->get_global_transform();
		for (int i = 0; i < brush->get_vertex_count(); i++) {
			Vector3 w = gt.xform(brush->get_vertex(i));
			if (p_camera->is_position_behind(w)) {
				continue;
			}
			Vector2 sp = p_camera->unproject_position(w);
			real_t d = sp.distance_to(p_screen);
			if (d < best) {
				best = d;
				best_v = i;
				best_brush = brush;
				found = true;
			}
		}
	}
	if (found) {
		r_brush = best_brush;
		r_vertex = best_v;
	}
	return found;
}

bool LevelEditorScreen::_pick_edge(Camera3D *p_camera, const Vector2 &p_screen, LevelBrush *&r_brush, LevelBrush::EdgeKey &r_edge) const {
	if (!current_map) {
		return false;
	}

	real_t best = LevelEditorHandles::EDGE_PICK_TOL * EDSCALE; // pixels.
	bool found = false;
	LevelBrush::EdgeKey best_edge;
	LevelBrush *best_brush = nullptr;

	Vector<LevelBrush *> brushes = current_map->get_brushes();
	for (LevelBrush *brush : brushes) {
		Transform3D gt = brush->get_global_transform();
		const HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> &edges = brush->get_edges();
		for (const LevelBrush::EdgeKey &e : edges) {
			Vector3 wa = gt.xform(brush->get_vertex(e.a));
			Vector3 wb = gt.xform(brush->get_vertex(e.b));
			if (p_camera->is_position_behind(wa) || p_camera->is_position_behind(wb)) {
				continue;
			}
			Vector2 sa = p_camera->unproject_position(wa);
			Vector2 sb = p_camera->unproject_position(wb);
			real_t d = closest_point_on_segment_2d(sa, sb, p_screen).distance_to(p_screen);
			if (d < best) {
				best = d;
				best_edge = e;
				best_brush = brush;
				found = true;
			}
		}
	}
	if (found) {
		r_brush = best_brush;
		r_edge = best_edge;
	}
	return found;
}

void LevelEditorScreen::_update_hover(LevelEditorViewport *p_vp, const Vector2 &p_mouse) {
	// Cheap early-out: re-picking every motion event ray-tests every brush
	// triangle and repaints all 4 overlays. The pick only matters once the
	// cursor has moved a few pixels (same throttle as the material-drop
	// probe, GOTCHAS #35).
	if (hover_last_vp == p_vp && hover_last_pick.x != Math::INF && hover_last_pick.distance_squared_to(p_mouse) < LevelEditorHandles::DROP_REPROBE_DIST_SQ) {
		return;
	}
	hover_last_pick = p_mouse;
	hover_last_vp = p_vp;

	LevelBrush *old_brush = hover_brush;
	int old_face = hover_face;
	LevelBrush::EdgeKey old_edge = hover_edge;
	bool old_has_edge = has_hover_edge;
	int old_vertex = hover_vertex;
	bool old_has_vertex = has_hover_vertex;

	hover_brush = nullptr;
	hover_face = -1;
	has_hover_edge = false;
	has_hover_vertex = false;

	Camera3D *cam = p_vp->get_camera();
	switch (selection_target) {
		case TARGET_MESH: {
			Vector3 hit;
			int f;
			_pick_face(cam, p_mouse, hover_brush, f, hit);
		} break;
		case TARGET_FACE: {
			Vector3 hit;
			_pick_face(cam, p_mouse, hover_brush, hover_face, hit);
		} break;
		case TARGET_VERTEX: {
			has_hover_vertex = _pick_vertex(cam, p_mouse, hover_brush, hover_vertex);
		} break;
		case TARGET_EDGE: {
			has_hover_edge = _pick_edge(cam, p_mouse, hover_brush, hover_edge);
		} break;
		default:
			break;
	}
	// Element targets: when no element is under the cursor, still resolve the
	// brush under it (face pick) so all of its elements can be shown.
	if ((selection_target == TARGET_VERTEX && !has_hover_vertex) ||
			(selection_target == TARGET_EDGE && !has_hover_edge)) {
		Vector3 hit;
		int f;
		LevelBrush *b = nullptr;
		if (_pick_face(cam, p_mouse, b, f, hit)) {
			hover_brush = b;
		}
	}

	// Repaint only when the pick actually changed - moving the mouse across
	// the same brush/element keeps the same highlight.
	if (hover_brush == old_brush && hover_face == old_face &&
			has_hover_edge == old_has_edge && (!has_hover_edge || hover_edge == old_edge) &&
			has_hover_vertex == old_has_vertex && (!has_hover_vertex || hover_vertex == old_vertex)) {
		return;
	}
	_update_overlays();
}

void LevelEditorScreen::forward_input(Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	if (!current_map) {
		return; // No map yet - tool input is disabled.
	}
	LevelEditorViewport *vp = nullptr;
	for (int i = 0; i < 4; i++) {
		if (viewports[i]->get_camera() == p_camera) {
			vp = viewports[i];
			break;
		}
	}
	if (!vp) {
		return;
	}

	// Camera navigation (RMB freelook / MMB pan) owns the mouse: skip ALL tool
	// input - hover picks ray-test every brush per motion event and their
	// results are stale the instant the camera moves anyway. Reset the hover
	// throttle so the first hover after navigation re-picks against the new
	// camera position.
	if (vp->is_navigating()) {
		hover_last_pick = Vector2(Math::INF, Math::INF);
		return;
	}

	// Delete/brackets are handled by LevelEditorScreen::shortcut_input (so
	// the scene dock can't hijack them); skip them here to avoid double-
	// handling when this viewport has focus.
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() &&
			(key->get_keycode() == Key::KEY_DELETE || key->get_keycode() == Key::BRACKETLEFT || key->get_keycode() == Key::BRACKETRIGHT)) {
		return;
	}

	// Dispatch to the per-tool input handlers in priority order. Each handler
	// owns one tool's input and returns true when it consumed the event.
	// The order matters: select handles beat the move gizmo, the gizmo beats
	// the creation tools, and selection clicks come last.
	if (_select_handles_input(vp, p_camera, p_event)) {
		return;
	}
	if (_rotate_input(vp, p_camera, p_event)) {
		return;
	}
	if (_gizmo_input(vp, p_camera, p_event)) {
		return;
	}
	if (_brush_input(vp, p_camera, p_event)) {
		return;
	}
	if (_clip_input(vp, p_camera, p_event)) {
		return;
	}
	if (_mirror_input(vp, p_camera, p_event)) {
		return;
	}
	if (_selection_input(vp, p_camera, p_event)) {
		return;
	}
}

void LevelEditorScreen::_paint_select_at(Camera3D *p_camera, const Vector2 &p_screen) {
	// Add (never remove) the element under the cursor, per mode. Called on the
	// initial click and on every mouse-motion while the button is held, so
	// dragging paints a trail of selected elements.
	switch (selection_target) {
		case TARGET_VERTEX: {
			LevelBrush *brush = nullptr;
			int v;
			if (_pick_vertex(p_camera, p_screen, brush, v)) {
				_vertex_set(brush).insert(v);
			}
		} break;
		case TARGET_EDGE: {
			LevelBrush *brush = nullptr;
			LevelBrush::EdgeKey e;
			if (_pick_edge(p_camera, p_screen, brush, e)) {
				_edge_set(brush).insert(e);
			}
		} break;
		case TARGET_FACE: {
			Vector3 hit;
			LevelBrush *brush = nullptr;
			int f;
			if (_pick_face(p_camera, p_screen, brush, f, hit)) {
				_face_set(brush).insert(f);
			}
		} break;
		default:
			break;
	}
}

void LevelEditorScreen::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			_update_mode_icons();
			_update_warning_color();
		} break;
		case NOTIFICATION_PROCESS: {
			// Drop dangling selection if the brush was deleted externally.
			bool pruned = false;
			for (int i = selected_brushes.size() - 1; i >= 0; i--) {
				if (!selected_brushes[i]->is_inside_tree()) {
					selected_brushes.remove_at(i);
					pruned = true;
				}
			}
			if (selected_brush && !selected_brush->is_inside_tree()) {
				selected_brush = selected_brushes.is_empty() ? nullptr : selected_brushes[selected_brushes.size() - 1];
				pruned = true;
			}
			// Prune element selections whose brush was deleted.
			pruned = _prune_dead_brushes(selected_faces) || pruned;
			pruned = _prune_dead_brushes(selected_edges) || pruned;
			pruned = _prune_dead_brushes(selected_vertices) || pruned;
			if (pruned) {
				_update_overlays();
			}
			if (current_map && !current_map->is_inside_tree()) {
				current_map = nullptr;
				_clear_selection();
				_update_map_ui();
			}
			// The 3D gizmo's screen-size compensation depends on the camera;
			// refresh it per frame (cheap early-outs when hidden/unchanged in
			// practice: the transform set is one node).
			_update_gizmo_3d();
			// 3D outlines must poll per frame: undo/redo restores brush data
			// through the property system (no editor code path runs), so the
			// geometry-version check is the only thing that catches it. Cheap:
			// version compares + material checks; meshes rebuild only on change.
			// When geometry DID change, also repaint the 2D overlay (AABB handles,
			// selection, hover) - it only redraws on explicit _update_overlays().
			if (_update_outlines()) {
				for (int i = 0; i < 4; i++) {
					viewports[i]->queue_overlay_redraw();
				}
			}
			// Advance the bevel preview's marching-ants and repaint while armed.
			// The preview draws on the cheap PreviewOverlay, so this only repaints
			// that overlay - not the whole scene overlay (GOTCHAS #33).
			if (armed_action == ACTION_BEVEL_EDGES && tool_preview.id == PREVIEW_BEVEL) {
				const double new_phase = Math::fposmod(preview_ants_phase + get_process_delta_time() * LevelEditorHandles::ANTS_SPEED, (double)LevelEditorHandles::ANTS_PERIOD);
				if (Math::floor(new_phase) != Math::floor(preview_ants_phase)) {
					for (int i = 0; i < 4; i++) {
						viewports[i]->_queue_preview_redraw();
					}
				}
				preview_ants_phase = new_phase;
			}
		} break;
	}
}

// ---- Drawing --------------------------------------------------------------

void LevelEditorScreen::_draw_viewport_overlay(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (!current_map) {
		return;
	}

	// Base brush outlines are 3D line meshes now (_update_outlines) - the 2D
	// pass re-projected every edge of every brush every frame (drag hotspot).
	// What remains here is the cheap per-element 2D feedback: hovered element,
	// vertex markers, face fills, selection, gizmos, tool previews.
	// Element targets: the hovered brush - and any brush with selected elements
	// of the current target - gets green hover elements and vertex markers.
	if (_is_element_target()) {
		// Collect the brushes to highlight: hovered + those with a selection.
		LocalVector<LevelBrush *> highlight;
		if (hover_brush) {
			highlight.push_back(hover_brush);
		}
		auto add_selected_brushes = [&](auto &p_map) {
			for (const auto &E : p_map) {
				bool dup = false;
				for (LevelBrush *b : highlight) {
					if (b == E.key) {
						dup = true;
						break;
					}
				}
				if (!dup) {
					highlight.push_back(E.key);
				}
			}
		};
		switch (selection_target) {
			case TARGET_VERTEX:
				add_selected_brushes(selected_vertices);
				break;
			case TARGET_FACE:
				add_selected_brushes(selected_faces);
				break;
			case TARGET_EDGE:
				add_selected_brushes(selected_edges);
				break;
			default:
				break;
		}

		const real_t vs = LevelEditorHandles::VERTEX_SIZE * EDSCALE; // half-size, normal.
		const real_t vs_hot = LevelEditorHandles::VERTEX_HOT_SIZE * EDSCALE; // half-size, hovered.
		for (LevelBrush *brush : highlight) {
			if (selection_target == TARGET_EDGE && brush == hover_brush && has_hover_edge) {
				Transform3D gt = brush->get_global_transform();
				Vector2 a, b;
				if (p_vp->project_segment(gt.xform(brush->get_vertex(hover_edge.a)), gt.xform(brush->get_vertex(hover_edge.b)), a, b)) {
					if (brush->get_open_edges().has(hover_edge)) {
						LevelHelpers::draw_dashed_line_clipped(p_canvas, a, b, LevelEditorColors::HOVER_ELEMENT, 2.5, LevelEditorHandles::OPEN_EDGE_DASH_PX * EDSCALE);
					} else {
						p_canvas->draw_line(a, b, LevelEditorColors::HOVER_ELEMENT, 2.5);
					}
				}
			}
			if (selection_target == TARGET_FACE && brush == hover_brush && hover_face >= 0) {
				// Hovered face: green fill + outline.
				LocalVector<int> poly = brush->get_face(hover_face);
				if (poly.size() >= 3) {
					Transform3D gt = brush->get_global_transform();
					Vector<Vector3> world;
					for (int idx : poly) {
						world.push_back(gt.xform(brush->get_vertex(idx)));
					}
					PackedVector2Array pts;
					if (p_vp->project_polygon(world, pts)) {
						// The projected polygon can be degenerate (face viewed edge-on,
						// or a concave/self-intersecting outline after vertex edits) -
						// pre-flight the same triangulation the renderer does and skip
						// the fill if it fails (the outline still draws).
						if (!Geometry2D::triangulate_polygon(pts).is_empty()) {
							p_canvas->draw_colored_polygon(pts, LevelEditorColors::HOVER_FACE_FILL);
						}
						for (int i = 0; i < pts.size(); i++) {
							p_canvas->draw_line(pts[i], pts[(i + 1) % pts.size()], LevelEditorColors::HOVER_ELEMENT, 2.0);
						}
					}
				}
			}
			if (selection_target != TARGET_VERTEX) {
				continue; // Only the vertex target shows vertex markers.
			}
			// All vertices in bright green; the vertex under the cursor is
			// slightly larger.
			Color vert_col = LevelEditorColors::HOVER_ELEMENT;
			Transform3D gt = brush->get_global_transform();
			for (int i = 0; i < brush->get_vertex_count(); i++) {
				Vector2 sp;
				if (p_vp->project(gt.xform(brush->get_vertex(i)), sp)) {
					bool hot = (brush == hover_brush && has_hover_vertex && i == hover_vertex);
					LevelHelpers::draw_vertex_marker(p_canvas, sp, hot ? vs_hot : vs, vert_col);
				}
			}
		}
	}
	_draw_drag_feedback(p_vp, p_canvas);
	_draw_ghost(p_vp, p_canvas);
	_draw_selection(p_vp, p_canvas);
	_draw_select_handles(p_vp, p_canvas);
	_draw_rotate_gizmo(p_vp, p_canvas);
	_draw_clip(p_vp, p_canvas);
	_draw_mirror(p_vp, p_canvas);
	// Rebuild the bevel preview cache here (cheap hash check); the preview
	// itself draws on the PreviewOverlay so its ants don't repaint this overlay.
	_bevel_preview_rebuild();
}

void LevelEditorScreen::_draw_drag_feedback(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (!dragging || !drag_active || !drag_viewport || ghost_active) {
		return;
	}

	Vector3 mins, maxs;
	_compute_drag_aabb(mins, maxs);

	Color col = LevelEditorColors::GHOST;
	if (brush_type == BRUSH_SPHERE) {
		// Sphere drag: draw the cached wireframe (rebuilt only when the drag
		// AABB or sides change) instead of a per-frame setup_sphere.
		_rebuild_sphere_preview(AABB(mins, maxs - mins));
		_draw_sphere_preview(p_vp, p_canvas, col);
	} else {
		// Box edges are constant - draw them straight from the AABB (the old
		// per-paint memnew(LevelBrush)+setup_box ran x4 viewports per frame).
		Vector3 corners[8];
		LevelHelpers::aabb_corners(AABB(mins, maxs - mins), corners);
		for (const auto &edge : LevelHelpers::AABB_EDGE_IDX) {
			Vector2 a, b;
			if (p_vp->project(corners[edge[0]], a) && p_vp->project(corners[edge[1]], b)) {
				p_canvas->draw_line(a, b, col, 2.0);
			}
		}
	}

	// Show the in-progress box's dimensions too.
	_draw_dim_labels(p_vp, p_canvas, AABB(mins, maxs - mins));

	if (p_vp == drag_viewport && p_vp->get_view_type() != LevelEditorViewport::VIEW_PERSPECTIVE) {
		Vector2 s0, s1;
		if (p_vp->project(drag_start, s0) && p_vp->project(drag_current, s1)) {
			Rect2 r(s0, s1 - s0);
			r = r.abs();
			p_canvas->draw_rect(r, LevelEditorColors::DRAG_RECT, false, 1.0);
		}
	}
}

void LevelEditorScreen::_draw_selection(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (!current_map) {
		return;
	}

	// Faces.
	Color face_col = LevelEditorColors::SELECTED_FACE_FILL;
	Color face_outline = LevelEditorColors::SELECTED_ELEMENT;
	for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_faces) {
		Transform3D gt = E.key->get_global_transform();
		for (int f : E.value) {
			LocalVector<int> poly = E.key->get_face(f);
			if (poly.size() < 3) {
				continue;
			}
			Vector<Vector3> world;
			for (int idx : poly) {
				world.push_back(gt.xform(E.key->get_vertex(idx)));
			}
			PackedVector2Array pts;
			if (p_vp->project_polygon(world, pts)) {
				// Same degenerate-projection guard as the hover fill.
				if (!Geometry2D::triangulate_polygon(pts).is_empty()) {
					p_canvas->draw_colored_polygon(pts, face_col);
				}
				for (int i = 0; i < pts.size(); i++) {
					p_canvas->draw_line(pts[i], pts[(i + 1) % pts.size()], face_outline, 2.0);
				}
			}
		}
	}

	// Edges (selected: same orange as selected-face outlines). Hidden while
	// a bevel is armed - the cyan preview shows the bevel target instead.
	if (armed_action != ACTION_BEVEL_EDGES) {
		Color edge_col = LevelEditorColors::SELECTED_ELEMENT;
		for (const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E : selected_edges) {
			Transform3D gt = E.key->get_global_transform();
			const HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> &open_edges = E.key->get_open_edges();
			for (const LevelBrush::EdgeKey &e : E.value) {
				Vector2 a, b;
				if (p_vp->project_segment(gt.xform(E.key->get_vertex(e.a)), gt.xform(E.key->get_vertex(e.b)), a, b)) {
					if (open_edges.has(e)) {
						LevelHelpers::draw_dashed_line_clipped(p_canvas, a, b, edge_col, 3.0, LevelEditorHandles::OPEN_EDGE_DASH_PX * EDSCALE);
					} else {
						p_canvas->draw_line(a, b, edge_col, 3.0);
					}
				}
			}
		}
	}

	// Vertices (selected: same orange as selected-face outlines).
	Color vert_col = LevelEditorColors::SELECTED_ELEMENT;
	const real_t sel_vs = LevelEditorHandles::VERTEX_HOT_SIZE * EDSCALE;
	for (const KeyValue<LevelBrush *, HashSet<int>> &E : selected_vertices) {
		Transform3D gt = E.key->get_global_transform();
		for (int v : E.value) {
			Vector2 sp;
			if (p_vp->project(gt.xform(E.key->get_vertex(v)), sp)) {
				LevelHelpers::draw_vertex_marker(p_canvas, sp, sel_vs, vert_col);
			}
		}
	}
}
