// Bionicle Heroes HD: loaded by Ultimate ASI Loader (dinput8.dll) as BionicleHeroesHD.asi.

#include <windows.h>

#include <string>

#include "hd_textures.h"
#include "log.h"

namespace
{
HMODULE g_module = nullptr;

// This .asi's path without the extension.
std::string BasePath()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(g_module, path, MAX_PATH);
    std::string result(path);
    const size_t dot = result.find_last_of('.');
    if (dot != std::string::npos)
        result.resize(dot);
    return result;
}

std::string Folder(const std::string& path)
{
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

void Init()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    const std::string base = BasePath();
    LogOpen((base + ".log").c_str());
    const std::string ini = base + ".ini";
    const std::string dir = Folder(base);

    if (GetPrivateProfileIntA("Textures", "Enabled", 1, ini.c_str()) == 0)
    {
        Log("HD textures: off in %s", ini.c_str());
        return;
    }
    char folder[MAX_PATH] = {};
    GetPrivateProfileStringA("Textures", "Folder", "BionicleHeroesHD\\textures", folder, sizeof(folder), ini.c_str());
    HdTexturesConfig cfg;
    cfg.folder = (folder[0] && folder[1] == ':') ? folder : dir + "\\" + folder;
    cfg.dumpFolder = dir + "\\BionicleHeroesHD\\dump";
    cfg.logTextures = GetPrivateProfileIntA("Debug", "LogTextures", 0, ini.c_str()) != 0;
    cfg.dump = GetPrivateProfileIntA("Debug", "DumpTextures", 0, ini.c_str()) != 0;
    InstallHdTextures(cfg);
}
} // namespace

extern "C" __declspec(dllexport) void InitializeASI()
{
    Init();
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
