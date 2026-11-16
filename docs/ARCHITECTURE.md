# OpenScatter – architecture

How OpenScatter is designed and why. It describes the whole design; each section says what exists
today and what is still a plan. Today the engine (`core/`) exists, and so does a first version of
the 3ds Max plugin (`plugin/`): the object, its render outputs and its viewport, without an
interface yet.

## 1. Principles

1. **Deterministic between machines.** The same scene gives exactly the same instances on any
   computer and with any number of threads. This is a requirement, not a nicety: without it a
   render spread over several machines, or split in tiles, flickers or shows seams.
2. **Stable under edits.** Changing the density, a map or an area adds or removes instances; it
   never moves one that stays. This is also what will let hand edits (moved, deleted or painted
   instances) survive changes to the procedural settings.
3. **Old scenes do not change.** A scene stores the version of the distribution algorithm it was
   made with (`kAlgorithmVersion`), and a newer plugin keeps scattering it the same way unless the
   user asks to update it.
4. **The engine knows nothing about 3ds Max.** It is tested outside it, in seconds, with random and
   malformed input: a crash in a C++ plugin takes 3ds Max down with it, so the plugin stays thin and
   the engine is where the work and the tests are.
5. **Only the 3ds Max SDK in the plugin.** No renderer SDKs: instances reach each renderer through
   something 3ds Max itself provides.
6. **Written from scratch.** Existing scatters are a reference for behaviour only.

## 2. Components

```
OpenScatter.bundle
  OpenScatter plugin (C++, 3ds Max SDK, one build per 3ds Max version)        [exists, 3ds Max 2027]
    scatter object: parameters, references to surfaces and models             [exists]
                    references to areas and maps, rollouts                    [planned]
      |  extracts meshes (exists), flattened splines and baked maps (planned)
      v
    osc_core (C++20, no 3ds Max, no dependencies)                             [exists]
      pattern -> candidates -> density and filters -> model -> transform
      |  blocks of instances, in a fixed order
      v
    output adapters over the same instance buffer                             [exist]
      render: the standard render-time instancing interface of 3ds Max
      render, for renderers that do not read that interface: a Particle Flow operator
      viewport: GPU instancing
      fallback: one aggregated mesh
  Python (PySide6): lister, presets, export                                   [planned]
```

`osc_core` takes plain data – triangle meshes, polylines, images, numbers – and returns instances.
The plugin is a translator: it pulls that data out of the scene, calls the engine and hands the
result to the renderer and the viewport. Adding or removing an output adapter does not touch the
engine.

## 3. The engine (`core/`)

Everything works in one space, the local space of the scatter with Z up; the plugin brings its
inputs into that space and takes the instances out of it. The design is for that space to travel
with the scattered surface, so that moving or rotating it carries the instances along without
recomputing anything; in the first version of the plugin it is simply the world (§4.1).

### 3.1 Two ideas

**Stable id.** Every candidate has a 64-bit id that comes from *where it is born*, not from the
order in which it survives: `hash(pattern, seed, tile x, tile y, index in the pattern)`. Changing
the density, a map or an area does not change the id of the instances that remain. Everything
random about an instance – model, rotation, scale, translation – is `hash(id, seed of that aspect,
aspect)`. There is no random-number generator with state anywhere.

**Progressive rank.** Every candidate has a rank between 0 and 1 such that, for any threshold `d`,
the candidates with `rank < d` are well spread (blue noise). Density then reduces to one
comparison: the instance exists if `rank < d(p)`, where `d(p)` is the product of everything that
modulates the density at that point.

### 3.2 Patterns (`pattern.h`)

A pattern is a finite set of points on the unit square that tiles seamlessly, stored in rank
order: any prefix of it is itself blue noise. It is built by weighted sample elimination (Yuksel,
2015): random candidates are thinned down to the pattern, which is then halved again and again; the
order in which points are removed, reversed, is the rank order. With 16 384 points, every prefix
keeps its closest pair at about 0.63 of the spacing of a perfect hexagonal packing (random points:
0.02).

The generator uses only arithmetic and square roots, so a pattern can be regenerated bit for bit
on any machine; `osc_tilegen` writes patterns to `.oscp` files (a header, the points, a checksum)
and the tests pin the hash of the default one. A pattern's id is the hash of its points.

Planned: regular patterns (grid, staggered, hexagonal, rows) and patterns from an image; they need
no change to the scatter, only other point sets. A repeated pattern can show from above in very
regular fields; a set of tiles that match at their edges would break the repetition, again without
changing the scatter.

### 3.3 The planar scatter (`scatter.h`)

`scatterPlanar(pattern, settings, threads)` tiles the pattern over the XY plane with tiles of side
`tileSize`, and for every point of every tile that falls in the domain:

1. **Density.** `d = density / patternDensity` (the share of the pattern in use), times the
   weight of the areas there, times the density map there. Every factor is at most 1, so the
   candidate is dropped as soon as its rank is not below the running product.
2. **Projection** straight down onto the surface (`surface_index.h`); candidates with nothing
   under them are dropped. Without a surface the ground is the plane z = 0.
3. **Limits**: the angle between the surface normal and the vertical, and the altitude along the
   vertical. The vertical is a setting, so the plugin can measure from the world's or the object's.
4. **Model**: by probability.
5. **Transform**: the instance's Z goes along a blend of the vertical and the surface normal, by
   the shortest rotation (no twist); then a random rotation about its own X, Y and Z, a random
   scale along its own axes (so there is never shear), and a random translation along the base
   orientation.

`tileSize` fixes where every candidate is; `density` only moves the rank threshold. That is what
makes a density change add or remove instances without moving any. The price is that the density
cannot exceed what the pattern holds at that tile size (`patternDensity`), and that it comes in
whole pattern points per tile. The plugin picks the tile size from the density in steps of powers
of two (§4.1), a first answer that the interface may still change.

The seed shifts the tiling and picks one of the eight symmetries of the square, and is part of
every id: another seed is another scatter. Model, rotation, scale and translation each have their
own extra seed, so one aspect can be reseeded without touching the rest.

The **instance limit** (`maxInstances`) counts, over the whole domain, how many instances each
pattern index gives, and keeps the longest prefix of the pattern that stays within the limit. It
removes instances evenly everywhere, counts what the filters really leave, and does not depend on
the thread count. The same mechanism will serve the viewport percentage.

An `Instance` is 64 bytes: a transform (three axis vectors and a translation), the id, the rank and
the model. Problems come back as a status, never as an exception.

### 3.4 Surface projection (`surface_index.h`)

Dropping a point along a fixed axis needs no ray tracer: the triangles are binned by their XY
bounding box into a grid with about one cell for every two triangles, and a query tests the
triangles of one cell, in double precision, keeping the highest hit. One code path and plain
arithmetic: the same on every CPU. A million triangles are indexed in about 0.03 s.

Triangles with an index out of range, a vertex that is not finite or no area seen from above are
left out. A mesh whose triangles each cover a large part of it gets a coarser grid instead of an
index that eats the memory.

The normal is the triangle's, facing up. Planned: smooth normals, every layer of an overhang
instead of the highest, and a second generator that samples the surface by area for meshes that are
not terrains.

### 3.5 Areas (`area.h`)

An area is a set of shapes in the XY plane; the plugin flattens its splines to polylines and
projects them there. Its inside is what its closed shapes enclose (even-odd, so a shape inside
another is a hole) plus a band of a given width around its open shapes. Its effect fades with the
signed distance to the edge of that inside – full up to `falloffNear`, gone from `falloffFar` on –
which covers a fade outside the edge, inside it or across it with two numbers.

The result is the union of the include areas (everything, when there are none) minus the union of
the exclude areas: `max(include weights) * (1 - max(exclude weights))`.

So that a query does not walk every edge, each area is compiled into a grid whose cells list the
edges close enough for their distance to matter (a cell with none has a single answer) and bands
in Y that list the closed edges crossing them, for the inside test. The tests compare this against
the definition evaluated edge by edge on random concave and self-crossing shapes.

Planned: falloff curves, falloff of scale, areas from objects and from other scatters, painted
areas.

### 3.6 Maps (`image.h`)

Evaluating a 3ds Max map from several threads is not safe, so the plugin bakes each map to an image
on the main thread and the engine samples the image (bilinear, clamped or tiled) from every thread,
at the texture coordinates of the surface under the candidate or over a rectangle of the plane.

### 3.7 Threads and order (`parallel.h`)

Each tile is computed on its own, writes to its own block, and the blocks are kept in a fixed
order (row after row; inside a block, in rank order). Which thread computes which tile, and when,
cannot change the result. A tile first collects the candidates that stay and then allocates a block
of exactly that size, so the result holds no spare memory.

The work is spread with a small parallel loop that starts its threads on each call. A pool that
outlives the call would save about a millisecond but is one more thing to get right when 3ds Max
unloads the plugin; it can replace the loop without touching anything else.

### 3.8 The floating-point rules

The same bits on every machine means:

- IEEE 754 arithmetic to the letter: no fast-math, and no contraction of `a * b + c` into one fused
  operation, which rounds once instead of twice. With MSVC that is `/fp:precise`; with GCC and
  Clang, `-ffp-contract=off`. The root `CMakeLists.txt` sets it for every target.
- Only operations IEEE 754 defines exactly: addition, multiplication, division, square root.
  The C library's `sin`, `cos` or `pow` may take another code path on another CPU and differ in the
  last bit, which is enough to move an instance across a threshold. `detmath.h` has a sine and
  cosine made of additions and multiplications; powers are written out.
- Nothing depends on the order of evaluation: no shared random state, no sums of floats across
  threads (the statistics are whole numbers).

The tests hold the engine to this: the hash of a scene that uses every feature must be the same
with 1, 4 and 32 threads and equal to a stored value, in release and in debug builds.

## 4. The plugin (`plugin/`)

A first version exists, for 3ds Max 2027: the object, the three render outputs, the viewport and
the bundle. It has no interface yet – every setting is a scriptable parameter – and it exposes only
part of the engine (§4.8). `plugin/README.md` says which file holds what.

### 4.1 The object and its instance buffer

`OpenScatter` is one geometry object. Its settings live in a parameter block, so MAXScript and
Python read and write them with no extra work, and its surfaces and models are ordinary 3ds Max
references held by that block: deleting one, or changing it, reaches the object like any other
change. A validator keeps an OpenScatter object, or anything that depends on this one, out of both
lists.

The object owns **one instance buffer**, and every output only reads it:

```
surfaces (nodes) --- one triangle mesh, in world space --+
settings ------------------------------------------------+--> osc::scatterPlanar --> instances
models (nodes) ----- mesh, pivot offset, material slots -+                           (world space,
                                                                                      one flat array,
                                                                                      a list per model)
```

- The engine's space is the **world**: the surfaces are brought to it with their node transforms
  and the instances come out in it. An object has no node of its own to anchor to when a renderer
  asks it for instances, and several surfaces share no local space. The price is that moving a
  surface drops the same pattern onto it again instead of carrying the instances along; anchoring
  the pattern to a surface is still to be designed.
- A model's mesh is kept in *pivot space*: its render mesh moved by the node's object offset, so
  that one instance transform places the pivot, and all outputs agree on what that means.
- **Nothing is recomputed without a reason.** The object keeps two flags. A change to the models
  (their meshes, their materials, their list) only rebuilds the model data; the scatter runs again
  only when a surface, a setting that decides instances, or which models are usable changed.
  Display and route settings touch neither. Both results carry a validity interval, so an animated
  surface rescatters per frame and a still one never does. The buffer has a version number each
  output compares with the one it last built from.
- The pattern is the one `osc_tilegen` writes with its default arguments, generated once per
  session on first use.

**Density and tile size.** The density is typed in instances per square metre and converted with
the scene's unit scale. The engine needs a tile size too, and the tile size fixes where every
candidate is, so it must not follow the density smoothly. Left automatic (`patternSize = 0`) it is
the largest power of two, in scene units, at which the pattern still holds the density: the pattern
is then used between a quarter and all, and changing the density inside one such step adds or
removes instances and moves none. Crossing a step lays the pattern out again. This is a first
answer to a question the engine left open, not a final one; `patternSize` sets the tile by hand.

The object publishes a few functions next to its parameters: the instance count (whole and per
model), an instance's transform and model, a status in words, the statistics of the last scatter,
the engine's hash of the buffer, the active route, and what each output handed out in the last
render.

### 4.2 Render: one route at a time

Three adapters read the buffer; `renderRoute` says which one hands the instances to the renderer,
and **only that one does**: the instancing interface is not published, the Particle Flow operator
holds no particles and `GetRenderMesh` returns an empty mesh whenever they are not the active
route. Left on automatic, the route follows the renderer in use, by its name:

| Renderer | Route | State |
|---|---|---|
| Arnold | The instancing interface | Tested: no pixel differs from the aggregate mesh, with three models, a Multi/Sub-Object material and a moved pivot. A million instances in about a second. |
| V-Ray | The instancing interface | **Not rendered by us.** V-Ray's own code wraps any object that publishes the interface. |
| Redshift, Octane | The instancing interface | Not tested; the interface's own documentation names them as readers. |
| Corona | Particle Flow | Tested with Corona 16: no pixel differs from the aggregate mesh, same scene. A million instances in about ten seconds. |
| Scanline, ART, unknown | The aggregate mesh | Tested with the scanline renderer. |

Each adapter counts what it hands out, and the object reports the counts after a render. The tests
use that to check, for every route, that exactly one adapter emitted – which is how a double render
that the picture cannot show (the same geometry twice in the same place) was found and fixed, see
§4.4.

### 4.3 The instancing interface

`MaxSDK::RenderTimeInstancing::RenderTimeInstancingInterface`, part of 3ds Max since 2023. The
object publishes one *source* per model – the model's node, so the renderer uses its object and
its material as they are – and one *target* per instance, with its world transform and the
engine's stable id. Nothing is copied: a target is filled from the buffer when the renderer asks
for it, into memory kept per thread, as the interface allows. The transform handed out is the
instance's, preceded by the model's object offset: the renderer draws a node source from its
object space, and the instance places the pivot.

### 4.4 Corona: the Particle Flow carrier

Corona does not read the instancing interface, and offers a third-party object no other general
door. What it does instance, and what is part of 3ds Max, is Particle Flow: particles that share
one shape mesh are rendered as real instances. So for Corona the instances travel as particles.

**The operator.** `OpenScatterOperator` is a Particle Flow operator of ours and the only action of
its event. It gives birth to the particles itself – one per instance – and writes each one's
position, orientation, scale and shape from the buffer; a standard Birth operator would need to be
told the count before it is known. There is no simulation: every step makes the particles match
the buffer again. The shape of a model is set as one value shared by all its particles, which is
what lets the renderer instance it. The operator is also the event's material holder: it answers
with the combined material (§4.5).

It emits only while Particle Flow is in its render state and Particle Flow is the active route of
a scatter node the renderer draws. In the viewport, and with every other route, the carrier holds
no particles.

Two things about Particle Flow shaped it, both found by measuring:

- *Its simulation starts one step before the earliest moment a birth action is active.* An
  operator that is active "for ever" makes it start at tick 0, and frame 0 itself is then never
  computed: an empty render. The operator's activity therefore begins at a real time, the start of
  the animation range (or frame 0 when the range starts later). The price is one step per frame
  between that start and the frame rendered when a render starts from scratch – about 25 ms per
  step for half a million instances. Starting the simulation next to the frame being rendered
  would remove it; that is not done yet.
- *It keeps its particles from one render to the next when none of its actions changed* – and
  choosing another renderer changes none. After a Corona render, Arnold would have been given the
  left-over particles next to the instances of its own route. When a render starts, each operator
  therefore compares what it emitted last with what this render needs, and when they differ tells
  Particle Flow that it changed, which makes it drop what it kept.

**The source: created, owned, out of the way.** The Particle Flow source around the operator is
built by the plugin, never by the user.

- *Created* the first time Particle Flow becomes the active route of a scatter object: when the
  route or the renderer changes, and again when a render starts, as the last moment the scene may
  still change. The graph is assembled by a short MAXScript the plugin runs – the documented way to
  build one – and is four nodes: the source, its event, our operator and a Render operator.
- *Owned* by nobody through references: the operator references the scatter node, the scatter
  object references nothing of the carrier. There is no loop, and deleting either side never
  deletes the other by accident. The four nodes carry the same user property – the handle of the
  source – so they are found and removed together. A carrier counts only while it is whole: when
  its scatter object is gone, or the user deleted a part of it, what is left is removed at the next
  sync and a new one is built if it is needed. An operator emits only under the source its tag
  names, so a carrier cloned along with the rest of a scene stays empty instead of emitting the
  same instances again. The carrier is saved with the scene and found again on load – also after a
  round trip through a 3ds Max without the plugin.
- *Out of the way*: no logo, no emitter icon, viewport quantity 0, frozen so that it cannot be
  picked in the viewport. It cannot be hidden – the renderer would skip it – and it does appear in
  Particle View, like every source.

Limits of the route: a Particle Flow source holds at most ten million particles; Corona gets no
per-instance colour this way, only a scalar or a material; and it costs Corona about twice the
time of its own scatter, most of it in Corona's reading of the particles, not in the operator.

### 4.5 The combined material

Particles take the material of their event, and the aggregate mesh takes the material of the
scatter node: both need one material for all the models. The object keeps a Multi/Sub-Object
material that gathers them – one slot per model, or one per sub-material when the model's own
material is itself a Multi/Sub-Object – and gives the faces of each model's mesh the matching
slots. The layout depends only on the models' node materials, so the material can be rebuilt on
the main thread without evaluating anything, and the meshes follow when they are next read. A
scatter node with no material of its own is given the combined one; a material the user chose is
never replaced. The instancing interface needs none of this: there each model renders with its own
node's material.

### 4.6 The aggregate mesh

One mesh with a copy of the model at every instance: what `GetRenderMesh` returns when the mesh is
the active route, and what collapsing the object or taking a snapshot of it gives. It duplicates
geometry, so it is capped (two million faces by default); above the cap the instances are thinned
by rank – evenly everywhere, like every other thinning here – until it fits.

### 4.7 The viewport

The instances are drawn with 3ds Max's viewport instancing API
(`MaxSDK::Graphics::ViewportInstancing::InstanceDisplayGeometry`): per model, one geometry – its
bounding box or its mesh – and one transform per instance, sent to the graphics card once and
rebuilt only when the buffer or a display setting changed. A geometry made of triangles shows only
in shaded views, so every model also gets one made of lines (the edges of the box, or the visible
edges of the mesh) for wireframe views. Each model is drawn in its node's object colour, flat.

How much is drawn depends on the display driver, because with the software driver (Nitrous
Software, Direct3D WARP) every triangle is drawn on the CPU:

| Driver | Instances | Triangles | Measured |
|---|---|---|---|
| Graphics card (Direct3D 11) | 2 000 000 | 30 000 000 | A million boxes: no measurable cost per frame on an RTX 4060 Ti. 51 605 meshes, 27 million triangles: a few milliseconds. |
| Software, or unknown | 20 000 | 250 000 | 20 000 boxes add 0.1 to 0.6 s to a frame that already takes 0.5 s empty at 4K. |

Above the instance budget the display is thinned by rank; meshes that exceed the triangle budget
are drawn as boxes; both limits can be set by hand. The SDK does not say which driver 3ds Max
created (the graphics window's driver string is empty with Nitrous, and the settings file does not
see a driver chosen on the command line), so the plugin asks 3ds Max's own `NitrousGraphicsManager`
interface once, through MAXScript: 3 is Direct3D 11, 4 is the software driver.

Next to the instances the object draws a small icon at its node, the classic way, so that an empty
scatter can still be seen and picked. Selection hits the icon, the instances where the graphics
card does the hit-testing, and otherwise the box around all of them.

### 4.8 What the plugin does not do yet

- **Interface.** No rollouts. The plan: Qt rollouts in the Modify panel bound to the parameter
  block, in the order one works (objects, distribution, surface, transform, display, statistics),
  with a creation macro that fills the lists from the selection; a lister and presets in Python.
- **The rest of the engine.** Areas (the splines have to be flattened to polylines) and density
  maps (they have to be baked to images on the main thread) are in the engine and not yet in the
  object.
- Models made of several objects, proxies, per-instance colour and material, motion blur, camera
  clipping, point-cloud display, converting to instances.
- Interactive rendering has not been tested in any renderer; neither has V-Ray at all.

## 5. Tests and measurements

| Level | What it checks | Where |
|---|---|---|
| Engine | Every generator and filter; edge cases (empty mesh, zero density, degenerate shapes, values that are not numbers); random nonsense in every setting. | `tests/core`, outside 3ds Max, in a couple of seconds. |
| Determinism | The same hash with 1, 4 and 32 threads, equal to a stored value. | `tests/core/test_determinism.cpp` |
| Stability | Raising the density, editing an area or a map, reseeding one aspect: every instance that stays is the same bit for bit. | `tests/core/test_stability.cpp` |
| Speed | One and ten million instances on a million triangles, against targets of 1 s and 10 s. | `tools/bench` |
| Plugin, object | Loads from the bundle; recomputes only for a reason; the carrier's life; the combined material; a snapshot is the aggregate mesh. | `tests/plugin`, in `3dsmaxbatch` |
| Plugin, render | Per renderer: a control object, nothing with the route off, the renderer's route against the aggregate mesh pixel by pixel in colour, exactly one adapter emitting, nothing left over, and up to a million instances with time and memory. | `tests/plugin`, in `3dsmaxbatch` |
| Plugin, scenes | Saved and reopened; opened without the plugin, saved, and opened with it again. | `tests/plugin`, four processes |
| Plugin, determinism | The buffer's hash in two processes, and with 1, 3 and all threads. | `tests/plugin`, two processes |
| Plugin, viewport | Boxes and meshes in shaded and wireframe views, the budget per driver, thinning. | `tests/plugin`, in a second 3ds Max with its interface, once per driver |

Every plugin test runs in a process of its own with the bundle loaded from a private folder: a
render test resets the scene it runs in, and a crash in a plugin takes 3ds Max down with it.

Measured with `osc_bench` on a 32-thread desktop CPU: one million instances on a terrain of one
million triangles in 0.012 s (0.14 s on one thread); ten million in 0.07 s (1.2 s on one thread),
in a buffer of 0.64 GB. Scattering is not the bottleneck; handing the instances to the renderer is.

Measured with the plugin tests on the same machine (3ds Max 2027.2, a 1 024-face model, 320 × 240,
instances small enough not to pile up):

| Instances | Scatter | Arnold, instancing interface | Corona 16, Particle Flow |
|---|---|---|---|
| 51 605 | 0.04 s | 0.8 s, +23 MB | 1.5 s, +33 MB |
| 515 996 | 0.09 s | 1.0 s, +264 MB | 5.4 s, +601 MB |
| 1 032 086 | 0.11 s | 1.1 s, +325 MB | 9.8 s, +617 MB |

The times are whole renders, the renderer's start-up included; the memory is the growth of the
process's peak.

## 6. Roadmap

| Version | Contents |
|---|---|
| 0.1 | Scatter on surfaces, models with probability, density and density map, random transforms, areas with falloff, slope and altitude, viewport modes, render in the three renderers, statistics. |
| 0.2 | Along splines, in volumes, UV mode and regular patterns, maps on transforms, look-at, falloff curves, camera clipping, collisions, lister, presets. |
| 0.3 | Clusters, editing and painting instances, per-instance colour, edge trimming, areas from objects and from other scatters. |
| later | Painted areas, level of detail, animation offsets, export. |

Of 0.1, the engine side exists except scattering by surface area. The plugin side exists as a
first version: the object, the render routes and the viewport, driven from scripts. Still to do
for 0.1: the interface, areas and density maps in the object, a render in V-Ray, and the release
itself.
