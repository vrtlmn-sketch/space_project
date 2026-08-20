// ---------------------------------------------------------------------------
// lensing_common.glsl — Schwarzschild geodesic physics + RK4 integrator.
//
// ONE definition of how light bends around a black hole, shared by every RT
// geodesic compute shader (cyclic + acyclic, plain + Doppler) and, later, the
// raster lensing pass. A second copy is how the two views would silently drift.
//
// The SCENE-specific accel is supplied by each includer through the
// geodesicAccel prototype below: the cyclic shaders superpose every hole in the
// objects[] SSBO, the acyclic shaders use their single uBHPos/uBH_RS, and the
// raster pass uses its own BH uniform array. evalDeriv/rk4Step call it through
// the prototype, so the integrator itself lives here exactly once.
// ---------------------------------------------------------------------------

// Exact Schwarzschild photon acceleration in Cartesian form. p is the photon
// position relative to the hole, v its velocity, rs the Schwarzschild radius:
//
//     a = -1.5 * rs * h^2 / r^5 * p ,   h = |cross(p, v)|,   r = |p|
//
// This gives the correct photon sphere at r = 1.5*rs and all lensing effects.
vec3 holeAccel(vec3 p, vec3 v, float rs)
{
    float r2 = dot(p, p);
    float r  = sqrt(r2);
    if (r < 0.001) return vec3(0.0);          // avoid the r = 0 singularity
    vec3  h_vec = cross(p, v);
    float h2    = dot(h_vec, h_vec);
    float r5 = r2 * r2 * r;
    return -1.5 * rs * h2 / r5 * p;
}

// Defined by each includer over its own scene representation.
vec3 geodesicAccel(vec3 pos, vec3 vel);

struct RayState { vec3 pos; vec3 vel; };
struct RayDeriv { vec3 dpos; vec3 dvel; };

RayDeriv evalDeriv(vec3 pos, vec3 vel)
{
    RayDeriv d;
    d.dpos = vel;
    d.dvel = geodesicAccel(pos, vel);
    return d;
}

RayState rk4Step(vec3 pos, vec3 vel, float dt)
{
    RayDeriv k1 = evalDeriv(pos, vel);
    vec3 p2 = pos + k1.dpos * (dt * 0.5);
    vec3 v2 = vel + k1.dvel * (dt * 0.5);
    RayDeriv k2 = evalDeriv(p2, v2);
    vec3 p3 = pos + k2.dpos * (dt * 0.5);
    vec3 v3 = vel + k2.dvel * (dt * 0.5);
    RayDeriv k3 = evalDeriv(p3, v3);
    vec3 p4 = pos + k3.dpos * dt;
    vec3 v4 = vel + k3.dvel * dt;
    RayDeriv k4 = evalDeriv(p4, v4);
    RayState result;
    result.pos = pos + (dt / 6.0) * (k1.dpos + 2.0 * k2.dpos + 2.0 * k3.dpos + k4.dpos);
    result.vel = vel + (dt / 6.0) * (k1.dvel + 2.0 * k2.dvel + 2.0 * k3.dvel + k4.dvel);
    return result;
}
