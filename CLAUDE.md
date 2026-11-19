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
- `bundle/`: the bundle's `PackageContents.xml`. `tools/`: `tilegen`, `bench`, `package.py` and the checks.

## Build and check

- Build: `powershell -ExecutionPolicy Bypass -File build.ps1` (MSVC, CMake, Ninja; warnings are errors):
  the engine, the tools and the tests. `-Config Debug`, `-BuildDir <dir>`, `-NoTests`, `-Bench`, `-Clean`.
- `-Plugin` also builds `OpenScatter.dlo` and puts `<build>\bundle\OpenScatter.bundle` together. It
  needs the 3ds Max 2027 SDK and refuses `-Config Debug`. Nothing is installed.
- The build goes to `build\<config>`, or under `$BuildRoot` when the ignored local file `build.local.ps1`
  sets it. The SDK comes from `-MaxSdk`, else `$MaxSdk` in that file, else `ADSK_3DSMAX_SDK_2027`.
- Checks: `tools\check.ps1` runs the quick ones, `-Full` all; all must pass. It needs Python 3.11 or newer
  and the ruff `ruff.toml` names. Its own steps (the version agrees, every source outside `third_party/`
  has its licence line, no file holds a path of somebody's computer), then those of `tools/repo.toml`:
  - `clang-format --dry-run -Werror` on the tracked `.cpp` and `.h` files. There is no C++ lint.
  - `ruff format --check .` and `ruff check .`, for the Python in `tools/`
  - `build.ps1`, which runs `osc_core_tests` (76 tests; none needs 3ds Max)
  - only with `-Full`: `build.ps1 -Plugin`, then `tests\plugin\run_plugin_tests.ps1` (5 tests in
    `3dsmaxbatch`; they need 3ds Max 2027, Arnold and Corona). `-Only <tests>` runs some.
- CI: `.github/workflows/check.yml` runs `tools\check.ps1`, without `-Full`, on every push and pull request
  to `main`, on GitHub's Windows runner with Python 3.14: the engine only, with the runner's clang-format
  and Visual Studio.
- `.editorconfig`, `.gitattributes`, `ruff.toml`, `LICENSE`, `tools/check.ps1` and `tools/repo.py` are
  the same in every Open3D repo. Do not change them here.

## Version and release

- The version is `project(OpenScatter VERSION X.Y.Z)` in `CMakeLists.txt`, which stamps the `.dlo`;
  `tools/repo.toml` lists its copies, and `python -I tools/repo.py version set X.Y.Z` writes them all.
  `build.ps1 -Plugin` also refuses a mismatch with `PackageContents.xml`. The CHANGELOG is by hand:
  what a user can notice goes under `## In development`, in the same commit.
- `python -I tools/repo.py package` makes the release package as the `[package]` table of
  `tools/repo.toml` says: it needs the version's `## X.Y.Z` section in `CHANGELOG.md`, runs the quick
  checks, then `tools/package.py` (`build.ps1 -Plugin`, then `dist/OpenScatter.bundle` with the `.dlo`,
  `LICENSE` and `NOTICE` in it), and writes `dist/openscatter-<version>-windows-x64-3dsmax.zip`, with
  that folder at its root, and `dist/SHA256SUMS.txt`. It refuses a file that names this computer and a
  bundle whose `AppVersion` is not the version. `tools/package.py --zip` still writes a zip of its own.
- `python -I tools/repo.py release X.Y.Z` turns `## In development` into `## X.Y.Z — <date>`, sets the
  version, commits `Release X.Y.Z`, packages, tags `vX.Y.Z` and prints the pushes to approve; run again
  once they are done, it packages again and makes the GitHub release as a draft. It never pushes.
  `--dry-run` tries it all and writes only to `dist/`; it builds in a copy of the repo inside `dist/`,
  which has no `build.local.ps1`: the SDK then comes only from `ADSK_3DSMAX_SDK_2027`.
- The Open3D launcher needs `OpenScatter.bundle` at the zip's root, with the release's version in its XML.
  Its publishing script still makes this repo's zip its own way, from the latest `vX.Y.Z` tag.

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
