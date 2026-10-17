# Bionicle Heroes HD

HD texture mod for **Bionicle Heroes** (PC, 2006), loaded as an `.asi` plugin. The game's textures are
upscaled with [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN): levels, characters, weapons, the
HUD and menus. Fonts and loading screens are left as they are.

## Downloads

Two packs, same textures, different resolution:

- **x4**: 4x the width and height (16x the pixels), ~1.8 GB installed.
- **x2**: 2x the width and height (4x the pixels), ~450 MB installed.

Extract the archive into the game folder (next to `Bionicle.exe`). That is all: it includes
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (`dinput8.dll`), which loads
the mod. Works together with Better Bionicle Heroes,
[BionicleHeroesGFX](https://github.com/ermaccer/BionicleHeroesGFX) and other `.asi` mods (same loader:
let either `dinput8.dll` overwrite the other).

To uninstall, delete `BionicleHeroesHD.asi`, `BionicleHeroesHD.ini`, `BionicleHeroesHD-README.md`, the
`BionicleHeroesHD` and `BionicleHeroesHD-licenses` folders (keep `dinput8.dll` if another mod needs it).

## How it works

- The game creates every texture from a DDS file in memory with `D3DXCreateTextureFromFileInMemory`
  (`d3dx9_30.dll`). The mod hooks it; when `BionicleHeroesHD\textures\<key>.dds` exists for a texture,
  the game gets that file instead. `<key>` is a hash of the original texture.
- The game still sees each texture's original size (the menus and HUD compute their sprite coordinates
  from it), but draws all the HD pixels.
- Textures stay in the game's own memory pool and DXT format (DXT1 when opaque, DXT5 with alpha), with
  full mipmaps.

## Building a pack yourself

From your own `bionicle.dat`, with [uv](https://docs.astral.sh/uv/) and
[realesrgan-ncnn-vulkan](https://github.com/xinntao/Real-ESRGAN/releases) (any Vulkan GPU, ~45 minutes
on an RTX 3050 Laptop for the whole game):

```
uv run --with pillow --with numpy python -I tools/build_pack.py --dat "<game>\bionicle.dat" --realesrgan "<folder with realesrgan-ncnn-vulkan.exe>" --out build\pack --scale 4
```

`--scale` is per side (2 or 4). Copy `build\pack\textures` to `<game>\BionicleHeroesHD\textures`.
Colour keeps the original's brightness and contrast (only the fine detail comes from Real-ESRGAN), and
soft alpha (shadows, glows) stays soft.

## Settings

`BionicleHeroesHD.ini`:

```ini
[Textures]
Enabled = 1                        ; use the HD textures
Folder = BionicleHeroesHD\textures ; where they are

[Debug]
LogTextures = 0                    ; log every texture the game loads and whether it was replaced
DumpTextures = 0                   ; save every loaded texture to BionicleHeroesHD\dump
```

## Building the mod

Visual Studio Build Tools with the C++ workload and CMake (x86 / Win32): `build.cmd` →
`build\Release\BionicleHeroesHD.asi`. `package.cmd <7z.exe> <textures folder> <name>` makes a release
archive in `dist\` (needs the x86 `dinput8.dll` of Ultimate ASI Loader in
`third_party\ultimate-asi-loader\`, not kept in git).

## Credits

- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu (BSD 2-Clause), vendored in
  `third_party/minhook`.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG (MIT), shipped
  in release archives.
- Upscaling: [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) by Xintao Wang et al. (BSD 3-Clause),
  not included.
