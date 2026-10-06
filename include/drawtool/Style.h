#pragma once

#include <cstdint>

#include "imgui.h"

namespace drawtool {

// Geometry cost per drawn box (without / with shadow):
enum class BoxShape : uint8_t {
    Outline,        // hollow rectangle         8 / 16 vtx
    Filled,         // solid quad               4 vtx (no shadow)
    FilledOutline,  // translucent fill + ring 12 / 20 vtx
    Corners,        // 4 L-shaped brackets     24 / 48 vtx
};

struct BoxStyle {
    BoxShape shape = BoxShape::Outline;
    float thickness = 1.0f;       // stroke width, px
    float cornerLength = 0.25f;   // Corners: arm length as fraction of min(w, h)
    float fillAlpha = 0.25f;      // FilledOutline: fill opacity relative to box color

    // Dark border drawn behind the stroke so boxes stay readable on any
    // background. Doubles stroke geometry. 0 disables. Ignored for Filled.
    float shadowThickness = 1.0f;
    ImU32 shadowColor = IM_COL32(0, 0, 0, 200);

    bool snapToPixel = true;      // round edges to whole pixels: crisp, no shimmer
    float minSize = 1.0f;         // skip boxes thinner than this (px)
};

}  // namespace drawtool
