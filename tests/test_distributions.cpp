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
