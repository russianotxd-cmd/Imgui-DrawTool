#include "drawtool/BoxRenderer.h"

#include <cmath>

namespace drawtool {
namespace {

// Raw vertex/index writer over a PrimReserve()'d region. All the per-box work
// is here: plain stores, no bounds checks, no ImVector operator calls.
struct Emitter {
    ImDrawVert* vtx;
    ImDrawIdx* idx;
    unsigned int base;
    ImVec2 uv;

    inline void Vtx(float x, float y, ImU32 c) {
        vtx->pos.x = x;
        vtx->pos.y = y;
        vtx->uv = uv;
        vtx->col = c;
        ++vtx;
    }

    inline void Tri(unsigned a, unsigned b, unsigned c) {
        idx[0] = static_cast<ImDrawIdx>(base + a);
        idx[1] = static_cast<ImDrawIdx>(base + b);
        idx[2] = static_cast<ImDrawIdx>(base + c);
        idx += 3;
    }

    // Solid rectangle. 4 vtx / 6 idx.
    inline void Quad(float x0, float y0, float x1, float y1, ImU32 c) {
        Vtx(x0, y0, c); Vtx(x1, y0, c); Vtx(x1, y1, c); Vtx(x0, y1, c);
        Tri(0, 1, 2); Tri(0, 2, 3);
        base += 4;
    }

    // Hollow rectangle of stroke width t, inset from the outer edge.
    // Outer corners 0-3 + inner corners 4-7, two tris per side. 8 vtx / 24 idx.
    inline void Ring(float x0, float y0, float x1, float y1, float t, ImU32 c) {
        const float half = 0.5f * ((x1 - x0) < (y1 - y0) ? (x1 - x0) : (y1 - y0));
        if (t > half) t = half;
        Vtx(x0, y0, c); Vtx(x1, y0, c); Vtx(x1, y1, c); Vtx(x0, y1, c);
        Vtx(x0 + t, y0 + t, c); Vtx(x1 - t, y0 + t, c); Vtx(x1 - t, y1 - t, c); Vtx(x0 + t, y1 - t, c);
        Tri(0, 1, 5); Tri(0, 5, 4);  // top
        Tri(1, 2, 6); Tri(1, 6, 5);  // right
        Tri(2, 3, 7); Tri(2, 7, 6);  // bottom
        Tri(3, 0, 4); Tri(3, 4, 7);  // left
        base += 8;
    }

    // L-shaped bracket with its outer corner at (cx, cy), arms pointing in
    // (sx, sy) = (+-1, +-1). Fan from the corner. 6 vtx / 12 idx.
    inline void Bracket(float cx, float cy, float sx, float sy, float len, float t, ImU32 c) {
        Vtx(cx, cy, c);
        Vtx(cx + sx * len, cy, c);
        Vtx(cx + sx * len, cy + sy * t, c);
        Vtx(cx + sx * t, cy + sy * t, c);
        Vtx(cx + sx * t, cy + sy * len, c);
        Vtx(cx, cy + sy * len, c);
        Tri(0, 1, 2); Tri(0, 2, 3); Tri(0, 3, 4); Tri(0, 4, 5);
        base += 6;
    }

    inline void Brackets(float x0, float y0, float x1, float y1, float len, float t, ImU32 c) {
        Bracket(x0, y0, +1.0f, +1.0f, len, t, c);
        Bracket(x1, y0, -1.0f, +1.0f, len, t, c);
        Bracket(x1, y1, -1.0f, -1.0f, len, t, c);
        Bracket(x0, y1, +1.0f, -1.0f, len, t, c);
    }
};

struct Params {
    float thickness;
    float cornerLength;
    float fillAlpha;
    float shadow;
    ImU32 shadowColor;
    float minSize;
    bool snap;
};

inline ImU32 ScaleAlpha(ImU32 col, float k) {
    const ImU32 a = (col >> IM_COL32_A_SHIFT) & 0xFF;
    const ImU32 na = static_cast<ImU32>(static_cast<float>(a) * k + 0.5f);
    return (col & ~IM_COL32_A_MASK) | ((na > 255 ? 255 : na) << IM_COL32_A_SHIFT);
}

// Compile-time per-shape geometry. Shadow variants are separate instantiations
// so the inner loop has no style branches at all.
template <BoxShape S, bool Shadow>
struct Kernel;

template <bool Shadow>
struct Kernel<BoxShape::Outline, Shadow> {
    static constexpr uint32_t kVtx = Shadow ? 16 : 8;
    static constexpr uint32_t kIdx = Shadow ? 48 : 24;
    static inline void Emit(Emitter& e, float x0, float y0, float x1, float y1, ImU32 c, const Params& p) {
        if (Shadow) {
            const float s = p.shadow;
            e.Ring(x0 - s, y0 - s, x1 + s, y1 + s, p.thickness + 2.0f * s, p.shadowColor);
        }
        e.Ring(x0, y0, x1, y1, p.thickness, c);
    }
};

template <bool Shadow>
struct Kernel<BoxShape::Filled, Shadow> {
    static constexpr uint32_t kVtx = 4;
    static constexpr uint32_t kIdx = 6;
    static inline void Emit(Emitter& e, float x0, float y0, float x1, float y1, ImU32 c, const Params&) {
        e.Quad(x0, y0, x1, y1, c);
    }
};

template <bool Shadow>
struct Kernel<BoxShape::FilledOutline, Shadow> {
    static constexpr uint32_t kVtx = Shadow ? 20 : 12;
    static constexpr uint32_t kIdx = Shadow ? 54 : 30;
    static inline void Emit(Emitter& e, float x0, float y0, float x1, float y1, ImU32 c, const Params& p) {
        if (Shadow) {
            const float s = p.shadow;
            e.Ring(x0 - s, y0 - s, x1 + s, y1 + s, p.thickness + 2.0f * s, p.shadowColor);
        }
        const float t = p.thickness;
        // Fill only the interior so a translucent fill never darkens the stroke.
        if (x1 - x0 > 2.0f * t && y1 - y0 > 2.0f * t)
            e.Quad(x0 + t, y0 + t, x1 - t, y1 - t, ScaleAlpha(c, p.fillAlpha));
        else
            e.Quad(x0, y0, x0, y0, 0);  // degenerate: keeps the per-box cost fixed
        e.Ring(x0, y0, x1, y1, t, c);
    }
};

template <bool Shadow>
struct Kernel<BoxShape::Corners, Shadow> {
    static constexpr uint32_t kVtx = Shadow ? 48 : 24;
    static constexpr uint32_t kIdx = Shadow ? 96 : 48;
    static inline void Emit(Emitter& e, float x0, float y0, float x1, float y1, ImU32 c, const Params& p) {
        const float minSide = (x1 - x0) < (y1 - y0) ? (x1 - x0) : (y1 - y0);
        const float t = p.thickness < 0.5f * minSide ? p.thickness : 0.5f * minSide;
        float len = minSide * p.cornerLength;
        if (len < t) len = t;
        if (len > 0.5f * minSide) len = 0.5f * minSide;
        if (Shadow) {
            const float s = p.shadow;
            e.Brackets(x0 - s, y0 - s, x1 + s, y1 + s, len + 2.0f * s, t + 2.0f * s, p.shadowColor);
        }
        e.Brackets(x0, y0, x1, y1, len, t, c);
    }
};

template <BoxShape S, bool Shadow>
RenderStats DrawImpl(ImDrawList* dl, const BoxView& b, const Params& p, const Rect& clip) {
    using K = Kernel<S, Shadow>;
    // Each reservation must stay under 64K vertices so 16-bit indices work
    // (ImGui bumps VtxOffset between reservations). Smaller chunks also keep
    // the unused tail we hand back via PrimUnreserve cheap.
    constexpr uint32_t kChunkByVtx = 65535u / K::kVtx;
    constexpr uint32_t kChunk = kChunkByVtx < 2048u ? kChunkByVtx : 2048u;

    RenderStats st;
    st.submitted = b.count;

    // Expand the cull rect by the shadow so edge boxes keep their border.
    const float pad = Shadow ? p.shadow : 0.0f;
    const float cx0 = clip.x0 - pad, cy0 = clip.y0 - pad, cx1 = clip.x1 + pad, cy1 = clip.y1 + pad;
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    const float minSize = p.minSize;
    const bool snap = p.snap;

    // Without VtxOffset support a 16-bit-index draw list can address only
    // 65535 vertices total (ImGui asserts _VtxCurrentIdx < 1 << 16). Stop drawing at that limit instead of wrapping indices
    // into garbage triangles.
    uint32_t budget = UINT32_MAX;
    if (sizeof(ImDrawIdx) == 2 && !(dl->Flags & ImDrawListFlags_AllowVtxOffset))
        budget = dl->_VtxCurrentIdx < 65535u ? (65535u - dl->_VtxCurrentIdx) / K::kVtx : 0u;

    uint32_t i = 0;
    while (i < b.count && st.drawn < budget) {
        uint32_t batch = (b.count - i) < kChunk ? (b.count - i) : kChunk;
        if (batch > budget - st.drawn)
            batch = budget - st.drawn;
        dl->PrimReserve(static_cast<int>(batch * K::kIdx), static_cast<int>(batch * K::kVtx));
        Emitter e{dl->_VtxWritePtr, dl->_IdxWritePtr, dl->_VtxCurrentIdx, uv};

        uint32_t drawn = 0;
        for (const uint32_t end = i + batch; i < end; ++i) {
            const float w = b.w[i], h = b.h[i];
            if (!(w >= minSize && h >= minSize))  // also rejects NaN
                continue;
            float x0 = b.x[i], y0 = b.y[i];
            float x1 = x0 + w, y1 = y0 + h;
            if (x1 <= cx0 || x0 >= cx1 || y1 <= cy0 || y0 >= cy1)
                continue;
            if (snap) {
                x0 = std::floor(x0 + 0.5f);
                y0 = std::floor(y0 + 0.5f);
                x1 = std::floor(x1 + 0.5f);
                y1 = std::floor(y1 + 0.5f);
                if (x1 <= x0 || y1 <= y0)
                    continue;
            }
            K::Emit(e, x0, y0, x1, y1, b.color[i], p);
            ++drawn;
        }

        dl->_VtxWritePtr = e.vtx;
        dl->_IdxWritePtr = e.idx;
        dl->_VtxCurrentIdx = e.base;
        const uint32_t unused = batch - drawn;
        if (unused)
            dl->PrimUnreserve(static_cast<int>(unused * K::kIdx), static_cast<int>(unused * K::kVtx));

        st.drawn += drawn;
    }

    st.culled = st.submitted - st.drawn;
    st.vertices = st.drawn * K::kVtx;
    st.indices = st.drawn * K::kIdx;
    return st;
}

template <BoxShape S>
RenderStats DrawShape(ImDrawList* dl, const BoxView& b, const Params& p, const Rect& clip) {
    return p.shadow > 0.0f ? DrawImpl<S, true>(dl, b, p, clip) : DrawImpl<S, false>(dl, b, p, clip);
}

Params MakeParams(const BoxStyle& s) {
    Params p;
    p.thickness = s.thickness > 0.0f ? s.thickness : 1.0f;
    p.cornerLength = s.cornerLength;
    p.fillAlpha = s.fillAlpha;
    p.shadow = s.shape == BoxShape::Filled ? 0.0f : s.shadowThickness;
    p.shadowColor = s.shadowColor;
    p.minSize = s.minSize;
    p.snap = s.snapToPixel;
    return p;
}

}  // namespace

RenderStats BoxRenderer::Draw(ImDrawList* dl, const BoxView& boxes, const BoxStyle& style, const Rect& clip) {
    if (!dl || boxes.count == 0)
        return {};
    const Params p = MakeParams(style);
    switch (style.shape) {
        case BoxShape::Outline:       return DrawShape<BoxShape::Outline>(dl, boxes, p, clip);
        case BoxShape::Filled:        return DrawShape<BoxShape::Filled>(dl, boxes, p, clip);
        case BoxShape::FilledOutline: return DrawShape<BoxShape::FilledOutline>(dl, boxes, p, clip);
        case BoxShape::Corners:       return DrawShape<BoxShape::Corners>(dl, boxes, p, clip);
    }
    return {};
}

RenderStats BoxRenderer::Draw(ImDrawList* dl, const BoxView& boxes, const BoxStyle& style) {
    if (!dl)
        return {};
    const ImVec2 mn = dl->GetClipRectMin(), mx = dl->GetClipRectMax();
    return Draw(dl, boxes, style, Rect{mn.x, mn.y, mx.x, mx.y});
}

void BoxRenderer::DrawLabels(ImDrawList* dl, const BoxView& b, const Rect& clip, LabelFn fn, void* ctx, ImU32 color,
                             ImU32 shadow) {
    if (!dl || !fn)
        return;
    char buf[128];
    const float lineH = ImGui::GetFontSize();
    for (uint32_t i = 0; i < b.count; ++i) {
        const float x0 = b.x[i], y0 = b.y[i], x1 = x0 + b.w[i], y1 = y0 + b.h[i];
        if (!clip.Overlaps(x0, y0 - lineH, x1, y1))
            continue;
        const int len = fn(ctx, b, i, buf, static_cast<int>(sizeof(buf)));
        if (len <= 0)
            continue;
        const char* end = buf + (len < static_cast<int>(sizeof(buf)) ? len : static_cast<int>(sizeof(buf)) - 1);
        const ImVec2 ts = ImGui::CalcTextSize(buf, end);
        const ImVec2 pos(std::floor(0.5f * (x0 + x1 - ts.x)), std::floor(y0 - ts.y - 2.0f));
        if (shadow & IM_COL32_A_MASK)
            dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), shadow, buf, end);
        dl->AddText(pos, color, buf, end);
    }
}

void BoxRenderer::CostPerBox(const BoxStyle& style, uint32_t& vtx, uint32_t& idx) {
    const bool sh = style.shape != BoxShape::Filled && style.shadowThickness > 0.0f;
    switch (style.shape) {
        case BoxShape::Outline:
            vtx = sh ? Kernel<BoxShape::Outline, true>::kVtx : Kernel<BoxShape::Outline, false>::kVtx;
            idx = sh ? Kernel<BoxShape::Outline, true>::kIdx : Kernel<BoxShape::Outline, false>::kIdx;
            return;
        case BoxShape::Filled:
            vtx = Kernel<BoxShape::Filled, false>::kVtx;
            idx = Kernel<BoxShape::Filled, false>::kIdx;
            return;
        case BoxShape::FilledOutline:
            vtx = sh ? Kernel<BoxShape::FilledOutline, true>::kVtx : Kernel<BoxShape::FilledOutline, false>::kVtx;
            idx = sh ? Kernel<BoxShape::FilledOutline, true>::kIdx : Kernel<BoxShape::FilledOutline, false>::kIdx;
            return;
        case BoxShape::Corners:
            vtx = sh ? Kernel<BoxShape::Corners, true>::kVtx : Kernel<BoxShape::Corners, false>::kVtx;
            idx = sh ? Kernel<BoxShape::Corners, true>::kIdx : Kernel<BoxShape::Corners, false>::kIdx;
            return;
    }
    vtx = idx = 0;
}

}  // namespace drawtool
