#pragma once

#include <cstdint>

#include "imgui.h"

namespace drawtool {

// Axis-aligned rectangle in screen pixels, min/max form.
struct Rect {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;

    float Width() const { return x1 - x0; }
    float Height() const { return y1 - y0; }
    bool Overlaps(float bx0, float by0, float bx1, float by1) const {
        return bx1 > x0 && bx0 < x1 && by1 > y0 && by0 < y1;
    }
};

// Read-only, structure-of-arrays view over boxes. This is the only thing the
// renderer consumes, so any storage (BoxStore, your own ECS, a network buffer)
// can be drawn as long as it can expose these four float columns + colors.
struct BoxView {
    const float* x = nullptr;  // top-left x
    const float* y = nullptr;  // top-left y
    const float* w = nullptr;  // width
    const float* h = nullptr;  // height
    const ImU32* color = nullptr;
    const uint64_t* user = nullptr;  // optional, may be null
    uint32_t count = 0;
};

// Mutable SoA view handed to motion modules.
struct BoxSpan {
    float* x = nullptr;
    float* y = nullptr;
    float* w = nullptr;
    float* h = nullptr;
    float* vx = nullptr;  // velocity, px/s
    float* vy = nullptr;
    float* tx = nullptr;  // target position/size (used by follow motions)
    float* ty = nullptr;
    float* tw = nullptr;
    float* th = nullptr;
    ImU32* color = nullptr;
    uint32_t count = 0;
};

}  // namespace drawtool
