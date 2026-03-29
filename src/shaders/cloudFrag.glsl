#version 460 core
out vec4 FragColor;

uniform float uTemperature; // Kelvin (0 = default warm grey)
uniform int   uRenderMode;  // 0 = Point, 1 = Nebula

vec3 blackbody(float T) {
    T = clamp(T, 1000.0, 40000.0);
    float t = T / 1000.0;
    float r, g, b;
    if (T <= 6600.0) r = 1.0;
    else r = clamp(1.2929362 * pow(t - 6.0, -0.1332047592), 0.0, 1.0);
    if (T <= 6600.0) g = clamp(0.39008157876 * log(t) - 0.63184144378, 0.0, 1.0);
    else g = clamp(1.1298908609 * pow(t - 6.0, -0.0755148492), 0.0, 1.0);
    if (T >= 6600.0) b = 1.0;
    else if (T <= 1900.0) b = 0.0;
    else b = clamp(0.54320678911 * log(t - 1.0) - 1.19625408914, 0.0, 1.0);
    return vec3(r, g, b);
}

void main() {
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
