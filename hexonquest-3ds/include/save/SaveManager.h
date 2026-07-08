#pragma once

#include <cstddef>
#include "game/GameState.h"
#include "game/TurnManager.h"

namespace poly {

// -----------------------------------------------------------------------
// Binary save/load for a full match. Format is intentionally flat and
// versioned (kSaveMagic/kSaveVersion) so a future format change can
// detect and reject incompatible old saves instead of crashing.
// -----------------------------------------------------------------------
class SaveManager {
public:
    static constexpr uint32_t kSaveMagic = 0x48585131; // "HXQ1"
    static constexpr uint32_t kSaveVersion = 3;
    static constexpr int32_t kSlotCount = 3;

    // Ensures sdmc:/3ds/hexonquest exists. Safe to call repeatedly.
    static void ensureSaveDirectory();

    static bool saveGame(const GameState& state, int32_t turnNumber,
                          int32_t currentPlayerIndex, const char* path);

    // On success, populates `state` and returns the persisted turn
    // counters via out-params so the caller can restore its TurnManager.
    static bool loadGame(GameState& state, int32_t& outTurnNumber,
                          int32_t& outCurrentPlayerIndex, const char* path);

    static bool saveExists(const char* path);

    // Writes "sdmc:/3ds/hexonquest/save<slot+1>.dat" into outBuf.
    static void getSlotPath(int32_t slot, char* outBuf, size_t bufSize);

    struct SlotInfo {
        bool exists = false;
        int32_t turnNumber = 0;
    };
    // Cheaply inspects a save file's header (magic/version/turn number)
    // without loading the full game, for slot-picker previews.
    static SlotInfo peekSlot(const char* path);

    static constexpr const char* kDefaultSlotPath = "sdmc:/3ds/hexonquest/save1.dat";
};

} // namespace poly
