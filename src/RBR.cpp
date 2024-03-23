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
    static ChangeCameraFn change_camera_fn = reinterpret_cast<ChangeCameraFn>(get_address(0x487680));
    static PrepareCameraFn apply_camera_position = reinterpret_cast<PrepareCameraFn>(get_address(0x4825B0));
    static PrepareCameraFn apply_camera_fov = reinterpret_cast<PrepareCameraFn>(get_address(0x4BF690));

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
            uintptr_t cameraData = *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(CAR_INFO_ADDR) + 0x758);
            uintptr_t cameraInfo = *reinterpret_cast<uintptr_t*>(cameraData + 0x10);
            g::camera_type_ptr = reinterpret_cast<uint32_t*>(cameraInfo);
        }

        if (!g::stage_id_ptr) [[unlikely]] {
            g::stage_id_ptr = reinterpret_cast<uint32_t*>(*reinterpret_cast<uintptr_t*>(*GAME_MODE_EXT_2_PTR + 0x70) + 0x20);
        }

        return *reinterpret_cast<uint32_t*>(ptr + 0x720) == 0;
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
        dx::set_render_target(RenderTarget::Primary);

        static bool flipflop;
        int i = 0;
        for (const auto& c : g::cfg.cameras) {
            if (!flipflop && i == RenderTarget::Left) {
                i++;
                continue;
            }
            if (flipflop && i == RenderTarget::Right) {
                i++;
                continue;
            }
            dx::set_render_target(static_cast<RenderTarget>(i));
            g::hooks::render.call(p);
            i++;
        };

        flipflop = !flipflop;
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
