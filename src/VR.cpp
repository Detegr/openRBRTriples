#include "VR.hpp"
#include "Config.hpp"
#include "Globals.hpp"
#include "OpenVR.hpp"
#include "OpenXR.hpp"

#include "D3D.hpp"
#include "Util.hpp"
#include "Vertex.hpp"

#include <format>
#include <vector>

// Compliation unit global variables
namespace g {
    static IDirect3DVertexBuffer9* companion_window_vertex_buf_menu;
    static IDirect3DVertexBuffer9* companion_window_vertex_buf_3d;
    static constexpr D3DMATRIX identity_matrix = d3d_from_m4(glm::identity<glm::mat4x4>());
    static IDirect3DVertexBuffer9* quad_vertex_buf[2];
    static IDirect3DVertexBuffer9* overlay_border_quad;
}

static bool create_menu_screen_companion_window_buffer(IDirect3DDevice9* dev)
{
    // clang-format off
    Vertex quad[] = {
        { -1, 1, 1, 0, 0, }, // Top-left
        { 1, 1, 1, 1, 0, }, // Top-right
        { -1, -1, 1, 0, 1 }, // Bottom-left
        { 1, -1, 1, 1, 1 } // Bottom-right
    };
    // clang-format on
    if (!create_vertex_buffer(dev, quad, 4, &g::companion_window_vertex_buf_menu)) {
        dbg("Could not create vertex buffer for companion window");
        return false;
    }
    return true;
}

bool VRInterface::create_companion_window_buffer(IDirect3DDevice9* dev)
{
    const auto size = g::cfg.companion_size / 100.0f;
    const auto x = g::cfg.companion_offset.x / 100.0f;
    const auto y = g::cfg.companion_offset.y / 100.0f;
    const auto aspect = static_cast<float>(aspect_ratio);

    // clang-format off
    Vertex quad[] = {
        { -1, 1, 1, x, y, }, // Top-left
        { 1, 1, 1, x+size, y, }, // Top-right
        { -1, -1, 1, x, y+size / aspect }, // Bottom-left
        { 1, -1, 1, x+size, y+size / aspect } // Bottom-right
    };
    // clang-format on
    if (g::companion_window_vertex_buf_3d) {
        g::companion_window_vertex_buf_3d->Release();
    }
    if (!create_vertex_buffer(dev, quad, 4, &g::companion_window_vertex_buf_3d)) {
        dbg("Could not create vertex buffer for companion window");
        return false;
    }
    return true;
}

bool create_quad(IDirect3DDevice9* dev, float size, float aspect, IDirect3DVertexBuffer9** dst)
{
    auto w = size;
    const auto h = w / aspect;
    constexpr auto left = 0.0f;
    constexpr auto rght = 1.0f;
    constexpr auto top = 0.0f;
    constexpr auto btm = 1.0f;
    constexpr auto z = 1.0f;
    // clang-format off
    Vertex quad[] = {
        { -w,  h, z, left, top, },
        {  w,  h, z, rght, top, },
        { -w, -h, z, left, btm  },
        {  w, -h, z, rght, btm  }
    };
    // clang-format on

    return create_vertex_buffer(dev, quad, 4, dst);
}

IDirect3DSurface9* VRInterface::prepare_vr_rendering(IDirect3DDevice9* dev, RenderTarget tgt, bool clear)
{
    if (is_using_texture_to_render(tgt)) {
        if (current_render_context->dx_texture[tgt]->GetSurfaceLevel(0, &current_render_context->dx_surface[tgt]) != D3D_OK) {
            dbg("PrepareVRRendering: Failed to get surface level");
            current_render_context->dx_surface[tgt] = nullptr;
            return nullptr;
        }
    }
    if (dev->SetRenderTarget(0, current_render_context->dx_surface[tgt]) != D3D_OK) {
        dbg("PrepareVRRendering: Failed to set render target");
        finish_vr_rendering(dev, tgt);
        return nullptr;
    }
    if (dev->SetDepthStencilSurface(current_render_context->dx_depth_stencil_surface[tgt]) != D3D_OK) {
        dbg("PrepareVRRendering: Failed to set depth stencil surface");
        finish_vr_rendering(dev, tgt);
        return nullptr;
    }
    if (clear) {
        if (dev->Clear(0, nullptr, D3DCLEAR_STENCIL | D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0, 0) != D3D_OK) {
            dbg("PrepareVRRendering: Failed to clear surface");
        }
    }
    return current_render_context->dx_surface[tgt];
}

void VRInterface::finish_vr_rendering(IDirect3DDevice9* dev, RenderTarget tgt)
{
    if (is_using_texture_to_render(tgt) && current_render_context->dx_surface[tgt]) {
        current_render_context->dx_surface[tgt]->Release();
        current_render_context->dx_surface[tgt] = nullptr;
    }
}

static bool create_render_target(IDirect3DDevice9* dev, RenderContext& ctx, RenderTarget tgt, D3DFORMAT fmt, uint32_t w, uint32_t h)
{
    return create_render_target(dev, ctx.dx_surface[tgt], ctx.dx_depth_stencil_surface[tgt], ctx.dx_texture[tgt], tgt, fmt, w, h);
}

void VRInterface::init_surfaces(IDirect3DDevice9* dev, RenderContext& ctx, uint32_t res_x_2d, uint32_t res_y_2d)
{
    if (!create_render_target(dev, ctx, LeftEye, D3DFMT_X8B8G8R8, ctx.width[0], ctx.height[0]))
        throw std::runtime_error("Could not create texture for left eye");
    if (!create_render_target(dev, ctx, RightEye, D3DFMT_X8B8G8R8, ctx.width[1], ctx.height[1]))
        throw std::runtime_error("Could not create texture for right eye");
    if (!create_render_target(dev, ctx, GameMenu, D3DFMT_X8B8G8R8, res_x_2d, res_y_2d))
        throw std::runtime_error("Could not create texture for menus");
    if (!create_render_target(dev, ctx, Overlay, D3DFMT_A8B8G8R8, res_x_2d, res_y_2d))
        throw std::runtime_error("Could not create texture for overlay");
    if (dev->CreateTexture(res_x_2d, res_y_2d, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8B8G8R8, D3DPOOL_DEFAULT, &ctx.overlay_border, nullptr) != D3D_OK)
        throw std::runtime_error("Could not create overlay border texture");

    auto aspect_ratio = static_cast<float>(static_cast<double>(res_x_2d) / static_cast<double>(res_y_2d));
    static bool quads_created = false;
    if (!quads_created) {
        this->aspect_ratio = aspect_ratio;

        // Create and fill a vertex buffers for the 2D planes
        // We can reuse all of these in every rendering context
        if (!create_quad(dev, 0.6f, aspect_ratio, &g::quad_vertex_buf[0]))
            throw std::runtime_error("Could not create menu quad");
        if (!create_quad(dev, 0.6f, aspect_ratio, &g::quad_vertex_buf[1]))
            throw std::runtime_error("Could not create overlay quad");
        if (!create_quad(dev, 1.0f, 1.0f, &g::overlay_border_quad))
            throw std::runtime_error("Could not create overlay border quad");
        if (!create_companion_window_buffer(dev))
            throw std::runtime_error("Could not create desktop window buffer");
        if (!create_menu_screen_companion_window_buffer(dev))
            throw std::runtime_error("Could not create menu screen desktop window buffer");

        quads_created = true;
    }

    // Render overlay border to a texture for later use
    IDirect3DSurface9* adj;
    if (ctx.overlay_border->GetSurfaceLevel(0, &adj) == D3D_OK) {
        IDirect3DSurface9* orig;
        dev->GetRenderTarget(0, &orig);
        dev->SetRenderTarget(0, adj);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_RGBA(255, 69, 0, 50), 1.0, 0);
        constexpr auto borderSize = 0.02;
        const D3DRECT center = {
            static_cast<LONG>(borderSize / aspect_ratio * res_x_2d),
            static_cast<LONG>(borderSize * res_y_2d),
            static_cast<LONG>((1.0 - borderSize / aspect_ratio) * res_x_2d),
            static_cast<LONG>((1.0 - borderSize) * res_y_2d)
        };
        dev->Clear(1, &center, D3DCLEAR_TARGET, D3DCOLOR_RGBA(0, 0, 0, 0), 1.0, 0);
        adj->Release();
        dev->SetRenderTarget(0, orig);
        orig->Release();
    }
}

static void render_texture(
    IDirect3DDevice9* dev,
    const D3DMATRIX* proj,
    const D3DMATRIX* view,
    const D3DMATRIX* world,
    IDirect3DTexture9* tex,
    IDirect3DVertexBuffer9* vbuf)
{
    IDirect3DVertexShader9* vs;
    IDirect3DPixelShader9* ps;
    D3DMATRIX origProj, origView, origWorld;

    dev->GetVertexShader(&vs);
    dev->GetPixelShader(&ps);

    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);

    dev->GetTransform(D3DTS_PROJECTION, &origProj);
    dev->GetTransform(D3DTS_VIEW, &origView);
    dev->GetTransform(D3DTS_WORLD, &origWorld);

    dev->SetTransform(D3DTS_PROJECTION, proj);
    dev->SetTransform(D3DTS_VIEW, view);
    dev->SetTransform(D3DTS_WORLD, world);

    dev->BeginScene();

    IDirect3DBaseTexture9* origTex;
    dev->GetTexture(0, &origTex);

    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);

    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

    dev->SetRenderState(D3DRS_ZENABLE, false);

    dev->SetTexture(0, tex);

    dev->SetStreamSource(0, vbuf, 0, sizeof(Vertex));
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
    dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);

    dev->EndScene();

    dev->SetRenderState(D3DRS_ZENABLE, true);
    dev->SetTexture(0, origTex);
    dev->SetVertexShader(vs);
    dev->SetPixelShader(ps);
    dev->SetTransform(D3DTS_PROJECTION, &origProj);
    dev->SetTransform(D3DTS_VIEW, &origView);
    dev->SetTransform(D3DTS_WORLD, &origWorld);
}

void render_overlay_border(IDirect3DDevice9* dev, IDirect3DTexture9* tex)
{
    render_texture(dev, &g::identity_matrix, &g::identity_matrix, &g::identity_matrix, tex, g::overlay_border_quad);
}

void render_menu_quad(IDirect3DDevice9* dev, VRInterface* vr, IDirect3DTexture9* texture, RenderTarget renderTarget3D, RenderTarget render_target_2d, Projection projection_type, float size, glm::vec3 translation, const std::optional<M4>& horizon_lock)
{
    const auto& projection = vr->get_projection(renderTarget3D, projection_type);
    const auto& eyepos = vr->get_eye_pos(renderTarget3D);
    const auto& pose = vr->get_pose(renderTarget3D);

    const D3DMATRIX mvp = d3d_from_m4(projection * glm::translate(glm::scale(eyepos * pose * g::flip_z_matrix * horizon_lock.value_or(glm::identity<M4>()), { size, size, 1.0f }), translation));
    render_texture(dev, &mvp, &g::identity_matrix, &g::identity_matrix, texture, g::quad_vertex_buf[render_target_2d == GameMenu ? 0 : 1]);
}

void render_companion_window_from_render_target(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget tgt)
{
    render_texture(dev, &g::identity_matrix, &g::identity_matrix, &g::identity_matrix, vr->get_texture(tgt), (tgt == GameMenu || tgt == Overlay) ? g::companion_window_vertex_buf_menu : g::companion_window_vertex_buf_3d);
}