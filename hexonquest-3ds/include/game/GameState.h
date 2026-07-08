#pragma once

#include <vector>
#include <optional>
#include "world/HexGrid.h"
#include "game/Unit.h"
#include "game/City.h"
#include "game/Player.h"
#include "game/FogOfWar.h"
#include "game/TechTree.h"
#include "game/Pathfinder.h"

namespace poly {

struct CombatResult {
    int32_t damageToDefender = 0;
    int32_t damageToAttacker = 0;
    bool defenderKilled = false;
    bool attackerKilled = false;
    bool cityCaptured = false;
    bool attackerPromoted = false;
};

// -----------------------------------------------------------------------
// GameState is the single source of truth for a match in progress: the
// board, every unit/city/player, and fog of war. All mutation goes
// through its methods so invariants (id bookkeeping, fog recompute,
// ownership lists) stay consistent in one place.
// -----------------------------------------------------------------------
class GameState {
public:
    void newGame(int32_t mapWidth, int32_t mapHeight, uint64_t seed,
                 int32_t numPlayers, PlayerId humanPlayerId);

    HexGrid& grid() { return grid_; }
    const HexGrid& grid() const { return grid_; }

    std::vector<Player>& players() { return players_; }
    const std::vector<Player>& players() const { return players_; }
    Player* player(PlayerId id);
    const Player* player(PlayerId id) const;

    std::vector<Unit>& units() { return units_; }
    const std::vector<Unit>& units() const { return units_; }
    Unit* unit(UnitId id);
    const Unit* unit(UnitId id) const;
    Unit* unitAt(const HexCoord& coord);
    const Unit* unitAt(const HexCoord& coord) const;

    std::vector<City>& cities() { return cities_; }
    const std::vector<City>& cities() const { return cities_; }
    City* city(CityId id);
    const City* city(CityId id) const;
    City* cityAt(const HexCoord& coord);
    const City* cityAt(const HexCoord& coord) const;

    FogOfWar& fog() { return fog_; }
    const FogOfWar& fog() const { return fog_; }

    // --- Mutations -------------------------------------------------
    UnitId spawnUnit(UnitType type, PlayerId owner, const HexCoord& coord);
    CityId foundCity(PlayerId owner, const HexCoord& coord, bool capital);

    bool canMoveUnit(UnitId id, const HexCoord& dest, std::vector<HexCoord>* outPath = nullptr) const;
    bool moveUnit(UnitId id, const HexCoord& dest);

    // Boarding a Boat consumes the passenger's remaining movement for
    // the turn; the passenger then shares the boat's tile and moves
    // with it until disembarkUnit() is called.
    bool canEmbark(UnitId passengerId, UnitId boatId) const;
    bool embarkUnit(UnitId passengerId, UnitId boatId);

    bool canDisembark(UnitId passengerId, const HexCoord& dest) const;
    bool disembarkUnit(UnitId passengerId, const HexCoord& dest);

    // Converts the tile a unit is standing on into a road (halves its
    // movement cost for everyone), consuming the unit's remaining
    // movement and a small star cost. Requires the Roads technology and
    // an owned, non-water tile without a road already.
    bool canBuildRoad(UnitId unitId) const;
    bool buildRoad(UnitId unitId);

    bool canAttack(UnitId attackerId, UnitId defenderId) const;
    CombatResult attack(UnitId attackerId, UnitId defenderId);

    // Fully heals a unit standing on one of its owner's cities, consuming
    // its remaining movement for the turn.
    bool canHealUnit(UnitId unitId) const;
    bool healUnitAtCity(UnitId unitId);

    // Disbands a unit for a partial star refund (half its training cost,
    // rounded up). Works on any of the unit's owner's units, embarked or
    // not (disbanding an embarked passenger also frees its boat's cargo
    // slot via the normal kill-cleanup path).
    bool canDisbandUnit(UnitId unitId) const;
    bool disbandUnit(UnitId unitId);

    bool queueProduction(CityId cityId, UnitType type);
    bool researchTech(PlayerId playerId, TechId tech);

    // Advances a single player's economy/production/growth for one turn
    // and refreshes their fog of war. Called by TurnManager at the start
    // of that player's turn.
    void beginPlayerTurn(PlayerId playerId);

    // Recomputes fog for every player (used once at game start).
    void recomputeAllFog();

    // Returns the winning player, or kInvalidPlayerId if the game
    // should continue.
    PlayerId checkVictory() const;

    // Combines stars, city population/level, tech count, and surviving
    // army size into a single comparable score, used to decide the
    // winner once the turn limit is reached without a domination win.
    int32_t computeScore(PlayerId id) const;

    static constexpr int32_t kTurnLimit = 30;

    int32_t livingPlayerCount() const;

    static float terrainDefenseMultiplier(TerrainType terrain);

    friend class SaveManager;

private:
    void killUnit(UnitId id);
    void removeUnitFromOwnerList(const Unit& u);
    void applyCityProductionAndGrowth(City& c, Player& p);

    // Recomputes and stores whether `id` still has any cities or units;
    // call after anything that could remove a player's last asset (a
    // unit dying, a city being captured) so Player::alive stays truthful
    // rather than being permanently stuck at its default.
    void updateEliminationStatus(PlayerId id);

    HexGrid grid_;
    std::vector<Player> players_;
    std::vector<Unit> units_;
    std::vector<City> cities_;
    FogOfWar fog_;

    std::vector<UnitId> freeUnitIds_;
    std::vector<CityId> freeCityIds_;
};

} // namespace poly
