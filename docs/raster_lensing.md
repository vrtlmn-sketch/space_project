# Raster gravitational lensing — design notes

Status: Phases 0–2 implemented and verified; Phases 3–4 still design. Raster
only. (Doppler is intentionally out of scope — it does not apply to the baked
far field.) A rasterized, gravitationally-lensed black hole (shadow + Einstein
rings of the lensed starfield) now renders headlessly via `LENS_TEST=1`.

Progress:
- **Phase 0 DONE** — `src/shaders/lensing_common.glsl` holds `holeAccel` + the
  RK4 integrator, included by all four geodesic compute shaders. Verified
  byte-identical RT/geodesic output (the geodesic capture matched a same-sitting
  committed control exactly; an apparent RT diff was environmental hard-edge
  drift, confirmed by rebuilding the control).
- **Phase 1 DONE** — far-field cube map baked from the black hole's viewpoint.
  `Renderer::EnsureLensCubemap` / `LensBeginFace` / `LensEndFace` render six 90°
  faces of the empty sky + clouds/galaxies (no planets/rings/nebulae) into a
  `GL_TEXTURE_CUBE_MAP` (RGBA16F). `LENS_DEBUG=1 ./bin/blackholesim --compare`
  dumps them to `/tmp/lens_{posx,negx,posy,negy,posz,negz}.png`
  (`LENS_FACE=<px>` sets the size, default 512). On milky_way.json the disc band
  lands on the equatorial faces and the poles are sparse — directionally
  correct. The normal render is byte-identical (feature is inert until called).
- **Phase 2 DONE (headless test)** — `src/shaders/lensRaster.glsl` (compute)
  reconstructs each camera ray, bends it with the SHARED `holeAccel`/`rk4Step`
  (Phase 0 include, single BH via `geodesicAccel = holeAccel(pos,vel,uBH_RS)`),
  and on escape samples the Phase-1 cube; horizon crossings are black. Same
  adaptive step schedule and escape test as the RT geodesic shader, but with the
  per-step scene tests deleted — one ODE march + one cube fetch per pixel.
  `Renderer::DispatchRasterLens` runs it into the record FBO.
  `LENS_TEST=1 ./bin/blackholesim --compare` parks the camera near the first BH,
  bakes, lenses, and writes `/tmp/lens_bh.png` — a black shadow ringed by the
  lensed starfield. Tunables: `LENS_DIST` (Rs, default 20), `LENS_FOV` (deg, 50),
  `LENS_STEPS` (1500), `LENS_FACE` (bake px, 1024). Normal render byte-identical
  (verified on the stable geodesic channel). NOT yet wired into the live view —
  that (composite over the real scene with the deflection + depth gate, and the
  single-BH LUT) is the remaining Phase 2→3 work.

## Goal

Real-time black-hole lensing in the **raster** view, so a scene with a lot of
stars/galaxies bends light around the black hole at interactive rates on a
laptop. Today the geodesic path exists **only** in the compute (RT) shaders, and
it is real-time only against a plain skybox — because it re-tests the whole
scene at every integration step. In raster a black hole is currently just a
black sphere (`blackHoleFrag.glsl` outputs solid black); there is no bending at
all.

The one idea that makes this cheap: **decouple ray-bending from scene
intersection.** Bake the far field once into an environment map, then bend rays
and do a single environment lookup on escape instead of comparing against every
star at every step.

## Why the current RT path is expensive (confirmed)

`geodesicCompute.glsl:787–982` integrates the photon path with RK4 and, **at
every step**, runs three O(`uObjectCount`) passes over the whole scene:

1. nearest-BH scan (step-size / capture / escape),
2. solid-hit test along the bent segment (`raySphere`/`rayMesh` + rings),
3. glow accumulation over every star (type 1), galaxy point (type 2) and nebula
   (type 4) — each via a closest-approach + PSF evaluation (`pointSourceGlow` in
   `galaxy_common.glsl`).

Cost is **O(uMaxSteps × uObjectCount)**; every star is touched ~`uMaxSteps`
times per ray. That per-step star comparison is the whole expense. Baking the
far field collapses passes 1–3's *environment* contribution to one texture
fetch.

## The physical shortcut

Far-field sources (stars, galaxies, background) are effectively **at infinity**:
only the *direction* an escaped photon came from matters, not parallax. And a
Schwarzschild hole is **static and spherically symmetric**, so deflection
depends only on the impact parameter. Two consequences drive the whole design:

- The far field can be baked into an environment map **centered on the black
  hole**, sampled by the bent escape direction.
- For a single hole at a fixed camera distance, the map from *screen-angle-off-
  the-BH* → *outgoing direction* is a **1-D function** — precomputable into a
  LUT, so the per-pixel runtime cost is two texture reads.

Anything close (planets, the accretion disk, the BH itself) has real parallax
and self-occlusion and **cannot** be baked — it is marched. This is the
far/near split, and it maps 1:1 onto the object-type enum:

| Regime | Objects | Treatment |
|---|---|---|
| **Far** | stars (`type 1`), galaxy points (`type 2`), nebulae far away, loaded spheremap background | bake to env map, bend ray → sample |
| **Near** | the BH (`type 3`), planets/meshes, accretion disk (rings) | march along the bent ray (Phase 4) |

## What already exists vs. what is new

Reusable, already correct in the tree:

- Photon physics `holeAccel` / `geodesicAccel` / `rk4Step` —
  `geodesicCompute.glsl:634–711`.
- Escape → environment sampling `sampleSkybox(dir)` (equirectangular) —
  `geodesicCompute.glsl:24–41`; the same math is in `skyboxFrag.glsl:16–27`.
- BH uniforms `uBHPos` / `uBH_RS` (single-BH acyclic path).
- Offscreen-FBO render-from-arbitrary-viewpoint: `EnsureRecRasterFBO` /
  `BeginRecordRaster` / `EndRecordRaster` (`renderer.cpp:8218–8273`); the PiP
  flip-camera path; `nebulaBake.glsl` as the "bake a field once, sample it many
  times" precedent.
- External env-texture injection: `main.cpp` `loadSpheremap` sets
  `renderer.skyboxTexID`; the renderer samples whatever handle it is given.
- Large-world camera-relative precision pattern: `(pos - gCamAnchor) +
  cameraTranslate` in double on the CPU, small float to the GPU.

New code required:

- A **cubemap** color target + `samplerCube` (only 2D equirect exists today).
- A **6-face far-field bake** from the BH viewpoint.
- A **full-screen raster lens pass** run after the scene draw.
- A **single-BH deflection LUT** (fast path).
- Settings + UI toggle, defaulted OFF.

## Architecture

A full-screen post pass, after the raster scene is drawn and before UI/overlays,
gated on `!rayTracerView && hasBH && rasterLensingEnabled`.

```
1. (dirty only) BakeLensEnvironment() -> 6 cube faces from BH center, far field only
2. (single BH)  BuildDeflectLUT()     -> 1-D deflection vs screen-angle, for camera dist R
3. every frame  RunRasterLensPass()   -> per pixel: bend ray, sample cube, composite
```

### The composite rule — preserve the current image outside the lensed disk

The lens pass **reads the existing scene color and depth**, and only replaces a
pixel when two things hold:

- the pixel's ray bends **meaningfully** (deflection above a small threshold),
  which is naturally a disk around the BH on screen, and
- the existing **depth is background/sky** (nothing solid in front).

Where deflection → 0 the output is the untouched scene pixel, so **outside the
lensed disk the image is byte-identical to today** (honours "the look is the
contract"). Because the pass respects the depth buffer, a planet *in front of*
the BH correctly occludes the lensed region **without** being marched — so
Phases 1–3 are already correct for foreground solids; only lensing *of* a
near-field object's own geometry waits for Phase 4.

Feather the deflection threshold over a narrow band. In that band the lateral
displacement is small (deflection just crossed the threshold), so any star
"doubling" between the direct pixel and the cube sample is sub-pixel. Keeping
the direct render outside the disk also sidesteps the cubemap's resolution
limit: the cube only has to look right where the field is small on screen and
heavily distorted.

### Env map: cubemap, not equirect

Bake into a `GL_TEXTURE_CUBE_MAP` (RGBA16F), because 6 × 90° raster renders map
naturally onto cube faces, with no pole distortion and no reprojection pass. Add
one trivial `textureLod(samplerCube, dir)` sampler for the baked path; the
existing equirect `sampleSkybox` stays for the loaded-spheremap-as-background
case. Face size is a setting (`lensCubeFaceSize`, default 1024 → ~48 MB;
2048 → ~192 MB).

## Phases

Each phase is independently shippable and leaves the default image unchanged
(feature defaults OFF).

### Phase 0 — shared physics include (DRY)

Extract `holeAccel` / `geodesicAccel` / `rk4Step` from `geodesicCompute.glsl`
into `src/shaders/lensing_common.glsl` and have the RT geodesic shaders `#include`
it, exactly like `rings_common.glsl` / `galaxy_common.glsl`. The raster lens
shader then shares **one** definition of the photon physics — a second copy is
how the two views would silently drift. Verify the RT baseline (milky_way RT
26.255 / geodesic 25.498) is byte-identical after the refactor before adding
anything new.

### Phase 1 — far-field cubemap bake

- `EnsureLensCubemap(faceSize)` — create/resize the cubemap FBO + RGBA16F cube
  texture + depth RBO, modeled on `EnsureRecRasterFBO`.
- `BakeLensEnvironment()` — for each of 6 faces: set a 90° FOV projection and the
  face's view rotation, camera **at the BH centre**, and draw the scene with a
  **far-field-only predicate** (stars/galaxies/nebulae + loaded background;
  **skip** the BH itself, planets, meshes, disk). Reuse the existing raster scene
  draw with a render flag; do NOT write a second draw path.
- Dirty tracking: bake on first frame, on far-field motion (physics running /
  object-position hash change), or BH-moved-relative-to-field. **Not** on camera
  move — the far field is at infinity. Amortise the 6 faces across frames
  (1–2/frame into a scratch cube, swap when complete) or bake all 6 on a dirty
  flag and accept a hitch for static scenes.
- `LENS_DEBUG=1`: dump the 6 faces to disk to eyeball the bake.

Deliverable: nothing visible yet; a validated cubemap of the far field.

### Phase 2 — per-pixel lensing pass (works for any number of BHs)

- New shader `src/shaders/lensRaster.frag` (full-screen) — or a compute pass if
  it composites more cleanly with the HDR target.
- Per pixel: reconstruct the world-space camera ray (NDC → inverse proj → view
  rotation, as `skyboxFrag.glsl:16–20`). BH position is camera-relative in double
  on the CPU (`(bhPos - gCamAnchor) + cameraTranslate`), passed as a float
  uniform `uBHPosRel`; verify with `CAM_ANCHOR=0`.
- **Early out**: if the ray's closest approach to the BH is large (deflection
  negligible), keep the existing framebuffer pixel. So only the BH disk pays the
  integration; most of the screen is a cheap reject.
- Otherwise integrate with `rk4Step` against every `type==3` hole (multi-BH
  superposition, same as RT). `r < Rs` → horizon (black for now; disk in
  Phase 4). On escape → bent direction → `textureLod(samplerCube, dir)`.
- Composite by the rule above (deflection gate + depth gate + feather).
- Setting `lensMaxSteps` (mirror `rtMaxSteps`); adaptive step size near the hole.

Deliverable: stars visibly bend around the BH in raster, at interactive rates,
for single or multiple holes.

### Phase 3 — single-BH deflection LUT (the fast path)

- `BuildDeflectLUT()` — for the single-BH case (`uBHPos`/`uBH_RS` acyclic
  scenario), integrate the geodesic once per LUT texel to build a 1-D
  `screen-angle α → outgoing angle β` (or deflection δ = β − α) table into a 1-D
  texture `uDeflectLUT`. Rebuild only when the camera–BH distance R changes by
  more than a few percent.
- Per pixel then: α = angle(camera ray, camera→BH); if α > α_max → existing
  pixel; else LUT fetch → rotate the camera→BH direction by β in the plane
  spanned by (camera→BH, ray) → sample cube. Two texture reads, no loop →
  hundreds of FPS.
- Multi-BH keeps the Phase-2 integrator (no single symmetry axis, so no LUT).
- Photon ring / higher-order images: the primary image falls out of a monotone
  LUT; secondary images need a longer integration or a multi-branch LUT — a
  stretch, mark the shadow edge and defer.

Deliverable: the common single-BH scene runs at near-free per-pixel cost.

### Phase 4 — near-field marching (planets + accretion disk)

- For objects flagged near-field (within a distance threshold of the BH), march
  them along the **bent** ray, restricted to lensed-disk pixels only. Reuse the
  RT solid-hit (`raySphere`/`rayMesh`) and `ringsAccumulateSegment`, but over the
  near-field subset only — a handful of objects, so per-step intersection is
  cheap again.
- Order along the path: march near-field solids/disk first; if hit, use that;
  else escape → cube sample. The accretion disk (rings on the hole) is the
  visually important element here and the reason the phase exists.

Deliverable: the planet next to the hole and the accretion disk lens correctly,
not just the star field.

## Settings

Add to `SceneSettings` (`projectSerializer.h`, defaults in one place per the repo
rule), serialize in `projectSerializer.cpp`, mirror on `Renderer`, and add a UI
toggle:

- `rasterLensingEnabled` (bool, **default false** — baseline unchanged).
- `lensCubeFaceSize` (int, default 1024).
- `lensMaxSteps` (int, default = a fraction of `rtMaxSteps`).
- `lensDeflectThreshold` / feather width (float) — the composite gate.

Default OFF means `projects/milky_way.json` (which contains Sagittarius A*)
renders exactly as today until the user turns lensing on, so the raster baseline
(mean 29.161, byte-identical back-to-back) holds.

## Verification

- **Refactor gate (Phase 0):** RT/geodesic captures byte-identical before/after
  the shared-include extraction.
- **Composite gate (Phases 1–3):** with lensing ON but the camera pointed away
  from the BH, the frame is byte-identical to lensing OFF (the deflection gate
  never fires) — proves the pass only touches the lensed disk.
- **Depth gate:** a planet dropped in front of the BH is not overwritten by the
  cube sample.
- **Precision:** `CAM_ANCHOR=0` reproduces the old quantised camera; the lensed
  disk must not jitter as the camera moves at universe scale.
- Harness: reuse `--compare`; add `LENS_DEBUG=1` to dump the cube faces and the
  deflection LUT. Remember the harness traps (`rm -f` the capture first, check
  the `[IMG] Saved` line, discard the first run after a rebuild, only ever
  `pkill -f 'blackholesim --compare'`).

## Performance budget (laptop)

- **Bake:** 6 × scene-raster cost, but only on a dirty far field; amortised or
  hitched. Steady-state ≈ free for a static field. Memory: 1024² RGBA16F cube ≈
  48 MB.
- **Lens pass:** most of the screen early-outs (ray far from the hole). LUT path
  ≈ 2 fetches/pixel over the disk → hundreds of FPS. Integration path ≈ 50–100
  cheap steps/pixel over the disk only → comfortably real-time at 1080p.

## Risks / open questions

- **Cube resolution vs. star detail in the disk** — mitigated because the direct
  render is kept outside the disk, and inside it the field is small and
  distorted. If insufficient, raise `lensCubeFaceSize` or bake the loaded
  spheremap at higher res.
- **Multi-BH** has no LUT shortcut — still cheap (integration with no scene
  test), just not two-fetch cheap.
- **Photon ring / multiple images** — Phase 2/3 give the primary image; secondary
  images are a stretch (longer integration or multi-branch LUT).
- **Accretion disk** is unlensed until Phase 4; it can be drawn unlensed by the
  ordinary raster path in the meantime.
- **Feather-band doubling** — kept sub-pixel by construction; revisit if visible.

## Agreed next step

Phase 0 (extract `lensing_common.glsl`, prove the RT baseline is byte-identical),
then Phase 1 (cubemap bake + `LENS_DEBUG` dump) so the bake can be eyeballed
before any pixel is lensed.
