#include "input/InputManager.h"
#include <cmath>

namespace poly {

void InputManager::poll() {
    hidScanInput();

    frame_.keysDown = hidKeysDown();
    frame_.keysHeld = hidKeysHeld();
    frame_.keysUp = hidKeysUp();

    circlePosition cpos;
    hidCircleRead(&cpos);
    Vec2 pad(static_cast<float>(cpos.dx) / 156.0f, -static_cast<float>(cpos.dy) / 156.0f);
    if (pad.length() < kCirclePadDeadzone) {
        pad = Vec2(0.0f, 0.0f);
    } else {
        // Rescale so the deadzone doesn't create a dead band at low tilt.
        const float len = pad.length();
        const float rescaled = (len - kCirclePadDeadzone) / (1.0f - kCirclePadDeadzone);
        pad = pad * (rescaled / len);
    }
    frame_.circlePad = pad;

    touchPosition touch;
    hidTouchRead(&touch);
    const bool touchingNow = (frame_.keysHeld & KEY_TOUCH) != 0;
    frame_.touching = touchingNow;
    frame_.touchPos = Vec2(static_cast<float>(touch.px), static_cast<float>(touch.py));

    frame_.gesture = TouchGesture::None;
    frame_.dragDelta = Vec2(0.0f, 0.0f);

    if (touchingNow && !wasTouching_) {
        // Finger just touched down.
        touchStartPos_ = frame_.touchPos;
        lastTouchPos_ = frame_.touchPos;
        dragActive_ = false;
        frame_.gesture = TouchGesture::DragStart;
    } else if (touchingNow && wasTouching_) {
        const Vec2 delta = frame_.touchPos - lastTouchPos_;
        const Vec2 fromStart = frame_.touchPos - touchStartPos_;
        if (!dragActive_ && fromStart.length() > kDragThresholdPx) {
            dragActive_ = true;
        }
        if (dragActive_) {
            frame_.gesture = TouchGesture::Dragging;
            frame_.dragDelta = delta;
        }
        lastTouchPos_ = frame_.touchPos;
    } else if (!touchingNow && wasTouching_) {
        if (!dragActive_) {
            frame_.gesture = TouchGesture::Tap;
            frame_.tapPos = touchStartPos_;
        } else {
            frame_.gesture = TouchGesture::DragEnd;
        }
        dragActive_ = false;
    }

    wasTouching_ = touchingNow;
}

} // namespace poly
