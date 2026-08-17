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
| `PLAY=1` | unpause at frame 1 so physics steps between captures; pair `COMPARE_FRAMES=n` with `n+1` to measure temporal flicker |

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
- **Reruns are byte-identical BACK-TO-BACK** (milky_way and UNIVERSE alike,
  verified with `cmp` on the same binary), so a byte compare against a control
  built from the committed source is the test — a mean can hide a large local
  change. But capture control and candidate within the same few minutes: a
  control from 20 minutes earlier once differed by ±1 along the two hardest
  edges in the frame (Saturn's outer ring boundary and one ringlet, 11k px, a
  tail to 41) while the galaxy and planet were untouched — and rebuilding that
  control's exact source reproduced the NEW image, not the old. Something in
  the environment moved (root cause not found; no wall-clock path reaches the
  camera or the rings). If a diff is a thin line on a hard edge and nothing
  else, rebuild the control from source before blaming the change.
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
- **If `--compare` blocks forever right after "Loaded N objects", run it with
  `env -u WAYLAND_DISPLAY`.** Under sway a window that receives no frame
  callbacks blocks in `eglSwapBuffers` (the process sits in `do_poll`); GLFW
  falls back to X11 through Xwayland, which presents on its own. This cost most
  of a day of "the harness is stuck" before it was understood. Renders are
  byte-identical either way.
- **Discard the FIRST run after a rebuild.** It lands outside the noise band
  (46.745 against a 46.67–46.69 baseline; 19.43 against 20.98 on a universe
  scene) and every run after it is stable. Seen twice, both times mistaken for a
  regression. Run twice, keep the second.

## Regression baseline

`RASTER_ONLY=1 ./bin/blackholesim --compare` on `projects/milky_way.json` gives a
raster mean luminance of **29.161** (29.150 seen later the same day — see the
hard-edge drift note in the harness traps). Back-to-back reruns are
byte-identical (`cmp`), so on this scene a byte compare against a control built
from the committed source, captured in the same sitting, is the right test, not
a mean. Check it after any shared-shader or cloud-pipeline change.

The same scene's other two renderers, for changes that touch the compute path:
**RT 26.255** (`/tmp/cmp_rt.png`) and **geodesic 25.498** (`/tmp/cmp_geo.png`).
Note this scene as committed HAS Saturn's rings in frame (camera parked on the
planet), so it is a valid control for ring work and a weak one for far-field
galaxy work — see the UNIVERSE_TEST recipe above for that.

**The number tracks the PROJECT FILE, not just the code.** History: 60.95 for a
long stretch, ~61.61 after the float->double position work, then 46.68 when the
user retuned milky_way.json (resolvedCut 0.6->0.0, unresolvedStrength
6.83->3.4, dustDetail 200->14000, softer bloom/edge light, RT as the main
view), then 48.63 when the empty sky stopped being pure black (a near-black
blue background adds a floor to every pixel: `lit` jumps 51.6% -> 88.9% while
`sat` is unchanged). Before calling a mean shift a regression, check
`git diff projects/milky_way.json` — a retune is not a bug. Latest step:
48.63 -> 29.16 (RT 21.65 -> 26.26, geodesic 29.50 -> 25.50) when the user parked
the camera on Saturn and rings landed in all renderers (project change at
524e55b + the ring ports), then the two rim-light fixes (planets finally
populate `rimOccluders`; snap/record passes keep their own rim lists — every
harness raster capture before that ran the dust-density pass TWICE).

The same applies to `projects/universe.json`, which the user re-saves from the
live app: its camera, keyframes and look settings change under you. It is not a
fixed baseline.

## Pointing the camera from a script

`camRotation` / `camPitch` / `camRoll` are **RADIANS**, not degrees, and they
are unwrapped for continuity — a saved -18.04 is a camera that was spun round
several times, not 18 of anything. The camera POSITION is `camX/Y/Z = -P`
(position = anchor - translate, and the anchor is 0 in a saved project).

`camMatrix` is the view rotation Rx(pitch)·Ry(yaw), row-major, and the world
direction the camera looks is **minus its third ROW**:

```
forward = ( sin y,  -cos y · sin p,  -cos y · cos p )
```

Inverting that has TWO solutions (`cos y = ±sqrt(fy²+fz²)`); they differ by the
camera being upside down, so take the one with the smaller `|pitch|`. Both
branches reduce to `(0,0,-1)` at yaw = pitch = 0, so a test aimed straight down
-Z will not catch a wrong derivation — always rebuild the forward vector from
the angles you solved and compare it against the one you wanted.

## Keyframes are ONE evaluator, and "smooth 0" means the CHORD, not zero

Every keyframe lane — freecam, spawned cameras, non-simulated planets and
clouds — is played back by `Renderer::EvalKeyframes` (renderer.cpp). It used to
be four separate copies of the bracketing-search-and-lerp, so a change to how
playback feels had to be made four times; there is now exactly one, and the
serializer likewise has ONE writer/reader (`keyframeToJson`/`jsonToKeyframe`)
for all five save sites — two of them used to inline their own copies, which is
how a new per-key field would silently have gone missing from a lane.

The interpolant is cubic Hermite through the keys. Each key carries `smooth`
in [0,1]. **The diamond IS the control**: drag it sideways to retime, drag it
up/down to set smoothness (up = smoother, 90 px for the full range, relative
to the grab so the value never jumps), click to jump. The gesture locks to
whichever axis wins past a 4 px dead zone. There is deliberately NO selection
state — the first version had click-to-select + wheel/slider, and a mode is
what makes every adjustment cost a click. While a drag is live the lane turns
into a curve editor: the path's value against time is drawn THROUGH the
diamonds (which move onto the curve for the duration), so a sharp key reads as
a kink and a smooth one as a bend, and the readout rides above the dragged
diamond. Value = position along the keys' principal axis (power iteration on
the keys' covariance); a lane whose keys share a position falls back to the
angle/zoom with the widest swing. Held ends outside [first, last] are drawn
dim. It lives on the foreground draw list so neighbouring lanes' text cannot
cover it, and it is only visible during the drag. **The drag is applied
BEFORE the curve is sampled** — sampled after the diamond loop it would trail
the mouse by a frame. Its tangent for a segment blends between the
segment's **chord** `(P1 - P0)` at 0 and the Catmull-Rom estimate through its
neighbours at 1, scaled by segment length so a short segment next to a long
one does not overshoot.

- **The 0 end is the chord, NOT a zero tangent.** A Hermite segment whose two
  tangents both equal the chord IS the straight line at constant speed, so a
  project whose keys all load at 0 plays back bit-for-bit as the old lerp did
  (checked to 2e-6 against the old code, all channels, every frame). A zero
  tangent does something else entirely: an ease-in/ease-out S-curve — same
  endpoints, different timing — which was the first version and is why the
  standalone check exists. If a "sharp" key ever looks like it decelerates,
  that is what went wrong.
- **Loaded keys without the field default to 0; NEW keys default to 1.** Old
  projects are untouched until you touch a key; new paths are smooth because
  that is what smoothing is for. `jsonToKeyframe` and `CameraKeyframe{}` hold
  the two defaults respectively — do not unify them.
- Re-capturing on an existing key **keeps its smoothness** (all three insert
  paths copy it across before overwriting).
- The angles ride the same interpolant as position with no shortest-path
  fixups. That is safe ONLY because `syncEulerFromMatrix` unwraps them for
  continuity — a saved -18.04 is a camera that spun round several times.
- **Smoothing an END key changes nothing visible** — the Catmull-Rom estimate
  at an endpoint has only one neighbour, so it collapses to the chord and the
  blend is chord-to-chord whatever the value. The graph says "end key: no bend
  yet"; the value is kept and takes effect once a key exists past it.
- The bundled font (DejaVuSansMono, default glyph range) has no arrow glyphs
  — UTF-8 arrows in a tooltip render as `?`. Use words.

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
dustDetail 14000, farFalloff 0.08, background 0.005/0.005/0.030 at level 1.2).
Verified: stripping those keys from a project renders the same image the
explicit values do.

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

## Time step, playback, and the multi-scale regimes

`simSpeed` is the TIME STEP: dt per recorded frame = `kDtYears (0.0005 yr) x
simSpeed`, log range 0.01..1e11 (minutes to tens of Myr). `playbackSpeed` is
frames per tick / 5, INDEPENDENT of dt (it used to hold world-time-per-tick
constant across sim speeds — meaningless across ten orders of dt, and it forced
Play off its own slider). Consequence for old projects: changing the step now
also changes wall speed proportionally; the Play tooltip shows the world rate.
**Auto** sets dt = T / autoStepsPerOrbit for the selected object/cloud, else
the largest simulated cloud, else the fastest body. Numbers that motivated all
of this: milky_way_5k orbits in ~113 yr; milky_way_real_20k in 5.6e7 yr — at the
old max (Sim 10x, Play 10x) one galactic orbit took **43 days** of wall time.

One dt cannot resolve a moon and a galaxy at once, so `dynamics.h` gives every
simulated body a REGIME from its own dynamical time T against dt:
- **Numeric** (dt <= T/200): integrated as always.
- **Analytic** (otherwise): the orbit cannot be integrated at this dt (Euler at
  a few steps per orbit does not get it wrong, it EJECTS it — measured: a
  planet at 1 AU flung to 2e10 AU in 30 frames at a 5e4 yr step), so the body
  rides its PARENT on an exact two-body Kepler orbit (universal variables,
  handles hyperbolic too; verified to 1e-9 over 1e5 orbits). Same test with
  the regimes: 1.0000 AU after 1.5 Myr.
- Clouds whose internal T is unresolved are **rigid**: particles frozen (they
  are phase-mixed — thousands of internal orbits per frame), the centre of mass
  on a Kepler orbit around its parent, or coasting.
- Parent = the HEAVIER attractor whose pull is largest (never a lighter one —
  that gave Sun-orbits-Jupiter cycles), with 1.5x hysteresis. Cloud parents
  are its centre of mass. Regime and parent are derived every frame, never
  saved.
- **A numeric body skips gravity from its analytic satellites.** A planet
  whose orbit is unresolved delivers an impulse per step that is garbage
  (dt >> its period) and would fling its star; its mass rides with the parent.
- **Objects update parents-first** (`objectOrder` from UpdateSceneDynamics), so
  an analytic satellite reads its parent's position for THIS tick — from the
  parent's recorded frames at each sub-step, so it never lags by one step
  (at galactic dt a one-step lag is millions of AU).
- **`clearRecording` drops the regime** (epoch, elapsed): the analytic epoch was
  taken from a state that a reset/Apply erased; re-enter from the new state or
  the body jumps.
- Softening is per cloud now: `max(0.001, (0.25 x RMS / N^(1/3))^2)`. The old
  constant 0.001 AU^2 was spacing/4 for a 3 AU formation and 1e16x too small
  for a real galaxy (spacing 1.4e7 AU). The floor keeps small clouds
  bit-identical (verified); the CPU O(n^2) path still has no softening.
- The cloud inspector shows T, steps per orbit, regime, and v/v_circ with a
  **Virialize masses** button (explicit, clears the recording). Neither
  bundled formation is self-consistent: the 5k has 400x too much gravity for
  its speeds and collapses; milky_way_real_20k has 2e4 Msun for a 6 kly disc
  and is unbound 1000x — under simulation it flies apart until virialized.
- Harness: `DYN_DEBUG=1` logs regime transitions. `PLAY=1 COMPARE_FRAMES=n
  SAVE_PROJECT=...` and reading positions back is how the ejection test above
  was measured; when comparing physics across builds match FRAMES PER TICK, not
  Play values (Play's meaning changed).

## A galaxy is held together by its HALO, not by its stars or a black hole

The generator gives every galaxy a real flat rotation curve (46 AU/yr = 220
km/s) but each star 1 Msun, so a 50k galaxy weighs 5e4 Msun and holding
220 km/s at 6 kly needs ~1.7e10 inside that radius: unbound by ~1e6 in mass,
so simulated stars simply drift away from the centre. Sgr A*'s 4e6 Msun does
not help (its sphere of influence is ~3 pc), and a giant central mass would
give a Keplerian curve (v ~ 1/sqrt r), not the flat one the stars were placed
on. Real galaxies get the flat curve from dark matter, and galaxy simulators
model that as a STATIC ANALYTIC POTENTIAL — a declared field, not sampled mass.

Per cloud: `haloVFlat`, `haloRCore` (RenderedObject; persisted in CloudData,
absent keys = fitted at load). v_c(r) = vFlat·r/(r+rc); the force is the
centripetal acceleration `a = -v_c(r)^2/r · r̂` toward the cloud's LOCAL
origin (spherical, so orientation is irrelevant). Every star the generator
placed at v_c(r) is then in equilibrium BY CONSTRUCTION — no mass fiddling.
- **The SAME term, from the SAME two numbers, in every force path**: the BH
  compute shader (`uHaloVFlat/uHaloRCore`, after the tree walk), the CPU
  integrator (`UpdateCloudPhysics`), and the big bodies (`cloudSources` carry
  `haloVFlat/haloRCore/haloCenter`, applied in `PhysicsObject::Update`). Add a
  fourth force path and it must get the term too, or the paths disagree.
- Generated galaxies take the recipe's curve exactly (`materializeGalaxy`).
  Formations fit it from their own tangential speeds about the angular-
  momentum axis (`fitHaloFromVelocities`: binned medians, least squares over
  a log grid of rc). milky_way_real_20k fits to 217 km/s.
- The regime picker (dynamics.cpp) uses the halo's ENCLOSED mass
  `v_c(d)^2 d / G` as part of a cloud attractor's effective mass, so a star's
  parent and analytic mu see the galaxy the star actually feels.
- `virialRatio` counts the halo, so a fitted cloud reads "balanced" and
  Virialize is not offered.
- **The CPU cloud path has NO star-on-star gravity** (only big bodies + halo);
  the halo is its only internal binding force.
- **milky_way.json**: cloud switched to GPU Barnes-Hut (the file said CPU; the
  spawn default was already GPU), halo keys written, and Sol given its
  orbital velocity — 45.1 AU/yr tangential in the disc's rotation sense (from
  the formation's angular-momentum axis), added to EVERY planet too so the
  solar system keeps its relative motion. Sgr A* stays at rest: it IS the
  centre. None of this changes a paused frame, so the raster baseline holds.
  A star at rest 26 kly out was "moving consistently" because it was
  free-falling toward the black hole with no tangential speed.

## Clouds draw in TWO phases: every cloud's light, then every cloud's dust

Haze, gas and cores are additive (`GL_ONE, GL_ONE`); dust is multiplicative
(`GL_ZERO, GL_SRC_COLOR`). Drawn as "each cloud: its light, then its dust", the
image depended on the far-to-near cloud sort — and two COLLIDING clouds swap
that order in one frame when the camera crosses their equidistance plane, so a
whole cloud's dust started (or stopped) darkening the other's light: "a red
blob appears in one frame, and goes back if I fly back". Reproduced in the
harness with two overlapping 5k clouds: sorted vs `CLOUD_DRAW_SORT=0` at ONE
camera differed by 633 px over 96/255 in blob shapes; now 0.

`Renderer::Draw` on a cloud draws the LIGHT phase only and marks
`cloudLightDrawn`; `Renderer::DrawCloudDust` (a second loop over the clouds,
AFTER all their Draws) draws the dust and clears the mark, re-sending the
per-object dust uniforms because another cloud's Draw overwrote them (shared
program). Sum then product — cloud order no longer matters at all, so the sort
is now cosmetic. The look is unchanged for a single cloud (same draw calls, same
order; byte-identical against a back-to-back control) and changes only where
clouds overlap on screen: a far cloud's dust now also darkens a near cloud's
light there. Accepted deliberately — distant galaxies are never that close
unless they are colliding, and far ones are LOD stand-ins anyway.

- **Every raster site that loops Draw over the clouds MUST also loop
  DrawCloudDust** (main.cpp: primary, record raster, snap, compare, PiP). A site
  that forgets it renders no dust — visibly wrong, but at least not silently:
  the flag means a skipped cloud can never grow dust from nothing. RT sites need
  nothing (Draw's raster branch does not run under `rayTracerView`).
- The nav path ignores the phase (draws once on Light; Dust is a no-op).
- `CloudObject::Update` calls Draw itself; the primary loop's dust phase is a
  second loop over `cloudDrawOrder` after the Update loop.

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

## Planetary rings are a LIST, and RASTER ONLY

Defaults describe a Saturn-like system in ONE ring, so "Add Ring" already looks
like a ring. A planet holds `std::vector<PlanetRing> rings` (physicsObject.h), capped at
`kMaxPlanetRings` = 8. Every parameter is per-ring including the plane, so two
rings need not be coplanar. Radii, thickness and centre offset are in PLANET
RADII, so a ring keeps its proportions when `visualRadius` is edited or size
exaggeration is switched on.

Rings render in the rasterizer and in **all six** compute shaders.
`RayTracerObject` is a fixed 96-byte struct with every spare `.w` lane already
carrying cloud params, so rings ride their own SSBO on **binding 6** (`RtRing`,
10x vec4). The look comes from `rings_common.glsl` for both views and
`rings_rt.glsl` holds the RT-only plumbing, so there is exactly ONE definition
of how a ring looks.

In the geodesic shaders rings are **lensed**: `ringsAccumulateSegment` runs on
each marched sub-segment with the segment's own solid-hit distance as its
clip, so rings bend with everything else and a ring behind a surface cannot
show through it. Verified by parking a black hole beside Saturn — the ansae
warp exactly as the planet does.

### Traps the RT port hit

- **Planets are drawn by `DrawPhysicsObject`, NOT `Renderer::Draw`.** Both have
  a `meshType == sphere` RT branch that pushes to `rayTracedObjects`, and the
  one in `Draw` never sees a planet. Anything new that must accompany a planet
  into the RT buffers goes in `DrawPhysicsObject`. **This trap has now been hit
  twice** — the rim-light occluder push also lived only in `Draw`, so
  `rimOccluders` was permanently empty, `uOccCount` permanently 0, and the
  tonemap's "no dust glow on top of a planet" loop never executed once. Every
  `renderer.Draw()` call site passes a CLOUD; that sphere branch is unreachable.
  A push that belongs to a planet goes in BOTH, via one shared helper
  (`AddRimOccluder`) — a second copy is what let this rot unnoticed.
  Consequence worth remembering: **code on a path that never runs is not
  reviewed by reality.** That dead push also stored an absolute world position
  as `float` (quantises to ~1e8 AU in a universe, so the disc lands nowhere
  near the planet) and its consumer multiplied `sphereRadius()` by
  `activeSizeExag()` a second time, though the mesh is already generated at
  `visualRadius * activeSizeExag()` — with exaggeration on, every disc would
  have been 750x too big and blanked the rim across the whole frame.
- **`CaptureImage` uploads `rtLastObjects`, the SNAPSHOT, not the live list**
  (so does the compare harness's RT capture, which goes through it). A new SSBO
  must upload the matching snapshot, and its COUNT uniform has to come from the
  same list — mixing a snapshot buffer with a live count silently renders
  nothing.
- **`rtDirty` will not notice a ring edit.** RT re-renders only when something
  changed, and the check memcmps `rayTracedObjects` — which a ring edit leaves
  byte-identical, because rings are in their own buffer. `DispatchRaytracer`
  memcmps `rtRings` against `rtLastRings` for exactly this reason. Any future
  per-frame RT buffer needs the same treatment or its sliders will do nothing.
- **A ring's owner index is per-object-LIST.** Doppler and plain keep separate
  object lists and clouds push different counts into each, so the two cannot
  share one ring list.
- **Compute shaders have no `fwidth`.** The profile's filter width comes from
  the projection instead: `t * 2 / (uProj[1][1] * uResolution.y)`. That is exact
  rather than a screen-space difference, so it is the better source anyway.
- **A geodesic ray does NOT end when the marching loop does.** Rays that leave
  the marched region finish as a straight line from `pos`/`vel` (`finalPos`/
  `finalVel` in the acyclic pair), and that escape block runs its own solid-hit
  scan. Accumulating rings only inside the loop rendered the ring that crossed
  the planet and dropped both ansae — anything volumetric needs a second call on
  the escape line, clipped at `escTMin`.
- **The two acyclic shaders carry a DIFFERENT `shadePlanet`** —
  `(ro, hitPos, normal, baseColor)`, the nav-style LDR model, with no clouds and
  no night lights. A patch written against the other four will not apply; the
  ring shadow rides their `attenuation` term instead of a radiance vector.
- Under extreme lensing the geodesic shaders draw hard-edged black wedges across
  a planet. That is **pre-existing** integrator stepping, not rings — it renders
  identically with rings switched off.

- **ONE proxy mesh per planet.** `GenerateRingMesh` makes a unit annulus
  parameterised by (radial 0..1, azimuth); `ringVert` builds the real geometry
  — elliptical radius, centre offset, warp — from uniforms. A second ring costs
  a draw call, not memory. Measured: no change to the 700 MB / ~3 s cost.
- **`uRingCount` must be uploaded on EVERY `renderMesh`, including 0.** Shader
  programs are shared via `s_programCache`, so a planet with rings otherwise
  leaves its count set and stripes the next planet drawn with the same program.
- **The shadow and the ring read the SAME `ringDensity`**, so a shadow band
  always lines up with the ringlet that cast it. Warp is ignored on the shadow
  side (it uses the flat mean plane) — that is the one deliberate divergence.
- **`opacity` is optical depth, and the radial profile MULTIPLIES it.** Real
  values: Saturn's B ring ~1, its C ring ~0.1. The first version shipped a 0–3
  slider that `verticalFalloff` then squared, so a setting of 2.76 gave
  `tau = density x 11` and every gap and ringlet clipped to alpha 0.99 — a flat
  white sheet. The structure was being computed and thrown away. **If a ring
  looks like a solid disc, it is saturated, not under-detailed.**
- **A ring is THREE frequencies, not one noise function.** `ringDensity` =
  broad zones (dense B vs thin C) x hard-edged gaps (Cassini, Encke) x fine
  ringlets. The first version was five octaves of smooth value noise, which
  cannot produce a hard edge at any setting — so a Cassini division needed a
  SECOND ring stacked on the first. Generating gaps inside one ring is what
  makes a single ring a whole system.
- **Fine detail is pixel-filtered, not just drawn.** `ringDensity` takes a
  filter width (`fwidth(vU)` in ringFrag; world-pixel / ring-width on the shadow
  side) and drops octaves finer than a pixel, and no gap edge is allowed below
  one pixel. Without it a ring crawls with moire the moment it is small on
  screen. In `defaultFrag` that derivative is taken OUTSIDE the ring loop —
  derivatives inside divergent control flow are undefined.
- Saturn in `projects/milky_way.json` is the reference ring: ONE ring, near
  defaults, framed by the saved camera. **The backdrop matters** — pointed into
  the galactic plane the rings are dimmer than the starfield behind them and
  read as washed-out grey; against empty sky the same rings look right. Frame
  the shot off the plane.
- `Snap` renders through `CaptureImage()` (the raytracer) unless the Cinematic
  View is on AND set to Performant, in which case it renders the raster path.
  Both carry rings, in every render method.
- **"Vertical Falloff" is an exponent, not a literal vertical density profile.**
  For a ray fully crossing a slab, any normalised vertical profile integrates to
  the same column, so the parameter would do nothing if taken literally. What it
  actually selects is how fast the ring thickens toward edge-on:
  `pow(min(1/cos, outer/thickness), falloff)` — 0 = no thickening, 1 = exact,
  >1 = exaggerated.
- Draw order is planet → atmosphere → rings, all depth-TESTED and never
  depth-WRITTEN. So the planet correctly hides the far half of a ring, but a
  ring in front of an atmosphere limb draws over it. Only visible on a planet
  that has both.

## Render resolution is a HEIGHT plus the target's aspect

Both resolution settings (Quality & Speed → **RT Resolution**, **Raster
Resolution**) store a w/h pair, but only the **height** is used as the quality
knob — the width is recomputed from the actual target's aspect at render time. A
fixed 16:9 image blitted into a non-16:9 window shifted everything sideways
against the mouse and the overlays, so a preset must never dictate the aspect.
`Renderer::RasterRenderSize(w, h, outW, outH)` is the one place that rule lives
for the raster side; the raytracer's two dispatch sites do the same inline.

`Raster Resolution` defaults to **1080p** (`0,0` = Viewport = follow the window).
It drives the live Performant view, its Snap, and the Performant recording
override in `StartRecording`, so the preview and the output cannot disagree.

- **`CineBeginIfActive` is the ONE funnel** for every live raster-cinematic pass
  — editor viewport, fullscreen and PiP all go through it, so the resolution
  rule only has to be applied there. Note this means the PiP renders at the full
  chosen resolution too, which is deliberate (its look must match a Snap) and is
  the main cost of a high setting.
- `cineSSAA` MULTIPLIES the chosen resolution rather than replacing it, and
  `currentPixelScale` is `bufferHeight / targetHeight` so sprites keep their
  apparent size however the two combine. 4K at 2x SSAA is a 7680x4320 RGBA16F
  plus depth, about 400 MB, reallocated whenever the preset changes.
- **The post chain is measured against the RENDER size, not the display.**
  `CineResolveIfActive` passes `cinePostW/H` (the raster render size) to
  `RunPostProcess`, which halves it for the bloom/spike/dust-density targets. Pass
  the display size instead and a 1080p preview blooms differently from the 1080p
  snap it is previewing. At Viewport the two are equal, so the old behaviour is
  reproduced exactly.
- **The `--compare` harness is unaffected** — its raster capture calls
  `BeginRecordRaster(W, H)` with explicit sizes and `currentPixelScale = 1`, so
  the regression baseline stays valid whatever the setting is. Verified
  byte-identical across the change.

## The empty sky is ONE value, for both views

`backgroundColor` x `backgroundLevel` (Background & Grid → Empty Sky) is what
"nothing" looks like. The rasterizer clears every SCENE target to it
(`Renderer::ClearSceneTarget` — cinematic HDR buffer, nav viewport, PiP, record
FBO) and the raytracer returns it where a ray escapes (`uBackground`, replacing
a hardcoded `vec3(0.0)` in all 6 compute shaders). Verified: a non-black
background renders the SAME pixel value in both views.

Post-process ping-pong targets (bloom, spike, dust density) are cleared to black
directly and must stay that way — they are accumulators, not sky.

Before this there were two hardcoded answers: the nav viewport cleared to 0.05
grey and RT returned pure black, so switching views changed the background.
The default is a near-black blue (0.005, 0.005, 0.030 at level 1.2), picked by
the user in the live app; it renders as [1, 1, 13] after the tonemap.

A spheremap still overrides the background when one is loaded. Its UI was
removed on request; the loader, the settings keys and the sampling code are all
still there, so a project with `spheremapEnabled: true` works unchanged.

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

## A star's identity is FROZEN — a hash is not noise

Every per-star attribute in the raster cloud path — colour, magnitude, whether
it resolves as a core or melts into haze, whether it is hot enough to seed gas,
and its dust-lane sample — is `hash13(position / dustScale + 17)`. A hash is
not noise: `hash13` has a slope of ~30–60 per unit, so a move of 0.1 % of the
galaxy radius wraps it completely. Hashing the LIVE position therefore
re-rolled every moving star on every physics step: that was the flicker on a
simulating galaxy, and why moving the CAMERA never flickered (positions did not
change). Measured on the 5k formation at simSpeed 0.02, where per-frame motion
is negligible: the old build still changed 46k pixels/frame (mean |Δ| 5.4) and
the frame's total brightness wobbled; now 0.4, seventeen pixels over 32/255,
none over 96.

The fix is `aHashPos` (attribute 3, `hashVbo`) + `uHashScale`: a copy of the
position and the dust scale captured when the particle SET is defined
(`LoadCloudFromFormation`, `GenerateMeshCloud`, promote via
`releaseCloudGlObjects`), flagged by `hashDirty`, and never refreshed by a
physics readback or a timeline restore. For a cloud that has not moved the
frozen values equal the live ones, so still images are bit-identical (verified
by `cmp` against a control build). Dust is sampled at the frozen position too,
so a star's dust rides with it instead of the star flowing through a lane field
pinned to the galaxy frame — the whole galaxy moves as one thing.

- **The chunk path (universe galaxies) deliberately still hashes on `aLocal`.**
  Its stars never move, and a star must keep its colour across LOD rungs, which
  only a position hash gives. `frozen = (uChunkExtent <= 0.0)` in cloudVert.
- **Do NOT switch the particle path to `gl_VertexID`.** Zero-cost, but it would
  reshuffle every existing cloud's per-star colours (same population, different
  individuals) and make promote/demote pop between the two paths. The frozen
  position keeps both consistent. (Particle ORDER is stable through the GPU
  readback — index i is particle i throughout — so ID would be a valid key,
  just a look-changing one.)
- **The stale comment that started it**: cloudVert used to say position hashing
  was for parity with the raytracer's `hash13`. `galaxy_common.glsl` shows the RT
  hashes by INDEX (`gxStarMag(idx)`) with a different hash — that parity was
  gone long ago. Read the other side before trusting a "for parity with X".
- **`uHashScale` must be uploaded per object in BOTH `renderCloud` and
  `renderCloudDustDensity`** — programs are shared, and the density pass runs
  later for each rim cloud.
- **Rim factors bake when positions changed, not on a clock.**
  `updateCloudRimFactors` used to run every 30 draws (hold, then jump, twice a
  second through a physics run — the "dust jumps around"), on a nearest-cell
  48³ grid with a hard f³. Now it triggers on `cloudGpuDirty` (a static cloud
  bakes once) and blends each star toward the new value (`kRimBlend` = 1/6) so a
  cell crossing fades. TRAP: the first bake runs BEFORE `setupRender` has
  created `rimVbo`, so it has nowhere to upload — it must not count as done
  (`rimFactors.clear()` on the no-VBO path) or the factors are never uploaded
  and a static cloud's rim light silently reads as zero. The old code survived
  that only because its counter quirk baked twice. Found as 102 dark pixels on
  the image's left edge in a byte compare — a mean would never have shown it.
- Not touched, noted: the RT dust-light bake (`updateCloudDustLight`) keys its
  placement stage on the live `dustInfluence`, so under physics it re-bakes the
  expensive stage every frame. Perf, not flicker; RT-only.

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
