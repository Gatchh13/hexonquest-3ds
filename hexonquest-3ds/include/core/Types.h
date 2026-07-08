#pragma once

#include <cstdint>
#include <cmath>

namespace poly {

// -----------------------------------------------------------------------
// Basic vector types. Kept POD + constexpr friendly for cache efficiency
// on the 3DS (no vtables, no heap).
// -----------------------------------------------------------------------
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float px, float py) : x(px), y(py) {}

    constexpr Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    constexpr Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    constexpr Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
    constexpr Vec2 operator/(float s) const { return Vec2(x / s, y / s); }

    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }

    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSq() const { return x * x + y * y; }
};

struct Vec2i {
    int32_t x = 0;
    int32_t y = 0;

    constexpr Vec2i() = default;
    constexpr Vec2i(int32_t px, int32_t py) : x(px), y(py) {}

    constexpr bool operator==(const Vec2i& o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Vec2i& o) const { return !(*this == o); }
};

struct Color4 {
    uint8_t r = 255, g = 255, b = 255, a = 255;
    constexpr Color4() = default;
    constexpr Color4(uint8_t pr, uint8_t pg, uint8_t pb, uint8_t pa = 255)
        : r(pr), g(pg), b(pb), a(pa) {}
};

// Fixed 60Hz-oriented delta time helper. The 3DS LCD refreshes at ~59.83Hz.
constexpr float kFixedDeltaTime = 1.0f / 59.8261f;

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

} // namespace poly
