#!/usr/bin/env python3
"""
Generate a REAL-SCALE Milky Way disk galaxy formation for blackholesim.

Unlike generate_milky_way.py (which emits tiny normalized world units),
this reproduces the geometry of milky_way_real_5k.json at real astronomical
scale — a 50,000 ly disc radius expressed in AU (1 ly = 63241 AU) — but at
a configurable, higher particle count so the rendered band can be denser.

  disc radius   ~ 50,000 ly  (~3.16e9 AU)
  disc height   ~  1,000 ly  (thin)
  rotation      ~ 46 AU/yr flat curve (rises through a small core)
  particleMass  = 1.0
"""

import json
import math
import random
import os

LY_TO_AU = 63241.0

# ── Geometry (fixed real scale, independent of particle count) ────────────────
DISK_RADIUS_LY = 50_000.0
DISK_HEIGHT_LY = 1_000.0
R_MAX          = DISK_RADIUS_LY * LY_TO_AU        # ~3.162e9 AU (hard truncation)
DISK_HEIGHT    = DISK_HEIGHT_LY * LY_TO_AU        # half-thickness in AU

# Exponential-disk radial profile: surface density ~ exp(-r/H) gives a radial
# PDF ~ r*exp(-r/H) == Gamma(2, H). H matches the 5K reference median
# (median = 1.678*H = 3.18e8 AU  ->  H ~ 1.9e8 AU ~ 3000 ly).
H_SCALE        = 1.895e8              # exponential scale length (AU) — inner/bulge
EXT_FRAC       = 0.15                 # fraction drawn from the extended outer disk
EXT_SCALE      = 4.5 * H_SCALE        # extended-disk scale length (fattens the tail)
R_WIND         = H_SCALE              # reference radius for spiral-arm winding

# ── Spiral arms (same shape as the 5K reference) ──────────────────────────────
NUM_ARMS       = 2
ARM_SPREAD     = 0.35
ARM_STRENGTH   = 0.6

# ── Rotation curve ────────────────────────────────────────────────────────────
V_FLAT         = 46.0                 # flat-curve speed (AU/yr)
R_CORE         = 3.0e7                # rise scale for the inner rotation curve (~475 ly)
VEL_SCATTER    = 0.5                  # AU/yr random scatter

PARTICLE_MASS  = 1.0
COUNT          = 20_000
SEED           = 42
OUT_NAME       = "milky_way_real_20k"
DESC           = ("Spiral galaxy at REAL scale: 50,000 ly disc radius in AU units "
                  "(1 ly = 63241 AU). Centre it on Sagittarius A*; the Sun then sits "
                  "inside the disc at 26,000 ly. Flat rotation curve ~46 AU/yr. 20K particles.")


# ── Star clusters / OB associations (localized knots along the arms) ──────────
CLUSTER_FRAC   = 0.18                 # fraction of stars bound into clusters
NUM_CLUSTERS   = 60
CLUSTER_SPREAD = 0.15 * H_SCALE       # cluster radius (AU)


def _field_position():
    """Sample one disk position (r, theta): two-component profile + spiral arms."""
    h = EXT_SCALE if random.random() < EXT_FRAC else H_SCALE
    while True:
        r = -h * (math.log(random.random()) + math.log(random.random()))
        if r <= R_MAX:
            break
    theta = random.uniform(0, 2 * math.pi)
    if ARM_STRENGTH > 0:
        wind = math.log(r / R_WIND + 0.01) * 2.5
        best_arm_dist = min(
            abs(((theta - 2 * math.pi * k / NUM_ARMS - wind) + math.pi) % (2 * math.pi) - math.pi)
            for k in range(NUM_ARMS)
        )
        arm_weight = math.exp(-0.5 * (best_arm_dist / ARM_SPREAD) ** 2)
        if random.random() > (1 - ARM_STRENGTH) + ARM_STRENGTH * arm_weight:
            nearest_arm = min(range(NUM_ARMS),
                key=lambda k: abs(((theta - 2*math.pi*k/NUM_ARMS - wind) + math.pi) % (2*math.pi) - math.pi))
            target = 2 * math.pi * nearest_arm / NUM_ARMS + wind
            theta = target + random.gauss(0, ARM_SPREAD * 0.5)
    return r, theta


def generate_particles(num_particles, seed=SEED):
    random.seed(seed)
    particles = []
    # Cluster centres sit on the same arm-modulated disk, so the knots are tied to
    # the galaxy's structure (star-forming regions), not scattered at random.
    clusters = []
    for _ in range(NUM_CLUSTERS):
        cr, ct = _field_position()
        clusters.append((cr * math.cos(ct), cr * math.sin(ct)))

    for _ in range(num_particles):
        # Most stars fill the disk; a fraction condense into compact clusters so
        # dense knots read as localized groups instead of continuous glitter.
        if clusters and random.random() < CLUSTER_FRAC:
            cx, cz = random.choice(clusters)
            x = cx + random.gauss(0, CLUSTER_SPREAD)
            z = cz + random.gauss(0, CLUSTER_SPREAD)
            r = math.hypot(x, z)
            theta = math.atan2(z, x)
        else:
            r, theta = _field_position()
            x = r * math.cos(theta)
            z = r * math.sin(theta)
        y = random.gauss(0, DISK_HEIGHT * (1 + 0.5 * (r / R_MAX)))

        # Flat rotation curve that rises through the core: v = V_FLAT * r/(r+R_CORE)
        v_circ = V_FLAT * (r / (r + R_CORE))
        vx = -v_circ * math.sin(theta)
        vz =  v_circ * math.cos(theta)
        vy = 0.0

        vx += random.gauss(0, VEL_SCATTER)
        vy += random.gauss(0, VEL_SCATTER * 0.3)
        vz += random.gauss(0, VEL_SCATTER)

        particles.append({
            "position": [round(x, 4), round(y, 4), round(z, 4)],
            "velocity": [round(vx, 4), round(vy, 4), round(vz, 4)],
            "acceleration": [0.0, 0.0, 0.0]
        })

    return particles


out_dir = os.path.dirname(os.path.abspath(__file__))
particles = generate_particles(COUNT)
formation = {
    "name": f"Milky Way (real scale) {COUNT // 1000}K",
    "description": DESC,
    "particleMass": PARTICLE_MASS,
    "particles": particles,
}
out_path = os.path.join(out_dir, f"{OUT_NAME}.json")
with open(out_path, "w") as f:
    json.dump(formation, f, separators=(",", ":"))

size_mb = os.path.getsize(out_path) / 1024 / 1024
print(f"Generated {COUNT:,} particles -> {OUT_NAME}.json  ({size_mb:.2f} MB, R_MAX={R_MAX:.3e} AU)")
