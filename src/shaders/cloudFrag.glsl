#version 460 core
out vec4 FragColor;

uniform float uTemperature; // Kelvin (0 = default warm grey)
uniform int   uRenderMode;  // 0 = Point, 1 = Nebula
uniform int   uRealistic;   // 0 = nav look, 1 = Cinematic Performant (HDR, RT-like)
uniform int   uCloudPass;   // 0 = haze, 1 = core, 2 = dust glow, 3 = dust extinction
uniform float uDustReddening;

in vec3  vColor;            // per-particle blackbody colour (from cloudVert)
in float vMag;              // per-particle magnitude 0..1
in float vGlow;             // dust glow intensity (0 unless a dust sprite)

vec3 blackbody(float T) {
    T = clamp(T, 1000.0, 40000.0);
    float t = T / 100.0;
    float r, g, b;
    if (T <= 6600.0) r = 1.0;
    else r = clamp(1.2929362 * pow(t - 60.0, -0.1332047592), 0.0, 1.0);
    if (T <= 6600.0) g = clamp(0.39008157876 * log(t) - 0.63184144378, 0.0, 1.0);
    else g = clamp(1.1298908609 * pow(t - 60.0, -0.0755148492), 0.0, 1.0);
    if (T >= 6600.0) b = 1.0;
    else if (T <= 1900.0) b = 0.0;
    else b = clamp(0.54320678911 * log(t - 10.0) - 1.19625408914, 0.0, 1.0);
    return vec3(r, g, b);
}

void main() {
    // ── Realistic HDR path (Cinematic Performant): two additive passes ──
    // Pure-additive blended (GL_ONE). Core pass = tight bright dots (individual
    // stars, clip to white via bloom). Haze pass = wide faint sprites that
    // overlap into the continuous galactic "milk" (unresolved-star field).
    if (uRealistic != 0) {
        vec2  pc = gl_PointCoord * 2.0 - 1.0; // [-1,1]
        float r2 = dot(pc, pc);
        if (r2 > 1.0) discard;

        if (uCloudPass == 3) {
            // Extinction: output per-channel transmittance, multiplied into the
            // framebuffer (GL_ZERO, GL_SRC_COLOR). Blue absorbed more → reddening.
            // Dominant dust cue: dense overlap compounds toward dark red.
            float t = vGlow * 0.07 * exp(-r2 * 1.6);
            vec3 trans = exp(-t * vec3(1.0, 1.0 + 0.6 * uDustReddening,
                                            1.0 + 1.6 * uDustReddening));
            FragColor = vec4(trans, 1.0);
            return;
        }

        vec3 c;
        if (uCloudPass == 1) {
            // Softer, anti-aliased core: smooth Gaussian + a smoothstep edge fade
            // across the outer sprite ring so motion doesn't make it flicker.
            float core  = exp(-r2 * 3.5);
            float edge  = smoothstep(1.0, 0.5, r2);
            float coreI = 0.30 + 3.5 * vMag;   // bright stars punch to white
            c = vColor * core * edge * coreI;
        } else if (uCloudPass == 2) {
            // Dust in-scatter: subtle warm wash (NOT bright balls). Kept faint and
            // wide so it only tints, letting extinction be the dominant dust cue.
            float blob = exp(-r2 * 0.9);
            c = vColor * blob * vGlow * 0.25;
        } else {
            float halo = exp(-r2 * 1.4);
            c = vColor * halo * 0.05;          // faint; sums into smooth milk
        }
        FragColor = vec4(c, 1.0);
        return;
    }

    vec3 col;
    if (uTemperature > 100.0)
        col = blackbody(uTemperature);
    else
        col = vec3(0.75, 0.68, 0.55); // warm dusty grey fallback

    if (uRenderMode == 1) {
        // Nebula mode: soft circular glow with alpha falloff
        vec2 pc = gl_PointCoord * 2.0 - 1.0; // [-1, 1]
        float d = dot(pc, pc);
        if (d > 1.0) discard;
        float alpha = exp(-d * 3.0) * 0.6;
        FragColor = vec4(col, alpha);
    } else {
        // Point mode: solid opaque pixel
        FragColor = vec4(col, 1.0);
    }
}
