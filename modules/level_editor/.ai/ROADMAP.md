# Level Editor - Roadmap / Open Items

Housekeeping: when an item here gets fixed/implemented, DELETE it from this
file (and record the work in AUDITS.md if it was an audit item). Don't leave
struck-through or "done" entries behind - git history is the archive.

## Known placeholders & limitations

- **Edge/vertex "Extrude"** toolbar button just moves the selection +Y by the
  amount - a placeholder. Needs a design decision (Blender extrudes along
  normals and creates geometry; Hammer doesn't extrude edges/verts at all).
- **Edge/vertex Delete** collapses toward neighbor average (rough
  Blender-dissolve approximation). Could be refined.
- **Non-planar faces** get a single Newell normal for shading - fine for
  mild deformation; per-triangle smooth normals could come later.
- **Preview rebuild** runs a full `bake()` (incl. StaticBody/Occluder
  allocation) on every edit - fine for small levels. DECISION: before
  optimizing, add a crude N-brushes timing benchmark (see .ai/TESTS.md;
  keep to pure geometry, no RenderingServer) to prove the lag is real.
- **Clip cap is a possibly non-convex n-gon** fan-triangulated downstream;
  non-convex cuts can produce overlapping tris (accepted, matches the
  no-convexity-guarantee data model).
- **Ghost/select handle resize** scales ALL vertices proportionally within
  the AABB - correct for boxes, stretches deformed brushes (acceptable).
- **Clip in perspective view requires clicking the brush** (ortho allows
  click-anywhere at brush depth).
- **Element selection is per-brush** (`HashMap<LevelBrush *, HashSet>`) -
  multi-brush gizmo drags work, but Bridge Edges requires both edges on the
  same brush (geometry constraint).
- **No convexity/watertight enforcement** (accepted tradeoff for free-form
  editing, same as Blender).

## Deferred smells (from the 2026-07 audit)

Known, small, and not worth churning right now. Fix when touching the area.

- `bevel_edges_profiled` is ~460 lines; its 3 passes are cleanly delimited
  and could each become a private method (only split when next changing
  bevel).
- `LevelEditorScreen::LevelEditorScreen()` (~300 lines) could split into
  `_build_toolbar()`/`_build_viewports()`; `_gizmo_begin_drag` (~230
  lines) into per-target extrude-setup helpers; `_selection_input` (~230
  lines) into per-target cases.
- clip/mirror "pick target brush" blocks still duplicate ~20 lines
  (low-risk shared helper, but both tools work - only merge when editing
  one of them).
- `LevelMap::bake` material dedup is O(m²); preview pays for collision +
  occluder construction it throws away (see placeholder note above -
  benchmark first).
- `delete_faces` leaves orphan verts (other ops compact; documented
  behavior - codified by a test would be nice).
- `set_default_material(null)` is ignored - the default can never be
  cleared once set (property asymmetry).
- `mutable ghost_flat_axis` is written from the const `_compute_drag_aabb`
  (side-channel output; out-param would be cleaner).
- Menu IDs are bare ints (`case 0/2/3/4` in `_face_menu_selected` etc.) -
  an enum would document the layout.
- The bevel default shape (0.5) is duplicated between the dock descriptor
  and the screen fallback.

## Discussed but not built yet

- **Material system rework** (the old chain was REMOVED in the 2026-07
  audit: `current_material`, `_material_changed`, `apply_material_from_dock`,
  and the no-op Face-menu "Apply Material" item are gone). Whatever replaces
  it (per-face picker, texture alignment tools: UV shift/rotate/scale, fit)
  should be designed fresh.
- Proper Hammer extrude semantics for edges/vertices.
- Blender-style dissolve (vs current collapse) for edge/vertex delete.
- Vertex merge/weld tool (weld_vertices exists in LevelBrush, no UI yet).
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
- Scale gizmo: show numeric factor while dragging.
- Esc cancels in-flight gizmo/rotate/select-handle drags (needs
  snapshot-revert semantics - design decision, not a bug fix).
