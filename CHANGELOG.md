# Changelog

What changed for users in each version of OpenScatter, newest first.

## In development

There is no release yet. OpenScatter is being built from its engine outwards: the engine exists,
and now so does a first version of the 3ds Max plugin around it – without an interface so far, so
it is driven from MAXScript or Python and has to be built from source.

**The 3ds Max plugin (3ds Max 2027)**

- **The OpenScatter object.** One object that scatters models over the surfaces you give it: pick
  the surfaces, the models and a probability for each, and set the density (instances per square
  metre), the seed, the random rotation and scale, how far the instances lean with the ground, and
  limits by slope and altitude. Every setting is scriptable; moving or editing a surface updates
  the scatter, and nothing is recomputed unless something it depends on changed.
- **Real instances in Arnold**, and in every renderer that reads 3ds Max's own render-time
  instancing: each model is rendered from its own object with its own material. A million
  instances of a thousand-polygon model render in about a second on a test scene.
- **Real instances in Corona.** Corona does not read that interface, so OpenScatter hands it the
  instances as Particle Flow particles, which Corona instances natively. The Particle Flow source
  is created and kept by OpenScatter: you do not build or edit it, it shows nothing in the
  viewport and it holds no particles except while Corona renders. A million instances render in
  about ten seconds – roughly twice the time of Corona's own scatter – with every model keeping its
  material, Multi/Sub-Object materials included.
- **V-Ray** is given the instances the same way as Arnold. V-Ray's own code reads that interface,
  but OpenScatter has **not been rendered in V-Ray yet**: treat it as untested.
- **Other renderers** (Scanline, ART) get one mesh with a copy of the model at every instance,
  capped in size and thinned evenly when it would be too large.
- **Nothing renders twice.** One setting decides how the instances reach the renderer; left on
  automatic, OpenScatter picks the right way for the renderer in use.
- **Viewport.** Instances are drawn by the graphics card as boxes or as the models themselves, in
  shaded and in wireframe views, each model in its object colour. How much is drawn follows the
  display driver: millions of boxes with a graphics card, far fewer with the software driver, and
  beyond the budget the instances are thinned evenly rather than cut off.
- **Scenes survive without the plugin.** A scene opened in a 3ds Max that does not have
  OpenScatter keeps the object as a stand-in, renders nothing of it, and gets it back – settings,
  instances and all – when opened again with the plugin, even after being saved without it.
- **One bundle.** The build puts together `OpenScatter.bundle`, the package 3ds Max loads from its
  ApplicationPlugins folder.

Not there yet in the plugin: the interface in the Modify panel, areas and density maps (the engine
has them), models made of several objects, proxies, per-instance colour and motion blur.

**The engine**

- **The scatter engine.** It distributes instances over a surface with a blue-noise pattern, so
  they look natural at any density, and gives each one a model, a rotation, a size and a position
  on the ground.
- **Nothing jumps when you edit.** Raising the density only adds instances and lowering it only
  removes them; changing an area, a density map or a limit adds or removes instances there and
  leaves every other one exactly where and how it was.
- **Density maps.** A map baked to an image thins the scatter smoothly, without clumps, in the
  surface's texture coordinates or over a rectangle of the ground.
- **Areas.** Include and exclude areas from closed shapes (a shape inside another makes a hole) and
  from open shapes with a width, like a path. Each area can fade in or out over a distance, inside
  or outside its edge.
- **Slope and altitude limits**, measured from any vertical.
- **Several models** with a probability each.
- **Random rotation, scale and translation** per axis, uniform or not, and a continuous blend
  between standing upright and following the surface. Each of them can be reseeded on its own,
  without touching the others or moving anything.
- **An instance limit** that removes instances evenly over the whole scatter instead of leaving a
  part of it empty.
- **The same result everywhere.** The same settings give exactly the same instances on any
  machine and with any number of threads.
- **Fast.** One million instances on a terrain of one million triangles in about a hundredth of a
  second on a 32-thread CPU, and ten million in under a tenth.
- **Tools.** `osc_tilegen` generates the distribution patterns, reproducibly; `osc_bench` times
  the engine on your machine.
