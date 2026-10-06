#include "drawtool/DrawTool.h"

#include <algorithm>
#include <chrono>

namespace drawtool {
namespace {

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

}  // namespace

BoxLayer& DrawTool::AddLayer(std::string name) {
    layers_.push_back(std::make_unique<BoxLayer>(std::move(name)));
    return *layers_.back();
}

BoxLayer* DrawTool::FindLayer(std::string_view name) {
    for (auto& l : layers_)
        if (l->Name() == name)
            return l.get();
    return nullptr;
}

bool DrawTool::RemoveLayer(std::string_view name) {
    auto it = std::find_if(layers_.begin(), layers_.end(), [&](const auto& l) { return l->Name() == name; });
    if (it == layers_.end())
        return false;
    layers_.erase(it);
    return true;
}

Rect DrawTool::CurrentBounds() const {
    if (hasBounds_)
        return bounds_;
    const ImGuiIO& io = ImGui::GetIO();
    return Rect{0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y};
}

void DrawTool::Update(float dt) {
    const auto t0 = Clock::now();
    MotionContext ctx;
    ctx.dt = std::clamp(dt, 0.0f, maxDeltaTime);
    const Rect toolBounds = CurrentBounds();
    for (auto& l : layers_) {
        IMotion* m = l->Motion();
        if (!m || l->paused || l->Boxes().Empty())
            continue;
        ctx.bounds = l->HasBounds() ? l->Bounds() : toolBounds;
        m->Update(l->Boxes().Span(), ctx);
    }
    stats_.updateMs = MsSince(t0);
}

const FrameStats& DrawTool::Render(ImDrawList* dl) {
    const auto t0 = Clock::now();
    if (!dl)
        dl = target_ == DrawTarget::Foreground ? ImGui::GetForegroundDrawList() : ImGui::GetBackgroundDrawList();

    const ImVec2 mn = dl->GetClipRectMin(), mx = dl->GetClipRectMax();
    const Rect clip{mn.x, mn.y, mx.x, mx.y};

    stats_.render = {};
    stats_.layers = 0;
    for (auto& l : layers_) {
        l->lastStats = {};
        if (!l->visible)
            continue;
        const BoxView view = l->Boxes().View();
        l->lastStats = BoxRenderer::Draw(dl, view, l->Style(), clip);
        if (l->Labels())
            BoxRenderer::DrawLabels(dl, view, clip, l->Labels(), l->LabelContext(), l->labelColor);
        stats_.render += l->lastStats;
        ++stats_.layers;
    }
    stats_.renderMs = MsSince(t0);
    return stats_;
}

const FrameStats& DrawTool::Frame() {
    Update(ImGui::GetIO().DeltaTime);
    return Render();
}

}  // namespace drawtool
