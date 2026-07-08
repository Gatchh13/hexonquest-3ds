#include "world/HexGrid.h"
#include <cmath>
#include <algorithm>

namespace poly {

void HexGrid::generate(int32_t width, int32_t height, uint64_t seed) {
    width_ = width;
    height_ = height;
    tiles_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_), Tile{});

    // Assign coordinates first so every tile knows its own axial position.
    for (int32_t row = 0; row < height_; ++row) {
        for (int32_t col = 0; col < width_; ++col) {
            const HexCoord axial = offsetToAxial(col, row);
            Tile& t = tiles_[static_cast<size_t>(row) * width_ + col];
            t.coord = axial;
        }
    }

    Random rng(seed);
    generateHeightmap(rng);
    smoothCoastlines();
    placeResources(rng);
}

bool HexGrid::isInBounds(const HexCoord& c) const {
    const Vec2i off = axialToOffset(c);
    return off.x >= 0 && off.x < width_ && off.y >= 0 && off.y < height_;
}

int32_t HexGrid::offsetIndex(const HexCoord& c) const {
    const Vec2i off = axialToOffset(c);
    return off.y * width_ + off.x;
}

Vec2i HexGrid::axialToOffset(const HexCoord& c) const {
    // odd-r horizontal layout
    const int32_t col = c.q + (c.r - (c.r & 1)) / 2;
    const int32_t row = c.r;
    return Vec2i(col, row);
}

HexCoord HexGrid::offsetToAxial(int32_t col, int32_t row) const {
    const int32_t q = col - (row - (row & 1)) / 2;
    const int32_t r = row;
    return HexCoord(q, r);
}

Tile* HexGrid::tileAt(const HexCoord& c) {
    if (!isInBounds(c)) return nullptr;
    return &tiles_[static_cast<size_t>(offsetIndex(c))];
}

const Tile* HexGrid::tileAt(const HexCoord& c) const {
    if (!isInBounds(c)) return nullptr;
    return &tiles_[static_cast<size_t>(offsetIndex(c))];
}

void HexGrid::forEachTile(const std::function<void(Tile&)>& fn) {
    for (Tile& t : tiles_) fn(t);
}

void HexGrid::forEachTile(const std::function<void(const Tile&)>& fn) const {
    for (const Tile& t : tiles_) fn(t);
}

int32_t HexGrid::getNeighbors(const HexCoord& c, std::array<HexCoord, 6>& outBuffer) const {
    int32_t count = 0;
    for (int32_t dir = 0; dir < 6; ++dir) {
        const HexCoord n = hexNeighbor(c, dir);
        if (isInBounds(n)) {
            outBuffer[static_cast<size_t>(count++)] = n;
        }
    }
    return count;
}

// -----------------------------------------------------------------------
// Terrain generation: value-noise-esque approach built from summed
// sine/cosine lattices (no external noise library, deterministic given
// the same rng draws) shaped into an island-biased radial falloff so
// generated maps resemble Polytopia's island continents rather than
// uniform noise fields.
// -----------------------------------------------------------------------
void HexGrid::generateHeightmap(Random& rng) {
    // Random per-octave phase/frequency so every seed looks different
    // while staying deterministic.
    struct Octave { float freqX, freqY, phaseX, phaseY, amplitude; };
    std::array<Octave, 4> octaves;
    float amp = 1.0f;
    float totalAmp = 0.0f;
    for (auto& o : octaves) {
        o.freqX = rng.rangeF(0.15f, 0.45f);
        o.freqY = rng.rangeF(0.15f, 0.45f);
        o.phaseX = rng.rangeF(0.0f, 6.2831853f);
        o.phaseY = rng.rangeF(0.0f, 6.2831853f);
        o.amplitude = amp;
        totalAmp += amp;
        amp *= 0.5f;
    }

    const float cx = static_cast<float>(width_) * 0.5f;
    const float cy = static_cast<float>(height_) * 0.5f;
    const float maxDist = std::sqrt(cx * cx + cy * cy);

    for (int32_t row = 0; row < height_; ++row) {
        for (int32_t col = 0; col < width_; ++col) {
            float n = 0.0f;
            for (const auto& o : octaves) {
                n += o.amplitude * std::sin(col * o.freqX + o.phaseX) *
                                    std::cos(row * o.freqY + o.phaseY);
            }
            n = (n / totalAmp) * 0.5f + 0.5f; // normalize to [0,1]

            // Radial falloff biases land toward the center, creating
            // island-like continents instead of edge-to-edge terrain.
            const float dx = static_cast<float>(col) - cx;
            const float dy = static_cast<float>(row) - cy;
            const float dist = std::sqrt(dx * dx + dy * dy) / maxDist;
            const float falloff = clampf(1.35f - dist * 1.6f, 0.0f, 1.0f);

            float elevation = n * falloff;
            elevation += rng.rangeF(-0.03f, 0.03f); // texture jitter

            Tile& t = tiles_[static_cast<size_t>(row) * width_ + col];
            t.elevation = elevation;

            if (elevation < 0.32f) {
                t.terrain = elevation < 0.20f ? TerrainType::Ocean : TerrainType::Water;
            } else if (elevation < 0.38f) {
                t.terrain = TerrainType::Beach;
            } else if (elevation < 0.60f) {
                t.terrain = rng.chance(0.35f) ? TerrainType::Forest : TerrainType::Plains;
            } else if (elevation < 0.78f) {
                t.terrain = TerrainType::Hills;
            } else {
                t.terrain = TerrainType::Mountains;
            }
        }
    }
}

void HexGrid::smoothCoastlines() {
    // Removes single-tile "pimple" land in open ocean and single-tile
    // ocean holes inside continents, which read as visual noise at the
    // 3DS's low top-screen resolution (400x240).
    std::vector<Tile> snapshot = tiles_;
    std::array<HexCoord, 6> neighborBuf;

    for (int32_t row = 0; row < height_; ++row) {
        for (int32_t col = 0; col < width_; ++col) {
            Tile& t = tiles_[static_cast<size_t>(row) * width_ + col];
            const int32_t neighborCount = getNeighbors(t.coord, neighborBuf);
            if (neighborCount == 0) continue;

            int32_t waterNeighbors = 0;
            for (int32_t i = 0; i < neighborCount; ++i) {
                const Tile* n = tileAt(neighborBuf[i]);
                if (n && n->isWater()) ++waterNeighbors;
            }

            const bool isLand = t.isPassableLand();
            if (isLand && waterNeighbors == neighborCount) {
                t.terrain = TerrainType::Beach; // isolated land -> tiny islet, keep as beach
            } else if (!isLand && waterNeighbors == 0) {
                t.terrain = TerrainType::Plains; // isolated water -> fill in
            }
        }
    }
}

void HexGrid::placeResources(Random& rng) {
    for (Tile& t : tiles_) {
        if (t.terrain == TerrainType::Ocean || t.terrain == TerrainType::Water) {
            if (rng.chance(0.12f)) t.resource = ResourceType::Fish;
        } else if (t.terrain == TerrainType::Forest) {
            if (rng.chance(0.30f)) t.resource = ResourceType::Game;
            else if (rng.chance(0.15f)) t.resource = ResourceType::Fruit;
        } else if (t.terrain == TerrainType::Plains) {
            if (rng.chance(0.20f)) t.resource = ResourceType::Crop;
        } else if (t.terrain == TerrainType::Hills || t.terrain == TerrainType::Mountains) {
            if (rng.chance(0.25f)) t.resource = ResourceType::Ore;
        } else if (t.terrain == TerrainType::Beach) {
            if (rng.chance(0.10f)) t.resource = ResourceType::Fruit;
        }
    }
}

} // namespace poly
