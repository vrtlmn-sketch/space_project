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
