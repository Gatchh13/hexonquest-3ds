#include "audio/AudioManager.h"
#include <cstdio>
#include <cstring>

namespace poly {

namespace {

struct WavHeader {
    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
};

// Minimal RIFF/WAVE chunk walker. Supports only uncompressed PCM16.
// Returns the byte offset and size of the "data" chunk on success.
bool parseWavHeader(FILE* f, WavHeader& outHeader, uint32_t& outDataSize) {
    char riffId[4];
    uint32_t riffSize = 0;
    char waveId[4];
    if (std::fread(riffId, 1, 4, f) != 4) return false;
    if (std::fread(&riffSize, 4, 1, f) != 1) return false;
    if (std::fread(waveId, 1, 4, f) != 4) return false;
    if (std::memcmp(riffId, "RIFF", 4) != 0 || std::memcmp(waveId, "WAVE", 4) != 0) return false;

    bool haveFmt = false;
    bool haveData = false;
    while (!haveData) {
        char chunkId[4];
        uint32_t chunkSize = 0;
        if (std::fread(chunkId, 1, 4, f) != 4) return false;
        if (std::fread(&chunkSize, 4, 1, f) != 1) return false;

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            uint16_t audioFormat = 0, numChannels = 0, blockAlign = 0, bitsPerSample = 0;
            uint32_t sampleRate = 0, byteRate = 0;
            if (std::fread(&audioFormat, 2, 1, f) != 1) return false;
            if (std::fread(&numChannels, 2, 1, f) != 1) return false;
            if (std::fread(&sampleRate, 4, 1, f) != 1) return false;
            if (std::fread(&byteRate, 4, 1, f) != 1) return false;
            if (std::fread(&blockAlign, 2, 1, f) != 1) return false;
            if (std::fread(&bitsPerSample, 2, 1, f) != 1) return false;
            outHeader.audioFormat = audioFormat;
            outHeader.numChannels = numChannels;
            outHeader.sampleRate = sampleRate;
            outHeader.bitsPerSample = bitsPerSample;
            haveFmt = true;

            const long extraBytes = static_cast<long>(chunkSize) - 16;
            if (extraBytes > 0) std::fseek(f, extraBytes, SEEK_CUR);
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            if (!haveFmt) return false;
            outDataSize = chunkSize;
            haveData = true; // leave the file position at the start of sample data
        } else {
            // Unknown chunk (e.g. "LIST", "fact"): skip it.
            std::fseek(f, static_cast<long>(chunkSize), SEEK_CUR);
        }

        // Chunks are word-aligned; skip a pad byte if size is odd.
        if ((chunkSize & 1) != 0 && !haveData) {
            std::fseek(f, 1, SEEK_CUR);
        }
    }
    return haveFmt && haveData;
}

} // namespace

bool AudioManager::init() {
    if (ndspInit() != 0) {
        return false;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);

    for (auto& wb : waveBufs_) {
        std::memset(&wb, 0, sizeof(wb));
    }

    initialized_ = true;
    return true;
}

void AudioManager::shutdown() {
    if (!initialized_) return;
    for (int32_t i = 0; i < clipCount_; ++i) {
        if (clips_[static_cast<size_t>(i)].samples) {
            linearFree(clips_[static_cast<size_t>(i)].samples);
            clips_[static_cast<size_t>(i)].samples = nullptr;
        }
    }
    clipCount_ = 0;
    ndspExit();
    initialized_ = false;
}

SoundHandle AudioManager::loadSound(const char* path) {
    if (!initialized_) return kInvalidSoundHandle;
    if (clipCount_ >= kMaxClips) return kInvalidSoundHandle;

    FILE* f = std::fopen(path, "rb");
    if (!f) return kInvalidSoundHandle;

    WavHeader header;
    uint32_t dataSize = 0;
    if (!parseWavHeader(f, header, dataSize)) {
        std::fclose(f);
        return kInvalidSoundHandle;
    }
    if (header.audioFormat != 1 /* PCM */ || header.bitsPerSample != 16 ||
        (header.numChannels != 1 && header.numChannels != 2)) {
        std::fclose(f);
        return kInvalidSoundHandle; // unsupported format; fail gracefully
    }

    int16_t* buffer = static_cast<int16_t*>(linearAlloc(dataSize));
    if (!buffer) {
        std::fclose(f);
        return kInvalidSoundHandle;
    }
    const size_t bytesRead = std::fread(buffer, 1, dataSize, f);
    std::fclose(f);
    if (bytesRead != dataSize) {
        linearFree(buffer);
        return kInvalidSoundHandle;
    }

    Clip& clip = clips_[static_cast<size_t>(clipCount_)];
    clip.loaded = true;
    clip.samples = buffer;
    clip.channelCount = static_cast<uint8_t>(header.numChannels);
    clip.sampleRate = header.sampleRate;
    clip.sampleCount = dataSize / (static_cast<uint32_t>(header.numChannels) * sizeof(int16_t));

    const SoundHandle handle = clipCount_;
    ++clipCount_;
    return handle;
}

uint8_t AudioManager::nextSfxChannel() {
    const uint8_t chosen = nextChannel_;
    ++nextChannel_;
    if (nextChannel_ > kLastSfxChannel) nextChannel_ = kFirstSfxChannel;
    return chosen;
}

namespace {
void configureChannel(uint8_t channel, const AudioManager* /*unused*/, uint32_t sampleRate,
                       uint8_t channelCount, float volume) {
    ndspChnReset(channel);
    ndspChnSetInterp(channel, NDSP_INTERP_LINEAR);
    ndspChnSetRate(channel, static_cast<float>(sampleRate));
    ndspChnSetFormat(channel, channelCount == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);

    float mix[12];
    std::memset(mix, 0, sizeof(mix));
    mix[0] = volume; // front-left
    mix[1] = volume; // front-right
    ndspChnSetMix(channel, mix);
}
} // namespace

void AudioManager::playSfx(SoundHandle handle, float volume) {
    if (!initialized_) return;
    if (handle < 0 || handle >= clipCount_) return;
    Clip& clip = clips_[static_cast<size_t>(handle)];
    if (!clip.loaded) return;

    const uint8_t channel = nextSfxChannel();
    configureChannel(channel, this, clip.sampleRate, clip.channelCount, volume * sfxVolume_);

    ndspWaveBuf& wb = waveBufs_[channel];
    std::memset(&wb, 0, sizeof(wb));
    wb.data_vaddr = clip.samples;
    wb.nsamples = clip.sampleCount;
    wb.looping = false;
    wb.status = NDSP_WBUF_FREE;

    DSP_FlushDataCache(clip.samples, clip.sampleCount * clip.channelCount * sizeof(int16_t));
    ndspChnWaveBufAdd(channel, &wb);
}

void AudioManager::playMusic(SoundHandle handle, float volume, bool loop) {
    if (!initialized_) return;
    if (handle < 0 || handle >= clipCount_) return;
    Clip& clip = clips_[static_cast<size_t>(handle)];
    if (!clip.loaded) return;

    configureChannel(kMusicChannel, this, clip.sampleRate, clip.channelCount, volume * musicVolume_);

    ndspWaveBuf& wb = waveBufs_[kMusicChannel];
    std::memset(&wb, 0, sizeof(wb));
    wb.data_vaddr = clip.samples;
    wb.nsamples = clip.sampleCount;
    wb.looping = loop;
    wb.status = NDSP_WBUF_FREE;

    DSP_FlushDataCache(clip.samples, clip.sampleCount * clip.channelCount * sizeof(int16_t));
    ndspChnWaveBufAdd(kMusicChannel, &wb);
}

void AudioManager::stopMusic() {
    if (!initialized_) return;
    ndspChnReset(kMusicChannel);
}

} // namespace poly
