#define NK_IMPLEMENTATION
#define NK_D3D9_IMPLEMENTATION

#include "UI.hpp"

#include <backends/imgui_impl_dx9.h>
#include <backends/imgui_impl_win32.h>

#include "Globals.hpp"

#include <format>
#include <numeric>

extern void toggle_side_monitor_setting(bool forward);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace ui {
#include "Font.hpp"

    static IDirect3DDevice9* dev;
    static uint8_t show_on_camera = 0;
    static int32_t selected_row = 1;
    static int32_t row_count = 0;
    static float x_pos = 0;
    static bool has_frame = false;
    static ImFontConfig font_cfg;
    static bool horizon_adjustment_changed;

    void init(HWND wnd, IDirect3DDevice9* device, int width, int height)
    {
        dev = device;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouse;

        ImGui_ImplWin32_Init(wnd);
        ImGui_ImplDX9_Init(device);

        font_cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(font, font_size, 18.0, &font_cfg);
    }

    static void update_config_files()
    {
        if (g::cfg.write("Plugins\\openRBRTriples.toml")) {
            g::saved_cfg = g::cfg;
        }
        if (!rbr::update_current_horizon_adjustment()) {
            dbg("Unable to update current horizon adjustment");
        }
        horizon_adjustment_changed = false;
    }

    void tick()
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F6, false)) {
            show_on_camera = (show_on_camera + 1) % (g::cfg.valid_cameras.size() + 1);
            if (show_on_camera > 0) {
                float pos = 0;
                for (auto i = 0; i < show_on_camera - 1; ++i) {
                    pos += g::cfg.valid_cameras[i].get()->w();
                }
                x_pos = pos;
            } else {
                x_pos = 0;
            }
        }

        if (show_on_camera > 0 && rbr::get_game_mode() != rbr::GameMode::MainMenu) {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                if (selected_row == 0) {
                    selected_row = row_count - 1;
                } else {
                    selected_row--;
                }
            }

            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                if (selected_row == row_count - 1) {
                    selected_row = 0;
                } else {
                    selected_row++;
                }
            }

            auto adj_bx = [&](bool dir, bool left) {
                auto& cam = g::cfg.cameras[left ? Left : Right];
                if (!cam.has_value()) return;
                if (dir) cam->bezel_x += 0.5; else cam->bezel_x -= 0.5;
            };
            auto adj_by = [&](bool dir, bool left) {
                auto& cam = g::cfg.cameras[left ? Left : Right];
                if (!cam.has_value()) return;
                if (dir) cam->bezel_y += 0.5; else cam->bezel_y -= 0.5;
            };

            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                bool dir = ImGui::IsKeyPressed(ImGuiKey_RightArrow);
                switch (selected_row) {
                    case 0: adj_bx(dir, true); break;
                    case 1: adj_by(dir, true); break;
                    case 2: adj_bx(dir, false); break;
                    case 3: adj_by(dir, false); break;
                    case 4:
                        if (dir) g::cfg.EyeDistance += 1.0f; else g::cfg.EyeDistance -= 1.0f;
                        break;
                    case 5:
                        if (dir) g::cfg.MonitorWidth += 1.0f; else g::cfg.MonitorWidth -= 1.0f;
                        break;
                    case 6:
                        if (dir) g::cfg.SideAngle += 0.5f; else g::cfg.SideAngle -= 0.5f;
                        break;
                    case 7:
                        // Horizon adjustment is per-car; start from 0 if unset
                        g::cfg.horizon_adjustment = g::cfg.horizon_adjustment.value_or(0.0f) + (dir ? 0.001f : -0.001f);
                        horizon_adjustment_changed = true;
                        break;
                    case 8:
                        toggle_side_monitor_setting(dir);
                        break;
                    default:
                        break;
                }
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Enter) && selected_row == 9) {
                update_config_files();
            }
        }
    }

    void draw()
    {
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        has_frame = true;

        if (show_on_camera == 0) {
            ImGui::EndFrame();
            return;
        }

        constexpr auto item_height = 23.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVarY(ImGuiStyleVar_WindowPadding, ImGui::GetStyle().WindowPadding.x * 2.0f);
        ImGui::SetNextWindowPos({ x_pos, 0 });
        ImGui::SetNextWindowSize({ 600, row_count * 40.0f + 17.0f });
        ImGui::Begin("Window", nullptr, ImGuiWindowFlags_NoDecoration);

        constexpr ImVec4 s = { 0.70f, 0.168f, 0.168f, 1.0f };
        ImGui::SetNavCursorVisible(false);
        ImGui::PushStyleColor(ImGuiCol_Header, s);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, { 0, 0, 0, 0 });
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, { 0, 0, 0, 0 });
        ImGui::PushStyleColor(ImGuiCol_NavHighlight, { 0, 0, 0, 0 });
        ImGui::PushStyleColor(ImGuiCol_NavCursor, { 0, 0, 0, 0 });

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, item_height));

        row_count = 0;
        {
            const bool disable = !g::cfg.cameras[Left].has_value();
            const ImGuiSelectableFlags flags = disable ? ImGuiSelectableFlags_Disabled : ImGuiSelectableFlags_None;
            ImGui::Selectable(std::format("Left bezel H {:.2f}", disable ? 0.0 : g::cfg.cameras[Left]->bezel_x).c_str(), selected_row == row_count++, flags);
            ImGui::Selectable(std::format("Left bezel V {:.2f}", disable ? 0.0 : g::cfg.cameras[Left]->bezel_y).c_str(), selected_row == row_count++, flags);
        }
        {
            const bool disable = !g::cfg.cameras[Right].has_value();
            const ImGuiSelectableFlags flags = disable ? ImGuiSelectableFlags_Disabled : ImGuiSelectableFlags_None;
            ImGui::Selectable(std::format("Right bezel H {:.2f}", disable ? 0.0 : g::cfg.cameras[Right]->bezel_x).c_str(), selected_row == row_count++, flags);
            ImGui::Selectable(std::format("Right bezel V {:.2f}", disable ? 0.0 : g::cfg.cameras[Right]->bezel_y).c_str(), selected_row == row_count++, flags);
        }

        ImGui::Selectable(std::format("Eye distance {:.0f}", g::cfg.EyeDistance).c_str(), selected_row == row_count++);
        ImGui::Selectable(std::format("Monitor width {:.0f}", g::cfg.MonitorWidth).c_str(), selected_row == row_count++);
        ImGui::Selectable(std::format("Side angle {:.1f}", g::cfg.SideAngle).c_str(), selected_row == row_count++);

        ImGui::Selectable(std::format("Horizon adjustment (car and camera specific) {:.3f}", g::cfg.horizon_adjustment.value_or(0.0f)).c_str(), selected_row == row_count++);
        ImGui::Selectable(std::format("Run side monitors with half FPS: {}", g::cfg.side_monitors_half_hz ? (g::cfg.side_monitors_half_hz_btb_only ? "BTB only" : "ON") : "OFF").c_str(), selected_row == row_count++);

        {
            const bool disable = g::cfg == g::saved_cfg && !horizon_adjustment_changed;
            const ImGuiSelectableFlags flags = disable ? ImGuiSelectableFlags_Disabled : ImGuiSelectableFlags_None;
            ImGui::Selectable("Save current settings", selected_row == row_count++, flags);
        }

        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(3);
        ImGui::End();
        ImGui::EndFrame();
    }

    void present(IDirect3DSurface9* surface)
    {
        if (!has_frame)
            return;

        g::d3d_dev->SetRenderTarget(0, surface);
        g::d3d_dev->SetVertexShader(nullptr);
        g::d3d_dev->SetPixelShader(nullptr);
        D3DMATRIX m = d3d_from_m4(glm::identity<M4>());
        g::d3d_dev->BeginScene();
        g::d3d_dev->SetTransform(D3DTS_VIEW, &m);
        g::d3d_dev->SetTransform(D3DTS_WORLD, &m);
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        g::d3d_dev->EndScene();
        g::d3d_dev->SetRenderTarget(0, nullptr);
        has_frame = false;
    }

    void wndproc(HWND hWindow, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        ImGui_ImplWin32_WndProcHandler(hWindow, uMsg, wParam, lParam);
    }
}
