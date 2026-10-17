#include "hooks.h"

#include "log.h"

#include <MinHook.h>

bool HooksInit()
{
    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
    {
        Log("MinHook init failed (%d)", static_cast<int>(status));
        return false;
    }
    return true;
}

bool InstallHook(const HookSpec& h)
{
    if (!h.address)
    {
        Log("%s: address not found, not hooked", h.name);
        return false;
    }
    void* target = reinterpret_cast<void*>(h.address);
    if (MH_CreateHook(target, h.detour, h.original) != MH_OK || MH_EnableHook(target) != MH_OK)
    {
        Log("%s @%08X: MinHook failed", h.name, static_cast<unsigned>(h.address));
        return false;
    }
    return true;
}
