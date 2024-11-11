#pragma once

#include <d3d9.h>
#include <imgui.h>

namespace ui {
    void init(HWND hWindow, IDirect3DDevice9* dev, int width, int height);
    void tick();
    void draw();
    void present(IDirect3DSurface9* surface);
    void capture_input();
    void stop_input_capture();
    void wndproc(HWND hWindow, UINT uMsg, WPARAM wParam, LPARAM lParam);
}
