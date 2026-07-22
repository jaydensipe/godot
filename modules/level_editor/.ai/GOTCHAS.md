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

## Undo

10. **`commit_action(false)` skips do-methods.** Used for live-applied changes
    (gizmo drags). But if any do-method must actually RUN now (e.g.
    `add_child` for a split brush), use `commit_action(true)`. A split once
    silently dropped the second half because of this.

11. **Per-plane/per-face undo entries are fragile.** Record whole serialized
    properties (`vertices`, `faces`, `face_materials`) in one pair - safe
    across face-count changes and keeps the inspector in sync.

## Rendering

12. **Black tops on fresh brushes were NOT normals.** Winding was provably
    correct (Newell check printed correct normals). Culprits were (a) scene's
    own WorldEnvironment/light, (b) the level viewports' single directional
    light pointing the wrong way - set light direction with
    `look_at_from_position`, don't hand-tune Euler rotations. Also preview
    instance gets `SHADOW_CASTING_SETTING_OFF`.

13. **Overlays bleed into other viewports** unless the SubViewportContainer
    has `set_clip_contents(true)`.

14. **SubViewports share the edited scene's World3D** (own_world_3d=false) -
    brush previews render because of this. Lights/cameras added under them
    live in the same world. (User explicitly chose separate lighting over
    sharing the 3D editor's world via `set_world_3d`.)

15. **Preview refresh:** `LevelMap::refresh()` calls `_update_preview()`
    immediately (not deferred) so gizmo drags stay in sync.

## Serialization

16. **Runtime classes must not live under `editor/`.** `LevelBrush`/`LevelMap`
    register at SCENE level and exist in exported games; the module SCsub only
    builds `editor/*.cpp` for editor builds. They were moved back to module
    root after an export-breaking placement.

17. **Brush persistence needs plain properties.** C++ members don't save;
    `vertices`/`faces`/`face_materials`/`faces_flipped` are real properties.
    Old scenes saved before this have empty brushes - recreate them.

## SCons/module mechanics

18. Module SCsub env flag is `env.editor_build`, not `env["tools"]`.
19. `initialize_<foldername>_module` must match the folder name exactly
    (module was renamed `leveleditor` → `level_editor` mid-project).
20. Clean stale `__pycache__` in the module dir after renames.
21. `Math::pow(2.0, step)` was "ambiguous" on MSVC - hardcoded a ladder array
    instead (simpler anyway).
