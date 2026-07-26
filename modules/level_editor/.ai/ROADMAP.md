# Level Editor - Roadmap / Open Items

## Code health (last audit)

Cleaned up in the latest pass:

- Consolidated triplicated AABB corner/edge/face tables into anonymous-namespace
  helpers (`aabb_corners`, `AABB_EDGE_IDX`, `aabb_face_center`) in
  level_editor_screen.cpp.
- `_pick_box_handle()` shared by ghost + select-handle picking.
- `LevelEditorViewport::ray_to_view_plane()` replaced ~8 copies of the
  view-type edit-plane intersection block.
- `_commit_brush_undo()` helper replaced 10 copies of the
  vertices/faces/materials undo block.
- Removed: unused `node_3d_editor_plugin.h` include, dead `focus_on`,
  dead `gizmo_drag_original_rotation`, no-op branches/unused locals.

### Audit (2026-07): bugs to fix

All fixed in the post-audit pass:

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
7. ~~`_clip_apply` leaves stale clip state~~ - resets everything `_clip_cancel` does.
8. `_action_extrude_faces` keeping `selected_faces` - VERIFIED NOT A BUG:
   `extrude_face` replaces each source face with its cap in place (no
   re-index), so the selection correctly tracks the new caps for chained
   extrudes.
9. ~~Scale pivots inconsistent~~ - `_apply_gizmo_scale` now pivots at the
   AABB center like `_apply_gizmo_scale_uniform`.
10. ~~`split_faces` welds against ANY nearby vertex~~ - welding is now
    restricted to intersection verts created by the same split call.

### Audit (2026-07): dead code / smells (low priority)

Removed: `_extrude_pressed()` + `_extrude_amount_changed()` (half-removed
extrude SpinBox feature), `SpinBox`/`EditorResourcePicker` fwd decls, unused
`plugin`/`dock` members + `set_plugin()`, dead `MODE_ROTATE` arm in
`_gizmo_end_drag`. Kept (intentional): unreachable-looking Enter/Esc
branches in forward_input (focus-path ambiguity, harmless), duplicated
gizmo axis-projection in pick/draw (must stay in sync visually),
`clip(p_add_cap=false)` param, key-swallow lists.

Remaining smells: `_draw_drag_feedback` memnews a full LevelBrush per
overlay draw. (Fixed since: weld epsilon + parallel-dot constants live in
`LevelBrushConstants`; orphan verts compacted by clip/bevel/weld/collapse.)

### Audit (2026-07, pass 2): fixed

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

### Audit (2026-07, pass 2): known, deferred

- Esc does not cancel gizmo/rotate/select-handle/whole-brush drags
  (would need snapshot-revert semantics - design decision, not a bug fix).
- `on_scene_changed` leaves in-flight drag snapshot maps; guards prevent
  crashes, but redoing undo actions mid scene-teardown is riskier than
  the stale state.
- `current_material`/`_action_apply_material`/`_material_changed` hooks
  are dead (dock picker removed) - pending the material system rework.
- clip/mirror "pick target brush" blocks duplicate ~20 lines; merging
  churns two working tools for modest gain.

## Known placeholders & limitations

- **Edge/vertex "Extrude"** toolbar button just moves the selection +Y by the
  amount - a placeholder. Needs a design decision (Blender extrudes along
  normals and creates geometry; Hammer doesn't extrude edges/verts at all).
- **Edge/vertex Delete** collapses toward neighbor average (rough
  Blender-dissolve approximation). Could be refined.
- **Non-planar faces** get a single Newell normal for shading - fine for
  mild deformation; per-triangle smooth normals could come later.

## RESOLVED: extrude_edge wall texture flip (2026-07)

Fixed. Full history and the final rule are in GOTCHAS #43 - read it
before touching `extrude_edge`/`extrude_vertex` or wall winding.

Related recent fixes (this session, all landed):
- extrude_edge/extrude_vertex ops + Shift+drag in edge/vertex targets.
- Extrude undo clears the selection (stale remapped indices read OOB).
- Paint-select drag (LMB drag over elements in vertex/edge/face targets).
- Tool x SelectionTarget state split (Q/E/R + 1/2/3/4) replacing Mode enum.
- Block-tool Brush Type (Block/Quad) + quad ghost handle rules
  (`_ghost_handle_usable` single predicate).
- **Preview rebuild** runs a full `bake()` (incl. StaticBody/Occluder
  allocation) on every edit - fine for small levels, will need
  incremental/dirty-face updates for large ones. DECISION: before optimizing,
  add a crude N-brushes timing benchmark (see .ai/TESTS.md; keep to pure
  geometry, no RenderingServer) to prove the lag is real.
- **No convexity/watertight enforcement** (accepted tradeoff for free-form
  editing, same as Blender).
- **Ghost/select handle resize** scales ALL vertices proportionally within
  the AABB - correct for boxes, stretches deformed brushes (acceptable).
- **Clip in perspective view requires clicking the brush** (ortho allows
  click-anywhere at brush depth).
- **Rotate/Scale modes don't change selection by design** (selection is
  Select-mode only).
- **Element selection is per-brush** (`HashMap<LevelBrush *, HashSet>`) -
  multi-brush gizmo drags work, but Bridge Edges requires both edges on the
  same brush (geometry constraint).
- **Preview bake allocates collision+occluder** even for the editor preview
  (wasted work) and material grouping is O(m²) - both known, deferred until a
  benchmark proves it matters on real levels. `bake()` returns nullptr when
  there's no renderable geometry (all faces deleted).
- **Clip cap is a possibly non-convex n-gon** fan-triangulated downstream;
  non-convex cuts can produce overlapping tris (accepted, matches the
  no-convexity-guarantee data model).

## Discussed but not built yet

- **Per-action settings panels in the dock** (DONE for Bevel): arming an
  action (Edge > Bevel) shows its options in `LevelEditorDock`; Enter
  applies, Esc cancels. Bevel options: width (defaults to grid size),
  steps (0 = single cut; >=1 = 2N band quads across the cross-section),
  shape 0..1 (0 = flat chamfer, 0.5 = quadratic-Bezier round-over,
  1 = full bulge back to the original corner) - all wired through
  `LevelBrush::bevel_edges_profiled`. A live wireframe preview of the
  armed bevel draws in the viewports and updates on every dock edit
  (`ToolPreview`, hash-cached). The dock's old active-material picker was
  removed; material logic pending rework (`_action_apply_material` still
  reachable from the Face menu but `current_material` is never set).
- Proper Hammer extrude semantics for edges/vertices.
- Blender-style dissolve (vs current collapse) for edge/vertex delete.
- Vertex merge/weld tool (weld_vertices exists in LevelBrush, no UI yet).
- Texture/material alignment tools (UV shift/rotate/scale per face, fit).
- Clip tool: multi-point clip (3-point plane), clip preview using actual
  split geometry instead of edge tinting.
- Duplicate brush (Ctrl+D) and other edit-menu actions.
- Brush grouping / hide-lock, visgroups.
- Logging system (debug prints were removed by request; add a proper
  verbose-log flag later).
- Export-template verification (module builds runtime classes, but the
  editor plugin path is tools-only; test an exported project with a baked
  level).
- View3DController parity for ortho views (currently custom pan/zoom).
- Rotate gizmo angle readout overlay; rotate snapping currently hardcoded 15°.
- Scale gizmo: show numeric factor while dragging; grid-snap scale factors.

## Recently completed (for context)

- Audit cleanup: rotate-ring pick now uses the drawn ring's world radius
  (was unpickable at most zooms), select-handle resize snapshots vertices
  (degenerate drags no longer bake data loss), extrude undo includes
  face_materials, bake no longer leaks on missing scene root. Gizmo no longer
  vanishes when one axis goes behind the camera (per-axis skip, plane handles
  gated on both axes, pick/draw share GIZMO_PLANE_EXTENT). Brush geometry ops
  self-notify preview refresh.
- Perspective 3D grid mesh (camera-following, layer-20, depth-tested) +
  View-menu display modes & grid toggles persisted via project metadata.
- New tests: clip no-op/full-clip, clip_split caps/slabs, weld_vertices,
  negative extrude normals, duplicate_brush fidelity.
- No-map gate: warning panel + Create LevelMap button; viewports hidden until
  a map exists (`edited_scene_changed` override, `_update_map_ui`).
- Cross-brush element selection (per-brush sets, hover highlight, persistent
  highlight on selected brushes, selection clears on tool switch).
- Perspective-view ground grid with near-plane segment clipping.
- Overlay colors centralized in `level_constants.h` (module root).
- Preview auto-refresh on undo/redo (brush-side `_notify_map_changed` from
  serialized setters + local-transform notification).
- Module icons via `config.py get_icons_path()` (`LevelMap.svg` node icon,
  `Subdivision.svg` tab icon).
- Tools menu (dropdown) with Bridge Edge; tool actions split into
  `editor/tools/level_editor_tools.cpp`.
- Rotate (ring gizmo) and Scale tools; Select-mode AABB handles and
  whole-brush drag by re-clicking the selected brush.
- Clip tool with face-split (keep-both), solid clip (left/right), edge-split
  wireframe preview.
- Flip Faces (interiors) via `faces_flipped` property + toolbar button.
- Quad-split 4-way dragging (nested SplitContainers + drag_nested_intersections).
- Perspective viewport on `View3DController` (freelook + wheel zoom fixed).
- Delete key with editor-shortcut hijack workaround (`input()` phase).
- Grid ladder on brackets, ghost dimension labels, last-block height reuse.
- Gizmo overhaul: translate/scale arrow gizmo + rotate rings work in all four
  viewports (axis-plane drags allow vertical moves in top view), Face-mode
  Shift+drag extrudes along face normals. Gizmo code split into
  `editor/gizmos/level_editor_gizmos.cpp`.
- Edge double-click selection: collinear chain by default, Blender-style
  edge loop on Alt+double-click (`get_edge_chain`/`get_edge_loop`).
- Mirror tool (MODE_MIRROR, toolbar next to Clip): clip-style 2-point plane,
  live reflected preview, Enter duplicates the brush mirrored as a new node.
- Bevel tool: Edge menu, `LevelBrush::bevel_edges` (Blender-style
  offset-line bevel - edge consumed, one strip quad per edge, mitred
  shared corners, continuous collinear-chain strips; grid-size default).
- Vertex/Edge/Face toolbar menus with `_action_*` tool actions; Subdivide
  (quad grid / n-gon fan) on Face menu; Flip Faces moved into Face menu.
- Mode shortcuts: Q/W/E/R/B/C (Select/Move/Rotate/Scale/Block/Clip) +
  1/2/3/4 targets; menu shortcuts Ctrl+D subdivide, F flip.
