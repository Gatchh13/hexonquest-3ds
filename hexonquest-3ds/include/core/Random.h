#pragma once

#include <cstdint>

namespace poly {

// Fast, allocation-free, deterministic PRNG. Using our own implementation
// (rather than <random>'s heavier engines) keeps codegen small and
// generation reproducible across platforms given the same seed —
// important for save/load and network-free "seeded map" sharing.
class Random {
public:
    explicit Random(uint64_t seed = 0x9E3779B97F4A7C15ULL) {
        // SplitMix64 to spread a possibly-weak seed into two 64-bit states.
        uint64_t z = seed;
        state0_ = splitMix64(z);
        state1_ = splitMix64(z);
        if (state0_ == 0 && state1_ == 0) {
            state0_ = 1;
        }
    }

    uint64_t nextU64() {
        uint64_t s1 = state0_;
        const uint64_t s0 = state1_;
        state0_ = s0;
        s1 ^= s1 << 23;
        s1 ^= s1 >> 17;
        s1 ^= s0;
        s1 ^= s0 >> 26;
        state1_ = s1;
        return state0_ + state1_;
    }

    uint32_t nextU32() { return static_cast<uint32_t>(nextU64() >> 32); }

    // Uniform int in [lo, hi] inclusive.
    int32_t range(int32_t lo, int32_t hi) {
        if (hi <= lo) return lo;
        const uint32_t span = static_cast<uint32_t>(hi - lo + 1);
        return lo + static_cast<int32_t>(nextU32() % span);
    }

    // Uniform float in [0, 1).
    float nextFloat() {
        return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f);
    }

    // Uniform float in [lo, hi).
    float rangeF(float lo, float hi) {
        return lo + nextFloat() * (hi - lo);
    }

    bool chance(float probability) { return nextFloat() < probability; }

private:
    static uint64_t splitMix64(uint64_t& x) {
        x += 0x9E3779B97F4A7C15ULL;
        uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    uint64_t state0_;
    uint64_t state1_;
};

} // namespace poly
