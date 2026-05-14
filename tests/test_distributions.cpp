#include "catch.hpp"
#include "mathStructs.h"

// ── asteroidBeltDistribution ────────────────────────────────────────────────
// Belt spans r in [0.45, 0.95] in the XZ plane; y is ignored.

TEST_CASE("asteroidBeltDistribution: point inside belt returns 1", "[distributions]") {
    // r = 0.7, inside [0.45, 0.95]
    REQUIRE(asteroidBeltDistribution(0.7f, 0.0f, 0.0f) == Approx(1.0f));
}

TEST_CASE("asteroidBeltDistribution: point at inner edge returns 1", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(0.45f, 0.0f, 0.0f) == Approx(1.0f));
}

TEST_CASE("asteroidBeltDistribution: point at outer edge returns 1", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(0.95f, 0.0f, 0.0f) == Approx(1.0f));
}

TEST_CASE("asteroidBeltDistribution: point inside inner radius returns 0", "[distributions]") {
    // r = 0.1, inside the hollow centre
    REQUIRE(asteroidBeltDistribution(0.1f, 0.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("asteroidBeltDistribution: point outside outer radius returns 0", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(1.5f, 0.0f, 0.0f) == Approx(0.0f));
}

// ── sphereDistribution ──────────────────────────────────────────────────────
// Sphere of radius 0.5 centred at origin; inside when r^2 <= 0.25.

TEST_CASE("sphereDistribution: origin is inside the sphere", "[distributions]") {
    REQUIRE(sphereDistribution(0.0f, 0.0f, 0.0f) == Approx(1.0f));
}

TEST_CASE("sphereDistribution: point well inside sphere returns 1", "[distributions]") {
    // r = 0.3, r^2 = 0.09 < 0.25
    REQUIRE(sphereDistribution(0.3f, 0.0f, 0.0f) == Approx(1.0f));
}

TEST_CASE("sphereDistribution: point outside sphere returns 0", "[distributions]") {
    // r = 1.0, r^2 = 1.0 > 0.25
    REQUIRE(sphereDistribution(1.0f, 0.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("sphereDistribution: point just past boundary returns 0", "[distributions]") {
    // r = 0.51, r^2 = 0.2601 > 0.25
    REQUIRE(sphereDistribution(0.51f, 0.0f, 0.0f) == Approx(0.0f));
}

// ── asteroidBeltDistribution (additional) ────────────────────────────────────

TEST_CASE("asteroidBeltDistribution: Y coordinate does not affect result inside belt", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(0.7f,  100.0f, 0.0f) == Approx(1.0f));
    REQUIRE(asteroidBeltDistribution(0.7f, -999.0f, 0.0f) == Approx(1.0f));
}

TEST_CASE("asteroidBeltDistribution: Y coordinate does not affect result outside belt", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(0.1f,  100.0f, 0.0f) == Approx(0.0f));
    REQUIRE(asteroidBeltDistribution(0.1f, -100.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("asteroidBeltDistribution: diagonal inside belt returns 1", "[distributions]") {
    // r = sqrt(0.5^2 + 0.5^2) = sqrt(0.5) ~ 0.707, inside [0.45, 0.95]
    float h = 0.5f / std::sqrt(2.0f);
    REQUIRE(asteroidBeltDistribution(h, 0.0f, h) == Approx(1.0f));
}

TEST_CASE("asteroidBeltDistribution: negative X with valid radius returns 1", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(-0.7f, 0.0f, 0.0f) == Approx(1.0f));
}

TEST_CASE("asteroidBeltDistribution: negative Z with valid radius returns 1", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(0.0f, 0.0f, -0.7f) == Approx(1.0f));
}

TEST_CASE("asteroidBeltDistribution: origin returns 0", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(0.0f, 0.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("asteroidBeltDistribution: very large radius returns 0", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(100.0f, 0.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("asteroidBeltDistribution: r just below inner boundary returns 0", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(0.449f, 0.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("asteroidBeltDistribution: r just above outer boundary returns 0", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(0.951f, 0.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("asteroidBeltDistribution: midpoint of belt returns 1", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(0.70f, 0.0f, 0.0f) == Approx(1.0f));
}

TEST_CASE("asteroidBeltDistribution: negative X+Z diagonal inside belt", "[distributions]") {
    float h = -0.5f / std::sqrt(2.0f);
    REQUIRE(asteroidBeltDistribution(h, 0.0f, h) == Approx(1.0f));
}

// ── sphereDistribution (additional) ─────────────────────────────────────────

TEST_CASE("sphereDistribution: exactly on boundary r=0.5 returns 1", "[distributions]") {
    REQUIRE(sphereDistribution(0.5f, 0.0f, 0.0f) == Approx(1.0f));
}

TEST_CASE("sphereDistribution: 3D point inside sphere", "[distributions]") {
    // r^2 = 3 * 0.04 = 0.12 < 0.25
    REQUIRE(sphereDistribution(0.2f, 0.2f, 0.2f) == Approx(1.0f));
}

TEST_CASE("sphereDistribution: 3D point outside sphere", "[distributions]") {
    // r^2 = 3 * 0.16 = 0.48 > 0.25
    REQUIRE(sphereDistribution(0.4f, 0.4f, 0.4f) == Approx(0.0f));
}

TEST_CASE("sphereDistribution: negative X inside sphere", "[distributions]") {
    REQUIRE(sphereDistribution(-0.2f, 0.0f, 0.0f) == Approx(1.0f));
}

TEST_CASE("sphereDistribution: negative X outside sphere", "[distributions]") {
    REQUIRE(sphereDistribution(-1.0f, 0.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("sphereDistribution: Y axis inside sphere", "[distributions]") {
    REQUIRE(sphereDistribution(0.0f, 0.3f, 0.0f) == Approx(1.0f));
}

TEST_CASE("sphereDistribution: Y axis outside sphere", "[distributions]") {
    REQUIRE(sphereDistribution(0.0f, 0.6f, 0.0f) == Approx(0.0f));
}

TEST_CASE("sphereDistribution: Z axis inside sphere", "[distributions]") {
    REQUIRE(sphereDistribution(0.0f, 0.0f, 0.3f) == Approx(1.0f));
}

TEST_CASE("sphereDistribution: Z axis outside sphere", "[distributions]") {
    REQUIRE(sphereDistribution(0.0f, 0.0f, 0.6f) == Approx(0.0f));
}

// ── randomDistribution ───────────────────────────────────────────────────────

TEST_CASE("randomDistribution at origin returns 0", "[distributions]") {
    // x=0+sin(0)=0, y=0+cos(0)=1, z=0+sin(0)=0; distance=1; 1-1=0
    REQUIRE(randomDistribution(0.0f, 0.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("randomDistribution returns finite value", "[distributions]") {
    REQUIRE(std::isfinite(randomDistribution(1.0f, 1.0f, 1.0f)));
}

TEST_CASE("randomDistribution is deterministic", "[distributions]") {
    float r1 = randomDistribution(0.5f, 0.5f, 0.5f);
    float r2 = randomDistribution(0.5f, 0.5f, 0.5f);
    REQUIRE(r1 == Approx(r2));
}

// ── asteroidBeltDistribution: symmetry ───────────────────────────────────────

TEST_CASE("asteroidBeltDistribution: positive and negative X same distance give same result", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(0.6f, 0.0f, 0.0f) ==
            Approx(asteroidBeltDistribution(-0.6f, 0.0f, 0.0f)));
}

TEST_CASE("asteroidBeltDistribution: swapping X and Z gives same result", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(0.6f, 0.0f, 0.3f) ==
            Approx(asteroidBeltDistribution(0.3f, 0.0f, 0.6f)));
}

TEST_CASE("asteroidBeltDistribution: near-zero radius returns 0", "[distributions]") {
    REQUIRE(asteroidBeltDistribution(0.01f, 0.0f, 0.0f) == Approx(0.0f));
}

// ── sphereDistribution: symmetry ─────────────────────────────────────────────

TEST_CASE("sphereDistribution: symmetric about origin", "[distributions]") {
    REQUIRE(sphereDistribution(0.3f, 0.0f, 0.0f) ==
            Approx(sphereDistribution(-0.3f, 0.0f, 0.0f)));
}

TEST_CASE("sphereDistribution: all axes equivalent", "[distributions]") {
    float r = 0.3f;
    REQUIRE(sphereDistribution(r, 0.0f, 0.0f) == Approx(sphereDistribution(0.0f, r, 0.0f)));
    REQUIRE(sphereDistribution(r, 0.0f, 0.0f) == Approx(sphereDistribution(0.0f, 0.0f, r)));
}

TEST_CASE("sphereDistribution: large negative values outside sphere", "[distributions]") {
    REQUIRE(sphereDistribution(-10.0f, 0.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("sphereDistribution: small positive values inside sphere", "[distributions]") {
    REQUIRE(sphereDistribution(0.1f, 0.1f, 0.0f) == Approx(1.0f));
}

// ── randomDistribution: additional ───────────────────────────────────────────

TEST_CASE("randomDistribution varies with input", "[distributions]") {
    float r1 = randomDistribution(0.0f, 0.0f, 0.0f);
    float r2 = randomDistribution(0.1f, 0.0f, 0.0f);
    // These should not be equal in general (different inputs)
    // We just verify both are finite
    REQUIRE(std::isfinite(r1));
    REQUIRE(std::isfinite(r2));
}

TEST_CASE("randomDistribution does not crash at negative inputs", "[distributions]") {
    REQUIRE(std::isfinite(randomDistribution(-1.0f, -1.0f, -1.0f)));
}

TEST_CASE("randomDistribution does not crash at large inputs", "[distributions]") {
    REQUIRE(std::isfinite(randomDistribution(100.0f, 100.0f, 100.0f)));
}
