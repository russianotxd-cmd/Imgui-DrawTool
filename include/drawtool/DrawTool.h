#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "drawtool/BoxRenderer.h"
#include "drawtool/BoxStore.h"
#include "drawtool/Motion.h"
#include "drawtool/Style.h"

namespace drawtool {

// A layer = one set of boxes + how they look + how they move. Layers are drawn
// in creation order, so later layers sit on top.
class BoxLayer {
public:
    explicit BoxLayer(std::string name) : name_(std::move(name)) {}

    const std::string& Name() const { return name_; }
    BoxStore& Boxes() { return boxes_; }
    const BoxStore& Boxes() const { return boxes_; }
    BoxStyle& Style() { return style_; }
    const BoxStyle& Style() const { return style_; }

    // nullptr = static boxes (you move them yourself through the store).
    void SetMotion(std::unique_ptr<IMotion> motion) { motion_ = std::move(motion); }
    IMotion* Motion() const { return motion_.get(); }
    template <class M, class... Args>
    M& EmplaceMotion(Args&&... args) {
        auto m = std::make_unique<M>(std::forward<Args>(args)...);
        M& ref = *m;
        motion_ = std::move(m);
        return ref;
    }

    // Per-layer motion bounds; when unset the tool's bounds are used.
    void SetBounds(const Rect& r) { bounds_ = r; hasBounds_ = true; }
    void ClearBounds() { hasBounds_ = false; }
    bool HasBounds() const { return hasBounds_; }
    const Rect& Bounds() const { return bounds_; }

    void SetLabels(LabelFn fn, void* ctx = nullptr) { labelFn_ = fn; labelCtx_ = ctx; }
    LabelFn Labels() const { return labelFn_; }
    void* LabelContext() const { return labelCtx_; }

    bool visible = true;
    bool paused = false;  // skip motion update, still draw
    ImU32 labelColor = IM_COL32_WHITE;

    RenderStats lastStats;

private:
    std::string name_;
    BoxStore boxes_;
    BoxStyle style_;
    std::unique_ptr<IMotion> motion_;
    Rect bounds_;
    bool hasBounds_ = false;
    LabelFn labelFn_ = nullptr;
    void* labelCtx_ = nullptr;
};

struct FrameStats {
    RenderStats render;
    double updateMs = 0.0;  // CPU time in motion modules
    double renderMs = 0.0;  // CPU time tessellating into the draw list
    uint32_t layers = 0;
};

enum class DrawTarget : uint8_t {
    Background,  // behind every ImGui window (typical overlay use)
    Foreground,  // above every ImGui window
};

// Facade tying layers, motion and rendering together. Call between
// ImGui::NewFrame() and ImGui::Render():
//
//     tool.Frame();                 // Update(io.DeltaTime) + Render()
//
// or drive the two halves yourself (e.g. update on another thread, render on
// the ImGui thread -- just don't overlap them for the same layer).
class DrawTool {
public:
    BoxLayer& AddLayer(std::string name);
    BoxLayer* FindLayer(std::string_view name);
    bool RemoveLayer(std::string_view name);
    std::vector<std::unique_ptr<BoxLayer>>& Layers() { return layers_; }

    // Motion bounds; default each frame is the full display (io.DisplaySize).
    void SetBounds(const Rect& r) { bounds_ = r; hasBounds_ = true; }
    void ClearBounds() { hasBounds_ = false; }
    Rect CurrentBounds() const;

    void SetTarget(DrawTarget t) { target_ = t; }
    DrawTarget Target() const { return target_; }

    // Clamp dt so a hitch (alt-tab, breakpoint) doesn't launch boxes across
    // the screen. Seconds.
    float maxDeltaTime = 0.1f;

    void Update(float dt);
    // dl == nullptr -> background/foreground draw list per SetTarget().
    const FrameStats& Render(ImDrawList* dl = nullptr);
    const FrameStats& Frame();

    const FrameStats& Stats() const { return stats_; }

private:
    std::vector<std::unique_ptr<BoxLayer>> layers_;
    Rect bounds_;
    bool hasBounds_ = false;
    DrawTarget target_ = DrawTarget::Background;
    FrameStats stats_;
};

}  // namespace drawtool
