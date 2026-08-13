# Working notes for this repo

Hard-won context. Most of these cost real time to discover.

## Build

`make -j4`. Header dependencies ARE tracked (`-MMD -MP`), so a `.h` change
rebuilds its dependents — `make clean` is no longer needed. Before that fix,
editing a header silently left stale objects linked against an old struct
layout, which did not fail to build, it **segfaulted at runtime**.

Build artefacts (`*.o`, `bin/`) are gitignored and untracked. They used to be
committed, which is what caused segfaults on other machines after a pull.

## Headless verification harness

```
./bin/blackholesim --compare [dx dy dz]      # renders to /tmp/cmp_*.png, then exits
```
Writes `/tmp/cmp_raster.png` (640x360), `/tmp/cmp_rt.png`, and a geodesic parity
pair at 256x144. Optional args offset the camera.

Env gates:
| var | effect |
|---|---|
| `RASTER_ONLY=1` | skip all RT captures |
| `SKIP_GEO=1` | skip the geodesic pair |
| `SKIP_RASTER=1` | skip the raster capture (RT isolation) |
| `RIM_DEBUG=1` | dump the dust density map to /tmp/dens_sharp.pgm |
| `STARDEBUG=1` | log starfield chunks visible / stars drawn |
| `STARDEBUG2=1` | log the top LOD allocations |
| `STARDEBUG3=1` | log draw counts for galaxies holding dynamic detail |
| `PROJECT=<path>` | load a specific project |
| `COMPARE_FRAMES=<n>` | capture at frame n instead of 3 (lets the LOD ladder settle) |
| `SAVE_PROJECT=<path>` | save the scene as a project at compare-exit (round-trip tests) |
| `CLOUD_DRAW_SORT=0` | keep list-order cloud draws (measure the far-to-near sort) |
| `UNIVERSE_TEST=<n>` | build a procedural universe at startup |
| `UNIVERSE_RADIUS=<Gly>`, `UNIVERSE_STARS=<n>` | override universe params |
| `UNIVERSE_DETAIL=<n>` | dynamic star density: star count up close, 0 = freeze LOD |
| `UNIVERSE_ROT=x,y,z` | rotate every generated galaxy (degrees) |
| `UNIVERSE_TEMP=<K>` | recolour every generated galaxy at creation |
| `UNIVERSE_PHYS=<n>` | enable physics on the first n galaxies (promote path) |
| `UNIVERSE_DEMOTE=1` | drop physics again at frame 2 (promote->demote round-trip) |
| `EDIT_TEMP=<K>` | set every cloud's temperature MID-SESSION at frame 2 (live-edit test) |
| `EDIT_TEMP_OUT=<K>` | same, but only clouds outside the universe |
| `CAM_ANCHOR=0` | freeze the camera anchor at 0 (pre-anchor absolute-double camera) |
| `UNIVERSE_CAM_DIST=<AU>` | park the camera this far from the galaxy (8 = inside the core) |
| `LOD_JUMP=<frac>` | view share above which a galaxy jumps straight to its target rung (default 0.15; 0 = old pure-doubling ladder) |
| `DUST_DEBUG=1` | log the dust/star-hash scale each cloud renders with |
| `BRING_TEST=1` | "Bring to me" the first cloud at frame 2 and log the framing it produced |
| `SCALE_DEBUG=1` | log the scene scale: focus distance, nearest surface, near plane |
| `NEAR_PIPE=<frac>` | view share above which a galaxy uses the real pipeline (default 0.10; 9 = always sample, 0.001 = never) |
| `STARDEBUG4=1` | far-field ledger per visible chunk: screen radius, samples its size is worth, built, drawn, flux correction |
| `FAR_FALLOFF=<g>` | override the project's Distance Falloff for an A/B (1 = exact flux, which makes anything past a few Gly invisible) |

**The harness uses `harness_imgui.ini`, not `imgui.ini`.** Viewport height feeds
the LOD star budget, and the live app rewrites `imgui.ini` as the user works —
so a shared file makes the same binary render differently depending on what was
happening in another window. That produced a 52.86–55.01 spread on one scene and
a phantom regression hunt. The harness now loads a frozen layout and never
writes it back; universe.json is stable across runs.

**universe.json's saved camera sees NO galaxies** — `STARDEBUG=1` reports "0
chunks visible" on every frame, so its mean luminance measures the rest of the
scene, not the universe. It is a fine determinism check and useless as a
far-field check. For anything about how galaxies look at distance, generate one
and park the camera: `UNIVERSE_TEST=200 UNIVERSE_STARS=20000
UNIVERSE_CAM_DIST=<AU>` (1e10 ≈ the real-pipeline switch, 1e11 ≈ a few pixels,
1e13 ≈ the deep field).

### Traps in the harness

- **Project settings OVERRIDE `settings.json`.** The loader fills missing keys
  with defaults, so editing `settings.json` for an A/B does nothing. Always edit
  `projects/<name>.json` (back it up and restore).
- **Noise floor is ~2% of pixels for milky_way.** Two identical runs differ by
  that much, so any effect smaller cannot be measured this way. Always run a
  same-settings control before believing a difference. UNIVERSE renders are
  fully deterministic (0-diff reruns) — byte comparison is valid there.
- **A blocked run leaves the PREVIOUS run's `/tmp/cmp_raster.png` in place.**
  Renders can block on vsync while the user's live app holds the display; a
  `cp` after that grabs stale data and fabricates a result. `rm -f` the capture
  before each run and check the `[IMG] Saved` line (this trap has produced two
  wrong diagnoses).
- **Pipes hide crashes.** `./bin/blackholesim ... 2>&1 | grep X` swallows the
  "Aborted (core dumped)" line and grep's exit code masks the failure. Run with
  `2>/tmp/log` and check `$?` when results look impossible.
- **Only ever `pkill -f 'blackholesim --compare'`.** A bare pkill kills the
  user's live session.

## Regression baseline

`RASTER_ONLY=1 ./bin/blackholesim --compare` on `projects/milky_way.json` gives a
raster mean luminance of **~46.68** (band 46.67–46.69 across runs; wider when
the user's live session holds the GPU). Check it after any shared-shader or
cloud-pipeline change.

**The number tracks the PROJECT FILE, not just the code.** History: 60.95 for a
long stretch, ~61.61 after the float->double position work, then 46.68 when the
user retuned milky_way.json (resolvedCut 0.6->0.0, unresolvedStrength
6.83->3.4, dustDetail 200->14000, softer bloom/edge light, RT as the main
view). Before calling a mean shift a regression, check
`git diff projects/milky_way.json` — a retune is not a bug.

## Large-world coordinates

The scene spans ~1 AU to ~1e15 AU, which no single float frame can hold.
The rules:
- World positions are **double** (`dvec3`): `RenderedObject::coordinates`,
  `CloudObject::position`, `StarChunk::center`.
- **The camera is anchor + local** (`gCamAnchor` + `cameraTranslate`; true
  camera position = anchor − translate). A single absolute double at 2.6e15 AU
  has an ULP of 0.5 AU, and 8 AU from a galaxy centre one pixel is 0.006 AU —
  so the smallest possible camera step was **80 pixels wide**. The anchor
  absorbs the large part (rebased in `UpdateInputs` when the local part exceeds
  1e6), leaving movement at full precision anywhere.
  **Every world→camera difference must be `(pos - gCamAnchor) + cameraTranslate`.**
  Anything that stores or restores a camera position (save/load, keyframes,
  record-camera swaps, RT dirty check) converts to the ABSOLUTE translate
  (`translate - anchor`) so a rebase can never shift it. Verify with
  `CAM_ANCHOR=0` — that reproduces the old quantised behaviour exactly.
  NOTE: object positions are still single doubles, so an object's own placement
  keeps ~0.5 AU granularity out there; per-object local frames (the hierarchy
  in docs/universe.md) are the next stage.
- Camera-relative differences are computed in **double on the CPU**, then handed
  to the GPU as small floats. Never subtract two large numbers in a shader.
- Cloud placement is a uniform (`uCloudOrigin` + `uCloudRot`), not per-particle
  CPU work. Positions upload once (static VBO) and only re-upload when
  `cloudGpuDirty` is set (physics readback, CPU integrate, snapshot restore).
  Starfield chunks apply the SAME rotation: chunk centres rotate about the
  cloud origin in double on the CPU, `uCloudRot` rotates the stars on the GPU.
- GPU physics runs in a **cloud-local frame**: particle SSBOs hold cloud-local
  floats; the octree/big bodies live in a shared sim frame bridged per dispatch
  by `uFrameOffset`, differenced in double. Float WORLD positions at 1e15 AU
  resolve to ~1e8 AU — physics there was silently garbage before this.
- The grid overlay is still float and shreds past ~1e15 AU (open task).

## GL traps

- **Never `glGetUniform*` into a small stack local.** It writes the WHOLE
  uniform at that location — a mat4 through a `float`/`GLint` pointer is a
  stack smash. This has now crashed the app twice (renderCloud's render-mode
  read-back, and a debug probe). Use the `cached*` mirror values instead.
- **Per-object uniform-location caches go stale.** `setupRender` fills them;
  anything that makes an object skip its first-draw `setupRender` (e.g. a
  builder setting `hasBeenRendered = true`) leaves locations at 0 and uploads
  write to the WRONG uniform. `uniformsCached` guards this — keep it true only
  when `setupRender` actually ran for the current program.
- Shader programs are shared via `s_programCache` (one per vert|frag pair).
  Never `glDeleteProgram` on re-setup, and remember uniforms are program-wide:
  the last upload before a draw wins.

## The look is the contract

The rendered image is the product. **Never change how anything looks without
being asked**, even to fix something that is technically wrong. Two examples
that were caught only because the user noticed: making the dust-density pass
draw chunked galaxies "correctly" brightened them ~25% up close, and giving
chunked galaxies a CPU sample silently pulled them into the RT accumulator.
Before touching a shared pass, capture a before/after on BOTH a near view and
an in-galaxy view (`UNIVERSE_CAM_DIST=8`) and diff them — a scene-average
mean can hide a large local change.

## Defaults live in ONE place

`SceneSettings` (projectSerializer.h) holds the default look. The JSON loader's
fallbacks now read `SceneSettings{}.field` instead of repeating a literal —
they used to be a second, silently diverging copy, so changing a struct default
did nothing for any project file that omitted the key. `Renderer`'s own members
carry the same values for the pre-project startup state; keep the two in sync.

The current defaults ARE the signed-off milky_way look (resolvedCut 0.0,
unresolvedStrength 3.4, unresolvedSize 45.55, bloom 0.045, edgeLight 0.45,
spikeStrength 1.56, spikeDecay 0.966, rtExposure 0.92, dustSkinContrast 6.5,
dustDetail 14000, farFalloff 0.08). Verified: stripping those keys from a
project renders the same image the explicit values do.

## ONE rendering model

A cloud is a cloud. Anything you can actually see — a hand-made formation, a
procedural cloud, a generated galaxy, inside a universe or outside one — is
drawn by the ORDINARY cloud pipeline: every point, standard passes. There is no
second look to keep in sync, and no brightness compensation to tune.

The chunked starfield is a SAMPLED STAND-IN, and only that: it exists because
3555 galaxies x 50000 stars is 178M points, so objects too small to resolve are
drawn with fewer. A galaxy switches to the real pipeline once it covers 10% of
view height (`NEAR_PIPE=<frac>` to move the line, `nearPromoted` on the cloud),
and back below half that. Under 10% it is a small smudge where sampling cannot
be told apart.

Do NOT try to make the stand-in match the real path by scaling brightness or
re-thresholding stars. That was tried (a `uSampleWeight` uniform) and it is the
wrong shape: brightness in this renderer is proportional to points drawn, so a
sampled object is a different look, not a coarser one. Either draw the object
properly or accept it is a distant smudge.

## Light falls off because objects get SMALLER — until the floors stop it

Nothing in this renderer has a 1/d² term. A sprite is a fixed screen size and a
fixed intensity; an object dims with distance only because it covers fewer
pixels and therefore draws fewer, smaller sprites. That works right up until the
floors: a sprite is at least a pixel, a visible chunk draws at least eight
points. Past that an object's light stops falling off ENTIRELY — a galaxy
0.001 px across still drew eight full-brightness stars, so the far field
outshone nearby stars and every galaxy sat at the same brightness no matter how
far away it was (measured: 333x the distance gave 11x less light).

`FarFieldDim(want, drawn)` (renderedObject.cpp) is the correction, and it is
subtractive ONLY — it never manufactures light, which is what separates it from
the rejected `uSampleWeight`:

- `want` is what an object's angular size is worth, at the same stars-per-pixel
  the draw budget uses. Both paths compute it the same way, so a hand-made cloud
  and a galaxy at the same distance dim by the same amount, and the
  chunked→real-pipeline switch is flux-continuous instead of a pop.
- Dividing by `drawn` is EXACT, so which LOD rung the ladder happens to be
  holding cannot change how bright an object is. That is what stops rungs from
  popping as you fly.
- Below the floor the object's own light is compressed rather than followed
  exactly. One fixed exposure spans 1 AU to 1e15 AU, so an exact falloff renders
  the deep field black. How hard to compress is a LOOK decision, so it is a
  setting — `farFalloff`, Stars → Star Haze → **Distance**, default 0.08, range
  0.05 (barely dims) to 1.0 (physically exact). `FAR_FALLOFF=<g>` overrides it
  headlessly. What it selects is effectively how deep you can see: at any
  setting a nearer galaxy always outlives a further one.

An earlier attempt did the opposite — `vHazeBoost` gave back the light a
size-capped haze lobe had lost, up to 48x. That holds an object's TOTAL light
constant as it shrinks, i.e. per-pixel brightness rising as d². Removed.

Never add a floor to a size or a count on a render path without asking what it
does to flux at the small end.

## Scene scale is ONE rule, for every kind of object

Camera speed (`focusDistance`) and the clip planes come from the nearest
SURFACE across everything in the scene — planets (distance − visual radius),
galaxies (per chunk, so being inside one reports its own size) and hand-made
clouds (distance − bounds radius) alike. It used to branch on what KINDS of
things existed: "nearest planet if any planet exists, else scan clouds". So a
single planet dropped into a universe drove the speed for 3555 galaxies, a
universe with no planets left the near plane at its 0.05 AU cap (swallowing any
true-scale planet), and milky_way only felt right because it happens to have
planets at AU scale. Verified with `SCALE_DEBUG=1`: planet 50 AU away in a
universe → focus 50 (planet wins); the same planet at 5e9 AU → 2.7e8 (field
wins); milky_way unchanged.

The one property that is still per-kind is deliberate: if the nearest thing is
a diffuse FIELD, travel may use field scale (`largestFieldRad * 0.02`), because
crossing a galaxy at "nearest star" speed takes tens of thousands of
keypresses. Next to a planet, the planet still wins.

## Never derive a per-scene render parameter from ONE object

`uDustInfluence` — the world scale the per-star colour/magnitude hash and the
dust-lane field are measured in — used to be a single global taken from
`clouds[0]` and applied to every cloud. Respawning a galaxy onto a 3 AU
formation file put that object at index 0, which set the scale to 0.106 for a
universe of 1e9 AU galaxies: `aLocal / uDustInfluence` reached 1e10, past
float's 24-bit mantissa, so every star in every galaxy hashed IDENTICALLY —
uniform colour, nothing above the resolved-star cut, just haze. It looked like
"the spread of the stars but not the stars".

Each cloud now derives its own scale (`RenderedObject::ownDustInfluence`) from
its own chunk extent or RMS radius. Any new shared render parameter must be
per-object the same way; one object must never be able to define how another
one is drawn.

## Cost traps

- **The RT path costs clouds × sample points, and a universe has THOUSANDS of
  clouds.** Giving chunked galaxies a CPU sample made every one of them feed
  the RT accumulator and the dust-light bake: 3555 galaxies × 2000 points =
  7.1M points, **5 GB and ~10 s/frame**, while 3554 of them projected to under
  0.03 pixels. `Renderer::Draw` now skips chunked clouds whose disc is under
  half a pixel at 1080p (a fixed reference, so a low RT live resolution cannot
  cull what the final render would show). Any new per-cloud CPU work must be
  screen-size gated the same way — measure with `/usr/bin/time -f %M` on
  `projects/universe.json`, which should stay near 700 MB / ~3 s.

## Conventions

- Commit messages: `fix:`, `add:`, `change:`, `misc:` + a short plain-language
  summary.
- Commits must appear authored solely by the configured git user. **No
  co-author, attribution or "generated by" trailers.**
- Never commit without being asked. Never push without being asked.
- Do not commit: `templates/dataset/` (643 MB source catalogue, gitignored),
  `templates/starfields/` (derived, regenerable via `tools/gaia_to_starfield.py`).

## Design docs

`docs/universe.md` — the universe feature: design, decisions, implementation
status, known bugs, and the agreed next step. Read it before touching universe
generation.
