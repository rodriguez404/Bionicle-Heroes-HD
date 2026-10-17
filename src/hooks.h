#pragma once

#include <cstdint>

// A function hook on a game address found by signature (addresses.h).
struct HookSpec
{
    const char* name;
    uintptr_t address;
    void* detour;
    void** original;
};

bool HooksInit();                   // MH_Initialize, safe to call more than once
bool InstallHook(const HookSpec& h);
