/**************************************************************************/
/*  game_view_plugin.h                                                    */
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

#include "editor/debugger/editor_debugger_node.h"
#include "editor/editor_main_screen.h"
#include "editor/plugins/editor_debugger_plugin.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/debugger/scene_debugger.h"
#include "scene/gui/box_container.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/split_container.h"

class EmbeddedProcess;
class VSeparator;
class WindowWrapper;
class ScriptEditorDebugger;
class GameViewPlugin;
class GameView;

class GameViewDebugger : public EditorDebuggerPlugin {
	GDCLASS(GameViewDebugger, EditorDebuggerPlugin);

private:
	Vector<Ref<EditorDebuggerSession>> sessions;

	bool is_feature_enabled = true;
	int node_type = RuntimeNodeSelect::NODE_TYPE_NONE;
	bool selection_visible = true;
	int select_mode = RuntimeNodeSelect::SELECT_MODE_SINGLE;
	bool mute_audio = false;
	EditorDebuggerNode::CameraOverride camera_override_mode = EditorDebuggerNode::OVERRIDE_INGAME;
	GameView *game_view = nullptr;

	void _session_started(Ref<EditorDebuggerSession> p_session, GameView *p_game_view);
	void _session_stopped();

	void _feature_profile_changed();

protected:
	static void _bind_methods();

public:
	void set_suspend(bool p_enabled);
	void next_frame();

	void set_node_type(int p_type);
	void set_select_mode(int p_mode);

	void set_selection_visible(bool p_visible);

	void set_debug_mute_audio(bool p_enabled);

	void set_camera_override(bool p_enabled);
	void set_camera_manipulate_mode(EditorDebuggerNode::CameraOverride p_mode);

	void reset_camera_2d_position();
	void reset_camera_3d_position();

	void set_game_view(GameView *p_game_view);

	virtual void setup_session(int p_session_id) override;

	GameViewDebugger();
};

class GameView : public VBoxContainer {
	GDCLASS(GameView, VBoxContainer);

	enum {
		CAMERA_RESET_2D,
		CAMERA_RESET_3D,
		CAMERA_MODE_INGAME,
		CAMERA_MODE_EDITORS,
		EMBED_RUN_GAME_EMBEDDED,
		EMBED_MAKE_FLOATING_ON_PLAY,
		WINDOW_SELECT_MAIN,
		WINDOW_SELECT_CUSTOM,
		GAME_VIEW_SINGLE,
		GAME_VIEW_DUAL,
	};

	enum EmbedSizeMode {
		SIZE_MODE_FIXED,
		SIZE_MODE_KEEP_ASPECT,
		SIZE_MODE_STRETCH,
	};

	Ref<GameViewDebugger> debugger;
	GameViewPlugin *plugin = nullptr;
	WindowWrapper *window_wrapper = nullptr;

	String setting_prefix = "";
	Dictionary *state_dictionary;

	bool is_feature_enabled = true;
	bool subwindow_embedding_available = true;
	bool debugger_controls_enabled = true;
	int active_sessions = 0;
	int screen_index_before_start = -1;
	ScriptEditorDebugger *embedded_script_debugger = nullptr;

	bool embed_on_play = true;
	bool make_floating_on_play = true;
	EmbedSizeMode embed_size_mode = SIZE_MODE_FIXED;
	bool paused = false;
	Size2 size_paused;
	bool show_embedded_window_title = false;
	String embedded_window_title = "";

	Rect2i floating_window_rect;
	int floating_window_screen = -1;

	bool debug_mute_audio = false;

	Button *suspend_button = nullptr;
	Button *next_frame_button = nullptr;
	VSeparator *time_buttons_separator = nullptr;

	Button *node_type_button[RuntimeNodeSelect::NODE_TYPE_MAX];
	VSeparator *node_type_separator = nullptr;
	Button *select_mode_button[RuntimeNodeSelect::SELECT_MODE_MAX];
	VSeparator *select_mode_separator = nullptr;

	Button *hide_selection = nullptr;
	VSeparator *hide_selection_separator = nullptr;

	Button *debug_mute_audio_button = nullptr;
	VSeparator *debug_mute_audio_separator = nullptr;

	Button *camera_override_button = nullptr;
	MenuButton *camera_override_menu = nullptr;

	VSeparator *embedding_separator = nullptr;
	Button *fixed_size_button = nullptr;
	Button *keep_aspect_button = nullptr;
	Button *stretch_button = nullptr;
	MenuButton *embed_options_menu = nullptr;
	Label *game_size_label = nullptr;
	Panel *panel = nullptr;
	OptionButton *window_select_dropdown = nullptr;
	LineEdit *window_select_text = nullptr;
	EmbeddedProcess *embedded_process = nullptr;
	Label *state_label = nullptr;

	void _sessions_changed();

	void _update_debugger_buttons();
	void _update_window_selector_controls();

	void _handle_shortcut_requested(int p_embed_action);
	void _toggle_suspend_button();
	void _suspend_button_toggled(bool p_pressed);

	void _node_type_pressed(int p_option);
	void _select_mode_pressed(int p_option);
	void _embed_options_menu_menu_id_pressed(int p_id);
	void _size_mode_button_pressed(int size_mode);
	void _select_window_dropdown_pressed(int p_id);
	void _window_select_text_changed(String p_text);

	void _play_pressed();
	void _stop_pressed();
	void _embedding_completed();
	void _embedding_failed();
	void _embedded_process_updated();
	void _embedded_process_focused();
	void _editor_or_project_settings_changed();

	void _update_ui();
	void _update_embed_menu_options();

	void _hide_selection_toggled(bool p_pressed);

	void _debug_mute_audio_button_pressed();

	void _camera_override_button_toggled(bool p_pressed);
	void _camera_override_menu_id_pressed(int p_id);

	void _window_close_request();
	void _update_floating_window_settings();
	void _attach_script_debugger();
	void _detach_script_debugger();
	void _remote_window_title_changed(String title);

	void _debugger_breaked(bool p_breaked, bool p_can_debug);

	void _feature_profile_changed();

protected:
	void _notification(int p_what);

public:
	void set_is_feature_enabled(bool p_enabled);
	void set_time_buttons_enabled(bool p_enabled);
	void set_debugger_controls_enabled(bool p_enabled);
	void set_embedded_window_title(String p_embedded_window_title);
	String get_embedded_window_title();
	bool get_embed_on_play();
	bool get_make_floating_on_play();
	bool is_view_embedding();

	EmbeddedProcess *get_embedded_process();
	GameViewPlugin *get_plugin();

	void update_all_ui();
	void update_embed_window_size();
	void show_update_window_wrapper();

	void set_window_layout(Ref<ConfigFile> p_layout);
	void get_window_layout(Ref<ConfigFile> p_layout);

	GameView(Ref<GameViewDebugger> p_debugger, GameViewPlugin *p_plugin, WindowWrapper *p_wrapper, String p_setting_prefix);
};

class GameViewPlugin : public EditorPlugin {
	GDCLASS(GameViewPlugin, EditorPlugin);

	inline static GameViewPlugin *singleton = nullptr;

	VSplitContainer *game_view_layout = nullptr;

#ifndef ANDROID_ENABLED
	GameView *game_view = nullptr;
	WindowWrapper *window_wrapper = nullptr;
	GameView *top_game_view = nullptr;
	WindowWrapper *top_window_wrapper = nullptr;
#endif // ANDROID_ENABLED

	Ref<GameViewDebugger> top_debugger;
	Ref<GameViewDebugger> debugger;

	String last_editor;

	bool is_feature_enabled = true;
	bool dual_view_enabled = false;

	void _feature_profile_changed();
	void _window_visibility_changed(bool p_visible);
	void _save_last_editor(const String &p_editor);
	void _focus_another_editor();
	bool _is_window_wrapper_enabled() const;

	static void _instance_starting_static(int p_idx, List<String> &r_arguments);
	void _instance_starting(int p_idx, List<String> &r_arguments);
	void _update_arguments_for_game_instance(int p_idx, List<String> &r_arguments);

protected:
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return TTRC("Game"); }

	enum EmbedAvailability {
		EMBED_AVAILABLE,
		EMBED_NOT_AVAILABLE_FEATURE_NOT_SUPPORTED,
		EMBED_NOT_AVAILABLE_MINIMIZED,
		EMBED_NOT_AVAILABLE_MAXIMIZED,
		EMBED_NOT_AVAILABLE_FULLSCREEN,
		EMBED_NOT_AVAILABLE_SINGLE_WINDOW_MODE,
		EMBED_NOT_AVAILABLE_PROJECT_DISPLAY_DRIVER,
	};

	bool has_main_screen() const override { return true; }
	virtual void edit(Object *p_object) override {}
	virtual bool handles(Object *p_object) const override { return false; }
	virtual void selected_notify() override;

	Ref<GameViewDebugger> get_debugger() const { return debugger; }
	GameView *get_game_view_with_main_screen_embed();
	GameView *get_game_view_with_selected_window(String p_window_title);
	EmbedAvailability get_embed_available();
	GameView *get_top_game_view();
	GameView *get_main_game_view();

	void game_view_changed_window_target();
	void enable_dual_game_view(bool p_enabled);
	void determine_dual_view_visible();

#ifndef ANDROID_ENABLED
	virtual void make_visible(bool p_visible) override;
	virtual void set_window_layout(Ref<ConfigFile> p_layout) override;
	virtual void get_window_layout(Ref<ConfigFile> p_layout) override;
#endif // ANDROID_ENABLED

	GameViewPlugin();
};
