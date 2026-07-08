#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include "world/Tile.h"
#include "game/TechTree.h"
#include "core/Types.h"

namespace poly {

struct Player {
    PlayerId id = kInvalidPlayerId;
    char name[16] = "Player";
    bool isAI = false;
    bool alive = true;
    Color4 color{255, 255, 255, 255};

    int32_t stars = 5;

    std::array<bool, static_cast<size_t>(TechId::Count)> researched{};

    std::vector<UnitId> unitIds;
    std::vector<CityId> cityIds;
    CityId capitalCityId = kInvalidCityId;

    bool hasTech(TechId t) const { return researched[static_cast<size_t>(t)]; }

    bool canResearch(TechId t) const {
        if (hasTech(t)) return false;
        const TechDef& def = techDef(t);
        if (def.prereq != kNoPrereq && !researched[def.prereq]) return false;
        return stars >= def.cost;
    }

    bool unitUnlocked(UnitType type) const {
        // Warrior and Boat's land-counterpart requirements: Warrior is
        // always available; everything else requires the tech that
        // unlocks it.
        if (type == UnitType::Warrior) return true;
        for (size_t i = 0; i < static_cast<size_t>(TechId::Count); ++i) {
            if (kTechDefs[i].unlocksUnit == type && researched[i]) return true;
        }
        return false;
    }
};

} // namespace poly
