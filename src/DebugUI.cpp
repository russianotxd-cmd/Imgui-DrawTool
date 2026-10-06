#include "drawtool/DebugUI.h"

namespace drawtool {

bool EditStyle(BoxStyle& s) {
    bool changed = false;
    int shape = static_cast<int>(s.shape);
    if (ImGui::Combo("Shape", &shape, "Outline\0Filled\0Filled + Outline\0Corners\0")) {
        s.shape = static_cast<BoxShape>(shape);
        changed = true;
    }
    changed |= ImGui::SliderFloat("Thickness", &s.thickness, 1.0f, 8.0f, "%.0f px");
    if (s.shape == BoxShape::Corners)
        changed |= ImGui::SliderFloat("Corner length", &s.cornerLength, 0.05f, 0.5f, "%.2f");
    if (s.shape == BoxShape::FilledOutline)
        changed |= ImGui::SliderFloat("Fill alpha", &s.fillAlpha, 0.0f, 1.0f, "%.2f");
    if (s.shape != BoxShape::Filled) {
        changed |= ImGui::SliderFloat("Shadow", &s.shadowThickness, 0.0f, 4.0f, "%.0f px");
        ImVec4 sc = ImGui::ColorConvertU32ToFloat4(s.shadowColor);
        if (ImGui::ColorEdit4("Shadow color", &sc.x, ImGuiColorEditFlags_NoInputs)) {
            s.shadowColor = ImGui::ColorConvertFloat4ToU32(sc);
            changed = true;
        }
    }
    changed |= ImGui::Checkbox("Snap to pixel", &s.snapToPixel);
    return changed;
}

bool EditSkeletonStyle(SkeletonStyle& s) {
    bool changed = false;
    changed |= ImGui::SliderFloat("Line width", &s.thickness, 1.0f, 6.0f, "%.1f px");
    changed |= ImGui::Checkbox("Anti-aliased", &s.antiAliased);
    changed |= ImGui::Checkbox("Use box color", &s.useBoxColor);
    if (!s.useBoxColor) {
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(s.color);
        if (ImGui::ColorEdit4("Line color", &c.x, ImGuiColorEditFlags_NoInputs)) {
            s.color = ImGui::ColorConvertFloat4ToU32(c);
            changed = true;
        }
    }
    changed |= ImGui::SliderFloat("Line shadow", &s.shadowThickness, 0.0f, 4.0f, "%.1f px");
    changed |= ImGui::Checkbox("Head circle", &s.drawHead);
    if (s.drawHead)
        changed |= ImGui::SliderFloat("Head size", &s.headRadius, 0.03f, 0.15f, "%.3f");
    return changed;
}

void ShowDebugWindow(DrawTool& tool, bool* open) {
    if (!ImGui::Begin("DrawTool", open)) {
        ImGui::End();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const FrameStats& st = tool.Stats();
    ImGui::Text("%.0f FPS (%.3f ms)", io.Framerate, 1000.0f / (io.Framerate > 0.0f ? io.Framerate : 1.0f));
    ImGui::Text("update %.3f ms | tessellate %.3f ms", st.updateMs, st.renderMs);
    ImGui::Text("boxes %u drawn / %u culled", st.render.drawn, st.render.culled);
    if (st.skeletons.submitted)
        ImGui::Text("skeletons %u drawn / %u culled", st.skeletons.drawn, st.skeletons.culled);
    ImGui::Text("%u vtx, %u idx", st.render.vertices + st.skeletons.vertices, st.render.indices + st.skeletons.indices);
    ImGui::Separator();

    for (auto& lp : tool.Layers()) {
        BoxLayer& l = *lp;
        ImGui::PushID(&l);
        const bool openNode = ImGui::TreeNode("##layer", "%s  (%u boxes)", l.Name().c_str(), l.Boxes().Size());
        if (openNode) {
            ImGui::Checkbox("Visible", &l.visible);
            ImGui::SameLine();
            ImGui::Checkbox("Paused", &l.paused);
            ImGui::Checkbox("Boxes", &l.drawBoxes);
            ImGui::SameLine();
            ImGui::Checkbox("Skeletons", &l.drawSkeletons);
            ImGui::Text("drawn %u, culled %u, %u vtx", l.lastStats.drawn, l.lastStats.culled, l.lastStats.vertices);
            if (auto* follow = dynamic_cast<SmoothFollowMotion*>(l.Motion())) {
                float rate = follow->Rate();
                if (ImGui::SliderFloat("Follow rate", &rate, 1.0f, 60.0f, "%.1f /s"))
                    follow->SetRate(rate);
            }
            if (l.drawBoxes && ImGui::TreeNode("Box style")) {
                EditStyle(l.Style());
                ImGui::TreePop();
            }
            if (l.drawSkeletons && ImGui::TreeNode("Skeleton style")) {
                EditSkeletonStyle(l.Skeleton());
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::End();
}

}  // namespace drawtool
