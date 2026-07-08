#include "save/SaveManager.h"
#include <cstdio>
#include <sys/stat.h>
#include <cstring>

namespace poly {

namespace {

template <typename T>
bool writeRaw(FILE* f, const T& value) {
    return std::fwrite(&value, sizeof(T), 1, f) == 1;
}

template <typename T>
bool readRaw(FILE* f, T& value) {
    return std::fread(&value, sizeof(T), 1, f) == 1;
}

} // namespace

void SaveManager::ensureSaveDirectory() {
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/hexonquest", 0777);
}

bool SaveManager::saveExists(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

void SaveManager::getSlotPath(int32_t slot, char* outBuf, size_t bufSize) {
    std::snprintf(outBuf, bufSize, "sdmc:/3ds/hexonquest/save%d.dat", slot + 1);
}

SaveManager::SlotInfo SaveManager::peekSlot(const char* path) {
    SlotInfo info;
    FILE* f = std::fopen(path, "rb");
    if (!f) return info;

    uint32_t magic = 0, version = 0;
    int32_t turnNumber = 0;
    bool ok = true;
    ok &= std::fread(&magic, sizeof(magic), 1, f) == 1;
    ok &= std::fread(&version, sizeof(version), 1, f) == 1;
    ok &= std::fread(&turnNumber, sizeof(turnNumber), 1, f) == 1;
    std::fclose(f);

    if (ok && magic == kSaveMagic && version == kSaveVersion) {
        info.exists = true;
        info.turnNumber = turnNumber;
    }
    return info;
}

bool SaveManager::saveGame(const GameState& state, int32_t turnNumber,
                            int32_t currentPlayerIndex, const char* path) {
    ensureSaveDirectory();
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    bool ok = true;
    ok &= writeRaw(f, kSaveMagic);
    ok &= writeRaw(f, kSaveVersion);
    ok &= writeRaw(f, turnNumber);
    ok &= writeRaw(f, currentPlayerIndex);

    // --- Grid ---
    const int32_t width = state.grid_.width();
    const int32_t height = state.grid_.height();
    ok &= writeRaw(f, width);
    ok &= writeRaw(f, height);
    for (const Tile& t : state.grid_.tiles()) {
        ok &= writeRaw(f, t.terrain);
        ok &= writeRaw(f, t.resource);
        const uint8_t harvested = t.resourceHarvested ? 1 : 0;
        ok &= writeRaw(f, harvested);
        ok &= writeRaw(f, t.owner);
        ok &= writeRaw(f, t.cityId);
        ok &= writeRaw(f, t.parentCityId);
        ok &= writeRaw(f, t.occupantUnit);
        const uint8_t hasRoad = t.hasRoad ? 1 : 0;
        ok &= writeRaw(f, hasRoad);
        const uint8_t isCapital = t.isCapital ? 1 : 0;
        ok &= writeRaw(f, isCapital);
        ok &= writeRaw(f, t.elevation);
    }

    // --- Players ---
    const int32_t numPlayers = static_cast<int32_t>(state.players_.size());
    ok &= writeRaw(f, numPlayers);
    for (const Player& p : state.players_) {
        ok &= writeRaw(f, p.id);
        ok &= std::fwrite(p.name, sizeof(p.name), 1, f) == 1;
        const uint8_t isAI = p.isAI ? 1 : 0;
        ok &= writeRaw(f, isAI);
        const uint8_t alive = p.alive ? 1 : 0;
        ok &= writeRaw(f, alive);
        ok &= writeRaw(f, p.color);
        ok &= writeRaw(f, p.stars);
        for (bool r : p.researched) {
            const uint8_t v = r ? 1 : 0;
            ok &= writeRaw(f, v);
        }
        ok &= writeRaw(f, p.capitalCityId);
    }

    // --- Units (including dead tombstones, to keep ids stable) ---
    const int32_t numUnits = static_cast<int32_t>(state.units_.size());
    ok &= writeRaw(f, numUnits);
    for (const Unit& u : state.units_) {
        ok &= writeRaw(f, u.id);
        ok &= writeRaw(f, u.type);
        ok &= writeRaw(f, u.owner);
        ok &= writeRaw(f, u.coord);
        ok &= writeRaw(f, u.health);
        ok &= writeRaw(f, u.movesLeft);
        const uint8_t hasAttacked = u.hasAttacked ? 1 : 0;
        ok &= writeRaw(f, hasAttacked);
        const uint8_t veteran = u.veteran ? 1 : 0;
        ok &= writeRaw(f, veteran);
        const uint8_t alive = u.alive ? 1 : 0;
        ok &= writeRaw(f, alive);
        const uint8_t embarked = u.embarked ? 1 : 0;
        ok &= writeRaw(f, embarked);
        ok &= writeRaw(f, u.carrierBoatId);
        ok &= writeRaw(f, u.passengerId);
        ok &= writeRaw(f, u.killCount);
    }
    const int32_t numFreeUnitIds = static_cast<int32_t>(state.freeUnitIds_.size());
    ok &= writeRaw(f, numFreeUnitIds);
    for (UnitId id : state.freeUnitIds_) ok &= writeRaw(f, id);

    // --- Cities ---
    const int32_t numCities = static_cast<int32_t>(state.cities_.size());
    ok &= writeRaw(f, numCities);
    for (const City& c : state.cities_) {
        ok &= writeRaw(f, c.id);
        ok &= writeRaw(f, c.owner);
        ok &= writeRaw(f, c.coord);
        ok &= std::fwrite(c.name, sizeof(c.name), 1, f) == 1;
        ok &= writeRaw(f, c.population);
        ok &= writeRaw(f, c.populationProgress);
        ok &= writeRaw(f, c.level);
        const uint8_t isCapital = c.isCapital ? 1 : 0;
        ok &= writeRaw(f, isCapital);
        const uint8_t alive = c.alive ? 1 : 0;
        ok &= writeRaw(f, alive);
        ok &= writeRaw(f, c.producing);
        ok &= writeRaw(f, c.productionProgress);
        ok &= writeRaw(f, c.workedTileCount);
        for (int32_t i = 0; i < c.workedTileCount; ++i) {
            ok &= writeRaw(f, c.workedTiles[static_cast<size_t>(i)]);
        }
    }
    const int32_t numFreeCityIds = static_cast<int32_t>(state.freeCityIds_.size());
    ok &= writeRaw(f, numFreeCityIds);
    for (CityId id : state.freeCityIds_) ok &= writeRaw(f, id);

    std::fclose(f);
    return ok;
}

bool SaveManager::loadGame(GameState& state, int32_t& outTurnNumber,
                            int32_t& outCurrentPlayerIndex, const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;

    bool ok = true;
    uint32_t magic = 0;
    uint32_t version = 0;
    ok &= readRaw(f, magic);
    ok &= readRaw(f, version);
    if (!ok || magic != kSaveMagic || version != kSaveVersion) {
        std::fclose(f);
        return false;
    }

    ok &= readRaw(f, outTurnNumber);
    ok &= readRaw(f, outCurrentPlayerIndex);

    int32_t width = 0, height = 0;
    ok &= readRaw(f, width);
    ok &= readRaw(f, height);
    if (!ok || width <= 0 || height <= 0) {
        std::fclose(f);
        return false;
    }

    // Allocate a grid of the right size/coords, then overwrite mutable
    // fields from the save (deterministic seed value here is irrelevant
    // since every field gets replaced below).
    state.grid_.generate(width, height, 0);
    for (Tile& t : state.grid_.tilesMutable()) {
        ok &= readRaw(f, t.terrain);
        ok &= readRaw(f, t.resource);
        uint8_t harvested = 0;
        ok &= readRaw(f, harvested);
        t.resourceHarvested = harvested != 0;
        ok &= readRaw(f, t.owner);
        ok &= readRaw(f, t.cityId);
        ok &= readRaw(f, t.parentCityId);
        ok &= readRaw(f, t.occupantUnit);
        uint8_t hasRoad = 0;
        ok &= readRaw(f, hasRoad);
        t.hasRoad = hasRoad != 0;
        uint8_t isCapital = 0;
        ok &= readRaw(f, isCapital);
        t.isCapital = isCapital != 0;
        ok &= readRaw(f, t.elevation);
    }

    int32_t numPlayers = 0;
    ok &= readRaw(f, numPlayers);
    state.players_.assign(static_cast<size_t>(numPlayers), Player{});
    for (Player& p : state.players_) {
        ok &= readRaw(f, p.id);
        ok &= std::fread(p.name, sizeof(p.name), 1, f) == 1;
        uint8_t isAI = 0;
        ok &= readRaw(f, isAI);
        p.isAI = isAI != 0;
        uint8_t alive = 0;
        ok &= readRaw(f, alive);
        p.alive = alive != 0;
        ok &= readRaw(f, p.color);
        ok &= readRaw(f, p.stars);
        for (size_t i = 0; i < p.researched.size(); ++i) {
            uint8_t v = 0;
            ok &= readRaw(f, v);
            p.researched[i] = v != 0;
        }
        ok &= readRaw(f, p.capitalCityId);
        p.unitIds.clear();
        p.cityIds.clear();
    }

    int32_t numUnits = 0;
    ok &= readRaw(f, numUnits);
    state.units_.assign(static_cast<size_t>(numUnits), Unit{});
    for (Unit& u : state.units_) {
        ok &= readRaw(f, u.id);
        ok &= readRaw(f, u.type);
        ok &= readRaw(f, u.owner);
        ok &= readRaw(f, u.coord);
        ok &= readRaw(f, u.health);
        ok &= readRaw(f, u.movesLeft);
        uint8_t hasAttacked = 0;
        ok &= readRaw(f, hasAttacked);
        u.hasAttacked = hasAttacked != 0;
        uint8_t veteran = 0;
        ok &= readRaw(f, veteran);
        u.veteran = veteran != 0;
        uint8_t alive = 0;
        ok &= readRaw(f, alive);
        u.alive = alive != 0;
        uint8_t embarked = 0;
        ok &= readRaw(f, embarked);
        u.embarked = embarked != 0;
        ok &= readRaw(f, u.carrierBoatId);
        ok &= readRaw(f, u.passengerId);
        ok &= readRaw(f, u.killCount);
        if (u.alive) {
            if (Player* owner = state.player(u.owner)) {
                owner->unitIds.push_back(u.id);
            }
        }
    }
    int32_t numFreeUnitIds = 0;
    ok &= readRaw(f, numFreeUnitIds);
    state.freeUnitIds_.assign(static_cast<size_t>(numFreeUnitIds), kInvalidUnitId);
    for (auto& id : state.freeUnitIds_) ok &= readRaw(f, id);

    int32_t numCities = 0;
    ok &= readRaw(f, numCities);
    state.cities_.assign(static_cast<size_t>(numCities), City{});
    for (City& c : state.cities_) {
        ok &= readRaw(f, c.id);
        ok &= readRaw(f, c.owner);
        ok &= readRaw(f, c.coord);
        ok &= std::fread(c.name, sizeof(c.name), 1, f) == 1;
        ok &= readRaw(f, c.population);
        ok &= readRaw(f, c.populationProgress);
        ok &= readRaw(f, c.level);
        uint8_t isCapital = 0;
        ok &= readRaw(f, isCapital);
        c.isCapital = isCapital != 0;
        uint8_t alive = 0;
        ok &= readRaw(f, alive);
        c.alive = alive != 0;
        ok &= readRaw(f, c.producing);
        ok &= readRaw(f, c.productionProgress);
        ok &= readRaw(f, c.workedTileCount);
        for (int32_t i = 0; i < c.workedTileCount; ++i) {
            ok &= readRaw(f, c.workedTiles[static_cast<size_t>(i)]);
        }
        if (c.alive) {
            if (Player* owner = state.player(c.owner)) {
                owner->cityIds.push_back(c.id);
            }
        }
    }
    int32_t numFreeCityIds = 0;
    ok &= readRaw(f, numFreeCityIds);
    state.freeCityIds_.assign(static_cast<size_t>(numFreeCityIds), kInvalidCityId);
    for (auto& id : state.freeCityIds_) ok &= readRaw(f, id);

    std::fclose(f);

    if (ok) {
        state.fog_.init(numPlayers, width, height);
        state.recomputeAllFog();
    }
    return ok;
}

} // namespace poly
