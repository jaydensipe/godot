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

Watch next: `level_editor_screen.cpp` is ~3.4k lines - candidates for future
splits: ghost block, clip tool, gizmos (same treatment as tools/).

## Known placeholders & limitations

- **Edge/vertex "Extrude"** toolbar button just moves the selection +Y by the
  amount - a placeholder. Needs a design decision (Blender extrudes along
  normals and creates geometry; Hammer doesn't extrude edges/verts at all).
- **Edge/vertex Delete** collapses toward neighbor average (rough
  Blender-dissolve approximation). Could be refined.
- **Non-planar faces** get a single Newell normal for shading - fine for
  mild deformation; per-triangle smooth normals could come later.
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

## Discussed but not built yet

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
