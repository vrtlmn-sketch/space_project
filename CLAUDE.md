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

## Dark matter is an ANALYTIC halo — DM particles were tried and abandoned

A galaxy's dark matter is a **static analytic potential** `v_c(r)=vFlat·r/(r+rc)`,
applied as a force to the cloud's particles — invisible, cheap, stable. It is
gated per cloud by **`useDarkMatterHalo`** (the "Dark matter halo" toggle;
default true, UI on the spawn form and the cloud inspector, persisted in
`CloudData`). Off → `uHaloVFlat` sent as 0 → no halo (a plain, unbound cloud).

**Multi-cloud collisions/mergers** work through the **halo list**
(barnesHutForce.glsl, binding 5): each simulating cloud's halo is centred on its
**live centre of mass** (recomputed per sub-step in `SimulateSharedForward`) and
**every particle feels every cloud's halo**. So galaxy A's stars feel galaxy B's
halo and vice versa, both COMs fall together, and as they converge the halo
centres coincide — the halos **merge over time, by distance, with no extra
particles**. Single cloud → the list is skipped and the one uniform halo is
centred on the sim origin (`uFrameOffset==0`, so it is that cloud's own centre).

**Merge boost** (`haloMergeStrength`, scene setting + "Merge" slider by the time
step, default 1 = physical): the flat halo already pulls at any distance (force
~ vFlat²/d, 1/d falloff), so galaxies merge from anywhere given enough sim time
(t ~ d/vFlat). The boost scales the force from ANOTHER galaxy's halo so distant
ones fall together fast. It must NOT scale a galaxy's OWN halo (that would change
its rotation curve / blow up its disc), so each halo carries an **owner id**
(`GPUHalo.pad0` → shader `rc.y`) and the dispatch passes the cloud's own id
(`uSelfHaloOwner`); the shader applies the multiplier only when they differ.

**Barnes-Hut theta > ~1.0 makes a galaxy snap to a GRID on play.** A large
opening angle accepts even adjacent octree cells as single point masses, so
stars get pulled toward the cells' COMs and clump onto the tree's **cubic cell
lattice** — the "galaxy goes into a grid when you start simulating" bug. It is a
force artifact, not a render one, and it is worst for concentrated galaxies
(ellipticals). A single galaxy is `sim[0]` (frame offset 0), so it CANNOT be
float-precision quantisation — the only cubic-lattice source is theta. Default
is back to **0.5** (was briefly 1.5 for speed — that is what caused it), the
inspector slider caps at 1.0, and the dispatch **clamps uTheta to 1.0** so an
older project storing a large theta still can't grid.

**Why NOT real DM particles** (the rejected experiment; scaffolding still exists,
`ensureDarkMatter`/`stripDarkMatter`, but is **not called**): a spherical
population of heavy invisible particles in the tail of `cloudParticles` is the
physically "universal" model, and it half-worked — but at galaxy scale a few DM
particles integrated to **inf/NaN**, and because they live in the SHARED octree
that NaN poisoned the tree bounds and took **every** cloud's stars with it (the
"everything disappeared, only the nav view survived" bug — the HDR passes break
on NaN). Heavy particles + a large dt + one bad apple is too fragile. The
analytic halo cannot NaN. If revived, it needs: robust IC velocities, NaN
clamping in the integrator, and its own softening — not worth it over the
analytic halo, which already gives stable collisions.

**Kept from that work (general wins):**
- **Bounded octree depth** (`Octree::minLeafHalf_` = rootHalf/2^24). The old
  ABSOLUTE `1e-6` leaf floor recursed ~50 levels at galaxy scale wherever
  particles clustered — ballooning node count and overrunning the shader's
  64-deep traversal stack. Now depth ≤ 24 at any scale. A depth-capped
  MULTI-particle leaf has no children, so the shader applies a **childless node
  as one COM point** (else its mass would vanish).
- The DM plumbing (`dmParticleCount`, `starCount()`, the `[0,nStars)` draw split,
  `uDMDraw`) is inert while `dmParticleCount==0` — identical to pre-DM behaviour.

## Spiral arms TRAIL — spin is OPPOSITE to the winding

Every generator (`GenerateGalaxyStars` in universeGen.cpp, and both
`templates/formations/generate_milky_way*.py`) had the galaxy rotating the SAME
way its arms wind, which makes the arms LEAD — the winding grows the polar angle
with radius (CCW) and the placed velocity `(-vc·y/r, vc·x/r)` is also CCW. Real
galaxies trail, so the rotation must be OPPOSITE the winding: the velocity is now
`(+vc·y/r, -vc·x/r)` (CW). The halo force is centripetal, so reversing the spin
keeps every star in equilibrium — a pure look/motion fix, no mass retune.

- **The runtime generator fix corrects every universe and procedural galaxy for
  free** — they are generated from the recipe each run, nothing is baked.
- **Baked formations (`milky_way_*.json`) store velocities**, so they were flipped
  in place: negating the whole velocity vector reverses the spin (positions
  untouched, scatter just re-signed — statistically identical). The non-spiral
  formations (belts, clusters, nebula, disc) were left alone.
- **`projects/milky_way.json`** carries Sol + planets with the disc drift added.
  Only the DRIFT was reversed (`v -= 2·SolDrift`) so the solar system keeps
  orbiting the Sun prograde while the galaxy-scale drift flips; Sagittarius A*
  (at rest at the centre) is untouched. None of this changes a paused frame.

## A nebula is a VOLUME — its own object type, not a cloud

`ObjectType::Nebula` (physicsObject.h). The first attempt was a cloud with a
flag that changed what the passes drew: it inherited the sprite look and the
particle cost and still read as puffs. Points cannot reach the Orion look —
overlapping discs are the ceiling of "points + Nebula render mode" — so a
nebula is a RAY-MARCHED density field drawn on the object's sphere mesh, which
only bounds it. Clouds are untouched. Reuses the object machinery: name,
position, gizmo, keyframes, persistence (`"nebula"` recipe block on the
object; `visualRadius` is the volume radius). Mass 1e-9.

**Cost model — this is what makes it run on a potato.** The field is NOT
evaluated per pixel per step. `nebulaBake.glsl` (compute) evaluates it ONCE
per voxel into an RGBA16F N^3 texture (R emit, G absorb, B baked excitation
from the embedded stars, A a cell hash), rebaked only when the SHAPE key
changes (`SyncNebulaToRender`: seed, reach, detail, density, lights, extent,
resolution, source); palette/emission/dust/steps are read at march time. The
march (`nebulaFrag.glsl`) is one trilinear fetch per step, ~40 steps, and runs
in a REDUCED-RESOLUTION PASS (`Begin/DrawNebula/EndNebulaPass`, Quality &
Speed → Nebula Pass, default 1/2): the scene depth is blitted down (nearest,
what GL allows) so a planet in front still hides it, the volumes march into an
accumulation buffer, and one composite blends it back. The first version was
~130 hashes x 64 steps x every full-res pixel; this is ~1 fetch x 40 x 1/4.

- **Two different blends, and they must not be swapped.** Into the
  accumulation buffer: `glBlendFuncSeparate(ONE, SRC_ALPHA, ZERO, SRC_ALPHA)`
  — colour composites, transmittance MULTIPLIES. Onto the scene:
  `(ONE, SRC_ALPHA)` — dst = col + T·scene. Using the scene blend for the
  buffer gave alpha = 2T: the background came out TWICE as bright inside every
  nebula with zero emission. Found by forcing emission 0 and reading pixels;
  neither the march nor T was at fault.
- **ONE fragment per pixel, by DISTANCE — not culling, not depth.** The proxy
  sphere is double-sided, so both faces rasterise per pixel; the shader keeps
  one by classifying THIS fragment's own perspective-correct distance
  (`length(fragRel)/R`, radius units — a unit mismatch here silently discarded
  EVERYTHING) as the near or far box hit, and dropping the far one while a near
  one exists (`traw0 > 0`; inside the box the lone far fragment stays). The
  march runs on the analytic `[t0,t1]` box segment, so which face survives does
  not matter. This replaced two things that both broke: depth-write dedup
  (proxy-vs-proxy depths TIE in facet-shaped patches under this project's
  1e-7..1e10 z-range — the "flat wedges and a seam" close up) and winding culls
  (the sphere generator's winding punched triangle holes in the far hemisphere
  from a distance — the "part of the mesh missing" the user first saw). Depth
  test is therefore OFF in the nebula draw: a solid IN FRONT is not occluded yet
  (follow-up). On the sphere path `uWorld` = rotation + camera-relative
  translation and `uCamera` is zero, so the centre is `uWorld[3].xyz` (the first
  build solved for a sphere at the camera and drew nothing).
- **GLSL `pow(x, 2.0)` is NaN for x < 0** — the shell/fill used
  `pow((r-rs)/th, 2.0)`; square by hand. (Was not the wedge cause but is a real
  trap.)
- **Shape source**: the recipe (`nebula_common.glsl`: lumpy bubble shell open
  toward the cluster, ridged filaments in bundles, clumps with a radial
  falloff — dropping that falloff put dust at the envelope with nothing
  emitting there, a dark rind — mottled fill, fine turbulence, all inside a
  lumpy envelope that fades OUTSIDE the shell), or **a cloud**: its particles
  are splatted into a source R16F volume (`SplatNebulaSource`, trilinear, sqrt
  normalised) and the fine turbulence is multiplied on top. That is the
  customisation path — any formation or procedural shape becomes a nebula;
  "Attach to cloud" copies position/rotation/bounds; "Re-splat" after editing
  or simulating the cloud (it does NOT track physics per frame). Verified: the
  5k spiral renders as gas with species colouring.
- Bounds are an oriented BOX (`extent` per axis, x radius, each <= 1 so the
  sphere still encloses it) with a slab test in the shader — squash for discs
  and bars; rotate the object to orient. Species from baked excitation (OIII
  inside, H-alpha body, SII rim) in a palette (Natural / Hubble); dense UNLIT
  gas absorbs. Not in RT (`DrawPhysicsObject` returns early — RT would hit the
  bounding sphere as a solid); not an occluder for rim light.
- Not done: reflection term, other recipes (planetary, SNR, dark), universe
  placement, RT integration, per-frame tracking of a simulating source cloud,
  and depth against solids INSIDE the volume (the low-res edge of a planet in
  front is slightly soft).

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

`Raster Resolution` defaults to **Viewport** (`0,0` = follow the window).
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

## Lens experiments that FAILED (2026-08-23) — read before touching the lens again

One day, four redesigns, all reverted to the lens below; the work is in
`lens_onerule_experiment.patch` (applies on bb06d0e). What was tried and why
each looked worse than the committed lens — judged at full resolution, which is
the only way to judge it (the 360p harness captures hid every one of these):

- **Volumetric emissivity** (bent ray integrates `rho * I(dir)/C(dir)` from a
  density volume; exact identity for unbent rays). Every star became a RADIAL
  streak: a star's light is spread over its whole column, every bent ray
  crossing that column anywhere in depth picks up a piece, and the locus of
  those rays is a radial line. Tube over the shadow, horizontal cut.
- **Density-quartile depth sheets** per pixel: each star became 4 displaced
  dots; the far disc quantised into rings.
- **Depth slabs** (particles binned by camera distance, per-pixel mean depth):
  a pixel's column through an edge-on disc is hundreds of AU deep, so every
  slab collapses to one depth per pixel — concentric rings on the disc, onion
  layers on a galaxy. Finer slabs only make more rings.
- **Flat front + per-cloud plane for the far side** (= the committed lens's
  geometry, per cloud, all crossings summed): the far side is still the sprite
  HAZE stretched into a flat opaque sheet with a hard edge against the 3D
  sprite field, no photon-ring/gap structure in galaxy scenes, and with the
  extra passes (per-cloud slabs on three rungs, column pass, dust passes) it
  ran at a few fps. Worse than the committed lens on every axis.

What the committed lens gets right and every redesign lost: the far field is
sampled at the cloud's PLANE CROSSING (exact for a thin disc, a fair proxy
otherwise) and the front is drawn FLAT (physically right: light from matter in
front of the hole never passes the hole). What it still gets wrong, unsolved:
the bent zone is the sprite haze remapped — flat, opaque, brighter with
glow/spread, a hard edge against the unbent sprites; no secondary arch or
dark gap in galaxy scenes. That is the real problem; a depth model does not
touch it. Any future attempt must show a full-resolution side-by-side against
THIS lens and list what got worse before claiming anything.

Harness: the raster capture is 1600x900 by default (`CMP_W/CMP_H` override;
RT stays 640x360), so the old milky_way mean (29.16 at 360p) no longer applies
— byte-compare against a same-sitting control instead.

## The raster lens is FORWARD: each source is bent by its own position

**This replaced the two-pass image remap** (`lensRaster.glsl` and everything
around it) on 2026-09-02. Read the failure log above first — it is the record of
why the old one could not be fixed.

The old lens was BACKWARD: per pixel, bend a ray, then ask "what is out there?"
But the raster pipeline has no scene to ask — only rendered PICTURES of it — so
it had to guess the depth of whatever it sampled. Every scene assumption grew out
of that one gap: a dominant disc plane to find the crossing, a source sphere when
the plane missed, apex cubes because an image is only valid from its own vantage,
and a front/back particle split so the sampled image would not contain the
foreground. That split is the "seam in space", and the plane is why a top-down
view made the hole vanish, why a spherical cloud had nothing to key on, and why
two colliding clouds picked one plane and got the other wrong.

Turn it around. The renderer DOES have the scene — particles with exact
positions — so ask each SOURCE where its light lands. One rule, `lfPlace` in
`src/shaders/lens_forward.glsl`, applied to every particle, dust puff and star
alike:

```
beta = theta - alpha(b) * 0.5 * [ h + (Dl*delta - b^2)/hl ] / Ds
b = Dl*sin(theta),  h = sqrt(b^2+delta^2),  hl = sqrt(b^2+Dl^2),  Ds = Dl+delta
```

`delta` is the source's position along the camera->hole axis measured FROM THE
HOLE, positive behind it. That is the whole input. **No plane, no split, no
reach, no falloff, no dominant cloud** — the absence is the point, and it is why
a sphere, a disc seen face-on, two holes and empty space all behave the same.

- `delta -> +inf` gives `D_ls/D_s`, the textbook thin lens.
- `delta = 0` gives `b/(2 Dl)`: **half** the bend. A source level with the hole
  only gets the outgoing half of the encounter. The thin lens says ZERO here,
  which is exactly why it cannot draw an accretion disc at 3-20 rs.
- `delta -> -inf` gives 0. Light from in front never passes the hole, so it does
  not bend **and it covers the shadow**. That is physics, not a rule.

Measured, front to back through a hole, 20000 steps: the image moves 412
rs-angles with a largest single step of 0.014% of the travel. There is no jump
anywhere because there is no boundary anywhere.

**The dust needed no second system.** Dust is `uCloudPass == 3` in the same
`cloudVert.glsl` — same VBO, same draw. So it bends by the same rule for free,
and because the FRAGMENT shader runs the map's explicit direction per pixel
(`lfSourceCoord`, replacing `gl_PointCoord`), the FBM that carves a puff into a
wisp is evaluated in SOURCE space: a lane stretches into a real arc with its
internal structure stretched too, not a stretched disc with unstretched noise
painted inside. Optical depth is carried across unchanged, so bent dust is
neither brighter nor darker than unbent dust.

- `src/lensForward.cpp` is the C++ MIRROR of the shader — same formulas, same
  constants. It exists so the map can be PROVEN on the CPU (monotonicity, solver
  convergence, identity at rs=0, depth-continuity, cull rate) instead of debugged
  through a framebuffer. `LENS_LUT_TEST=1` prints the whole suite. **Change both
  together.**
- `alpha(b)` is a 1024-tap table built at first use by integrating the SAME null
  geodesic the ray tracer marches, so the raster ring and the geodesic ring agree
  by construction. Uniform in `q = -ln(1 - b_c/b)`, where alpha is nearly linear
  even at the photon sphere: worst interpolation error 8e-7 rad. Costs ~155 ms,
  once, and only when a hole is actually resolvable.
- Two images per hole: `gl_InstanceID` 0 is the direct image, 1..N the secondary
  of hole N-1. They occupy **disjoint** regions of the screen (the sign of beta
  selects the branch), which is what lets the multiplicative dust be drawn twice
  and still never darken a pixel twice.
- Cost on bh_ref: **+1.0 s** against +9.4 s for the old lens, and +3 MB against
  ~145 MB of FBOs. A scene with no resolvable hole measures no cost at all
  (milky_way 2.61 s vs 2.68 s lens-off), and milky_way stays byte-identical.

### Traps this cost real time to find

- **`gl_PointCoord`'s origin is UPPER-LEFT — its t axis runs DOWN, screen y runs
  UP.** The fragment map builds each sprite's footprint in screen space, so
  without a flip every footprint is mirrored top-to-bottom and **every arc curves
  the wrong way**: the arch over the hole is drawn as a hammock under it. One
  sign, and it is nearly invisible in the obvious test — a round sprite is
  unchanged by a mirror, and so is a symmetric Einstein ring, so the sphere scene
  passed while the disc was inverted. Measured before/after with a structure
  tensor: streak-vs-tangential alignment 0.62 -> 0.74 (the geodesic scores 0.77),
  and the best-fit arc centre moved from 140 px above the hole to the hole. Flip
  on the way in and back on the way out, so the profile's FBM still reads the
  same way as an unlensed sprite.
- **A LENS MUST NEVER DELETE MATTER.** Reporting slow solver convergence as "no
  image" culled 8.9% of the direct images — thousands of particles blinking out
  around the hole. The only honest absence is the betaMin test. The direct image
  now always stands.
- **A STRETCH BUDGET MUST TRUNCATE THE IMAGE, NEVER SHRINK THE SOURCE.** Spending
  it by drawing a smaller piece of each source keeps the footprint small and
  deletes matter: at the default budget a dust puff was drawn at 42% of its
  radius — under a fifth of its area — so every arc came out blue-white while the
  dust stayed behind in the unlensed band. The geodesic shows those rings FULL of
  dark red dust, which is what caught it. Lensing conserves surface brightness:
  an arc must read exactly as dark as the unlensed lane and may only cover more
  sky. Draw the whole source, clip the arc at the footprint, and fade the cut.
  Measured: arc dust share 14.0% -> 35.7%, at no cost in frame time.
- **Size the footprint from the MAP, not from the centre's Jacobian.** The
  magnification at a sprite's centre badly underestimates how hard the map
  compresses further out near the shadow; the whole square then lands inside the
  source disc, nothing is discarded, and the sprite draws as a hard-edged
  rectangle. Those are the blocks that appear around a big hole. Measure how much
  source the footprint actually reaches (the forward map, explicit, no solve) and
  grow it to cover.
- **Draw the hole's silhouette at the SHADOW radius (2.598 rs), not the horizon.**
  No image can land inside 2.598 rs, so a disc drawn at rs leaves an annulus of
  plain background between it and the ring — which reads as the ring being
  detached from a small dot. Measured on one scene: drawn disc 29 px, true shadow
  229 px. `RenderedObject::lensMeshScale` carries the factor.
- **The solver is BISECTION, not Newton.** This is a Born-profile model and in
  the deep near-field it can FOLD (measured on bh_disk: 15 of 600 source depths,
  worst 0.88 px). Where it folds there are several roots and each is a legitimate
  image, but a fold breaks the bracket invariant a Newton safeguard relies on and
  returned roots tens of pixels wrong. 24 halvings is 0.007 px worst, nothing
  culled. The direct image is additionally bracketed at `theta > beta`, which is
  exact (alpha*g > 0 always) and steps over the fold entirely.
- **Capture applies only to sources BEHIND the hole.** A source in front is
  reached before closest approach, so its image may sit anywhere, including
  inside the shadow disc — which is precisely how foreground matter covers the
  hole. An unconditional capture test deleted every particle directly in front of
  the hole and punched the foreground open.
- **`theta` must stay below pi/2.** Past it `b = Dl*sin(theta)` turns over, every
  evaluation returns the capture sentinel, and the bracket-doubling loop walks off
  to hundreds of radians. That is what threw particles near the hole's axis across
  the screen as radial streaks.
- **`inf * 0 = NaN`, and NaN fails every discard test.** A fragment exactly on the
  hole's screen position has `b = 0`, where `2rs/b` is infinite while the Born
  factor goes as `b^2`. The NaN then passed `dot(pc,pc) > 1.0` (all NaN
  comparisons are false) and the sprite drew its ENTIRE square: dark red boxes
  stacked over the hole. `b` is floored, and the test is written negated so a NaN
  discards.
- **A stretch budget must be spent by shrinking the SOURCE, never the footprint.**
  Sizing a sprite from a capped magnification while the fragment shader still maps
  pixels through the TRUE one leaves every pixel inside the source disc — the same
  filled squares, from a different cause. Both budgets (the stretch limit and the
  pixel cap) now feed one source-shrink, so the profile always runs out inside the
  footprint.
- **The early-out must not use the thin-lens Einstein angle.** It is proportional
  to `delta` and therefore ~0 for matter level with the hole — exactly where the
  Born term is large. A thin-lens gate skipped every particle of an accretion disc
  and left the disc unbent while distant matter moved. Gate on the actual
  displacement instead: one table fetch.
- **Finite-source limit.** `theta/beta` diverges as a source nears the axis, but a
  source of angular radius R can never be closer to the axis than R, so its
  stretch is bounded by `theta/R`. This is the physical reason lensed images are
  bright arcs rather than infinitely thin infinitely long ones.
- **`delta` needs DOUBLE.** Forming (axial distance - Dl) in float32 at 1e6 AU
  leaves nothing of the near-hole term. The object's offset from each hole
  (`uLfDelta0`) is differenced on the CPU in double and the shader only ever adds
  a small per-particle offset. Starfield chunks upload it again per CHUNK, because
  that is what the vertex shader offsets from.

### Performance: the lens is FILL-bound, not ALU-bound

Measured on `projects/bh_ref.json` (58 800-particle cloud + hole, 1200x675), per
frame, by the slope between `COMPARE_FRAMES=3` and `=13`:

| | ms/frame |
|---|---|
| lens off | 87 |
| lens on | ~400 |
| lens on, no sprite enlargement (`LF_MAXMU=1`) | 227 |
| lens on, enlarged sprites but the fragment map ABLATED to `gl_PointCoord` | 214 |

Read the last two together: with a trivial fragment shader the enlarged sprites
still cost 119 ms over baseline. **The cost is the number of fragments
rasterised, not the work per fragment**, and the base cloud path is already
fill-bound (87 ms for one instance of dust/haze sprites up to 160 px).

What that means for optimising it:
- Arithmetic savings are worth little. Removing an `atan`, two transcendentals
  and an unused derivative from the per-pixel map bought ~8%.
- A "skip the map where the sprite is barely distorted" fast path made it
  SLOWER (415 vs 392): the branch is divergent, so the GPU runs both sides.
- A cheap reject before the expensive map (an exact cosine band around the arc)
  also made it slower — the compare and its two varyings cost more than the
  rejects saved.
- What does work is drawing fewer/smaller fragments. **Max arc size**
  (`lensMaxSprite`, Lens UI) caps the largest lensed sprite as a fraction of
  viewport height: 0.35 -> 423 ms, 0.15 -> 244 ms. It truncates the longest arcs
  and never changes brightness. Cost is dominated by a handful of very large
  magnified sprites whose area grows as the square.
- The untried structural fix is to stop rasterising the square: a point sprite
  bounding a 5:1 arc is ~85% empty. Splitting a long arc into several smaller
  sprites along it (extra `gl_InstanceID` segments, no VAO change) or oriented
  quads would cut that area several-fold. Estimated ~30% more, at real risk to
  the approved look.

### Two traps that wasted real time measuring this

- **A shader that fails to compile draws NOTHING and looks FAST.** A uniform
  declared below its first use in `lens_forward.glsl` failed the whole cloud
  program; the frame time "improved" from 176 ms to 17 ms and the diff was
  537 731 pixels. The harness prints the compile error to STDERR and the runs
  were discarding it. Always `2>` a file and grep for `error` after touching a
  shader, and treat any large speed-up with a changed image as a broken build.
- **Never trust a single timing sample on this machine.** Chasing the dust fix
  produced readings of 861-939 ms/frame that looked like a 5x regression; a
  repeat showed lens-OFF also reading 899 ms once, which is impossible, and the
  true medians were 36 ms (off) and 182 ms (on). Take the median of three, and
  treat any reading that implies an impossible conclusion as an outlier before
  reporting it.
- **Timings are only comparable within one power state.** This machine on
  battery clocks the iGPU to 400 MHz of 1800 (`pp_dpm_sclk`) with the CPU
  governor at `powersave`; the same scene measured 176 ms/frame on AC and
  ~400 ms on battery. Check `/sys/class/drm/card*/device/pp_dpm_sclk` before
  comparing against a number written down earlier, and take control and
  candidate back to back. The first run after a reboot is also an outlier
  (958 ms against a 430 ms steady state) — discard it, as with the raster
  baseline.

### Test scenes for context-independence

`projects/lens_test_sphere.json` (a uniform SPHERE — no plane exists, so any
plane-based lens has nothing to key on), `lens_test_topdown.json` (bh_ref seen
from straight above the galaxy plane, the view where the old lens showed nothing
at all), `lens_test_twoholes.json`. Verified on the sphere: evacuated centre,
ring of tangential arcs, and pixels change ONLY within r=322 px — the map
converges to the identity on its own, with no fade to tune.

**The ground truth for GEOMETRY is still `SKIP_RASTER=1 PROJECT=... --compare`**
(`/tmp/cmp_geo.png`). Verified on bh_disk that sprite CENTRES form the same arch
the geodesic does (peak directly over the hole, dropping at the sides).

### Known gaps

- The drawn black disc is the HORIZON (rs); the true shadow is 2.598 rs. No image
  lands in the annulus between them, so it renders as empty sky — invisible on a
  dark background, wrong on a bright one.
- Meshes (planets), nebula volumes and the skybox are NOT bent yet. The map is
  written to drop into `defaultVert.glsl` the same way; the skybox wants the
  per-pixel backward form, which is exact for sources at infinity.
- bh_disk parks the camera 23.5 rs from the hole, deep in the strong field where
  the Born-profile factorisation is weakest. Galaxy-scale scenes are far-field,
  where it is exact.

### Accretion disk = a cloud

`templates/formations/accretion_disk_30k.json`: thin Keplerian disc scaled
to the 4.15e6 Msun hole (3-20 Rs, H/R 1.5%, denser inward, orbital
velocities). Gargantua's look is what the lens does to a ring of matter
around the hole — a galaxy alone can never produce it, there is no matter at
3-20 Rs to make the cross. Known remaining gap vs the geodesic: it resolves
several discrete wound ring echoes; the raster shows the primary image filled
and averages deeper windings.
