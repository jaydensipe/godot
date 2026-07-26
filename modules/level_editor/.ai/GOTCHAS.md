# Level Editor - Hard-Won Gotchas

Bugs that cost real debugging time. Read before touching the module.

## Editor input & shortcuts

1. **Scene-tree Delete fires regardless of focus.** `SceneTreeDock::shortcut_input`
   has no shortcut context and runs whenever no text field is focused.
   `accept_event()` in a focused control's `shortcut_input` only works if that
   control IS in the shortcut context - and `EditorInterface::edit_node()`
   yanks focus away constantly. FIX: handle keys in `LevelEditorScreen::input()`
   (the `_vp_input` phase, runs before gui/shortcut dispatch) and call
   `get_viewport()->set_input_as_handled()`. Screen also needs
   `set_process_input(true)`. Keep a swallow-only `shortcut_input` as backup.

2. **Tab is eaten by GUI focus navigation.** Never use Tab for tool actions;
   put the action on a toolbar button instead (that's why clip-side cycling
   is on the Clip button itself).

3. **Toolbar buttons keep focus after being clicked**, so subsequent keypresses
   re-trigger the button. Call `release_focus()` in every toolbar callback and
   after mode changes.

4. **SpinBox `step` rounds `set_value()`.** A step of 0.25 destroyed the
   grid ladder (values rounded to 0). Match step to the smallest
   ladder value (currently 1/8, `LevelEditorGrid::STEPS`).

## Geometry / math

5. **Plane-set brushes can't do free vertex editing.** A quad face must stay
   planar, so moving a vertex via plane tilts drags distant vertices along
   (the reason the data model was switched to explicit topology). Don't go back.

6. **Ortho ray casts are parallel.** `Plane::intersects_ray` from an
   orthographic camera FAILS for planes perpendicular to the view axis (ray
   parallel to plane, or offset-parallel). Face-handle drags must intersect
   the viewport's EDIT PLANE (through the handle/center) and take one axis
   component - never raycast against the dragged face's own plane.

7. **`looking_at` with colinear up.** Top-view camera looking straight down -Y
   with up +Y warns "Target and up vectors are colinear". Build ortho camera
   transforms from an explicit Basis instead.

8. **Camera basis orientation per ortho view flips pan direction.** Top view:
   camera up = world -Z (pan feels inverted); side view: camera right = -Z.
   Pan formula needs per-view axis negation. Same reason the clip "left of
   line" normal had to be verified as `along × view_dir`.

9. **`dist` indexed by loop position vs vertex index** (crash in
   `split_faces`): distances were built per loop position but read per vertex
   index - out-of-bounds. Keep loop-position vs vertex-index bookkeeping
   explicit everywhere.

10. **Winding: stored loops are CCW-outward (Newell), Vulkan is CW-front.**
    `setup_box` loops report outward normals via Newell's method, which means
    they appear counter-clockwise from outside. Godot/Vulkan rasterizes
    clockwise-front, so emitting stored loops as-is makes the EXTERIOR the
    back face (culled -> you see the interior). FIX: the bake boundary
    (`get_bake_surface_data` / `get_collision_faces`) reverses winding for
    the default (solid) bake and emits loops as-is for `faces_flipped`
    (interior). Vertex normals are unaffected (outward = solid). RULE: any
    new geometry->render path must emit Vulkan-convention (clockwise-front)
    triangles; convert at the boundary, never change the stored topology.

## Undo

11. **`commit_action(false)` skips do-methods.** Used for live-applied changes
    (gizmo drags). But if any do-method must actually RUN now (e.g.
    `add_child` for a split brush), use `commit_action(true)`. A split once
    silently dropped the second half because of this.

12. **Per-plane/per-face undo entries are fragile.** Record whole serialized
    properties (`vertices`, `faces`, `face_materials`) in one pair - safe
    across face-count changes and keeps the inspector in sync.

13. **Node-creating undo actions need `add_do_reference`.** If the action
    does `add_do_method(parent, "add_child", node)` +
    `add_undo_method(parent, "remove_child", node)`, the node is freed on
    undo and redo re-adds a dangling pointer. Add `add_do_reference(node)`
    (node exists at commit time) or `add_undo_reference(node)` (node is
    created by the do side). (`_ghost_commit` / `_bake_pressed` have it;
    `_delete_selection` uses `add_undo_reference`.)

14. **Face/vertex-index selections go stale after topology ops.** Any action
    that adds/removes verts/faces (subdivide, delete, clip, extrude)
    invalidates `selected_faces`/`selected_edges`/`selected_vertices`
    indices. Either clear the selection after the op (subdivide/delete do
    this) or remap it (the gizmo extrude updates selection to the new cap
    faces / duplicated verts). EXCEPTION: `extrude_face` replaces each
    source face with its cap in place, so a face selection stays valid
    across extrudes (chained extrude relies on this). UNDO SIDE: an op
    that remaps the selection to NEW indices must also record
    `add_undo_method(screen, "clear_selection")` - undo restores the
    pre-op `vertices`/`faces` arrays (fewer elements), and the stale
    remapped indices then read out of bounds in every draw/pick (the
    "Index p_index is out of bounds" spam after undoing an extrude).
    MULTI-ELEMENT SIDE: parallel per-element arrays keyed by SEPARATE
    HashMaps (`gizmo_extrude_cap_faces` vs a flat
    `gizmo_extrude_normals`) silently misalign once two elements are
    involved - HashMap iteration order is per-map, and a shared flat
    counter walked off the end (OOB in `get_face_normal` when Shift+
    dragging two faces). Keep per-element data in ONE per-brush struct
    (`gizmo_extrude_cap_normals` alongside `gizmo_extrude_cap_faces`),
    iterated in lockstep.

15. **`_set_tool`/`_set_target` must interrupt active drags.** Tool/target
    shortcuts can fire mid-drag; the drag's undo action commits against the
    old state snapshots, so both setters end gizmo/rotate/select-handle
    drags (committing their undos) before switching. Any NEW drag state
    must be added there too, or its edit becomes un-undoable.

## Rendering

16. **Black tops on fresh brushes were NOT normals.** Winding was provably
    correct (Newell check printed correct normals). Culprits were (a) scene's
    own WorldEnvironment/light, (b) the level viewports' single directional
    light pointing the wrong way - set light direction with
    `look_at_from_position`, don't hand-tune Euler rotations. Also preview
    instance gets `SHADOW_CASTING_SETTING_OFF`.

17. **Overlays bleed into other viewports** unless the SubViewportContainer
    has `set_clip_contents(true)`.

18. **SubViewports share the edited scene's World3D** (own_world_3d=false) -
    brush previews render because of this. Lights/cameras added under them
    live in the same world. (User explicitly chose separate lighting over
    sharing the 3D editor's world via `set_world_3d`.)

19. **Preview refresh:** `LevelMap::refresh()` calls `_update_preview()`
    immediately (not deferred) so gizmo drags stay in sync.

20. **Undo/redo restores bypass the editing code paths.** `EditorUndoRedoManager`
    sets serialized properties / `position` directly - the overlay outline
    (drawn live from brush data) updated, but the baked preview mesh stayed
    stale. FIX: `LevelBrush` notifies the parent map itself -
    `_notify_map_changed()` (deferred `refresh()`) from all serialized
    setters, and `NOTIFICATION_LOCAL_TRANSFORM_CHANGED` for node-position
    undo (brush has `set_notify_local_transform(true)` in editor; enabled in
    `NOTIFICATION_ENTER_TREE`). Deferred, not immediate: setters fire during
    scene load / before the brush is in the tree, and the 3 property restores
    coalesce into one rebuild.

21. **`EditorNode::scene_changed` signal connection failed** at runtime
    ("nonexistent signal") even though it's declared in `_bind_methods`.
    FIX: use the `EditorPlugin::edited_scene_changed()` virtual override
    instead - called via `EditorData::notify_edited_scene_changed()`.

22. **Projecting a behind-camera point mirrors it.** `Camera3D::
    unproject_position` on a point past the near plane returns a mirrored
    screen position, and `is_position_behind` rejects it - so any overlay
    that projects per-vertex vanishes ENTIRELY when one endpoint goes
    behind (grid lines once did; selected-face fills did). FIX: clip in
    camera space against the near plane before projecting. Use the shared
    helpers `LevelEditorViewport::project_segment` (lines) and
    `project_polygon` (face loops, Sutherland-Hodgman) - never raw
    `project()` per vertex in overlay draw code. Ortho cameras short-
    circuit (parallel rays can't cross the near plane).

23. **Module editor icons**: add `get_icons_path()` to `config.py` and drop
    SVGs in that folder - `editor/icons/SCsub` embeds them by filename into
    `EditorIcons`. Name one after a registered class (e.g. `LevelMap.svg`)
    and it becomes the scene-tree node icon automatically.

24. **SubViewports sharing a World3D render EVERYTHING in that world.** All 4
    level viewports share the scene's World3D, so `set_visible(false)` on one
    viewport's own MeshInstance3D doesn't keep it out of the other panes.
    Editor-only 3D content (the perspective grid mesh) must live on a render
    layer only the intended camera culls in (layer 20 + camera cull_mask).

25. **Pick and draw must share screen-size math.** The rotate ring was picked
    at unit world radius but drawn at a screen-constant radius - picking was
    offset at almost every zoom. Any gizmo with a pixel-constant size needs
    ONE world-size helper used by both paths (`_rotate_world_radius`).

26. **Restore-from-snapshot, never restore-by-recompute.** The select-handle
    resize "restored" original geometry by remapping current verts into the
    original AABB; a degenerate intermediate drag (zero extent on an axis)
    destroyed vertex data and the restore baked the loss in. Snapshot the
    serialized array at drag start (`select_drag_original_verts`) and
    `set_vertices_data()` to restore - same pattern as gizmo drags.

27. **Undo do/undo property lists must cover EVERY mutated property.**
    Extrude recorded `vertices`+`faces` but not `face_materials`, which
    `extrude_face` also mutates - undo left materials desynced from faces.
    When in doubt use `_add_brush_undo_pair` (records all three).

28. **Don't bail the whole gizmo when one axis is camera-behind.** Early code
    returned GIZMO_NONE / skipped drawing entirely if ANY axis tip was behind
    the camera - the gizmo vanished at grazing angles. Skip per-axis
    (`axis_ok[]`), and gate plane handles on BOTH their axes being visible.
    Pick and draw must use the same plane-handle extent
    (`LevelEditorColors::GIZMO_PLANE_EXTENT`).

29. **Plane keep-side semantics:** `Plane::distance_to(p) = normal.dot(p)-d`;
    `clip()` keeps `distance >= -eps`. A clip plane at +X normal, d=5 keeps
    only x>=5 (clips a unit box at origin AWAY) - the tests got this wrong
    once (no-op plane needs the brush fully on the keep side).
43. **Stitched-wall winding: test the centroid's SIDE of the wall plane,
    and re-wind as the drag moves.** Getting `extrude_edge`/
    `extrude_vertex` walls to face out of the solid (Hammer behavior,
    user-verified in Hammer 2) burned FIVE sign rules before landing:
    - Every rule built from `edge_dir x pull` / `edge_dir x bisector` /
      dots against `normal_sum` has a degenerate case: flat quads
      (normal_sum perpendicular to every wall -> sign flip is a no-op,
      3 of 4 walls arbitrary), convex edges (bisector cross points
      inward depending on loop order), bisector-parallel pulls (cross
      perpendicular to the wall, useless).
    - `normal.dot(centroid - wall_center)` (direction dot) is ALSO
      degenerate: the 45-degree bevel plane from a bisector pull passes
      exactly through the centroid.
    What works: `wn.dot(centroid - wall_point)` - the wall normal must
    point AWAY from the brush centroid as a PLANE-SIDE test. The
    centroid is strictly inside for any real pull, so it never
    degenerates except the begin-drag stub (bisector bevel through the
    centroid), which falls back to the bisector side.
    **The stub freeze is the second half of the bug**: winding is decided
    at extrude time from the 0.001 stub while the drag rotates the wall
    plane, so NO once-computed rule is correct for all final positions.
    `_apply_gizmo_delta` (edge/vertex extrude branch) re-winds every
    appended wall face (indices >= pre-extrude face count) each frame via
    `LevelBrush::rewind_face_outward()`. Undo is safe because
    `_add_brush_undo_pair` reads the CURRENT faces for the do-side.
    Reminder: a mirrored-looking texture is always a WINDING problem -
    bake UVs are position-based planar projections, loop-order
    independent. Don't touch the UV code for this (one speculative UV
    "fix" was already reverted).

44. **Off-gizmo LMB swallows block selection clicks.** `_gizmo_input` runs
    BEFORE `_selection_input` in `forward_input`. The Scale tool had a
    "click anywhere to uniform-scale" affordance that consumed EVERY LMB
    press when a selection was active - so once a brush was selected, no
    other brush could be clicked (selection worked only while empty).
    FIX: the off-gizmo uniform-scale swallow was REMOVED; off-gizmo clicks
    fall through to `_selection_input` in every transform tool, and
    uniform scale lives on the gizmo's center handle (plane pick ->
    uniform factor). Any future "click anywhere to drag" affordance must
    not eat plain clicks that the user needs for selection. Also: the
    Mesh-target hover highlight was gated on `tool == TOOL_SELECT`, which
    made it look like picking itself was broken in Rotate/Scale -
    diagnostic prints on INPUT are useless when the bug is in DRAWING.

45. **Scale-tool paths must not run during an extrude drag.** Shift+drag
    extrude duplicates topology at begin-drag; the drag then restores
    `gizmo_extrude_moved_verts` (POST-extrude count) each frame. The
    Scale tool's `_apply_gizmo_scale`/`_apply_gizmo_scale_uniform`
    instead restored `gizmo_drag_brush_verts` - the PRE-extrude snapshot
    - shrinking `verts` below the extruded faces' loop indices; the next
    `_refresh_map()` crashed in bake (`verts[f[i]]` OOB in
    `get_face_normal`). FIX: `_gizmo_drag_to` routes ALL extrude drags to
    `_apply_gizmo_delta` (which has the extrude-aware restore) regardless
    of tool. RULE: any per-frame drag-apply path that restores a vertex
    snapshot must know whether the drag extruded - check
    `gizmo_extrude_drag` before touching `set_vertices_data`.

## Serialization

30. **Runtime classes must not live under `editor/`.** `LevelBrush`/`LevelMap`
    register at SCENE level and exist in exported games; the module SCsub only
    builds `editor/*.cpp` for editor builds. They were moved back to module
    root after an export-breaking placement.

31. **Brush persistence needs plain properties.** C++ members don't save;
    `vertices`/`faces`/`face_materials`/`faces_flipped` are real properties.
    Old scenes saved before this have empty brushes - recreate them.

32. **Bevel: edge CONSUMED; strip cross-section profiled by shape.** After
    several wrong models, the working one: each beveled edge is consumed;
    every adjacent face gets an offset line at distance d (measured
    ALONG the boundary edges); the strip between them is either ONE quad
    (steps=0) or 2*steps band quads whose iso verts follow the profile:
    shape 0 = straight chord (flat chamfer), 0.5 = quadratic Bezier
    A->corner->B (apex halfway between chord mid and the corner - NOT on
    it), 1 = the original face segments (full bulge, no visual bevel).
    The per-side "retained centerline" model was geometrically flat on
    the original faces (invisible bevel) - the strip must span the gap.
    Corners where beveled edges meet miter to ONE shared vert per
    (vertex, face); collinear chains share via the same mechanism.
    Faces touching an endpoint are trimmed along the profile polyline
    (skip faces bordering another selected edge at that vertex - the
    bowtie X). All corner positions come from ORIGINAL topology
    (two-pass: gather, then apply). A corner whose two runs are
    collinear (chain midpoint on a face boundary) has NO bisector -
    use face normal x edge-dir toward the face centroid, or every edge
    touching it gets rejected.

33. **subdivide_face must split the NEIGHBORS' shared edges too.** The quad
    grid creates midpoint verts on the subdivided face's boundary, but
    neighboring faces kept their original long edge - a T-junction
    (render cracks, and the boundary edge no longer exists in the
    neighbor's loop, so bevel's adjacency search found only ONE face and
    refused - "top edges of a subdivided face won't bevel"). FIX: after
    rewiring the subdivided face, insert each midpoint into any other
    face whose loop contains that boundary run consecutively. The n-gon
    fan path adds no boundary verts, so it needs no such fix.

34. **Clip seam dedup: near-plane kept verts must SNAP into the cap's
    weld set.** `clip()` classified verts within epsilon as "inside" and
    emitted the ORIGINAL vert, while crossing edges produced a WELDED
    intersection vert - two distinct seam verts within WELD_DIST of each
    other (one drives the kept face, the other the cap; dragging showed
    exactly that split). FIX: kept verts within WELD_DIST of the plane
    are projected onto it and routed through the same weld lambda, plus
    a consecutive-duplicate cleanup per loop.

## SCons/module mechanics

36. Module SCsub env flag is `env.editor_build`, not `env["tools"]`.
37. `initialize_<foldername>_module` must match the folder name exactly
    (module was renamed `leveleditor` → `level_editor` mid-project).
38. Clean stale `__pycache__` in the module dir after renames.
39. `Math::pow(2.0, step)` was "ambiguous" on MSVC - hardcoded a ladder array
    instead (simpler anyway).
40. **`draw_colored_polygon` ERR_FAILs on untriangulable polygons.** Projected
    n-gon fills (face hover/selection overlays) can be degenerate in screen
    space - viewed edge-on, or concave/self-intersecting after vertex edits -
    and `RendererCanvasCull` errors once per redraw (log spam on every hover).
    Pre-flight with `Geometry2D::triangulate_polygon` and skip the fill on
    failure; keep drawing the outline.
41. **Member names collide with locals.** Naming the selection-target member
    plain `target` shadowed ~15 existing `LevelBrush *target` locals (C4458
    warnings everywhere). Module state members that describe editor concepts
    (tool, target, mode) need qualified names (`selection_target`).
42. **Flat (zero-extent) ghost AABBs break handle math in edge-on views.**
    A quad ghost projects to a LINE in the two ortho views looking
    perpendicular to its normal: point-in-polygon hit tests can never hit,
    and all 8 corner handles stack onto one line. Funnel picking,
    inside-drag, AND drawing through ONE predicate
    (`_ghost_handle_usable`) instead of parallel filters - the first
    version drifted into `_quad_flat_axis`/`_quad_edge_on`/
    `_edge_on_handle_axis` helpers and shipped with the edge-on test
    inverted (face-on vs edge-on view axis), which removed ALL handles
    from the one usable view.
