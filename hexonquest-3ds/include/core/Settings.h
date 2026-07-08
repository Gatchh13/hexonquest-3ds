#pragma once

#include <cstdint>

namespace poly {

struct Settings {
    float musicVolume = 0.6f;
    float sfxVolume = 1.0f;
    bool showGridCoordinates = false;
    bool showMinimapOnTop = false;
    bool tutorialSeen = false;
    int32_t lastMapSizeIndex = 1;   // remembers the New Game setup screen's last choice
    int32_t lastOpponentCount = 1;

    static constexpr uint32_t kMagic = 0x53455432; // "SET2"
    static constexpr const char* kPath = "sdmc:/3ds/hexonquest/settings.dat";

    // Loads from disk, silently falling back to defaults (already set by
    // the in-class member initializers above) if the file is missing or
    // corrupt -- a missing settings file must never be a fatal error.
    bool load();
    bool save() const;
};

} // namespace poly
