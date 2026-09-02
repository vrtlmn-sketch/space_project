// ---------------------------------------------------------------------------
// lens_forward.glsl — the FORWARD black-hole lens map.
//
// ONE rule, for every source in the scene: a particle, a dust puff, a mesh
// vertex, near or far, inside a galaxy or in empty space. Given where a source
// IS, this says where its light lands.
//
// This is the GPU mirror of src/lensForward.cpp — same formulas, same
// constants, same iteration count. That file proves the map on the CPU
// (monotonicity, solver convergence, identity, depth-continuity) so this one
// does not have to be debugged through a framebuffer. CHANGE BOTH TOGETHER.
//
// Why forward and not per-pixel: a per-pixel lens bends a ray and then has to
// ask "what is out there?" — but the raster pipeline only has rendered PICTURES
// of the scene, never the scene, so it must guess the depth of what it sampled.
// Every scene assumption in the old lens (a dominant disc plane, a source
// sphere, a front/back split) was a stand-in for that missing depth. A source
// knows its own position, so this map needs nothing about the scene's shape:
// no plane, no split, no reach, no falloff, no dominant cloud.
//
// Geometry, all angles in radians, distances in AU:
//   Dl     camera -> hole distance
//   delta  the source's position along the camera->hole axis MEASURED FROM THE
//          HOLE; positive is BEHIND the hole (further from the camera)
//   theta  image angle from the hole direction (always > the photon angle)
//   beta   the source's true angle from the hole direction. SIGNED: negative
//          means "on the far side of the hole from this image", which is how
//          one expression covers both the primary and the secondary image.
// ---------------------------------------------------------------------------

const float LF_BCRIT = 2.5980762;   // 3*sqrt(3)/2 — photon capture, in units of rs
const float LF_QMAX  = 8.0;         // table range; alpha up to ~7.6 rad (over one winding)

uniform float uLfPxPerRad;  // pixels per radian — the "is this worth doing" gate.
                            // Declared HERE, above every user: GLSL requires declaration
                            // before use, and putting it further down silently failed the
                            // whole cloud shader to compile — which draws nothing and
                            // therefore looks FAST.
uniform sampler2D uLfLut;   // R = alpha(q), G = d(alpha)/dq. Built by lensForward.cpp
                            // by integrating the SAME null geodesic the ray tracer
                            // marches, so the raster ring and the geodesic ring
                            // land in the same place by construction.

// alpha(b) alone. The fragment path runs this millions of times a frame and
// never uses the derivative, so it does not pay for it.
float lfAlphaValue(float bR) {
    float u = LF_BCRIT / bR;
    if (u >= 1.0) return 1e8;
    return texture(uLfLut, vec2(clamp(-log(1.0 - u) / LF_QMAX, 0.0, 1.0), 0.5)).r;
}

// alpha(b) and d(alpha)/d(b/rs), from one fetch. b is in units of rs.
vec2 lfAlpha(float bR) {
    float u = LF_BCRIT / bR;
    if (u >= 1.0) return vec2(1e8, 0.0);            // captured
    float q = -log(1.0 - u);
    // The table is uniform in q = -ln(1 - b_c/b), NOT in b: alpha diverges
    // logarithmically at the photon sphere and is very nearly LINEAR in q there,
    // so this is what makes a 1024-tap table exact at both ends (measured:
    // 8e-7 rad worst interpolation error).
    vec2  t = texture(uLfLut, vec2(clamp(q / LF_QMAX, 0.0, 1.0), 0.5)).rg;
    return vec2(t.r, -t.g * u / (bR * (1.0 - u)));   // chain rule out to b
}

// Image angle -> source angle, and d(beta)/d(theta). Explicit, no iteration.
// This is the direction the FRAGMENT shader runs, once per pixel.
float lfBeta(float theta, float delta, float Dl, float rs, out float dbdt) {
    dbdt = 1.0;
    if (rs <= 0.0 || Dl <= 0.0) return theta;
    // Floor b away from zero. A fragment sitting exactly on the hole's screen
    // position has b = 0, where the weak-field alpha = 2rs/b is infinite while
    // the Born factor g goes as b^2 — and inf * 0 is NaN. NaN then fails every
    // `> 1.0` discard test (NaN comparisons are false), so the sprite drew its
    // ENTIRE square: the dark red boxes stacked over the hole. The product is
    // ~b and vanishes properly once b cannot reach zero.
    float b = max(Dl * sin(theta), 1e-9 * Dl);
    // Capture applies only to light that has to PASS the hole to reach us. A
    // source IN FRONT of the hole plane is reached before closest approach, so
    // its image may sit anywhere — including inside the shadow disc, which is
    // precisely how foreground matter covers the hole. An unconditional capture
    // test deleted every particle directly in front of the hole.
    if (delta > 0.0 && b <= LF_BCRIT * rs) return -1e30;
    float Ds = Dl + delta;
    if (Ds <= 0.0) return theta;                    // behind the camera: never drawn
    // Below capture the table has no value; only a foreground source gets here.
    vec2  adF   = lfAlpha(max(b / rs, LF_BCRIT * 1.0000001));   // completed encounter
    float aWeak = 2.0 * rs / b;                                  // Born, same b
    float h0    = sqrt(b * b + delta * delta);
    float fFar  = 0.5 * (1.0 + delta / h0);   // share of the encounter past closest approach
    // alpha(b) diverges at the photon sphere because a ray lingers and winds
    // there — a COMPLETED-encounter effect. Light from a source in FRONT never
    // goes around the hole, so it must not collect that enhancement; handing it
    // the full near-capture value made beta(theta) fold over and the solver
    // chase phantom roots. Weight the strong-field EXCESS by fFar: the far field
    // keeps its exact photon ring, the foreground reduces to the honest Born bend.
    vec2  ad = vec2(aWeak + (adF.x - aWeak) * fFar,
                    adF.y * fFar - (aWeak / b) * (1.0 - fFar));
    float h  = sqrt(b * b + delta * delta);
    float hl = sqrt(b * b + Dl * Dl);
    // The Born integral of the transverse pull, integrated TWICE (bend, then
    // lever arm) from the observer at -Dl to the source at delta:
    //
    //   beta = theta - alpha(b) * 0.5 * [ h + (Dl*delta - b^2)/hl ] / Ds
    //
    // The Dl*Ds/hl coupling inside that bracket is not optional: a first version
    // used (delta + h) + (Dl - hl), which looks equivalent and is not — it made
    // beta(theta) turn over and go negative, so the solver chased a phantom root
    // and flung particles across the screen as radial streaks.
    // For delta < 0 the direct form cancels to nothing in float32 and the
    // residual IS the answer, so factor out the b^2 every term carries.
    float S = (delta >= 0.0)
        ? (h + (Dl * delta - b * b) / hl)
        : (b * b * (1.0 / (h - delta) - (1.0 + delta / (Dl + hl)) / hl));
    float g = 0.5 * S / Ds;
    //   delta -> +inf : g -> delta/(Dl+delta) = D_ls/D_s   textbook thin lens
    //   delta  =  0   : g -> b/(2 Dl)                      HALF the bend — the source
    //                                                      sits level with the hole so
    //                                                      only the outgoing half of
    //                                                      the encounter happens. The
    //                                                      thin lens says ZERO here,
    //                                                      which is why it cannot draw
    //                                                      an accretion disc at 3-20 rs.
    //   delta -> -inf : g -> 0                             in front: light never passes
    //                                                      the hole, so it does not bend
    //                                                      and it COVERS the shadow.
    float dbdtheta = Dl * cos(theta);
    float dSdb = b / h - 2.0 * b / hl - b * (Dl * delta - b * b) / (hl * hl * hl);
    dbdt = 1.0 - ((ad.y / rs) * g + ad.x * 0.5 * dSdb / Ds) * dbdtheta;
    return theta - ad.x * g;
}

float lfBeta(float theta, float delta, float Dl, float rs) {
    float d;
    return lfBeta(theta, delta, Dl, rs, d);
}

// Weak-field closed form — the solver's starting point, and already exact
// wherever the field is weak (which is most of any scene).
float lfWeakGuess(float betaTrue, float delta, float Dl, float rs, int branch) {
    float dls = max(delta, 0.0);
    float ds  = max(Dl + delta, 1e-30);
    float tE  = sqrt(max(2.0 * rs * dls / (Dl * ds), 0.0));
    float d   = sqrt(betaTrue * betaTrue + 4.0 * tE * tE);
    return (branch == 0) ? 0.5 * (betaTrue + d) : 0.5 * (d - betaTrue);
}

// Source angle -> image angle. branch 0 = primary, 1 = secondary.
// Returns false when this source has NO image on that branch: beta(theta) does
// not run to -infinity at the photon angle (the table stops a little past one
// winding), so a source close behind a hole can want a deeper image than is
// modelled. Those sit within a hair of the photon ring and are demagnified far
// below a pixel — culling them is right, and returning a bogus root is what the
// first version did.
bool lfSolve(float betaTrue, float delta, float Dl, float rs, int branch, out float theta) {
    theta = betaTrue;
    if (rs <= 0.0 || Dl <= 0.0) return true;
    // asin, not the small-angle b_c*rs/Dl: lfBeta uses b = Dl*sin(theta), and a
    // small-angle photon angle sits just INSIDE the capture radius.
    float thPh   = asin(min(1.0, LF_BCRIT * rs / Dl));
    float target = (branch == 0) ? betaTrue : -betaTrue;
    // Behind the hole: no image inside the photon angle. In front: any angle.
    float lo     = (delta > 0.0) ? (thPh * (1.0 + 1e-7) + 1e-30) : 0.0;
    // strict: target == betaMin is a source dead in front of the hole, which
    // must be KEPT — it is what covers the shadow.
    if (target < lfBeta(lo, delta, Dl, rs)) return false;

    // beta(theta) is monotonic only while b = Dl*sin(theta) is, so the domain
    // stops just short of a right angle. Past pi/2 sin turns over, every
    // evaluation returns the capture sentinel, and the doubling loop walks off
    // to hundreds of radians — which threw particles near the hole's axis right
    // across the screen as radial streaks.
    const float LF_HIMAX = 1.5533;   // ~89 degrees
    float hi = min(LF_HIMAX, max(betaTrue, 0.0)
             + 8.0 * max(lfWeakGuess(betaTrue, delta, Dl, rs, 0), thPh) + 20.0 * thPh);
    for (int i = 0; i < 24; ++i) {
        if (lfBeta(hi, delta, Dl, rs) >= target) break;
        if (hi >= LF_HIMAX) { theta = betaTrue; return false; }   // leave it unlensed
        hi = min(hi * 2.0, LF_HIMAX);
    }
    {
        float tw = lfWeakGuess(betaTrue, delta, Dl, rs, branch);
        if (delta > 0.0) tw = max(tw, thPh * (1.0 + 1e-6));
        if (tw > lo && tw < hi
            && abs(lfBeta(tw, delta, Dl, rs) - target) * uLfPxPerRad < 0.02) { theta = tw; return true; }
    }
    // BISECTION, not Newton. This is a Born-profile model and in the deep
    // near-field it can FOLD; where it folds there are several roots and each is
    // a legitimate image, but a fold breaks the bracket invariant a Newton
    // safeguard depends on and returns answers tens of pixels wrong. Bisection
    // converges to one of the roots and cannot be misled by a derivative that
    // disagrees with the function. 24 halvings is 0.007 px worst over the whole
    // bh_disk geometry, nothing culled — and only particles the early-out has
    // already judged worth bending ever get here.
    for (int i = 0; i < 24; ++i) {
        float mid = 0.5 * (lo + hi);
        if (lfBeta(mid, delta, Dl, rs) < target) lo = mid; else hi = mid;
    }
    theta = 0.5 * (lo + hi);
    // A LENS MUST NEVER DELETE MATTER: the only honest "no image" is the betaMin
    // test above. Treating slow convergence as absence culled ~9% of the direct
    // images — thousands of particles blinking out around the hole.
    return true;
}

// ---------------------------------------------------------------------------
// Placement. Called once per source (particle, dust puff, mesh vertex).
// ---------------------------------------------------------------------------

uniform int   uLfCount;          // resolvable holes; 0 = lens off (everything below is skipped)
uniform vec3  uLfHoleDirV[4];    // unit camera->hole, VIEW axes
uniform vec3  uLfHoleDirW[4];    // unit camera->hole, WORLD axes
uniform float uLfHoleDist[4];    // Dl (AU)
uniform float uLfHoleRs[4];      // rs (AU)
uniform float uLfDelta0[4];      // PER OBJECT: (object centre - hole) . holeDir, in double on the CPU
uniform float uLfMaxMu;          // cap on how far a sprite may be stretched (fill-rate guard)
uniform float uLfMaxSprite;      // largest lensed sprite, as a fraction of viewport height.
                                 // The single strongest speed dial: cost is dominated by a few
                                 // very large magnified sprites and their area is quadratic,
                                 // so halving this nearly halves the lens's frame time.

// Filled by lfPlace, consumed by the caller and forwarded to the fragment shader.
// Screen space here means aspect-corrected NDC, the frame in which a square
// point sprite is square: S = (ndc.x * aspect, ndc.y), so one unit is half the
// viewport height in both axes.
bool  gLfActive;    // false = this source was not bent at all
float gLfThetaS;    // this image's angle from the hole (radians)
float gLfBetaS;     // signed source angle there: +beta direct, -beta secondary
vec2  gLfSrcS;      // where the source would sit WITHOUT the lens, screen space
vec2  gLfCenterS;   // where the image is drawn, screen space
vec3  gLfHoleN;     // owner hole's unit direction, view space
vec3  gLfGeom;      // (delta, Dl, rs) of the hole that owns this image
float gLfMuT;       // tangential magnification (the stretch along the arc)
float gLfMuR;       // radial magnification

// Rotate d away from nHat by dTheta, in the plane the two of them span. Exact —
// no small-angle assumption — so an off-axis hole and a wide FOV are handled
// correctly, and this is what decides where the ring lands.
vec3 lfRotateAway(vec3 d, vec3 nHat, float dTheta) {
    float c  = dot(d, nHat);
    vec3  t  = d - nHat * c;
    float st = length(t);
    if (st < 1e-20) return d;                    // exactly on the axis: no unique plane
    float th = atan(st, c) + dTheta;             // atan2 form, not acos: exact for tiny beta
    return nHat * cos(th) + (t / st) * sin(th);
}

// image: 0 = primary (every hole bends it), 1..uLfCount = the secondary image of
// hole (image-1). Returns false when this source produces no image here, in
// which case the caller must drop the vertex.
bool lfPlace(inout vec4 clipPos, vec3 offW, float fx, float fy, int image) {
    gLfActive = false;
    gLfThetaS = 0.0; gLfBetaS = 0.0; gLfSrcS = vec2(0.0); gLfCenterS = vec2(0.0);
    gLfHoleN  = vec3(0.0, 0.0, -1.0); gLfGeom = vec3(0.0); gLfMuT = 1.0; gLfMuR = 1.0;
    if (uLfCount <= 0 || clipPos.w <= 0.0) return image == 0;

    vec2 T0 = vec2(clipPos.x / clipPos.w / fx, clipPos.y / clipPos.w / fy);  // tan-space
    vec3 d  = normalize(vec3(T0, -1.0));                                     // view-space direction
    vec3 dOut = d;
    bool  haveOwner = false;
    int   ownerIdx  = 0;
    float ownerBend = -1.0;

    for (int h = 0; h < uLfCount; ++h) {
        float rs = uLfHoleRs[h];
        float Dl = uLfHoleDist[h];
        if (rs <= 0.0 || Dl <= 0.0) continue;
        vec3  nH = uLfHoleDirV[h];
        float c  = dot(d, nH);
        vec3  tv = d - nH * c;
        float beta = atan(length(tv), c);
        // delta in DOUBLE-derived pieces: the object centre's offset from the
        // hole comes from the CPU (uLfDelta0), the particle's own offset is
        // small and safe in float. Forming (axial distance - Dl) directly would
        // subtract two ~1e6 AU numbers in float32 and leave nothing — which is
        // exactly the near-hole term an accretion disc depends on.
        float delta = uLfDelta0[h] + dot(offW, uLfHoleDirW[h]);

        bool wantSecondary = (image == h + 1);
        // Skip the work where it cannot show: the largest displacement this hole
        // can produce for this source is bounded, and below a fraction of a
        // pixel the map IS the identity. This is what keeps a galaxy with a
        // distant hole at full speed.
        if (!wantSecondary) {
            // The displacement this hole would actually produce, to first order:
            // evaluate the map AT the source's own angle. One table fetch.
            //
            // This must NOT use the thin-lens Einstein angle. That is
            // proportional to delta and therefore ~0 for matter level with the
            // hole — which is exactly where the Born term is large, so a
            // thin-lens gate skipped every particle of an accretion disc and
            // left the disc unbent while distant matter moved.
            float disp = abs(beta - lfBeta(beta, delta, Dl, rs));
            if (disp * uLfPxPerRad < 0.05) continue;
        } else {
            // The secondary image of a source well outside the Einstein radius
            // sits at ~thetaE^2/beta, hard against the photon ring, and is
            // demagnified as (thetaE/beta)^4. Below half a pixel it is inside
            // the shadow and contributes nothing, so do not pay 24 bisection
            // steps to place it — this is most of the second pass's cost in any
            // scene where the hole is small on screen.
            float dls = max(delta, 0.0);
            float ds  = max(Dl + delta, 1e-30);
            float tE2 = 2.0 * rs * dls / (Dl * ds);
            if ((tE2 / max(beta, 1e-20)) * uLfPxPerRad < 0.5) return false;
        }

        float theta;
        if (!lfSolve(beta, delta, Dl, rs, wantSecondary ? 1 : 0, theta)) {
            if (wantSecondary) return false;                       // culled: photon-ring image
            continue;
        }
        dOut = lfRotateAway(dOut, nH, theta - beta);
        // The hole that bends this source the most owns the fragment-level map.
        // A source is essentially never near two Einstein rings at once, so the
        // others only need to displace the sprite, not reshape it.
        if (!haveOwner || abs(theta - beta) > ownerBend || wantSecondary) {
            haveOwner = true;
            ownerIdx  = h;
            ownerBend = abs(theta - beta);
            gLfThetaS = theta;
            gLfHoleN  = nH;
            gLfGeom   = vec3(delta, Dl, rs);
            float dbdt;
            gLfBetaS  = lfBeta(theta, delta, Dl, rs, dbdt);
            // TRUE magnifications, not clamped here. The stretch budget is spent
            // in the sizing code by shrinking the source, which keeps the
            // footprint and the fragment shader's source-disc test consistent;
            // clamping them here made the two disagree and filled sprites in.
            gLfMuT    = (abs(gLfBetaS) > 1e-20) ? abs(theta / gLfBetaS) : 1e6;
            gLfMuR    = (abs(dbdt)     > 1e-20) ? abs(1.0 / dbdt)       : 1e6;
        }
        if (wantSecondary) break;
    }
    if (image != 0 && !haveOwner) return false;                    // no secondary for this source
    if (!haveOwner) return true;                                   // nothing bent it: unchanged

    // Project the bent direction back to clip space. Depth (z, w) is untouched,
    // so a lensed sprite keeps its true distance and solids still occlude it.
    vec2 Tn = dOut.xy / max(-dOut.z, 1e-30);
    clipPos.xy = vec2(Tn.x * fx, Tn.y * fy) * clipPos.w;

    // Screen-space anchors for the fragment shader. It reconstructs each pixel's
    // own view direction from these and runs the map exactly — no linearisation
    // of the angle or the azimuth. An earlier version passed a radial direction
    // and a single radians-per-pixel scale and linearised the azimuth as dt/th;
    // that is fine for a small sprite and complete nonsense for a 500 px one
    // beside a 95 px photon radius, which is what filled the frame with straight
    // bright wedges instead of arcs.
    gLfSrcS    = T0 * fy;     // where this source would be with no lens
    gLfCenterS = Tn * fy;     // where its image is drawn
    gLfActive  = true;
    return true;
}


// ---------------------------------------------------------------------------
// The FRAGMENT-side map. Same physics as lfBeta, arranged so a pixel never pays
// for anything it does not use.
//
// theta is never formed. Everything that matters depends on sin(theta), and a
// fragment already HAS it: d and n are unit vectors, so c = dot(d,n) is cos and
// |d - n c| is sin. The source direction then follows from the DEFLECTION alone
// through the angle-difference identity
//     n cos(theta-D) + tHat sin(theta-D) = d cos D + (n sin - tHat cos) sin D
// so one sin/cos of the small deflection replaces an atan and two more
// transcendentals, and the unused d(beta)/d(theta) disappears with them.
// ---------------------------------------------------------------------------
bool lfSourceDir(vec3 d, vec3 n, float c, float st, vec3 tHat,
                 float delta, float Dl, float rs, out vec3 sdir, out bool primary) {
    sdir = d; primary = true;
    if (rs <= 0.0 || Dl <= 0.0) return true;
    float b = max(Dl * st, 1e-9 * Dl);              // b = Dl sin(theta), floored (inf*0 = NaN)
    if (delta > 0.0 && b <= LF_BCRIT * rs) return false;   // captured (sources BEHIND only)
    float Ds = Dl + delta;
    if (Ds <= 0.0) return false;
    float aFull = lfAlphaValue(max(b / rs, LF_BCRIT * 1.0000001));
    float aWeak = 2.0 * rs / b;
    float h     = sqrt(b * b + delta * delta);
    float fFar  = 0.5 * (1.0 + delta / h);
    float a     = aWeak + (aFull - aWeak) * fFar;
    float hl    = sqrt(b * b + Dl * Dl);
    float S     = (delta >= 0.0)
                ? (h + (Dl * delta - b * b) / hl)
                : (b * b * (1.0 / (h - delta) - (1.0 + delta / (Dl + hl)) / hl));
    float D  = a * 0.5 * S / Ds;                    // the deflection this source receives
    float cd = cos(D), sd = sin(D);
    sdir     = d * cd + (n * st - tHat * c) * sd;   // = n cos(beta) + tHat sin(beta)
    // The branch is the SIGN of beta = theta - D, and sin(beta) is not a safe
    // proxy: for beta in (-2pi,-pi) — deeply wound fragments at the photon ring —
    // the sine flips positive while beta stays negative. cos is monotone on
    // [0,pi] and theta lives there, so theta > D is exactly c < cos(D), and past
    // pi no theta can exceed D. Free: cos(D) is already in hand.
    primary  = (D <= 3.14159265) && (c < cd);
    return true;
}
