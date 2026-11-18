# OpenScatter

A scatter for 3ds Max 2027: one procedural object that places instances of models on surfaces and hands
them to the renderer without creating nodes. A plain C++ engine and a C++ plugin around it, one `.dlo`.

This file holds facts about this repo only, each one checkable in the code. Update it in the same commit
as a change that makes it old.

## Layout

- `core/`: the engine, namespace `osc`: plain C++20, no dependencies, no 3ds Max header.
- `plugin/src/`: the plugin, namespace `oscmax`, with the 3ds Max SDK and nothing else. `plugin.h`: class
  ids, parameter ids, the instance buffer. `scatter_object.cpp`: the object. `render_instancing.cpp`,
  `pflow_operator.cpp` with `carrier.cpp`, `render_mesh.cpp`: the three render routes. `viewport.cpp`.
- `tests/core/`: the engine's tests (doctest). `tests/plugin/`: the plugin's. `docs/ARCHITECTURE.md`: design.
- `bundle/`: the bundle's `PackageContents.xml`. `tools/`: `tilegen`, `bench` and `package.py`, the packager.

## Build and check

- Build: `powershell -ExecutionPolicy Bypass -File build.ps1` (MSVC, CMake, Ninja; warnings are errors):
  the engine, the tools and the tests. `-Config Debug`, `-BuildDir <dir>`, `-NoTests`, `-Bench`, `-Clean`.
- `-Plugin` also builds `OpenScatter.dlo` and puts `<build>\bundle\OpenScatter.bundle` together. It
  needs the 3ds Max 2027 SDK and refuses `-Config Debug`. Nothing is installed.
- The build goes to `build\<config>`, or under `$BuildRoot` when the ignored local file `build.local.ps1`
  sets it. The SDK comes from `-MaxSdk`, else `$MaxSdk` in that file, else `ADSK_3DSMAX_SDK_2027`.
- Checks, all of which must pass:
  - `clang-format --dry-run -Werror` on the tracked `.cpp` and `.h` files. There is no C++ lint.
  - `build.ps1`, which runs `osc_core_tests` (76 tests; none needs 3ds Max)
  - `powershell -ExecutionPolicy Bypass -File tests\plugin\run_plugin_tests.ps1` after a `-Plugin` build
    (5 tests in `3dsmaxbatch`; they need 3ds Max 2027, Arnold and Corona). `-Only <tests>` runs some.
  - `ruff format --check .` and `ruff check .`, for `tools/package.py`
- `.editorconfig`, `.gitattributes` and `ruff.toml` are common to every Open3D repo. Do not change them here.

## Version and release

- The version is in `CMakeLists.txt`, which stamps it into the `.dlo`, and twice in `PackageContents.xml`;
  `build.ps1 -Plugin` refuses a mismatch. Nothing here checks the other copies: the README, the CHANGELOG,
  `OPENSCATTER_VERSION_STRING` in `plugin/src/plugin.h` and the numbers in `core/include/osc/version.h`.
- `python tools/package.py` runs `build.ps1 -Plugin` and writes `dist/OpenScatter.bundle` with `LICENSE`
  and `NOTICE` in it; `--zip` adds `dist/openscatter-<version>-windows-x64-3dsmax.zip`.
- The Open3D launcher needs `OpenScatter.bundle` at the zip's root, with the release's version in its XML.

## Rules of this code

- Determinism is a contract: the same settings give the same instances on any machine and thread count.
  The tests hold stored hashes; moving one needs a new `kAlgorithmVersion`. Sines and cosines come from
  `osc/detmath.h`, never the C library; random values are hashes (`osc/hash.h`); no fast-math, no FMA.
- Parameter ids (`ScatterParam`) and class ids are stored in scenes: add at the end, never renumber.
- Exactly one render route emits. Automatic goes by the renderer's name (`routeForRenderer`): Corona
  through Particle Flow; Arnold, V-Ray, Redshift and Octane through instancing; anything else as a mesh.
- `.clang-format`: Microsoft style, 120 columns, include order left alone, `Type* name` and `{ a, b }`
  kept. `.clang-format-ignore` skips doctest; `// clang-format off` skips three hand-laid SDK tables.

## Traps

- No interface yet: the Modify panel shows no rollouts; settings are made from MAXScript or Python.
- The pattern is laid out in world space: where a surface sits changes how many instances it gets.
- Viewport limits: 2 million instances with a graphics driver, 20,000 with the software one.
- Every plugin test resets its scene. The five run in `3dsmaxbatch` processes of their own, with a private
  copy of the bundle under `-Work`; `run_viewport_test.ps1 -Driver x11` (or `xs`) starts a second 3ds Max.
- Two builds of the `.dlo` are never identical byte for byte: a zip built again is not the zip tested.
- `OPENSCATTER_LOG=<file>` switches on a diagnostic log.
