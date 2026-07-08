#pragma once

#include <cstdint>
#include <cstdio>
#include "core/Types.h"

namespace poly {

struct UIButton {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    char label[40] = "";
    int32_t actionId = -1;
    bool enabled = true;
    bool highlighted = false;
    bool isHeader = false; // rendered as plain section-title text, never tappable

    UIButton() = default;
    UIButton(float px, float py, float pw, float ph, const char* text, int32_t action,
              bool isEnabled = true, bool isHighlighted = false)
        : x(px), y(py), w(pw), h(ph), actionId(action), enabled(isEnabled), highlighted(isHighlighted) {
        std::snprintf(label, sizeof(label), "%s", text);
    }

    static UIButton header(float px, float py, float pw, const char* text) {
        UIButton b(px, py, pw, 14.0f, text, -1, false, false);
        b.isHeader = true;
        return b;
    }

    bool contains(const Vec2& p) const {
        if (isHeader) return false;
        return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }
};

} // namespace poly
