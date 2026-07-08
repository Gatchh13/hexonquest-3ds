#include "game/GameState.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace poly {

namespace {

constexpr std::array<Color4, 6> kPlayerColors = {{
    Color4(220, 70, 70), Color4(70, 120, 220), Color4(230, 200, 60),
    Color4(90, 200, 120), Color4(190, 100, 220), Color4(230, 150, 70)
}};

HexCoord findNearestLandTile(const HexGrid& grid, const HexCoord& approx) {
    if (const Tile* t = grid.tileAt(approx); t && t->isPassableLand()) {
        return approx;
    }
    for (int32_t radius = 1; radius < std::max(grid.width(), grid.height()); ++radius) {
        HexCoord best = approx;
        bool foundAny = false;
        forEachHexInRadius(approx, radius, [&](const HexCoord& c) {
            if (foundAny) return;
            const Tile* t = grid.tileAt(c);
            if (t && t->isPassableLand() && t->cityId == kInvalidCityId) {
                best = c;
                foundAny = true;
            }
        });
        if (foundAny) return best;
    }
    return approx; // fallback (shouldn't happen on a reasonably sized map)
}

// Like findNearestLandTile, but additionally prefers tiles that stay at
// least `minSpacing` hexes away from every already-claimed starting
// position, expanding the search radius until a suitable tile is found.
// Falls back to the plain nearest-land search (ignoring spacing) if no
// tile satisfies it within the whole map -- better to place tribes a
// little close together than to fail game setup outright on tiny maps
// with many rivals.
HexCoord findSpacedStartTile(const HexGrid& grid, const HexCoord& approx,
                              const std::vector<HexCoord>& existingStarts, int32_t minSpacing) {
    const int32_t maxRadius = std::max(grid.width(), grid.height());
    for (int32_t radius = 0; radius < maxRadius; ++radius) {
        HexCoord best = approx;
        bool foundAny = false;
        forEachHexInRadius(approx, radius, [&](const HexCoord& c) {
            if (foundAny) return;
            const Tile* t = grid.tileAt(c);
            if (!t || !t->isPassableLand() || t->cityId != kInvalidCityId) return;
            for (const HexCoord& existing : existingStarts) {
                if (c.distanceTo(existing) < minSpacing) return;
            }
            best = c;
            foundAny = true;
        });
        if (foundAny) return best;
    }
    return findNearestLandTile(grid, approx);
}

} // namespace

void GameState::newGame(int32_t mapWidth, int32_t mapHeight, uint64_t seed,
                         int32_t numPlayers, PlayerId humanPlayerId) {
    grid_.generate(mapWidth, mapHeight, seed);

    players_.clear();
    units_.clear();
    cities_.clear();
    freeUnitIds_.clear();
    freeCityIds_.clear();

    numPlayers = std::max(1, std::min(numPlayers, static_cast<int32_t>(kPlayerColors.size())));

    for (int32_t i = 0; i < numPlayers; ++i) {
        Player p;
        p.id = static_cast<PlayerId>(i);
        std::snprintf(p.name, sizeof(p.name), "%s", i == humanPlayerId ? "You" : "Rival");
        p.isAI = (i != humanPlayerId);
        p.color = kPlayerColors[static_cast<size_t>(i) % kPlayerColors.size()];
        p.stars = 5;
        players_.push_back(p);
    }

    fog_.init(numPlayers, mapWidth, mapHeight);

    // Distribute starting positions across a roughly square grid of
    // approach points (rather than a single row), so tribes spread out
    // in both dimensions on wide-but-short or tall-but-narrow maps, and
    // enforce a minimum hex distance between the actual chosen tiles so
    // capitals never spawn overlapping or immediately adjacent to each
    // other even on small maps with many rivals.
    const int32_t cols = static_cast<int32_t>(std::ceil(std::sqrt(static_cast<float>(numPlayers))));
    const int32_t rows = (numPlayers + cols - 1) / cols;
    const int32_t minSpacing = std::max(3, std::min(mapWidth, mapHeight) / (std::max(cols, rows) + 1));

    std::vector<HexCoord> starts;
    starts.reserve(static_cast<size_t>(numPlayers));

    for (int32_t i = 0; i < numPlayers; ++i) {
        const int32_t gridCol = i % cols;
        const int32_t gridRow = i / cols;
        const float fracX = (static_cast<float>(gridCol) + 0.5f) / static_cast<float>(cols);
        const float fracY = (static_cast<float>(gridRow) + 0.5f) / static_cast<float>(rows);
        const int32_t col = static_cast<int32_t>(fracX * static_cast<float>(mapWidth));
        const int32_t row = static_cast<int32_t>(fracY * static_cast<float>(mapHeight));
        const HexCoord approx(col - (row - (row & 1)) / 2, row);

        const HexCoord start = findSpacedStartTile(grid_, approx, starts, minSpacing);
        starts.push_back(start);

        const CityId capitalId = foundCity(static_cast<PlayerId>(i), start, /*capital=*/true);
        (void)capitalId;
        spawnUnit(UnitType::Warrior, static_cast<PlayerId>(i), start);
    }

    recomputeAllFog();
}

Player* GameState::player(PlayerId id) {
    if (id < 0 || static_cast<size_t>(id) >= players_.size()) return nullptr;
    return &players_[static_cast<size_t>(id)];
}
const Player* GameState::player(PlayerId id) const {
    if (id < 0 || static_cast<size_t>(id) >= players_.size()) return nullptr;
    return &players_[static_cast<size_t>(id)];
}

Unit* GameState::unit(UnitId id) {
    if (id < 0 || static_cast<size_t>(id) >= units_.size()) return nullptr;
    Unit& u = units_[static_cast<size_t>(id)];
    return u.alive ? &u : nullptr;
}
const Unit* GameState::unit(UnitId id) const {
    if (id < 0 || static_cast<size_t>(id) >= units_.size()) return nullptr;
    const Unit& u = units_[static_cast<size_t>(id)];
    return u.alive ? &u : nullptr;
}

Unit* GameState::unitAt(const HexCoord& coord) {
    for (Unit& u : units_) {
        if (u.alive && !u.embarked && u.coord == coord) return &u;
    }
    return nullptr;
}
const Unit* GameState::unitAt(const HexCoord& coord) const {
    for (const Unit& u : units_) {
        if (u.alive && !u.embarked && u.coord == coord) return &u;
    }
    return nullptr;
}

City* GameState::city(CityId id) {
    if (id < 0 || static_cast<size_t>(id) >= cities_.size()) return nullptr;
    City& c = cities_[static_cast<size_t>(id)];
    return c.alive ? &c : nullptr;
}
const City* GameState::city(CityId id) const {
    if (id < 0 || static_cast<size_t>(id) >= cities_.size()) return nullptr;
    const City& c = cities_[static_cast<size_t>(id)];
    return c.alive ? &c : nullptr;
}

City* GameState::cityAt(const HexCoord& coord) {
    for (City& c : cities_) {
        if (c.alive && c.coord == coord) return &c;
    }
    return nullptr;
}
const City* GameState::cityAt(const HexCoord& coord) const {
    for (const City& c : cities_) {
        if (c.alive && c.coord == coord) return &c;
    }
    return nullptr;
}

UnitId GameState::spawnUnit(UnitType type, PlayerId owner, const HexCoord& coord) {
    Unit u;
    u.type = type;
    u.owner = owner;
    u.coord = coord;
    u.health = statsFor(type).maxHealth;
    u.movesLeft = static_cast<float>(statsFor(type).movement);
    u.hasAttacked = false;
    u.veteran = false;
    u.alive = true;

    UnitId newId;
    if (!freeUnitIds_.empty()) {
        newId = freeUnitIds_.back();
        freeUnitIds_.pop_back();
        u.id = newId;
        units_[static_cast<size_t>(newId)] = u;
    } else {
        newId = static_cast<UnitId>(units_.size());
        u.id = newId;
        units_.push_back(u);
    }

    if (Tile* t = grid_.tileAt(coord)) {
        t->occupantUnit = newId;
    }
    if (Player* p = player(owner)) {
        p->unitIds.push_back(newId);
    }
    return newId;
}

CityId GameState::foundCity(PlayerId owner, const HexCoord& coord, bool capital) {
    City c;
    c.owner = owner;
    c.coord = coord;
    c.isCapital = capital;
    c.population = 1;
    c.level = 1;
    c.alive = true;
    std::snprintf(c.name, sizeof(c.name), capital ? "Capital" : "Town");

    CityId newId;
    if (!freeCityIds_.empty()) {
        newId = freeCityIds_.back();
        freeCityIds_.pop_back();
        c.id = newId;
        cities_[static_cast<size_t>(newId)] = c;
    } else {
        newId = static_cast<CityId>(cities_.size());
        c.id = newId;
        cities_.push_back(c);
    }

    City& stored = cities_[static_cast<size_t>(newId)];
    stored.workedTileCount = 0;
    forEachHexInRadius(coord, stored.workRadius(), [&](const HexCoord& hc) {
        const Tile* t = grid_.tileAt(hc);
        if (t && t->isPassableLand() && t->parentCityId == kInvalidCityId &&
            stored.workedTileCount < kMaxWorkedTiles) {
            stored.workedTiles[static_cast<size_t>(stored.workedTileCount++)] = hc;
        }
    });

    if (Tile* t = grid_.tileAt(coord)) {
        t->cityId = newId;
        t->owner = owner;
        t->isCapital = capital;
    }
    for (int32_t i = 0; i < stored.workedTileCount; ++i) {
        if (Tile* t = grid_.tileAt(stored.workedTiles[static_cast<size_t>(i)])) {
            if (t->owner == kInvalidPlayerId) t->owner = owner;
            t->parentCityId = newId;
        }
    }

    if (Player* p = player(owner)) {
        p->cityIds.push_back(newId);
        if (capital) p->capitalCityId = newId;
    }

    fog_.recompute(owner, grid_, units_, cities_);
    return newId;
}

namespace {
bool defaultLandPredicate(const HexGrid& grid, PlayerId mover, UnitType type, const Tile& tile,
                           const std::vector<Unit>& units, bool allowEnemyAsBlocked) {
    (void)grid;
    const UnitStats& stats = statsFor(type);
    if (tile.isWater() && !stats.canEnterWater) return false;
    if (!tile.isWater() && !stats.canEnterLand) return false;
    if (tile.occupantUnit != kInvalidUnitId) {
        if (!allowEnemyAsBlocked) return false;
        // Find occupant to check ownership.
        for (const Unit& u : units) {
            if (u.id == tile.occupantUnit && u.alive) {
                if (u.owner != mover) return false; // enemy blocks movement
            }
        }
    }
    return true;
}
} // namespace

bool GameState::canMoveUnit(UnitId id, const HexCoord& dest, std::vector<HexCoord>* outPath) const {
    const Unit* u = unit(id);
    if (!u || !u->alive) return false;
    if (u->movesLeft <= 0.0f) return false;
    if (u->coord == dest) return false;
    const Tile* destTile = grid_.tileAt(dest);
    if (!destTile) return false;
    if (destTile->occupantUnit != kInvalidUnitId) return false; // occupied -> not a move, attack instead

    auto predicate = [&](const Tile& t) {
        return defaultLandPredicate(grid_, u->owner, u->type, t, units_, false);
    };

    const std::vector<HexCoord> path = Pathfinder::findPath(grid_, u->coord, dest, predicate);
    if (path.empty()) return false;

    // Validate total path cost fits remaining movement.
    float cost = 0.0f;
    for (size_t i = 1; i < path.size(); ++i) {
        const Tile* t = grid_.tileAt(path[i]);
        if (!t) return false;
        cost += t->baseMoveCost();
    }
    if (cost > u->movesLeft + 0.0001f) return false;

    if (outPath) *outPath = path;
    return true;
}

bool GameState::moveUnit(UnitId id, const HexCoord& dest) {
    std::vector<HexCoord> path;
    if (!canMoveUnit(id, dest, &path)) return false;

    Unit* u = unit(id);
    float cost = 0.0f;
    for (size_t i = 1; i < path.size(); ++i) {
        const Tile* t = grid_.tileAt(path[i]);
        cost += t->baseMoveCost();
    }

    if (Tile* oldTile = grid_.tileAt(u->coord)) {
        oldTile->occupantUnit = kInvalidUnitId;
    }
    u->coord = dest;
    u->movesLeft = std::max(0.0f, u->movesLeft - cost);
    if (Tile* newTile = grid_.tileAt(dest)) {
        newTile->occupantUnit = id;
    }

    // A boat carries its passenger along for the ride; the passenger
    // never occupies a tile of its own while embarked.
    if (u->isBoat() && u->passengerId != kInvalidUnitId) {
        if (Unit* passenger = unit(u->passengerId)) {
            passenger->coord = dest;
        }
    }

    // Recompute fog immediately so newly explored terrain is visible the
    // instant the unit steps into range, rather than waiting until the
    // owner's next turn (when beginPlayerTurn() would otherwise be the
    // only place fog gets refreshed).
    fog_.recompute(u->owner, grid_, units_, cities_);
    return true;
}

namespace {
constexpr int32_t kRoadCost = 2;
} // namespace

bool GameState::canEmbark(UnitId passengerId, UnitId boatId) const {
    const Unit* passenger = unit(passengerId);
    const Unit* boat = unit(boatId);
    if (!passenger || !boat) return false;
    if (passenger->isBoat() || passenger->embarked) return false;
    if (!boat->isBoat() || boat->passengerId != kInvalidUnitId) return false;
    if (passenger->owner != boat->owner) return false;
    if (passenger->movesLeft <= 0.0f) return false;
    return passenger->coord.distanceTo(boat->coord) == 1;
}

bool GameState::embarkUnit(UnitId passengerId, UnitId boatId) {
    if (!canEmbark(passengerId, boatId)) return false;
    Unit* passenger = unit(passengerId);
    Unit* boat = unit(boatId);

    if (Tile* oldTile = grid_.tileAt(passenger->coord)) {
        if (oldTile->occupantUnit == passengerId) oldTile->occupantUnit = kInvalidUnitId;
    }
    passenger->coord = boat->coord;
    passenger->embarked = true;
    passenger->carrierBoatId = boat->id;
    passenger->movesLeft = 0.0f; // boarding uses up the rest of this turn's movement
    boat->passengerId = passenger->id;
    fog_.recompute(passenger->owner, grid_, units_, cities_);
    return true;
}

bool GameState::canDisembark(UnitId passengerId, const HexCoord& dest) const {
    const Unit* passenger = unit(passengerId);
    if (!passenger || !passenger->embarked) return false;
    const Unit* boat = unit(passenger->carrierBoatId);
    if (!boat) return false;
    if (boat->coord.distanceTo(dest) != 1) return false;
    const Tile* t = grid_.tileAt(dest);
    if (!t || !t->isPassableLand() || t->occupantUnit != kInvalidUnitId) return false;
    return true;
}

bool GameState::disembarkUnit(UnitId passengerId, const HexCoord& dest) {
    if (!canDisembark(passengerId, dest)) return false;
    Unit* passenger = unit(passengerId);
    Unit* boat = unit(passenger->carrierBoatId);

    passenger->embarked = false;
    passenger->carrierBoatId = kInvalidUnitId;
    passenger->coord = dest;
    passenger->movesLeft = 0.0f; // disembarking uses up the remaining move
    if (boat) boat->passengerId = kInvalidUnitId;

    if (Tile* newTile = grid_.tileAt(dest)) {
        newTile->occupantUnit = passenger->id;
    }
    fog_.recompute(passenger->owner, grid_, units_, cities_);
    return true;
}

bool GameState::canBuildRoad(UnitId unitId) const {
    const Unit* u = unit(unitId);
    if (!u || u->embarked || u->isBoat()) return false;
    if (u->movesLeft <= 0.0f) return false;
    const Player* p = player(u->owner);
    if (!p || !p->hasTech(TechId::Roads)) return false;
    if (p->stars < kRoadCost) return false;
    const Tile* t = grid_.tileAt(u->coord);
    if (!t || !t->isPassableLand() || t->hasRoad) return false;
    if (t->owner != kInvalidPlayerId && t->owner != u->owner) return false;
    return true;
}

bool GameState::buildRoad(UnitId unitId) {
    if (!canBuildRoad(unitId)) return false;
    Unit* u = unit(unitId);
    Player* p = player(u->owner);
    Tile* t = grid_.tileAt(u->coord);

    p->stars -= kRoadCost;
    t->hasRoad = true;
    u->movesLeft = 0.0f;
    return true;
}

bool GameState::canAttack(UnitId attackerId, UnitId defenderId) const {
    const Unit* a = unit(attackerId);
    const Unit* d = unit(defenderId);
    if (!a || !d) return false;
    if (d->embarked) return false; // cargo is protected; attack the carrying boat instead
    if (a->hasAttacked) return false;
    if (a->owner == d->owner) return false;
    const int32_t range = statsFor(a->type).attackRange;
    return a->coord.distanceTo(d->coord) <= range;
}

float GameState::terrainDefenseMultiplier(TerrainType terrain) {
    switch (terrain) {
        case TerrainType::Hills:     return 1.5f;
        case TerrainType::Mountains: return 1.75f;
        case TerrainType::Forest:    return 1.25f;
        default:                     return 1.0f;
    }
}

CombatResult GameState::attack(UnitId attackerId, UnitId defenderId) {
    CombatResult result;
    if (!canAttack(attackerId, defenderId)) return result;

    Unit* attacker = unit(attackerId);
    Unit* defender = unit(defenderId);
    const UnitStats& aStats = statsFor(attacker->type);
    const UnitStats& dStats = statsFor(defender->type);
    const Tile* defenderTile = grid_.tileAt(defender->coord);
    const float defenseTerrain = defenderTile ? terrainDefenseMultiplier(defenderTile->terrain) : 1.0f;

    const float attackPower = static_cast<float>(aStats.attack) *
        (static_cast<float>(attacker->health) / static_cast<float>(attacker->maxHealth()));
    const float defensePower = static_cast<float>(dStats.defense) *
        (static_cast<float>(defender->health) / static_cast<float>(defender->maxHealth())) * defenseTerrain;
    const float totalPower = attackPower + defensePower;

    constexpr float kDamageScale = 4.5f;
    result.damageToDefender = std::max(1, static_cast<int32_t>(
        std::round((attackPower / totalPower) * static_cast<float>(aStats.attack) * kDamageScale)));

    defender->health -= result.damageToDefender;
    attacker->hasAttacked = true;

    const bool isMeleeCounter = (aStats.attackRange == 1) && (defender->health > 0);
    if (isMeleeCounter) {
        result.damageToAttacker = std::max(1, static_cast<int32_t>(
            std::round((defensePower / totalPower) * static_cast<float>(dStats.defense) * kDamageScale)));
        attacker->health -= result.damageToAttacker;
    }

    if (defender->health <= 0) {
        result.defenderKilled = true;
        const HexCoord defenderCoord = defender->coord;
        const PlayerId defenderOwner = defender->owner;
        killUnit(defenderId);

        attacker->killCount += 1;
        result.attackerPromoted = attacker->checkVeteranPromotion();

        // Capturing: if the attacker is melee and adjacent, it advances
        // into the now-empty tile; if that tile holds an enemy city, the
        // city changes hands.
        if (aStats.attackRange == 1 && attacker->coord.distanceTo(defenderCoord) == 1) {
            if (City* capturedCity = cityAt(defenderCoord)) {
                if (capturedCity->owner != attacker->owner) {
                    if (Player* oldOwner = player(defenderOwner)) {
                        auto& ids = oldOwner->cityIds;
                        ids.erase(std::remove(ids.begin(), ids.end(), capturedCity->id), ids.end());
                        if (oldOwner->capitalCityId == capturedCity->id) {
                            oldOwner->capitalCityId = kInvalidCityId;
                        }
                    }
                    capturedCity->owner = attacker->owner;
                    capturedCity->isCapital = false; // a captured city never becomes a foreign capital
                    if (Player* newOwner = player(attacker->owner)) {
                        newOwner->cityIds.push_back(capturedCity->id);
                    }
                    if (Tile* t = grid_.tileAt(defenderCoord)) {
                        t->owner = attacker->owner;
                        t->isCapital = false;
                    }
                    // Territory around the city changes hands too, so the
                    // map's ownership borders redraw correctly.
                    for (int32_t i = 0; i < capturedCity->workedTileCount; ++i) {
                        if (Tile* wt = grid_.tileAt(capturedCity->workedTiles[static_cast<size_t>(i)])) {
                            wt->owner = attacker->owner;
                        }
                    }
                    result.cityCaptured = true;
                    updateEliminationStatus(defenderOwner);
                }
            }
            moveUnit(attackerId, defenderCoord);
        }
    }

    if (attacker->alive && attacker->health <= 0) {
        result.attackerKilled = true;
        killUnit(attackerId);
    }

    return result;
}

bool GameState::canHealUnit(UnitId unitId) const {
    const Unit* u = unit(unitId);
    if (!u || !u->alive || u->embarked) return false;
    if (u->health >= u->maxHealth()) return false;
    if (u->movesLeft <= 0.0f) return false;
    const City* c = cityAt(u->coord);
    return c && c->owner == u->owner;
}

bool GameState::healUnitAtCity(UnitId unitId) {
    if (!canHealUnit(unitId)) return false;
    Unit* u = unit(unitId);
    u->health = u->maxHealth();
    u->movesLeft = 0.0f;
    return true;
}

bool GameState::canDisbandUnit(UnitId unitId) const {
    const Unit* u = unit(unitId);
    if (!u || !u->alive || u->embarked) return false;
    if (u->isBoat() && u->passengerId != kInvalidUnitId) return false; // disembark the passenger first
    return true;
}

bool GameState::disbandUnit(UnitId unitId) {
    if (!canDisbandUnit(unitId)) return false;
    Unit* u = unit(unitId);
    const int32_t refund = (statsFor(u->type).cost + 1) / 2; // round up half cost
    if (Player* p = player(u->owner)) {
        p->stars += refund;
    }
    killUnit(unitId);
    return true;
}

void GameState::updateEliminationStatus(PlayerId id) {
    Player* p = player(id);
    if (!p) return;
    p->alive = !(p->cityIds.empty() && p->unitIds.empty());
}

void GameState::killUnit(UnitId id) {
    Unit* u = unit(id);
    if (!u) return;

    // A sunk boat takes its passenger with it. A killed passenger simply
    // frees up its former carrier's cargo slot.
    if (u->isBoat() && u->passengerId != kInvalidUnitId) {
        const UnitId passengerId = u->passengerId;
        u->passengerId = kInvalidUnitId;
        killUnit(passengerId);
        u = unit(id); // re-fetch: recursive killUnit() call may not move
                       // the vector, but re-fetching is cheap and safe.
    }
    if (u && u->embarked && u->carrierBoatId != kInvalidUnitId) {
        if (Unit* boat = unit(u->carrierBoatId)) {
            if (boat->passengerId == id) boat->passengerId = kInvalidUnitId;
        }
    }

    if (Tile* t = grid_.tileAt(u->coord)) {
        if (t->occupantUnit == id) t->occupantUnit = kInvalidUnitId;
    }
    const PlayerId owner = u->owner;
    removeUnitFromOwnerList(*u);
    u->alive = false;
    u->health = 0;
    freeUnitIds_.push_back(id);
    updateEliminationStatus(owner);
}

void GameState::removeUnitFromOwnerList(const Unit& u) {
    if (Player* p = player(u.owner)) {
        auto& ids = p->unitIds;
        ids.erase(std::remove(ids.begin(), ids.end(), u.id), ids.end());
    }
}

bool GameState::queueProduction(CityId cityId, UnitType type) {
    City* c = city(cityId);
    if (!c) return false;
    Player* p = player(c->owner);
    if (!p) return false;
    if (!p->unitUnlocked(type)) return false;
    c->producing = type;
    c->productionProgress = 0;
    return true;
}

bool GameState::researchTech(PlayerId playerId, TechId tech) {
    Player* p = player(playerId);
    if (!p) return false;
    if (!p->canResearch(tech)) return false;
    const TechDef& def = techDef(tech);
    p->stars -= def.cost;
    p->researched[static_cast<size_t>(tech)] = true;
    return true;
}

void GameState::applyCityProductionAndGrowth(City& c, Player& p) {
    // --- Income & growth from worked tiles ---
    int32_t starIncome = 1; // base city upkeep income
    int32_t growthPoints = 0;
    for (int32_t i = 0; i < c.workedTileCount; ++i) {
        const Tile* t = grid_.tileAt(c.workedTiles[static_cast<size_t>(i)]);
        if (!t) continue;
        if (t->resource == ResourceType::None) continue;
        switch (t->resource) {
            case ResourceType::Ore:
                starIncome += 1;
                break;
            case ResourceType::Fish:
            case ResourceType::Fruit:
            case ResourceType::Crop:
            case ResourceType::Game:
                growthPoints += 1;
                break;
            default:
                break;
        }
    }
    p.stars += starIncome;

    c.populationProgress += growthPoints;
    const int32_t needed = c.populationToGrow();
    while (c.populationProgress >= needed && c.level < 10) {
        c.populationProgress -= needed;
        c.population += 1;
        c.level += 1;
        // Re-scan worked tiles in case work radius just expanded. Tiles
        // already claimed by a *different* city are skipped so overlapping
        // work radii can't double-count the same resource in two
        // economies; tiles this city already claims are kept.
        c.workedTileCount = 0;
        forEachHexInRadius(c.coord, c.workRadius(), [&](const HexCoord& hc) {
            const Tile* t = grid_.tileAt(hc);
            if (t && t->isPassableLand() &&
                (t->parentCityId == kInvalidCityId || t->parentCityId == c.id) &&
                c.workedTileCount < kMaxWorkedTiles) {
                c.workedTiles[static_cast<size_t>(c.workedTileCount++)] = hc;
            }
        });
    }

    // --- Production ---
    if (c.isProducing()) {
        c.productionProgress += starIncome;
        const int32_t cost = statsFor(c.producing).cost;
        if (c.productionProgress >= cost) {
            if (unitAt(c.coord) == nullptr) {
                c.productionProgress -= cost;
                spawnUnit(c.producing, c.owner, c.coord);
                // Stays queued (repeat production) until player changes it;
                // mirrors Polytopia's "keep training" convenience behavior.
            }
            // else: the city tile is occupied (e.g. a unit is garrisoned
            // there), so hold the accumulated progress rather than
            // silently spending stars on a unit that can't appear -- it
            // will complete the moment the tile frees up.
        }
    }
}

void GameState::beginPlayerTurn(PlayerId playerId) {
    Player* p = player(playerId);
    if (!p) return;

    for (UnitId uid : p->unitIds) {
        if (Unit* u = unit(uid)) {
            u->resetForNewTurn();
        }
    }

    for (CityId cid : p->cityIds) {
        if (City* c = city(cid)) {
            applyCityProductionAndGrowth(*c, *p);
        }
    }

    fog_.recompute(playerId, grid_, units_, cities_);
}

void GameState::recomputeAllFog() {
    for (const Player& p : players_) {
        fog_.recompute(p.id, grid_, units_, cities_);
    }
}

PlayerId GameState::checkVictory() const {
    PlayerId lastAlive = kInvalidPlayerId;
    int32_t aliveCount = 0;
    bool humanAlive = false;
    for (const Player& p : players_) {
        const bool hasAssets = !p.cityIds.empty() || !p.unitIds.empty();
        if (hasAssets) {
            ++aliveCount;
            lastAlive = p.id;
            if (!p.isAI) humanAlive = true;
        }
    }
    if (aliveCount == 1) return lastAlive;

    if (aliveCount > 1 && !humanAlive) {
        // The human tribe has nothing left; rather than leaving them
        // stuck watching AI-only turns resolve turn after turn with no
        // input ever required from them, end the match now and credit
        // whichever surviving tribe currently has the highest score.
        PlayerId best = kInvalidPlayerId;
        int32_t bestScore = -1;
        for (const Player& p : players_) {
            if (p.cityIds.empty() && p.unitIds.empty()) continue;
            const int32_t score = computeScore(p.id);
            if (score > bestScore) {
                bestScore = score;
                best = p.id;
            }
        }
        return best;
    }

    return kInvalidPlayerId;
}

int32_t GameState::computeScore(PlayerId id) const {
    const Player* p = player(id);
    if (!p) return 0;

    int32_t score = p->stars;
    for (CityId cid : p->cityIds) {
        if (const City* c = city(cid)) {
            score += c->population * 10 + c->level * 5;
        }
    }
    for (size_t i = 0; i < p->researched.size(); ++i) {
        if (p->researched[i]) score += 8;
    }
    score += static_cast<int32_t>(p->unitIds.size()) * 4;
    return score;
}

int32_t GameState::livingPlayerCount() const {
    int32_t count = 0;
    for (const Player& p : players_) {
        if (!p.cityIds.empty() || !p.unitIds.empty()) ++count;
    }
    return count;
}

} // namespace poly
