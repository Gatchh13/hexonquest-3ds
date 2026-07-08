#pragma once

#include <vector>
#include <cstdint>
#include <functional>
#include "core/HexCoord.h"
#include "core/Random.h"
#include "world/Tile.h"

namespace poly {

// -----------------------------------------------------------------------
// HexGrid owns the full tile board for a rectangular-in-axial-space map.
// Storage is a flat std::vector indexed via axial->array mapping so
// gameplay code never touches raw indices; this keeps cache locality
// good for the small maps (<= 24x24) appropriate to 3DS memory limits.
// -----------------------------------------------------------------------
class HexGrid {
public:
    HexGrid() = default;

    // Allocates storage and generates a fresh map. `width`/`height` are in
    // axial-offset space (rectangular playable area of that many columns
    // and rows). `seed` makes generation fully deterministic.
    void generate(int32_t width, int32_t height, uint64_t seed);

    bool isInBounds(const HexCoord& c) const;

    Tile* tileAt(const HexCoord& c);
    const Tile* tileAt(const HexCoord& c) const;

    int32_t width() const { return width_; }
    int32_t height() const { return height_; }

    // Iterates every tile in the grid (row-major over the offset storage).
    void forEachTile(const std::function<void(Tile&)>& fn);
    void forEachTile(const std::function<void(const Tile&)>& fn) const;

    // Neighbor helper that filters out-of-bounds directions automatically.
    // Returns the count written into `outBuffer` (max 6).
    int32_t getNeighbors(const HexCoord& c, std::array<HexCoord, 6>& outBuffer) const;

    const std::vector<Tile>& tiles() const { return tiles_; }
    std::vector<Tile>& tilesMutable() { return tiles_; }

private:
    int32_t offsetIndex(const HexCoord& c) const;
    // Converts axial coord to an "offset" row/col used purely for storage
    // addressing (odd-r horizontal layout).
    Vec2i axialToOffset(const HexCoord& c) const;
    HexCoord offsetToAxial(int32_t col, int32_t row) const;

    void generateHeightmap(Random& rng);
    void placeResources(Random& rng);
    void smoothCoastlines();

    int32_t width_ = 0;
    int32_t height_ = 0;
    std::vector<Tile> tiles_;
};

} // namespace poly
