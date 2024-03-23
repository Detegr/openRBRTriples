#include "RenderTarget.hpp"
#include "Globals.hpp"

bool create_render_target(
    IDirect3DDevice9* dev,
    IDirect3DSurface9** surface,
    IDirect3DSurface9** depth_stencil_surface,
    D3DFORMAT fmt,
    D3DMULTISAMPLE_TYPE msaa,
    uint32_t w,
    uint32_t h)
{
    HRESULT ret = dev->CreateRenderTarget(w, h, fmt, msaa, 0, false, surface, nullptr);

    static D3DFORMAT depth_stencil_format;
    if (depth_stencil_format == D3DFMT_UNKNOWN) {
        D3DFORMAT wantedFormats[] = {
            D3DFMT_D24S8,
            D3DFMT_D24X8,
            D3DFMT_D16,
        };
        IDirect3D9* d3d;
        if (dev->GetDirect3D(&d3d) != D3D_OK) {
            dbg("Could not get Direct3D adapter");
            depth_stencil_format = D3DFMT_D16;
        } else {
            for (const auto& format : wantedFormats) {
                if (d3d->CheckDepthStencilMatch(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, fmt, fmt, format) == D3D_OK) {
                    depth_stencil_format = format;
                    dbg(std::format("Using {} as depthstencil format", (int)format));
                    break;
                }
            }
            if (depth_stencil_format == D3DFMT_UNKNOWN) {
                dbg("No depth stencil format found?? Using D3DFMT_D16");
            }
        }
    }
    ret |= dev->CreateDepthStencilSurface(w, h, depth_stencil_format, msaa, 0, TRUE, depth_stencil_surface, nullptr);
    if (FAILED(ret)) {
        dbg("D3D initialization failed: CreateRenderTarget");
        return false;
    }
    return true;
}
