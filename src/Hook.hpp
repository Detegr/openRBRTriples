#pragma once

#include <MinHook.h>
#include <stdexcept>

// RAII wrapper for MinHook
template <typename T>
struct Hook {
    T call;
    T src;

    explicit Hook()
        : call(nullptr)
        , src(nullptr)
    {
    }

    explicit Hook(T src, T tgt)
        : src(src)
    {
        if (const auto ret = MH_CreateHook(reinterpret_cast<void*>(src), reinterpret_cast<void*>(tgt), reinterpret_cast<void**>(&call)); ret != MH_OK) {
            throw std::runtime_error(std::format("Could not create a function hook, error {}", (int)ret));
        }
        enable();
    }
    void enable()
    {
        if (MH_EnableHook(src) != MH_OK) {
            throw std::runtime_error("Could not disable hook");
        }
    }
    void disable()
    {
        if (MH_DisableHook(src) != MH_OK) {
            throw std::runtime_error("Could not disable hook");
        }
    }
    Hook(const Hook&) = delete;
    Hook(Hook&& rhs)
    {
        call = rhs.call;
        src = rhs.src;
        rhs.call = nullptr;
        rhs.src = nullptr;
        return *this;
    }
    Hook& operator=(const Hook&) = delete;
    Hook& operator=(Hook&& rhs) noexcept
    {
        call = rhs.call;
        src = rhs.src;
        rhs.call = nullptr;
        rhs.src = nullptr;
        return *this;
    }
    ~Hook()
    {
        if (src)
            MH_DisableHook(src);
    }
};

template <typename T, typename F>
T* get_vtable(F* obj)
{
    return (T*)(*(uintptr_t*)obj);
}
