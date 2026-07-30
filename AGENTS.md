# AGENTS.md

Godot 4.8.dev fork. The only custom code is `modules/level_editor` (a
Hammer-style 3D level editor as a "Level" main-screen tab). Everything else
is vanilla upstream Godot — follow upstream Godot conventions for engine code.

## Golden rule: the user builds and runs everything

Never compile, run the editor, or execute tests yourself (this is also stated
in `modules/level_editor/.ai/PROJECT.md` and the user enforces it). Make the
changes, tell the user what to build/run, and let them report results. When a
fix/feature is headlessly testable, add a test in the same change (see
`.ai/TESTS.md`).

Reference commands (what the user runs; quote these when handing off):

```
scons platform=windows target=editor dev_build=yes accesskit=no d3d12=no angle=no
scons platform=windows target=editor dev_build=yes accesskit=no d3d12=no angle=no tests=yes
bin\godot.windows.editor.dev.x86_64.exe --test --test-case="*[LevelBrush]*"   # doctest filter
```

- `accesskit=no d3d12=no angle=no` skip optional deps that error/warn otherwise.
- `dev_build=yes` artifacts carry a `.dev` infix: objects are
  `bin/obj/<path>/<name>.windows.editor.dev.x86_64.obj`, the binary is
  `bin\godot.windows.editor.dev.x86_64.exe` (non-dev builds omit `.dev`).
- `scons compiledb=yes` regenerates `compile_commands.json` (a full build pass).

## Level editor module (`modules/level_editor`)

### Docs — read before editing

The `.ai/` folder is the project's agent context system:

- `PROJECT.md` — overview, build command, user preferences/workflow rules.
- `ARCHITECTURE.md` — file layout + data flow + the geometry-op checklist.
  **Keep its file-layout section in sync when you add/move/rename files.**
- `GOTCHAS.md` — hard-won bug lessons (50+ entries). Read before touching the
  module; append new ones at the end of the flat numbered list.
- `ROADMAP.md` — open items and deferred smells.
- `TESTS.md` — test coverage + how to run.
- `AUDITS.md` — READ-ONLY archive; do not read for context handoff. Append a
  dated section only after a completed audit pass.

### Structure rules

- `level_brush.*`, `level_map.*`, `level_constants.h` stay at module root:
  they register at SCENE level and ship in exported games. `level_constants.h`
  must stay editor-free. Only editor-only code goes under `editor/`.
- `editor/` uses a split-file pattern: `LevelEditorScreen` member functions
  live split across `editor/*.cpp` and subfolders (`tools/`, `gizmos/`,
  `dock/`, `materials/`, `modifiers/`). New top-level subfolders need a glob
  line in the module `SCsub`; existing globs cover `editor/*.cpp` and
  `editor/<dir>/*.cpp` for the dirs listed there.
- Tests are doctest headers in `tests/*.h`, auto-globbed into the engine test
  harness with `tests=yes` — no SCsub change needed. Geometry ops should get
  headless tests; `test_level_map.h` cases need the `[SceneTree]` tag.
