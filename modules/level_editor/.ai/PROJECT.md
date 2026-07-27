# Level Editor Module - Project Overview

Docs in this folder: PROJECT (this), ARCHITECTURE (file layout + class/data
flow), GOTCHAS (hard-won bug lessons - read before editing), ROADMAP (open
items + deferred smells), TESTS (how to build/run + coverage), AUDITS
(archive of completed audit passes).

**Workflow: the user builds and runs all tests manually.** Never try to
compile, run the editor, or execute tests yourself - make the changes,
say what to build/run, and let the user report results. When a
fix/feature can be tested headlessly, add a test with the change (see
TESTS.md).

## What this is

A Godot engine **module** (`modules/level_editor`) that adds a Hammer-style
(Valve) 3D level editor as a new main-screen tab called **"Level"**, next to
2D/3D/Script/AssetLib. Built directly into the engine source at
`T:\Projects\Windows Projects\godot` (a 4.8.dev fork).

Build command used (Windows, from repo root):

```
scons platform=windows target=editor dev_build=yes accesskit=no d3d12=no angle=no
```

## High-level features

- **Quad viewport layout** (perspective / top / front / side) with 2-way and
  4-way split dragging (nested `SplitContainer`s with
  `set_drag_nested_intersections(true)`). A ground-plane grid also draws in
  the perspective view (near-plane-clipped segments).
- **LevelMap gate**: the tab shows a warning + "Create LevelMap" button
  instead of the viewports until the scene has a `LevelMap` node.
- Perspective viewport uses Godot's own `View3DController` (same class as the
  3D editor) with the user's editor navigation/freelook settings and the
  `spatial_editor/freelook_*` shortcuts: RMB-hold freelook + WASD/QE, wheel
  speed, MMB pan, wheel zoom. Ortho views have MMB pan (cursor-tracking,
  flipped axes per view) and cursor-centered wheel zoom.
- **Brush data model**: `LevelBrush` (a `Node3D`) stores explicit topology -
  `LocalVector<Vector3> verts` + n-gon face loops (`LocalVector<LocalVector<int>>`)
  + per-face materials, serialized as `vertices`, `faces`, `face_materials`
  properties (persists in scenes). Brushes are children of a `LevelMap` node
  (created via the Level tab button). Faces may be non-planar; fan-triangulated
  at bake/preview time.
- **Cross-brush element selection**: vertex/edge/face modes pick from ANY
  brush (per-brush `HashMap` selection sets), with hover highlighting
  (light-blue brush outline, green elements) and orange selected elements.
  Selection clears on tool switch.
- **Material system**: Active Material panel pinned at the dock bottom
  (lit-sphere preview, Browse via Quick Load dialog incl. texture files
  auto-wrapped in StandardMaterial3D, Save to resource file, preview is a
  drag source). New brushes inherit the active material; materials/textures
  drag-drop onto viewport faces (Face mode) or whole brushes (other modes)
  with an animated marching-ants target highlight; texture picks share one
  cached wrapper per path. Extrudes inherit materials from the geometrically
  continuing face (hallway floors stay red). Open edges (single-face
  boundary) render dashed.
- **Tools**: Select (Q: pure selection + single-brush AABB resize
  handles) / Move (W: translate gizmo + click-drag) / Rotate (E) /
  Scale (R) / Block / Clip / Mirror toolbar buttons, with Vertex / Edge /
  Face / Mesh selection targets (three `PanelContainerButtonGroup`
  panels). Ghost block drawing with resize handles, clip tool with
  keep-left/right/both, gizmo manipulation, per-face materials, extrude,
  flip faces (interiors), bridge edges, delete (brush/faces/collapse),
  grid ladder on `[`/`]`.
- **Bake**: `LevelMap::bake()` produces a `MeshInstance3D` (one surface per
  material) with `StaticBody3D` + `ConcavePolygonShape3D` collision and an
  `OccluderInstance3D` (`ArrayOccluder3D`) - see `level_map.cpp` (`bake()`).
- Full undo/redo via `EditorUndoRedoManager`, mostly by recording the
  serialized `vertices`/`faces` properties in one do/undo pair.

## Key design decisions (and why)

- **Explicit topology instead of plane-set brushes.** The module started as
  Hammer-style convex plane-intersection brushes, but that made any vertex
  edit move distant vertices (a quad face cannot bend). The user explicitly
  wanted Blender-style free vertex editing, so brushes store
  vertices + polygon faces. Consequence: no automatic convexity/watertight
  guarantee (same tradeoff Blender makes).
- **Face "move" via gizmo = translate vertices of the loop** (Blender-style),
  not plane sliding.
- **Clip tool**: keep-left/right does a true solid clip + cap; keep-both does
  `split_faces()` which subdivides faces along the line in place (no caps, no
  seam, no second node) - the user specifically wanted "just cut the faces".
- **Editor shortcut conflicts**: the scene dock's Delete shortcut runs in a
  no-context path regardless of focus. Keys handled by the level editor are
  swallowed in `LevelEditorScreen::input()` (the `_vp_input` phase, which runs
  before gui/shortcut dispatch) with `get_viewport()->set_input_as_handled()`.
  Do NOT rely on focus/`shortcut_input` for this - `edit_node()` yanks focus.
- **Undo with `commit_action(false)`** is used when the change was already
  applied live (gizmo drags). Use `commit_action(true)` when do-methods must
  execute (e.g. `add_child` in a split). Gotcha hit once already.
- **Brush previews**: `LevelMap` keeps an internal `_LevelPreview`
  `MeshInstance3D` rebuilt from `bake()` on `refresh()` (immediate, editor only).
  Wasteful on big levels - known future optimization. Refresh triggers:
  explicit calls + brush-side `_notify_map_changed()` from the serialized
  setters and `NOTIFICATION_LOCAL_TRANSFORM_CHANGED` (covers all undo/redo
  paths - see GOTCHAS #22).
- **Overlay colors centralized** in `level_constants.h` (module root;
  `LevelEditorColors` + `hot()` hover-lerp helper, `LevelEditorGrid` ladder/
  3D-grid extents) - the single source for viewport overlay styling.
- **Per-viewport display modes** (Normal/Wireframe/Overdraw/Lighting/Unshaded)
  and 2D/3D grid toggles in the View menu, persisted per-project.

## User preferences / workflow notes

- Wants Hammer fidelity: ghost block stays editable after draw (Enter commits,
  Esc cancels), `[`/`]` grid ladder (power-of-two, 1/8..512), last-block
  height reuse for walls, clip cycling on the Clip toolbar button (Tab is
  eaten by GUI focus nav).
- Compares vertex editing to Blender, expects free-form deformation.
- Prefers switch statements in `_notification`.
- Wants code refactored into logical files (tool actions were split into
  `editor/tools/level_editor_tools.cpp`).
- Toolbar buttons must `release_focus()` after clicks so keys reach viewports.
- UI scale: use `EDSCALE` for handle sizes/line widths/pick tolerances, and
  theme font sizes for overlay text (already EDSCALE-aware).
- Hover feedback: lighter version of the element's own color (50% lerp to
  white), not pure white.
- Drop-target highlight must be visually DISTINCT from hover (orange
  selected-colors, thicker) - see GOTCHAS #34.
