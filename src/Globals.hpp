#pragma once

#include "Config.hpp"
#include "D3D.hpp"
#include "Hook.hpp"
#include "RBR.hpp"
#include "RenderTarget.hpp"

#include <optional>

// Forward declarations

class IPlugin;
class IRBRGame;
class Menu;
class VRInterface;

// Global variables
// A lot of the work done by the plugin is done in DirectX hooks
// There is no way to pass variables into the hooks, so the code is using a lot of globals
// None of these are thread safe and should not be treated as such

namespace g {
    // Pointer to the plugin
    extern IPlugin* openrbrtriples;

    // Pointer to IRBRGame object, from the RBR plugin system
    extern IRBRGame* game;

    // Window handle to the main window
    extern HWND main_window;

    // Currently selected menu, if any
    extern Menu* menu;

    // openRBRTriples config. Contains all local modifications
    extern Config cfg;

    // openRBRTriples config. has the information that's saved on the disk.
    extern Config saved_cfg;

    // Pointer to hooked D3D device. Used for everything graphics related.
    extern IDirect3DDevice9* d3d_dev;

    // Vector of pointers to RBR base game vertex shaders
    extern std::vector<IDirect3DVertexShader9*> base_game_shaders;

    // Mapping from car name to car textures
    extern std::unordered_map<std::string, IDirect3DTexture9*> car_textures;

    // Current render target, if any
    extern std::optional<RenderTarget> current_render_target;

    // Original RBR screen render target
    extern IDirect3DSurface9* original_render_target;

    // Original RBR screen depth/stencil target
    extern IDirect3DSurface9* original_depth_stencil_target;

    // Pointer to BTB track status information. Non-zero if a BTB stage is loaded.
    extern uint8_t* btb_track_status_ptr;

    // Custom projection matrix used by the plugin
    extern M4 projection_matrix;

    // Hooks to DirectX and RBR functions
    namespace hooks {
        // DirectX functions
        extern Hook<decltype(&Direct3DCreate9)> create;
        extern Hook<decltype(IDirect3D9Vtbl::CreateDevice)> create_device;
        extern Hook<decltype(IDirect3DDevice9Vtbl::SetVertexShaderConstantF)> set_vertex_shader_constant_f;
        extern Hook<decltype(IDirect3DDevice9Vtbl::SetTransform)> set_transform;
        extern Hook<decltype(IDirect3DDevice9Vtbl::Present)> present;
        extern Hook<decltype(IDirect3DDevice9Vtbl::CreateVertexShader)> create_vertex_shader;
        extern Hook<decltype(IDirect3DDevice9Vtbl::SetRenderTarget)> btb_set_render_target;
        extern Hook<decltype(IDirect3DDevice9Vtbl::DrawIndexedPrimitive)> draw_indexed_primitive;
        extern Hook<decltype(IDirect3DDevice9Vtbl::DrawPrimitive)> draw_primitive;

        // RBR functions
        extern Hook<decltype(&rbr::render)> render;
    }
}
