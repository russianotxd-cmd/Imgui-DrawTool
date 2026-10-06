#pragma once

#include "drawtool/DrawTool.h"

namespace drawtool {

// Optional module: an ImGui window with frame timings, per-layer counters and
// live style editing. Not needed at runtime -- leave out of release builds.
void ShowDebugWindow(DrawTool& tool, bool* open = nullptr);

// Just the style controls, for embedding in your own UI.
bool EditStyle(BoxStyle& style);
bool EditSkeletonStyle(SkeletonStyle& style);

}  // namespace drawtool
