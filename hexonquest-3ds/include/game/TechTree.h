#pragma once

#include <cstdint>
#include <array>
#include "game/Unit.h"

namespace poly {

enum class TechId : uint8_t {
    Climbing = 0,   // see over mountains, mountains passable
    Forestry,       // reduced forest movement cost, unlocks Defender
    Riding,         // unlocks Rider
    Archery,        // unlocks Archer
    Sailing,        // unlocks Boat
    Strategy,       // unlocks Catapult
    Chivalry,       // unlocks Knight
    Roads,          // unlocks road construction (reduces move cost to 0.5)
    Count
};

constexpr uint8_t kNoPrereq = static_cast<uint8_t>(TechId::Count);

struct TechDef {
    const char* name;
    int32_t cost;         // stars
    uint8_t prereq;        // kNoPrereq if none, else index into TechId
    UnitType unlocksUnit;  // UnitType::Count if it unlocks no unit
};

inline constexpr std::array<TechDef, static_cast<size_t>(TechId::Count)> kTechDefs = {{
    /* Climbing */  { "Climbing",  3, kNoPrereq, UnitType::Count },
    /* Forestry */  { "Forestry",  3, kNoPrereq, UnitType::Defender },
    /* Riding   */  { "Riding",    4, kNoPrereq, UnitType::Rider },
    /* Archery  */  { "Archery",   4, kNoPrereq, UnitType::Archer },
    /* Sailing  */  { "Sailing",   5, kNoPrereq, UnitType::Boat },
    /* Strategy */  { "Strategy",  6, static_cast<uint8_t>(TechId::Riding), UnitType::Catapult },
    /* Chivalry */  { "Chivalry",  7, static_cast<uint8_t>(TechId::Riding), UnitType::Knight },
    /* Roads    */  { "Roads",     5, static_cast<uint8_t>(TechId::Climbing), UnitType::Count },
}};

inline const TechDef& techDef(TechId id) {
    return kTechDefs[static_cast<size_t>(id)];
}

} // namespace poly
