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

    static uintptr_t get_address(uintptr_t target)
    {
        constexpr uintptr_t RBR_ABSOLUTE_LOAD_ADDR = 0x400000;
        return get_base_address() + target - RBR_ABSOLUTE_LOAD_ADDR;
    }

    static uintptr_t RENDER_FUNCTION_ADDR = get_address(0x47E1E0);
    static uintptr_t CAR_INFO_ADDR = get_address(0x165FC68);
    static uintptr_t* GAME_MODE_EXT_2_PTR = reinterpret_cast<uintptr_t*>(get_address(0x007EA678));

    using ChangeCameraFn = void(__thiscall*)(void* p, int cameraType, uint32_t a);
    using PrepareCameraFn = void(__thiscall*)(void* This, uint32_t a);
    using PostPrepareCameraFn = void(__thiscall*)(void* This, uint32_t a);
    static ChangeCameraFn change_camera_fn = reinterpret_cast<ChangeCameraFn>(get_address(0x487680));
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
        post_prepare_camera(camera_post_prepare_this, 0);
        apply_camera_fov(camera_fov_this, 0);
        *current_fov_ptr = original_fov_ptr_value;
    }

    static bool init_or_update_game_data(uintptr_t ptr)
    {
        static bool window_position_set = false;
        if (!window_position_set) [[unlikely]] {
            SetWindowPos(g::main_window, nullptr, g::cfg.cameras[0].extent[0], g::cfg.cameras[0].extent[1], 0, 0, SWP_NOSIZE);
        }

        auto game_mode = *reinterpret_cast<GameMode*>(ptr + 0x728);
        if (game_mode != g::game_mode) [[unlikely]] {
            g::previous_game_mode = g::game_mode;
            g::game_mode = game_mode;
        }

        if (g::game_mode == GameMode::MainMenu && !g::car_textures.empty()) [[unlikely]] {
            // Clear saved car textures if we're in the menu
            // Not sure if this is needed, but better be safe than sorry,
            // the car textures will be reloaded when loading the stage.
            g::car_textures.clear();
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

    void change_camera(void* p, uint32_t cameraType)
    {
        const auto camera_data = *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(CAR_INFO_ADDR) + 0x758);
        const auto camera_info = reinterpret_cast<void*>(*reinterpret_cast<uintptr_t*>(camera_data + 0x10));
        const auto camera_fov_this = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(p) + 0xcf4);

        change_camera_fn(camera_info, cameraType, 1);
        apply_camera_position(p, 0);
        apply_camera_fov(camera_fov_this, 0);
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
