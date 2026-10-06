# Imgui-DrawTool

A fast, modular Dear ImGui system for drawing large numbers of moving 2D boxes.
It is built for high-refresh overlays (300+ FPS) and visualizations.

```
10,000 boxes @ 2560x1440, CPU per frame (ImGui frame + motion + tessellation)

style                  frame ms   max FPS*   vertices  draw cmds
Filled                   0.13      7,700       40,000      1
Outline                  0.17      5,600       80,000      2
Outline + shadow         0.29      3,400      160,000      4
Filled + outline + sh.   0.40      2,500      200,000      5
Corners                  0.50      2,000      240,000      5
Corners + shadow         1.0–1.1     ~900     480,000      8

ImDrawList::AddRect               0.57      1,700   (same boxes, no shadow)
AddRect + shadow AddRect          1.12        890
```
<sub>* CPU side only, from `drawtool_bench` (4-core cloud VM). At 300 FPS the frame budget is 3.33 ms. A typical overlay (tens to hundreds of boxes) costs around 0.01–0.03 ms.</sub>

## Why it's fast

| Technique | Effect |
|---|---|
| **Structure-of-arrays storage** (`BoxStore`) | The update and render loops read contiguous `float` columns, so they stay cache friendly and the compiler can auto-vectorize them. |
| **Raw vertex writes** | One `PrimReserve` per 2,048-box chunk, then plain pointer stores. Skips ImGui's path, stroke and anti-alias fringe code. Unused space is returned with `PrimUnreserve`. |
| **Minimal geometry** | An outline is 8 vertices (outer and inner ring), where `AddRect` builds a stroked anti-aliased path. Corner brackets are 6-vertex L-fans. |
| **Pixel snapping** | Edges are rounded to whole pixels. Lines stay sharp without AA and don't shimmer while moving. |
| **Templated kernels** | Shape and shadow are chosen once per layer, so the inner loop has no style branches. |
| **Culling** | Boxes that are off-screen, degenerate or NaN are skipped before any geometry is written. |
| **~1 draw call** | All layers go into one draw list using the font atlas white pixel. A new command is only added each time ~64K vertices fill up (16-bit index rollover). |
| **No per-frame allocations** | Draw list and store buffers keep their capacity between frames. |

## Modules

```
include/drawtool/
  Types.h          Rect, BoxView (read-only SoA), BoxSpan (mutable SoA)
  BoxStore.h       SoA storage with stable handles (slot map, O(1) add/remove)
  Motion.h         IMotion + LinearMotion, BounceMotion, WrapMotion,
                   SmoothFollowMotion, LambdaMotion
  Style.h          BoxShape (Outline/Filled/FilledOutline/Corners), BoxStyle
  BoxRenderer.h    stateless tessellator: BoxView -> ImDrawList (+ labels)
  DrawTool.h       facade: layers, bounds, per-frame Update/Render, stats
  DebugUI.h        optional ImGui window: timings, per-layer stats, live style editing
  FrameLimiter.h   header-only sleep+spin limiter (e.g. hold 300 FPS)
```

Each piece can be used without the others:

- **Renderer only:** keep your boxes in your own arrays and call
  `BoxRenderer::Draw(dl, BoxView{...}, style)`.
- **Store + renderer:** skip motion modules and update positions yourself
  through `BoxStore` handles.
- **Custom motion:** derive from `IMotion`, or use `MakeLambdaMotion([](BoxSpan b, const MotionContext& c){...})`.
- **DebugUI** and **FrameLimiter** are optional, so you can leave them out of a release build.

## Usage

```cpp
#include "drawtool/DrawTool.h"
using namespace drawtool;

DrawTool tool;

// Boxes that simulate themselves
BoxLayer& bouncers = tool.AddLayer("bouncers");
bouncers.EmplaceMotion<BounceMotion>();
bouncers.Style().shape = BoxShape::Outline;
BoxDesc d;
d.pos = {100, 100};
d.size = {40, 40};
d.vel = {250, -180};                                // px/s
d.color = IM_COL32(0, 255, 120, 255);
bouncers.Boxes().Add(d);

// Boxes driven by an external, slower data source
BoxLayer& tracked = tool.AddLayer("tracked");
tracked.EmplaceMotion<SmoothFollowMotion>(20.0f);   // glide toward targets
tracked.Style().shape = BoxShape::Corners;
BoxDesc t;
t.pos = {500, 300};
t.size = {60, 120};
BoxHandle h = tracked.Boxes().Add(t);               // handle stays valid until Remove(h)

// ...whenever new data arrives (30 Hz, 64 Hz, whatever):
tracked.Boxes().SetTarget(h, newPos, newSize);

// Every frame, between ImGui::NewFrame() and ImGui::Render():
tool.Frame();                 // Update(io.DeltaTime) + Render() to background draw list
```

`SmoothFollowMotion` matters when your positions update slower than you render.
Without it, boxes jump at the data rate (for example 30 Hz) even though the
screen redraws at 300 Hz. The smoothing is exponential and independent of frame
rate, so 300 × (1/300 s) gives the same result as 60 × (1/60 s). Use
`BoxStore::Snap()` to teleport a box without gliding.

Labels are optional, per layer, and drawn as a separate text pass. Text costs
far more than boxes, so keep labels to the boxes that need them:

```cpp
tracked.SetLabels([](void*, const BoxView& b, uint32_t i, char* buf, int cap) {
    return snprintf(buf, cap, "#%llu", (unsigned long long)b.user[i]);
});
```

## Hitting 300 FPS

1. **Turn vsync off.** Use `glfwSwapInterval(0)` or `swapChain->Present(0, 0)`.
   With vsync on you're capped at the monitor refresh rate.
2. **Cap with `FrameLimiter`** instead of running uncapped. This avoids burning
   the GPU and keeps frame pacing even. On Windows, call `timeBeginPeriod(1)`
   so sleep has 1 ms granularity.
3. **Large meshes:** every official ImGui backend supports `VtxOffset`. A custom
   backend that doesn't should `#define ImDrawIdx unsigned int` in
   `imconfig.h`. Otherwise the renderer stops at 65,535 vertices per draw list
   rather than drawing corrupted geometry.
4. **Shadow doubles stroke geometry.** If you're pushing tens of thousands of
   boxes, set `shadowThickness = 0` or use `Outline` instead of `Corners`.

## Building

The first configure downloads Dear ImGui v1.92.9, plus GLFW for the demo, so it needs `git` and internet access.

**Windows (Visual Studio):** build type is picked at *build* time with `--config`:

```bat
cmake -S . -B build
cmake --build build --config Release
build\Release\drawtool_tests.exe
build\Release\drawtool_bench.exe 10000 2000
build\examples\Release\drawtool_demo.exe
```

**Linux / macOS / Ninja:** build type is picked at *configure* time:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/drawtool_tests                   # unit tests
./build/drawtool_bench 10000 2000        # headless CPU benchmark: [boxes] [frames]
./build/examples/drawtool_demo           # GLFW + OpenGL3 demo, 300 FPS cap
./build/examples/drawtool_demo --frames 3000   # run N frames, print average FPS
```

Always benchmark a Release build. A Debug build is many times slower.

CMake options:

| Option | Default | |
|---|---|---|
| `DRAWTOOL_IMGUI_DIR` | *(empty)* | Use an existing ImGui checkout instead of downloading one |
| `DRAWTOOL_BUILD_DEMO` | ON (top level) | Needs OpenGL and the X11/Win32 dev libs used by GLFW |
| `DRAWTOOL_BUILD_BENCH` | ON (top level) | |
| `DRAWTOOL_BUILD_TESTS` | ON (top level) | |

### Dropping into an existing project

If your project already defines an `imgui` target, `add_subdirectory(Imgui-DrawTool)`
picks it up and only builds the `drawtool` library:

```cmake
add_subdirectory(third_party/Imgui-DrawTool)
target_link_libraries(my_app PRIVATE drawtool::drawtool)
```

Without CMake, add `src/*.cpp` and `include/` to your build. The only
dependency is `imgui.h`. Requires C++17.
