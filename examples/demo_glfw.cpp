// DrawTool demo: GLFW + OpenGL3, vsync off, frame limiter at 300 FPS.
//
// Three layers show the three ways boxes usually move:
//   "bouncers"  - self-simulated (BounceMotion), thousands of boxes
//   "drifters"  - self-simulated, wrapping, translucent fill
//   "tracked"   - positions arrive from a slow "data feed" (30 Hz here) and
//                 SmoothFollowMotion glides them at render rate, with labels
//                 and an animated walking skeleton inside each box

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "drawtool/DebugUI.h"
#include "drawtool/DrawTool.h"
#include "drawtool/FrameLimiter.h"

using namespace drawtool;

namespace {

std::mt19937 g_rng(42);

float Rand(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(g_rng); }

ImU32 RandColor() {
    ImVec4 c;
    ImGui::ColorConvertHSVtoRGB(Rand(0.0f, 1.0f), 0.75f, 1.0f, c.x, c.y, c.z);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, 1.0f));
}

void Populate(BoxStore& s, int count, ImVec2 disp, float minSize, float maxSize, float speed) {
    s.Clear();
    s.Reserve(static_cast<uint32_t>(count));
    for (int i = 0; i < count; ++i) {
        BoxDesc d;
        d.size = {Rand(minSize, maxSize), Rand(minSize, maxSize)};
        d.pos = {Rand(0.0f, disp.x - d.size.x), Rand(0.0f, disp.y - d.size.y)};
        d.vel = {Rand(-speed, speed), Rand(-speed, speed)};
        d.color = RandColor();
        d.user = static_cast<uint64_t>(i);
        s.Add(d);
    }
}

// Stand-in for your real data source (game state, network, CV tracker...).
struct FakeFeed {
    struct Target {
        ImVec2 pos, size, vel;
    };
    std::vector<Target> targets;
    std::vector<BoxHandle> handles;
    double accum = 0.0;
    double rate = 30.0;  // updates per second

    void Init(BoxStore& s, int count, ImVec2 disp) {
        s.Clear();
        targets.clear();
        handles.clear();
        for (int i = 0; i < count; ++i) {
            Target t;
            const float h = Rand(90.0f, 220.0f);
            t.size = {h * Rand(0.40f, 0.50f), h};  // roughly human proportions
            t.pos = {Rand(0.0f, disp.x - t.size.x), Rand(0.0f, disp.y - t.size.y)};
            t.vel = {Rand(-250.0f, 250.0f), Rand(-250.0f, 250.0f)};
            targets.push_back(t);
            BoxDesc d;
            d.pos = t.pos;
            d.size = t.size;
            d.color = IM_COL32(255, 70, 70, 255);
            d.user = static_cast<uint64_t>(i);
            handles.push_back(s.Add(d));
        }
    }

    void Tick(BoxStore& s, double dt, ImVec2 disp) {
        accum += dt;
        const double step = 1.0 / rate;
        while (accum >= step) {
            accum -= step;
            for (size_t i = 0; i < targets.size(); ++i) {
                Target& t = targets[i];
                t.vel.x += Rand(-60.0f, 60.0f);
                t.vel.y += Rand(-60.0f, 60.0f);
                t.pos.x += t.vel.x * static_cast<float>(step);
                t.pos.y += t.vel.y * static_cast<float>(step);
                if (t.pos.x < 0 || t.pos.x + t.size.x > disp.x) t.vel.x = -t.vel.x;
                if (t.pos.y < 0 || t.pos.y + t.size.y > disp.y) t.vel.y = -t.vel.y;
                s.SetTarget(handles[i], t.pos, t.size);
            }
        }
    }
};

// Pose source for the skeleton layer: each box walks with its own cadence and
// phase offset so they don't march in lockstep.
struct WalkClock {
    float time = 0.0f;
    bool animate = true;
};

void WalkPoseFn(void* ctx, const BoxView& b, uint32_t i, Pose& out) {
    const WalkClock& clock = *static_cast<const WalkClock*>(ctx);
    if (!clock.animate) {
        out = StandingPose();
        return;
    }
    const uint64_t id = b.user[i];
    const float cadence = 0.9f + 0.1f * static_cast<float>(id % 7);  // cycles per second
    const float offset = 0.37f * static_cast<float>(id);
    out = WalkPose(clock.time * cadence + offset);
}

int TrackedLabel(void*, const BoxView& b, uint32_t i, char* buf, int cap) {
    return std::snprintf(buf, static_cast<size_t>(cap), "#%llu  %.0fx%.0f",
                         static_cast<unsigned long long>(b.user[i]), b.w[i], b.h[i]);
}

}  // namespace

int main(int argc, char** argv) {
    // --frames N: run N frames, print the average frame rate, exit.
    long maxFrames = 0;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--frames") == 0)
            maxFrames = std::atol(argv[i + 1]);

    if (!glfwInit())
        return 1;

    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1600, 900, "ImGui DrawTool demo", nullptr, nullptr);
    if (!window)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);  // vsync OFF -- otherwise you're capped at refresh rate

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    DrawTool tool;

    BoxLayer& bouncers = tool.AddLayer("bouncers");
    bouncers.EmplaceMotion<BounceMotion>();
    bouncers.Style().shape = BoxShape::Outline;

    BoxLayer& drifters = tool.AddLayer("drifters");
    drifters.EmplaceMotion<WrapMotion>();
    drifters.Style().shape = BoxShape::FilledOutline;
    drifters.Style().fillAlpha = 0.2f;

    BoxLayer& tracked = tool.AddLayer("tracked");
    tracked.EmplaceMotion<SmoothFollowMotion>(18.0f);
    tracked.Style().shape = BoxShape::Corners;
    tracked.Style().thickness = 2.0f;
    tracked.SetLabels(&TrackedLabel);
    WalkClock walkClock;
    tracked.drawSkeletons = true;
    tracked.SetPoses(&WalkPoseFn, &walkClock);
    tracked.Skeleton().thickness = 2.0f;

    FakeFeed feed;
    int bouncerCount = 5000, drifterCount = 300, trackedCount = 24;
    bool populated = false;

    FrameLimiter limiter(300.0);
    int fpsChoice = 2;  // index into fpsOptions
    const double fpsOptions[] = {0.0, 144.0, 300.0, 360.0, 500.0};
    bool showDebug = true;

    long frameCount = 0;
    const auto start = std::chrono::steady_clock::now();
    while (!glfwWindowShouldClose(window) && (maxFrames <= 0 || frameCount < maxFrames)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const ImGuiIO& io = ImGui::GetIO();
        if (!populated && io.DisplaySize.x > 0) {
            Populate(bouncers.Boxes(), bouncerCount, io.DisplaySize, 10.0f, 60.0f, 300.0f);
            Populate(drifters.Boxes(), drifterCount, io.DisplaySize, 40.0f, 140.0f, 80.0f);
            feed.Init(tracked.Boxes(), trackedCount, io.DisplaySize);
            populated = true;
        }

        feed.Tick(tracked.Boxes(), io.DeltaTime, io.DisplaySize);
        walkClock.time += io.DeltaTime;
        tool.Frame();

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("Demo");
        if (ImGui::Combo("FPS cap", &fpsChoice, "Uncapped\0" "144\0" "300\0" "360\0" "500\0"))
            limiter.SetTarget(fpsOptions[fpsChoice]);
        bool repop = false;
        repop |= ImGui::SliderInt("Bouncers", &bouncerCount, 0, 100000, "%d", ImGuiSliderFlags_Logarithmic);
        repop |= ImGui::SliderInt("Drifters", &drifterCount, 0, 10000, "%d", ImGuiSliderFlags_Logarithmic);
        repop |= ImGui::SliderInt("Tracked", &trackedCount, 0, 500);
        float feedRate = static_cast<float>(feed.rate);
        if (ImGui::SliderFloat("Feed rate", &feedRate, 5.0f, 128.0f, "%.0f Hz"))
            feed.rate = feedRate;
        if (repop)
            populated = false;
        ImGui::Checkbox("Skeletons", &tracked.drawSkeletons);
        ImGui::SameLine();
        ImGui::Checkbox("Walk", &walkClock.animate);
        ImGui::SameLine();
        ImGui::Checkbox("Boxes##tracked", &tracked.drawBoxes);
        ImGui::Checkbox("Debug window", &showDebug);
        ImGui::End();

        if (showDebug)
            ShowDebugWindow(tool, &showDebug);

        ImGui::Render();
        int fbw, fbh;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        limiter.Wait();
        ++frameCount;
    }

    if (maxFrames > 0) {
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("%ld frames in %.2f s = %.1f FPS avg\n", frameCount, secs, frameCount / secs);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
