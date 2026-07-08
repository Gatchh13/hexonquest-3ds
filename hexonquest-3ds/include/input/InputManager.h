#pragma once

#include <3ds.h>
#include "core/Types.h"
#include "core/HexCoord.h"

namespace poly {

enum class TouchGesture : uint8_t {
    None = 0,
    Tap,
    DragStart,
    Dragging,
    DragEnd
};

struct FrameInput {
    // Raw button state this frame.
    u32 keysDown = 0;
    u32 keysHeld = 0;
    u32 keysUp = 0;

    // Circle pad, normalized to [-1, 1] per axis.
    Vec2 circlePad{0.0f, 0.0f};

    // Touch (bottom screen only, per 3DS hardware).
    bool touching = false;
    Vec2 touchPos{0.0f, 0.0f};
    TouchGesture gesture = TouchGesture::None;
    Vec2 dragDelta{0.0f, 0.0f}; // valid during Dragging
    Vec2 tapPos{0.0f, 0.0f};    // valid on Tap

    bool pressed(u32 key) const { return (keysDown & key) != 0; }
    bool held(u32 key) const { return (keysHeld & key) != 0; }
    bool released(u32 key) const { return (keysUp & key) != 0; }
};

// -----------------------------------------------------------------------
// InputManager polls hid/touch state once per frame and derives
// higher-level gesture events (tap vs. drag) from raw touch deltas, so
// gameplay code never has to hand-roll debounce logic.
// -----------------------------------------------------------------------
class InputManager {
public:
    void poll();

    const FrameInput& frame() const { return frame_; }

private:
    static constexpr float kDragThresholdPx = 6.0f;
    static constexpr float kCirclePadDeadzone = 0.15f;

    FrameInput frame_{};

    bool wasTouching_ = false;
    bool dragActive_ = false;
    Vec2 touchStartPos_{0.0f, 0.0f};
    Vec2 lastTouchPos_{0.0f, 0.0f};
};

} // namespace poly
