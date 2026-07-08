#pragma once

#include <cstdint>
#include <array>
#include "core/HexCoord.h"
#include "world/Tile.h"

namespace poly {

enum class UnitType : uint8_t {
    Warrior = 0,
    Archer,
    Rider,
    Defender,
    Knight,
    Catapult,
    Boat,
    Count
};

struct UnitStats {
    const char* name;
    int32_t cost;         // stars to train
    int32_t maxHealth;
    int32_t attack;
    int32_t defense;
    int32_t movement;     // movement points per turn
    int32_t attackRange;  // 1 = melee, 2+ = ranged
    bool canEnterWater;
    bool canEnterLand;
};

// Central stat table. Index with static_cast<size_t>(UnitType).
inline constexpr std::array<UnitStats, static_cast<size_t>(UnitType::Count)> kUnitStats = {{
    /* Warrior  */ { "Warrior",  2, 10, 2, 2, 1, 1, false, true  },
    /* Archer   */ { "Archer",   3, 10, 2, 1, 1, 2, false, true  },
    /* Rider    */ { "Rider",    3, 10, 2, 1, 2, 1, false, true  },
    /* Defender */ { "Defender", 3, 15, 1, 3, 1, 1, false, true  },
    /* Knight   */ { "Knight",   5, 15, 3, 1, 3, 1, false, true  },
    /* Catapult */ { "Catapult", 5, 10, 4, 1, 1, 3, false, true  },
    /* Boat     */ { "Boat",     3, 10, 1, 1, 2, 1, true,  false },
}};

inline const UnitStats& statsFor(UnitType type) {
    return kUnitStats[static_cast<size_t>(type)];
}

// -----------------------------------------------------------------------
// Runtime unit instance. Plain data (no vtable) so the unit list can be
// stored as a flat, cache-friendly std::vector inside GameState.
// -----------------------------------------------------------------------
struct Unit {
    UnitId id = kInvalidUnitId;
    UnitType type = UnitType::Warrior;
    PlayerId owner = kInvalidPlayerId;
    HexCoord coord{};

    int32_t health = 0;
    float movesLeft = 0.0f;
    bool hasAttacked = false;
    bool veteran = false;
    bool alive = false;

    // Embarkation: a land unit riding a Boat sets embarked=true and
    // carrierBoatId to the boat's id, and shares the boat's tile rather
    // than occupying its own. A Boat currently carrying a passenger
    // records that passenger's id here; boats can carry at most one
    // passenger at a time.
    bool embarked = false;
    UnitId carrierBoatId = kInvalidUnitId;
    UnitId passengerId = kInvalidUnitId;

    // Combat experience: a unit becomes veteran after enough kills, which
    // toughens it (higher max health) without inflating its offense --
    // keeps veteran units durable survivors rather than snowballing
    // damage dealers.
    int32_t killCount = 0;
    static constexpr int32_t kVeteranKillThreshold = 3;
    static constexpr float kVeteranHealthMultiplier = 1.5f;

    int32_t maxHealth() const {
        const int32_t base = statsFor(type).maxHealth;
        return veteran ? static_cast<int32_t>(base * kVeteranHealthMultiplier) : base;
    }

    // Call after incrementing killCount on a kill; returns true if this
    // kill just pushed the unit into veteran status (caller can use this
    // to trigger a promotion visual/sfx).
    bool checkVeteranPromotion() {
        if (veteran || killCount < kVeteranKillThreshold) return false;
        const int32_t oldMax = maxHealth();
        veteran = true;
        const int32_t newMax = maxHealth();
        health += (newMax - oldMax); // the toughness gain also heals by the same amount
        return true;
    }

    bool isBoat() const { return type == UnitType::Boat; }

    bool canAct() const { return alive && (movesLeft > 0.0f || !hasAttacked); }

    void resetForNewTurn() {
        movesLeft = static_cast<float>(statsFor(type).movement);
        hasAttacked = false;
    }
};

} // namespace poly
