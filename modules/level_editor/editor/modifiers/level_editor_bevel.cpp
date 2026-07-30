/**************************************************************************/
/*  level_editor_bevel.cpp                                                */
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

// Bevel edges (Edge target): the armed-action plumbing (arm -> dock edits
// Width/Steps/Shape -> Enter applies / Esc cancels; currently bevel is the
// only armed action), the bevel apply itself, and the marching-ants preview
// that shows the bevel result live while armed. These are LevelEditorScreen
// member functions, split out of level_editor_screen.cpp for organization.

#include "../../level_constants.h"
#include "../dock/level_editor_dock.h"
#include "../level_editor_screen.h"
#include "../../level_helpers.h"

#include "core/templates/hashfuncs.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/themes/editor_scale.h"

// --- Armed action plumbing (dock settings, Enter applies) ---

void LevelEditorScreen::_arm_action(ArmedAction p_action) {
	if (armed_action == p_action) {
		return; // Keep values across a re-click (acts as a toggle-off guard).
	}
	armed_action = p_action;
	if (dock) {
		dock->refresh();
	}
	_update_overlays();
}

void LevelEditorScreen::_action_cancel_armed() {
	if (armed_action == ACTION_NONE) {
		return;
	}
	armed_action = ACTION_NONE;
	armed_values.clear();
	tool_preview = ToolPreview();
	if (dock) {
		dock->refresh();
	}
	_update_overlays();
}

double LevelEditorScreen::get_armed_value(const StringName &p_id, double p_fallback) const {
	const double *v = armed_values.getptr(p_id);
	return v ? *v : p_fallback;
}

void LevelEditorScreen::set_armed_value(const StringName &p_id, double p_value) {
	armed_values[p_id] = p_value;
	_update_overlays(); // Live previews follow dock edits.
}

void LevelEditorScreen::_action_apply_armed() {
	switch (armed_action) {
		case ACTION_BEVEL_EDGES:
			_action_bevel_edges();
			break;
		default:
			break;
	}
	_action_cancel_armed();
}

// --- Bevel apply ---

void LevelEditorScreen::_action_bevel_edges(bool p_quick) {
	if (!current_map || selected_edges.is_empty()) {
		return;
	}

	if (p_quick) {
		// F in Edge mode: bevel immediately with the current grid size and
		// default steps/shape (no arming, no dock round-trip).
		if (selection_target != TARGET_EDGE || armed_action != ACTION_NONE) {
			return;
		}
		_bevel_edges_apply(grid_size, 0, LevelBrushConstants::BEVEL_DEFAULT_SHAPE);
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
	const real_t shape = get_armed_value(StringName("shape"), LevelBrushConstants::BEVEL_DEFAULT_SHAPE);
	_bevel_edges_apply(width, steps, shape);
}

void LevelEditorScreen::_bevel_edges_apply(real_t p_width, int p_steps, real_t p_shape) {
	if (!current_map || selected_edges.is_empty()) {
		return;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
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
		if (working->bevel_edges_profiled(edges, p_width, p_steps, p_shape) > 0) {
			// Create the undo action lazily: an uncommitted create_action (no
			// brush beveled anything) leaves a dangling action in the manager.
			if (!did) {
				undo_redo->create_action(TTR("Bevel Edges"));
			}
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

// --- Tool previews ---

void LevelEditorScreen::_bevel_preview_rebuild() {
	if (armed_action != ACTION_BEVEL_EDGES || selected_edges.size() != 1) {
		if (tool_preview.id == PREVIEW_BEVEL) {
			tool_preview = ToolPreview();
		}
		return;
	}

	const real_t width = get_armed_value(StringName("width"), grid_size);
	const int steps = (int)get_armed_value(StringName("steps"), 0.0);
	const real_t shape = get_armed_value(StringName("shape"), LevelBrushConstants::BEVEL_DEFAULT_SHAPE);
	const KeyValue<LevelBrush *, HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher>> &E = *selected_edges.begin();

	// Cache key: everything the preview depends on (brush, selection,
	// armed values, AND current geometry - gizmo edits while armed must
	// invalidate).
	uint32_t h = hash_murmur3_one_64((uint64_t)E.key);
	h = hash_murmur3_one_32((uint64_t)E.value.size(), h);
	for (const LevelBrush::EdgeKey &e : E.value) {
		h = hash_murmur3_one_32((uint32_t)e.a, h);
		h = hash_murmur3_one_32((uint32_t)e.b, h);
	}
	const PackedVector3Array cur_verts = E.key->get_vertices_data();
	for (const Vector3 &v : cur_verts) {
		h = hash_murmur3_one_real(v.x, h);
		h = hash_murmur3_one_real(v.y, h);
		h = hash_murmur3_one_real(v.z, h);
	}
	h = hash_murmur3_one_real((double)width, h);
	h = hash_murmur3_one_32((uint32_t)steps, h);
	h = hash_murmur3_one_real((double)shape, h);
	if (tool_preview.id == PREVIEW_BEVEL && tool_preview.cache_hash == h) {
		return;
	}

	tool_preview = ToolPreview();

	Vector<LevelBrush::EdgeKey> edges;
	for (const LevelBrush::EdgeKey &e : E.value) {
		edges.push_back(e);
	}

	LevelBrush *working = E.key->duplicate_brush();
	if (working->bevel_edges_profiled(edges, width, steps, shape) > 0) {
		// Draw only edges the bevel CREATED (present in the result but not
		// in the original brush) - the original edges stay drawn by the
		// normal outline pass, and consumed edges (steps=0 centerline) must
		// not ghost back in here.
		const HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> &orig_edges = E.key->get_edges();
		const HashSet<LevelBrush::EdgeKey, LevelBrush::EdgeKeyHasher> &result_edges = working->get_edges();
		for (const LevelBrush::EdgeKey &e : result_edges) {
			const Vector3 va = working->get_vertex(e.a);
			const Vector3 vb = working->get_vertex(e.b);
			bool existed = false;
			for (const LevelBrush::EdgeKey &oe : orig_edges) {
				if (E.key->get_vertex(oe.a).is_equal_approx(va) && E.key->get_vertex(oe.b).is_equal_approx(vb)) {
					existed = true;
					break;
				}
			}
			if (!existed) {
				tool_preview.lines.push_back(va);
				tool_preview.lines.push_back(vb);
			}
		}
		tool_preview.id = PREVIEW_BEVEL;
		tool_preview.brush = E.key;
		tool_preview.cache_hash = h;
	}
	memdelete(working);
}

void LevelEditorScreen::_draw_tool_preview(LevelEditorViewport *p_vp, Control *p_canvas) {
	if (tool_preview.id == PREVIEW_NONE || !tool_preview.brush || tool_preview.lines.is_empty()) {
		return;
	}
	const Transform3D gt = tool_preview.brush->get_global_transform();

	if (tool_preview.id == PREVIEW_BEVEL) {
		// Yellow marching-ants (ACTION_PREVIEW). The phase runs continuously
		// across segments so dashes turn corners; the helper clips near-plane-
		// asymptotic projections to the overlay rect.
		const real_t dash_len = LevelEditorHandles::ANTS_DASH * EDSCALE;
		const real_t period = dash_len * 2.0;
		const Rect2 visible_rect = LevelHelpers::overlay_visible_rect(p_canvas);
		real_t phase = Math::fposmod((real_t)preview_ants_phase * EDSCALE, period);
		for (uint32_t i = 0; i + 1 < tool_preview.lines.size(); i += 2) {
			Vector2 a, b;
			// project_segment clips near-plane-crossing endpoints instead of
			// dropping the whole segment (GOTCHAS #22), like every other edge draw.
			if (p_vp->project_segment(gt.xform(tool_preview.lines[i]), gt.xform(tool_preview.lines[i + 1]), a, b)) {
				phase = LevelHelpers::draw_marching_segment(p_canvas, a, b, phase, 2.0, LevelEditorColors::ACTION_PREVIEW, dash_len, visible_rect);
			}
		}
		return;
	}

	Color col = LevelEditorColors::GHOST;
	for (uint32_t i = 0; i + 1 < tool_preview.lines.size(); i += 2) {
		Vector2 a, b;
		if (p_vp->project_segment(gt.xform(tool_preview.lines[i]), gt.xform(tool_preview.lines[i + 1]), a, b)) {
			p_canvas->draw_line(a, b, col, 2.0);
		}
	}
}
