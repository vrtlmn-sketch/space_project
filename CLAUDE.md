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
raster mean luminance of **~61.61** (band 61.60–61.69 across runs; wider when
the user's live session holds the GPU). Check it after any shared-shader or
cloud-pipeline change. History: it was 60.95 for a long stretch, drifted with
the float->double position work, and moved to ~61.61 where it has held since.

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
