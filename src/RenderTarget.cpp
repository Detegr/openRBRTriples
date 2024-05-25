#include "RenderTarget.hpp"
#include "Globals.hpp"

bool create_render_target(
    IDirect3DDevice9* dev,
    IDirect3DSurface9** surface,
    IDirect3DSurface9** depth_stencil_surface,
    D3DFORMAT fmt,
    D3DFORMAT depth_stencil_fmt,
    D3DMULTISAMPLE_TYPE msaa,
    uint32_t w,
    uint32_t h)
{
    dbg(std::format("create_render_target: surface: {:x} depth_surface: {:x} fmt: {} depth_fmt: {} msaa: {} w: {} h: {}", (uintptr_t)surface, (uintptr_t)depth_stencil_surface, (int)fmt, (int)depth_stencil_fmt, (int)msaa, w, h));
    HRESULT ret = dev->CreateRenderTarget(w, h, fmt, msaa, 0, false, surface, nullptr);
    ret |= dev->CreateDepthStencilSurface(w, h, depth_stencil_fmt, msaa, 0, TRUE, depth_stencil_surface, nullptr);
    if (FAILED(ret)) {
        dbg("D3D initialization failed: CreateRenderTarget");
        return false;
    }
    return true;
}
