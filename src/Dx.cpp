#include "Dx.hpp"
#include "Globals.hpp"
#include "IPlugin.h"
#include "RBR.hpp"
#include "Util.hpp"
#include "Version.hpp"

#include <gtx/matrix_decompose.hpp>
#include <ranges>

// Compilation unit global variables
namespace g {
    static std::vector<std::tuple<IDirect3DSurface9*, IDirect3DSurface9*>> surfaces;
}

namespace dx {
    namespace shader {
        static M4 current_projection_matrix;
        static M4 current_projection_matrix_inverse;
    }

    namespace fixedfunction {
        static D3DMATRIX current_projection_matrix;
        static D3DMATRIX current_view_matrix;
    }

    using rbr::GameMode;

    LRESULT CALLBACK wnd_proc(HWND hWindow, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg) {
            case WM_CLOSE:
                DestroyWindow(hWindow);
                break;
            case WM_DESTROY:
                PostQuitMessage(0);
                break;
            default:
                return DefWindowProc(hWindow, uMsg, wParam, lParam);
        }
        return 0;
    }

    HRESULT __stdcall CreateVertexShader(IDirect3DDevice9* This, const DWORD* pFunction, IDirect3DVertexShader9** ppShader)
    {
        static int i = 0;
        auto ret = g::hooks::create_vertex_shader.call(g::d3d_dev, pFunction, ppShader);
        if (i < 40) {
            // These are the base game shaders for RBR that need
            // to be patched with the VR projection.
            g::base_game_shaders.push_back(*ppShader);
        }
        i++;
        return ret;
    }

    void set_render_target(RenderTarget tgt, bool clear)
    {
        const auto& surface = g::surfaces[tgt];
        IDirect3DSurface9* rt = std::get<0>(surface);
        IDirect3DSurface9* dt = std::get<1>(surface);

        if (rt && dt) {
            if (g::d3d_dev->SetRenderTarget(0, rt) != D3D_OK) {
                dbg("Failed to set render target");
            }
            if (g::d3d_dev->SetDepthStencilSurface(dt) != D3D_OK) {
                dbg("Failed to set depth surface");
            }
            if (clear && g::d3d_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0, 0) != D3D_OK) {
                dbg("Failed to clear surface");
            }
            g::current_render_target = tgt;
        }
    }

    HRESULT __stdcall Present(IDirect3DDevice9* This, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion)
    {
        if (g::d3d_dev->SetRenderTarget(0, g::original_render_target) != D3D_OK) {
            dbg("Failed to reset render target to original");
        }
        if (g::d3d_dev->SetDepthStencilSurface(g::original_depth_stencil_target) != D3D_OK) {
            dbg("Failed to reset depth stencil surface to original");
        }
        if (g::d3d_dev->Clear(0, nullptr, D3DCLEAR_STENCIL | D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0, 0) != D3D_OK) {
            dbg("Failed to clear surface");
        }

        IDirect3DSurface9* back_buffer;
        auto buf = g::swapchain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &back_buffer);
        for (const auto& [i, c] : std::views::enumerate(g::cfg.cameras)) {
            RECT src = { c.crop.x, c.crop.y, c.crop.x + c.w(), c.crop.y + c.h() };
            RECT dst = { c.extent[0], c.extent[1], c.extent[0] + c.w(), c.extent[1] + c.h() };
            g::d3d_dev->StretchRect(std::get<0>(g::surfaces[i]), &src, back_buffer, &dst, D3DTEXF_NONE);
        }
        back_buffer->Release();

        auto ret = g::swapchain->Present(nullptr, nullptr, nullptr, nullptr, 0);

        g::original_render_target->Release();
        g::original_depth_stencil_target->Release();

        return ret;
    }

    static M4 get_rotation_matrix()
    {
        float angle = 0.0;
        auto main_menu_camera_tweak = glm::identity<M4>();
        if (rbr::is_rendering_3d()) {
            if (rbr::get_game_mode() == GameMode::MainMenu) {
                // The main menu camera looks weird. This is an attempt to make it look like normal.
                main_menu_camera_tweak = glm::translate(glm::mat4x4(1.0f), glm::vec3(0, -1.5f, 2.0f)) * glm::mat4_cast(glm::angleAxis(glm::radians(-20.0f), glm::vec3 { 1, 0, 0 }));
            }
            angle = static_cast<float>(g::cfg.cameras[g::current_render_target.value()].angle);
            angle += static_cast<float>(glm::radians(g::cfg.cameras[g::current_render_target.value()].angle_adjustment));
            if (g::current_render_target.value() == RenderTarget::Right) {
                angle = -angle;
            }
        }
        return glm::rotate(glm::identity<M4>(), angle, { 0, 1, 0 }) * main_menu_camera_tweak;
    }

    static M4 get_translation_matrix()
    {
        if (rbr::is_rendering_3d()) {
            return glm::translate(glm::identity<M4>(), g::cfg.cameras[g::current_render_target.value()].translation);
        } else {
            return glm::identity<M4>();
        }
    }

    HRESULT __stdcall SetVertexShaderConstantF(IDirect3DDevice9* This, UINT StartRegister, const float* pConstantData, UINT Vector4fCount)
    {
        IDirect3DVertexShader9* shader;
        if (auto ret = g::d3d_dev->GetVertexShader(&shader); ret != D3D_OK) {
            dbg("Could not get vertex shader");
            return ret;
        }

        auto is_base_shader = true;
        if (rbr::is_on_btb_stage()) {
            is_base_shader = std::find(g::base_game_shaders.cbegin(), g::base_game_shaders.cend(), shader) != g::base_game_shaders.end();
        }
        if (shader)
            shader->Release();

        if (is_base_shader && Vector4fCount == 4) {
            if (StartRegister == 0) {
                const auto orig = glm::transpose(m4_from_shader_constant_ptr(pConstantData));
                const auto mv = shader::current_projection_matrix_inverse * orig;
                const auto mvp = glm::transpose(g::projection_matrix[g::current_render_target.value_or(RenderTarget::Primary)] * get_translation_matrix() * get_rotation_matrix() * mv);
                return g::hooks::set_vertex_shader_constant_f.call(g::d3d_dev, StartRegister, glm::value_ptr(mvp), Vector4fCount);
            } else if (StartRegister == 20) {
                // Sky/fog
                const auto orig = glm::transpose(m4_from_shader_constant_ptr(pConstantData));
                const auto m = glm::transpose(get_translation_matrix() * get_rotation_matrix() * orig);
                return g::hooks::set_vertex_shader_constant_f.call(g::d3d_dev, StartRegister, glm::value_ptr(m), Vector4fCount);
            }
        }
        return g::hooks::set_vertex_shader_constant_f.call(g::d3d_dev, StartRegister, pConstantData, Vector4fCount);
    }

    HRESULT __stdcall SetTransform(IDirect3DDevice9* This, D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix)
    {
        if (rbr::is_rendering_3d() && State == D3DTS_PROJECTION) {
            shader::current_projection_matrix = m4_from_d3d(*pMatrix);
            shader::current_projection_matrix_inverse = glm::inverse(shader::current_projection_matrix);
            fixedfunction::current_projection_matrix = d3d_from_m4(g::projection_matrix[g::current_render_target.value_or(RenderTarget::Primary)]);
            return g::hooks::set_transform.call(g::d3d_dev, State, &fixedfunction::current_projection_matrix);
        } else if (rbr::is_rendering_3d() && State == D3DTS_VIEW) {
            fixedfunction::current_view_matrix = d3d_from_m4(get_translation_matrix() * get_rotation_matrix() * m4_from_d3d(*pMatrix));
            return g::hooks::set_transform.call(g::d3d_dev, State, &fixedfunction::current_view_matrix);
        }

        return g::hooks::set_transform.call(g::d3d_dev, State, pMatrix);
    }

    HRESULT __stdcall BTB_SetRenderTarget(IDirect3DDevice9* This, DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget)
    {
        // This was found purely by luck after testing all kinds of things.
        // For some reason, if this call is called with the original This pointer (from RBRRX)
        // plugins switching the render target (i.e. RBRHUD) will cause the stage geometry
        // to not be rendered at all. Routing the call to the D3D device created by openRBRTriples,
        // it seems to work correctly.
        return g::d3d_dev->SetRenderTarget(RenderTargetIndex, pRenderTarget);
    }

    HRESULT __stdcall DrawPrimitive(IDirect3DDevice9* This, D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
    {
        if (rbr::is_on_btb_stage()) {
            IDirect3DVertexShader9* shader;
            g::d3d_dev->GetVertexShader(&shader);

            if (shader) {
                // Shader #39 causes strange "shadows" on BTB stages
                // Probably some projection matrix issue, but changing the projection matrix like
                // we do normally had no effect, so on BTB stages we just won't draw this primitive with this shader.
                auto should_skip_drawing = shader == g::base_game_shaders[39];
                shader->Release();
                if (should_skip_drawing) {
                    return 0;
                }
            }
        }
        return g::hooks::draw_primitive.call(This, PrimitiveType, StartVertex, PrimitiveCount);
    }

    HRESULT __stdcall CreateDevice(
        IDirect3D9* This,
        UINT Adapter,
        D3DDEVTYPE DeviceType,
        HWND hFocusWindow,
        DWORD BehaviorFlags,
        D3DPRESENT_PARAMETERS* pPresentationParameters,
        IDirect3DDevice9** ppReturnedDeviceInterface)
    {
        IDirect3DDevice9* dev = nullptr;

        auto ret = g::hooks::create_device.call(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, &dev);
        if (FAILED(ret)) {
            dbg("D3D initialization failed: CreateDevice");
            return ret;
        }
        *ppReturnedDeviceInterface = dev;

        const auto w = pPresentationParameters->BackBufferWidth;
        const auto h = pPresentationParameters->BackBufferHeight;
        g::cfg = g::saved_cfg = Config::from_path("Plugins", { 0, 0, w, h });

        auto windowClass = "window";
        HINSTANCE instance = GetModuleHandleA(nullptr);
        WNDCLASS wnd = {};
        wnd.lpfnWndProc = wnd_proc;
        wnd.hInstance = instance;
        wnd.lpszClassName = windowClass;
        RegisterClassA(&wnd);

        RECT rect = {};
        GetWindowRect(hFocusWindow, &rect);

        for (size_t i = 0; i < g::cfg.cameras.size(); ++i) {
            if (i == RenderTarget::Primary) {
                g::cfg.cameras[i].w() = w;
                g::cfg.cameras[i].h() = h;
                continue;
            }

            auto x = g::cfg.cameras[i].x();
            auto y = g::cfg.cameras[i].y();
            auto winw = g::cfg.cameras[i].w() == 0 ? g::cfg.cameras[0].w() : g::cfg.cameras[i].w();
            auto winh = g::cfg.cameras[i].h() == 0 ? g::cfg.cameras[0].h() : g::cfg.cameras[i].h();

            g::cfg.cameras[i].w() = winw;
            g::cfg.cameras[i].h() = winh;
        }

        auto devvtbl = get_vtable<IDirect3DDevice9Vtbl>(dev);
        try {
            g::hooks::set_vertex_shader_constant_f = Hook(devvtbl->SetVertexShaderConstantF, SetVertexShaderConstantF);
            g::hooks::set_transform = Hook(devvtbl->SetTransform, SetTransform);
            g::hooks::present = Hook(devvtbl->Present, Present);
            g::hooks::create_vertex_shader = Hook(devvtbl->CreateVertexShader, CreateVertexShader);
            g::hooks::draw_primitive = Hook(devvtbl->DrawPrimitive, DrawPrimitive);
        } catch (const std::runtime_error& e) {
            dbg(e.what());
            MessageBoxA(hFocusWindow, e.what(), "Hooking failed", MB_OK);
        }

        g::main_window = hFocusWindow;
        g::d3d_dev = dev;

        g::surfaces.resize(g::cfg.cameras.size());
        auto total_width = 0;
        for (const auto& [i, c] : std::views::enumerate(g::cfg.cameras)) {
            auto msaa = pPresentationParameters->MultiSampleType;
            if (g::cfg.aa_center_screen_only && i != RenderTarget::Primary) {
                msaa = D3DMULTISAMPLE_NONE;
            }

            // Make all render targets the size of the main window
            // If the side screens are smaller, the view will be cropped
            create_render_target(
                dev,
                &std::get<0>(g::surfaces[i]),
                &std::get<1>(g::surfaces[i]),
                pPresentationParameters->BackBufferFormat,
                pPresentationParameters->AutoDepthStencilFormat,
                msaa,
                g::cfg.cameras[0].w(),
                g::cfg.cameras[0].h());

            // Calculate total_width for creating a swapchain for a large (combined width) window
            total_width += g::cfg.cameras[i].w();
        }

        pPresentationParameters->hDeviceWindow = g::main_window;
        pPresentationParameters->BackBufferWidth = total_width;
        pPresentationParameters->BackBufferHeight = g::cfg.cameras[0].h();

        ret = dev->CreateAdditionalSwapChain(pPresentationParameters, &g::swapchain);
        if (FAILED(ret)) {
            dbg("D3D initialization failed: CreateAdditionalSwapChain");
            return ret;
        }

        // Initialize RBR pointers here, as it's too early to do this in the plugin constructor
        auto handle = GetModuleHandle("Plugins\\rbr_rx.dll");
        if (handle) {
            auto rx_addr = reinterpret_cast<uintptr_t>(handle);
            g::btb_track_status_ptr = reinterpret_cast<uint8_t*>(rx_addr + rbr_rx::TRACK_STATUS_OFFSET);

            IDirect3DDevice9Vtbl* rbrrxdev = reinterpret_cast<IDirect3DDevice9Vtbl*>(rx_addr + rbr_rx::DEVICE_VTABLE_OFFSET);
            try {
                g::hooks::btb_set_render_target = Hook(rbrrxdev->SetRenderTarget, BTB_SetRenderTarget);
            } catch (const std::runtime_error& e) {
                dbg(e.what());
                MessageBoxA(hFocusWindow, e.what(), "Hooking failed", MB_OK);
            }
        }

        return ret;
    }

    IDirect3D9* __stdcall Direct3DCreate9(UINT SDKVersion)
    {
        auto d3d = g::hooks::create.call(SDKVersion);
        if (!d3d) {
            dbg("Could not initialize D3D");
            return nullptr;
        }
        auto d3d_vtbl = get_vtable<IDirect3D9Vtbl>(d3d);
        try {
            g::hooks::create_device = Hook(d3d_vtbl->CreateDevice, CreateDevice);
        } catch (const std::runtime_error& e) {
            dbg(e.what());
            MessageBoxA(nullptr, e.what(), "Hooking failed", MB_OK);
        }
        return d3d;
    }
}
