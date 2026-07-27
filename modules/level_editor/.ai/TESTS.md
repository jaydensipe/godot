# Level Editor - Tests

Note: when a bug is fixed or a feature lands, if it can be tested headlessly
(pure geometry / logic), please add a test in the same change - a regression
test for fixes, invariant tests for features.

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
- `setup_quad` (4 verts, 1 face, outward normal from winding; reversed
  winding flips it) - the Quad brush type's commit path
- `move_vertices` isolation (only the moved vertex changes; incident faces tilt)
- `ray_intersect` (entry face, distance, miss)
- `extrude_face` (cap + side walls, counts)
- `extrude_edge` (edge dup + one wall per using face, winding verified
  outward via Newell-vs-center flip; duplicated vert positions)
- `extrude_vertex` (vert dup + one wedge per using face, same outward
  verification)
- `clip` (front kept + cap with correct outward normal; no-cap variant)
- `split_faces` (subdivide in place, no clipping, no caps)
- `subdivide_face` (quad -> 4 quads with material inheritance AND
  neighbor boundary edges split at the new midpoints - no T-junctions;
  n-gon -> triangle fan; invalid-index rejection)
- `flip_faces` / `faces_flipped` (flag-only; bake data inverts)
- `bridge_edges` (quad span, winding, rejection cases)
- `delete_faces` (materials stay aligned after removal)
- `collapse_vertices` (neighbor-average weld)
- `get_edges` (uniqueness, canonical a<b ordering)
- `get_open_edges` (closed box: none; flat quad: all 4; uncapped clip opens
  exactly the 4 cut-plane edges)
- `set_all_face_materials` (faces start null, all assigned)
- extrude material inheritance (face: cap keeps source mat, walls inherit
  seam NEIGHBOR's - the hallway case: floor-like wall goes red; edge: wall
  inherits the using face whose normal matches the WALL normal, floor and
  wall pull cases; vertex: per-stitched-face)
- `get_edge_loop` (Blender alt-click ring: 4 parallel edges on a box,
  terminates at n-gon faces and open boundaries; walks share a visited set
  so closed rings meet in the middle instead of duplicating)
- `get_edge_chain` (collinear segments through shared verts, e.g. the two
  halves of a subdivided straight edge - double-click in Edge mode)
- `bevel_edges` (Blender-style: edge consumed, one strip quad bridging
  lines offset p_distance into each adjacent face, measured along the
  boundary edges; corners shared/mitred between meeting edges; collinear
  chains produce one continuous strip with shared corner verts; rejection
  of open edges and zero/negative distance)
- `bevel_edges_profiled` (segments + shape: steps=0 == bevel_edges single
  cut; steps>=1 subdivides the WHOLE cross-section into 2N band quads;
  shape profiles it: 0 = straight chord (flat chamfer), 0.5 = quadratic
  Bezier through the original corner (apex halfway between chord and
  corner), 1 = original face segments (bulge back to the corner))
- `mirror` (verts reflected across the plane, winding reversed so normals
  stay outward - reflection flips chirality)
- `compact_vertices` (clip without cap leaves zero unreferenced verts)
- `get_edge_chain` invalid-edge tolerance (default EdgeKey is UB-safe)
- `delete_faces` duplicate-index dedup
- `split_faces` weld regression (intersections near existing verts must
  create new verts, not weld to them)
- `bevel_edges_profiled` collinear chain with steps>=1 (bounds sanity -
  guards the bowtie regression)
- `get_face_center` / `get_center` (unit-box geometry)
- `get_face_normal` invariants (unit length, planar cross match, bent
  quad robustness, degenerate-face (0,1,0) fallback)
- `ray_intersect` edge cases (miss leaves r_dist untouched, inside-out
  exit-face hit, face-parallel miss)
- `clip` with a diagonal plane (cap is planar, on-plane verts, stored
  normal faces the removed side)
- `rewind_face_outward` (no-op on outward face, reverses an inward one)
- `mirror` round-trip (double mirror = identity; face materials survive)
- `compact_vertices` no-op stability (verts + loop indices untouched when
  nothing is orphaned)
- extrude winding regressions: quad extruded downward on all 4 sides
  (every wall faces out - the flat-quad degenerate case, GOTCHAS #30);
  cube top edge x 4 pull directions (centroid-side rule);
  `extrude_face` on an interior (flipped) brush
- serialization round-trip (`vertices`/`faces`/`face_materials` properties)
- bake/collision triangle counts

**`test_level_helpers.h`** — pure drag/picking math, no tree needed:

- `axis_drag_plane` (contains axis + point, faces camera, degenerate
  fallbacks when the camera is on the axis, top-view vertical-drag case)
- `closest_point_on_line_to_ray` (basic solve, near-axis-parallel ray,
  parallel-line rejection)
- `closest_point_on_segment_2d` (interior projection, endpoint clamps,
  degenerate segment)
- `clip_segment_to_rect` (inside span, slab crossings, diagonal corner
  entry, parallel-outside + slab-non-overlap + degenerate rejection)

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
- ~~`clip()` leaves orphaned vertices in the array~~ (fixed) and bevel
  consumed-edge endpoints (fixed): clip/bevel/weld/collapse all call
  `compact_vertices()`. CONSEQUENCE for tests: never assert on raw vertex
  INDICES after these ops - compaction reuses indices; compare positions.
- Geometry tolerances live in `LevelBrushConstants` (level_constants.h,
  module root): `PLANE_EPSILON` 0.0005, `WELD_DIST` = 4x that,
  `PARALLEL_DOT` 0.999, `WINDING_SIDE_EPS` 0.001, `BEVEL_MITRE_MIN_SIN`
  0.05, `BAKE_UV_SCALE` 0.25.
- `LevelMap::bake()` falls back to local transforms when detached from the
  tree, but in `[SceneTree]` tests the map/brushes are in-tree and use
  globals — write assertions accordingly.
