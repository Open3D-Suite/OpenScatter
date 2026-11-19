# OpenScatter

A scatter for 3ds Max: one procedural object that places millions of instances on surfaces – grass,
trees, stones, anything – and hands them to the renderer as real instances, without creating
nodes. Part of the [Open3D](https://github.com/carlesllavata/Open3D) suite of open-source tools
for 3D work.

**Version 0.1.0 is the first release, and an early one.** OpenScatter is being built from its
engine outwards. The engine and a first version of the 3ds Max plugin are there and tested: it
scatters, renders in Arnold and Corona and draws in the viewport. But the plugin has **no
interface yet** – there are no rollouts, and every setting is made from MAXScript or Python – and
it has **not been tested in V-Ray**.

## Install

OpenScatter needs **3ds Max 2027** on Windows.

The easy way: install the [Open3D Launcher](https://github.com/carlesllavata/Open3D/releases/latest)
and click **Install** next to OpenScatter, with 3ds Max closed. It puts OpenScatter in
`%APPDATA%\Autodesk\ApplicationPlugins` as a plug-in bundle, tells you when a new version is out
and updates it. To uninstall, choose **Uninstall…** in OpenScatter's menu in the launcher.

By hand: each [release](https://github.com/carlesllavata/OpenScatter/releases/latest) has the
bundle as a zip, `openscatter-<version>-windows-x64-3dsmax.zip`. With 3ds Max closed, unzip it,
copy the `OpenScatter.bundle` folder into `%APPDATA%\Autodesk\ApplicationPlugins\` and start
3ds Max. To remove it, delete that folder with 3ds Max closed.

Once it is installed, **OpenScatter** is in the Create panel, under Geometry, in the category
Open3D, and it is the `OpenScatter` class in MAXScript.

Every computer that renders a scene needs OpenScatter too. A 3ds Max without it still opens the
scene and keeps the object, with its settings, for the day the plugin is there again – but it
renders nothing of it.

## Where it is going

- A scatter object you edit in the Modify panel: density, density maps, include and exclude areas
  drawn with splines, slope and altitude limits, random rotation and scale, several models with
  probabilities.
- Real render-time instances in **Corona, V-Ray and Arnold**.
- **Nothing jumps when you edit.** Raising the density only adds instances; editing an area or a
  map only adds or removes them near the edit. An instance that stays keeps its place, its model,
  its rotation and its size.
- **The same result on every machine**, whatever its CPU or number of threads, so a scene rendered
  on a farm or in tiles shows no seams and no flicker.
- No account, no licence key, no per-node fee.

The aim is the feature set artists know from the scatters they use today, written from scratch.

## What exists today: the plugin

`plugin/` builds `OpenScatter.dlo` for **3ds Max 2027**, and the build packs it into
`OpenScatter.bundle`, the package 3ds Max loads. It adds one object, **OpenScatter** (Create
panel, category Open3D), that scatters the models you give it over the surfaces you give it.

How the instances reach each renderer:

| Renderer | How | State |
|---|---|---|
| Arnold | 3ds Max's render-time instancing interface: each model is rendered from its own object, with its own material. | Tested: identical, pixel for pixel, to the same instances as plain geometry. A million instances of a 1 000-polygon model render in about a second. |
| Corona | Corona does not read that interface. OpenScatter hands it the instances as Particle Flow particles, which Corona instances natively; it creates and keeps the Particle Flow source itself. | Tested with Corona 16: identical, pixel for pixel, to the same instances as plain geometry, Multi/Sub-Object materials included. A million instances render in about ten seconds, roughly twice the time of Corona's own scatter. |
| V-Ray | The same interface as Arnold. | **Not tested.** V-Ray's own code reads the interface, but OpenScatter has not been rendered in V-Ray. |
| Scanline, ART, others | One mesh with a copy of the model at every instance. | Tested with the scanline renderer. Capped (two million faces by default); above the cap the instances are thinned evenly. |

One setting, `renderRoute`, decides which of these hands the instances out, so nothing is rendered
twice; left on automatic it follows the renderer in use.

In the viewport the instances are drawn by the graphics card, as boxes or as the models
themselves, in shaded and in wireframe views. The amount drawn follows the display driver – two
million instances with a graphics card, twenty thousand with the software driver – and beyond it
the instances are thinned evenly, never cut off at an edge.

### Using it

There are no rollouts yet. From MAXScript:

```maxscript
sc = OpenScatter()
sc.surfaces = #($Terrain)
sc.models = #($Grass_A, $Grass_B, $Stone)
sc.modelProbabilities = #(5.0, 5.0, 1.0)
sc.density = 40.0                 -- instances per square metre
sc.scaleMin = [0.8, 0.8, 0.8]
sc.scaleMax = [1.3, 1.3, 1.3]
sc.alignToNormal = 0.3            -- 0 = upright, 1 = along the surface
sc.getInstanceCount()
```

| Setting | What it is |
|---|---|
| `surfaces` | The objects to scatter on. The instances are dropped onto them along the world Z axis. |
| `models`, `modelProbabilities` | The objects to scatter and how often each is chosen (a missing probability counts as 1). An instance places the model's pivot. |
| `density` | Instances per square metre, whatever the scene's units. |
| `patternSize` | Side of one tile of the distribution pattern, in scene units. 0 (the default) picks it from the density: the largest power of two that still holds it. Inside one such step, changing the density adds or removes instances and moves none. |
| `seed` | Another seed is another scatter. |
| `maxInstances` | Never more instances than this (default two million; 0 = no limit). Above it the scatter is thinned evenly. |
| `alignToNormal` | From 0, standing upright, to 1, standing along the surface. |
| `rotationMin`, `rotationMax` | Random rotation in degrees about the instance's own X, Y and Z. Default: any angle about Z. |
| `scaleMin`, `scaleMax`, `uniformScale` | Random scale per axis; with `uniformScale` (the default) one factor, from the X range, for the three. |
| `limitSlope`, `slopeMin`, `slopeMax` | Only where the surface's slope, in degrees from the horizontal, is in the range. |
| `limitAltitude`, `altitudeMin`, `altitudeMax` | Only where the world Z is in the range. |
| `renderRoute` | 0 automatic, 1 instancing interface, 2 Particle Flow, 3 mesh, 4 nothing. |
| `meshMaxFaces` | The cap of the mesh route (0 = none). |
| `viewportMode` | 0 nothing, 1 boxes, 2 meshes (boxes when the meshes exceed the budget). |
| `viewportMaxInstances`, `viewportMaxTriangles` | The viewport budget; 0 = chosen from the display driver. |
| `iconSize` | Size of the icon drawn at the object. |
| `combinedMaterial` | Kept by OpenScatter: a Multi/Sub-Object material that gathers the models' materials. The Particle Flow route and the mesh route render with it. |

Functions: `update()`, `getInstanceCount()`, `getModelInstanceCount <model>`,
`getInstanceTransform <index>`, `getInstanceModel <index>`, `getStatus()`, `getStatistics()`,
`getInstanceHash()`, `getActiveRoute()`, `getRenderReport()`, `getViewportReport()`,
`getCarrier()`, `syncCarrier()` and `getVersion()`.

### The Particle Flow source

For Corona, OpenScatter adds a Particle Flow source to the scene, named
`OpenScatter carrier - <object>`, the first time Corona is the renderer (or `renderRoute` is set to
Particle Flow). It is not meant to be edited:

- It shows nothing in the viewport and is frozen so that it cannot be picked there. It must stay
  renderable and unhidden, or Corona would skip it. It does appear in Particle View.
- It holds particles only while a render uses it; with any other renderer or route it is empty.
- It is saved with the scene. Deleting the OpenScatter object removes it; deleting it by hand does
  no harm either – it is created again when it is needed.

### Limits of this first version

- No interface: no rollouts, no menu entry, no creation macro.
- The plugin does not expose the engine's areas and density maps yet, nor does it scatter along
  splines or in volumes.
- The pattern is laid out in world space: moving a surface drops the same pattern onto it again
  instead of carrying the instances along.
- A model is one object. Groups and hierarchies are not gathered into one model, and proxies
  (`CoronaProxy`, `VRayProxy`) are untested.
- Through Particle Flow, Corona renders at most ten million instances per object (the limit of a
  Particle Flow source), and the render takes one Particle Flow step per frame between the start of
  the animation and the frame rendered: about 25 ms per step for half a million instances.
- No per-instance colour, no motion blur for the instances, no camera clipping.
- Interactive rendering has not been tested in any renderer.
- Only 3ds Max 2027.

## What exists today: the engine

`core/` is the scatter engine as a C++ library with no dependencies and no 3ds Max in it. It takes
meshes, polylines, images and settings and returns instances:

- **Blue-noise distribution in tiles.** A pattern of points that is well spread at every density is
  repeated over the ground. Every instance has a stable id and a rank; the density is a threshold
  on the rank.
- **Projection** onto a surface mesh along its vertical, with the triangle's normal.
- **Limits** by slope and by altitude.
- **Density map** baked to an image, sampled in the surface's texture coordinates or over a
  rectangle of the ground.
- **Areas**: include and exclude shapes (closed shapes, with holes, and open shapes as bands of a
  given width), each with a near–far falloff.
- **Models** chosen by probability.
- **Transforms**: alignment between the vertical and the surface normal, and random rotation, scale
  and translation per axis.
- **An instance limit** that thins evenly instead of cutting a corner off.
- Output in blocks, one per tile, in a fixed order: identical for any number of threads.

On a 32-thread desktop CPU it scatters one million instances onto a terrain of one million
triangles in about 0.012 s, and ten million in about 0.07 s (`osc_bench` measures it on your
machine).

Not there yet: scattering by surface area (for meshes that are not terrains), along splines and in
volumes, collisions, camera clipping and falloff curves.

## Building

Windows, with the Visual Studio 2022 Build Tools (the C++ workload, which brings CMake and Ninja):

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

That builds the library, the tools and the tests into `build\release` and runs the tests.
`-Config Debug` builds a debug version, `-Bench` also runs the benchmark, `-BuildDir <folder>`
builds somewhere else. To always build somewhere else (a local disk when the repository is on a
network share, for example), create `build.local.ps1` next to `build.ps1` with one line:

```powershell
$BuildRoot = "D:\builds\openscatter"
```

Elsewhere, any C++20 compiler should do through CMake (`cmake --preset release`, then
`cmake --build --preset release` and `ctest --preset release`); only MSVC is tested so far.

The programs end up in the `bin` folder of the build:

| Program | What it does |
|---|---|
| `osc_core_tests` | The tests of the engine. A couple of seconds. |
| `osc_bench` | Times the engine on one and ten million instances and checks the results are identical with one thread and with all of them. `--quick` leaves out the ten-million cases. |
| `osc_tilegen` | Generates a blue-noise pattern and writes it to a pattern file (`.oscp`), optionally with a picture: `osc_tilegen --out blue_noise_16384.oscp --svg preview.svg`. The same arguments give the same file on any machine. |

### The plugin

The plugin also needs the **3ds Max 2027 SDK** (a free download from Autodesk; installing 3ds Max
is not needed to build):

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -Plugin -MaxSdk "C:\Program Files\Autodesk\3ds Max 2027 SDK\maxsdk"
```

`-MaxSdk` is the SDK's `maxsdk` folder, the one that holds `include` and `lib`. Without it the
script looks for `$MaxSdk` in `build.local.ps1` and then for the `ADSK_3DSMAX_SDK_2027` environment
variable the SDK's installer sets. The result is `build\release\bundle\OpenScatter.bundle`.
Nothing is installed: to use the bundle, copy that folder into
`%APPDATA%\Autodesk\ApplicationPlugins` and start 3ds Max; to remove it, delete the folder.

`python tools/package.py` does that build and writes the package a release carries to `dist\`:
`OpenScatter.bundle` with the licence texts in it and, with `--zip`, the zip of that folder.

## Tests

One command runs the checks: the version and the licence lines, the formatting of the C++ and the
Python, ruff's lints, the build and the engine's tests. With `-Full` it also builds the plugin and
runs its tests in `3dsmaxbatch`. It needs Python 3.11 or newer and ruff 0.17.5:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check.ps1
powershell -ExecutionPolicy Bypass -File tools\check.ps1 -Full
```

The tests, one by one:

- The engine: `osc_core_tests`, run by `build.ps1`. No 3ds Max needed.
- The plugin: [`tests/plugin`](tests/plugin). They need 3ds Max 2027, and Arnold and Corona for
  the render tests. They run in their own `3dsmaxbatch` processes with the bundle loaded from a
  private folder, so nothing is installed and an open 3ds Max is left alone:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tests\plugin\run_plugin_tests.ps1
  powershell -ExecutionPolicy Bypass -File tests\plugin\run_viewport_test.ps1 -Driver x11
  ```

## Layout

| Folder | Contents |
|---|---|
| `core/` | The engine: `include/osc/` is its interface, `src/` its implementation. |
| `plugin/` | The 3ds Max plugin: the object, its outputs to the renderers and the viewport. |
| `bundle/` | The description of the package 3ds Max loads; the build adds the plugin to it. |
| `tools/` | `tilegen` (pattern generator), `bench` (benchmark) and `package.py` (the release package). |
| `tests/` | `core`: the tests of the engine ([doctest](https://github.com/doctest/doctest)). `plugin`: the tests of the plugin, in 3ds Max. |
| `docs/` | [`ARCHITECTURE.md`](docs/ARCHITECTURE.md): how it is designed and why. |
| `third_party/` | doctest, used by the engine's tests only. |

## Credits

OpenScatter is written from scratch. The scatters it learns from are proprietary products: only
their behaviour, as their public documentation describes it, is a reference; it contains nothing
of theirs and is not affiliated with their makers. Algorithms and third-party code are credited in
[`NOTICE`](NOTICE).

It is not related to the Blender add-on of the same name.

## License

OpenScatter is free software under the GNU General Public License, version 3 or later
(`GPL-3.0-or-later`). See [`LICENSE`](LICENSE).
