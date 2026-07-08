#include "render/Camera.h"

namespace poly {

void Camera2D::update(float dt) {
    if (velocity_.lengthSq() > 0.0001f) {
        position_ += velocity_ * dt;
        // Exponential decay for a natural-feeling inertial stop after a
        // touch-drag flick.
        const float damping = 1.0f - clampf(6.0f * dt, 0.0f, 1.0f);
        velocity_ *= damping;
        clampToBounds();
    } else {
        velocity_ = Vec2(0.0f, 0.0f);
    }
}

void Camera2D::clampToBounds() {
    position_.x = clampf(position_.x, worldMinX_, worldMaxX_);
    position_.y = clampf(position_.y, worldMinY_, worldMaxY_);
}

} // namespace poly
