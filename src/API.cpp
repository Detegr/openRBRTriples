#pragma once

#include "Config.hpp"
#include "Globals.hpp"
#include "IPlugin.h"
#include "openRBRTriples.hpp"
#include <MinHook.h>

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    MH_Initialize();
    return TRUE;
}

extern "C" __declspec(dllexport) IPlugin* RBR_CreatePlugin(IRBRGame* game)
{
    if (!g::openrbrtriples) {
        g::openrbrtriples = new openRBRTriples(game);
    }
    return g::openrbrtriples;
}

enum ApiOperations : uint64_t {
    API_VERSION = 0x0,
    NOTIFY_VERTEX_SHADER = 0x1,
};

extern "C" __declspec(dllexport) int64_t openRBRTriples_Exec(ApiOperations ops, uint64_t value)
{
    dbg(std::format("Exec: {} {}", (uint64_t)ops, value));

    if (ops == API_VERSION) {
        return 2;
    }

    if (ops == NOTIFY_VERTEX_SHADER) {
        const auto shader = reinterpret_cast<IDirect3DVertexShader9*>(value);
        if (std::find(g::base_game_shaders.cbegin(), g::base_game_shaders.cend(), shader) == g::base_game_shaders.cend()) {
            g::base_game_shaders.push_back(shader);
        }
        return 0;
    }

    return 0;
}
