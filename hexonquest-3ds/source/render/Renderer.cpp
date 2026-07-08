#include "render/Renderer.h"
#include <cstdio>
#include <cstdarg>
#include <cmath>

namespace poly {

namespace {
constexpr size_t kCmdBufferSize = 256 * 1024;
constexpr float kPi = 3.14159265358979323846f;
} // namespace

Renderer::~Renderer() {
    if (initialized_) shutdown();
}

u32 Renderer::packColor(const Color4& c) {
    return C2D_Color32(c.r, c.g, c.b, c.a);
}

bool Renderer::init() {
    gfxInitDefault();
    gfxSet3D(false);

    if (!C3D_Init(kCmdBufferSize)) return false;
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        C3D_Fini();
        return false;
    }
    C2D_Prepare();

    topTarget_ = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottomTarget_ = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    if (!topTarget_ || !bottomTarget_) return false;

    textBuf_ = C2D_TextBufNew(4096);

    initialized_ = true;
    return true;
}

void Renderer::shutdown() {
    if (!initialized_) return;
    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
    if (font_) {
        C2D_FontFree(font_);
        font_ = nullptr;
    }
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    initialized_ = false;
}

void Renderer::beginFrame() {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(textBuf_);
}

void Renderer::endFrame() {
    C3D_FrameEnd(0);
}

void Renderer::beginTopScreen() {
    activeTarget_ = topTarget_;
    C2D_SceneBegin(topTarget_);
}

void Renderer::beginBottomScreen() {
    activeTarget_ = bottomTarget_;
    C2D_SceneBegin(bottomTarget_);
}

void Renderer::clear(const Color4& color) {
    if (!activeTarget_) return;
    C2D_TargetClear(activeTarget_, packColor(color));
    // C2D_TargetClear does not rebind the scene for subsequent draws on
    // some citro2d versions, so re-assert it explicitly to be safe.
    C2D_SceneBegin(activeTarget_);
}

void Renderer::drawHex(const Vec2& screenCenter, float radius, const Color4& color) const {
    // Flat-top hexagon: 6 corners starting at angle 0 (pointing right +
    // up 30deg offsets), drawn as a triangle fan from the center.
    Vec2 corners[6];
    for (int i = 0; i < 6; ++i) {
        const float angleDeg = 60.0f * static_cast<float>(i);
        const float angleRad = angleDeg * (kPi / 180.0f);
        corners[i] = Vec2(screenCenter.x + radius * std::cos(angleRad),
                           screenCenter.y + radius * std::sin(angleRad));
    }
    const u32 packed = packColor(color);
    for (int i = 0; i < 6; ++i) {
        const Vec2& a = corners[i];
        const Vec2& b = corners[(i + 1) % 6];
        C2D_DrawTriangle(screenCenter.x, screenCenter.y, packed,
                          a.x, a.y, packed,
                          b.x, b.y, packed,
                          0.0f);
    }
}

void Renderer::drawHexOutline(const Vec2& screenCenter, float radius, const Color4& color,
                               float thickness) const {
    Vec2 corners[6];
    for (int i = 0; i < 6; ++i) {
        const float angleRad = (60.0f * static_cast<float>(i)) * (kPi / 180.0f);
        corners[i] = Vec2(screenCenter.x + radius * std::cos(angleRad),
                           screenCenter.y + radius * std::sin(angleRad));
    }
    for (int i = 0; i < 6; ++i) {
        drawLine(corners[i], corners[(i + 1) % 6], thickness, color);
    }
}

void Renderer::drawRect(float x, float y, float w, float h, const Color4& color) const {
    C2D_DrawRectSolid(x, y, 0.0f, w, h, packColor(color));
}

void Renderer::drawRectOutline(float x, float y, float w, float h, const Color4& color,
                                float thickness) const {
    drawRect(x, y, w, thickness, color);
    drawRect(x, y + h - thickness, w, thickness, color);
    drawRect(x, y, thickness, h, color);
    drawRect(x + w - thickness, y, thickness, h, color);
}

void Renderer::drawLine(const Vec2& a, const Vec2& b, float thickness, const Color4& color) const {
    C2D_DrawLine(a.x, a.y, packColor(color), b.x, b.y, packColor(color), thickness, 0.0f);
}

void Renderer::drawCircle(const Vec2& center, float radius, const Color4& color) const {
    constexpr int kSegments = 16;
    const u32 packed = packColor(color);
    Vec2 prev(center.x + radius, center.y);
    for (int i = 1; i <= kSegments; ++i) {
        const float angle = (2.0f * kPi) * (static_cast<float>(i) / kSegments);
        const Vec2 cur(center.x + radius * std::cos(angle), center.y + radius * std::sin(angle));
        C2D_DrawTriangle(center.x, center.y, packed, prev.x, prev.y, packed, cur.x, cur.y, packed, 0.0f);
        prev = cur;
    }
}

void Renderer::drawText(float x, float y, float scale, const Color4& color, const char* text) const {
    C2D_Text c2dText;
    C2D_TextParse(&c2dText, textBuf_, text);
    C2D_TextOptimize(&c2dText);
    C2D_DrawText(&c2dText, C2D_WithColor, x, y, 0.0f, scale, scale, packColor(color));
}

void Renderer::drawTextf(float x, float y, float scale, const Color4& color, const char* fmt, ...) const {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    drawText(x, y, scale, color, buffer);
}

void Renderer::drawButton(const UIButton& button) const {
    if (button.isHeader) {
        drawText(button.x, button.y, 0.38f, Color4(150, 190, 230), button.label);
        return;
    }

    Color4 fill = button.highlighted ? Color4(70, 100, 150) : Color4(40, 44, 58);
    Color4 border = Color4(200, 200, 210);
    Color4 text = Color4(240, 240, 245);
    if (!button.enabled) {
        fill = Color4(30, 30, 34);
        border = Color4(90, 90, 96);
        text = Color4(120, 120, 126);
    }
    drawRect(button.x, button.y, button.w, button.h, fill);
    drawRectOutline(button.x, button.y, button.w, button.h, border, 1.5f);
    drawText(button.x + 6.0f, button.y + button.h * 0.5f - 7.0f, 0.45f, text, button.label);
}

Color4 Renderer::terrainColor(TerrainType terrain) {
    switch (terrain) {
        case TerrainType::Ocean:     return Color4(28, 62, 110);
        case TerrainType::Water:     return Color4(45, 95, 158);
        case TerrainType::Beach:     return Color4(220, 202, 148);
        case TerrainType::Plains:    return Color4(140, 188, 95);
        case TerrainType::Forest:    return Color4(60, 120, 70);
        case TerrainType::Hills:     return Color4(150, 140, 95);
        case TerrainType::Mountains: return Color4(120, 115, 115);
        default:                     return Color4(255, 0, 255);
    }
}

Color4 Renderer::resourceMarkerColor(ResourceType resource) {
    switch (resource) {
        case ResourceType::Fish:  return Color4(200, 230, 255);
        case ResourceType::Fruit: return Color4(230, 90, 120);
        case ResourceType::Game:  return Color4(150, 90, 50);
        case ResourceType::Ore:   return Color4(180, 180, 190);
        case ResourceType::Crop:  return Color4(240, 210, 90);
        default:                  return Color4(0, 0, 0, 0);
    }
}

} // namespace poly
