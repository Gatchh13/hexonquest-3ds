#pragma once

#include <cstdint>
#include "core/HexCoord.h"

namespace poly {

enum class TerrainType : uint8_t {
    Water = 0,
    Ocean,
    Beach,
    Plains,
    Forest,
    Hills,
    Mountains,
    Count
};

enum class ResourceType : uint8_t {
    None = 0,
    Fish,
    Fruit,
    Game,        // animals in forest
    Ore,
    Crop,
    Count
};

// Visibility state for fog of war, tracked per-tile per local player
// (single byte, cheap to store per-player in a parallel array).
enum class VisibilityState : uint8_t {
    Unexplored = 0, // never seen
    Explored,       // seen before, not currently visible (fog)
    Visible         // currently in a unit/city's sight range
};

// Forward declarations to avoid circular includes; full definitions live
// in their respective systems and are referenced here only by id.
using UnitId = int32_t;
using CityId = int32_t;
using PlayerId = int8_t;

constexpr UnitId kInvalidUnitId = -1;
constexpr CityId kInvalidCityId = -1;
constexpr PlayerId kInvalidPlayerId = -1;
constexpr PlayerId kNeutralPlayerId = -2;

struct Tile {
    HexCoord coord{};
    TerrainType terrain = TerrainType::Ocean;
    ResourceType resource = ResourceType::None;
    bool resourceHarvested = false;

    PlayerId owner = kInvalidPlayerId; // territory ownership
    CityId cityId = kInvalidCityId;    // set if this tile is a city center
    CityId parentCityId = kInvalidCityId; // set if claimed as a workable tile of a city

    UnitId occupantUnit = kInvalidUnitId;

    bool hasRoad = false;
    bool isCapital = false;

    // Elevation in abstract units, used for movement cost and rendering
    // offsets (mountains sit visually higher than plains).
    float elevation = 0.0f;

    constexpr bool isWater() const {
        return terrain == TerrainType::Water || terrain == TerrainType::Ocean;
    }

    constexpr bool isPassableLand() const {
        return !isWater();
    }

    constexpr bool isOccupied() const { return occupantUnit != kInvalidUnitId; }

    // Base movement cost to enter this tile (before unit-specific modifiers).
    constexpr float baseMoveCost() const {
        if (hasRoad) return 0.5f;
        switch (terrain) {
            case TerrainType::Mountains: return 3.0f;
            case TerrainType::Hills:     return 1.5f;
            case TerrainType::Forest:    return 1.5f;
            default:                     return 1.0f;
        }
    }
};

} // namespace poly
