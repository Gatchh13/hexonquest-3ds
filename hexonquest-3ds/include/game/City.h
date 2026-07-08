#pragma once

#include <cstdint>
#include <array>
#include "core/HexCoord.h"
#include "world/Tile.h"
#include "game/Unit.h"

namespace poly {

constexpr int32_t kCityNameMaxLen = 16;
constexpr int32_t kMaxWorkedTiles = 19; // radius-2 hex ring (1 + 6 + 12)

struct City {
    CityId id = kInvalidCityId;
    PlayerId owner = kInvalidPlayerId;
    HexCoord coord{};
    char name[kCityNameMaxLen] = "City";

    int32_t population = 1;
    int32_t populationProgress = 0; // accumulated growth points toward next population
    int32_t level = 1;
    bool isCapital = false;
    bool alive = false;

    UnitType producing = UnitType::Count; // Count == not producing
    int32_t productionProgress = 0;       // stars invested so far

    std::array<HexCoord, kMaxWorkedTiles> workedTiles{};
    int32_t workedTileCount = 0;

    // Work radius grows with city level: 1 at level<3, 2 at level>=3.
    int32_t workRadius() const { return level >= 3 ? 2 : 1; }

    // Population needed to grow from current level to the next.
    int32_t populationToGrow() const { return 3 + (level - 1) * 2; }

    bool isProducing() const { return producing != UnitType::Count; }
};

} // namespace poly
