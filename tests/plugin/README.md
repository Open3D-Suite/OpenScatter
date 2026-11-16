# Plugin tests

Tests of the 3ds Max plugin. They need 3ds Max 2027 and the bundle `build.ps1 -Plugin` makes; the
render tests also need Arnold (it comes with 3ds Max) and Corona, and skip what is not installed.

**Every test resets the scene of the 3ds Max it runs in.** The runners start their own processes
and load the plugin from a private folder (`ADSK_APPLICATION_PLUGINS`), so nothing is installed and
a 3ds Max that is already open is never touched – but never run one of the `.ms` files by hand in a
3ds Max with work in it.

```powershell
powershell -ExecutionPolicy Bypass -File tests\plugin\run_plugin_tests.ps1                     # all, in 3dsmaxbatch
powershell -ExecutionPolicy Bypass -File tests\plugin\run_plugin_tests.ps1 -Only render_corona
powershell -ExecutionPolicy Bypass -File tests\plugin\run_viewport_test.ps1 -Driver x11         # a second 3ds Max, GPU driver
powershell -ExecutionPolicy Bypass -File tests\plugin\run_viewport_test.ps1 -Driver xs          # the same with the software driver
```

Each test writes `results\<test>.txt`, one line per check, ending in `RESULT: PASS` or
`RESULT: FAIL`; renders and viewport captures go to `results\images\`. The folder is not kept in
git: it describes one run on one machine.

| Test | Script | What it checks |
|---|---|---|
| `load` | `test_load.ms` | The plugin loads from the bundle. An empty object is harmless. Instances appear with a surface and models; only an input that matters recomputes them (a display setting or moving a model does not; a seed, the density or moving the surface does). Raising the density moves nothing. The instance limit. A snapshot is the aggregate mesh. The combined material, and a copy of the object getting one of its own. The Particle Flow carrier is created when Corona becomes the renderer, exists once, is built again when it is deleted by hand, stays when the renderer changes and goes away with its scatter object. The scanline renderer gets the aggregate mesh. |
| `render_arnold`, `render_corona` | `test_render.ms` | A control box (the renderer renders at all); route "none" covers no pixel; the aggregate mesh as the reference; the renderer's own route – set explicitly, then chosen automatically, at frame 0 and at frame 5 – against the reference, pixel by pixel in colour; the object's report says exactly one adapter handed instances out; nothing is left over when the route goes back to "none"; a hidden object renders nothing. Then 50 000, 500 000 and a million instances, with time and memory. The models are a teapot, a box with a moved pivot and a cylinder with a Multi/Sub-Object material. |
| `save_reopen` | `test_save_reopen.ms` | Four processes: save a scene Corona has rendered; reopen it (same parameters, same instances, same picture, the carrier once); open it **without** the plugin (the object is kept as a stand-in and nothing of it renders) and save it; open that one with the plugin back. |
| `determinism` | `test_determinism.ms` | Two processes build the same scene: the hashes of their instance buffers are equal. Inside each, the hash is the same with 1, 3 and all threads, and for a scatter of over a hundred thousand instances. |
| viewport | `test_viewport.ms` | In a 3ds Max with its interface: boxes and meshes are drawn in shaded and in wireframe views; the budget follows the display driver; a limit thins the display instead of emptying it; meshes over the triangle budget become boxes; the icon. |

`common.ms` holds what they share: the result file, the test scene and the render helpers.
The tests of the engine, which need no 3ds Max, are in [`tests/core`](../core).
