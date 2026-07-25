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
   1/64..64 grid ladder (values rounded to 0). Match step to the smallest
   ladder value.

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

14. **Face-index selections go stale after topology ops.** Any action that
    adds/removes faces (subdivide, delete, clip) invalidates
    `selected_faces`/`selected_edges` indices. Either clear the selection
    after the op (subdivide/delete do this) or remap it (the gizmo extrude
    updates selection to the new cap faces). EXCEPTION: `extrude_face`
    replaces each source face with its cap in place, so a face selection
    stays valid across extrudes (chained extrude relies on this).

15. **`_set_mode` must interrupt active drags.** Mode shortcuts can fire
    mid-drag; the drag's undo action commits against the old mode's
    snapshots, so `_set_mode` ends gizmo/rotate/select-handle drags
    (committing their undos) before switching. Any NEW drag state must be
    added there too, or its edit becomes un-undoable.

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

22. **Grid lines vanish when flying low** in the perspective view: projecting
    a segment with an endpoint behind the camera fails. FIX: clip segments
    against the camera near plane in camera space before projecting.

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

## Serialization

30. **Runtime classes must not live under `editor/`.** `LevelBrush`/`LevelMap`
    register at SCENE level and exist in exported games; the module SCsub only
    builds `editor/*.cpp` for editor builds. They were moved back to module
    root after an export-breaking placement.

31. **Brush persistence needs plain properties.** C++ members don't save;
    `vertices`/`faces`/`face_materials`/`faces_flipped` are real properties.
    Old scenes saved before this have empty brushes - recreate them.

32. **Bevel: edge CONSUMED, offset lines mitred at shared corners.** After
    three wrong models (bisector-scaled chamfer, perpendicular chamfer,
    retained-centerline strip), the working model is Blender's: each
    beveled edge is consumed; every adjacent face gets an offset line
    parallel to the edge at distance d (measured ALONG the boundary edges
    at the corners - the corner point sits on the unbeveled boundary ray
    at distance d); one strip quad bridges the two offset lines. Where
    several beveled edges meet at a vertex, the corner point is the
    intersection of their offset lines (on the angle bisector at
    d/sin(angle-to-edge)) - ONE shared vert per (vertex, face), so strips
    join cleanly. Collinear chains share via the same mechanism. Two
    failure modes this avoids: (a) corner offsets computed from MUTATED
    loops poison later edges in the same action - all positions must come
    from original topology (two-pass: gather, then apply); (b) keeping
    the original edge as a raised ridge overlaps coplanar neighbors and
    crosses at shared verts (the dark X-fins screenshot).

## SCons/module mechanics

33. Module SCsub env flag is `env.editor_build`, not `env["tools"]`.
34. `initialize_<foldername>_module` must match the folder name exactly
    (module was renamed `leveleditor` → `level_editor` mid-project).
35. Clean stale `__pycache__` in the module dir after renames.
36. `Math::pow(2.0, step)` was "ambiguous" on MSVC - hardcoded a ladder array
    instead (simpler anyway).
