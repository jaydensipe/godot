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
- `clip` (front kept + cap with correct outward normal)
- `split_faces` (subdivide in place, no clipping, no caps)
- `flip_faces` / `faces_flipped` (flag-only; bake data inverts)
- `bridge_edges` (quad span, winding, rejection cases)
- `delete_faces` (materials stay aligned after removal)
- `collapse_vertices` (neighbor-average weld)
- serialization round-trip (`vertices`/`faces`/`face_materials` properties)
- bake/collision triangle counts

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
