#include "FlatTriples.hpp"

#include "Globals.hpp"
#include "RenderTarget.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace flattriples {

// Per-screen rotated view frustum.
// Each screen has its own view direction (pointing toward the screen center)
// and a nearly-symmetric frustum in that rotated frame.
// This avoids the extreme tan(θ) distortion of pure off-axis projection.
void rebuild_flat_projection(size_t rt)
{
    if (rt >= 3) return;
    if (!g::cfg.cameras[rt].has_value()) return;
    if (g::flat_znear <= 0.0f) return;

    // Skip if already computed this frame
    if (!g::projection_dirty[rt]) return;
    g::projection_dirty[rt] = false;

    float E = g::cfg.EyeDistance; // mm
    float W = g::cfg.MonitorWidth; // mm
    float cam_ref_w = static_cast<float>(g::cfg.cameras[rt]->w());
    float sm_cx = g::surround_mode
        ? cam_ref_w
        : static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
    float H = W * static_cast<float>(GetSystemMetrics(SM_CYSCREEN)) / sm_cx;

    // View rotation angle. Center=0, left=-SideAngle, right=+SideAngle.
    float theta = 0.0f;
    if (rt == Left) {
        theta = -glm::radians(g::cfg.SideAngle);
    } else if (rt == Right) {
        theta = glm::radians(g::cfg.SideAngle);
    }

    float st = std::sin(theta);
    float ct = std::cos(theta);

    // Screen edge positions in world space (hinged model)
    float x_left_w, x_right_w, z_left_w, z_right_w;
    if (rt == Primary) {
        x_left_w  = -W / 2.0f; z_left_w  = E;
        x_right_w =  W / 2.0f; z_right_w = E;
    } else if (rt == Left) {
        x_right_w = -W / 2.0f; z_right_w = E;
        x_left_w  = -W / 2.0f - W * ct; z_left_w  = E + W * st;
    } else {
        x_left_w  =  W / 2.0f; z_left_w  = E;
        x_right_w =  W / 2.0f + W * ct; z_right_w = E - W * st;
    }

    // Direction angles of screen edges in world space
    float angle_left  = std::atan2(x_left_w,  z_left_w);
    float angle_right = std::atan2(x_right_w, z_right_w);

    // Angles relative to the rotated view direction
    float rel_left  = angle_left  - theta;
    float rel_right = angle_right - theta;

    // Nearly-symmetric frustum in the rotated view frame.
    //
    // glm::frustumLH_ZO(l,r,b,t,n,f) maps NDC boundaries to x/z like this:
    //   NDC_x = -1  →  x/z = -r/n   (NOT l/n)
    //   NDC_x = +1  →  x/z = -l/n   (NOT r/n)
    //
    // So to place the inner edge (rel_left) at NDC = inner_edge_side
    // and the outer edge (rel_right) at the opposite NDC edge:
    //   inner edge → x/z = tan(rel_left)
    //   outer edge → x/z = tan(rel_right)
    //   l = -tan(rel_right) * znear  (pins outer edge at NDC = -1 for left screen, +1 for right)
    //   r = -tan(rel_left)  * znear  (pins inner edge at NDC = +1 for left screen, -1 for right)
    float l = -std::tan(rel_right) * g::flat_znear;
    float r = -std::tan(rel_left)  * g::flat_znear;

    // Scale horizontal frustum to match the pixel width of the render target.
    // When bezel compensation widens the per-camera pixel width (cam_w) beyond
    // the physical screen pixel width, the frustum must cover a wider horizontal
    // world-span so that the rendered image maps 1:1 to the additional pixels,
    // avoiding horizontal squish / vertical stretch.
    {
        float phys_w = g::surround_mode
            ? static_cast<float>(g::cfg.cameras[rt]->w())
            : static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
        float ratio = static_cast<float>(g::cfg.cameras[rt]->w()) / phys_w;
        l *= ratio;
        r *= ratio;
    }

    // Vertical: perpendicular distance from eye to screen plane.
    // Center screen is at z=E, so d_perp = E.
    // Side screens are rotated by θ, so d_perp = W_eff/2·sin|θ| + E·cos|θ|.
    // W_eff accounts for bezel correction: the image shift effectively widens
    // the visible span, moving the connection point and changing VFOV.
    // A gap of bezel_center + bezel_side between screens widens total FOV;
    // side screens' d_perp is adjusted via W_eff = W + bezel_center + bezel_side.
    float d_perp = E;
    if (rt != Primary) {
        float sr = glm::radians(g::cfg.SideAngle);
        float bx_side = static_cast<float>(g::cfg.cameras[rt]->bezel_x);
        float bx_center = static_cast<float>(g::cfg.cameras[Primary]->bezel_x);
        float w_eff = W + std::abs(bx_side) + std::abs(bx_center);
        d_perp = w_eff / 2.0f * std::sin(sr) + E * std::cos(sr);
    }
    float b = -(H / 2.0f) / d_perp * g::flat_znear;
    float t =  (H / 2.0f) / d_perp * g::flat_znear;

    float fw = r - l;
    float fh = t - b;

    // Bezel correction: shift in screen plane (mm → frustum units)
    float bx = static_cast<float>(g::cfg.cameras[rt]->bezel_x);
    float by = static_cast<float>(g::cfg.cameras[rt]->bezel_y);
    if (bx != 0.0f || by != 0.0f) {
        float dl = bx / W * fw;
        float db = by / H * fh;
        l += dl; r += dl;
        b += db; t += db;
    }

    // Pitch adjustment (overall horizon)
    float yoffs = g::flat_znear * (g::cfg.horizon_adjustment.value_or(0.0f) + static_cast<float>(g::cfg.cameras[rt]->horizon_adjustment));
    b += yoffs; t += yoffs;

    g::projection_matrix[rt] = glm::frustumLH_ZO(l, r, b, t, g::flat_znear, 10000.0f);
}

}
