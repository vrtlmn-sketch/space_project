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

// ── vec3 -= ──────────────────────────────────────────────────────────────────

TEST_CASE("vec3 -= reduces components correctly", "[math]") {
    vec3 a{10.0f, 20.0f, 30.0f};
    a -= vec3{1.0f, 2.0f, 3.0f};
    REQUIRE(a.x == Approx(9.0f));
    REQUIRE(a.y == Approx(18.0f));
    REQUIRE(a.z == Approx(27.0f));
}

TEST_CASE("vec3 -= by zero leaves vector unchanged", "[math]") {
    vec3 a{5.0f, 6.0f, 7.0f};
    a -= vec3{0.0f, 0.0f, 0.0f};
    REQUIRE(a.x == Approx(5.0f));
    REQUIRE(a.y == Approx(6.0f));
    REQUIRE(a.z == Approx(7.0f));
}

TEST_CASE("vec3 -= itself gives zero", "[math]") {
    vec3 a{3.0f, 4.0f, 5.0f};
    a -= a;
    REQUIRE(a.x == Approx(0.0f));
    REQUIRE(a.y == Approx(0.0f));
    REQUIRE(a.z == Approx(0.0f));
}

// ── vec3 *= ──────────────────────────────────────────────────────────────────

TEST_CASE("vec3 *= scales all components", "[math]") {
    vec3 v{2.0f, 3.0f, 4.0f};
    v *= 2.0f;
    REQUIRE(v.x == Approx(4.0f));
    REQUIRE(v.y == Approx(6.0f));
    REQUIRE(v.z == Approx(8.0f));
}

TEST_CASE("vec3 *= by zero gives zero vector", "[math]") {
    vec3 v{5.0f, 5.0f, 5.0f};
    v *= 0.0f;
    REQUIRE(v.x == Approx(0.0f));
    REQUIRE(v.y == Approx(0.0f));
    REQUIRE(v.z == Approx(0.0f));
}

TEST_CASE("vec3 *= by 1 leaves vector unchanged", "[math]") {
    vec3 v{3.0f, 4.0f, 5.0f};
    v *= 1.0f;
    REQUIRE(v.x == Approx(3.0f));
    REQUIRE(v.y == Approx(4.0f));
    REQUIRE(v.z == Approx(5.0f));
}

TEST_CASE("vec3 *= by -1 negates all components", "[math]") {
    vec3 v{2.0f, -3.0f, 4.0f};
    v *= -1.0f;
    REQUIRE(v.x == Approx(-2.0f));
    REQUIRE(v.y == Approx(3.0f));
    REQUIRE(v.z == Approx(-4.0f));
}

// ── vec3 - vec4 ──────────────────────────────────────────────────────────────

TEST_CASE("vec3 minus vec4 subtracts xyz, ignores w", "[math]") {
    vec3 v{5.0f, 7.0f, 9.0f};
    vec4 u{1.0f, 2.0f, 3.0f, 99.0f};
    vec3 r = v - u;
    REQUIRE(r.x == Approx(4.0f));
    REQUIRE(r.y == Approx(5.0f));
    REQUIRE(r.z == Approx(6.0f));
}

TEST_CASE("vec3 minus vec4 with negative result", "[math]") {
    vec3 v{1.0f, 1.0f, 1.0f};
    vec4 u{3.0f, 4.0f, 5.0f, 0.0f};
    vec3 r = v - u;
    REQUIRE(r.x == Approx(-2.0f));
    REQUIRE(r.y == Approx(-3.0f));
    REQUIRE(r.z == Approx(-4.0f));
}

// ── vec3 == vec4 ─────────────────────────────────────────────────────────────

TEST_CASE("vec3 equals vec4 when xyz match", "[math]") {
    vec3 v{1.0f, 2.0f, 3.0f};
    vec4 u{1.0f, 2.0f, 3.0f, 0.0f};
    REQUIRE(v == u);
}

TEST_CASE("vec3 not equal to vec4 when xyz differ", "[math]") {
    vec3 v{1.0f, 2.0f, 3.0f};
    vec4 u{1.0f, 2.0f, 4.0f, 0.0f};
    REQUIRE_FALSE(v == u);
}

TEST_CASE("vec3 equals vec4 regardless of w value", "[math]") {
    vec3 v{1.0f, 2.0f, 3.0f};
    vec4 u1{1.0f, 2.0f, 3.0f, 0.0f};
    vec4 u2{1.0f, 2.0f, 3.0f, 999.0f};
    REQUIRE(v == u1);
    REQUIRE(v == u2);
}

// ── translate ────────────────────────────────────────────────────────────────

TEST_CASE("translate moves point by positive displacement", "[math]") {
    vec3 v{1.0f, 2.0f, 3.0f};
    vec3 r = translate(v, {4.0f, 5.0f, 6.0f});
    REQUIRE(r.x == Approx(5.0f));
    REQUIRE(r.y == Approx(7.0f));
    REQUIRE(r.z == Approx(9.0f));
}

TEST_CASE("translate by zero leaves point unchanged", "[math]") {
    vec3 v{3.0f, 4.0f, 5.0f};
    vec3 r = translate(v, {0.0f, 0.0f, 0.0f});
    REQUIRE(r.x == Approx(3.0f));
    REQUIRE(r.y == Approx(4.0f));
    REQUIRE(r.z == Approx(5.0f));
}

TEST_CASE("translate by negative displacement", "[math]") {
    vec3 v{5.0f, 5.0f, 5.0f};
    vec3 r = translate(v, {-3.0f, -4.0f, -5.0f});
    REQUIRE(r.x == Approx(2.0f));
    REQUIRE(r.y == Approx(1.0f));
    REQUIRE(r.z == Approx(0.0f));
}

TEST_CASE("translate does not modify the original vector", "[math]") {
    vec3 v{1.0f, 2.0f, 3.0f};
    translate(v, {10.0f, 10.0f, 10.0f});
    REQUIRE(v.x == Approx(1.0f));
    REQUIRE(v.y == Approx(2.0f));
    REQUIRE(v.z == Approx(3.0f));
}

TEST_CASE("translate from origin gives displacement vector", "[math]") {
    vec3 r = translate({0.0f, 0.0f, 0.0f}, {7.0f, 8.0f, 9.0f});
    REQUIRE(r.x == Approx(7.0f));
    REQUIRE(r.y == Approx(8.0f));
    REQUIRE(r.z == Approx(9.0f));
}

// ── perspectiveTransform ─────────────────────────────────────────────────────

TEST_CASE("perspectiveTransform divides x and y by z*90", "[math]") {
    vec3 v{90.0f, 90.0f, 1.0f};
    perspectiveTransform(v, 0.0f);
    REQUIRE(v.x == Approx(1.0f));
    REQUIRE(v.y == Approx(1.0f));
}

TEST_CASE("perspectiveTransform with z=2 halves result", "[math]") {
    vec3 v{90.0f, 180.0f, 2.0f};
    perspectiveTransform(v, 0.0f);
    REQUIRE(v.x == Approx(0.5f));
    REQUIRE(v.y == Approx(1.0f));
}

TEST_CASE("perspectiveTransform with z=0.5 doubles result", "[math]") {
    vec3 v{90.0f, 90.0f, 0.5f};
    perspectiveTransform(v, 0.0f);
    REQUIRE(v.x == Approx(2.0f));
    REQUIRE(v.y == Approx(2.0f));
}

// ── distance (additional) ────────────────────────────────────────────────────

TEST_CASE("distance between identical points is zero", "[math]") {
    vec3 a{3.0f, 4.0f, 5.0f};
    REQUIRE(distance(a, a) == Approx(0.0f));
}

TEST_CASE("distance is symmetric", "[math]") {
    vec3 a{1.0f, 2.0f, 3.0f};
    vec3 b{4.0f, 6.0f, 8.0f};
    REQUIRE(distance(a, b) == Approx(distance(b, a)));
}

TEST_CASE("distance along single X axis", "[math]") {
    REQUIRE(distance({0.0f,0.0f,0.0f}, {5.0f,0.0f,0.0f}) == Approx(5.0f));
}

TEST_CASE("distance along single Y axis", "[math]") {
    REQUIRE(distance({0.0f,0.0f,0.0f}, {0.0f,7.0f,0.0f}) == Approx(7.0f));
}

TEST_CASE("distance along single Z axis", "[math]") {
    REQUIRE(distance({0.0f,0.0f,0.0f}, {0.0f,0.0f,6.0f}) == Approx(6.0f));
}

TEST_CASE("distance with negative coordinates", "[math]") {
    REQUIRE(distance({-3.0f,0.0f,-4.0f}, {0.0f,0.0f,0.0f}) == Approx(5.0f));
}

TEST_CASE("distance between two negative points", "[math]") {
    REQUIRE(distance({-1.0f,-1.0f,-1.0f}, {-4.0f,-5.0f,-1.0f}) == Approx(5.0f));
}

TEST_CASE("distance is non-negative", "[math]") {
    REQUIRE(distance({5.0f,1.0f,2.0f}, {1.0f,5.0f,2.0f}) >= 0.0f);
}

// ── getLength (additional) ───────────────────────────────────────────────────

TEST_CASE("getLength of zero vector is zero", "[math]") {
    REQUIRE(getLength(vec3{0.0f, 0.0f, 0.0f}) == Approx(0.0f));
}

TEST_CASE("getLength of positive unit X vector is 1", "[math]") {
    REQUIRE(getLength(vec3{1.0f, 0.0f, 0.0f}) == Approx(1.0f));
}

TEST_CASE("getLength of positive unit Y vector is 1", "[math]") {
    REQUIRE(getLength(vec3{0.0f, 1.0f, 0.0f}) == Approx(1.0f));
}

TEST_CASE("getLength of positive unit Z vector is 1", "[math]") {
    REQUIRE(getLength(vec3{0.0f, 0.0f, 1.0f}) == Approx(1.0f));
}

TEST_CASE("getLength with negative components equals positive", "[math]") {
    REQUIRE(getLength(vec3{-3.0f, 4.0f, 0.0f}) == Approx(5.0f));
}

TEST_CASE("getLength scales with scalar multiplication", "[math]") {
    vec3 v{1.0f, 0.0f, 0.0f};
    REQUIRE(getLength(v * 7.0f) == Approx(7.0f));
}

TEST_CASE("getLength is equivalent to distance from origin", "[math]") {
    vec3 v{2.0f, 3.0f, 6.0f};
    REQUIRE(getLength(v) == Approx(distance(v, vec3{0.0f,0.0f,0.0f})));
}

// ── normalize (additional) ───────────────────────────────────────────────────

TEST_CASE("normalize X-axis vector gives unit X", "[math]") {
    vec3 n = normalize(vec3{5.0f, 0.0f, 0.0f});
    REQUIRE(n.x == Approx(1.0f));
    REQUIRE(n.y == Approx(0.0f));
    REQUIRE(n.z == Approx(0.0f));
}

TEST_CASE("normalize Y-axis vector gives unit Y", "[math]") {
    vec3 n = normalize(vec3{0.0f, 3.0f, 0.0f});
    REQUIRE(n.x == Approx(0.0f));
    REQUIRE(n.y == Approx(1.0f));
    REQUIRE(n.z == Approx(0.0f));
}

TEST_CASE("normalize Z-axis vector gives unit Z", "[math]") {
    vec3 n = normalize(vec3{0.0f, 0.0f, 8.0f});
    REQUIRE(n.x == Approx(0.0f));
    REQUIRE(n.y == Approx(0.0f));
    REQUIRE(n.z == Approx(1.0f));
}

TEST_CASE("normalize negative vector gives correct direction", "[math]") {
    vec3 n = normalize(vec3{-4.0f, 0.0f, 0.0f});
    REQUIRE(n.x == Approx(-1.0f));
    REQUIRE(n.y == Approx(0.0f));
    REQUIRE(n.z == Approx(0.0f));
}

TEST_CASE("normalize result has unit length", "[math]") {
    vec3 n = normalize(vec3{3.0f, 5.0f, 7.0f});
    REQUIRE(getLength(n) == Approx(1.0f).epsilon(0.001));
}

TEST_CASE("normalize of already-unit vector is unchanged", "[math]") {
    vec3 v{1.0f, 0.0f, 0.0f};
    vec3 n = normalize(v);
    REQUIRE(n.x == Approx(1.0f));
    REQUIRE(n.y == Approx(0.0f));
    REQUIRE(n.z == Approx(0.0f));
}

TEST_CASE("normalize twice gives same result as once", "[math]") {
    vec3 v{3.0f, 4.0f, 0.0f};
    vec3 n1 = normalize(v);
    vec3 n2 = normalize(n1);
    REQUIRE(n1.x == Approx(n2.x));
    REQUIRE(n1.y == Approx(n2.y));
    REQUIRE(n1.z == Approx(n2.z));
}

// ── rotate (additional) ──────────────────────────────────────────────────────

TEST_CASE("rotate 180 degrees flips X and zeroes Z", "[math]") {
    vec3 v{1.0f, 0.0f, 0.0f};
    rotate(v, 180.0f);
    REQUIRE(v.x == Approx(-1.0f).margin(1e-5f));
    REQUIRE(v.z == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("rotate 360 degrees returns to original", "[math]") {
    vec3 v{1.0f, 0.0f, 0.0f};
    rotate(v, 360.0f);
    REQUIRE(v.x == Approx(1.0f).margin(1e-5f));
    REQUIRE(v.z == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("rotate does not change Y component", "[math]") {
    vec3 v{1.0f, 5.0f, 0.0f};
    rotate(v, 90.0f);
    REQUIRE(v.y == Approx(5.0f));
}

TEST_CASE("rotate negative 90 degrees is inverse of positive 90", "[math]") {
    vec3 v{1.0f, 0.0f, 0.0f};
    rotate(v, -90.0f);
    REQUIRE(v.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(v.z == Approx(1.0f).margin(1e-5f));
}

TEST_CASE("two 90-degree rotations equal one 180-degree rotation", "[math]") {
    vec3 v1{1.0f, 0.0f, 0.0f};
    rotate(v1, 90.0f);
    rotate(v1, 90.0f);

    vec3 v2{1.0f, 0.0f, 0.0f};
    rotate(v2, 180.0f);

    REQUIRE(v1.x == Approx(v2.x).margin(1e-4f));
    REQUIRE(v1.z == Approx(v2.z).margin(1e-4f));
}

TEST_CASE("rotate 270 degrees is same as rotate -90 degrees", "[math]") {
    vec3 v1{1.0f, 0.0f, 0.0f};
    rotate(v1, 270.0f);

    vec3 v2{1.0f, 0.0f, 0.0f};
    rotate(v2, -90.0f);

    REQUIRE(v1.x == Approx(v2.x).margin(1e-4f));
    REQUIRE(v1.z == Approx(v2.z).margin(1e-4f));
}

// ── algebraic properties ─────────────────────────────────────────────────────

TEST_CASE("vec3 addition is commutative", "[math]") {
    vec3 a{1.0f, 2.0f, 3.0f};
    vec3 b{4.0f, 5.0f, 6.0f};
    REQUIRE(a + b == b + a);
}

TEST_CASE("scalar multiplication then division returns original", "[math]") {
    vec3 v{6.0f, 9.0f, 12.0f};
    vec3 r = (v * 5.0f) / 5.0f;
    REQUIRE(r.x == Approx(v.x));
    REQUIRE(r.y == Approx(v.y));
    REQUIRE(r.z == Approx(v.z));
}

TEST_CASE("addition then subtraction of same vector returns original", "[math]") {
    vec3 v{3.0f, 4.0f, 5.0f};
    vec3 d{1.0f, 2.0f, 3.0f};
    vec3 r = (v + d) - d;
    REQUIRE(r.x == Approx(v.x));
    REQUIRE(r.y == Approx(v.y));
    REQUIRE(r.z == Approx(v.z));
}

TEST_CASE("scalar multiply is associative: (s*t)*v == s*(t*v)", "[math]") {
    vec3 v{1.0f, 2.0f, 3.0f};
    vec3 lhs = (v * 2.0f) * 3.0f;
    vec3 rhs = v * 6.0f;
    REQUIRE(lhs.x == Approx(rhs.x));
    REQUIRE(lhs.y == Approx(rhs.y));
    REQUIRE(lhs.z == Approx(rhs.z));
}

TEST_CASE("normalize and scale preserves direction", "[math]") {
    vec3 v{3.0f, 4.0f, 0.0f};
    vec3 n = normalize(v);
    vec3 s = n * 5.0f;
    REQUIRE(getLength(s) == Approx(5.0f).epsilon(0.001));
}

// ── vec3 division edge cases ──────────────────────────────────────────────────

TEST_CASE("vec3 division by 2 halves all components", "[math]") {
    vec3 v{4.0f, 6.0f, 8.0f};
    vec3 r = v / 2.0f;
    REQUIRE(r.x == Approx(2.0f));
    REQUIRE(r.y == Approx(3.0f));
    REQUIRE(r.z == Approx(4.0f));
}

TEST_CASE("vec3 division by 1 leaves unchanged", "[math]") {
    vec3 v{3.0f, 5.0f, 7.0f};
    vec3 r = v / 1.0f;
    REQUIRE(r.x == Approx(3.0f));
    REQUIRE(r.y == Approx(5.0f));
    REQUIRE(r.z == Approx(7.0f));
}

TEST_CASE("vec3 subtraction from zero negates", "[math]") {
    vec3 zero{0.0f, 0.0f, 0.0f};
    vec3 v{3.0f, -4.0f, 5.0f};
    vec3 r = zero - v;
    REQUIRE(r.x == Approx(-3.0f));
    REQUIRE(r.y == Approx(4.0f));
    REQUIRE(r.z == Approx(-5.0f));
}

TEST_CASE("distance satisfies triangle inequality", "[math]") {
    vec3 a{0.0f, 0.0f, 0.0f};
    vec3 b{1.0f, 0.0f, 0.0f};
    vec3 c{0.5f, 1.0f, 0.0f};
    REQUIRE(distance(a, c) <= distance(a, b) + distance(b, c) + 1e-5f);
}

TEST_CASE("normalize of 45-degree vector gives correct components", "[math]") {
    vec3 v{1.0f, 1.0f, 0.0f};
    vec3 n = normalize(v);
    float inv_sqrt2 = 1.0f / std::sqrt(2.0f);
    REQUIRE(n.x == Approx(inv_sqrt2).epsilon(0.001));
    REQUIRE(n.y == Approx(inv_sqrt2).epsilon(0.001));
    REQUIRE(n.z == Approx(0.0f));
}

TEST_CASE("translate is equivalent to addition", "[math]") {
    vec3 a{1.0f, 2.0f, 3.0f};
    vec3 b{4.0f, 5.0f, 6.0f};
    vec3 t = translate(a, b);
    vec3 s = a + b;
    REQUIRE(t.x == Approx(s.x));
    REQUIRE(t.y == Approx(s.y));
    REQUIRE(t.z == Approx(s.z));
}

TEST_CASE("rotate 45 then 45 equals rotate 90", "[math]") {
    vec3 v1{1.0f, 0.0f, 0.0f};
    rotate(v1, 45.0f);
    rotate(v1, 45.0f);
    vec3 v2{1.0f, 0.0f, 0.0f};
    rotate(v2, 90.0f);
    REQUIRE(v1.x == Approx(v2.x).margin(1e-4f));
    REQUIRE(v1.z == Approx(v2.z).margin(1e-4f));
}

TEST_CASE("getLength of 1-2-2 vector is 3", "[math]") {
    REQUIRE(getLength(vec3{1.0f, 2.0f, 2.0f}) == Approx(3.0f));
}

TEST_CASE("getLength of 2-6-3 vector is 7", "[math]") {
    REQUIRE(getLength(vec3{2.0f, 6.0f, 3.0f}) == Approx(7.0f));
}

TEST_CASE("distance of 3D 3-4-5 triple", "[math]") {
    // 12-16-0 should give 20 (scale of 3-4-5 by 4)
    REQUIRE(distance({0.0f,0.0f,0.0f}, {12.0f,16.0f,0.0f}) == Approx(20.0f));
}

TEST_CASE("vec3 += returns reference to self", "[math]") {
    vec3 a{1.0f, 1.0f, 1.0f};
    vec3& ref = (a += vec3{2.0f, 2.0f, 2.0f});
    REQUIRE(&ref == &a);
    REQUIRE(a.x == Approx(3.0f));
}

TEST_CASE("vec3 *= returns reference to self", "[math]") {
    vec3 a{2.0f, 2.0f, 2.0f};
    vec3& ref = (a *= 3.0f);
    REQUIRE(&ref == &a);
    REQUIRE(a.x == Approx(6.0f));
}

TEST_CASE("negating twice returns original", "[math]") {
    vec3 v{3.0f, -4.0f, 5.0f};
    vec3 neg = v * -1.0f;
    vec3 back = neg * -1.0f;
    REQUIRE(back.x == Approx(v.x));
    REQUIRE(back.y == Approx(v.y));
    REQUIRE(back.z == Approx(v.z));
}

TEST_CASE("perspectiveTransform large x produces small result", "[math]") {
    // x=9, y=9, z=10: v.x = 9/(10*90) = 0.01
    vec3 v{9.0f, 9.0f, 10.0f};
    perspectiveTransform(v, 0.0f);
    REQUIRE(v.x == Approx(0.01f));
    REQUIRE(v.y == Approx(0.01f));
}

TEST_CASE("perspectiveTransform with negative x", "[math]") {
    vec3 v{-90.0f, 90.0f, 1.0f};
    perspectiveTransform(v, 0.0f);
    REQUIRE(v.x == Approx(-1.0f));
    REQUIRE(v.y == Approx(1.0f));
}

TEST_CASE("getLength with all equal components", "[math]") {
    // length of {a, a, a} = a * sqrt(3)
    float a = 4.0f;
    float expected = a * std::sqrt(3.0f);
    REQUIRE(getLength(vec3{a, a, a}) == Approx(expected).epsilon(0.001));
}

TEST_CASE("distance between points on sphere surface is consistent", "[math]") {
    // All unit axes are distance sqrt(2) apart
    float d = distance({1.0f,0.0f,0.0f}, {0.0f,1.0f,0.0f});
    REQUIRE(d == Approx(std::sqrt(2.0f)).epsilon(0.001));
}

TEST_CASE("vec3 -= then += returns original", "[math]") {
    vec3 v{5.0f, 6.0f, 7.0f};
    vec3 d{1.0f, 2.0f, 3.0f};
    v -= d;
    v += d;
    REQUIRE(v.x == Approx(5.0f));
    REQUIRE(v.y == Approx(6.0f));
    REQUIRE(v.z == Approx(7.0f));
}
