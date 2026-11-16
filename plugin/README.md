# plugin

The 3ds Max plugin: `OpenScatter.dlo`, built for 3ds Max 2027 with the 3ds Max SDK and nothing
else (no renderer SDKs, no Qt yet). It is a translator around the engine in [`core/`](../core): it
takes meshes and settings out of the scene, calls the engine and hands the one instance buffer it
gets back to the renderers and to the viewport. [`docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)
§4 explains the design.

Build it with `build.ps1 -Plugin` from the root of the repository (see the main README).

| File | What it holds |
|---|---|
| `src/plugin.h` | What the files share: class ids, the parameter ids, the instance buffer. |
| `src/scatter_object.h/.cpp` | The `OpenScatter` object: parameters, references to surfaces and models, the instance buffer and when it is recomputed, the functions published to MAXScript. |
| `src/render_instancing.cpp` | Output through 3ds Max's render-time instancing interface (Arnold, V-Ray). |
| `src/pflow_operator.cpp` | The Particle Flow operator that carries the instances into Corona. |
| `src/carrier.cpp` | The Particle Flow source around that operator – created, found and removed by the plugin – and the material that gathers the models' materials. |
| `src/render_mesh.cpp` | The aggregate mesh: for renderers with no instancing route, and for collapsing the object. |
| `src/viewport.cpp` | GPU instancing in the viewport, with a budget that depends on the display driver; the icon. |
| `src/dllmain.cpp` | The entry points 3ds Max looks for, and an optional diagnostic log. |

There are no rollouts yet: the object is driven from MAXScript or Python. The tests are in
[`tests/plugin`](../tests/plugin).

Setting the environment variable `OPENSCATTER_LOG` to a file name makes the plugin append what
Particle Flow asks of its operator to that file. Nothing else depends on it.
