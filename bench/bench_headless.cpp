// Headless CPU benchmark: runs real ImGui frames (NewFrame -> DrawTool ->
// Render) with no GPU, and reports how much of a 300 FPS budget (3.33 ms)
// the box system eats per frame.
//
//   drawtool_bench [boxes=10000] [frames=2000]

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

#include "drawtool/DrawTool.h"

using namespace drawtool;

namespace {

void InitHeadlessImGui() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(2560.0f, 1440.0f);
    io.DeltaTime = 1.0f / 300.0f;
    io.IniFilename = nullptr;
    // No renderer: build the legacy font atlas so NewFrame() is happy.
    unsigned char* px;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
}

void Fill(BoxStore& s, uint32_t n, std::mt19937& rng, ImVec2 disp) {
    std::uniform_real_distribution<float> px(0.0f, disp.x), py(0.0f, disp.y), sz(8.0f, 120.0f), v(-400.0f, 400.0f);
    std::uniform_int_distribution<int> c(64, 255);
    s.Reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        BoxDesc d;
        d.pos = {px(rng), py(rng)};
        d.size = {sz(rng), sz(rng)};
        d.vel = {v(rng), v(rng)};
        d.color = IM_COL32(c(rng), c(rng), c(rng), 255);
        s.Add(d);
    }
}

struct Result {
    double frameMs, updateMs, renderMs;
    uint32_t vtx, cmds;
};

Result Run(DrawTool& tool, int frames) {
    using Clock = std::chrono::steady_clock;
    double upd = 0.0, ren = 0.0;
    uint32_t vtx = 0, cmds = 0;
    // warm-up: grow draw list buffers to steady-state capacity
    for (int i = 0; i < 20; ++i) {
        ImGui::NewFrame();
        tool.Frame();
        ImGui::Render();
    }
    const auto t0 = Clock::now();
    for (int i = 0; i < frames; ++i) {
        ImGui::NewFrame();
        const FrameStats& st = tool.Frame();
        upd += st.updateMs;
        ren += st.renderMs;
        ImGui::Render();
        vtx = static_cast<uint32_t>(ImGui::GetDrawData()->TotalVtxCount);
        cmds = 0;
        for (ImDrawList* dl : ImGui::GetDrawData()->CmdLists)
            cmds += static_cast<uint32_t>(dl->CmdBuffer.Size);
    }
    const double total = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    return {total / frames, upd / frames, ren / frames, vtx, cmds};
}

// Reference: the same boxes through stock ImDrawList::AddRect (AA stroke).
double RunBaseline(BoxStore& boxes, int frames, bool shadow) {
    using Clock = std::chrono::steady_clock;
    BounceMotion motion;
    auto frame = [&] {
        ImGui::NewFrame();
        MotionContext ctx{ImGui::GetIO().DeltaTime, Rect{0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y}};
        motion.Update(boxes.Span(), ctx);
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const BoxView v = boxes.View();
        for (uint32_t i = 0; i < v.count; ++i) {
            const ImVec2 a(v.x[i], v.y[i]), b(v.x[i] + v.w[i], v.y[i] + v.h[i]);
            if (shadow)
                dl->AddRect(a, b, IM_COL32(0, 0, 0, 200), 0.0f, 0, 3.0f);
            dl->AddRect(a, b, v.color[i]);
        }
        ImGui::Render();
    };
    for (int i = 0; i < 20; ++i) frame();
    const auto t0 = Clock::now();
    for (int i = 0; i < frames; ++i) frame();
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count() / frames;
}

}  // namespace

int main(int argc, char** argv) {
    const uint32_t boxes = argc > 1 ? static_cast<uint32_t>(std::atoi(argv[1])) : 10000u;
    const int frames = argc > 2 ? std::atoi(argv[2]) : 2000;

    InitHeadlessImGui();
    std::mt19937 rng(1234);

    struct Case {
        const char* name;
        BoxShape shape;
        float shadow;
    } cases[] = {
        {"Filled", BoxShape::Filled, 0.0f},
        {"Outline", BoxShape::Outline, 0.0f},
        {"Outline+shadow", BoxShape::Outline, 1.0f},
        {"FilledOutline+shadow", BoxShape::FilledOutline, 1.0f},
        {"Corners", BoxShape::Corners, 0.0f},
        {"Corners+shadow", BoxShape::Corners, 1.0f},
    };

    std::printf("%u boxes, %d frames, 2560x1440, budget @300FPS = 3.333 ms\n\n", boxes, frames);
    std::printf("%-22s %10s %10s %10s %10s %9s %6s\n", "style", "frame ms", "update ms", "tess ms", "max FPS*", "vertices",
                "cmds");
    for (const Case& c : cases) {
        DrawTool tool;
        BoxLayer& l = tool.AddLayer(c.name);
        l.Style().shape = c.shape;
        l.Style().shadowThickness = c.shadow;
        l.EmplaceMotion<BounceMotion>();
        Fill(l.Boxes(), boxes, rng, ImGui::GetIO().DisplaySize);
        const Result r = Run(tool, frames);
        std::printf("%-22s %10.4f %10.4f %10.4f %10.0f %9u %6u\n", c.name, r.frameMs, r.updateMs, r.renderMs,
                    1000.0 / r.frameMs, r.vtx, r.cmds);
    }

    {
        BoxStore boxesRef;
        Fill(boxesRef, boxes, rng, ImGui::GetIO().DisplaySize);
        const double plain = RunBaseline(boxesRef, frames, false);
        const double shadowed = RunBaseline(boxesRef, frames, true);
        std::printf("\nbaseline ImDrawList::AddRect:          %8.4f ms/frame (%.0f FPS*)\n", plain, 1000.0 / plain);
        std::printf("baseline AddRect + shadow AddRect:     %8.4f ms/frame (%.0f FPS*)\n", shadowed, 1000.0 / shadowed);
    }
    std::printf("\n* CPU-side only (ImGui frame + update + tessellation); GPU upload/draw not included.\n");

    ImGui::DestroyContext();
    return 0;
}
