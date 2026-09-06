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
| `COMPARE_FRAMES=<n>` | capture at frame n instead of 90 (90 is past the settling event — see the traps below) |
| `SAVE_PROJECT=<path>` | save the scene as a project at compare-exit (round-trip tests) |
| `CLOUD_DRAW_SORT=0` | keep list-order cloud draws (measure the far-to-near sort) |
| `UNIVERSE_TEST=<n>` | build a procedural universe at startup |
| `UNIVERSE_RADIUS=<Gly>`, `UNIVERSE_STARS=<n>` | override universe params |
| `UNIVERSE_DETAIL=<n>` | dynamic star density: star count up close, 0 = freeze LOD |
| `UNIVERSE_ROT=x,y,z` | rotate every generated galaxy (degrees) |
| `UNIVERSE_TEMP=<K>` | recolour every generated galaxy at creation |
| `UNIVERSE_PHYS=<n>` | enable physics on the first n galaxies (promote path). NOTE: the camera is never parked at those, so this cannot reproduce anything that needs materialised contents |
| `UNIVERSE_PHYS_IDX=<gi>` | physics on ONE named galaxy (gi = the number in its name minus 1) |
| `UNIVERSE_PHYS_CAM=1` | physics on the galaxy the camera was parked at |
| `SIM_SPEED=<x>` | set the time step headlessly (it lives behind a UI slider and a Save modal, so PLAY alone could never move the clock) |
| `SIM_AUTO=1` | press Auto: dt = the largest simulated cloud's T / autoStepsPerOrbit, then Save |
| `UNIVERSE_DEMOTE=1` | drop physics again at frame 2 (promote->demote round-trip) |
| `UNIVERSE_CONTENTS=1` | pool occupancy per frame: holes / nebulae / stars / planets, nearest body, edits |
| `UNIVERSE_BODY=star:<AU>` or `planet:<AU>` | park the camera that far from the galaxy's first notable star, or its first planet |
| `UNIVERSE_MOVE=1` | nudge the nearest generated body at frame 6 (exercises the edit-and-remember path) |
| `IMPOSTOR_DEBUG=1` | log each far-object point sprite: screen size, sprite size, fade, colour |
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
  change.
- **The scene is NOT settled after three frames — capture at 90.** Something
  flips ONCE at about frame 41 and then holds forever. A capture taken before
  it and one taken after differ by **33 549 px on milky_way** (59 427 on
  universe), concentrated on the softest edge in the frame: Saturn's ring
  boundary. Frames 2–40 are identical to each other and frames 42+ are
  identical to each other, so it is one discrete event, not drift. It is
  FRAME-driven, not wall-clock — making frames four times slower does not move
  it. This is what used to be blamed on "the environment moving" between a
  control and a candidate captured 20 minutes apart: both were unsettled and
  landed on different sides of frame 41. The harness default `COMPARE_FRAMES`
  is now **90**, which is past it with margin at 720p and 1440p, and a settled
  capture matches across directories, commits and hours. **The mechanism is
  still unidentified** — the obvious suspect, `updateCloudRimFactors`, was
  disproved (`rimConverged` is true from frame 2). Do not lower the default to
  save time; the cost is ~1.2 s on milky_way and ~4.8 s on universe, and a
  33k-pixel phantom is worth far more than that.
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

`RASTER_ONLY=1 SKIP_GEO=1 ./bin/blackholesim --compare` on
`projects/milky_way.json` gives a raster mean luminance of **25.556** at the
harness default of **1280x720**, captured at the default frame **90** (at the
old frame-3 default the same scene read 27.704 — see the settling trap above).
Companions: `blackhole.json` **44.131**, `universe.json` **20.050**. The
universe number moved a long way (was 14.538) because its GALAXIES changed
shape — bulge, bar, wider discs, a wider radius range — not because anything
regressed. Back-to-back reruns are byte-identical (`cmp`), so the right test is a
BYTE COMPARE against a control built from the committed source — not a mean,
which hides a large local change.

**The capture height is part of the baseline.** Sprite sizes scale with render
height (see "Sprite sizes are a FRACTION OF RENDER HEIGHT"), so a capture at a
different height is a different picture: thinner dust, weaker haze. 1280x720 is
the user's viewport and the height every project is calibrated at, so a harness
capture and what the user sees are the same image. Changing `CMP_H` changes the
look; an A/B must hold it fixed.

**The number tracks the PROJECT FILE, not just the code.** Before calling a mean
shift a regression, check `git diff projects/milky_way.json` — a retune is not a
bug. The same applies to `projects/universe.json`, which the user re-saves from
the live app: its camera, keyframes and look settings change under you.

`projects/universe.json` is the cost check: ~710 MB peak, ~8.0 s to frame 90.

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

## NEVER add a small offset to a large absolute position

This is the single trap that cost most of a day. Past ~1e14 AU a double's step
is bigger than the thing you are adding, so **the small part vanishes and no
error is raised**:

```
frame origin      2319000000000000.000000
+ orbit 0.417  -> 2319000000000000.500000     (ULP here is 0.5 AU)
round-trip error           0.083 AU
```

Every symptom below was the SAME line of arithmetic wearing a different coat:

- A planet stored as one absolute position had **two positions in its entire
  orbit** at 46 Gly. Hence per-object frames (below).
- `LocateCamera` framed a body with `target + direction * standoff`. At 2e15 AU
  a 0.0002 AU standoff rounded away entirely, so the camera landed INSIDE the
  planet — back faces culled, nothing on screen. Stars survived because they
  are a hundred times larger than their own standoff, which is exactly the kind
  of partial success that hides a bug.
- `PhysicsObject::truePosition()` (origin + offset) collapses the split back
  into one absolute double. It was on every geometry path — locate, picking,
  the selection ring, the name/distance readout, camera focus — and each one
  silently lost half an AU. **It is for DISPLAY ONLY.** For geometry use
  `Renderer::CameraRelative(origin, offset)`, which differences the origin
  first and adds the exact offset after.
- The `--compare` harness parked its camera by putting the whole 2e15 into
  `cameraTranslate` with the anchor at 0. `UpdateInputs` never runs headless, so
  the rebase never happened and **every deep-universe measurement was quantised
  to 0.5 AU** — including ones that had been reported as verified.
- The debug logging had the same disease: it used `truePosition()`, so it
  disagreed with the renderer and sent the investigation round in circles for
  several rounds. **If a measurement contradicts itself, suspect the
  instrument.**

The shape to grep for is any `bigAbsolute + smallThing`. The fix is always the
same: difference against an anchor FIRST, add the small part after.

**And verify by LOOKING.** A "236 px planet" was reported from arithmetic while
the rendered frame was empty space. The number described what should have
drawn, not what did. Scan the actual image for a contiguous bright run.

## Per-object local frames

`RenderedObject::localOffset` / `PhysicsObject::localOffset`: `coordinates` is
the frame ORIGIN and this is an exact small offset inside it. Zero for
everything the user makes, so that path is unchanged.

A star shares its origin with its planets, so the origin carries the coarse
position (the whole system sits half an AU from where the seed said, which
nothing can tell) while a planet's place inside it stays exact. Measured on a
real system at 2.3e15 AU: an orbit of 0.41 AU came out at 0.50 AU as a single
double, a 21% error that would jump as the planet moved.

Every world->camera difference for a framed body is
`(coordinates - gCamAnchor) + cameraTranslate + localOffset`, in that order.
`transformPerspectiveMesh` does it once for the mesh path; the impostor, scene
scale, picking and the overlays each do it too. Saved as `localOffset` only
when non-zero, so ordinary projects are byte-for-byte unchanged.

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

## A view-cone test must subtract the object's SIZE, not treat it as a dot

`offScreen` (main.cpp) decides which galaxy is "the one you are looking at", and
that choice gates BOTH the LOD rebuild ladder and `nearPromoted` — whether a
galaxy renders through the real particle pipeline or the sampled stand-in.

It used to return early on `cosA <= 0` ("behind the camera"), which skipped the
very line below it that subtracts the object's angular size. That is a dot test.
A galaxy you are parked INSIDE has its centre behind you for half of every turn
while it still fills the whole sky, so it lost `nearPromoted`, fell back to the
stand-in, and recovered when you turned round: **"the galaxy phases in and out
depending on which angle I look at it from"**, worst exactly where it was
reported — deep inside, at a star.

- **Pass the RADIUS, not a precomputed angle.** `2*atan2(extent, d)` saturates,
  so it can never say "this thing is all around me". `d <= radius` says it.
- **Clamp `acos` at BOTH ends.** It was clamped only above; a rounding overshoot
  below -1 makes `acos` return NaN, and every comparison against NaN is false —
  which reads as "on screen" and hides the mistake instead of raising it.
- `drawStarfieldChunks`'s own frustum cull is CORRECT for comparison: it tests
  `depth < -r` against the chunk's half-diagonal, so a chunk enclosing the
  camera is kept.
- Same shape still present in `considerForward` (renderer.cpp, the deep-zoom
  forward target): `tc <= 0` skips a galaxy the camera is inside. Different
  symptom — zoom targeting, not visibility — and fixing it changes camera feel,
  so it was left alone deliberately.

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

## Calibrating a new noise field: match the DEPTH, not the area

Replacing the dust field's smooth 3-octave FBM with a 6-octave ridged
multifractal changed the look as intended and quietly multiplied extinction
**eightfold**. `projects/blackhole.json` went from mean 45.5 to **4.3** — a
reference scene rendered essentially black.

The mistake: I fitted the new threshold so the AREA of dusty space matched the
old field (13.5% vs 13.1% at the default coverage) and never checked the mean
optical depth. Ridged noise **saturates near 1 inside a ridge**, where smooth
FBM only just cleared its threshold — so every dusty sample carried far more
depth even though the same fraction of space was dusty:

| coverage | old mean tau | new mean tau | ratio |
|---|---|---|---|
| 0.30 | 0.0162 | 0.1376 | **8.5x** |
| 0.33 | 0.0245 | 0.1700 | **6.9x** |

**When swapping a noise function, the invariant to preserve is the integral it
feeds, not the fraction of space it covers.** The two curves are shaped too
differently to reconcile by threshold alone, so the per-sprite depth ceiling
(`uDustDepth`, cloudFrag) absorbs the difference at **0.22** — a small number
for exactly this reason, with the sweep recorded beside it in renderedObject.cpp.

## Dust settles toward the cloud's OWN plane — and a sphere is the identity

Dust is thin because gas is dissipative: it collides, radiates its energy away
and sinks to whatever plane the object's rotation defines, while stars are
collisionless and keep their vertical motion. An object with NO preferred plane
has nothing to settle into — which is why real ellipticals are dust-poor.

`RenderedObject::measureDustShape` therefore measures the distribution's
principal axes (Jacobi on the 3x3 covariance, on the `cloudGpuDirty` trigger
next to `cloudRmsRadius`) and hands the shader `uDustAxis`, `uDustFlatten`,
`uDustScaleH`, `uDustAxisQ`. **It never branches on "is this a disc".** A sphere
measures q = c/a = 1, at which every term in `dustLane` collapses to the
identity.

- Verified: `globular_cluster_real_2k` measures **q = 0.932** and renders
  **BYTE-IDENTICAL** with `dustSettle` 0 or 1. milky_way's disc measures
  q = 0.122 and gets a 519 ly dust scale height (real MW: 325-490 ly).
- The measurement found the milky_way formation's disc normal on the **Y** axis,
  because that Python-generated file is Y-up while the C++ generator is Z-up.
  A hardcoded normal would have been wrong for one of the two.
- **TRAP:** the vertical window must be weighted by `(1 - q)` as well as the
  dial. Every other term is already the identity at q = 1, but a bare Gaussian
  window would still carve a sphere into a slab along an arbitrary axis.

## Volumetric dust has been tried TWICE — read this before a third

`DrawCloudDust` carries a one-line verdict: *"the honest column through a galaxy
is smooth saturated fog — the granular look lives in the sprites."* The pass was
added in `a61c154` and deleted in `5597302`. It is worth knowing WHY, because
the idea is very appealing and the reasoning that leads to it is sound.

The appeal is real. The sprite model's ceilings are hard, not tunable: a fixed
**1,463 ly** sprite size, point sampling at the **724 ly** particle spacing, and
dust that exists only where stars exist. Real filaments are 1-30 ly. So "make it
a real 3D medium and march it" is the obvious answer.

**Attempt 2 (2026-09-06) reached the same wall from the other side.** The field
recipe was prototyped offline first, against the reference photograph:

- Tileable ridged multifractal + domain warp + log-normal density, sampled at
  three periods with an inter-scale warp. As a FIELD this is right — slices show
  torn branching filaments with no dominant feature size, which is exactly what
  the sprite dust cannot do.
- Integrated as a column from OUTSIDE, it renders **a featureless black lens**.
  The central limit theorem: a line integral through a statistically uniform
  medium averages its own structure away.
- The obvious rescue is depth: attenuate each star only by the dust in FRONT of
  it (a froxel volume), since the deleted pass multiplied the whole scene by the
  whole column. **Tested, and it does not rescue it.** Front-to-back ordered
  compositing and a flat multiply produce nearly identical external edge-on
  images. The rejection is general, not an artefact of how it was applied.

**What IS true, and is the one thing worth keeping:** structure is resolved when
the medium is NEAR. The reference photograph is the view from INSIDE the Milky
Way, where the nearest clouds are a few hundred ly away and subtend large
angles; the same prototype viewed from inside DOES produce branching filaments.
That is physically correct behaviour at both ends — a distant galaxy's dust lane
really is smooth — so a volumetric medium would help the inside-the-galaxy view
and change little else. Whether that one case justifies the build is a judgement
call; it has now twice looked like a bigger win than it is.

Prototype and images: `/tmp/dustproto` is not kept. The recipe is written out in
full above; rebuilding it is an hour, and the negative result is the expensive
part, which is why it is written down here.

**Do not commit the medium bake without a consumer.** The first attempt was
deleted partly because "its draw call had no callers — ticking the box built a
3D texture nothing ever marched". The second attempt's bake was written, built,
measured and then deleted for the same reason rather than left in place.

## Thresholding smooth noise can only make ISLANDS

Why dust read as a string of beads and never as branching filaments: the
excursion set of a SMOOTH random field above a high level is a collection of
isolated, roughly convex islands. That is a property of the operation, not a
parameter — no value of coverage, contrast or patch size turns it into threads.

Folding the field, `r = 1 - |2n - 1|`, puts a crease on the LEVEL SET n = 0.5.
A level set is codimension-1 — a connected surface — so ridges fork and rejoin
the way real dust does. Multifractal weighting (each octave scaled by the
previous) then puts fine structure only where coarse structure already is.

**Octave count is nearly free here.** `dustLane` is evaluated ONCE PER PARTICLE
in the VERTEX shader (cloudVert.glsl), never per fragment, so 3 -> 6 octaves
costs ~3 hash evaluations per particle per frame and nothing per pixel.

**Known ceiling of the sprite model** (this is why the volumetric column work
exists): every dust sprite is a fixed **1,463 ly** across (`worldR = 1.5 x
uDustInfluence`), and the field is point-sampled at the **724 ly** particle
spacing, against real filament widths of 1-30 ly. So a characteristic blob size
is literally a constant in the code, and structure finer than the star spacing
is not representable at any noise quality.

## resolvedCut only DELETES cores — it does not redistribute

The haze pass draws every star unconditionally (cloudVert's final `else` has no
`uResolvedCut` gate), so total = haze(all) + cores(resolved). Raising the cut
removes core sprites and moves nothing into the unresolved sheet: the galaxy
gets dimmer and duller, not smoother. Left at **0** for that reason. To make the
"real telescope look" work, the cut has to be made energy-conserving first — an
unresolved star's haze lobe should carry the flux its core would have had.

## Galaxy shape: measure it offline, do not reason about it

`tools/galaxy_stats.cpp` links `universeGen.cpp` and runs the REAL generator with
no GL context and no window. Build:

```
g++ -std=c++20 -O2 -Isrc -Ivendor/include -o /tmp/galaxy_stats \
    tools/galaxy_stats.cpp src/universeGen.cpp && /tmp/galaxy_stats
```

Two bugs it caught that arithmetic had got wrong:

- **discScale.** Scaling the old fitted scale length by the ratio gave 0.107,
  which measures h_R = 6,449 ly against a target of 8,480. H is not the fitted
  scale length — the 15% extended component flattens the outer profile. Sweeping
  and fitting gives **0.146** (h_R 8,239). The old 0.06 measured 5,583, which is
  why the disc read as "too tall": its height was right all along and its width
  was 1.8x short.
- **bulgeRadius.** `r = Rb * u^n` has median `Rb * 0.5^n`, so asking for a 2,750
  ly bulge produced one with half its stars inside **450 ly** — two pixels, and
  invisible. The outer scale must be divided by `0.5^sersic` for the parameter to
  mean the effective radius its name promises.

Generated galaxies now carry a bulge, an optional bar (35% of spirals; NOTE a
real bar is held by orbital resonances that are not modelled, so a barred galaxy
left simulating smears its bar away), per-galaxy E0-E7 flattening and Sersic
index for ellipticals, per-galaxy lump count for irregulars, pitch angles across
the real 5-35 degree range, and Tully-Fisher `vFlat ~ sqrt(R)` so a dwarf and a
giant stop sharing a rotation curve and an 8e6 Msun black hole.

**Nebulae were in the wrong plane.** `GenerateGalaxyContents` built their origin
as `{r*cos, z, r*sin}` — the Python generator's Y-up convention — while
`GenerateGalaxyStars` puts the disc in XY. Every galaxy's nebulae sat in a plane
at RIGHT ANGLES to its own stellar disc, and never received the galaxy's
inclination or roll either. Fixed; verified out-of-plane spread is now 6.7% of
in-plane.

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

**Why NOT real DM particles** (the rejected experiment; `ensureDarkMatter` was
deleted in the 2026-09-03 cleanup, `stripDarkMatter` is kept as a defensive
call): a spherical
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

## Softening the DIVISION does not soften `normalize()`

`barnesHutForce.glsl` computed every gravity term as `normalize(r) * (G*m/d2)`
with `d2 = dot(r,r) + uSoftening2`. That LOOKS protected — the division cannot
blow up — but `normalize` divides by the RAW length on its own, so `r == 0`
gives `0/0` = **NaN** no matter how large the softening is.

`r` is genuinely zero in a universe: a galaxy's own central black hole is placed
at the galaxy's origin, and the galaxy has stars at its origin too. So the
moment you tick "Simulate physics" on the galaxy you are standing at, one
particle goes NaN, the NaN poisons the shared octree bounds, and the whole
cloud is gone within two frames — the same cascade the DM-particle experiment
died of (above). Symptom as reported: "turned on physics, pressed play, it
disappears instantly."

All three force sites now use the Plummer form, which has no singularity
(`r = 0` yields zero force) and is the consistent law — softening the magnitude
while taking the direction from the unsoftened vector is not:

```
acc += r * (uG * mass / (d2 * sqrt(d2)));
```

- **Only ONE of the three sites had ever fired**; the octree-node and
  depth-capped-leaf sites were the same bug waiting for any two coincident
  particles. Fix all of a shape, not the instance that bit.
- The CPU paths (`physicsObject.cpp`, `renderedObject.cpp`) were already safe —
  they guard `if (d2 == 0) continue;` — and `physicsObject.cpp`'s cloud-source
  loop already used Plummer, so the shader now matches what was there.
- **Plummer is not free.** It differs from the old law by `r/sqrt(r^2+eps^2)`:
  0.2% at milky_way's softening, but up to ~30% for nearest neighbours in a
  SMALL formation where eps is comparable to the particle spacing. No rendered
  frame moved (both reference scenes byte-identical, because they are paused),
  but a small formation left simulating evolves slightly differently now.
- **`UNIVERSE_PHYS=<n>` cannot reach this bug.** It enables physics on the FIRST
  n galaxies, and the camera is never parked at those — a galaxy you are not at
  has no materialised contents, so no black hole, so no coincident pair, and it
  simulates perfectly for hundreds of frames. Four separate reproduction
  attempts came back clean because of this. Use **`UNIVERSE_PHYS_IDX=<gi>`**
  (physics on one named galaxy) or **`UNIVERSE_PHYS_CAM=1`** (the galaxy the
  camera is parked at) instead.

## OPEN BUG: the shared sim frame is anchored on sim[0]

`CloudObject::SimulateSharedForward` sets `simOrigin = sim[0]->position` — the
first simulating cloud — and hands every other cloud to the GPU as a float
offset from it. That is right for the case it was written for (colliding
galaxies near each other) and wrong whenever two simulating clouds are far
apart, which a universe makes easy.

Measured with a cloud at the origin and a galaxy at 2.5e15 AU: the galaxy's
`uFrameOffset` is 2.5e15, whose float ULP is **268 million AU** against a galaxy
radius of 1e9. `pos += vel * uDt` then does NOTHING — one step of motion is a
hundred times smaller than the smallest representable change. Positions freeze
BIT-FOR-BIT while velocities, computed locally, wind up without limit:

| frame | positions identical to start | median speed | max speed |
|---|---|---|---|
| 12 | 15000/15000 | 46 | 109 |
| 120 | 14102/15000 | 198 | 431 |
| 300 | 14431/15000 | 480 | 1050 |

Then stars start teleporting: every non-zero displacement measured was an exact
multiple of 2^25 AU — the ULP, not motion. This is "NEVER add a small offset to
a large absolute position" again, on the GPU, inside the physics shader.

It needs two simulating clouds, so it is not what makes a single galaxy vanish —
but **`projects/milky_way.json` ships a 20 000-particle cloud with
`simulatePhysics: true`**, so starting from the template, generating a universe
and enabling physics on a galaxy lands straight in it. Reproduce with
`UNIVERSE_TEST=2000 UNIVERSE_STARS=15000 UNIVERSE_PHYS=1 SIM_AUTO=1 PLAY=1`.
The fix is to stop anchoring on `sim[0]` and give clouds that are far apart
their own frames; not yet done.

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

## A solid under a pixel is a POINT SOURCE, not a small sphere

There is no MSAA, so a sub-pixel triangle only produces a fragment when a pixel
centre lands inside it: Saturn from Earth is 0.027 px across and flickered in
and out. Below the pixel floor an object draws as one flux-matched point sprite
(`Renderer::DrawObjectImpostor`, `impostorVert/Frag.glsl`), gated inside
`DrawPhysicsObject` — the ONE funnel every raster site already goes through.

This is NOT the rejected `uSampleWeight`: that made 8 sampled stars impersonate
50 000, a different look. One sphere collapsing to one point AT the resolution
limit is the same object, and the handover is a fade.

- **Flux is exact, then compressed by a POWER** (`kImpostorRange`), which is
  order-preserving. Compressing the object's ANGULAR SIZE instead — the cloud
  path's rule — squashes radius² to radius^0.1, so a body's own size stops
  counting: Jupiter came out seven times DIMMER than Mars when it should be
  forty times brighter.
- **Only the luminance is compressed**, with all three channels scaled by the
  same factor. Per channel washes the colour out of exactly the objects meant
  to be recognisable.
- **A textured planet's albedo is the TEXTURE's average**, area-weighted by
  sin(theta) — an equirectangular map gives a pole as many rows as the equator.
  Its `color` field is normally still the spawn default because the shader
  never reads it, which is why every planet used to come out the same brown.
- **Keep the dot SMALL and bright.** Apparent size comes from the post chain:
  `spikeSourceFrag` keeps only what a pixel exceeds its neighbour six half-res
  texels out, so a wide soft disc cancels itself and gets NO spikes — it just
  sits there as a ball.
- `screenPx` is divided back to `spriteRefHeight` before the flux, or peak
  brightness scales with render height.

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

## Depth is REVERSED-Z in a 32-bit FLOAT buffer

A 24-bit fixed buffer with the classic `z = 1 - n/d` mapping resolves
`d^2/(n*2^24)`. The near plane follows the NEAREST surface, so parked on one
planet and looking at another, the whole target lands in ONE depth bucket: its
far side z-fights through its near side, and a ring — depth-TESTED against the
planet but never depth-WRITTEN — leaks through the body in triangle-shaped
patches. That is the "parts of the mesh are missing" Saturn. **No near-plane
rule fixes it** (`n = d/10` still gives one bucket at 10 AU), so the MAPPING
changed instead: `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)` at init, depth
= `n/d` (near 1, far 0), `GL_DEPTH_COMPONENT32F` everywhere. Precision is
~1e-7 RELATIVE at any distance, independent of the near plane.

Everything that touches the convention, all of which must agree:
- `RenderedObject::perspective` and `ProceduralGenWindow::buildProj` are the
  ONLY rasterisation projection builders (`out[10] = n/(f-n)`,
  `out[14] = fn/(f-n)`; far stays finite so far clipping and `GL_DEPTH_CLAMP`
  are unchanged).
- Depth funcs are `GL_GREATER` (set at init, restored after the cloud passes)
  and `GL_GEQUAL` where `LEQUAL` was — far stars clamp to depth 0, which IS the
  cleared value. `glClearDepth(0.0)`. **Never write `LESS`/`LEQUAL`/
  `glClearDepth(1.0)` again.**
- Every depth attachment is `GL_DEPTH_COMPONENT32F`.
- Anything computing NDC depth by hand must use `n/d` in [0,1] and clamp just
  ABOVE 0, never to 1 (`DrawObjectImpostor` does this). A dot sitting exactly
  at 0 fails `GL_GREATER` against the cleared value and vanishes.

**The nebula depth blit is the trap.** `BeginNebulaPass` blits scene depth into
its own target and runs whether or not a nebula exists. `glBlitFramebuffer`
refuses a depth copy between DIFFERENT formats, and it fails SILENTLY — one
warning, then nebulae stop being occluded by solids for the rest of the
session. Scene targets are 32F, but `glfwWindowHint(GLFW_DEPTH_BITS, 24)` means
the default framebuffer is 24-bit normalised and the scene is sometimes drawn
straight into it, so `EnsureNebulaTarget` matches its depth format to whatever
is currently bound. When querying that: the default framebuffer names its depth
`GL_DEPTH`, a real FBO names it `GL_DEPTH_ATTACHMENT` — asking the wrong one is
an `INVALID_ENUM` returning zeros, which reads exactly like "no depth here".

Verified against the commit before it: `universe.json` **byte-identical** (a
galaxy never writes depth — `glDepthMask(GL_FALSE)`, additive, order-
independent — so precision cannot reach it), a galaxy at 1e13 AU and at the
1e10 pipeline switch byte-identical, the point-source impostors identical to
1 px, milky_way different only along the ring/planet contact (882 px at max 20),
and the deep-zoom Saturn 29 091 px — the chevrons gone. RT and the geodesic
capture move only where a cloud is in frame, because the raster pass builds the
dust-density map the tonemap consumes; with the galaxy removed they are
byte-identical.

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

## A universe generates bodies, and only a few are ever real

Each galaxy DESCRIBES a central black hole (M-sigma off its own rotation curve
— use vFlat/2 for the bulge dispersion, `vFlat/sqrt(2)` overshoots Sgr A*
twelvefold), nebulae out in the disc, and star systems. All from the galaxy's
own seed, so nothing is stored and a universe stays a few bytes.

**Systems sit on the galaxy's OWN stars.** A short prefix of the star field is
cheap to generate, and star `i` is the same star at any count (the generator
draws its cluster knots with a fixed count BEFORE the star loop, which is what
preserves the prefix property) — so what you fly at is what materialises, and a
system keeps its identity as the LOD ladder climbs. Gating systems on being
inside a galaxy was circular: you could not find a star until you were at one.

**The pool is allocated once and recycled IN PLACE, never erased.** Same shape
as a galaxy promoting. Erasing would leak — `deleteObject` frees no GL handles
— and would shift every later index under `selectedIdx`, `hoverIdx`,
`dynParent` and `nebula.sourceCloud`, all of which are bounds-checked and would
silently retarget. `reserve(256)` on `physicsObjects` is a PHANTOM: no
destructor touches GL and the move is noexcept, so reallocation is a correct
relocation.

- **Choose by apparent size, not distance.** By distance the budget went to
  planets far too small to see. But a star is 0.005 AU and a planet 4e-5, so on
  size alone they lose to every nebula in the sky for ever — hence a reserved
  share for WHOLE systems (a planet without its star reads as a bug).
- **Throttle by COST, not count.** Only a nebula is expensive (it rebakes a
  volume). A flat few-per-frame cap meant a 256-slot pool took 64 frames and the
  body you had just flown to was still not live.
- **A nebula's volume is 7.1 MB at the stock 96³** — 300 MB for 42 of them, for
  something 33 px across. Generated ones use `nebulaVolumeRes` (48 = 0.88 MB).
- Edits are remembered against a body's stable `key` (derived from its star's
  index), detected by comparing against where the pool PUT it — so the gizmo,
  the inspector and bring-to-me all work without knowing this exists.
- **Absent keys mean OFF.** A universe saved before contents existed must not
  grow them, so the loader's fallbacks and the struct defaults are deliberately
  DIFFERENT numbers — same split as keyframe smoothing. Do not unify them.

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

## Sprite sizes are a FRACTION OF RENDER HEIGHT, not absolute pixels

Brightness in this renderer comes from sprites OVERLAPPING (see "Light falls off
because objects get SMALLER"). Every sprite size was therefore a look decision
expressed in absolute pixels — the haze lobe a fixed `clamp(..., 8, 160)`, the
dust and gas puffs perspective-sized but clamped to absolute pixels, the star
core a fixed `2..9`. Double the render height and all of them stay the same
PIXEL size, i.e. HALF the size relative to the frame, so they overlap less.

Measured on a galaxy + a large hole, the SAME scene, 720p vs 1440p: mean
luminance **33.32 vs 13.60** — 2.5x darker at double the height, with thin dust
and a weak, streaky haze. Scaling dust/gas/haze alone only got the ratio from
0.41 to 0.56; the star cores are the other half. With all four scaled:
**19.06 vs 19.12, a ratio of 1.003**.

`uSpriteRefH` (setting `spriteRefHeight`, Quality & Speed -> **Sprite Density**,
default **720**) is the height sizes are calibrated at; everything is multiplied
by `uViewportH / uSpriteRefH`. 0 = off (absolute pixels, the old behaviour).

- **The reference runs BACKWARDS from how it reads.** Scale is height/reference,
  so a LOWER reference draws BIGGER sprites — more overlap, thicker dust, and
  far more fill. At a 720p render: 480p ref = 1.5x sprites and 7.70 s/30 frames;
  4K ref = 0.33x and 4.36 s. That, not the lens, is why a scene can be much
  faster on the "better"-sounding setting. The UI is named by density for this
  reason.
- This is NOT what `uCinePixelScale` does. That compensates SSAA, where the
  buffer grows but the TARGET height does not.
- A project's `spriteRefHeight` should be the height its look was tuned at.

## Fewer particles is NOT a smaller version of the same picture

Decimating a cloud does not scale the image down, it changes what the image IS.
A 58 800 -> 10 000 particle cloud: mean **27.79 -> 41.48**. The picture gets
much BRIGHTER and the dark dust lanes largely vanish, because dust is
multiplicative and 5.9x fewer puffs darken 5.9x less. Sprite Density cannot
compensate — it pushes the wrong way (480p ref 63.5, 360p 78.6, 300p 86.7),
since bigger sprites add more light than they add darkening. **Dust lanes need
MANY SMALL sprites, never a few big ones.** Lensed arcs need the density too:
at 10k they read as separate strokes instead of merging into rings.

`blackhole.json` runs at 10k for speed (~15 ms/frame against ~54 ms at 58.8k),
which is right for geometry and artefact hunting and misleading for anything
about dust or the cloud look — its original 58 800-particle sidecar is still on
disk as `projects/blackhole.data/` if the full look is ever needed. milky_way
(20k) and universe (procedural) are full size.

## The raster black-hole lens is FORWARD: each source is bent by its own position

Per source, not per pixel: `lfPlace` in `src/shaders/lens_forward.glsl` asks each
particle where its light lands. One rule, applied to every star and dust puff:

```
beta = theta - alpha(b) * 0.5 * [ h + (Dl*delta - b^2)/hl ] / Ds
```

`delta` is the source's position along the camera->hole axis measured FROM THE
HOLE, positive behind it. **No plane, no front/back split, no dominant cloud** —
the absence is the point, and it is why a sphere, a disc face-on, two holes and
empty space all behave the same. `delta -> +inf` gives the textbook thin lens;
`delta = 0` gives HALF the bend (which is why a thin lens cannot draw an
accretion disc); `delta -> -inf` gives 0, so foreground matter covers the shadow.

`src/lensForward.cpp` is the C++ MIRROR of the shader — same formulas, so the map
can be PROVEN on the CPU (`LENS_LUT_TEST=1`) instead of debugged through a
framebuffer. **Change both together.** `alpha(b)` is a 1024-tap table built by
integrating the same null geodesic the ray tracer marches, so the raster ring and
the geodesic ring agree by construction.

### The traps that cost real time

- **A LENS MUST NEVER DELETE MATTER.** Reporting slow solver convergence as "no
  image" culled 8.9% of images — particles blinking out around the hole.
- **A footprint smaller than its own source draws a SOLID BLOCK.** If the sprite
  cannot grow to cover the source it is drawing, nothing ever reaches the
  source-disc test, so nothing is discarded and the square fills with a
  slowly-varying sample. The edge fade only softens its border, which is why it
  reads as a BLURRY rectangle and why more fading never removes it. The
  grow-to-cover step iterates for this reason (four rounds of at most 4x).
- **A LOW Stretch is a FALSE ECONOMY.** It reads like a cost dial and is not:
  57 ms/frame at 2.3 against 10 ms at 12 on blackhole.json, ~6x slower for a
  small look change — because a capped footprint discards nothing and every
  fragment it draws survives to be shaded. **Max arc size** is the speed dial;
  Stretch is a look dial that happens to cost.
- **Azimuthal extent is `asin(srcRad/|beta|)`, saturating at pi.** Deriving it
  from the finite-source cap on tangential stretch forces `tangA <= theta`, a
  hard **57 degree** ceiling on every arc's half-sweep in every scene — under by
  up to 3.14x exactly where the arc should be longest, which is why arcs did not
  wrap round the hole.
- **`gl_PointCoord`'s origin is UPPER-LEFT** — its t axis runs DOWN, screen y
  runs UP. Without the flip every arc curves the wrong way, and a round sprite
  and a symmetric ring both hide it completely.
- **`inf * 0 = NaN`, and NaN fails every discard test.** `b` is floored and the
  disc test is written negated so a NaN discards.
- **`delta` needs DOUBLE.** The object's offset from each hole is differenced on
  the CPU; the shader only adds a small per-particle offset.
- **The solver is BISECTION, not Newton** — the Born profile can FOLD in the deep
  near field, which breaks the bracket invariant a Newton safeguard relies on.
- **The shader and `lensForward.cpp` are NOT byte-equivalent, deliberately.** The
  C++ brackets the root at `theta > beta` (exact, since `alpha*g > 0` always,
  and it steps over the fold); the shader does not. Same root either way — the
  shader just searches a wider interval — but do not "sync" them without
  re-checking the image, and do not assume a CPU number transfers exactly.

### The map is NOT off centre — measure before believing it is

On a uniform-sphere scene the ring fits a circle to **0.59 px rms**, centred within
**1 px** of the drawn hole (two independent estimators). `lfRotateAway` displaces
every image strictly radially from the hole, so a ring of equal-deflection
sources is concentric by construction and no term could shift it.

An eyeball estimate on a disc scene gave "126 px above the hole"; that was a
structure-tensor fit biased by the disc's own horizontal lanes. Against the
lens's real screen position (`LF_HOLEPOS=1` prints it) the figure was 13 px, and
that is the arch's asymmetry, not a centring error.

**What the raster genuinely lacks** is the higher-order photon windings: it draws
the direct image only, while the geodesic resolves light that orbits the hole
two or more times. Those windings are the concentric rings the ray tracer shows
and the raster does not. A cube map baked from the hole, backward-traced over an
annulus just outside the shadow, is the sketched fix — not yet built.

### Test scenes and known gaps

There are **three projects**, and that is deliberate: `blackhole.json` (a hole
inside a galaxy — the lens scene), `milky_way.json` (planets, rings, Saturn in
frame — the raster baseline) and `universe.json` (procedural galaxies — the cost
check). The lens test scenes were deleted in the same cleanup; if a lens question
needs a uniform sphere or two holes again, build the scene rather than assume one
is lying around.

Ground truth for GEOMETRY is `SKIP_RASTER=1 PROJECT=... --compare`
(`/tmp/cmp_geo.png`). Not bent yet: meshes, nebula volumes, the skybox.

### Deleted (2026-09-03) — do not go looking for these

The old BACKWARD lens and everything around it: `lensRaster.glsl`, the cube bake
and apex vantages, the wide back-field pass, the two-pass front/back split, the
foreground dust-warp slabs, and every `uBH*`/`uSizeRef*` uniform on the cloud
path. Also **volumetric dust** (its draw call had no callers — ticking the box
built a 3D texture nothing ever marched), **secondary images**, and
`ensureDarkMatter()`. ~2 300 lines; every reference scene byte-identical after.
Then the lens test PROJECTS (`bh_ref`, `bh_disk`, `bh_out`, `lens_test_*`) and
the formations that only fed them.

**Flux is not the look.** Arc ribbons (drawing each image as the annular sector
it truly is) cut fragment work 508M -> 248M and conserved flux to +3.1%, and were
still rejected on sight: thin exact arcs lost the OVERLAP between fat sprites
that makes dust saturated and the foreground opaque. A test that checks total
light cannot see an overlap change. Same lesson as the rejected `uSampleWeight`.

## Accretion disk = a cloud

`templates/formations/accretion_disk_30k.json` (spawn it onto a hole): thin
Keplerian disc scaled to the 4.15e6 Msun hole (3-20 Rs, H/R 1.5%, denser inward,
orbital velocities).
Gargantua's look is what the lens does to a ring of matter around the hole — a
galaxy alone can never produce it, there is no matter at 3-20 Rs to make the
cross.
