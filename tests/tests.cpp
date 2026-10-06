// Minimal self-contained tests (no framework). Exit code != 0 on failure.

#include <cmath>
#include <cstdio>

#include "drawtool/DrawTool.h"

using namespace drawtool;

static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

static BoxDesc Box(float x, float y, float w = 10.0f, float h = 10.0f) {
    BoxDesc d;
    d.pos = {x, y};
    d.size = {w, h};
    return d;
}

static void TestStoreHandles() {
    BoxStore s;
    BoxHandle a = s.Add(Box(1, 1));
    BoxHandle b = s.Add(Box(2, 2));
    BoxHandle c = s.Add(Box(3, 3));
    CHECK(s.Size() == 3);

    CHECK(s.Remove(a));
    CHECK(!s.IsValid(a));
    CHECK(!s.Remove(a));  // double remove is a no-op
    CHECK(s.Size() == 2);

    // c was swapped into a's dense slot; handles still resolve correctly.
    CHECK(s.IsValid(b) && s.IsValid(c));
    CHECK(s.View().x[s.IndexOf(b)] == 2.0f);
    CHECK(s.View().x[s.IndexOf(c)] == 3.0f);
    CHECK(s.HandleAt(s.IndexOf(c)) == c);

    // Slot reuse bumps generation: old handle must not alias the new box.
    BoxHandle d = s.Add(Box(4, 4));
    CHECK(d.slot == a.slot && d.generation != a.generation);
    CHECK(!s.IsValid(a));
    CHECK(!s.SetPosition(a, {9, 9}));
    CHECK(s.SetPosition(d, {5, 5}));
    CHECK(s.View().x[s.IndexOf(d)] == 5.0f);

    s.Clear();
    CHECK(s.Empty() && !s.IsValid(b) && !s.IsValid(c) && !s.IsValid(d));
    BoxHandle e = s.Add(Box(0, 0));
    CHECK(s.IsValid(e) && s.Size() == 1);
}

static void TestBounce() {
    BoxStore s;
    BoxDesc d = Box(95, 50);
    d.vel = {100, 0};
    BoxHandle h = s.Add(d);
    BounceMotion m;
    MotionContext ctx;
    ctx.dt = 0.1f;  // would move 10px -> overshoot right edge by 15
    ctx.bounds = {0, 0, 100, 100};
    m.Update(s.Span(), ctx);
    const uint32_t i = s.IndexOf(h);
    CHECK(std::fabs(s.View().x[i] - 75.0f) < 1e-4f);
    CHECK(s.Span().vx[i] < 0.0f);
}

static void TestFollow() {
    BoxStore s;
    BoxHandle h = s.Add(Box(0, 0));
    s.SetTarget(h, {100, 0}, {10, 10});
    SmoothFollowMotion m(20.0f);
    MotionContext ctx;
    ctx.bounds = {0, 0, 1000, 1000};
    // Frame-rate independence: 300 x 1/300s == 60 x 1/60s.
    BoxStore s2;
    BoxHandle h2 = s2.Add(Box(0, 0));
    s2.SetTarget(h2, {100, 0}, {10, 10});
    ctx.dt = 1.0f / 300.0f;
    for (int i = 0; i < 300; ++i) m.Update(s.Span(), ctx);
    ctx.dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) m.Update(s2.Span(), ctx);
    CHECK(std::fabs(s.View().x[0] - s2.View().x[0]) < 1e-2f);
    CHECK(s.View().x[0] > 99.0f);
}

static void TestRenderer() {
    ImGui::NewFrame();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    BoxStore s;
    s.Add(Box(10, 10, 50, 50));     // visible
    s.Add(Box(-500, 10, 50, 50));   // off-screen left
    s.Add(Box(100, 100, 0.5f, 50)); // below minSize
    s.Add(Box(300, 300, 20, 20));   // visible

    const Rect clip{0, 0, 800, 600};
    const BoxShape shapes[] = {BoxShape::Outline, BoxShape::Filled, BoxShape::FilledOutline, BoxShape::Corners};
    for (BoxShape shape : shapes) {
        for (float shadow : {0.0f, 1.0f}) {
            BoxStyle st;
            st.shape = shape;
            st.shadowThickness = shadow;
            const int v0 = dl->VtxBuffer.Size, i0 = dl->IdxBuffer.Size;
            const RenderStats r = BoxRenderer::Draw(dl, s.View(), st, clip);
            uint32_t pv, pi;
            BoxRenderer::CostPerBox(st, pv, pi);
            CHECK(r.drawn == 2 && r.culled == 2);
            CHECK(r.vertices == 2 * pv && r.indices == 2 * pi);
            // Unused reservation was handed back exactly.
            CHECK(static_cast<uint32_t>(dl->VtxBuffer.Size - v0) == r.vertices);
            CHECK(static_cast<uint32_t>(dl->IdxBuffer.Size - i0) == r.indices);
            CHECK(dl->_VtxWritePtr == dl->VtxBuffer.Data + dl->VtxBuffer.Size);
            CHECK(dl->_IdxWritePtr == dl->IdxBuffer.Data + dl->IdxBuffer.Size);
        }
    }
    ImGui::Render();
}

static void TestLargeBatch16BitIndices() {
    // > 64K vertices in one draw list must split via VtxOffset with all
    // indices in range of their command.
    BoxStore s;
    for (int i = 0; i < 20000; ++i) s.Add(Box(static_cast<float>(i % 700), static_cast<float>((i / 700) % 500), 20, 20));

    ImGui::NewFrame();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    BoxStyle st;
    st.shape = BoxShape::Corners;  // 48 vtx/box with shadow -> ~960K vtx
    const RenderStats r = BoxRenderer::Draw(dl, s.View(), st, Rect{0, 0, 2000, 2000});
    CHECK(r.drawn == 20000);
    ImGui::Render();

    const ImDrawData* dd = ImGui::GetDrawData();
    bool ok = true;
    for (const ImDrawList* list : dd->CmdLists) {
        for (int c = 0; c < list->CmdBuffer.Size; ++c) {
            const ImDrawCmd& cmd = list->CmdBuffer[c];
            if (cmd.UserCallback) continue;
            for (unsigned k = 0; k < cmd.ElemCount; ++k) {
                const unsigned idx = list->IdxBuffer[static_cast<int>(cmd.IdxOffset + k)] + cmd.VtxOffset;
                if (idx >= static_cast<unsigned>(list->VtxBuffer.Size)) ok = false;
            }
        }
    }
    CHECK(ok);
    CHECK(dd->TotalVtxCount >= static_cast<int>(r.vertices));
}

static void TestNoVtxOffsetBudget() {
    // Backend without VtxOffset + 16-bit indices: geometry must be capped at
    // 64K vertices rather than wrapping indices.
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
    BoxStore s;
    for (int i = 0; i < 10000; ++i) s.Add(Box(10, 10, 20, 20));

    ImGui::NewFrame();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    BoxStyle st;  // Outline + shadow, 16 vtx/box -> 65535 / 16 = 4095 boxes fit
    const RenderStats r = BoxRenderer::Draw(dl, s.View(), st, Rect{0, 0, 800, 600});
    if (sizeof(ImDrawIdx) == 2) {
        CHECK(r.drawn == 4095);
        CHECK(r.culled == 10000 - 4095);
        CHECK(dl->VtxBuffer.Size < 65536);
    } else {
        CHECK(r.drawn == 10000);
    }
    CHECK(static_cast<uint32_t>(dl->VtxBuffer.Size) == r.vertices);
    ImGui::Render();
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
}

static void TestToolLayers() {
    DrawTool tool;
    BoxLayer& a = tool.AddLayer("a");
    tool.AddLayer("b");
    a.Boxes().Add(Box(10, 10));
    CHECK(tool.FindLayer("b") != nullptr);
    CHECK(tool.RemoveLayer("b"));
    CHECK(tool.FindLayer("b") == nullptr);

    ImGui::NewFrame();
    const FrameStats& st = tool.Frame();
    CHECK(st.render.drawn == 1);
    a.visible = false;
    CHECK(tool.Render().render.drawn == 0);
    ImGui::Render();
}

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 300.0f;
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    unsigned char* px;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);

    TestStoreHandles();
    TestBounce();
    TestFollow();
    TestRenderer();
    TestLargeBatch16BitIndices();
    TestNoVtxOffsetBudget();
    TestToolLayers();

    ImGui::DestroyContext();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
