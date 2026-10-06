#pragma once

#include <cstdint>

#include "drawtool/BoxRenderer.h"
#include "drawtool/Types.h"

namespace drawtool {

// 16-joint human stick figure.
enum Joint : uint8_t {
    Head,  // center of the head circle
    Neck,
    Chest,
    Pelvis,
    ShoulderL, ElbowL, HandL,
    ShoulderR, ElbowR, HandR,
    HipL, KneeL, FootL,
    HipR, KneeR, FootR,
    JointCount
};

struct Bone {
    uint8_t a, b;
};

inline constexpr Bone kHumanBones[] = {
    {Head, Neck},       {Neck, Chest},     {Chest, Pelvis},
    {Neck, ShoulderL},  {ShoulderL, ElbowL}, {ElbowL, HandL},
    {Neck, ShoulderR},  {ShoulderR, ElbowR}, {ElbowR, HandR},
    {Pelvis, HipL},     {HipL, KneeL},     {KneeL, FootL},
    {Pelvis, HipR},     {HipR, KneeR},     {KneeR, FootR},
};
inline constexpr uint32_t kHumanBoneCount = sizeof(kHumanBones) / sizeof(kHumanBones[0]);

// Joint positions. Box mode: normalized to the box, (0,0) = top-left,
// (1,1) = bottom-right. Screen mode: pixels. A joint with NaN x is treated as
// missing and every bone touching it is skipped (handy for pose trackers that
// lose a limb).
struct Pose {
    ImVec2 joints[JointCount];
};

// Neutral standing figure filling a roughly 1:2.2 (w:h) box.
Pose StandingPose();
// Procedural walk cycle. `phase` is in cycles; any value works (wraps).
Pose WalkPose(float phase);

struct SkeletonStyle {
    float thickness = 1.5f;         // line width, px
    // Anti-aliased lines look smooth when moving diagonally but cost 2x the
    // vertices and 3x the indices. Turn off when drawing thousands.
    bool antiAliased = true;
    bool useBoxColor = true;        // box mode: take each box's color
    ImU32 color = IM_COL32_WHITE;   // otherwise this (and screen mode without colors)

    float shadowThickness = 1.0f;   // dark border behind the lines; 0 disables
    ImU32 shadowColor = IM_COL32(0, 0, 0, 200);

    bool drawHead = true;           // circle around the Head joint
    float headRadius = 0.07f;       // box mode: fraction of box height
                                    // (screen mode: 0.85 x the Head->Neck distance)
    float minHeight = 12.0f;        // box mode: skip boxes shorter than this (unreadable)
};

// Fill `out` with the pose (box-normalized) for box `index`.
using PoseFn = void (*)(void* ctx, const BoxView& boxes, uint32_t index, Pose& out);

// Stateless skeleton tessellator. Same approach as BoxRenderer: raw writes into
// a PrimReserve()'d region, chunked for 16-bit indices, all into the current
// draw command. Bones are quads (8 vtx anti-aliased, 4 aliased); the head is a
// 12-segment ring.
class SkeletonRenderer {
public:
    // One skeleton per box, pose scaled into the box. fn == nullptr draws
    // StandingPose() for every box.
    static RenderStats Draw(ImDrawList* dl, const BoxView& boxes, PoseFn fn, void* ctx, const SkeletonStyle& style,
                            const Rect& clip);

    // Screen-space joints you already have (pose estimation, projected 3D
    // bones...). `colors` may be null to use style.color for all.
    static RenderStats DrawScreen(ImDrawList* dl, const Pose* poses, const ImU32* colors, uint32_t count,
                                  const SkeletonStyle& style, const Rect& clip);

    // Upper bound on geometry per skeleton (fewer if joints are missing).
    static void MaxCostPerSkeleton(const SkeletonStyle& style, uint32_t& vtx, uint32_t& idx);
};

}  // namespace drawtool
