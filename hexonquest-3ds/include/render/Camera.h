#pragma once

#include "core/Types.h"

namespace poly {

// -----------------------------------------------------------------------
// Camera2D manages the world-space viewport shown on the 3DS top screen
// (400x240). Position is the world-space point at the center of the
// screen. Zoom multiplies hex pixel size.
// -----------------------------------------------------------------------
class Camera2D {
public:
    Camera2D() = default;

    void setViewportSize(float w, float h) { viewportW_ = w; viewportH_ = h; }
    void setWorldBounds(float minX, float minY, float maxX, float maxY) {
        worldMinX_ = minX; worldMinY_ = minY; worldMaxX_ = maxX; worldMaxY_ = maxY;
    }

    void pan(const Vec2& delta) {
        position_ += delta;
        clampToBounds();
    }

    void setPosition(const Vec2& p) {
        position_ = p;
        clampToBounds();
    }

    const Vec2& position() const { return position_; }

    void zoomBy(float factor) {
        zoom_ = clampf(zoom_ * factor, kMinZoom, kMaxZoom);
        clampToBounds();
    }

    void setZoom(float z) {
        zoom_ = clampf(z, kMinZoom, kMaxZoom);
        clampToBounds();
    }

    float zoom() const { return zoom_; }

    // World-space point -> screen-space pixel coordinates (top-left origin).
    Vec2 worldToScreen(const Vec2& world) const {
        const Vec2 rel = (world - position_) * zoom_;
        return Vec2(rel.x + viewportW_ * 0.5f, rel.y + viewportH_ * 0.5f);
    }

    // Screen-space pixel coordinates -> world-space point.
    Vec2 screenToWorld(const Vec2& screen) const {
        const Vec2 centered(screen.x - viewportW_ * 0.5f, screen.y - viewportH_ * 0.5f);
        return position_ + (centered / zoom_);
    }

    // Effective hex pixel radius after zoom, given the base hex size.
    float effectiveHexSize(float baseHexSize) const { return baseHexSize * zoom_; }

    void update(float dt);

    // Applies edge/velocity smoothing toward a target pan (used by
    // touch-drag inertia). Call once per frame.
    void addVelocity(const Vec2& v) { velocity_ += v; }

private:
    void clampToBounds();

    Vec2 position_{0.0f, 0.0f};
    Vec2 velocity_{0.0f, 0.0f};
    float zoom_ = 1.0f;
    float viewportW_ = 400.0f;
    float viewportH_ = 240.0f;

    float worldMinX_ = -1000.0f;
    float worldMinY_ = -1000.0f;
    float worldMaxX_ = 1000.0f;
    float worldMaxY_ = 1000.0f;

    static constexpr float kMinZoom = 0.5f;
    static constexpr float kMaxZoom = 2.5f;
};

} // namespace poly
