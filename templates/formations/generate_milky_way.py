#!/usr/bin/env python3
"""
Generate Milky Way-like disk galaxy formations for blackholesim.

Produces a range of sizes:
  milky_way_2k.json    —    2,000 particles
  milky_way_5k.json    —    5,000 particles
  milky_way_10k.json   —   10,000 particles
  milky_way_20k.json   —   20,000 particles
  milky_way_50k.json   —   50,000 particles
  milky_way_100k.json  —  100,000 particles
  milky_way_250k.json  —  250,000 particles
  milky_way_500k.json  —  500,000 particles
  milky_way_1m.json    —1,000,000 particles

Each is a thin disk with tangential velocities for approximately circular
orbits around a central mass. The central mass is NOT included — place a
heavy sun/black-hole at (0,0,-3) in the scene for the formation to orbit.

Physics parameters are matched to the simulator:
  G = 0.0001,  dt = 0.1,  central mass ~ 250 (adjustable in-sim)
"""

import json
import math
import random
import os

# ── Parameters ────────────────────────────────────────────────────────────────
PARTICLE_MASS   = 0.02       # uniform mass for all particles

# Disk geometry (world units)
R_MIN           = 0.15       # inner radius  (avoid singularity near center)
R_MAX           = 2.5        # outer radius
DISK_HEIGHT     = 0.06       # half-thickness of the disk (Y)

# Spiral arm perturbation
NUM_ARMS        = 2
ARM_SPREAD      = 0.35       # radians — width of each arm gaussian
ARM_STRENGTH    = 0.6        # 0 = uniform disk, 1 = only in arms

# Velocity scaling
# Cloud physics (corrected to match planet physics exactly):
#   accel = G * M_other / r²          (Newton: F/m = GM/r²)
#   vel  += dir * accel * dt
#   pos  += vel * dt
#
# Standard circular orbit: v²/r = GM/r²  →  v = sqrt(GM/r)
G_SIM           = 0.0001
M_CENTRAL       = 250.0        # Sun mass in the template scene

# Small random scatter added to velocities for realism
VEL_SCATTER     = 0.01

# Variants to generate
VARIANTS = [
    ("milky_way_2k",      2_000, "Milky Way 2K"),
    ("milky_way_5k",      5_000, "Milky Way 5K"),
    ("milky_way_10k",    10_000, "Milky Way 10K"),
    ("milky_way_20k",    20_000, "Milky Way 20K"),
    ("milky_way_50k",    50_000, "Milky Way 50K"),
    ("milky_way_100k",  100_000, "Milky Way 100K"),
    ("milky_way_250k",  250_000, "Milky Way 250K"),
    ("milky_way_500k",  500_000, "Milky Way 500K"),
    ("milky_way_1m",  1_000_000, "Milky Way 1M"),
]


def generate_particles(num_particles, seed=42):
    """Generate a list of particle dicts for a spiral galaxy disk."""
    random.seed(seed)
    particles = []

    for _ in range(num_particles):
        # Radial distribution: power-law biased toward inner radii
        u = random.random()
        r = R_MIN + (R_MAX - R_MIN) * (u ** 0.6)

        # Base angle — uniform
        theta = random.uniform(0, 2 * math.pi)

        # Spiral arm modulation
        if ARM_STRENGTH > 0:
            wind = math.log(r / R_MIN + 0.01) * 2.5
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

        # Position
        x = r * math.cos(theta)
        z = r * math.sin(theta)
        y = random.gauss(0, DISK_HEIGHT * (1 + 0.5 * (r / R_MAX)))

        # Tangential velocity for approximately circular orbit
        # v = sqrt(G * M / r)  (standard Newtonian circular orbit)
        v_circ = math.sqrt(G_SIM * M_CENTRAL / max(r, 0.01))
        vx = -v_circ * math.sin(theta)
        vz =  v_circ * math.cos(theta)
        vy = 0.0

        # Add scatter
        vx += random.gauss(0, VEL_SCATTER)
        vy += random.gauss(0, VEL_SCATTER * 0.3)
        vz += random.gauss(0, VEL_SCATTER)

        particles.append({
            "position": [round(x, 6), round(y, 6), round(z, 6)],
            "velocity": [round(vx, 6), round(vy, 6), round(vz, 6)],
            "acceleration": [0.0, 0.0, 0.0]
        })

    return particles


# ── Generate all variants ─────────────────────────────────────────────────────
out_dir = os.path.dirname(os.path.abspath(__file__))

for filename, count, desc in VARIANTS:
    particles = generate_particles(count)

    formation = {
        "name": desc,
        "description": f"Spiral galaxy with {count:,} particles. Place a heavy star (mass ~250) at the cloud origin for orbital motion.",
        "particleMass": PARTICLE_MASS,
        "particles": particles
    }

    out_path = os.path.join(out_dir, f"{filename}.json")
    with open(out_path, "w") as f:
        json.dump(formation, f, separators=(",", ":"))

    size_mb = os.path.getsize(out_path) / 1024 / 1024
    print(f"Generated {count:>9,} particles -> {filename}.json  ({size_mb:.1f} MB)")
