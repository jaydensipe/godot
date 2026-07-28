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
  level_modifiers.cpp        # LevelBrush geometry-op implementations (clip, extrude,
                             #   bevel, weld, bridge, subdivide...) - split from level_brush.cpp
  level_map.{h,cpp}          # LevelMap : Node3D - brush container, live preview, bake (runtime + editor)
  level_constants.h          # LevelBrushConstants (geometry tolerances, runtime-safe)
                             #   + LevelEditorColors/LevelEditorGrid/LevelEditorHandles
                             #   (editor overlay styling) - at module ROOT so runtime code
                             #   can include it (must stay editor-free)
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
    level_helpers.h          # LevelHelpers namespace - aabb_corners/AABB_EDGE_IDX/
                             #   AABB_FACE_DIRS/aabb_face_center box helpers, the
                             #   pure gizmo-drag math (axis_drag_plane,
                             #   closest_point_on_line_to_ray), 2D segment math
                             #   (closest_point_on_segment_2d, clip_segment_to_rect),
                             #   and clipped overlay drawing (draw_dashed_line_clipped,
                             #   draw_marching_segment, draw_vertex_marker)
    tools/
      level_editor_tools.cpp     # LevelEditorScreen tool-action members (_action_*: extrude,
                                 #   flip faces, subdivide, bridge, collapse; plus
                                 #   menu handlers and bake)
      clip/
        level_editor_clip.cpp    # Clip tool state machine (begin/drag/cycle/apply/cancel)
                                 #   + cut-line and edge-color preview drawing
      mirror/
        level_editor_mirror.cpp  # Mirror tool (MODE_MIRROR): clip-style 2-point
                                 #   plane, live reflected-wireframe preview, Enter
                                 #   duplicates the brush mirrored as a NEW node
                                 #   (undo via add_do_method add_child + do_reference)
      brush/
        level_editor_brush.cpp   # Block tool ghost box (handles, drag, dim labels, commit)
                                 #   + shared box-handle picking (_pick_box_handle,
                                 #   _ray_to_axis_plane used by select-mode handles)
    gizmos/
      level_editor_gizmos.cpp    # LevelEditorScreen gizmo members (translate/scale arrow
                                 #   gizmo, rotate rings, Shift+drag face extrude, undo commits)
    dock/
      level_editor_dock.{h,cpp}  # LevelEditorDock - scrollable per-tool/armed
                                 #   settings form on top; STICKY Active Material
                                 #   panel at the bottom (framed PanelContainer:
                                 #   lit-sphere preview via EditorResourcePreview,
                                 #   name label, Browse (EditorQuickOpenDialog,
                                 #   Material+Texture2D types) | VSeparator | Save
                                 #   (EditorFileDialog -> ResourceSaver); preview
                                 #   is a drag source ("resource" payload, same as
                                 #   EditorResourcePicker)
    materials/
      level_editor_materials.{h,cpp}  # LevelEditorMaterialCache (ONE generated
                                 #   StandardMaterial3D per texture path - shared
                                 #   by Browse and drops, no embedded dupes, single
                                 #   bake surface) + LevelEditorMaterials namespace:
                                 #   drag_data_is_material (cheap hover check),
                                 #   material_from_drag_data (payload -> Material,
                                 #   "files" and "resource" conventions),
                                 #   path_is_material_or_texture
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
- Geometry ops: `setup_box`, `setup_quad` (single-face flat brush),
  `setup_sphere` (lat/long convex solid, sides clamped [4,64],
  rings = MAX(sides/2, 2)),
  `move_vertices`, `extrude_face` (cap + walls),
  `extrude_edge`/`extrude_vertex` (duplicate the element's verts with an
  offset, rewire using faces to the dupes, stitch one wall quad/wedge per
  face; wall winding is verified against the brush center and flipped if
  inward - using faces may traverse the element in either direction),
  `clip(plane)` (solid clip + cap), `split_faces(plane)` (subdivide in place),
  `mirror(plane)` (reflect verts + reverse winding - reflection flips
  chirality), `compact_vertices` (drop unreferenced verts, remap loops),
  `clip_split`, `delete_faces`, `collapse_vertices`, `weld_vertices`,
  `bridge_edges`, `get_edge_loop`/`get_edge_chain` (quad-strip walkers),
  `subdivide_face`, `rewind_face_outward` (per-face winding fix),
  `bevel_edges`/`bevel_edges_profiled` (Blender-style bevel: edge consumed, one
  strip quad per edge bridging offset lines p_distance into each adjacent
  face; meeting edges mitred to shared corner verts; collinear chains
  share verts into one continuous strip),
  `flip_faces`/`set_faces_flipped`.
- `EdgeKey {int a,b; ordered}` + `EdgeKeyHasher` used for edge identity;
  `get_edges()` (unique set) and `get_open_edges()` (used by <2 faces =
  surface boundary; dashed in overlays).
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
  set_vertex, bridge_edges, set_face_material, set_all_face_materials) and
  the serialized setters (covers undo/redo), and
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

- **Armed actions**: toolbar actions with dock settings (currently Edge >
  Bevel) arm instead of applying: `armed_action`/`armed_values` on the
  screen; the dock builds a SpinBox form from `LevelActionSetting`
  descriptors (`get_action_settings` in the dock cpp) and writes values
  back via `set_armed_value`; Enter runs `_action_apply_armed`, Esc
  cancels; `_set_tool`/`_set_target` cancel any armed action. To add a
  configurable
  action: enum entry in `ArmedAction` + descriptor list + a case in
  `_action_apply_armed`.

- **Element menu actions** (Vertex/Edge/Face toolbar menus) are DECLARATIVE:
  the `LEVEL_MENU_ACTIONS[]` table at the top of
  `editor/tools/level_editor_tools.cpp` maps (menu, id) -> a `_has_*`
  selection predicate; `_update_menu_states()` (called from
  `_update_overlays()`, which fires on every selection change, plus once in
  the ctor) enables/disables items. Bridge requires exactly 2 edges on the
  same brush. To add an action: one table entry + a case in the menu's
  `_*_menu_selected`.

- **Active material & drops**: the screen holds `active_material` (null =
  map default; the dock panel falls back to showing `LevelMap::
default_material`'s preview). New brushes get it in `_ghost_commit`
  (null leaves faces empty so the map default applies). Materials can be
  DRAGGED onto viewports - from the FileSystem dock ("files" payload:
  .material/.tres/.png/...), the dock's preview, or any inspector
  EditorResourcePicker ("resource" payload). Face target drops on the
  hovered face, every other target drops on the whole brush; textures are
  wrapped via the shared `LevelEditorMaterialCache`. Drop = undo-recorded
  (`_commit_brush_undo`), drop target gets an animated marching-ants
  highlight on a DEDICATED PreviewOverlay Control (so the animation redraw
  never repaints the main overlay).

- **PreviewOverlay rule**: every viewport has TWO overlay Controls - the main
  `Overlay` (brush outlines, gizmos, hover, clip/mirror planes) and the cheap
  `PreviewOverlay`. ANY content that animates or redraws per-frame (marching
  ants: the material-drop highlight, tool previews like the bevel) MUST draw
  on the `PreviewOverlay`, never the main `Overlay` - a per-frame repaint of
  the main overlay re-runs every brush outline/gizmo and tanks FPS (GOTCHAS
  #33). Redraw it via `_queue_preview_redraw()`; `_update_overlays()` queues
  both so state changes stay in sync.

- **Extrude material inheritance** (level_modifiers.cpp): `extrude_face`
  cap keeps the source material; each appended wall inherits from the
  NEIGHBOR face across its seam edge (floor continues into hallways).
  `extrude_edge` walls inherit from the using face whose normal best
  matches the WALL's normal (floor-like walls continue floors).
  `extrude_vertex` wedges inherit per stitched face.

- **Open-edge rendering**: `LevelBrush::get_open_edges()` (edges used by <2
  faces) draws as dashed lines in all outline/hover/selection passes.
- **Tool previews**: generic `ToolPreview` struct (preview id, source
  brush, local-space line pairs, cache-hash). Each tool owns a producer
  (`_bevel_preview_rebuild` builds from the armed values, keyed by a
  hash of brush + selection + values); `_draw_tool_preview` draws with a
  per-id color. Add a preview: enum entry + color case + producer.

- `Tool` × `SelectionTarget` (Hammer 2 style): Tool = Select/Move/Rotate/
  Scale (Q/W/E/R) + modal Block/Clip/Mirror (stashes and restores
  `last_transform_tool`/`last_target`); Target = Vertex/Edge/Face/Mesh
  (1/2/3/4). `_set_tool`/`_set_target` end active drags first (committing
  their undos).
- **No-map gate**: no `LevelMap` in the scene → warning panel + "Create
  LevelMap" button instead of viewports. `_update_map_ui()` only ADOPTS a
  found map (never auto-creates); `edited_scene_changed()` re-resolves.
- Selection: Mesh target multi-selects (`selected_brushes` authoritative,
  `selected_brush` = primary/last-clicked for inspector + single-brush
  handles); element targets keep per-brush `HashMap<LevelBrush *,
HashSet<...>>` sets (cross-brush selection). `_set_target` clears
  selection; LMB-drag paint-selects crossed elements (add-only).
- Hover/selection fills pre-flight `Geometry2D::triangulate_polygon`
  before `draw_colored_polygon` (GOTCHAS #47); on failure the fill is
  skipped, outline still draws.
- Editor selection sync is bidirectional and exact: level-editor selection
  changes mirror into `EditorSelection` (`_sync_editor_selection`, diff-based,
  guarded by `applying_editor_selection` against echoes); editor selection
  changes mirror back via `apply_editor_selection(nodes)` (replace vs.
  accumulate matches the scene tree). Inspector: 1 brush → `edit_node`, 2+ →
  `MultiNodeEdit` (like SceneTreeDock's TOOL_MULTI_EDIT). Map lookup:
  `_find_map_in_scene()` (single DFS) wrapped by `_update_map_ui()`.
- View menu: per-viewport display modes (ids = vp*MAX+mode) + global
  2D/3D grid toggles + per-viewport "View Information"/"View Frame Time"
  HUD toggles (same as the 3D editor; updated per-frame in
  NOTIFICATION_PROCESS, render-time measurement enabled only while shown),
  persisted via project metadata.
- Block flow: drag → editable ghost (AABB + handles + dim labels) → Enter
  commits / Esc cancels. All handle rules funnel through ONE predicate
  `_box_handle_usable(vp, handle, flat_axis)` (shared with select-mode
  handles; GOTCHAS #49).
- Clip flow: 2 snapped points + captured view dir; plane normal =
  `along × view_dir`. Toolbar re-click cycles keep-left/right/both.
  Preview: green kept / red discarded edges + cut markers. Enter applies.
- Gizmos (screen-space, custom): move arrows + plane quads + center,
  rotate rings (ortho restricts to view axis), scale axis/uniform handles,
  Select-tool AABB resize handles. Pick and draw share size math
  (`compute_gizmo_axes`, `_rotate_world_radius` - GOTCHAS #25).
  Shift+drag in any element target extrudes (faces pull caps, edges/verts
  duplicate + stitch; selection tracks new geometry for chained extrudes).
  Drags snapshot serialized state at begin and apply absolute deltas;
  undo = `_commit_brush_verts_undo` (`commit_action(false)`).
- Keys (handled in `LevelEditorScreen::input()`, `_vp_input` phase, swallowed
  via `set_input_as_handled`): Delete (per-mode delete/collapse), `[`/`]`
  grid ladder (`LevelEditorGrid::STEPS`, 1/8..512), Enter/Esc (ghost/clip/
  mirror/armed commit/cancel, drag cancel). `shortcut_input` on screen+viewport also accepts them as a
  fallback. Brackets in `forward_input` are intentionally skipped to avoid
  double-handling.
- **All overlay colors** live in `level_constants.h` (module root,
  `LevelEditorColors` namespace) - never hardcode `Color(...)` literals in
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
  `forward_input` maps camera → viewport and dispatches to per-tool input
  handlers in priority order (`_select_handles_input` → `_rotate_input` →
  `_gizmo_input` → `_brush_input` → `_clip_input` → `_mirror_input` →
  `_selection_input`); each lives in its tool's .cpp and returns true when
  it consumed the event.
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

## Checklist: adding a LevelBrush geometry op

Every geometry op follows the same recipe - skipping a step produces the
bugs GOTCHAS is full of (stale previews, desynced materials, undo gaps,
inward walls):

1. **Topology in `level_modifiers.cpp`** (or `level_brush.cpp` for trivial
   ops). Winding of any new face is verified against the brush CENTROID
   (plane-side test), never a pull/normal sign rule (GOTCHAS #30).
2. **Face materials stay aligned**: `face_materials` must match `faces`
   1:1 (`_update_face_count_storage()`), and new faces inherit from the
   geometrically continuing face (seam neighbor / best-normal match -
   GOTCHAS #37), not null.
3. **`_notify_map_changed()`** at the end so the preview rebuilds (also
   covers undo/redo restores).
4. **Undo**: callers record via `_commit_brush_undo`/`_add_brush_undo_pair`
   (vertices + faces + face_materials - GOTCHAS #27). If the op runs live
   during a drag, commit with `commit_action(false)`.
5. **Compact or document**: if the op can orphan vertices, call
   `compact_vertices()` (and remember it REMAPS indices - selections/tests
   must compare positions, not indices). If it deliberately doesn't
   (delete_faces), note it in ROADMAP/TESTS.
6. **Selection staleness**: if indices shift, the editor-side caller clears
   or remaps the per-brush selection sets (GOTCHAS #14).
7. **Test in `tests/test_level_brush.h`**: topology counts, outward
   normals, material inheritance, and a degenerate case (flat quad,
   zero distance, open edge). Pure geometry - no tags needed.

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
