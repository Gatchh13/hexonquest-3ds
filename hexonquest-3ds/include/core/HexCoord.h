#pragma once

#include <cstdint>
#include <cmath>
#include <array>
#include "core/Types.h"

namespace poly {

// -----------------------------------------------------------------------
// Axial hex coordinates (q, r). Flat-top orientation.
// Cube coordinate s is derived (q + r + s = 0), never stored, to keep
// this struct 8 bytes and trivially copyable / hashable.
// -----------------------------------------------------------------------
struct HexCoord {
    int32_t q = 0;
    int32_t r = 0;

    constexpr HexCoord() = default;
    constexpr HexCoord(int32_t pq, int32_t pr) : q(pq), r(pr) {}

    constexpr int32_t s() const { return -q - r; }

    constexpr bool operator==(const HexCoord& o) const { return q == o.q && r == o.r; }
    constexpr bool operator!=(const HexCoord& o) const { return !(*this == o); }

    constexpr HexCoord operator+(const HexCoord& o) const { return HexCoord(q + o.q, r + o.r); }
    constexpr HexCoord operator-(const HexCoord& o) const { return HexCoord(q - o.q, r - o.r); }

    int32_t distanceTo(const HexCoord& o) const {
        return (std::abs(q - o.q) + std::abs(r - o.r) + std::abs(s() - o.s())) / 2;
    }

    // Deterministic hash for use as a key in flat hash maps / lookup tables.
    struct Hash {
        std::size_t operator()(const HexCoord& h) const noexcept {
            // Pack into 64 bits then mix — avoids collisions for the map sizes
            // we use (<= 64x64 world).
            uint64_t packed = (static_cast<uint64_t>(static_cast<uint32_t>(h.q)) << 32) |
                               static_cast<uint32_t>(h.r);
            packed ^= packed >> 33;
            packed *= 0xff51afd7ed558ccdULL;
            packed ^= packed >> 33;
            return static_cast<std::size_t>(packed);
        }
    };
};

// The six axial direction vectors, in clockwise order starting East.
inline constexpr std::array<HexCoord, 6> kHexDirections = {{
    HexCoord(1, 0), HexCoord(1, -1), HexCoord(0, -1),
    HexCoord(-1, 0), HexCoord(-1, 1), HexCoord(0, 1)
}};

inline HexCoord hexNeighbor(const HexCoord& h, int32_t direction) {
    return h + kHexDirections[static_cast<size_t>(direction) % 6];
}

// Flat-top axial -> pixel center, given a hex "size" (center to corner radius).
inline Vec2 hexToPixel(const HexCoord& h, float size) {
    const float x = size * (1.5f * static_cast<float>(h.q));
    const float y = size * (std::sqrt(3.0f) * 0.5f * static_cast<float>(h.q) +
                             std::sqrt(3.0f) * static_cast<float>(h.r));
    return Vec2(x, y);
}

// Rounds fractional cube coordinates to the nearest valid hex.
inline HexCoord cubeRound(float fq, float fr, float fs) {
    int32_t q = static_cast<int32_t>(std::round(fq));
    int32_t r = static_cast<int32_t>(std::round(fr));
    int32_t s = static_cast<int32_t>(std::round(fs));

    const float qDiff = std::fabs(static_cast<float>(q) - fq);
    const float rDiff = std::fabs(static_cast<float>(r) - fr);
    const float sDiff = std::fabs(static_cast<float>(s) - fs);

    if (qDiff > rDiff && qDiff > sDiff) {
        q = -r - s;
    } else if (rDiff > sDiff) {
        r = -q - s;
    }
    return HexCoord(q, r);
}

// Pixel -> nearest hex, inverse of hexToPixel.
inline HexCoord pixelToHex(const Vec2& p, float size) {
    const float q = (2.0f / 3.0f * p.x) / size;
    const float r = (-1.0f / 3.0f * p.x + std::sqrt(3.0f) / 3.0f * p.y) / size;
    return cubeRound(q, r, -q - r);
}

// Returns all hexes within `radius` (inclusive) of `center`, using cube
// coordinate range iteration. Caller-provided callback avoids heap churn.
template <typename Callback>
void forEachHexInRadius(const HexCoord& center, int32_t radius, Callback&& cb) {
    for (int32_t dq = -radius; dq <= radius; ++dq) {
        const int32_t rMin = std::max(-radius, -dq - radius);
        const int32_t rMax = std::min(radius, -dq + radius);
        for (int32_t dr = rMin; dr <= rMax; ++dr) {
            cb(HexCoord(center.q + dq, center.r + dr));
        }
    }
}

} // namespace poly
