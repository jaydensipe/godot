# Level Editor - Hard-Won Gotchas

Bugs that cost real debugging time. Read before touching the module.

Entries are one flat numbered list - append new ones at the end. (The file
used to be grouped into sections; the legacy grouping was: 1-4 editor input
& shortcuts, 5-10 geometry/math, 11-15 undo, 16-37 rendering, 38-42
serialization, 43-49 SCons/module mechanics. Section headers were removed
because new entries kept landing in ambiguous sections.)

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
30. **Stitched-wall winding: THE 0.001 STUB MAKES BEGIN-DRAG WINDING
    UNKNOWABLE - decide it from REAL geometry, per-frame.** This one burned a
    full day of flip-flopping between rules (centroid, perpendicular-face,
    coplanar-face, pull-direction) before the actual culprit surfaced. THE
    RULE THAT WORKS (Hammer 2-verified across solid cube walls, the clipped
    flipped dome brim, and the flat quad): a new wall stitched across a seam
    edge must be wound to CONTINUE the using face it is most PARALLEL to in
    its current orientation - align the wall's normal with that face's normal
    (`rewind_edge_wall`). This is purely local (no centroid, no `faces_flipped`
    check, no open-edge heuristic) and correct for solids, open shells, flat
    quads, and interior brushes alike.
    - **THE REAL BUG WAS THE STUB, NOT THE RULE.** Shift+drag extrudes call
      `extrude_edge` ONCE at begin-drag with a 0.001 stub offset along the
      face-normal BISECTOR. That stub's direction ties between the two using
      faces of a convex edge, and its Newell normal is numerically unstable
      (~0.02 magnitude, direction dominated by float rounding). Any rule that
      reads the provisional wall geometry AT EXTRUDE TIME picks a near-random
      reference face - and the rounding shifts with ABSOLUTE world
      coordinates, so the SAME edge wound up on a brush at the origin and
      down on the identical brush drawn off-origin (two walls up, two down).
      We chased "position-dependent winding" for hours before the debug dump
      showed identical local geometry with different ref-face picks.
    - **THE FIX: two-phase winding.** `extrude_edge` winds the wall with the
      plain manifold seam rule (traverse opposite `using_faces[0]`) as a
      deterministic placeholder, then immediately calls `rewind_edge_wall` -
      which, for a REAL (non-stub) offset, lands the correct final winding.
      The gizmo drag ALSO calls `rewind_edge_wall` PER FRAME (both
      `_apply_gizmo_delta` and `_apply_gizmo_scale_extrude` edge branches,
      tracked via `gizmo_extrude_wall_edges` = original seam edge per wall)
      because the stub's initial winding IS wrong and only corrects once the
      drag rotates the wall to its real plane. Do NOT remove the per-frame
      re-wind assuming "seam winding is topological and can't go stale" -
      that was true only when the wall was wound correctly to begin with;
      the stub breaks that assumption.
    - `rewind_edge_wall(wall, a, b)` finds the (up to two) source faces
      sharing the seam edge and flips the wall iff its normal dots NEGATIVE
      against the most-parallel one. Stable on real geometry, order/position
      independent.
    - `extrude_face` walls are a fixed band (src[i]->src[j] on the source
      edge, base+j->base+i on the cap edge) - consistent with the cap, no
      per-frame re-wind needed (the cap keeps the source winding, so the band
      stays coherent as it slides). `extrude_vertex` wedges are
      (v, prev, new_v, next).
    - `rewind_face_outward` (centroid, outward-only) survives ONLY for
      `setup_sphere` (fresh convex solid). The old centroid re-wind of
      extruded walls was removed - it fought the correct winding.
    - `faces_flipped` is a pure RENDER toggle (bake emits loops as-is);
      topology ops must ignore it.
      Reminder: a mirrored-looking texture is always a WINDING problem -
      bake UVs are position-based planar projections, loop-order
      independent. Don't touch the UV code for this (one speculative UV
      "fix" was already reverted).
    - DEBUGGING LESSON: when a winding bug looks position- or draw-direction-
      dependent, suspect a degenerate stub/provisional normal feeding the
      decision BEFORE inventing a new geometric rule. `print_line` the local
      geometry (edge verts, using-face loops/normals/traversals, the chosen
      reference) on the exact failing input - the dump is what broke this
      open.

31. **Off-gizmo LMB swallows block selection clicks.** `_gizmo_input` runs
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

32. **Scale-tool paths must not run during an extrude drag.** Shift+drag
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

33. **Near-plane-clipped projections can be ASTRONOMIC.** Clipping a segment
    against the near plane maps the crossing point onto the plane, which
    unprojects to a screen point hundreds of thousands of pixels off-screen
    (perspective asymptote). Any code that then iterates over the projected
    length explodes: the marching-ants dash walk generated millions of
    draw_line calls per frame when the camera got close to an edge
    ("more ants as I get closer" = the lag repro). FIX: clip projected
    segments to the overlay rect BEFORE iterating
    (`LevelHelpers::clip_segment_to_rect`, slab test), and skip fills whose
    verts exceed a sane coordinate bound.

33b. **Per-frame animation belongs on the PreviewOverlay, never the main
Overlay.** The bevel preview's marching-ants originally animated by
calling `_update_overlays()` every frame, which repaints ALL FOUR
viewports' main overlays (every brush outline/gizmo/hover) - FPS tanked
even though the dash walk itself was clipped/cheap. Rule: anything that
animates or redraws per-frame (ants, drop highlight, tool previews) draws
on the cheap `PreviewOverlay` and redraws via `_queue_preview_redraw()`.
`_update_overlays()` queues both, so state changes (arm/disarm/edits)
still sync. These are separate costs from #33: #33 is too many draw calls
in ONE paint; this is repainting the EXPENSIVE overlay too OFTEN.

34. **A drag highlight in the hover color/width is invisible.** The face-mode
    drop highlight drew green 2px dashes over the regular green 2px solid
    hover outline - perfectly camouflaged; hours of "it doesn't draw"
    debugging while every pipeline stage was provably working. Drop-target
    visuals use the SELECTED (orange) colors + thicker width precisely so
    they can't be mistaken for a plain hover.

35. **Drag-and-drop housekeeping: payload cache, pick throttle, Esc path.**
    `can_drop_data_fw` fires on every mouse-move during a drag: validate the
    payload ONCE per drag session (`gui_get_drag_data()` is invariant), and
    throttle ray-picks to a few-pixel movement delta. Cancelling a drag
    (Esc) never calls `can_drop_data` with the (INF,INF) sentinel - clear
    drag state on `NOTIFICATION_DRAG_END` instead (propagated tree-wide).
    Also: drag-forwarding callables receive only (position[, data]) - bind
    the `p_from` control at registration (`callable_mp(...).bind(this)`),
    same as the SET_DRAG_FORWARDING_* macros.

36. **Plugin teardown must pair ctor registrations.** LevelEditorPlugin
    never removed its dock or freed the screen/dock - the editor's saved
    layout (`project/.godot/editor/editor_layout.cfg`, `dock_*` keys) kept
    a zombie "Level" dock that reappeared next to the fresh one each
    session. FIX: `~LevelEditorPlugin` calls `remove_control_from_docks` +
    `memdelete` on both (same pattern as GridMap's enter/exit-tree pairing).
    Pre-existing zombies need a one-time layout.cfg cleanup.

37. **Inheritance rules must be tested against the RIGHT source face.** The
    first edge-extrude material rule ("first using face") was deterministic
    but wrong for users: a floor edge pulled sideways inherited the side
    face's material (face order), not the floor's. RULE: extruded walls
    inherit from the face geometrically CONTINUING them - `extrude_face`
    walls from the neighbor across their seam edge, `extrude_edge` walls
    from the using face whose normal best matches the wall's normal. Tests
    must assert the geometric relationship (wall normal vs source normal),
    not just face indices.

38. **Runtime classes must not live under `editor/`.** `LevelBrush`/`LevelMap`
    register at SCENE level and exist in exported games; the module SCsub only
    builds `editor/*.cpp` for editor builds. They were moved back to module
    root after an export-breaking placement.

39. **Brush persistence needs plain properties.** C++ members don't save;
    `vertices`/`faces`/`face_materials`/`faces_flipped` are real properties.
    Old scenes saved before this have empty brushes - recreate them.

40. **Bevel: edge CONSUMED; strip cross-section profiled by shape.** After
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

41. **subdivide_face must split the NEIGHBORS' shared edges too.** The quad
    grid creates midpoint verts on the subdivided face's boundary, but
    neighboring faces kept their original long edge - a T-junction
    (render cracks, and the boundary edge no longer exists in the
    neighbor's loop, so bevel's adjacency search found only ONE face and
    refused - "top edges of a subdivided face won't bevel"). FIX: after
    rewiring the subdivided face, insert each midpoint into any other
    face whose loop contains that boundary run consecutively. The n-gon
    fan path adds no boundary verts, so it needs no such fix.

42. **Clip seam dedup: near-plane kept verts must SNAP into the cap's
    weld set.** `clip()` classified verts within epsilon as "inside" and
    emitted the ORIGINAL vert, while crossing edges produced a WELDED
    intersection vert - two distinct seam verts within WELD_DIST of each
    other (one drives the kept face, the other the cap; dragging showed
    exactly that split). FIX: kept verts within WELD_DIST of the plane
    are projected onto it and routed through the same weld lambda, plus
    a consecutive-duplicate cleanup per loop.

43. Module SCsub env flag is `env.editor_build`, not `env["tools"]`.
44. `initialize_<foldername>_module` must match the folder name exactly
    (module was renamed `leveleditor` → `level_editor` mid-project).
45. Clean stale `__pycache__` in the module dir after renames.
46. `Math::pow(2.0, step)` was "ambiguous" on MSVC - hardcoded a ladder array
    instead (simpler anyway).
47. **`draw_colored_polygon` ERR_FAILs on untriangulable polygons.** Projected
    n-gon fills (face hover/selection overlays) can be degenerate in screen
    space - viewed edge-on, or concave/self-intersecting after vertex edits -
    and `RendererCanvasCull` errors once per redraw (log spam on every hover).
    Pre-flight with `Geometry2D::triangulate_polygon` and skip the fill on
    failure; keep drawing the outline.
48. **Member names collide with locals.** Naming the selection-target member
    plain `target` shadowed ~15 existing `LevelBrush *target` locals (C4458
    warnings everywhere). Module state members that describe editor concepts
    (tool, target, mode) need qualified names (`selection_target`).
49. **Flat (zero-extent) ghost AABBs break handle math in edge-on views.**
    A quad ghost projects to a LINE in the two ortho views looking
    perpendicular to its normal: point-in-polygon hit tests can never hit,
    and all 8 corner handles stack onto one line. Funnel picking,
    inside-drag, AND drawing through ONE predicate instead of parallel
    filters - now `_box_handle_usable(p_vp, handle, flat_axis)`, shared by
    the ghost AND the select-mode AABB handles. Rules: in ANY ortho view
    the two face handles on the view axis are dropped (they stack at the
    box's projected center and can never drag there - the ortho ray is
    parallel to their axis, so `_ray_to_axis_plane` can never hit); a flat
    quad additionally drops thickness handles and, edge-on, everything but
    the two endpoint handles.

50. **`get_edges()`/`get_open_edges()` rebuild a HashSet by scanning every
    face loop - never call them per frame.** The overlay called them per
    brush x 4 viewports per repaint (a 64-side sphere = ~6000 edges = ~50k
    hash inserts per viewport), and gizmo drags repaint on every
    mouse-motion: that alone tanked FPS on dense brushes even with the
    geometry-only preview bake. FIX: LevelBrush caches both sets
    (`edges_cache`/`open_edges_cache`, invalidated in
    `_notify_map_changed`) and returns them by const reference (Godot
    HashSet has no copy ctor anyway). Anything derived from face loops
    (edge sets, loop walkers) deserves the same suspicion before being
    used in a draw/pick path.

51. **`LevelMap::bake()` was O(materials x faces) with per-material
    re-transforms.** The material dedup linear-scanned per face (quadratic
    with per-face unique materials), then the surface loop re-scanned
    every brush x face per material and recomputed each brush's
    global transform + normal basis per material. Now: one grouping pass
    (`HashMap<Ref<Material>, (brush, face) pairs>`) and per-brush
    transforms computed once.

52. **PopupMenu IDs collide in two non-obvious ways.** (a) Encoding the
    viewport index in menu IDs (`id = vp*MAX + mode`) collided with the
    grid-toggle IDs at `4*MAX` when HUD toggles were added at
    `vp*MAX + MAX` (vp=3 info = 20 = grid 2D). (b) Worse:
    `add_submenu_node_item(label, sub)` with no explicit ID assigns
    `items.size()` as the ID - the 4 viewport submenu items silently got
    IDs 0..3, so `get_item_index(0)` found "Perspective", not "Show 2D
    Grid", and the grid checkmarks never updated (the state toggled fine
    because `id_pressed` still fired with the grid item's own ID). FIX:
    per-submenu ID spaces with the viewport index bound to each submenu's
    handler (`callable_mp(...).bind(vp)`), and grid toggles on named
    constants (VIEW_MENU_GRID_2D_ID/3D_ID = 100/101) well past any
    auto-assigned range. Never use small literal IDs on a popup that also
    contains submenu items.

53. **Hover picking must be throttled AND change-gated.** `_update_hover`
    ran on every mouse-motion event: a full face/edge/vertex ray-pick over
    every brush triangle PLUS `_update_overlays()` (repainting all 4
    viewports' brush edges). Bare mouse movement over empty space dropped
    FPS 240->190. FIX: (a) skip the pick until the cursor moved >= ~4px
    (same DROP_REPROBE_DIST_SQ throttle as the material-drop probe), (b)
    repaint only when the pick RESULT changed (brush/face/edge/vertex),
    (c) reset the throttle in `_clear_selection` so stale cleared state
    isn't compared against. Any per-motion handler should be assumed hot:
    profile before letting it ray-cast or repaint unconditionally. Tool input
    is additionally skipped entirely while the camera navigates
    (`vp->is_navigating()`: RMB freelook or ortho MMB pan) - hover results
    are stale the instant the camera moves, and the throttle is reset so the
    first hover afterwards re-picks.

54. **Per-viewport resources need per-viewport dirty tracking.** The 3D brush
    outlines keep one `ImmediateMesh` per brush PER VIEWPORT (in the
    viewport's `outline_instances`), but the geometry-version bookkeeping
    (`outline_versions`) was a single screen-level map shared by all four
    panes. In `_update_outlines()` the per-viewport loop let viewport 0
    rebuild its mesh on a version bump and write the new version into the
    shared map - viewports 1/2/3 then read `built == version` and SKIPPED
    rebuilding their own meshes, leaving a stale outline box in 3 of 4 panes
    after any local-space vertex edit (rotate/scale/select-handle resize).
    Move looked fine only because it changes the node transform (applied
    per-frame via `mi->set_transform()` for all 4 panes) and never rebuilds
    the mesh. FIX: `outline_versions` is now `outline_versions[4]` (indexed
    by viewport) so each pane independently detects the change. RULE: when a
    resource is duplicated per viewport (meshes, caches), its dirty/version
    tracking must be per viewport too - a shared stamp always starves every
    consumer after the first.

55. **Scene change must ABANDON drags, not end them.** `on_scene_changed`
    used to only cancel ghost/clip/mirror and clear the selection - an
    in-flight gizmo/rotate/select-handle drag kept running against the old
    scene's brushes, and its undo never committed. `_abandon_drags()` now
    resets every drag member WITHOUT ending the drags the `_set_tool` way:
    ending means dereferencing brushes (snapshot restores, undo commits)
    that may already be freed, and the scene's undo history is gone anyway.
    RULE: scene teardown resets state; tool/target switches end drags
    cleanly (brushes survive there). Same split applies to any NEW drag
    state - add it to both `_abandon_drags()` and `_set_tool`/`_set_target`.
