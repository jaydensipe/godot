# Level Editor - Tests

## Where

`modules/level_editor/tests/` — picked up automatically by the engine's test
harness (module `tests/*.h` headers are globbed by `modules/SCsub` into
`modules_tests.gen.h`). No SCsub changes needed.

## Build & run

From the repo root:

```
scons platform=windows target=editor dev_build=yes accesskit=no d3d12=no angle=no tests=yes
bin\godot.windows.editor.dev.x86_64.exe --test
```

Run only this module's cases (doctest filter):

```
bin\godot.windows.editor.dev.x86_64.exe --test --test-case="*[LevelBrush]*"
bin\godot.windows.editor.dev.x86_64.exe --test --test-case="*[LevelMap]*"
```

## Coverage

**`test_level_brush.h`** — pure geometry, no tree needed:

- `setup_box` topology (8 verts, 6 quads, 12 edges, outward normals)
- `move_vertices` isolation (only the moved vertex changes; incident faces tilt)
- `ray_intersect` (entry face, distance, miss)
- `extrude_face` (cap + side walls, counts)
- `clip` (front kept + cap with correct outward normal; no-cap variant)
- `split_faces` (subdivide in place, no clipping, no caps)
- `subdivide_face` (quad -> 4 quads with material inheritance; n-gon ->
  triangle fan; invalid-index rejection)
- `flip_faces` / `faces_flipped` (flag-only; bake data inverts)
- `bridge_edges` (quad span, winding, rejection cases)
- `delete_faces` (materials stay aligned after removal)
- `collapse_vertices` (neighbor-average weld)
- `get_edges` (uniqueness, canonical a<b ordering)
- `get_edge_loop` (Blender alt-click ring: 4 parallel edges on a box,
  terminates at n-gon faces and open boundaries; walks share a visited set
  so closed rings meet in the middle instead of duplicating)
- `get_edge_chain` (collinear segments through shared verts, e.g. the two
  halves of a subdivided straight edge - double-click in Edge mode)
- `bevel_edges` (chamfer: edge replaced by a new face; verts slide along
  the boundary angle bisector by distance/cos(half-angle), winding flipped
  to point out of the original volume)
- `get_face_center` / `get_center` (unit-box geometry)
- serialization round-trip (`vertices`/`faces`/`face_materials` properties)
- bake/collision triangle counts

**`test_level_helpers.h`** — pure drag/picking math, no tree needed:

- `axis_drag_plane` (contains axis + point, faces camera, degenerate
  fallbacks when the camera is on the axis, top-view vertical-drag case)
- `closest_point_on_line_to_ray` (basic solve, near-axis-parallel ray,
  parallel-line rejection)

**`test_level_map.h`** — tagged `[SceneTree]` (needs the test harness's
physics server for `StaticBody3D` construction — untagged cases crash):

- bake output hierarchy (mesh, collision, occluder)
- per-material surface grouping (face mats + map default)
- brush transform applied to baked geometry
- empty map bakes to nullptr

## Not covered (and why)

- **`LevelEditorScreen` / viewports / gizmos / input handling** — UI and
  editor-integration code; not headless-testable without an editor harness.
  Test indirectly via brush-level ops where possible.
- **Undo/redo paths** — tied to `EditorUndoRedoManager` (editor-only).

## Gotchas for future test authors

- Anything constructing physics nodes (`StaticBody3D`, `CollisionShape3D`)
  must be tagged `[SceneTree]` (or `[Editor]`) — the harness only starts a
  physics server for those tags. Otherwise you get a SIGSEGV in
  `PhysicsBody3D::PhysicsBody3D`.
- `clip()` leaves orphaned vertices in the array (faces don't reference
  them). Assert on face-referenced verts, not the whole array.
- `LevelMap::bake()` falls back to local transforms when detached from the
  tree, but in `[SceneTree]` tests the map/brushes are in-tree and use
  globals — write assertions accordingly.
