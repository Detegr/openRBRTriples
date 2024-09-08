#include "RBR.hpp"
#include "Dx.hpp"
#include "Globals.hpp"
#include "Util.hpp"

#include <ranges>

// Compilation unit global variables
namespace g {
    static uint32_t* camera_type_ptr;
    static uint32_t* stage_id_ptr;

    static rbr::GameMode game_mode;
    static rbr::GameMode previous_game_mode;
    static uint32_t current_stage_id;
    static bool is_rendering_3d;
}

namespace rbr {
    static uintptr_t get_base_address()
    {
        // If ASLR is enabled, the base address is randomized
        static uintptr_t addr;
        addr = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
        if (!addr) {
            dbg("Could not retrieve RBR base address, this may be bad.");
        }
        return addr;
    }

    static uintptr_t get_hedgehog_base_address()
    {
        // If ASLR is enabled, the base address is randomized
        static uintptr_t addr;
        addr = reinterpret_cast<uintptr_t>(GetModuleHandle("HedgeHog3D.dll"));
        if (!addr) {
            dbg("Could not retrieve RBR base address, this may be bad.");
        }
        return addr;
    }

    static uintptr_t get_address(uintptr_t target)
    {
        constexpr uintptr_t RBR_ABSOLUTE_LOAD_ADDR = 0x400000;
        return get_base_address() + target - RBR_ABSOLUTE_LOAD_ADDR;
    }

    uintptr_t get_hedgehog_address(uintptr_t target)
    {
        constexpr uintptr_t HEDGEHOG_ABSOLUTE_LOAD_ADDR = 0x10000000;
        return get_hedgehog_base_address() + target - HEDGEHOG_ABSOLUTE_LOAD_ADDR;
    }

    static uintptr_t RENDER_FUNCTION_ADDR = get_address(0x47E1E0);
    static uintptr_t CAR_INFO_ADDR = get_address(0x165FC68);
    static uintptr_t* GAME_MODE_EXT_2_PTR = reinterpret_cast<uintptr_t*>(get_address(0x007EA678));

    using ChangeCameraFn = void(__thiscall*)(void* p, int cameraType, uint32_t a);
    using PrepareCameraFn = void(__thiscall*)(void* This, float a);
    using SomeFn = void(__thiscall*)(void* This);
    using PostPrepareCameraFn = void(__thiscall*)(void* This, float a);
    static PrepareCameraFn apply_camera_position = reinterpret_cast<PrepareCameraFn>(get_address(0x4825B0));
    static PrepareCameraFn apply_camera_fov = reinterpret_cast<PrepareCameraFn>(get_address(0x4BF690));
    static PostPrepareCameraFn post_prepare_camera = reinterpret_cast<PostPrepareCameraFn>(get_address(0x487320));

    uintptr_t get_render_function_addr()
    {
        return RENDER_FUNCTION_ADDR;
    }

    GameMode get_game_mode()
    {
        return g::game_mode;
    }

    bool is_on_btb_stage()
    {
        return g::btb_track_status_ptr && *g::btb_track_status_ptr == 1;
    }

    bool is_loading_btb_stage()
    {
        return is_on_btb_stage() && g::game_mode == GameMode::Loading;
    }

    bool is_rendering_3d()
    {
        return g::is_rendering_3d;
    }

    bool is_using_cockpit_camera()
    {
        if (!g::camera_type_ptr) {
            return false;
        }

        const auto camera = *g::camera_type_ptr;
        return (camera >= 3) && (camera <= 6);
    }

    uint32_t get_current_stage_id()
    {
        return g::current_stage_id;
    }

    static uintptr_t get_camera_info_ptr()
    {
        uintptr_t cameraData = *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(CAR_INFO_ADDR) + 0x758);
        return *reinterpret_cast<uintptr_t*>(cameraData + 0x10);
    }

    static void write_byte(uint8_t* address, uint8_t newValue)
    {
        DWORD oldProtect;

        // Change memory protection to allow writing
        if (VirtualProtect(address, sizeof(BYTE), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            *address = newValue; // Modify the byte
            // Restore the original protection
            VirtualProtect(address, sizeof(BYTE), oldProtect, &oldProtect);
        } else {
            std::cerr << "Failed to change memory protection." << std::endl;
        }
    }

    // Read camera FoV from the currently selected RBR camera
    // and recreate the projection matrix with the correct FoV
    void update_current_camera_fov(uintptr_t p)
    {
        float* original_fov_ptr;
        float* current_fov_ptr = reinterpret_cast<float*>(p + 0x70 + 0x2c0);
        float* z_near_ptr = reinterpret_cast<float*>(p + 0x70 + 0x290);

        // The 3 cameras (bumper, bonnet, internal) whose FoV can be set via Pacenote plugin
        // The FoV is read from these values by the game, and written into `current_fov_ptr` in memory
        switch (*g::camera_type_ptr) {
            case 3:
                original_fov_ptr = reinterpret_cast<float*>(get_camera_info_ptr() + 0x348);
                break;

            case 4:
                original_fov_ptr = reinterpret_cast<float*>(get_camera_info_ptr() + 0x380);
                break;

            case 5:
                original_fov_ptr = reinterpret_cast<float*>(get_camera_info_ptr() + 0x3b8);
                break;

            default:
                original_fov_ptr = current_fov_ptr;
        }

        if (*original_fov_ptr == 0.0) {
            return;
        }

        float original_fov_ptr_value = *original_fov_ptr;

        // This has to be done to make the FoV value correct for glm::perspectiveFovLH
        // It is always 4/3 even if the current resolution has a different aspect ratio
        float original_fov = *original_fov_ptr / (4.0f / 3.0f);
        float fov = glm::radians(original_fov);

        // Z-near at 0 breaks Z-buffer (and does not make sense anyway), so force it non-zero
        *z_near_ptr = std::max(0.01f, *z_near_ptr);

        if (g::game_mode == GameMode::MainMenu) [[unlikely]] {
            // Fix the main menu FoV to make it look good
            fov = 0.4f;
        }

        // Re-calculate the correct angle for the new FoV for the side views
        for (size_t i = 0; i < g::cfg.cameras.size(); ++i) {
            auto cfov = fov + static_cast<float>(g::cfg.cameras[i].fov);
            g::projection_matrix[i] = glm::perspectiveFovLH_ZO(
                cfov,
                static_cast<float>(g::cfg.cameras[0].w()),
                static_cast<float>(g::cfg.cameras[0].h()),
                *z_near_ptr, 10000.0f);

            if (i != RenderTarget::Primary) {
                const auto aspect = static_cast<double>(g::cfg.cameras[0].w()) / static_cast<double>(g::cfg.cameras[0].h());
                g::cfg.cameras[i].angle = 2.0 * std::atan(std::tan(fov / 2.0) * aspect);
            }
        }

        const auto mode = rbr::get_game_mode();
        // On BTB stages the FoV does not matter as the object culling effect is not in use
        // Also there's no bad weather on BTB stages so we don't need the wiper fix either
        if (!is_on_btb_stage()) {
            // Apply larger FoV for rendering in order to prevent objects from disappearing
            // from the peripheral view. As we're using a separate projection matrix, this has no effect
            // on the actual projection, just for the RBR rendering optimization logic that starts culling
            // objects that are not visible.
            const auto camera_post_prepare_this = reinterpret_cast<void*>(p + 0x70);
            const auto camera_fov_this = *reinterpret_cast<void**>(p + 0xcf4);

            // 2.4 seems to work very well with all kinds of FoVs for some reason
            // It really like a sweet spot with the least amount of objects popping out
            // We can't go 3 times the normal FoV because RBR does not like very wide FoVs.
            // With very wide FoVs the objects start to disappear the same way as they do with a small FoV.
            *current_fov_ptr = glm::degrees(2.4f);
            post_prepare_camera(camera_post_prepare_this, 0.0);

            // Fix wiper animation
            // The function at 0x10067254 must not be called when patching the FoV
            // for the wiper animation to run correctly.
            // Therefore, nop (0x90) out the call at 0x10067254 when calling `apply_camera_fov` and
            // restore it back to correctly call it in g::hooks::render
            static uint8_t* wiper_anim_loc;
            static uint8_t orig_bytes[5];

            if (!wiper_anim_loc) {
                wiper_anim_loc = reinterpret_cast<uint8_t*>(rbr::get_hedgehog_address(0x10067254));
                memcpy(orig_bytes, wiper_anim_loc, 5);
            }

            for (int i = 0; i < 5; ++i) {
                write_byte(wiper_anim_loc + i, 0x90);
            }

            apply_camera_fov(camera_fov_this, 0.0);

            for (int i = 0; i < 5; ++i) {
                write_byte(wiper_anim_loc + i, orig_bytes[i]);
            }

            *current_fov_ptr = original_fov_ptr_value;
        }
    }

    static bool init_or_update_game_data(uintptr_t ptr)
    {
        static bool window_resized = false;
        if (!window_resized) [[unlikely]] {
            D3DPRESENT_PARAMETERS params;
            g::swapchain->GetPresentParameters(&params);
            auto xmin = std::min_element(g::cfg.cameras.cbegin(), g::cfg.cameras.cend(), [](const auto& a, const auto& b) { return a.extent[0] < b.extent[0]; })->extent[0];
            SetWindowPos(g::main_window, HWND_TOP, xmin, 0, params.BackBufferWidth, params.BackBufferHeight, SWP_NOREPOSITION | SWP_FRAMECHANGED);
            window_resized = true;
        }

        auto game_mode = *reinterpret_cast<GameMode*>(ptr + 0x728);
        if (game_mode != g::game_mode) [[unlikely]] {
            g::previous_game_mode = g::game_mode;
            g::game_mode = game_mode;
        }

        if (!g::camera_type_ptr) [[unlikely]] {
            g::camera_type_ptr = reinterpret_cast<uint32_t*>(get_camera_info_ptr());
        }

        if (!g::stage_id_ptr) [[unlikely]] {
            g::stage_id_ptr = reinterpret_cast<uint32_t*>(*reinterpret_cast<uintptr_t*>(*GAME_MODE_EXT_2_PTR + 0x70) + 0x20);
        }

        auto should_draw = *reinterpret_cast<uint32_t*>(ptr + 0x720) == 0;

        if (should_draw && (g::game_mode == GameMode::MainMenu || g::game_mode == GameMode::Driving || g::game_mode == GameMode::Replay || g::game_mode == Pause || g::game_mode == PreStage)) {
            update_current_camera_fov(ptr);
        }

        return should_draw;
    }

    // RBR 3D scene draw function is rerouted here
    void __fastcall render(void* p)
    {
        auto do_rendering = init_or_update_game_data(reinterpret_cast<uintptr_t>(p));

        if (g::d3d_dev->GetRenderTarget(0, &g::original_render_target) != D3D_OK) [[unlikely]] {
            dbg("Could not get render original target");
        }
        if (g::d3d_dev->GetDepthStencilSurface(&g::original_depth_stencil_target) != D3D_OK) [[unlikely]] {
            dbg("Could not get render original depth stencil surface");
        }

        if (!do_rendering) [[unlikely]] {
            return;
        }

        g::is_rendering_3d = true;

        static RenderTarget render_target_to_skip = RenderTarget::Left;

        for (const auto& [i, c] : std::views::enumerate(g::cfg.cameras)) {
            if (g::cfg.side_monitors_half_hz && i == render_target_to_skip) {
                if (!g::cfg.side_monitors_half_hz_btb_only || (g::cfg.side_monitors_half_hz_btb_only && rbr::is_on_btb_stage())) {
                    continue;
                }
            }
            dx::set_render_target(static_cast<RenderTarget>(i));
            g::hooks::render.call(p);
        };

        render_target_to_skip = (render_target_to_skip == RenderTarget::Right) ? RenderTarget::Left : RenderTarget::Right;
        dx::set_render_target(RenderTarget::Primary, false);
        g::is_rendering_3d = false;
    }
}

namespace rbr_rx {
    bool is_loaded()
    {
        return reinterpret_cast<uintptr_t>(g::btb_track_status_ptr) != TRACK_STATUS_OFFSET;
    }
}

namespace rbrhud {
    bool is_loaded()
    {
        static bool tried_to_obtain;
        static bool is_loaded;

        if (!tried_to_obtain) {
            auto handle = GetModuleHandle("Plugins\\RBRHUD.dll");
            is_loaded = (handle != nullptr);
        }
        return is_loaded;
    }
}
