#pragma once

#include "game/GameState.h"

namespace poly {

// -----------------------------------------------------------------------
// A deliberately simple, deterministic-ish greedy AI: it is not meant to
// be strong, just to provide a functioning opponent that expands,
// researches techs it can afford, produces units, and fights back.
// -----------------------------------------------------------------------
class AIController {
public:
    static void takeTurn(GameState& state, PlayerId playerId);

private:
    static void handleResearch(GameState& state, Player& player);
    static void handleProduction(GameState& state, Player& player);
    static void handleUnits(GameState& state, Player& player);

    // Moves an AI-owned Boat: ferries an embarked passenger toward the
    // nearest unclaimed coastal tile and disembarks it on arrival, or
    // (if empty) drifts toward a friendly land unit that might want a
    // ride across water.
    static void handleBoatUnit(GameState& state, Player& player, Unit& boat);

    static bool isCoastalCity(const GameState& state, const City& city);

    // Finds the closest living enemy unit to `from`. Returns nullptr if
    // no enemy units exist anywhere on the board.
    static const Unit* findClosestEnemyUnit(const GameState& state, PlayerId self,
                                             const HexCoord& from);
};

} // namespace poly
