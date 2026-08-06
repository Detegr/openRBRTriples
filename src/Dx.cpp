#include "Dx.hpp"
#include "FlatTriples.hpp"
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
    WNDPROC wndproc;
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

    LRESULT CALLBACK wndproc(HWND hWindow, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        ui::wndproc(hWindow, uMsg, wParam, lParam);
        return g::wndproc(hWindow, uMsg, wParam, lParam);
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
        IDirect3DSurface9* back_buffer;
        if (g::swapchain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &back_buffer) != D3D_OK) {
            return 0;
        }

        D3DSURFACE_DESC bb_desc;
        ZeroMemory(&bb_desc, sizeof(bb_desc));
        back_buffer->GetDesc(&bb_desc);

        int min_x = INT_MAX;
        for (const auto& [ic, cc] : std::views::enumerate(g::cfg.cameras)) {
            if (cc.has_value()) {
                min_x = std::min(min_x, cc->x());
            }
        }

        for (const auto& [i, c] : std::views::enumerate(g::cfg.cameras)) {
            if (!c.has_value()) {
                continue;
            }

            IDirect3DSurface9* src_surface = std::get<0>(g::surfaces[i]);

            // Crop to physical screen pixel dimensions so that bezel-compensation
            // extra pixels (e.g. 1960 vs 1920) are hidden and don't cause overlap
            // between adjacent screens in the backbuffer.
            uint32_t phys_w, phys_h;
            if (g::surround_mode) {
                phys_w = c->w();
                phys_h = c->h();
            } else {
                phys_w = GetSystemMetrics(SM_CXSCREEN);
                phys_h = GetSystemMetrics(SM_CYSCREEN);
            }
            uint32_t extra_w = (static_cast<uint32_t>(c->w()) > phys_w)
                             ? (static_cast<uint32_t>(c->w()) - phys_w) / 2
                             : 0;

            RECT src = {
                static_cast<LONG>(std::max(c->crop.x, 0) + extra_w),
                static_cast<LONG>(std::max(c->crop.y, 0)),
                static_cast<LONG>(std::min(g::cfg.cameras[Primary]->w(), c->crop.x + static_cast<int>(phys_w)) + extra_w),
                static_cast<LONG>(std::min(g::cfg.cameras[Primary]->h(), c->crop.y + static_cast<int>(phys_h)))
            };

            RECT dst;
            if (g::surround_mode) {
                // NVIDIA Surround: single backbuffer spanning all monitors.
                // Split it into equal thirds in logical screen order (Left, Primary, Right).
                int n = 0;
                for (const auto& cam : g::cfg.cameras) if (cam.has_value()) n++;
                uint32_t per_mon_w = bb_desc.Width / static_cast<DWORD>(n > 0 ? n : 1);
                static const size_t screen_order[3] = { Left, Primary, Right };
                int si = -1;
                for (int j = 0; j < 3; ++j) {
                    if (screen_order[j] == i && j < n) { si = j; break; }
                }
                if (si < 0) continue;
                dst = { static_cast<LONG>(si * per_mon_w), c->y(),
                        static_cast<LONG>((si + 1) * per_mon_w), static_cast<LONG>(c->y() + c->h()) };
            } else {
                // Independent screens: place each RT at its configured position
                uint32_t dstx = c->x() - min_x;
                dst = { static_cast<LONG>(dstx), c->y(), static_cast<LONG>(dstx + c->w()), static_cast<LONG>(c->y() + c->h()) };
            }
            if (const auto ret = g::d3d_dev->StretchRect(src_surface, &src, back_buffer, &dst, D3DTEXF_NONE); ret != D3D_OK) {
                dbg(std::format("StretchRect #{} failed: {}", i, ret));
                if (rbr::get_game_mode() == GameMode::Starting) {
                    std::string screen_name;
                    switch (i) {
                        case Primary: screen_name = "middle"; break;
                        case Left: screen_name = "left"; break;
                        case Right: screen_name = "right"; break;
                        default: screen_name = "unknown"; break;
                    }
                    MessageBoxA(nullptr, std::format("Unable to draw the screens correctly. Please check the validity of openRBRTriples.toml\nThe issue occurred with {} screen", screen_name).c_str(), "Screen configuration error", MB_OK);
                    throw std::runtime_error("Screen configuration error");
                }
            }
        }

        ui::present(back_buffer);

        back_buffer->Release();

        auto ret = g::swapchain->Present(nullptr, nullptr, nullptr, nullptr, 0);

        return ret;
    }

    static M4 get_rotation_matrix()
    {
        if (rbr::is_rendering()) {
            if (rbr::get_game_mode() == GameMode::MainMenu) {
                // The main menu camera looks weird. This is an attempt to make it look like normal.
                return glm::translate(glm::mat4x4(1.0f), glm::vec3(0, -1.5f, 2.0f)) * glm::mat4_cast(glm::angleAxis(glm::radians(-20.0f), glm::vec3 { 1, 0, 0 }));
            }

            auto rt = g::current_render_target.value_or(RenderTarget::Primary);

            // Per-screen view rotation.
            //
            // The frustum from rebuild_flat_projection() is defined in a coordinate
            // frame rotated by theta (θ) from world space, where:
            //   θ < 0 for left screen (looking left), θ > 0 for right (looking right).
            // To transform world points into this frame we apply R(+θ) — NOT R(θ).
            //
            // Reason: a standard Y-rotation matrix R(φ) adds φ to the point's world
            // angle α as measured from +Z (i.e. atan2(x, z)):
            //   R(φ)·[r·sin α, 0, r·cos α]ᵀ = [r·sin(α+φ), 0, r·cos(α+φ)]ᵀ
            //   ⇒ α' = α + φ
            //
            // The frustum expects α' = α - θ (rel = world_angle - theta).
            // So we need R(+θ) since α + φ = α - θ ⇒ φ = -θ.
            //   Left:  θ = -SideAngle ⇒ φ = +SideAngle → R(+SideAngle)
            //   Right: θ = +SideAngle ⇒ φ = -SideAngle → R(-SideAngle)
            //
            // NOTE: the sign of φ is OPPOSITE to the intuitive "look left, rotate left".
            //       If you change this, verify with the math above, not intuition.
            float angle = 0.0f;
            if (rt == Left) {
                angle = glm::radians(g::cfg.SideAngle);
            } else if (rt == Right) {
                angle = -glm::radians(g::cfg.SideAngle);
            }

            // Legacy per-camera angle adjustment, applied on top of the physical angle
            angle += static_cast<float>(glm::radians(g::cfg.cameras[rt]->angle_adjustment)) * (rt == Right ? -1.0f : 1.0f);

            return glm::rotate(glm::identity<M4>(), angle, { 0, 1, 0 });
        }
        return glm::identity<M4>();
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
                const auto mvp = glm::transpose(g::projection_matrix[g::current_render_target.value_or(RenderTarget::Primary)] * get_rotation_matrix() * mv);
                return g::hooks::set_vertex_shader_constant_f.call(g::d3d_dev, StartRegister, glm::value_ptr(mvp), Vector4fCount);
            } else if (StartRegister == 20) {
                // Sky/fog
                const auto orig = glm::transpose(m4_from_shader_constant_ptr(pConstantData));
                const auto m = glm::transpose(get_rotation_matrix() * orig);
                return g::hooks::set_vertex_shader_constant_f.call(g::d3d_dev, StartRegister, glm::value_ptr(m), Vector4fCount);
            }
        }
        return g::hooks::set_vertex_shader_constant_f.call(g::d3d_dev, StartRegister, pConstantData, Vector4fCount);
    }

    HRESULT __stdcall SetTransform(IDirect3DDevice9* This, D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix)
    {
        if (rbr::is_rendering() && State == D3DTS_PROJECTION) {
            auto rt = g::current_render_target.value_or(RenderTarget::Primary);
            // Rebuild the per-camera projection from the physical monitor
            // geometry if it hasn't been built for this frame yet
            flattriples::rebuild_flat_projection(rt);
            shader::current_projection_matrix = m4_from_d3d(*pMatrix);
            // Cache the game projection's inverse — it rarely changes
            // per-camera per-frame, avoiding a glm::inverse every draw call
            static M4 last_orig_proj;
            static M4 last_orig_inv;
            if (memcmp(&last_orig_proj, pMatrix, sizeof(D3DMATRIX)) != 0) {
                last_orig_proj = m4_from_d3d(*pMatrix);
                last_orig_inv = glm::inverse(last_orig_proj);
            }
            shader::current_projection_matrix_inverse = last_orig_inv;
            fixedfunction::current_projection_matrix = d3d_from_m4(g::projection_matrix[rt]);
            return g::hooks::set_transform.call(g::d3d_dev, State, &fixedfunction::current_projection_matrix);
        } else if (rbr::is_rendering() && State == D3DTS_VIEW) {
            fixedfunction::current_view_matrix = d3d_from_m4(get_rotation_matrix() * m4_from_d3d(*pMatrix));
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
        try {
            g::cfg = g::saved_cfg = Config::from_path("Plugins", { 0, 0, w, h });
        } catch (const std::runtime_error& e) {
            dbg(e.what());
            MessageBoxA(hFocusWindow, e.what(), "Config error", MB_OK);
        }

        RECT rect = {};
        GetWindowRect(hFocusWindow, &rect);

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

        // Detect NVIDIA Surround: the game window is created spanning all
        // monitors, so its width exceeds a single screen's configured width.
        if (g::cfg.auto_detect_surround) {
            g::surround_mode = (w > static_cast<UINT>(g::cfg.cameras[Primary]->w()));
        }

        // Force the Surround window to the primary monitor to prevent
        // resolution mismatch on non-Surround displays.
        if (g::surround_mode) {
            HMONITOR hMon = MonitorFromWindow(g::main_window, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfo(hMon, &mi)) {
                SetWindowPos(g::main_window, nullptr,
                    mi.rcMonitor.left, mi.rcMonitor.top,
                    mi.rcMonitor.right - mi.rcMonitor.left,
                    mi.rcMonitor.bottom - mi.rcMonitor.top,
                    SWP_NOZORDER | SWP_FRAMECHANGED);
            }
        }

        g::surfaces.resize(g::cfg.cameras.size());
        int min_x = INT_MAX, max_x = INT_MIN;
        for (const auto& [i, c] : std::views::enumerate(g::cfg.cameras)) {
            if (!c.has_value()) {
                continue;
            }

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
                g::cfg.cameras[Primary]->w(),
                g::cfg.cameras[Primary]->h());

            min_x = std::min(min_x, c->x());
            max_x = std::max(max_x, c->x() + c->w());
        }
        // Span all configured screens; tolerates arbitrary x() offsets
        auto total_width = max_x - min_x;

        pPresentationParameters->hDeviceWindow = g::main_window;
        pPresentationParameters->BackBufferWidth = total_width;
        pPresentationParameters->BackBufferHeight = g::cfg.cameras[Primary]->h();

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

        g::wndproc = reinterpret_cast<WNDPROC>(rbr::get_wndproc_addr());
        (void)reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hFocusWindow, GWLP_WNDPROC, reinterpret_cast<uintptr_t>(wndproc)));
        ui::init(hFocusWindow, *ppReturnedDeviceInterface, pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight);

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
