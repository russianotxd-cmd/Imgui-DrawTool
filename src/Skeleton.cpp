#include "drawtool/Skeleton.h"

#include <cmath>

namespace drawtool {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kAA = 1.0f;  // anti-alias fringe width, px
constexpr int kHeadSegments = 12;

// Per-primitive geometry, anti-aliased / aliased.
constexpr uint32_t kBoneVtxAA = 8, kBoneIdxAA = 18, kBoneVtx = 4, kBoneIdx = 6;
constexpr uint32_t kRingVtxAA = 4 * kHeadSegments, kRingIdxAA = 18 * kHeadSegments;
constexpr uint32_t kRingVtx = 2 * kHeadSegments, kRingIdx = 6 * kHeadSegments;

struct UnitCircle {
    ImVec2 v[kHeadSegments];
    UnitCircle() {
        for (int i = 0; i < kHeadSegments; ++i) {
            const float a = 2.0f * kPi * static_cast<float>(i) / kHeadSegments;
            v[i] = ImVec2(std::cos(a), std::sin(a));
        }
    }
};

const UnitCircle& Circle() {
    static const UnitCircle c;
    return c;
}

inline bool Valid(ImVec2 p) { return p.x == p.x && p.y == p.y; }

// Constant index patterns per primitive; offset by the primitive's base
// vertex when written. A fixed-size add+store loop the compiler vectorizes.
struct IndexTemplates {
    unsigned short segAA[kBoneIdxAA];
    unsigned short seg[kBoneIdx];
    unsigned short ringAA[kRingIdxAA];
    unsigned short ring[kRingIdx];

    IndexTemplates() {
        // AA segment: 4 verts across at each end (0-3, 4-7), three strips.
        unsigned short* o = segAA;
        for (unsigned short k = 0; k < 3; ++k) {
            const unsigned short t[6] = {k, static_cast<unsigned short>(k + 1), static_cast<unsigned short>(k + 5),
                                         k, static_cast<unsigned short>(k + 5), static_cast<unsigned short>(k + 4)};
            for (unsigned short v : t) *o++ = v;
        }
        const unsigned short q[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; ++k) seg[k] = q[k];

        // AA ring: 4 radii per point, three strips around.
        o = ringAA;
        for (unsigned i = 0; i < kHeadSegments; ++i) {
            const unsigned j = (i + 1) % kHeadSegments;
            for (unsigned k = 0; k < 3; ++k) {
                const unsigned a = i * 4 + k, b = j * 4 + k;
                const unsigned t[6] = {a, a + 1, b + 1, a, b + 1, b};
                for (unsigned v : t) *o++ = static_cast<unsigned short>(v);
            }
        }
        // Aliased ring: 2 radii per point, one strip.
        o = ring;
        for (unsigned i = 0; i < kHeadSegments; ++i) {
            const unsigned j = (i + 1) % kHeadSegments;
            const unsigned t[6] = {i * 2, i * 2 + 1, j * 2 + 1, i * 2, j * 2 + 1, j * 2};
            for (unsigned v : t) *o++ = static_cast<unsigned short>(v);
        }
    }
};

const IndexTemplates& Templates() {
    static const IndexTemplates t;
    return t;
}

// A figure resolved to screen-space segments once, then emitted for each pass
// (shadow, color) without recomputing directions.
struct Figure {
    ImVec2 a[kHumanBoneCount], b[kHumanBoneCount], n[kHumanBoneCount];  // n = unit normal
    uint32_t bones = 0;
    bool head = false;
    ImVec2 headC;
    float headR = 0.0f;

    void Build(const ImVec2* j, float r, bool wantHead) {
        bones = 0;
        head = wantHead && r > 0.0f && Valid(j[Head]);
        headC = j[Head];
        headR = r;
        for (uint32_t i = 0; i < kHumanBoneCount; ++i) {
            ImVec2 p = j[kHumanBones[i].a], q = j[kHumanBones[i].b];
            if (!Valid(p) || !Valid(q))
                continue;
            float dx = q.x - p.x, dy = q.y - p.y;
            const float l2 = dx * dx + dy * dy;
            const float inv = l2 > 1e-12f ? 1.0f / std::sqrt(l2) : 0.0f;
            if (head && kHumanBones[i].a == Head) {
                // Start the neck bone at the edge of the head circle.
                const float len = l2 * inv;
                if (len <= r)
                    continue;
                p = ImVec2(p.x + dx * r * inv, p.y + dy * r * inv);
            }
            a[bones] = p;
            b[bones] = q;
            n[bones] = inv > 0.0f ? ImVec2(-dy * inv, dx * inv) : ImVec2(0.0f, 1.0f);
            ++bones;
        }
    }
};

// Line writer over a PrimReserve()'d region. Anti-aliased primitives are a
// solid core with a 1px fringe fading to transparent on both sides; aliased
// ones are just the core (half the vertices, a third of the indices).
struct LineEmitter {
    ImDrawVert* vtx;
    ImDrawIdx* idx;
    unsigned int base;
    ImVec2 uv;
    bool aa;

    inline void V(float x, float y, ImU32 c) {
        vtx->pos.x = x;
        vtx->pos.y = y;
        vtx->uv = uv;
        vtx->col = c;
        ++vtx;
    }

    template <uint32_t N>
    inline void Indices(const unsigned short (&t)[N], uint32_t vcount) {
        for (uint32_t k = 0; k < N; ++k)
            idx[k] = static_cast<ImDrawIdx>(base + t[k]);
        idx += N;
        base += vcount;
    }

    inline void Segment(ImVec2 a, ImVec2 b, ImVec2 n, float half, ImU32 c, ImU32 c0) {
        const float hx = n.x * half, hy = n.y * half;
        if (!aa) {
            V(a.x + hx, a.y + hy, c);
            V(a.x - hx, a.y - hy, c);
            V(b.x - hx, b.y - hy, c);
            V(b.x + hx, b.y + hy, c);
            Indices(Templates().seg, kBoneVtx);
            return;
        }
        const float ox = n.x * (half + kAA), oy = n.y * (half + kAA);
        V(a.x + ox, a.y + oy, c0);
        V(a.x + hx, a.y + hy, c);
        V(a.x - hx, a.y - hy, c);
        V(a.x - ox, a.y - oy, c0);
        V(b.x + ox, b.y + oy, c0);
        V(b.x + hx, b.y + hy, c);
        V(b.x - hx, b.y - hy, c);
        V(b.x - ox, b.y - oy, c0);
        Indices(Templates().segAA, kBoneVtxAA);
    }

    inline void Ring(ImVec2 ctr, float r, float half, ImU32 c, ImU32 c0) {
        const UnitCircle& u = Circle();
        if (!aa) {
            const float ro = r + half, ri = r - half > 0.0f ? r - half : 0.0f;
            for (int i = 0; i < kHeadSegments; ++i) {
                const ImVec2 d = u.v[i];
                V(ctr.x + d.x * ro, ctr.y + d.y * ro, c);
                V(ctr.x + d.x * ri, ctr.y + d.y * ri, c);
            }
            Indices(Templates().ring, kRingVtx);
            return;
        }
        const float r0 = r + half + kAA, r1 = r + half;
        const float r2 = r - half > 0.0f ? r - half : 0.0f;
        const float r3 = r - half - kAA > 0.0f ? r - half - kAA : 0.0f;
        for (int i = 0; i < kHeadSegments; ++i) {
            const ImVec2 d = u.v[i];
            V(ctr.x + d.x * r0, ctr.y + d.y * r0, c0);
            V(ctr.x + d.x * r1, ctr.y + d.y * r1, c);
            V(ctr.x + d.x * r2, ctr.y + d.y * r2, c);
            V(ctr.x + d.x * r3, ctr.y + d.y * r3, c0);
        }
        Indices(Templates().ringAA, kRingVtxAA);
    }

    inline void Emit(const Figure& f, float half, ImU32 c) {
        const ImU32 c0 = c & ~IM_COL32_A_MASK;
        for (uint32_t i = 0; i < f.bones; ++i)
            Segment(f.a[i], f.b[i], f.n[i], half, c, c0);
        if (f.head)
            Ring(f.headC, f.headR, half, c, c0);
    }
};

struct Params {
    bool aa;
    float half;        // core half-width
    float shadowHalf;  // core half-width of the shadow pass
    bool shadow;
    ImU32 shadowColor;
    bool head;
};

Params MakeParams(const SkeletonStyle& s) {
    Params p;
    p.aa = s.antiAliased;
    if (p.aa) {
        const float t = s.thickness > kAA ? s.thickness : kAA;
        p.half = 0.5f * (t - kAA);  // core + 1px fringe ~= requested width
    } else {
        p.half = 0.5f * (s.thickness > 1.0f ? s.thickness : 1.0f);
    }
    p.shadow = s.shadowThickness > 0.0f;
    p.shadowHalf = p.half + s.shadowThickness;
    p.shadowColor = s.shadowColor;
    p.head = s.drawHead;
    return p;
}

uint32_t MaxVtx(const Params& p) {
    const uint32_t bone = p.aa ? kBoneVtxAA : kBoneVtx, ring = p.aa ? kRingVtxAA : kRingVtx;
    const uint32_t one = kHumanBoneCount * bone + (p.head ? ring : 0);
    return p.shadow ? 2 * one : one;
}

uint32_t MaxIdx(const Params& p) {
    const uint32_t bone = p.aa ? kBoneIdxAA : kBoneIdx, ring = p.aa ? kRingIdxAA : kRingIdx;
    const uint32_t one = kHumanBoneCount * bone + (p.head ? ring : 0);
    return p.shadow ? 2 * one : one;
}

// Shared chunked driver. `get(i, joints, headR, color)` returns false to skip
// skeleton i (culled), otherwise fills screen-space joints.
template <class GetFn>
RenderStats DrawChunked(ImDrawList* dl, uint32_t count, const Params& p, GetFn&& get) {
    RenderStats st;
    st.submitted = count;
    if (!dl || count == 0)
        return st;

    const uint32_t maxVtx = MaxVtx(p), maxIdx = MaxIdx(p);
    const uint32_t chunkByVtx = 65535u / maxVtx;
    const uint32_t chunk = chunkByVtx < 512u ? chunkByVtx : 512u;

    // 16-bit indices without VtxOffset: never exceed 65535 vertices.
    uint32_t budget = UINT32_MAX;
    if (sizeof(ImDrawIdx) == 2 && !(dl->Flags & ImDrawListFlags_AllowVtxOffset))
        budget = dl->_VtxCurrentIdx < 65535u ? (65535u - dl->_VtxCurrentIdx) / maxVtx : 0u;

    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    Templates();  // build the static tables outside the hot loop
    ImVec2 j[JointCount];
    Figure fig;
    uint32_t i = 0;
    while (i < count && st.drawn < budget) {
        uint32_t batch = (count - i) < chunk ? (count - i) : chunk;
        if (batch > budget - st.drawn)
            batch = budget - st.drawn;

        dl->PrimReserve(static_cast<int>(batch * maxIdx), static_cast<int>(batch * maxVtx));
        ImDrawVert* const vtx0 = dl->_VtxWritePtr;
        ImDrawIdx* const idx0 = dl->_IdxWritePtr;
        LineEmitter e{vtx0, idx0, dl->_VtxCurrentIdx, uv, p.aa};

        for (const uint32_t end = i + batch; i < end; ++i) {
            float headR;
            ImU32 col;
            if (!get(i, j, headR, col))
                continue;
            fig.Build(j, headR, p.head);
            if (p.shadow)
                e.Emit(fig, p.shadowHalf, p.shadowColor);
            e.Emit(fig, p.half, col);
            ++st.drawn;
        }

        const uint32_t wroteVtx = static_cast<uint32_t>(e.vtx - vtx0);
        const uint32_t wroteIdx = static_cast<uint32_t>(e.idx - idx0);
        dl->_VtxWritePtr = e.vtx;
        dl->_IdxWritePtr = e.idx;
        dl->_VtxCurrentIdx = e.base;
        dl->PrimUnreserve(static_cast<int>(batch * maxIdx - wroteIdx), static_cast<int>(batch * maxVtx - wroteVtx));
        st.vertices += wroteVtx;
        st.indices += wroteIdx;
    }
    st.culled = st.submitted - st.drawn;
    return st;
}

}  // namespace

Pose StandingPose() {
    Pose p;
    ImVec2* j = p.joints;
    j[Head] = {0.50f, 0.075f};
    j[Neck] = {0.50f, 0.165f};
    j[Chest] = {0.50f, 0.30f};
    j[Pelvis] = {0.50f, 0.52f};
    j[ShoulderL] = {0.27f, 0.195f};
    j[ElbowL] = {0.20f, 0.36f};
    j[HandL] = {0.18f, 0.51f};
    j[ShoulderR] = {0.73f, 0.195f};
    j[ElbowR] = {0.80f, 0.36f};
    j[HandR] = {0.82f, 0.51f};
    j[HipL] = {0.38f, 0.53f};
    j[KneeL] = {0.35f, 0.75f};
    j[FootL] = {0.33f, 0.98f};
    j[HipR] = {0.62f, 0.53f};
    j[KneeR] = {0.65f, 0.75f};
    j[FootR] = {0.67f, 0.98f};
    return p;
}

Pose WalkPose(float phase) {
    Pose p = StandingPose();
    ImVec2* j = p.joints;
    const float a = 2.0f * kPi * (phase - std::floor(phase));
    const float s = std::sin(a);
    const float liftL = s > 0.0f ? s : 0.0f;    // left leg swinging forward
    const float liftR = s < 0.0f ? -s : 0.0f;   // right leg swinging forward

    // Body dips slightly at mid-stride, rises when legs pass each other.
    const float bob = 0.015f * std::fabs(s);
    for (int k = Head; k <= HipR; ++k)
        if (k != KneeL && k != FootL)
            j[k].y += bob;

    // Legs scissor in opposite directions; the forward leg lifts knee/foot.
    j[KneeL].x += 0.07f * s;
    j[KneeL].y -= 0.05f * liftL;
    j[FootL].x += 0.13f * s;
    j[FootL].y -= 0.07f * liftL;
    j[KneeR].x -= 0.07f * s;
    j[KneeR].y -= 0.05f * liftR;
    j[FootR].x -= 0.13f * s;
    j[FootR].y -= 0.07f * liftR;

    // Arms counter-swing against the legs; the arm opposite the forward leg
    // comes up.
    j[ElbowL].x -= 0.04f * s;
    j[ElbowL].y -= 0.03f * liftR;
    j[HandL].x -= 0.09f * s;
    j[HandL].y -= 0.08f * liftR;
    j[ElbowR].x += 0.04f * s;
    j[ElbowR].y -= 0.03f * liftL;
    j[HandR].x += 0.09f * s;
    j[HandR].y -= 0.08f * liftL;

    // Slight shoulder counter-rotation.
    j[ShoulderL].y += 0.01f * s;
    j[ShoulderR].y -= 0.01f * s;
    return p;
}

RenderStats SkeletonRenderer::Draw(ImDrawList* dl, const BoxView& b, PoseFn fn, void* ctx, const SkeletonStyle& style,
                                   const Rect& clip) {
    const Params p = MakeParams(style);
    const Pose standing = StandingPose();
    // Poses may reach a little outside the box (swinging limbs, head ring).
    const float pad = (p.shadow ? p.shadowHalf : p.half) + kAA + 1.0f;
    const float minH = style.minHeight;
    const float headFrac = style.headRadius;
    const bool boxColor = style.useBoxColor && b.color;
    Pose pose;

    return DrawChunked(dl, b.count, p, [&](uint32_t i, ImVec2* j, float& headR, ImU32& col) {
        const float x = b.x[i], y = b.y[i], w = b.w[i], h = b.h[i];
        if (!(h >= minH && w > 0.0f))
            return false;
        const float mx = 0.15f * w + pad, my = pad;
        if (x + w + mx <= clip.x0 || x - mx >= clip.x1 || y + h + my <= clip.y0 || y - my >= clip.y1)
            return false;
        const Pose* src = &standing;
        if (fn) {
            fn(ctx, b, i, pose);
            src = &pose;
        }
        for (int k = 0; k < JointCount; ++k)
            j[k] = ImVec2(x + src->joints[k].x * w, y + src->joints[k].y * h);
        headR = headFrac * h;
        col = boxColor ? b.color[i] : style.color;
        return true;
    });
}

RenderStats SkeletonRenderer::DrawScreen(ImDrawList* dl, const Pose* poses, const ImU32* colors, uint32_t count,
                                         const SkeletonStyle& style, const Rect& clip) {
    const Params p = MakeParams(style);
    const float pad = (p.shadow ? p.shadowHalf : p.half) + kAA + 1.0f;

    return DrawChunked(dl, count, p, [&](uint32_t i, ImVec2* j, float& headR, ImU32& col) {
        const ImVec2* src = poses[i].joints;
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        for (int k = 0; k < JointCount; ++k) {
            j[k] = src[k];
            if (!Valid(src[k]))
                continue;
            x0 = src[k].x < x0 ? src[k].x : x0;
            y0 = src[k].y < y0 ? src[k].y : y0;
            x1 = src[k].x > x1 ? src[k].x : x1;
            y1 = src[k].y > y1 ? src[k].y : y1;
        }
        headR = 0.0f;
        if (Valid(src[Head]) && Valid(src[Neck])) {
            const float dx = src[Neck].x - src[Head].x, dy = src[Neck].y - src[Head].y;
            headR = 0.85f * std::sqrt(dx * dx + dy * dy);
        }
        const float m = pad + headR;
        if (x1 < x0 || x1 + m <= clip.x0 || x0 - m >= clip.x1 || y1 + m <= clip.y0 || y0 - m >= clip.y1)
            return false;
        col = colors ? colors[i] : style.color;
        return true;
    });
}

void SkeletonRenderer::MaxCostPerSkeleton(const SkeletonStyle& style, uint32_t& vtx, uint32_t& idx) {
    const Params p = MakeParams(style);
    vtx = MaxVtx(p);
    idx = MaxIdx(p);
}

}  // namespace drawtool
