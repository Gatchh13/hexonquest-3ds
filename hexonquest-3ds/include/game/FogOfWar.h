#pragma once

#include <vector>
#include <cstdint>
#include "world/Tile.h"
#include "world/HexGrid.h"
#include "game/Unit.h"
#include "game/City.h"

namespace poly {

// -----------------------------------------------------------------------
// FogOfWar stores one VisibilityState per tile per player. Recomputed
// from scratch each turn (cheap at our map sizes: <= 20x16 = 320 tiles)
// rather than incrementally tracked, which keeps the logic simple and
// bug-free at the cost of a few hundred writes per turn.
// -----------------------------------------------------------------------
class FogOfWar {
public:
    void init(int32_t numPlayers, int32_t width, int32_t height) {
        width_ = width;
        height_ = height;
        perPlayer_.assign(static_cast<size_t>(numPlayers),
                           std::vector<VisibilityState>(static_cast<size_t>(width * height),
                                                         VisibilityState::Unexplored));
    }

    // Demotes all currently-Visible tiles to Explored, then re-marks
    // Visible around every living unit/city the player owns.
    void recompute(PlayerId player, const HexGrid& grid,
                    const std::vector<Unit>& units, const std::vector<City>& cities) {
        if (player < 0 || static_cast<size_t>(player) >= perPlayer_.size()) return;
        auto& vis = perPlayer_[static_cast<size_t>(player)];

        for (auto& v : vis) {
            if (v == VisibilityState::Visible) v = VisibilityState::Explored;
        }

        constexpr int32_t kUnitSight = 2;
        constexpr int32_t kCitySight = 2;

        for (const Unit& u : units) {
            if (!u.alive || u.owner != player) continue;
            revealAround(vis, grid, u.coord, kUnitSight);
        }
        for (const City& c : cities) {
            if (!c.alive || c.owner != player) continue;
            revealAround(vis, grid, c.coord, kCitySight);
        }
    }

    VisibilityState stateAt(PlayerId player, const HexCoord& coord, const HexGrid& grid) const {
        if (player < 0 || static_cast<size_t>(player) >= perPlayer_.size()) {
            return VisibilityState::Visible; // no fog tracked (e.g. neutral) -> fully visible
        }
        if (!grid.isInBounds(coord)) return VisibilityState::Unexplored;
        const size_t idx = flatIndex(grid, coord);
        return perPlayer_[static_cast<size_t>(player)][idx];
    }

private:
    size_t flatIndex(const HexGrid& grid, const HexCoord& coord) const {
        // Mirrors HexGrid's internal odd-r offset addressing so indices
        // line up 1:1 with the tile storage order.
        const int32_t row = coord.r;
        const int32_t col = coord.q + (row - (row & 1)) / 2;
        (void)grid;
        return static_cast<size_t>(row) * static_cast<size_t>(width_) + static_cast<size_t>(col);
    }

    void revealAround(std::vector<VisibilityState>& vis, const HexGrid& grid,
                       const HexCoord& center, int32_t radius) {
        forEachHexInRadius(center, radius, [&](const HexCoord& c) {
            if (!grid.isInBounds(c)) return;
            vis[flatIndex(grid, c)] = VisibilityState::Visible;
        });
    }

    int32_t width_ = 0;
    int32_t height_ = 0;
    std::vector<std::vector<VisibilityState>> perPlayer_;
};

} // namespace poly
