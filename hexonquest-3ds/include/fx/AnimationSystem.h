#pragma once

#include <array>
#include <cstdint>
#include "core/Types.h"
#include "world/Tile.h"

namespace poly {

enum class AnimType : uint8_t { UnitMove, UnitAttackLunge, CityPulse };

struct Animation {
    bool active = false;
    AnimType type = AnimType::UnitMove;
    int32_t targetId = -1; // UnitId or CityId depending on type
    Vec2 fromPos{};
    Vec2 toPos{};
    float duration = 0.3f;
    float elapsed = 0.0f;
};

// -----------------------------------------------------------------------
// Drives short-lived visual interpolations so gameplay state (which is
// always instantly authoritative) can still be *presented* smoothly.
// Rendering code queries getUnitOffset()/getCityPulseScale() each frame
// and adds the result on top of the unit/city's logical grid position.
// -----------------------------------------------------------------------
class AnimationSystem {
public:
    static constexpr size_t kMaxAnimations = 32;

    void update(float dt) {
        for (Animation& a : animations_) {
            if (!a.active) continue;
            a.elapsed += dt;
            if (a.elapsed >= a.duration) {
                a.active = false;
            }
        }
    }

    void playUnitMove(UnitId unitId, const Vec2& from, const Vec2& to, float duration = 0.22f) {
        Animation* slot = findFreeSlot();
        if (!slot) return;
        slot->active = true;
        slot->type = AnimType::UnitMove;
        slot->targetId = unitId;
        slot->fromPos = from;
        slot->toPos = to;
        slot->duration = duration;
        slot->elapsed = 0.0f;
    }

    void playAttackLunge(UnitId unitId, const Vec2& from, const Vec2& towards, float duration = 0.18f) {
        Animation* slot = findFreeSlot();
        if (!slot) return;
        slot->active = true;
        slot->type = AnimType::UnitAttackLunge;
        slot->targetId = unitId;
        slot->fromPos = from;
        // "toPos" here holds the peak displacement point, not a final
        // resting position: the unit lunges toward the target and
        // springs back to its own tile.
        const Vec2 dir = towards - from;
        const float len = dir.length();
        const Vec2 norm = (len > 0.0001f) ? dir * (1.0f / len) : Vec2(0.0f, 0.0f);
        slot->toPos = from + norm * 8.0f;
        slot->duration = duration;
        slot->elapsed = 0.0f;
    }

    void playCityPulse(CityId cityId, float duration = 0.4f) {
        Animation* slot = findFreeSlot();
        if (!slot) return;
        slot->active = true;
        slot->type = AnimType::CityPulse;
        slot->targetId = cityId;
        slot->fromPos = Vec2(0.0f, 0.0f);
        slot->toPos = Vec2(0.0f, 0.0f);
        slot->duration = duration;
        slot->elapsed = 0.0f;
    }

    // Returns the screen-space offset to add to a unit's resting
    // (destination) position this frame; zero if nothing is animating
    // for that unit.
    Vec2 getUnitOffset(UnitId unitId) const {
        for (const Animation& a : animations_) {
            if (!a.active || a.targetId != unitId) continue;
            const float t = clampf(a.elapsed / a.duration, 0.0f, 1.0f);
            if (a.type == AnimType::UnitMove) {
                const Vec2 interpolated(lerp(a.fromPos.x, a.toPos.x, t), lerp(a.fromPos.y, a.toPos.y, t));
                return interpolated - a.toPos; // caller adds this to the logical (destination) position
            }
            if (a.type == AnimType::UnitAttackLunge) {
                // Out-and-back: 0 -> 1 -> 0 across the duration.
                const float phase = (t < 0.5f) ? (t * 2.0f) : (1.0f - (t - 0.5f) * 2.0f);
                return (a.toPos - a.fromPos) * phase;
            }
        }
        return Vec2(0.0f, 0.0f);
    }

    // Returns a scale multiplier (1.0 = normal size) for city pulse
    // feedback on level-up / founding.
    float getCityPulseScale(CityId cityId) const {
        for (const Animation& a : animations_) {
            if (!a.active || a.type != AnimType::CityPulse || a.targetId != cityId) continue;
            const float t = clampf(a.elapsed / a.duration, 0.0f, 1.0f);
            const float phase = (t < 0.5f) ? (t * 2.0f) : (1.0f - (t - 0.5f) * 2.0f);
            return 1.0f + phase * 0.35f;
        }
        return 1.0f;
    }

    bool isUnitAnimating(UnitId unitId) const {
        for (const Animation& a : animations_) {
            if (a.active && a.targetId == unitId &&
                (a.type == AnimType::UnitMove || a.type == AnimType::UnitAttackLunge)) {
                return true;
            }
        }
        return false;
    }

    void clear() {
        for (Animation& a : animations_) a.active = false;
    }

private:
    Animation* findFreeSlot() {
        for (Animation& a : animations_) {
            if (!a.active) return &a;
        }
        return nullptr;
    }

    std::array<Animation, kMaxAnimations> animations_{};
};

} // namespace poly
