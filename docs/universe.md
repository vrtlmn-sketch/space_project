# Universe — design notes

Status: design. Nothing here is implemented yet.

## Vision

The user creates **universes**, not just galaxies. A universe is a first-class
entity that contains galaxies, clouds, planets and everything else, generated
from a seed and optionally anchored to real astronomical data.

```
Universe/
  Sol
  Sagittarius A*
  ~10k galaxies
  clouds, planets, ...
some objects outside the universe   <- untouched, not participating
```

**Ownership rule (the one that must not be broken):** the universe owns
everything it generates, edited or not. An edited galaxy stays a universe
member carrying edited fields — it does not migrate out and become a different
kind of object. Anything outside the universe is a separate world: not a child,
not affected, not generated. One rule, no mixed ownership.

## The universe entity

It is *not* a transform parent. A universe has no position, rotation or scale —
it IS the coordinate space. Nothing to move, no cascading gizmos, no nested
frames. It is purely a container plus a generator configuration.

It plays two UX roles:

1. **Time traveller** — deferred. Later, a universe can simulate the passage of
   cosmic time: redshift, metric expansion, and interesting behaviour near black
   holes. Not being built now, but see "Reserved for time" below.
2. **Spawnable, shareable entity** — a universe can ship inside a project, or be
   spawned into an existing project like any other object. There is a galaxy
   spawner; there is also a universe spawner.

**Universes are tiny to share.** A universe is fully described by its seed,
generator parameters, source configuration and a sparse override list — a small
JSON file that reproduces bit-identically for anyone who opens it. That property
comes for free from being procedural, and it is worth protecting.

## Persistence

Generated content is never stored. A galaxy is `id -> seed -> generator`, so it
regenerates identically on every visit and costs nothing when you leave.

Edits are stored as **sparse overrides** keyed by stable id:

```json
"overrides": {
  "G-4471829": { "armCount": 5, "radius": 1.4e9, "name": "Saulius A" },
  "G-9902311": { "deleted": true }
}
```

Ten million galaxies, twelve edited -> twelve entries.

**Hard requirement: stable ids.** An override is worthless if the object it
names can shift identity. Catalogue objects use the catalogue id. Procedural
objects need an id derived from spatial cell + index so it cannot move when an
unrelated parameter changes. Painful to retrofit; decide before writing data.

## Reality mix

The organising idea: one slider from real to procedural, with both ends
configurable.

```
        35% real
           v
REAL  ---- * ------------  PROCEDURAL

[ Configure Real Source ]   [ Configure Generator ]
```

### Real source
- Dataset preset: best available / Gaia-focused / extragalactic / nearby / custom
- Data sources: stars, galaxies, exoplanets, nebulae, black holes, clusters
- Unknown data: leave unknown / infer statistically / procedurally complete
- Measurement handling: best estimate / sample uncertainty / show uncertainty
- Epoch

**Real does not mean complete.** Catalogues are magnitude-limited, geometrically
biased and full of holes. "Procedurally complete" must be *seeded from* the real
object so the filled-in parts stay deterministic and consistent with what is
actually known.

### Procedural
- Seed (+ randomize)
- Generator: standard cosmological / artistic / uniform / clustered / custom
- Scale: radius (default 46 Gly)
- Structure: cosmic web, clustering, void size, galaxy density
- Galaxy population: spiral / elliptical / irregular split
- Physical model: realistic / relaxed / custom laws
- Generation depth: galaxies / stars / planets / surfaces (full, dynamic, on demand)

### Mixing
- Axis label: `N% empirical` (see Decisions)
- Modes: preserve observed objects / spatial blend / statistical blend /
  replace progressively
- Reality constraints by scale: solar system (locked) -> distant universe
- Reality by data type: stars, exoplanets, galaxies, nebulae, black holes,
  dark matter

Because there are many dials, **presets are the primary interface**. Most users
should never open the detail panels.

## Decisions

1. **Global slider first, then a "home surroundings" toggle.** Ship the single
   macro slider before any of the detailed panels. The toggle is separate and
   discrete: it pins familiar anchors into the universe regardless of where the
   slider sits — the real Milky Way at a chosen fidelity, the Solar System with
   its actual planets — so a user can start somewhere recognisable. Per-scale
   and per-type dials come later; when they arrive, the slider acts as a macro
   that writes them and touching one pins it.

2. **The axis is EMPIRICAL, not "real".** Nothing is 35% real — an object either
   comes from a catalogue or it does not. The number is how much of the universe
   is empirically grounded. Label the slider `35% empirical`.

3. **Multiple universes per project are allowed.** Not a headline feature, but
   nothing should assume a single one. Objects belong to exactly one universe;
   universes never nest.

4. **The reality boundary is per SCALE BAND.** Not per object (interleaving real
   and generated neighbours reads as inconsistent) and not per region (visible
   seams). Reality falls off with distance from the observer, which is honest:
   we genuinely know more about nearby things. The "Reality constraints by
   scale" table is the interface to this. Per-data-type percentages refine
   within a band.

5. **Physics is OFF by default for everything a universe generates.** Stars and
   galaxies are fixed points unless a user explicitly enables simulation on
   something. At these counts anything else is untenable, and a static universe
   is also the physically sensible default at cosmological scales.

## Technical constraints

**Coordinate precision is the hard limit.** 46 Gly is 2.9e15 AU. A double gives
~15-16 significant digits, so absolute positions at that range resolve to about
1 AU — and planet surfaces need metres. A single absolute frame cannot span the
whole feature. Hierarchical frames are required: universe -> galaxy -> system ->
surface, each with a local origin. The renderer already does part of this
(camera-relative doubles, chunk-relative floats).

**Streaming is solved.** The chunked starfield path (static VBO, per-chunk
frustum culling, budgeted LOD) already holds 8.2M stars in 46 MB of VRAM and is
data-agnostic — it does not care whether stars come from a catalogue or a
generator. `.starfield` is the handoff format between generator and renderer.

**Generator quality is adequate.** `generate_milky_way_real.py` already has a
two-component exponential disc, logarithmic spiral arms, arm-tied star clusters
and a rotation curve. It needs parameterising (seed, type, radius, arms,
inclination), not replacing.

## Reserved for time

Time travel is deferred, but the data model should not preclude it: keep an
`epoch` field on the universe and make generation a function of `(seed, epoch)`.
Cheap to reserve now, expensive to retrofit.

## Plan

1. **UI mockup first.** Build the full panel with every dial visible and
   "not implemented yet" beside the inactive ones. Getting the shape wrong is
   cheap now and expensive later.
2. **Implement one extreme end: the fully procedural universe.** Avoids the data
   sourcing question entirely and exercises seed -> galaxies -> stars end to end.
3. Then the home-surroundings toggle (real Milky Way + Solar System anchors).
4. Then real sources, then mixing, then time.

## Implementation notes

- Generated objects default to `simulatePhysics = false`.
- Hierarchical coordinate frames (universe -> galaxy -> system -> surface) are
  required before any position data is written; see Technical constraints.
- `.starfield` chunking is the streaming mechanism and already works; the
  generator should emit it directly rather than JSON.


---

# Implementation status  (updated at session end)

## Built and working

**`src/universeGen.{h,cpp}`** — deterministic generators, pure functions of a seed
(xorshift64*, never `std::rand`, so a universe reproduces bit-for-bit anywhere):
- `GenerateUniverseGalaxies(params, out)` — galaxy descriptors: position, radius,
  type, seed, inclination, arms. Clumps galaxies around attractor nodes so the
  result has filaments and voids rather than a uniform fog.
- `GenerateGalaxyStars(desc, count, out)` — spiral (two-component exponential
  disc + logarithmic arms + bulge), elliptical (r^2.2, mildly triaxial),
  irregular (knots + envelope). Inclination and roll applied per galaxy.

**`RenderedObject::BuildGalaxyStarfield(desc, count)`** — one galaxy into one
chunk, in galaxy-LOCAL coordinates. The owning cloud carries the double-precision
universe position, so chunk centres stay small and float-safe at any distance.

**`main.cpp` `renderer.universeCreate`** — spawns ONE CloudObject PER GALAXY,
named (`Spiral Galaxy 7`), `universeMember = true`, physics off. This replaced an
earlier design that packed every galaxy into a single cloud: efficient, but
galaxies then had no presence in the scene and could not be selected, located or
edited. Do not go back to that.

**Hierarchy** — `[U] Universe (N galaxies)` parent node with galaxies indented
beneath it and a Settings shortcut to the generator panel. Only `universeMember`
clouds are grouped; hand-placed clouds stay top level and unaffected.

**UI** — Spawn -> Universe tab is just a button; the generator is a floating
window (`showUniversePanel`, `DrawUniversePanel`). 54 controls, 17 marked
"(not implemented)". Seed + Randomize and the galaxy/star count sliders are live.

**Test gates** (no GUI needed):
```
UNIVERSE_TEST=<galaxies>   build a universe at startup
UNIVERSE_RADIUS=<Gly>      override radius
UNIVERSE_STARS=<n>         override stars per galaxy
PROJECT=<path>             load a specific project
```

## Bugs fixed (do not re-introduce)

1. **`CloudObject::position` was `vec3`** — a float. At 1e15 AU a float resolves
   to ~1e8 AU, coarser than a whole galaxy, and it overwrote the correct
   `dvec3 RenderedObject::coordinates` every frame. Now `dvec3`.
2. **`StarChunk::center` was `vec3`** — same problem. Now `dvec3`, and the
   shader uniform is the CAMERA-RELATIVE centre, differenced in double on the
   CPU. `cloudVert.glsl` uses it directly for starfields and skips
   `uCloudOrigin`/`uCloudRot` (which would add a large number back).
3. **`boundsEstimate` returned cloud-LOCAL float coordinates** for starfields
   while callers treated the result as world — so Locate flew to the origin.
   Now WORLD-space `dvec3` + `double`. Five call sites updated.
4. **LOD spent the whole budget regardless of screen size** — distant galaxies
   drew 50k stars into one pixel, summing to a blown-out spike. Allocation is
   now capped by the screen area a chunk actually covers: the same view went
   from 300,000 stars drawn to 104.
5. **`dustInfluence`** was derived from the whole cloud's radius; at universe
   scale every star in a galaxy hashed to the same value. Now scales to a
   representative chunk extent (local structure).

## Known broken

- **Close-up galaxies look sparse.** Not an LOD failure: `LocateCamera` frames at
  5.7x radius (~20 degrees), which covers ~500k pixels on a 1080p screen, and a
  galaxy has 50k stars — 0.1 stars per pixel. Cannot be fixed by raising the
  global count (200 galaxies x 400k = 80M stars, ~480 MB VRAM).
- **Grid and gizmo overlays shred at ~1e15 AU** — the editor overlays still build
  geometry in float. Same root cause as the fixed items above, not yet converted.
- **Scale sweep**: renders at 0.001 Gly, black from 0.01 Gly up. The position and
  chunk-centre fixes improved it (0.000 -> 0.003 mean) but did not resolve it.
  Bisect between those two radii to find the remaining float narrowing.
- **milky_way regression baseline drifted 60.95 -> 60.94** after the float->double
  position change. Almost certainly benign, unverified.

## Galaxy level of detail  (built)

TERMINOLOGY, as agreed with the user: a galaxy HAS `galaxyFullStars` stars — that
is what the user typed and it is the galaxy's identity. An LOD is any cheaper
stand-in: one point, then a few stars, then more, then the full galaxy. The user
never controls LOD star counts; that is an implementation detail.

- `RenderedObject` keeps `galaxyDesc` (so it can be regenerated at all),
  `galaxyFullStars` (identity) and `galaxyStarCount` (what is built right now).
- Galaxies SPAWN at 128 stars, not at full size. Spawning 800 galaxies at full
  size costs gigabytes and nearly all of them are a few pixels wide. The ladder
  climbs on approach and never exceeds `galaxyFullStars`.
- `UpdateUniverseDetail` (main.cpp) picks the rung from screen coverage alone:
  `want = 4 * pi * r_px^2` (about four stars per pixel it covers). One rung per
  rebuild, 4x deadband between climbing and dropping so drifting near a boundary
  cannot thrash. At most ONE rebuild per frame — a full galaxy can cost ~25 ms.
- `starBudgetOverride` is set to the built count: generation IS the LOD, so the
  global star budget must not second-guess it and drop stars we chose to build.
- The UI reports `galaxyFullStars`, never the live count. The built count changes
  as you fly and watching it jump reads as a bug (it was reported as one).

An earlier version had this backwards — "stars each" was a floor that a global
"Galaxy Detail" setting multiplied up to 300k. Do not go back to that: the number
the user types has to be the number they get.

### Distant galaxies were drawn as yellow balls

The haze pass in `cloudVert.glsl` sized its lobe in fixed SCREEN pixels with an
8 px floor, so a galaxy 4 px across was a stack of 8 px+ additive lobes — a
saturated blob bigger than the galaxy. The dust and gas passes never had this
problem because they are perspective-sized; the haze pass just never got the
same treatment.

Fixed by capping the lobe with `uChunkScreenPx` (the chunk's projected radius,
already computed by `drawStarfieldChunks`) and returning the light the shrunken
lobe would have spread, via the area ratio, so flux is preserved. Up close the
radius is huge and the cap does nothing, so near views and procedural clouds are
untouched — verified: milky_way 61.619 before / 61.616 after.

### Crash: stack smashing in renderCloud

`renderCloud` read the render mode back off the GPU with `glGetUniformiv` into a
4-byte stack GLint; it was the only call in that function writing through a
pointer to a stack local, and it aborted with `*** stack smashing detected ***`
once a universe existed. `uploadRenderMode` already caches the same value in
`cachedRenderMode`, so the read-back was pointless. It was also a driver
round-trip PER CLOUD PER FRAME — 200 of them with a universe loaded.

### Known problem: particle count is standing in for physical density

The dust pass (`renderedObject.cpp`, `glBlendFunc(GL_ZERO, GL_SRC_COLOR)`) emits
one multiplicative extinction sprite PER DRAWN POINT, so optical depth scales
linearly with the draw count. Raising a galaxy from 100k to 800k stars makes it
DARKER (mean 83.7 -> 57.3); with that pass skipped the curve goes monotonic
(91.2 -> 100.8), which is how this was isolated.

The same object at two detail levels must have the same brightness and the same
dust column. The fix is a `uSampleWeight = referenceCount / drawn` uniform
scaling dust optical depth and haze/core brightness, defaulting to 1.0 so
existing clouds are untouched. This also fixes a latent bug: `.starfield`
catalogues already dim and brighten as the LOD budget drops stars with distance.

## Also queued

- Black holes at galaxy centres — gives each galaxy a navigable anchor object,
  which suits "children you can select and fly to" better than a cloud of points.
- Convert grid/gizmo overlays to camera-relative doubles, or suppress them past
  the scale where they are meaningless.
- `BuildProceduralUniverse` (all galaxies in one cloud) is superseded by the
  per-galaxy path and should probably be deleted.

---

# Open issues  (handoff — read before touching galaxy rendering)

## 1. Distant galaxies flicker while moving  (UNSOLVED, prime suspect known)

Symptom: distant galaxies pulse/flicker as the camera moves. Only in motion —
invisible in a still, so DO NOT try to diagnose it from harness mean luminance.
Get a screenshot or fly. Three wrong guesses were burned reasoning from means.

PRIME SUSPECT, in `cloudVert.glsl`, added while fixing the "yellow balls":

```
vHazeBoost = clamp((sz * sz) / max(capped * capped, 1e-4), 1.0, 48.0);
```

`capped` comes from `uChunkScreenPx`, recomputed EVERY FRAME from camera
distance, so the boost varies continuously as you fly — brightness pumping,
which is what flicker looks like. It also reaches 48x, and combined with an
experiment that drew every built star (instead of a screen-area subset) it
whited out the whole view.

FIRST EXPERIMENT: force `vHazeBoost = 1.0` and fly. If the flicker stops, it is
confirmed. Then the real question is how to keep distant galaxies visible
WITHOUT a multiplier that tracks live camera distance — e.g. derive it from the
galaxy's own fixed properties, or cap it far below 48.

The size cap and the boost are A PAIR: the cap shrinks the lobe, the boost
returns the light. Removing the boost alone makes distant galaxies dimmer than
the version that was signed off.

Ruled out (verified, do not re-investigate):
- Stars do NOT move between LOD levels. `GenerateGalaxyStars` is a proper
  prefix: LOD 128 and LOD 1024 share identical first-128 positions.
- Camera-relative differencing in double on the CPU is intact (the `4682aeb`
  fix). Float precision of `uChunkCenter` is ~2^-24 relative, i.e. sub-pixel at
  any distance.

Attempted and REVERTED (all three changed the look without fixing the flicker):
- Deriving the chunk frame from the desc instead of the sampled bounding box.
  NOTE: this fixed a REAL bug — the bbox-derived centre shifted ~1e8 AU per rung
  and the shader hashes star colour, magnitude and the dust lane field on
  position RELATIVE TO THAT CENTRE, so every rebuild re-rolled the galaxy. Worth
  redoing on a clean base. Per-type bounds (spiral 1.30, elliptical 1.15,
  irregular 1.70 x radius) measured over 300 galaxies; a flat 1.7 washes spirals
  out.
- Drawing every built star for galaxies (`n = sc.count`) instead of the
  per-frame screen-area cap. Made galaxies much brighter.
- Quantising the capped sprite size to integer pixels. Also shifted the
  milky_way baseline 61.62 -> 61.79 when applied unconditionally.

## 2. Draw order: things render through things  (diagnosed, not fixed)

Two symptoms, one cause:
- Background galaxies shine through a foreground galaxy's dark dust lanes.
- Galaxy haze edges overflow ON TOP OF planets.

Clouds draw in LIST ORDER with `glDepthMask(GL_FALSE)`, so a background galaxy
drawn later paints over an earlier galaxy's multiplicative dust. A point sprite
also carries ONE depth for the whole quad, so a large haze lobe centred beside a
planet bleeds across the planet's silhouette instead of being clipped by it.

Fix: draw back-to-front by camera distance. CRITICAL: build a sorted INDEX LIST
for drawing — do NOT reorder the `clouds` vector. Selection sentinels encode
cloud indices as `-(2 + i)`, so shuffling it silently retargets selections and
deletes.

## 3. Editor overlays shred past ~1e15 AU

Grid and gizmo still build geometry in float. Same conversion the rest of the
scene already had: camera-relative differences in double on the CPU, small
floats to the GPU. See "Large-world coordinates" in CLAUDE.md.
