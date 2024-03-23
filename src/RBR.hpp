#pragma once

#include "Util.hpp"

#include <cstdint>
#include <d3d9.h>

namespace rbr {
    enum GameMode : uint32_t {
        Driving = 1,
        Pause = 2,
        MainMenu = 3,
        Blackout = 4,
        Loading = 5,
        Exiting = 6,
        Quit = 7,
        Replay = 8,
        End = 9,
        PreStage = 10,
        Starting = 12,
    };

    uintptr_t get_render_function_addr();
    GameMode get_game_mode();
    bool is_on_btb_stage();
    bool is_loading_btb_stage();
    bool is_rendering_3d();
    bool is_using_cockpit_camera();
    uint32_t get_current_stage_id();

    void update_current_camera_fov(uintptr_t p);
    void change_camera(void* p, uint32_t cameraType);

    // Hookable functions
    void __fastcall render(void* p);
}

namespace rbr_rx {
    constexpr uintptr_t DEVICE_VTABLE_OFFSET = 0x4f56c;
    constexpr uintptr_t TRACK_STATUS_OFFSET = 0x608d0;
    bool is_loaded();
}

namespace rbrhud {
    bool is_loaded();
}
