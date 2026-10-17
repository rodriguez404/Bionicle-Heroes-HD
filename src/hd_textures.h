#pragma once

#include <string>

struct HdTexturesConfig
{
    std::string folder;      // replacement textures, <key>.dds
    std::string dumpFolder;  // originals are saved here when dump is on
    bool logTextures = false;
    bool dump = false;
};

// Replaces the game's textures with <folder>\<key>.dds where one exists.
bool InstallHdTextures(const HdTexturesConfig& cfg);
