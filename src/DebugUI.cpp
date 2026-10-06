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
    ImGui::Text("%u vtx, %u idx", st.render.vertices, st.render.indices);
    ImGui::Separator();

    for (auto& lp : tool.Layers()) {
        BoxLayer& l = *lp;
        ImGui::PushID(&l);
        const bool openNode = ImGui::TreeNode("##layer", "%s  (%u boxes)", l.Name().c_str(), l.Boxes().Size());
        if (openNode) {
            ImGui::Checkbox("Visible", &l.visible);
            ImGui::SameLine();
            ImGui::Checkbox("Paused", &l.paused);
            ImGui::Text("drawn %u, culled %u, %u vtx", l.lastStats.drawn, l.lastStats.culled, l.lastStats.vertices);
            if (auto* follow = dynamic_cast<SmoothFollowMotion*>(l.Motion())) {
                float rate = follow->Rate();
                if (ImGui::SliderFloat("Follow rate", &rate, 1.0f, 60.0f, "%.1f /s"))
                    follow->SetRate(rate);
            }
            EditStyle(l.Style());
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::End();
}

}  // namespace drawtool
