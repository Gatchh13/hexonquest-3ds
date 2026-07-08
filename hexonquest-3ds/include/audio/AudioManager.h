#pragma once

#include <cstdint>
#include <array>
#include <3ds.h>

namespace poly {

using SoundHandle = int32_t;
constexpr SoundHandle kInvalidSoundHandle = -1;

// -----------------------------------------------------------------------
// Thin wrapper over libctru's ndsp mixer. Loads uncompressed PCM16 WAV
// files into linear (DMA-capable) memory and plays them on dedicated
// ndsp channels: channel 0 is reserved for music, channels 1-23 are
// round-robined for sound effects so overlapping SFX don't cut each
// other off.
// -----------------------------------------------------------------------
class AudioManager {
public:
    static constexpr int32_t kMaxClips = 24;
    static constexpr uint8_t kMusicChannel = 0;
    static constexpr uint8_t kFirstSfxChannel = 1;
    static constexpr uint8_t kLastSfxChannel = 23;

    bool init();
    void shutdown();

    // Loads a mono or stereo 16-bit PCM WAV from `path` (sdmc: or
    // romfs: prefixed). Returns kInvalidSoundHandle on failure (missing
    // file, unsupported format) rather than aborting -- missing audio
    // assets should never crash the game.
    SoundHandle loadSound(const char* path);

    void playSfx(SoundHandle handle, float volume = 1.0f);
    void playMusic(SoundHandle handle, float volume = 0.6f, bool loop = true);
    void stopMusic();

    void setMasterSfxVolume(float v) { sfxVolume_ = clampVolume(v); }
    void setMasterMusicVolume(float v) { musicVolume_ = clampVolume(v); }
    float masterSfxVolume() const { return sfxVolume_; }
    float masterMusicVolume() const { return musicVolume_; }

private:
    struct Clip {
        bool loaded = false;
        int16_t* samples = nullptr; // linearAlloc'd
        uint32_t sampleCount = 0;   // per-channel sample count
        uint32_t sampleRate = 44100;
        uint8_t channelCount = 1;
    };

    static float clampVolume(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    uint8_t nextSfxChannel();

    std::array<Clip, kMaxClips> clips_{};
    int32_t clipCount_ = 0;
    uint8_t nextChannel_ = kFirstSfxChannel;
    bool initialized_ = false;
    float sfxVolume_ = 1.0f;
    float musicVolume_ = 0.6f;

    // One persistent wavebuf per channel so ndsp always has valid,
    // stable-lifetime buffer descriptors to reference (ndsp keeps
    // pointers to these, they must outlive playback).
    std::array<ndspWaveBuf, 24> waveBufs_{};
};

} // namespace poly
