// Texture replacement.
//
// The game creates every 2D texture from a DDS file in memory with D3DXCreateTextureFromFileInMemory
// (d3dx9_30.dll, imported by Bionicle.exe): scene textures (the TST0 chunk of .nup files), standalone
// .dds files, the GUI. The mod hooks that export. Each texture gets a key, the FNV-1a 64 hash of its DDS
// header and the mip levels the header describes (not of the size passed in, which may include padding);
// if <folder>\<key>.dds exists, D3DX gets that file instead. Same pool and usage as the original
// (D3DPOOL_MANAGED). tools/build_pack.py computes the same key.
//
// The size is hidden too: the game keeps each texture's width and height (asked with
// IDirect3DTexture9::GetLevelDesc right after creating it, 0x57ba10 -> table 0x35bd260 + i*0x3c, +4/+8)
// and its GUI turns pixel coordinates into texture coordinates with them (0x54baf0 <- 0x4b6680), so a
// bigger texture would show only its top left part. A replaced texture carries its original size as
// private data, and the mod's GetLevelDesc reports that size; drawing still uses all HD pixels.

#include "hd_textures.h"

#include "hooks.h"
#include "log.h"

#include <windows.h>

#include <d3d9.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
using CreateTexture_t = HRESULT(WINAPI*)(void* device, const void* data, UINT size, void** texture);
CreateTexture_t o_CreateTexture = nullptr;

HdTexturesConfig g_cfg;
std::unordered_map<uint64_t, std::string> g_files;  // key -> replacement path
std::mutex g_mutex;                                  // dumps and the log of seen keys
std::unordered_set<uint64_t> g_seen;
std::atomic<unsigned> g_loaded{0}, g_replaced{0};

// Original size of a replaced texture, attached to it as private data.
struct OriginalSize
{
    UINT width, height;
};
// {6A1D3F52-8C4B-4E7A-9B1E-2F5C7D0A4B61}
const GUID kOriginalSizeGuid = {0x6a1d3f52, 0x8c4b, 0x4e7a, {0x9b, 0x1e, 0x2f, 0x5c, 0x7d, 0x0a, 0x4b, 0x61}};

using GetLevelDesc_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DTexture9* texture, UINT level, D3DSURFACE_DESC* desc);
GetLevelDesc_t o_GetLevelDesc = nullptr;

HRESULT STDMETHODCALLTYPE hk_GetLevelDesc(IDirect3DTexture9* texture, UINT level, D3DSURFACE_DESC* desc)
{
    const HRESULT hr = o_GetLevelDesc(texture, level, desc);
    OriginalSize size{};
    DWORD bytes = sizeof(size);
    if (SUCCEEDED(hr) && desc && texture &&
        SUCCEEDED(texture->GetPrivateData(kOriginalSizeGuid, &size, &bytes)) && bytes == sizeof(size))
    {
        desc->Width = (size.width >> level) ? (size.width >> level) : 1;
        desc->Height = (size.height >> level) ? (size.height >> level) : 1;
    }
    return hr;
}

// GetLevelDesc is shared by all textures of the D3D9 runtime: hooked once, through the first replacement.
void HookGetLevelDesc(IDirect3DTexture9* texture)
{
    static bool done = false;
    if (done)
        return;
    done = true;
    void** vtable = *reinterpret_cast<void***>(texture);
    const HookSpec hook = {"IDirect3DTexture9::GetLevelDesc", reinterpret_cast<uintptr_t>(vtable[17]),
                           reinterpret_cast<void*>(&hk_GetLevelDesc), reinterpret_cast<void**>(&o_GetLevelDesc)};
    Log("GetLevelDesc hook: %s", InstallHook(hook) ? "installed" : "FAILED");
}

void MarkReplaced(void* texturePtr, UINT width, UINT height)
{
    auto* texture = static_cast<IDirect3DTexture9*>(texturePtr);
    if (!texture)
        return;
    const OriginalSize size{width, height};
    texture->SetPrivateData(kOriginalSizeGuid, &size, sizeof(size), 0);
    HookGetLevelDesc(texture);
}

struct DdsInfo
{
    uint32_t width, height, mips;
    char format[8];
    size_t payload;  // header + described mip levels, at most the size passed in
};

bool ParseDds(const uint8_t* d, size_t size, DdsInfo& info)
{
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, d + o, 4); return v; };
    if (size < 128 || std::memcmp(d, "DDS ", 4) != 0 || u32(4) != 124)
        return false;
    info.height = u32(12);
    info.width = u32(16);
    info.mips = u32(28) ? u32(28) : 1;
    const bool fourcc = (u32(80) & 4) != 0;
    const uint32_t bits = u32(88);
    if (fourcc)
    {
        std::memcpy(info.format, d + 84, 4);
        info.format[4] = 0;
    }
    else
        std::snprintf(info.format, sizeof(info.format), "RGB%u", bits);
    const bool dxt = fourcc && std::memcmp(info.format, "DXT", 3) == 0;
    const bool dxt1 = dxt && info.format[3] == '1';
    uint64_t total = 128;
    uint32_t w = info.width, h = info.height;
    for (uint32_t i = 0; i < info.mips && i < 16; ++i)
    {
        if (dxt)
            total += uint64_t((w + 3) / 4 ? (w + 3) / 4 : 1) * ((h + 3) / 4 ? (h + 3) / 4 : 1) * (dxt1 ? 8 : 16);
        else
            total += uint64_t(w) * h * (bits >= 8 ? bits : 8) / 8;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    info.payload = total < size ? size_t(total) : size;
    return true;
}

uint64_t Fnv1a64(const uint8_t* d, size_t n)
{
    uint64_t x = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; ++i)
        x = (x ^ d[i]) * 0x100000001B3ull;
    return x;
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(size > 0 ? size_t(size) : 0);
    const bool ok = size > 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

void IndexFolder()
{
    WIN32_FIND_DATAA fd;
    const HANDLE h = FindFirstFileA((g_cfg.folder + "\\*.dds").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do
    {
        unsigned long long key = 0;
        char tail[8] = {};
        if (std::strlen(fd.cFileName) == 20 && std::sscanf(fd.cFileName, "%16llx%4s", &key, tail) == 2 &&
            _stricmp(tail, ".dds") == 0)
            g_files[key] = g_cfg.folder + "\\" + fd.cFileName;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

void Dump(uint64_t key, const uint8_t* data, size_t size)
{
    char path[MAX_PATH];
    std::snprintf(path, sizeof(path), "%s\\%016llx.dds", g_cfg.dumpFolder.c_str(), static_cast<unsigned long long>(key));
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
        return;
    if (FILE* f = std::fopen(path, "wb"))
    {
        std::fwrite(data, 1, size, f);
        std::fclose(f);
    }
}

HRESULT WINAPI hk_CreateTexture(void* device, const void* data, UINT size, void** texture)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    DdsInfo info{};
    if (!bytes || !ParseDds(bytes, size, info))
        return o_CreateTexture(device, data, size, texture);

    const uint64_t key = Fnv1a64(bytes, info.payload);
    const unsigned loaded = ++g_loaded;
    bool firstTime = false;
    if (g_cfg.logTextures || g_cfg.dump)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        firstTime = g_seen.insert(key).second;
        if (firstTime && g_cfg.dump)
            Dump(key, bytes, info.payload);
    }

    const auto it = g_files.find(key);
    if (it != g_files.end())
    {
        std::vector<uint8_t> hd;
        DdsInfo hdInfo{};
        if (ReadFile(it->second, hd) && ParseDds(hd.data(), hd.size(), hdInfo))
        {
            const HRESULT hr = o_CreateTexture(device, hd.data(), UINT(hd.size()), texture);
            if (SUCCEEDED(hr))
            {
                if (texture)
                    MarkReplaced(*texture, info.width, info.height);
                const unsigned replaced = ++g_replaced;
                if (g_cfg.logTextures && firstTime)
                    Log("%016llx %ux%u %s -> %ux%u %s (%u of %u replaced)", static_cast<unsigned long long>(key),
                        info.width, info.height, info.format, hdInfo.width, hdInfo.height, hdInfo.format, replaced, loaded);
                return hr;
            }
            Log("%016llx: D3DX refused the replacement (0x%08lX), original used", static_cast<unsigned long long>(key), hr);
        }
        else
            Log("%016llx: %s unreadable, original used", static_cast<unsigned long long>(key), it->second.c_str());
    }
    else if (g_cfg.logTextures && firstTime)
        Log("%016llx %ux%u %s original", static_cast<unsigned long long>(key), info.width, info.height, info.format);
    return o_CreateTexture(device, data, size, texture);
}
} // namespace

bool InstallHdTextures(const HdTexturesConfig& cfg)
{
    g_cfg = cfg;
    IndexFolder();
    if (g_cfg.dump)
        CreateDirectoryA(g_cfg.dumpFolder.c_str(), nullptr);

    HMODULE d3dx = GetModuleHandleA("d3dx9_30.dll");
    if (!d3dx)
        d3dx = LoadLibraryA("d3dx9_30.dll");
    void* target = d3dx ? reinterpret_cast<void*>(GetProcAddress(d3dx, "D3DXCreateTextureFromFileInMemory")) : nullptr;
    if (!target)
    {
        Log("d3dx9_30.dll / D3DXCreateTextureFromFileInMemory not found");
        return false;
    }
    const HookSpec hook = {"D3DXCreateTextureFromFileInMemory", reinterpret_cast<uintptr_t>(target),
                           reinterpret_cast<void*>(&hk_CreateTexture), reinterpret_cast<void**>(&o_CreateTexture)};
    const bool ok = HooksInit() && InstallHook(hook);
    Log("HD textures: %s, %zu replacement(s) in %s%s", ok ? "installed" : "FAILED", g_files.size(), g_cfg.folder.c_str(),
        g_cfg.dump ? ", dumping originals" : "");
    return ok;
}
