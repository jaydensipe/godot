# Level Editor - Audit Archive

Completed audit passes, kept for history. For CURRENT open items see
ROADMAP.md. New audits: append a dated section at the bottom and move
still-open items into ROADMAP.md.

## Audit (2026-07, pass 3) - multi-agent module sweep

Dead code removed:
- Material chain: `current_material`, `_material_changed`,
  `apply_material_from_dock`, `_action_apply_material`, Face-menu
  "Apply Material" (never assigned since the dock picker was removed;
  material system to be redesigned - see ROADMAP).
- `gizmo_drag_uniform_scale` + `_apply_gizmo_scale_uniform` (~75 lines;
  orphaned by the GOTCHAS #31 fix).
- `gizmo_drag_original_verts`, `gizmo_drag_original_position`
  (write-only single-brush-era snapshots).

Bugs fixed:
- Multi-brush gizmo move only moved the primary brush (now snapshots and
  moves all selected node positions; one undo action).
- `last_transform_tool` restore-on-leave-drawing-tool was lost
  (written, never read) - restored.
- `set_vertex` didn't notify the map (inspector edits left stale preview).
- Clip cap angular sort could normalize a zero vector (symmetric
  diagonal clip) - farthest-vert reference + perpendicular fallback.
- `ROTATE_RING_PX` called EDSCALE at static-init time - applied per use.

Deduped: `newell_normal` (3 copies), `aabb_from_points` (5 copies),
`_commit_brush_verts_undo` (4 copies), `compute_gizmo_axes` + shared
plane-handle table (gizmo pick/draw). Stale "Select mode" comments
after the TOOL_MOVE split. Constants centralized in level_constants.h:
`WINDING_SIDE_EPS`/`BEVEL_MITRE_MIN_SIN`/`BAKE_UV_SCALE` (runtime),
`DEFAULT_BRUSH_ALBEDO` (colors), and a new `LevelEditorHandles`
namespace (pick tolerances + handle sizes, replacing 10+ scattered
EDSCALE literals).

Tests added: get_face_normal invariants, ray_intersect edge cases,
diagonal clip, rewind_face_outward, mirror round-trip, compact_vertices
no-op stability.

Deferred (listed in ROADMAP.md "Deferred smells"): bevel_edges_profiled
split, constructor/_gizmo_begin_drag/_selection_input length, clip/mirror
pick duplication, bake O(m2), delete_faces orphans, default-material
clearing, mutable ghost_flat_axis, menu ID enums, bevel shape-default
duplication.

## Audit (2026-07, pass 2): fixed

- Bevel apply left `selected_edges` stale across `compact_vertices()` -
  now cleared (GOTCHAS #14 rule).
- `_set_mode` dropped `select_moving` without committing its undo -
  mid-move mode shortcut made the move un-undoable; now commits.
- `get_edge_chain` read `verts[-1]` on a default/invalid EdgeKey (UB) -
  bounds-checked.
- `delete_faces` duplicate indices half-applied the op - now deduped.
- `get_face_normal` degenerate test false-triggered on tiny faces -
  threshold squared.
- Bevel preview cache omitted brush geometry (stale after gizmo edits
  while armed) - vertex data folded into the hash.
- `_clip_plane`/`_mirror_plane` were line-identical - shared
  `_two_point_plane()`.
- Newell normal inlined twice in bevel - uses `get_face_normal()`.
- Magic 0.0005/0.999 -> `LevelBrushConstants::{PLANE_EPSILON, WELD_DIST,
  PARALLEL_DOT}` in level_brush.h.

## Audit (2026-07): bugs fixed

1. ~~Undo loses face materials after Clip/Split~~ - `_clip_apply` now
   snapshots `face_materials` too; also resets all clip state.
2. ~~Node-creation undo actions missing `add_do_reference`~~ - added in
   `_ghost_commit` and `_bake_pressed`.
3. ~~Brush-delete undo doesn't restore `owner`~~ - undo re-sets owner.
4. ~~Mid-drag mode switch drops drag/extrude state + undo~~ - `_set_mode`
   now cleanly ends gizmo/rotate/select-handle drags (committing their
   undo actions) before switching.
5. ~~Missing `release_focus()` in `_grid_size_selected`~~ - added.
6. ~~`on_scene_changed` doesn't cancel ghost/clip~~ - cancels both.
7. ~~`_clip_apply` leaves stale clip state~~ - resets everything
   `_clip_cancel` does.
8. `_action_extrude_faces` keeping `selected_faces` - VERIFIED NOT A BUG:
   `extrude_face` replaces each source face with its cap in place (no
   re-index), so the selection correctly tracks the new caps for chained
   extrudes.
9. ~~Scale pivots inconsistent~~ - `_apply_gizmo_scale` now pivots at the
   AABB center like `_apply_gizmo_scale_uniform`.
10. ~~`split_faces` welds against ANY nearby vertex~~ - welding is now
    restricted to intersection verts created by the same split call.

## Audit (2026-07): dead code / smells removed

- Consolidated triplicated AABB corner/edge/face tables into
  anonymous-namespace helpers (`aabb_corners`, `AABB_EDGE_IDX`,
  `aabb_face_center`).
- `_pick_box_handle()` shared by ghost + select-handle picking.
- `LevelEditorViewport::ray_to_view_plane()` replaced ~8 copies of the
  view-type edit-plane intersection block.
- `_commit_brush_undo()` helper replaced 10 copies of the
  vertices/faces/materials undo block.
- Removed: unused `node_3d_editor_plugin.h` include, dead `focus_on`,
  dead `gizmo_drag_original_rotation`, no-op branches/unused locals,
  `_extrude_pressed()` + `_extrude_amount_changed()` (half-removed
  extrude SpinBox feature), `SpinBox`/`EditorResourcePicker` fwd decls,
  unused `plugin`/`dock` members + `set_plugin()`, dead `MODE_ROTATE`
  arm in `_gizmo_end_drag`.
- Kept (intentional): unreachable-looking Enter/Esc branches in
  forward_input (focus-path ambiguity, harmless), `clip(p_add_cap=false)`
  param, key-swallow lists.
