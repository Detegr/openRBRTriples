#include "RBR.hpp"
#include "Dx.hpp"
#include "Globals.hpp"
#include "Util.hpp"
#include "VR.hpp"

#include <ranges>

// Compilation unit global variables
namespace g {
    static uint32_t* camera_type_ptr;
    static uint32_t* stage_id_ptr;
    static M3* car_rotation_ptr;

    static rbr::GameMode game_mode;
    static rbr::GameMode previous_game_mode;
    static uint32_t current_stage_id;
    static M4 horizon_lock_matrix = glm::identity<M4>();
    static bool is_driving;
    static bool is_rendering_3d;
    static bool is_rendering_car;
    static bool is_rendering_particles;
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
    static uintptr_t RENDER_PARTICLES_FUNCTION_ADDR = get_address(0x5eff60); // Other possible hooking points are at 0x5efed0, 0x5effd0 and 0x5f0040
    static uintptr_t CAR_INFO_ADDR = get_address(0x165FC68);
    static uintptr_t* GAME_MODE_EXT_2_PTR = reinterpret_cast<uintptr_t*>(get_address(0x007EA678));
    static uintptr_t* CAR_ROTATION_STRUCT_PTR = reinterpret_cast<uintptr_t*>(get_address(0x7ea67c));

    using ChangeCameraFn = void(__thiscall*)(void* p, int cameraType, uint32_t a);
    using PrepareCameraFn = void(__thiscall*)(void* This, uint32_t a);
    static ChangeCameraFn change_camera_fn = reinterpret_cast<ChangeCameraFn>(get_address(0x487680));
    static PrepareCameraFn apply_camera_position = reinterpret_cast<PrepareCameraFn>(get_address(0x4825B0));
    static PrepareCameraFn apply_camera_fov = reinterpret_cast<PrepareCameraFn>(get_address(0x4BF690));

    uintptr_t get_render_function_addr()
    {
        return RENDER_FUNCTION_ADDR;
    }

    uintptr_t get_render_particles_function_addr()
    {
        return RENDER_PARTICLES_FUNCTION_ADDR;
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

    bool is_car_texture(IDirect3DBaseTexture9* tex)
    {
        for (const auto& entry : g::car_textures) {
            if (std::get<1>(entry) == tex) {
                return true;
            }
        }
        return false;
    }

    bool is_rendering_car()
    {
        return g::is_rendering_car;
    }

    bool is_rendering_particles()
    {
        return g::is_rendering_particles;
    }

    static bool init_or_update_game_data(uintptr_t ptr)
    {
        if (!g::hooks::render_car.call || !g::hooks::load_texture.call) [[unlikely]] {
            auto handle = reinterpret_cast<uintptr_t>(GetModuleHandle("HedgeHog3D.dll"));
            if (!handle) {
                dbg("Could not get handle for HedgeHog3D");
            } else {
                if (!g::hooks::load_texture.call) {
                    g::hooks::load_texture = Hook(*reinterpret_cast<decltype(load_texture)*>(handle + 0xAEC15), load_texture);
                }
                if (!g::hooks::render_car.call) {
                    g::hooks::render_car = Hook(*reinterpret_cast<decltype(render_car)*>(handle + 0x7BC60), render_car);
                }
            }
        }

        auto game_mode = *reinterpret_cast<GameMode*>(ptr + 0x728);
        if (game_mode != g::game_mode) [[unlikely]] {
            g::previous_game_mode = g::game_mode;
            g::game_mode = game_mode;
        }

        g::is_driving = g::game_mode == GameMode::Driving;
        g::is_rendering_3d = g::is_driving
            || (g::cfg.render_mainmenu_3d && g::game_mode == GameMode::MainMenu)
            || (g::cfg.render_pausemenu_3d && g::game_mode == GameMode::Pause && g::previous_game_mode != GameMode::Replay)
            || (g::cfg.render_pausemenu_3d && g::game_mode == GameMode::Pause && (g::previous_game_mode == GameMode::Replay && g::cfg.render_replays_3d))
            || (g::cfg.render_prestage_3d && g::game_mode == GameMode::PreStage)
            || (g::cfg.render_replays_3d && g::game_mode == GameMode::Replay);

        auto horizon_lock_game_mode = is_using_cockpit_camera() && (g::game_mode == Driving || g::game_mode == Replay);
        if (horizon_lock_game_mode && (g::cfg.lock_to_horizon != HorizonLock::LOCK_NONE) && !g::car_rotation_ptr) {
            g::car_rotation_ptr = reinterpret_cast<M3*>((*rbr::CAR_ROTATION_STRUCT_PTR + 0x16C));
        } else if (g::car_rotation_ptr && !horizon_lock_game_mode) {
            g::car_rotation_ptr = nullptr;
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

    M4 get_horizon_lock_matrix()
    {
        if (g::car_rotation_ptr) {
            // If car quaternion is given, calculate matrix for locking the horizon
            M3 car_rotation;
            auto q = glm::quat_cast(car_rotation);
            const auto multiplier = static_cast<float>(g::cfg.horizon_lock_multiplier);
            auto pitch = (g::cfg.lock_to_horizon & HorizonLock::LOCK_PITCH) ? glm::pitch(q) * multiplier : 0.0f;
            auto roll = (g::cfg.lock_to_horizon & HorizonLock::LOCK_ROLL) ? glm::yaw(q) * multiplier : 0.0f; // somehow in glm the axis is yaw

            // Flip the yaw if the car goes upside down (by pitch)
            auto yaw = 0.0f;
            if (pitch > 1.5708f) {
                yaw = 3.14159f;
            }
            if (pitch < -1.5708f) {
                yaw = 3.14159f;
            }

            glm::quat cancel_car_rotation = glm::normalize(glm::quat(glm::vec3(pitch, yaw, roll)));
            return glm::mat4_cast(cancel_car_rotation);
        } else {
            return glm::identity<M4>();
        }
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

        if (g::vr) [[likely]] {
            // UpdateVRPoses should be called as close to rendering as possible
            if (!g::vr->update_vr_poses()) {
                dbg("UpdateVRPoses failed, skipping frame");
                g::vr_error = true;
                return;
            }

            g::frame_start = std::chrono::steady_clock::now();

            if (g::is_rendering_3d) {
                dx::render_vr_eye(p, LeftEye);
                dx::render_vr_eye(p, RightEye);

                if (g::cfg.companion_mode == CompanionMode::Static && g::game_mode != PreStage && g::game_mode != Replay) {
                    auto orig_camera = *g::camera_type_ptr;
                    // 13 is the CFH camera. We don't want to change the camera while it is active.
                    if (orig_camera != 13) {
                        rbr::change_camera(p, 4);
                    }
                    if (g::d3d_dev->SetRenderTarget(0, g::original_render_target) != D3D_OK) {
                        dbg("Failed to reset render target to original");
                    }
                    if (g::d3d_dev->SetDepthStencilSurface(g::original_depth_stencil_target) != D3D_OK) {
                        dbg("Failed to reset depth stencil surface to original");
                    }
                    g::hooks::render.call(p);
                    if (orig_camera != 13) {
                        rbr::change_camera(p, orig_camera);
                    }
                }

                if (g::vr->prepare_vr_rendering(g::d3d_dev, Overlay)) {
                    g::current_2d_render_target = Overlay;
                } else {
                    dbg("Failed to set 2D render target");
                    g::current_2d_render_target = std::nullopt;
                    g::d3d_dev->SetRenderTarget(0, g::original_render_target);
                    g::d3d_dev->SetDepthStencilSurface(g::original_depth_stencil_target);
                }
            } else {
                // We are not driving, render the scene into a plane
                g::vr_render_target = std::nullopt;
                auto should_swap_render_target = !(is_loading_btb_stage() && !g::cfg.draw_loading_screen);
                if (should_swap_render_target) {
                    if (g::vr->prepare_vr_rendering(g::d3d_dev, GameMenu)) {
                        g::current_2d_render_target = GameMenu;
                    } else {
                        dbg("Failed to set 2D render target");
                        g::current_2d_render_target = std::nullopt;
                        g::d3d_dev->SetRenderTarget(0, g::original_render_target);
                        g::d3d_dev->SetDepthStencilSurface(g::original_depth_stencil_target);
                        return;
                    }
                }

                auto should_render = !(g::game_mode == GameMode::Loading && !g::cfg.draw_loading_screen);
                if (should_render) {
                    g::hooks::render.call(p);
                }
            }
        } else {
            g::hooks::render.call(p);
        }
    }

    uint32_t __stdcall load_texture(void* p, const char* tex_name, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g, uint32_t h, uint32_t i, uint32_t j, uint32_t k, IDirect3DTexture9** pp_texture)
    {
        auto ret = g::hooks::load_texture.call(p, tex_name, a, b, c, d, e, f, g, h, i, j, k, pp_texture);
        auto tex = std::string(tex_name)
            | std::ranges::views::transform([](char c) { return std::tolower(c); })
            | std::ranges::to<std::string>();

        if (ret == 0 && (tex.starts_with("cars\\") || tex.starts_with("cars/") || tex.starts_with("textures/ws_broken") || tex.starts_with("textures\\ws_broken"))) {
            g::car_textures[tex] = *pp_texture;
        }
        return ret;
    }

    void __stdcall render_car(void* a, void* b)
    {
        g::is_rendering_car = true;
        g::hooks::render_car.call(a, b);
        g::is_rendering_car = false;
    }

    void __fastcall render_particles(void* This)
    {
        g::is_rendering_particles = true;
        g::hooks::render_particles.call(This);
        g::is_rendering_particles = false;
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
