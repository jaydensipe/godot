# Level Editor - Audit Archive

> **READ ONLY** — Do not read this file when reading the `.ai/` folder for context handoff. It is write-only: append a new dated section only after an audit pass has been completed and its items have been moved into ROADMAP.md or ARCHITECTURE.md.

Completed audit passes, kept for history. For CURRENT open items see
ROADMAP.md. New audits: append a dated section at the bottom and move
still-open items into ROADMAP.md.

## Audit (2026-07, pass 5) - targeted deferred-smell sweep

Scope: the small, safe, non-churn items from ROADMAP "Deferred smells".
The big refactors (clip/mirror ~250-line state-machine merge, ctor /
`_gizmo_begin_drag` / `_selection_input` splits, `bevel_edges_profiled`
split) were deliberately LEFT - the roadmap marks those "only when next
editing them"; doing them cold is regression risk with no behavior win.
Also fixed the stale-outline bug separately this session (GOTCHAS #54:
per-viewport `outline_versions`).

Bugs fixed:

- Stale 3D brush outline in 3 of 4 viewports after rotate/scale/
  select-handle resize: `outline_versions` (the per-brush geometry-version
  stamp that gates outline-mesh rebuilds) was a single map shared across
  all four viewports, but each viewport owns a SEPARATE `ImmediateMesh`.
  Viewport 0 rebuilt on a version bump and bumped the shared stamp, so
  viewports 1/2/3 read `built == version` and skipped their own rebuild.
  Move looked fine only because it changes the node transform (applied
  per-frame for all 4 panes) and never rebuilds the mesh. Fix:
  `outline_versions` is now `outline_versions[4]` (per viewport).
- `move_vertices` validated each index mid-loop - a stale index half-
  applied the delta. Now validates all indices up front (same rule
  `delete_faces` got in pass 4). Regression test added.

Dead code removed:

- `LevelBrushConstants::WINDING_SIDE_EPS` - unused since the GOTCHAS #30
  two-phase-winding fix removed the centroid/bisector winding rule it
  fed. Only docs referenced it.

Helpers/constants added:

- `LevelHelpers::ortho_view_axis(int view_type)` -> world axis the ortho
  view looks down (TOP=Y, FRONT=Z, SIDE=X, perspective/unknown=-1).
  Replaced the identical switch in `_box_handle_usable` and
  `_rotate_allowed_axis`. Takes the view type as an int so level_helpers.h
  stays free of the screen class (no circular include).
- `LevelBrushConstants::BEVEL_DEFAULT_SHAPE` (0.5) - the bevel profile
  default was a bare `0.5` in four places (dock descriptor, quick-bevel,
  armed fallback, `bevel_edges`). Now one constant; all four sites use it.
  (`level_editor_dock.cpp` and `level_editor_tools.cpp` needed an explicit
  `level_constants.h` include - it wasn't transitively reachable.)

Tests added: `ortho_view_axis` mapping (test_level_helpers.h);
`move_vertices` stale-index whole-op rejection (test_level_brush.h).

Verified-already-done (removed from ROADMAP): the level_modifiers.cpp
`newell_normal` duplication no longer exists - all sites call
`LevelBrush::get_face_normal`.

## Audit (2026-07, pass 4) - multi-agent module sweep

Bugs fixed:

- `_draw_brush_outline` used per-endpoint `project()` while every other
  outline pass used `project_segment()` - brush outlines vanished when one
  endpoint crossed the near plane (GOTCHAS #22). Fixed via a new shared
  `_draw_brush_edges()` (screen member; needs LevelBrush + viewport types
  so it can't live in level_helpers.h) which also collapsed 3 duplicated
  edge-outline loops (base outline, mesh hover, element-target highlight).
- `_clip_input`/`_mirror_input` ended with an unconditional `return true` -
  the clip/mirror tools swallowed ALL viewport input while active (camera
  nav blocked even before starting a clip). Now returns false unless a
  clip/mirror drag is active.
- Clip/mirror ortho "click anywhere" fallback dereferenced `current_map`
  unchecked (null-deref with no LevelMap in the scene) - guarded.
- Vertex/edge pick tolerances were raw literals (16.0/12.0) not multiplied
  by EDSCALE unlike every other pick tolerance - moved to
  `LevelEditorHandles::VERTEX_PICK_TOL`/`EDGE_PICK_TOL` (HiDPI fix).
- Marching-ants phase wrap was a hardcoded `16.0` in two places, decoupled
  from the dash length it must match - now
  `LevelEditorHandles::ANTS_PERIOD` derived from `ANTS_DASH` (+ `ANTS_SPEED`).
- `delete_faces` validated indices mid-removal - a stale index half-applied
  the op. Now validates all indices up front.
- `extrude_vertex` read `face_materials[u.face]` unchecked (every other
  site guards) - guarded.

Dead code removed:

- Dock: redundant double null-check + double `set_tooltip_text` on
  material_save (first immediately overwritten).

Helpers/constants added:

- `LevelHelpers::draw_vertex_marker` (filled square + 1px black outline) -
  replaced 2 duplicated vertex-marker blocks.
- `LevelEditorHandles::VERTEX_SIZE`/`VERTEX_HOT_SIZE`/`DROP_REPROBE_DIST_SQ`.

Tests added: `aabb_corners`/`AABB_EDGE_IDX`/`aabb_face_center`/
`aabb_from_points` invariants, setup_sphere max-clamp (64), odd side
counts (rings floor), non-uniform AABB ellipsoid + pole positions,
vertex/face accessor round-trip, is_valid negative/minimal cases.

Deferred (added to ROADMAP.md "Deferred smells"): clip/mirror two-point
plane state machines (~250 lines, merge when next editing them), ortho-
view->axis switch (5 copies), selected-brushes HashSet collection (2
copies), sorted-descending face iteration (3 copies), face fill+outline
projection block (3 copies, divergent guards), Newell duplication
(level_brush.cpp vs level_modifiers.cpp), `_pick_gizmo` re-implements
`closest_point_on_segment_2d`, `clip_split`/`get_face_center`/`is_valid`
production-dead (test-only).

## Perf pass (2026-07) - dense-brush drag lag

The rotate/scale lag on committed spheres came back after the geometry-
only bake fix. Two compounding causes found and fixed:

- The overlay called `get_edges()` + `get_open_edges()` per brush per
  viewport per repaint - each rebuilt a HashSet by scanning every face
  loop (~50k inserts/viewport for a 64-side sphere, x4 viewports, on
  every mouse-motion). Fixed with a LevelBrush edge cache invalidated in
  `_notify_map_changed`; both getters now return const refs (GOTCHAS #50).
- `LevelMap::bake()` material dedup was O(m^2) and the surface loop
  re-scanned all brush faces per material with per-material transform
  recomputation. Now a single grouping pass + per-brush transforms
  computed once (GOTCHAS #51).

API change: `LevelBrush::get_edges()`/`get_open_edges()` now return
`const HashSet &` (no copies; HashSet has no copy ctor). All callers
updated to references.

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

## Audit (2026-07, pass 6) - full-module multi-agent sweep

Scope: whole module (runtime + editor + dock + tests), 4 parallel audit
agents, findings verified and fixed by hand. Deferred-smell big refactors
(clip/mirror state-machine merge, ctor/`_gizmo_begin_drag`/`_selection_input`
splits, `bevel_edges_profiled` split) deliberately LEFT, per roadmap policy.

Bugs fixed:

- `subdivide_face` clobbered the materials of every pre-existing face AFTER
  the subdivided index (re-assigned `face_materials[p_face..end]` instead of
  only the source + appended sub-faces). Regression test added.
- `collapse_vertices` validated indices mid-op (stale index half-applied the
  collapse). Now validates up front, same rule as move_vertices/delete_faces.
- `compact_vertices` never called `_notify_map_changed()` when called
  standalone (stale edge caches/geometry version). Now notifies (the no-op
  early-out still skips).
- `bridge_edges` indexed `verts[]` with unchecked EdgeKey endpoints (stale
  selection could OOB). Now bounds-rejects.
- `_bevel_edges_apply` left a dangling uncommitted `create_action` when no
  brush beveled anything. Action is now created lazily on first success.
- `on_scene_changed` left in-flight drags running against brushes of the old
  scene (gizmo/rotate/select-handle/block drags, extrude snapshot maps).
  New `_abandon_drags()` resets ALL drag state without committing undo (the
  brushes and the scene's undo history are gone - unlike `_set_tool`, which
  ends drags cleanly because the brushes survive). GOTCHAS #55.
- `_clip_input` handled Enter/Esc AND `input()` handled them (double-key
  risk). Clip keys now live only in `input()`, matching mirror.
- `_draw_drag_feedback` allocated a `LevelBrush` + `setup_box` per overlay
  paint (x4 viewports per frame during block drags). Draws the 12 AABB edges
  directly via `aabb_corners`/`AABB_EDGE_IDX` (same pattern as the sphere
  preview cache).
- `_apply_gizmo_delta` face-extrude path indexed `cap_normals[...][size-1]`
  on an implicitly-nonempty map - guarded with ERR_CONTINUE.
- Dock: armed-action SpinBoxes with `min == 0` (bevel Steps/Shape) allowed
  negative values (`allow_lesser` was `min <= 0`; now `min < 0`). Dock:
  failed material preview renders left the previous material's texture up;
  now cleared. Dock: Browse/Save buttons now `release_focus()` like every
  other toolbar button.
- `LevelMap::_update_preview`: removed a redundant `structure_changed`
  clause (subsumed by `any_dirty ||`) and guarded the `memcmp` layout diff
  against empty (null-ptr) arrays.

Considered and REJECTED: validating face-loop indices in `set_faces_data`
(corrupt .tscn defense) - undo restores faces BEFORE vertices (reverse
order), so validation would falsely reject legitimate restores where the
do-state shrank the vertex array. Documented here so nobody re-adds it.

Dead code removed:

- `LevelEditorViewport::get_gizmo_root()`, `is_info_visible()`,
  `is_frame_time_visible()` (never called).
- `gizmo_drag_mouse_start` (write-only member).
- `_rotate_world_radius`'s unused `p_center` parameter.
- Enter/Esc block in `_clip_input` (dead - `input()` runs first and swallows).

Helpers/constants added:

- `level_modifiers.cpp` anonymous namespace: `reversed_loop` (4 copies),
  `remove_duplicate_loop_verts` (2 copies), `find_faces_with_edge` (3
  face/edge adjacency scans). New private member `LevelBrush::_remove_face`
  (face+material removal, 3 copies).
- `LevelMap::_brush_to_map_transform` (3 copies of the map-local transform
  expression) and `_append_face_geometry` (2 copies of the tessellate-
  transform-append loop).
- `LevelEditorScreen::_pixels_to_world_at` - the ONE pixels->world helper
  `_gizmo_3d_world_scale` and `_rotate_world_radius` now share (GOTCHAS #25
  rule). `_scale_factors_from_drag` (2 identical scale-factor blocks).
  `_sync_display_submenu` (3 copies of the display-mode radio sync loop).
  `_draw_tool_hint` (clip/mirror mode-hint footer).
  `_element_stub_dir` (gizmos; edge/vertex stub-normal dup + EXTRUDE_STUB).
- Constants: `LevelBrushConstants::PERP_AXIS_MAX_X`, `SPHERE_SIDES_MIN/MAX`,
  `BEVEL_WIDTH_MIN/MAX`, `BEVEL_STEPS_MAX`; `LevelEditorGrid::
  GRID_MAJOR_INTERVAL`, `ROTATE_SNAP_DEGREES`; `LevelEditorHandles::
  PICK_RAY_LEN`, `ROTATE_RING_PICK_TOL`, `SCALE_DRAG_RATE`, `EXTRUDE_STUB`,
  `OPEN_EDGE_DASH_PX/WORLD`, `FILL_COORD_LIMIT`, `HINT_OFFSET_X/Y`,
  `HINT_FONT_SIZE`; file-static `ORTHO_ZOOM_*` (screen.cpp),
  `ROTATE_RING_SEGMENTS` (gizmos), `MATERIAL_PREVIEW_SIZE` (dock). Fixed the
  missed `BEVEL_DEFAULT_SHAPE`/`ANTS_DASH` sites from pass 5/4.
- Dock brush-type item ids and sphere range now use the BrushType enum /
  SPHERE_SIDES constants instead of bare 0/1/2 and 4/64.
- `_ghost_hit_test` now uses `Geometry2D::is_point_in_polygon` instead of a
  hand-rolled ray-casting test (removes the pass-4 deferred smell).
- `_update_hover`'s duplicated "no element hit, resolve the brush" fallback
  collapsed to one shared block.

Tests added: subdivide_face material preservation (regression), collapse/
weld stale-index whole-op rejection, bridge_edges material + stale-endpoint
rejection, bevel_edges_profiled rejection cases, get_bake_surface_data UV
projection values, clip_split fully-front plane, rewind_edge_wall two-phase
re-wind (GOTCHAS #30 mechanism, direct), setup_sphere min-clamp exact counts,
find_vert test helper.
