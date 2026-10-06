#pragma once

#include <cstdint>

#include "drawtool/Style.h"
#include "drawtool/Types.h"

namespace drawtool {

struct RenderStats {
    uint32_t submitted = 0;  // boxes handed to the renderer
    uint32_t drawn = 0;      // boxes that produced geometry
    uint32_t culled = 0;     // off-screen or degenerate
    uint32_t vertices = 0;
    uint32_t indices = 0;

    RenderStats& operator+=(const RenderStats& o) {
        submitted += o.submitted;
        drawn += o.drawn;
        culled += o.culled;
        vertices += o.vertices;
        indices += o.indices;
        return *this;
    }
};

// Optional per-box label. Write up to `cap` chars into `buf`, return length
// (0 = no label). Text is far more expensive than boxes -- use sparingly.
using LabelFn = int (*)(void* ctx, const BoxView& boxes, uint32_t index, char* buf, int cap);

// Stateless, allocation-free box tessellator.
//
// Writes vertices straight into the ImDrawList buffers (PrimReserve + raw
// pointer writes), skipping ImGui's path/stroke machinery and its anti-alias
// fringe -- axis-aligned, pixel-snapped quads don't need AA. Everything goes
// into the draw list's current ImDrawCmd, so N boxes cost ~1 draw call.
//
// 16-bit indices: geometry is emitted in chunks under 64K vertices so ImGui
// can roll VtxOffset. Every official backend sets RendererHasVtxOffset; if
// yours doesn't, #define ImDrawIdx unsigned int in imconfig.h.
class BoxRenderer {
public:
    static RenderStats Draw(ImDrawList* dl, const BoxView& boxes, const BoxStyle& style, const Rect& clip);

    // Same, culling against the draw list's current clip rect.
    static RenderStats Draw(ImDrawList* dl, const BoxView& boxes, const BoxStyle& style);

    // Labels centered above each visible box.
    static void DrawLabels(ImDrawList* dl, const BoxView& boxes, const Rect& clip, LabelFn fn, void* ctx,
                           ImU32 color = IM_COL32_WHITE, ImU32 shadow = IM_COL32(0, 0, 0, 220));

    // Geometry cost per drawn box for a given style (for budgeting / tests).
    static void CostPerBox(const BoxStyle& style, uint32_t& vtx, uint32_t& idx);
};

}  // namespace drawtool
