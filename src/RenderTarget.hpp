#pragma once

#include "Util.hpp"

enum RenderTarget : size_t {
    Primary = 0,
    Left = 1,
    Right = 2,
};

bool create_render_target(
    IDirect3DDevice9* dev,
    IDirect3DSurface9** surface,
    IDirect3DSurface9** depth_stencil_surface,
    D3DFORMAT fmt,
    D3DMULTISAMPLE_TYPE msaa,
    uint32_t w,
    uint32_t h);
