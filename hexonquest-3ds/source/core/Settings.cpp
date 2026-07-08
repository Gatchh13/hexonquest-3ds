#include "core/Settings.h"
#include <cstdio>
#include <sys/stat.h>

namespace poly {

bool Settings::save() const {
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/hexonquest", 0777);

    FILE* f = std::fopen(kPath, "wb");
    if (!f) return false;

    bool ok = true;
    const uint32_t magic = kMagic;
    ok &= std::fwrite(&magic, sizeof(magic), 1, f) == 1;
    ok &= std::fwrite(&musicVolume, sizeof(musicVolume), 1, f) == 1;
    ok &= std::fwrite(&sfxVolume, sizeof(sfxVolume), 1, f) == 1;
    const uint8_t gridFlag = showGridCoordinates ? 1 : 0;
    ok &= std::fwrite(&gridFlag, sizeof(gridFlag), 1, f) == 1;
    const uint8_t minimapFlag = showMinimapOnTop ? 1 : 0;
    ok &= std::fwrite(&minimapFlag, sizeof(minimapFlag), 1, f) == 1;
    const uint8_t tutorialFlag = tutorialSeen ? 1 : 0;
    ok &= std::fwrite(&tutorialFlag, sizeof(tutorialFlag), 1, f) == 1;
    ok &= std::fwrite(&lastMapSizeIndex, sizeof(lastMapSizeIndex), 1, f) == 1;
    ok &= std::fwrite(&lastOpponentCount, sizeof(lastOpponentCount), 1, f) == 1;

    std::fclose(f);
    return ok;
}

bool Settings::load() {
    FILE* f = std::fopen(kPath, "rb");
    if (!f) return false; // no settings file yet: keep defaults

    uint32_t magic = 0;
    bool ok = true;
    ok &= std::fread(&magic, sizeof(magic), 1, f) == 1;
    if (!ok || magic != kMagic) {
        std::fclose(f);
        return false;
    }

    float loadedMusic = musicVolume;
    float loadedSfx = sfxVolume;
    uint8_t gridFlag = 0;
    uint8_t minimapFlag = 0;
    uint8_t tutorialFlag = 0;
    int32_t sizeIndex = lastMapSizeIndex;
    int32_t opponents = lastOpponentCount;

    ok &= std::fread(&loadedMusic, sizeof(loadedMusic), 1, f) == 1;
    ok &= std::fread(&loadedSfx, sizeof(loadedSfx), 1, f) == 1;
    ok &= std::fread(&gridFlag, sizeof(gridFlag), 1, f) == 1;
    ok &= std::fread(&minimapFlag, sizeof(minimapFlag), 1, f) == 1;
    ok &= std::fread(&tutorialFlag, sizeof(tutorialFlag), 1, f) == 1;
    ok &= std::fread(&sizeIndex, sizeof(sizeIndex), 1, f) == 1;
    ok &= std::fread(&opponents, sizeof(opponents), 1, f) == 1;
    std::fclose(f);

    if (!ok) return false;

    musicVolume = loadedMusic;
    sfxVolume = loadedSfx;
    showGridCoordinates = gridFlag != 0;
    showMinimapOnTop = minimapFlag != 0;
    tutorialSeen = tutorialFlag != 0;
    lastMapSizeIndex = sizeIndex;
    lastOpponentCount = opponents;
    return true;
}

} // namespace poly
