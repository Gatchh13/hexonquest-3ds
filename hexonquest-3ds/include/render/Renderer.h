#pragma once

#include <citro2d.h>
#include <citro3d.h>
#include <3ds.h>
#include "core/Types.h"
#include "core/HexCoord.h"
#include "world/Tile.h"
#include "render/Camera.h"
#include "ui/UIWidgets.h"

namespace poly {

// Pixel-space radius (center to corner) of a hex at zoom == 1.0.
constexpr float kBaseHexSize = 18.0f;

// -----------------------------------------------------------------------
// Renderer owns all citro2d/citro3d state: the two screen render targets,
// frame begin/end lifecycle, and drawing primitives used by higher-level
// systems (world view, UI, HUD). Kept allocation-free per-frame: all
// vertex data for a hex is computed on the stack and pushed straight to
// citro2d's immediate-mode triangle list.
// -----------------------------------------------------------------------
class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    bool init();
    void shutdown();

    void beginFrame();
    void endFrame();

    // Screen selection for subsequent draw calls.
    void beginTopScreen();
    void beginBottomScreen();

    void clear(const Color4& color);

    // Draws a filled flat-top hexagon centered at `screenCenter` with
    // `radius` (screen pixels, center-to-corner) and solid `color`.
    void drawHex(const Vec2& screenCenter, float radius, const Color4& color) const;

    // Draws just the hex outline (6 line segments), used for cursor
    // highlight / selection rings.
    void drawHexOutline(const Vec2& screenCenter, float radius, const Color4& color,
                         float thickness = 2.0f) const;

    void drawRect(float x, float y, float w, float h, const Color4& color) const;
    void drawRectOutline(float x, float y, float w, float h, const Color4& color,
                          float thickness = 1.0f) const;
    void drawLine(const Vec2& a, const Vec2& b, float thickness, const Color4& color) const;
    void drawCircle(const Vec2& center, float radius, const Color4& color) const;

    void drawText(float x, float y, float scale, const Color4& color, const char* text) const;
    void drawTextf(float x, float y, float scale, const Color4& color, const char* fmt, ...) const;

    // Draws a filled+outlined rectangle with centered-ish label text,
    // dimmed automatically when the button is disabled. Shared by every
    // touch-driven menu (main menu, city/tech panels, pause, settings)
    // so button styling stays consistent across the whole UI.
    void drawButton(const UIButton& button) const;

    // Maps semantic terrain -> a base fill color. Centralized here so
    // both the world renderer and minimap agree on the palette.
    static Color4 terrainColor(TerrainType terrain);
    static Color4 resourceMarkerColor(ResourceType resource);

    C2D_TextBuf textBuffer() const { return textBuf_; }
    bool isInitialized() const { return initialized_; }

private:
    static u32 packColor(const Color4& c);

    C3D_RenderTarget* topTarget_ = nullptr;
    C3D_RenderTarget* bottomTarget_ = nullptr;
    C3D_RenderTarget* activeTarget_ = nullptr;
    C2D_TextBuf textBuf_ = nullptr;
    C2D_Font font_ = nullptr;
    bool initialized_ = false;
};

} // namespace poly
