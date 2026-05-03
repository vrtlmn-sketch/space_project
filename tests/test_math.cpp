#include "catch.hpp"
#include "mathStructs.h"

TEST_CASE("vec3 addition", "[math]") {
    vec3 a{1.0f, 2.0f, 3.0f};
    vec3 b{4.0f, 5.0f, 6.0f};
    vec3 c = a + b;
    REQUIRE(c.x == Approx(5.0f));
    REQUIRE(c.y == Approx(7.0f));
    REQUIRE(c.z == Approx(9.0f));
}

TEST_CASE("vec3 subtraction", "[math]") {
    vec3 a{5.0f, 7.0f, 9.0f};
    vec3 b{1.0f, 2.0f, 3.0f};
    vec3 c = a - b;
    REQUIRE(c.x == Approx(4.0f));
    REQUIRE(c.y == Approx(5.0f));
    REQUIRE(c.z == Approx(6.0f));
}

TEST_CASE("vec3 scalar multiplication", "[math]") {
    vec3 v{1.0f, 2.0f, 3.0f};
    vec3 r = v * 3.0f;
    REQUIRE(r.x == Approx(3.0f));
    REQUIRE(r.y == Approx(6.0f));
    REQUIRE(r.z == Approx(9.0f));
}

TEST_CASE("vec3 scalar division", "[math]") {
    vec3 v{6.0f, 9.0f, 12.0f};
    vec3 r = v / 3.0f;
    REQUIRE(r.x == Approx(2.0f));
    REQUIRE(r.y == Approx(3.0f));
    REQUIRE(r.z == Approx(4.0f));
}

TEST_CASE("vec3 += operator accumulates correctly", "[math]") {
    vec3 a{1.0f, 2.0f, 3.0f};
    a += vec3{10.0f, 20.0f, 30.0f};
    REQUIRE(a.x == Approx(11.0f));
    REQUIRE(a.y == Approx(22.0f));
    REQUIRE(a.z == Approx(33.0f));
}

TEST_CASE("vec3 equality operator", "[math]") {
    vec3 a{1.0f, 2.0f, 3.0f};
    vec3 b{1.0f, 2.0f, 3.0f};
    vec3 c{1.0f, 2.0f, 4.0f};
    REQUIRE(a == b);
    REQUIRE_FALSE(a == c);
}

TEST_CASE("distance: 3-4-5 right triangle", "[math]") {
    vec3 a{0.0f, 0.0f, 0.0f};
    vec3 b{3.0f, 4.0f, 0.0f};
    REQUIRE(distance(a, b) == Approx(5.0f));
}

TEST_CASE("getLength of a known vector", "[math]") {
    // {1, 2, 2} has length sqrt(1+4+4) = 3
    vec3 v{1.0f, 2.0f, 2.0f};
    REQUIRE(getLength(v) == Approx(3.0f));
}

TEST_CASE("normalize produces a unit vector", "[math]") {
    vec3 v{0.0f, 3.0f, 4.0f};
    vec3 n = normalize(v);
    REQUIRE(getLength(n) == Approx(1.0f).epsilon(0.001));
    REQUIRE(n.x == Approx(0.0f));
    REQUIRE(n.y == Approx(0.6f));
    REQUIRE(n.z == Approx(0.8f));
}

TEST_CASE("normalize of zero vector returns zero without crash", "[math]") {
    vec3 zero{0.0f, 0.0f, 0.0f};
    vec3 n = normalize(zero);
    REQUIRE(n.x == Approx(0.0f));
    REQUIRE(n.y == Approx(0.0f));
    REQUIRE(n.z == Approx(0.0f));
}

TEST_CASE("rotate 90 degrees around Y axis", "[math]") {
    // +X rotated 90 degrees around Y should land on -Z
    vec3 v{1.0f, 0.0f, 0.0f};
    rotate(v, 90.0f);
    REQUIRE(v.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(v.y == Approx(0.0f));
    REQUIRE(v.z == Approx(-1.0f).margin(1e-5f));
}

TEST_CASE("rotate 0 degrees leaves vector unchanged", "[math]") {
    vec3 v{1.0f, 2.0f, 3.0f};
    rotate(v, 0.0f);
    REQUIRE(v.x == Approx(1.0f));
    REQUIRE(v.y == Approx(2.0f));
    REQUIRE(v.z == Approx(3.0f));
}
