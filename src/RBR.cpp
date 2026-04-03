#include "RBR.hpp"
#include "Dx.hpp"
#include "Globals.hpp"
#include "UI.hpp"
#include "Util.hpp"

#include <numeric>
#include <ranges>

// Compilation unit global variables
namespace g {
    static uint32_t* camera_type_ptr;
    static uint32_t* car_id_ptr;
    static uint32_t* stage_id_ptr;

    static rbr::GameMode game_mode;
    static rbr::GameMode previous_game_mode;
    static uint32_t current_stage_id;
    static uint32_t current_camera_id;
    static bool is_rendering;
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

    uintptr_t get_address(uintptr_t target)
    {
        constexpr uintptr_t RBR_ABSOLUTE_LOAD_ADDR = 0x400000;
        return get_base_address() + target - RBR_ABSOLUTE_LOAD_ADDR;
    }

    uintptr_t get_hedgehog_address(uintptr_t target)
    {
        constexpr uintptr_t HEDGEHOG_ABSOLUTE_LOAD_ADDR = 0x10000000;
        return get_hedgehog_base_address() + target - HEDGEHOG_ABSOLUTE_LOAD_ADDR;
    }

    static uintptr_t WNDPROC_ADDR = get_address(0x447AB0);
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

    uintptr_t get_wndproc_addr()
    {
        return WNDPROC_ADDR;
    }

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

    bool is_rendering()
    {
        return g::is_rendering;
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

    static void write_bytes(uint8_t* address, uint8_t* data, int length)
    {
        DWORD oldProtect;

        // Change memory protection to allow writing
        if (VirtualProtect(address, length, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memcpy(address, data, length);
            // Restore the original protection
            VirtualProtect(address, length, oldProtect, &oldProtect);
        } else {
            std::cerr << "Failed to change memory protection." << std::endl;
        }
    }

    // Read camera FoV from the currently selected RBR camera
    // and recreate the projection matrix with the correct FoV
    float* update_current_camera_fov(uintptr_t p)
    {
        static bool fenceFixApplied = false;
        if(!fenceFixApplied)
        {
            // Apply (at most once) a patch that makes a point-in-frustum check that makes fences 
            // not disppear at high FoVs.
            uint8_t patched_bytes[] {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC2, 0x08, 0x00};
            write_bytes((uint8_t *)get_address(0x4bf9d0), patched_bytes, sizeof(patched_bytes));
            fenceFixApplied = true;
        }

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
            return original_fov_ptr;
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
            if (!g::cfg.cameras[i].has_value()) {
                continue;
            }

            const float sideScreensPhysicalFactor = 24.0 / 28.0;
            const float physicalFactor = i != RenderTarget::Primary ? sideScreensPhysicalFactor : 1.0;

            const auto znear = *z_near_ptr;
            const auto aspect = static_cast<float>(g::cfg.cameras[Primary]->w()) / static_cast<float>(g::cfg.cameras[Primary]->h());

            const float top = glm::tan(0.5f * fov) * znear * physicalFactor;
            const float bottom = -top;
            const float half_width = top * aspect;
            const float width = half_width * 2;
            float right = half_width;
            float left = -half_width;

            if (i == RenderTarget::Right) {
                left += static_cast<float>(g::cfg.cameras[i]->fov_adjustment) * width;
            }
            if (i == RenderTarget::Left) {
                right += static_cast<float>(g::cfg.cameras[i]->fov_adjustment) * width;
            }

            const auto yoffs = znear * (g::cfg.horizon_adjustment.value_or(0.0f) + static_cast<float>(g::cfg.cameras[i]->horizon_adjustment));
            g::projection_matrix[i] = glm::frustumLH_ZO(left, right, bottom + yoffs, top + yoffs, znear, 10000.0f);

            if (i != RenderTarget::Primary) {
                // 1/2 of HFoV of the primary plus 1/2 of the HFoV of this side screen adjusted by the physical factor
                g::calculated_screen_angle[i] = std::atan(std::tan(0.5f * fov) * aspect) + std::atan(std::tan(0.5f * fov) * aspect * physicalFactor);
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

        return original_fov_ptr;
    }

    static bool init_or_update_game_data(uintptr_t ptr)
    {
        static bool window_resized = false;
        if (!window_resized) [[unlikely]] {
            D3DPRESENT_PARAMETERS params;
            g::swapchain->GetPresentParameters(&params);
            const auto valid_cameras = g::cfg.cameras | std::views::filter([](const auto& cam) { return cam.has_value(); }) | std::ranges::to<std::vector>();
            const auto xmin = (*std::min_element(valid_cameras.cbegin(), valid_cameras.cend(), [](const auto& a, const auto& b) { return a->x() < b->x(); }))->x();
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
            const auto game_mode_ext_2 = *reinterpret_cast<uintptr_t*>(*GAME_MODE_EXT_2_PTR + 0x70);
            g::car_id_ptr = reinterpret_cast<uint32_t*>(game_mode_ext_2 + 0x1C);
            g::stage_id_ptr = reinterpret_cast<uint32_t*>(game_mode_ext_2 + 0x20);
        }

        if (g::previous_game_mode != g::game_mode && (g::game_mode == GameMode::PreStage || g::game_mode == GameMode::Pause)) {
            g::cfg.horizon_adjustment = std::nullopt;
        }

        bool camera_changed = false;
        if (g::current_camera_id != *g::camera_type_ptr) {
            g::current_camera_id = *g::camera_type_ptr;
            camera_changed = true;
        }

        if (g::current_stage_id != *g::stage_id_ptr) {
            g::current_stage_id = *g::stage_id_ptr;
            g::cfg.horizon_adjustment = std::nullopt;
        }

        auto should_draw = *reinterpret_cast<uint32_t*>(ptr + 0x720) == 0;
        if (should_draw && (g::game_mode == GameMode::MainMenu || g::game_mode == GameMode::Driving || g::game_mode == GameMode::Replay || g::game_mode == GameMode::Pause || g::game_mode == GameMode::PreStage)) {
            g::current_fov_ptr = update_current_camera_fov(ptr);
            if (g::current_fov_ptr && g::game_mode == GameMode::Driving) {
                ui::draw();
                ui::tick();
            }

            if (g::game_mode == GameMode::Driving && g::car_id_ptr && (!g::cfg.horizon_adjustment.has_value() || camera_changed)) {
                g::cfg.horizon_adjustment = Config::load_horizon_adjustment(*g::car_id_ptr, g::current_camera_id);
            }
        }

        return should_draw;
    }

    bool update_current_horizon_adjustment()
    {
        if (!g::car_id_ptr || !g::camera_type_ptr || !g::cfg.horizon_adjustment) {
            return false;
        }

        return Config::insert_or_update_horizon_adjustment(*g::car_id_ptr, *g::camera_type_ptr, g::cfg.horizon_adjustment.value());
    }

    // RBR 3D scene draw function is rerouted here
    void __fastcall render(void* p)
    {
        auto do_rendering = init_or_update_game_data(reinterpret_cast<uintptr_t>(p));

        if (!do_rendering) [[unlikely]] {
            return;
        }

        static RenderTarget render_target_to_skip = RenderTarget::Left;

        g::is_rendering = true;

        for (const auto& [i, c] : std::views::enumerate(g::cfg.cameras)) {
            if (!c.has_value()) {
                continue;
            }

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
        g::is_rendering = false;
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
