#pragma once

#include <array>
#include <cstdint>
#include <cmath>
#include "core/Types.h"
#include "render/Renderer.h"

namespace poly {

enum class ParticleShape : uint8_t { Circle, Square };

struct Particle {
    bool active = false;
    Vec2 position{};
    Vec2 velocity{};
    float lifeRemaining = 0.0f;
    float lifeTotal = 0.0f;
    float startRadius = 2.0f;
    float endRadius = 0.0f;
    Color4 colorStart{255, 255, 255, 255};
    Color4 colorEnd{255, 255, 255, 0};
    ParticleShape shape = ParticleShape::Circle;
};

// -----------------------------------------------------------------------
// Fixed-capacity particle pool. All emission functions are O(1) and
// never allocate; once the pool is full, new emissions are silently
// dropped (graceful degradation rather than growing memory on a
// memory-constrained handheld).
// -----------------------------------------------------------------------
class ParticleSystem {
public:
    static constexpr size_t kMaxParticles = 128;

    void update(float dt) {
        for (Particle& p : particles_) {
            if (!p.active) continue;
            p.lifeRemaining -= dt;
            if (p.lifeRemaining <= 0.0f) {
                p.active = false;
                continue;
            }
            p.position += p.velocity * dt;
            p.velocity *= (1.0f - clampf(dt * 1.5f, 0.0f, 1.0f)); // drag
        }
    }

    void render(const Renderer& renderer) const {
        for (const Particle& p : particles_) {
            if (!p.active) continue;
            const float t = 1.0f - (p.lifeRemaining / p.lifeTotal);
            const float radius = lerp(p.startRadius, p.endRadius, t);
            Color4 color;
            color.r = static_cast<uint8_t>(lerp(p.colorStart.r, p.colorEnd.r, t));
            color.g = static_cast<uint8_t>(lerp(p.colorStart.g, p.colorEnd.g, t));
            color.b = static_cast<uint8_t>(lerp(p.colorStart.b, p.colorEnd.b, t));
            color.a = static_cast<uint8_t>(lerp(p.colorStart.a, p.colorEnd.a, t));
            if (p.shape == ParticleShape::Circle) {
                renderer.drawCircle(p.position, radius, color);
            } else {
                renderer.drawRect(p.position.x - radius, p.position.y - radius,
                                   radius * 2.0f, radius * 2.0f, color);
            }
        }
    }

    // Emits a small radial burst (used for combat impacts, unit death).
    void emitBurst(const Vec2& origin, int32_t count, const Color4& colorStart,
                    const Color4& colorEnd, float speed, float lifeSeconds,
                    ParticleShape shape = ParticleShape::Circle) {
        for (int32_t i = 0; i < count; ++i) {
            Particle* slot = findFreeSlot();
            if (!slot) return;
            const float angle = (6.2831853f * static_cast<float>(i)) / static_cast<float>(count);
            slot->active = true;
            slot->position = origin;
            slot->velocity = Vec2(std::cos(angle) * speed, std::sin(angle) * speed);
            slot->lifeTotal = lifeSeconds;
            slot->lifeRemaining = lifeSeconds;
            slot->startRadius = 3.0f;
            slot->endRadius = 0.5f;
            slot->colorStart = colorStart;
            slot->colorEnd = colorEnd;
            slot->shape = shape;
        }
    }

    // Emits a single upward "sparkle" particle (used for city founding /
    // level-up feedback), staggered slightly so repeated calls look like
    // a small fountain rather than a single dot.
    void emitSparkle(const Vec2& origin, const Color4& color) {
        Particle* slot = findFreeSlot();
        if (!slot) return;
        slot->active = true;
        slot->position = origin;
        slot->velocity = Vec2(rangeJitter(-10.0f, 10.0f), rangeJitter(-30.0f, -10.0f));
        slot->lifeTotal = 0.8f;
        slot->lifeRemaining = 0.8f;
        slot->startRadius = 2.0f;
        slot->endRadius = 0.0f;
        slot->colorStart = color;
        slot->colorEnd = Color4(color.r, color.g, color.b, 0);
        slot->shape = ParticleShape::Circle;
    }

    void clear() {
        for (Particle& p : particles_) p.active = false;
    }

private:
    Particle* findFreeSlot() {
        for (Particle& p : particles_) {
            if (!p.active) return &p;
        }
        return nullptr;
    }

    static float rangeJitter(float lo, float hi) {
        // Cheap deterministic-ish jitter without pulling in a full RNG
        // object here; particle visuals don't need cryptographic
        // randomness, just enough variety to avoid looking uniform.
        jitterState_ = jitterState_ * 1103515245u + 12345u;
        const float t = static_cast<float>((jitterState_ >> 16) & 0x7FFF) / 32767.0f;
        return lo + t * (hi - lo);
    }

    std::array<Particle, kMaxParticles> particles_{};
    static inline uint32_t jitterState_ = 0x1234ABCDu;
};

} // namespace poly
