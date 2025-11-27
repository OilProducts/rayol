#include "ui/demo_ui.h"

#if RAYOL_USE_IMGUI
#include <imgui.h>

namespace rayol::ui {

bool render_demo_ui() {
    bool exit_requested = false;

    const ImGuiViewport* vp = ImGui::GetMainViewport();

    // Top panel (upper ~2/3)
    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y * 0.66f));
    ImGuiWindowFlags top_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("RayolTopPane", nullptr, top_flags);
    ImGui::TextUnformatted("Rayol Prototype");
    static int gfx_quality = 2;  // 0=Low,1=Med,2=High,3=Ultra
    const char* qualities[] = {"Low", "Medium", "High", "Ultra"};
    ImGui::Separator();
    if (ImGui::Button("Start")) {
        // placeholder start
    }
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Graphics", &gfx_quality, qualities, IM_ARRAYSIZE(qualities));
    static float master_volume = 0.8f;
    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderFloat("Master Volume", &master_volume, 0.0f, 1.0f, "%.0f%%");
    static bool vsync = true;
    ImGui::Checkbox("VSync", &vsync);
    ImGui::End();

    // Bottom panel (lower ~1/3) with centered Exit
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y * 0.66f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y * 0.34f));
    ImGuiWindowFlags bottom_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("RayolBottomPane", nullptr, bottom_flags);
    ImGui::Dummy(ImVec2(0.0f, 20.0f));
    float button_width = 120.0f;
    float button_height = 32.0f;
    float x = (ImGui::GetContentRegionAvail().x - button_width) * 0.5f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
    if (ImGui::Button("Exit", ImVec2(button_width, button_height))) {
        exit_requested = true;
    }
    ImGui::End();

    return exit_requested;
}

}  // namespace rayol::ui

#else

namespace rayol::ui {
bool render_demo_ui() { return false; }
}  // namespace rayol::ui

#endif
