#include "ai/AIController.h"
#include "game/Pathfinder.h"
#include <limits>
#include <algorithm>
#include <array>

namespace poly {

void AIController::takeTurn(GameState& state, PlayerId playerId) {
    Player* p = state.player(playerId);
    if (!p || !p->alive) return;

    handleResearch(state, *p);
    handleProduction(state, *p);
    handleUnits(state, *p);
}

void AIController::handleResearch(GameState& state, Player& player) {
    // Pick the cheapest currently-affordable, prereq-satisfied tech the
    // AI doesn't have yet. Keeps a small star reserve (2) for reacting
    // to production needs.
    int32_t bestIdx = -1;
    int32_t bestCost = std::numeric_limits<int32_t>::max();
    for (size_t i = 0; i < static_cast<size_t>(TechId::Count); ++i) {
        const TechId tech = static_cast<TechId>(i);
        if (!player.canResearch(tech)) continue;
        const TechDef& def = techDef(tech);
        if (def.cost < bestCost && player.stars - def.cost >= 2) {
            bestCost = def.cost;
            bestIdx = static_cast<int32_t>(i);
        }
    }
    if (bestIdx >= 0) {
        state.researchTech(player.id, static_cast<TechId>(bestIdx));
    }
}

bool AIController::isCoastalCity(const GameState& state, const City& city) {
    std::array<HexCoord, 6> neighbors;
    const int32_t n = state.grid().getNeighbors(city.coord, neighbors);
    for (int32_t i = 0; i < n; ++i) {
        if (const Tile* t = state.grid().tileAt(neighbors[i]); t && t->isWater()) {
            return true;
        }
    }
    return false;
}

void AIController::handleProduction(GameState& state, Player& player) {
    for (CityId cid : player.cityIds) {
        City* c = state.city(cid);
        if (!c || c->isProducing()) continue;

        // Prefer the strongest unlocked land unit type the city can
        // eventually afford; fall back to Warrior (always unlocked).
        UnitType best = UnitType::Warrior;
        int32_t bestScore = -1;
        for (size_t i = 0; i < static_cast<size_t>(UnitType::Count); ++i) {
            const UnitType type = static_cast<UnitType>(i);
            if (type == UnitType::Boat) continue; // boats are handled separately below
            if (!player.unitUnlocked(type)) continue;
            const UnitStats& stats = statsFor(type);
            const int32_t score = stats.attack + stats.defense + stats.movement;
            if (score > bestScore) {
                bestScore = score;
                best = type;
            }
        }

        // Occasionally build a Boat instead, once Sailing is unlocked and
        // the tribe doesn't already have an idle one waiting to ferry
        // units -- lets the AI eventually expand across water.
        if (player.unitUnlocked(UnitType::Boat) && isCoastalCity(state, *c)) {
            bool hasIdleBoat = false;
            for (UnitId uid : player.unitIds) {
                const Unit* u = state.unit(uid);
                if (u && u->isBoat() && u->passengerId == kInvalidUnitId) {
                    hasIdleBoat = true;
                    break;
                }
            }
            if (!hasIdleBoat) {
                best = UnitType::Boat;
            }
        }

        state.queueProduction(cid, best);
    }
}

const Unit* AIController::findClosestEnemyUnit(const GameState& state, PlayerId self,
                                                const HexCoord& from) {
    const Unit* closest = nullptr;
    int32_t bestDist = std::numeric_limits<int32_t>::max();
    for (const Unit& u : state.units()) {
        if (!u.alive || u.owner == self || u.embarked) continue;
        const int32_t d = from.distanceTo(u.coord);
        if (d < bestDist) {
            bestDist = d;
            closest = &u;
        }
    }
    return closest;
}

namespace {

bool boatEnterPredicate(const GameState& state, PlayerId self, const Unit& boat, const Tile& t) {
    if (!t.isWater()) return false;
    if (t.occupantUnit != kInvalidUnitId && t.occupantUnit != boat.id) {
        const Unit* occ = state.unit(t.occupantUnit);
        if (occ && occ->owner != self) return false;
    }
    return true;
}

// Moves `moverId` one hop closer to `target` using `predicate` to gate
// which tiles may be entered, choosing the reachable tile that most
// reduces hex distance to the target. No-op if nothing gets closer.
void stepToward(GameState& state, UnitId moverId, const HexCoord& target,
                 const Pathfinder::EnterPredicate& predicate) {
    const Unit* mover = state.unit(moverId);
    if (!mover || mover->movesLeft <= 0.0f) return;

    const std::vector<ReachableTile> reachable =
        Pathfinder::computeReachable(state.grid(), mover->coord, mover->movesLeft, predicate);

    HexCoord bestStep = mover->coord;
    int32_t bestRemaining = mover->coord.distanceTo(target);
    for (const ReachableTile& rt : reachable) {
        if (rt.coord == mover->coord) continue;
        const int32_t d = rt.coord.distanceTo(target);
        if (d < bestRemaining) {
            bestRemaining = d;
            bestStep = rt.coord;
        }
    }
    if (bestStep != mover->coord) {
        state.moveUnit(moverId, bestStep);
    }
}

} // namespace

void AIController::handleBoatUnit(GameState& state, Player& player, Unit& boat) {
    auto predicate = [&](const Tile& t) { return boatEnterPredicate(state, player.id, boat, t); };

    if (boat.passengerId != kInvalidUnitId) {
        // Ferry the passenger toward the nearest unclaimed coastal tile.
        HexCoord target = boat.coord;
        bool haveTarget = false;
        int32_t bestDist = std::numeric_limits<int32_t>::max();
        state.grid().forEachTile([&](const Tile& t) {
            if (!t.isPassableLand() || t.owner == player.id) return;
            std::array<HexCoord, 6> neighbors;
            const int32_t n = state.grid().getNeighbors(t.coord, neighbors);
            bool coastal = false;
            for (int32_t i = 0; i < n; ++i) {
                const Tile* nt = state.grid().tileAt(neighbors[i]);
                if (nt && nt->isWater()) { coastal = true; break; }
            }
            if (!coastal) return;
            const int32_t d = boat.coord.distanceTo(t.coord);
            if (d < bestDist) {
                bestDist = d;
                target = t.coord;
                haveTarget = true;
            }
        });

        if (haveTarget) {
            stepToward(state, boat.id, target, predicate);
        }

        // Whether or not it moved, try to drop the passenger on any
        // adjacent unclaimed land tile now within reach.
        Unit* boatNow = state.unit(boat.id);
        if (boatNow) {
            std::array<HexCoord, 6> neighbors;
            const int32_t n = state.grid().getNeighbors(boatNow->coord, neighbors);
            for (int32_t i = 0; i < n; ++i) {
                if (state.canDisembark(boatNow->passengerId, neighbors[i])) {
                    state.disembarkUnit(boatNow->passengerId, neighbors[i]);
                    break;
                }
            }
        }
        return;
    }

    // Empty boat: drift toward the nearest friendly, unembarked land
    // unit so it's positioned to pick someone up.
    HexCoord target = boat.coord;
    bool haveTarget = false;
    int32_t bestDist = std::numeric_limits<int32_t>::max();
    for (UnitId uid : player.unitIds) {
        const Unit* candidate = state.unit(uid);
        if (!candidate || candidate->isBoat() || candidate->embarked) continue;
        const int32_t d = boat.coord.distanceTo(candidate->coord);
        if (d < bestDist) {
            bestDist = d;
            target = candidate->coord;
            haveTarget = true;
        }
    }
    if (haveTarget) {
        stepToward(state, boat.id, target, predicate);
    }
}

void AIController::handleUnits(GameState& state, Player& player) {
    // Copy ids first: attacking/moving can mutate unitIds indirectly
    // (captures, kills, embark/disembark) while we iterate.
    std::vector<UnitId> ids = player.unitIds;

    for (UnitId uid : ids) {
        Unit* u = state.unit(uid);
        if (!u || !u->alive) continue;
        if (u->embarked) continue; // cargo is moved indirectly via its carrier boat

        if (u->isBoat()) {
            handleBoatUnit(state, player, *u);
            continue;
        }

        const Unit* enemy = findClosestEnemyUnit(state, player.id, u->coord);

        // Try to attack first if any enemy is already in range.
        if (enemy && !u->hasAttacked && state.canAttack(uid, enemy->id)) {
            state.attack(uid, enemy->id);
            u = state.unit(uid);
            if (!u || !u->alive) continue;
        }

        if (!u->alive || u->movesLeft <= 0.0f) {
            continue;
        }

        // Decide a movement target: chase the closest enemy if one
        // exists, otherwise explore toward the nearest unowned land
        // tile with a resource (simple expansion heuristic), and
        // failing that, head for a friendly empty boat to hitch a ride
        // to another shore.
        HexCoord target = u->coord;
        bool haveTarget = false;

        if (enemy && enemy->alive) {
            target = enemy->coord;
            haveTarget = true;
        } else {
            int32_t bestDist = std::numeric_limits<int32_t>::max();
            state.grid().forEachTile([&](const Tile& t) {
                if (t.owner == player.id) return;
                if (!t.isPassableLand()) return;
                if (t.resource == ResourceType::None) return;
                const int32_t d = u->coord.distanceTo(t.coord);
                if (d < bestDist) {
                    bestDist = d;
                    target = t.coord;
                    haveTarget = true;
                }
            });
        }

        if (!haveTarget) {
            int32_t bestBoatDist = std::numeric_limits<int32_t>::max();
            for (UnitId otherId : player.unitIds) {
                const Unit* candidate = state.unit(otherId);
                if (!candidate || !candidate->isBoat() || candidate->passengerId != kInvalidUnitId) continue;
                const int32_t d = u->coord.distanceTo(candidate->coord);
                if (d < bestBoatDist) {
                    bestBoatDist = d;
                    target = candidate->coord;
                    haveTarget = true;
                }
            }
        }

        if (!haveTarget) continue;

        auto predicate = [&](const Tile& t) {
            const UnitStats& stats = statsFor(u->type);
            if (t.isWater() && !stats.canEnterWater) return false;
            if (!t.isWater() && !stats.canEnterLand) return false;
            if (t.occupantUnit != kInvalidUnitId) {
                const Unit* occ = state.unit(t.occupantUnit);
                if (occ && occ->owner != player.id) return false;
            }
            return true;
        };

        stepToward(state, uid, target, predicate);

        u = state.unit(uid);
        if (u && u->alive && !u->hasAttacked) {
            const Unit* newEnemy = findClosestEnemyUnit(state, player.id, u->coord);
            if (newEnemy && state.canAttack(uid, newEnemy->id)) {
                state.attack(uid, newEnemy->id);
                u = state.unit(uid);
            }
        }

        // If movement brought this unit next to an empty friendly boat,
        // hop aboard.
        if (u && u->alive && !u->embarked) {
            std::array<HexCoord, 6> neighbors;
            const int32_t n = state.grid().getNeighbors(u->coord, neighbors);
            for (int32_t i = 0; i < n; ++i) {
                Unit* boatAt = state.unitAt(neighbors[i]);
                if (boatAt && state.canEmbark(u->id, boatAt->id)) {
                    state.embarkUnit(u->id, boatAt->id);
                    break;
                }
            }
        }
    }
}

} // namespace poly
