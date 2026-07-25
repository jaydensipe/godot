# Level Editor Module - Architecture

## File layout

```
modules/level_editor/
  config.py                  # Module config (doc classes + get_icons_path)
  SCsub                      # Builds *.cpp; editor/*.cpp + editor/tools/*.cpp under env.editor_build
  register_types.{h,cpp}     # initialize_level_editor_module / uninitialize_...
                             #   SCENE level: GDREGISTER_CLASS(LevelBrush), (LevelMap)
                             #   EDITOR level: GDREGISTER_CLASS(LevelEditorPlugin) + EditorPlugins::add_by_type
  level_brush.{h,cpp}        # LevelBrush : Node3D - brush topology + geometry ops (runtime + editor)
  level_map.{h,cpp}          # LevelMap : Node3D - brush container, live preview, bake (runtime + editor)
  icons/                     # Editor icons (LevelMap.svg = node icon, Subdivision.svg = plugin tab)
  doc_classes/
    LevelBrush.xml
    LevelMap.xml
  tests/                     # doctest headers (auto-globbed with tests=yes build)
    test_level_brush.h       # pure geometry, no tags needed
    test_level_map.h         # [SceneTree] tagged (needs physics server)
    test_level_helpers.h     # pure drag/picking math, no tags needed
  .ai/                       # Project docs for AI/agent context handoff
  editor/
    level_editor_screen.{h,cpp}  # LevelEditorPlugin, LevelEditorScreen, LevelEditorViewport
    level_constants.h        # LevelEditorColors (+hot()) / LevelEditorGrid
    level_helpers.h          # LevelHelpers namespace - aabb_corners/AABB_EDGE_IDX/
                             #   AABB_FACE_DIRS/aabb_face_center box helpers, plus the
                             #   pure gizmo-drag math (axis_drag_plane, closest_point_on_line_to_ray)
    tools/
      level_editor_tools.cpp     # LevelEditorScreen tool-action members (_action_*: extrude,
                                 #   material, flip faces, subdivide, bridge, collapse; plus
                                 #   menu handlers and bake)
      clip/
        level_editor_clip.cpp    # Clip tool state machine (begin/drag/cycle/apply/cancel)
                                 #   + cut-line and edge-color preview drawing
      brush/
        level_editor_brush.cpp   # Block tool ghost box (handles, drag, dim labels, commit)
                                 #   + shared box-handle picking (_pick_box_handle,
                                 #   _ray_to_axis_plane used by select-mode handles)
    gizmos/
      level_editor_gizmos.cpp    # LevelEditorScreen gizmo members (translate/scale arrow
                                 #   gizmo, rotate rings, Shift+drag face extrude, undo commits)
    dock/
      level_editor_dock.{h,cpp}  # LevelEditorDock - right-side per-tool settings dock
                                 #   (active material picker + apply button)
```

IMPORTANT: `level_brush.*` and `level_map.*` MUST stay at module root (they
register at SCENE level and exist in exported games). Only editor-only code
goes under `editor/`.

## Class relationships

```
EditorPlugin
  └── LevelEditorPlugin            # has_main_screen -> "Level" tab; owns LevelEditorScreen
        added to editor main screen control in ctor, hidden; make_visible shows it

VBoxContainer
  └── LevelEditorScreen            # toolbar + 4 LevelEditorViewports; owns ALL editing state
        (modes, selection, ghost, clip, gizmos, drag state, keys, undo)

SubViewportContainer
  └── LevelEditorViewport          # own SubViewport + Camera3D + light + WorldEnvironment
        view types: PERSPECTIVE (Ref<View3DController>) | TOP/FRONT/SIDE (simple pan/zoom)
        Overlay (Control, MOUSE_FILTER_IGNORE) draws grid + screen-space overlays
        clip_contents = true (overlay drawing must not bleed into neighbors)
```

## Data model

`LevelBrush` (Node3D):
- `LocalVector<Vector3> verts`, `LocalVector<LocalVector<int>> faces` (n-gon
  loops, CCW-outward winding per Newell's method), `LocalVector<Ref<Material>> face_materials`,
  `bool faces_flipped`.
- Serialized: `vertices` (PackedVector3Array), `faces` (Array of
  PackedInt32Array), `face_materials` (Array), `faces_flipped` (bool).
- `get_face_normal()` = Newell's method (robust for non-planar n-gons).
- Geometry ops: `setup_box`, `move_vertices`, `extrude_face` (cap + walls),
  `clip(plane)` (solid clip + cap), `split_faces(plane)` (subdivide in place),
  `clip_split`, `delete_faces`, `collapse_vertices`, `weld_vertices`,
  `bridge_edges`, `bevel_edges` (Blender-style bevel: edge consumed, one
  strip quad per edge bridging offset lines p_distance into each adjacent
  face; meeting edges mitred to shared corner verts; collinear chains
  share verts into one continuous strip),
  `flip_faces`/`set_faces_flipped`.
- `EdgeKey {int a,b; ordered}` + `EdgeKeyHasher` used for edge identity.
- Bake helpers: `get_bake_surface_data` (fan tris + planar UV, uv_scale 0.25),
  `get_collision_faces`. Stored loops are CCW-outward, but Vulkan/Godot
  rasterizes clockwise-front - so both helpers REVERSE the winding for the
  default (solid) bake and emit loops as-is when `faces_flipped` (interior).
  Vertex normals follow the flag (outward = solid, negated = flipped).
- `ray_intersect` = Möller–Trumbore over fan tris, local space.

`LevelMap` (Node3D):
- Brushes = `LevelBrush` children (`get_brushes()` scans children).
- Editor-only internal `_LevelPreview` MeshInstance3D rebuilt in `refresh()`
  from `bake()`. `bake()` returns nullptr when no renderable geometry exists.
- **Preview refresh triggers**: `refresh()` is called explicitly after edits,
  plus automatically from `LevelBrush` - `_notify_map_changed()` (deferred
  `refresh` on parent) fires in ALL mutating geometry ops (clip, split_faces,
  extrude_face, delete_faces, weld_vertices, collapse_vertices, move_vertices,
  bridge_edges) and the serialized setters (covers undo/redo), and
  `NOTIFICATION_LOCAL_TRANSFORM_CHANGED` (covers undo of node position; brush
  enables `set_notify_local_transform(true)` in editor).
- `default_material` (StandardMaterial3D, albedo 0.7, CULL_BACK - standard
  back-face culling; correct because the bake emits Vulkan-convention
  clockwise-front triangles).
- `bake()` groups faces per unique material into an `ArrayMesh`
  (map-local space; normals via inverse-transpose), plus
  StaticBody3D+ConcavePolygonShape3D and OccluderInstance3D(ArrayOccluder3D)
  sharing the same triangle soup. Returns a detached node; caller owns/adds.

## Editor state (LevelEditorScreen)

- `Mode` enum order: SELECT, ROTATE, SCALE, BLOCK, CLIP, VERTEX, EDGE, FACE.
  Toolbar indices iterate `mode_buttons[MODE_MAX]`; mode buttons live in three
  button-group panels (SELECT..SCALE, BLOCK..CLIP, then VERTEX..FACE).
- **No-map gate**: if the edited scene has no `LevelMap`, the quad viewports
  are hidden and a warning panel ("Create LevelMap" button) shows instead.
  `_update_map_ui()` resolves/adopts a map found in the scene (never
  auto-creates); `LevelEditorPlugin::edited_scene_changed()` override
  re-resolves on scene switch. The map is only created by the button
  (`_get_or_create_map`).
- Selection: `LevelBrush *selected_brush` (whole-brush modes) + per-brush
  element sets: `HashMap<LevelBrush *, HashSet<...>> selected_faces /
  selected_edges / selected_vertices` - element modes select across brushes.
  Selection clears on EVERY mode switch (`_set_mode`).
- Element picking scans ALL brushes in the map (`_pick_vertex`/`_pick_edge`/
  `_pick_face`). Hover shows the hovered brush (light-blue outline) + its
  pickable elements: green vertices (vertex mode only), green hovered edge,
  green hovered face fill; selected elements draw in orange
  (`LevelEditorColors::SELECTED_ELEMENT`). Brushes with any selection keep the
  highlight after the cursor leaves.
- Editor selection sync: plugin listens to `EditorSelection::selection_changed`
  and adopts a selected `LevelBrush` (`set_selected_brush_from_editor`).
- Map lookup: `_find_map_in_scene()` is the single DFS that finds the first
  LevelMap in the edited scene; `_resolve_map()`/`_update_map_ui()` wrap it.
- View menu: per-viewport display modes (Normal/Wireframe/Overdraw/Lighting/
  Unshaded, ids = vp*MAX+mode) + global 2D/3D grid toggles; all persisted via
  `EditorSettings::set_project_metadata("level_editor", ...)`.
- Block flow: stage 1 drag → stage 2 "ghost" (AABB + 6 face handles + 8
  corner handles + inside-drag move + dim labels) → Enter commits
  (`_ghost_commit`), Esc cancels. `_compute_drag_aabb` shared by preview +
  commit; reuses last brush's Y height for walls.
- Clip flow: `_clip_begin` on brush click (or anywhere in ortho at brush
  depth), 2 snapped points + captured `clip_view_dir`; plane normal =
  `along × view_dir` (verified = screen-left of the drawn line). Clip toolbar
  button re-click cycles KEEP_FRONT(=left)/KEEP_BACK/BOTH (Tab doesn't work -
  GUI eats it). Preview: wireframe edges split at plane, green=kept,
  red=discarded (+ white cut markers). Apply on Enter via `input()`.
- Gizmos (screen-space, custom): move gizmo (arrows + plane quads + center,
  closest-point-on-axis drag), rotate gizmo (3 rings, ortho views restrict to
  the view axis + click-anywhere), scale (axis handles + off-gizmo uniform
  mouse-X), select-mode AABB handles (resize via `_apply_brush_aabb`).
  Element-mode drags snapshot `gizmo_drag_brush_verts` (per-brush map) and
  apply absolute deltas to each brush's selected vertices; whole-brush modes
  use `gizmo_drag_original_verts`. Undo = property pair across all dragged
  brushes in ONE action, `commit_action(false)`.
- Keys (handled in `LevelEditorScreen::input()`, `_vp_input` phase, swallowed
  via `set_input_as_handled`): Delete (per-mode delete/collapse), `[`/`]`
  grid ladder (hardcoded steps 1/64..64), Enter/Esc (ghost/clip commit/cancel,
  drag cancel). `shortcut_input` on screen+viewport also accepts them as a
  fallback. Brackets in `forward_input` are intentionally skipped to avoid
  double-handling.
- **All overlay colors** live in `editor/level_constants.h`
  (`LevelEditorColors` namespace) - never hardcode `Color(...)` literals in
  drawing code.

## Viewport details

- Perspective: `View3DController` instantiated with editor settings
  (`editors/3d/navigation*`, `editors/3d/freelook*`) and
  `ED_GET_SHORTCUT("spatial_editor/freelook_*")` for WASD/QE/modifiers
  (required - without `set_shortcut` calls, WASD does nothing).
  `update_freelook(delta)` in NOTIFICATION_PROCESS; camera resynced from
  `to_camera_transform()` on EVERY forwarded event (wheel zoom isn't
  "navigating" - only updating on navigate lost wheel zoom once).
  Cursor stores `pos_x/pos_y/pos_z` (doubles) - NOT `pos` (changed upstream).
- Ortho: camera placed 500 units back on the view axis with an explicit
  rotation Basis (do NOT use `looking_at` - colinear up warns in top view).
  Pan uses camera basis rows with per-view flips (top: negate up, side:
  negate right), 1:1 pixel mapping via `distance / viewport_height`.
  Zoom is cursor-centered (before/after `intersect_ortho_plane` pivot fixup).
  Ortho edit planes for tools pass through the relevant point (clip point /
  brush center), not the origin.
- Perspective grid: 3D line mesh (`ImmediateMesh`, vertex-colored unshaded)
  on render layer 20, camera-centered with `GRID_3D_EXTENT` extent, rebuilt
  when the camera moves > `GRID_3D_REBUILD_DIST` (layer mask needed because
  ALL SubViewports share the scene World3D - visibility alone leaks the mesh
  into ortho panes). Sits 2mm below Y=0 to avoid z-fighting floor brushes.
  Ortho grid: infinite 2D overlay lines on the edit plane. Both togglable in
  the View menu (persisted via project metadata).
- `gui_input` forwarding: `LevelEditorViewport::gui_input` calls
  `screen->forward_input(camera, event)` FIRST, then handles navigation.
  `forward_input` maps camera → viewport and implements ALL tool logic
  (block drag, ghost, clip, gizmo, selection, hover) in mode order:
  rotate-ring → move-gizmo → block/ghost → clip → select/element clicks.
- Freelook disabled on `NOTIFICATION_WM_WINDOW_FOCUS_OUT`.
- Each viewport has its own DirectionalLight3D (`look_at_from_position`
  from (10,20,10) → origin) + fill light (energy 0.35) + WorldEnvironment
  (BG_COLOR + color ambient). Lights/env intentionally NOT shared with the
  3D editor (user chose separate).

## Shared helpers

- `level_helpers.h` (`LevelHelpers` namespace): `aabb_corners()`,
  `AABB_EDGE_IDX`, `AABB_FACE_DIRS`, `aabb_face_center()` - all box
  drawing/picking uses these.
- `level_constants.h` (`LevelEditorColors`): all overlay colors + `hot(color)`
  (50% white lerp for hover/drag states). (`LevelEditorGrid`): grid `STEPS`/
  `STEP_COUNT` ladder + `GRID_3D_EXTENT`/`GRID_3D_REBUILD_DIST` for the
  perspective 3D grid mesh.
- Rotate gizmo: `_rotate_world_radius()` (screen-constant ring radius) and
  `_rotate_allowed_axis()` (per-view axis filter) are shared by pick + draw -
  they MUST stay in sync (a desync made rings unpickable at most zooms).
- `LevelEditorViewport::ray_to_view_plane(screen, point, hit)` - ray to the
  viewport's natural edit plane through a point (Y plane for perspective/top,
  Z for front, X for side). Use this for ALL edit-plane intersections.
- `LevelEditorScreen::_pick_box_handle(vp, screen, aabb, xform)` - shared
  corner/face handle picking for ghost + select handles.
- `LevelEditorScreen::_commit_brush_undo(action, brush, old_verts, old_faces,
  old_mats, execute=false)` - one-call vertices/faces/materials undo record.

## Build gotchas encountered

- `SCsub`: module env has `env.editor_build`, NOT `env["tools"]`.
- `register_types.cpp` needs `#include "core/object/class_db.h"` for
  `GDREGISTER_CLASS`; init function names must match the FOLDER name
  (`level_editor` → `initialize_level_editor_module`).
- `LocalVector::push_back` of a `LocalVector` needs an explicit copy:
  `push_back(LocalVector<int>(cap))` (explicit ctor).
- `Ref<>` can't wrap Node3D (no `init_ref`) - use raw pointers + memnew/memdelete.
- `Plane` API: `intersect_3`, `intersects_ray`, `is_point_over`,
  `is_equal_approx`, `distance_to` = `normal.dot(p) - d`.
- `Basis` rows are `basis[i]` (no `.x`/`.y` members).
- `Environment` ambient: `set_ambient_source(AMBIENT_SOURCE_COLOR)` etc.
- `get_editor_main_screen()` = `EditorNode::get_singleton()->get_editor_main_screen()->get_control()`.
- Editor plugins with main screens: ctor adds child + `hide()`, `make_visible` shows.
- `hash_murmur3_one_32` via `core/templates/hashfuncs.h` (through hash_set.h).
