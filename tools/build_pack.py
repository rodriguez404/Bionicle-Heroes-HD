"""Builds a Bionicle Heroes HD texture pack from your own bionicle.dat.

  uv run --with pillow --with numpy python -I tools/build_pack.py --dat "<game>/bionicle.dat"
      --realesrgan "<dir with realesrgan-ncnn-vulkan.exe>" --out build/pack [--files all] [--scale 4]

Every DDS texture in the archive (scene TST0 chunks, standalone .dds, .pak files...) is decoded,
upscaled with Real-ESRGAN (colour and alpha separately, edges wrapped so tiling textures stay
seamless) and saved with mipmaps as DXT1 (opaque) or DXT5 (alpha) under <out>/textures/<key>.dds.
<key> is the hash the mod computes for the texture the game loads (dds_key; see src/hd_textures.cpp).

Real-ESRGAN also sharpens what is meant to be soft (gradients, glows, soft shadows), so the result is a
per-pixel mix: where the original changes sharply from pixel to pixel (an edge or detail blurred by the
low resolution) the Real-ESRGAN pixels are used, where it changes gradually a plain Lanczos upscale.
Real-ESRGAN results are cached (<out>/esrgan), so the mix can be retuned without running it again.
--scale is per side (4 = 16x the pixels).
"""
import argparse
import concurrent.futures
import io
import os
import struct
import subprocess
import sys

import numpy as np
from PIL import Image

FILE_SETS = {
    "all": [""],
    "hub": ["levels/hub/frontend_hub/"],
}
# Left as they are: glyph positions of fonts / button atlases may be in texture pixels.
EXCLUDE = ["fonts/", "gui/fonts/", "gfx/glyphs", "gfx/pc_glyphs", "gfx/ps2_glyphs", "gfx/ngc_glyphs",
           # loading screens: full screen art, 415 MB even at 2x, left as they are
           "gfx/splashscreen"]
# Per side scale overrides, e.g. {"gfx/": 2}.
SCALE_OVERRIDE = {}
# Only the Lanczos upscale, no Real-ESRGAN.
LANCZOS_ONLY = []
MIN_SIZE = 16   # smaller textures get only the Lanczos upscale
PAD = 8         # source pixels wrapped around each edge before Real-ESRGAN
SOFT_LO, SOFT_HI = 24.0, 80.0  # per pixel change (0..255) from "gradual" to "sharp"


# --- bionicle.dat ------------------------------------------------------------------------------

def read_table(path):
    """[u32 table offset][u32 table size]; table: -1, files, files x {offset, size, packed size, flags},
    nodes, nodes x {s16 next, s16 prev, u32 name offset}, pool size, names. Node next <= 0: file with
    entry -next; next > 0: directory whose subtree ends before node `next`."""
    with open(path, "rb") as f:
        off, size = struct.unpack("<II", f.read(8))
        f.seek(off)
        t = f.read(size)
    files = struct.unpack_from("<I", t, 4)[0]
    entries = [struct.unpack_from("<IIII", t, 8 + i * 16) for i in range(files)]
    p = 8 + files * 16
    count = struct.unpack_from("<I", t, p)[0]
    nodes = [struct.unpack_from("<hhI", t, p + 4 + i * 8) for i in range(count)]
    q = p + 4 + count * 8
    pool = t[q + 4:q + 4 + struct.unpack_from("<I", t, q)[0]]
    name = lambda o: pool[o:pool.index(b"\0", o)].decode("latin1")
    result, stack = [], []
    for i, (nxt, _prev, noff) in enumerate(nodes):
        while stack and i >= stack[-1][0]:
            stack.pop()
        if i == 0:
            continue
        if nxt > 0:
            stack.append((nxt, name(noff)))
        else:
            d = "/".join(s[1] for s in stack if s[1])
            result.append(((d + "/" if d else "") + name(noff), entries[-nxt]))
    return result


def find_textures(data):
    """Offsets and payload lengths of the DDS files inside a file of the archive."""
    i = data.find(b"DDS |")
    while i != -1:
        if dds_info(data[i:i + 128]):
            yield i, dds_payload(data[i:])
        i = data.find(b"DDS |", i + 4)


# --- the texture key (same as the mod) -----------------------------------------------------------

def dds_info(dds):
    if len(dds) < 128 or dds[:4] != b"DDS " or struct.unpack_from("<I", dds, 4)[0] != 124:
        return None
    h, w = struct.unpack_from("<II", dds, 12)
    mips = max(1, struct.unpack_from("<I", dds, 28)[0])
    pf_flags = struct.unpack_from("<I", dds, 80)[0]
    fourcc = dds[84:88]
    bits = struct.unpack_from("<I", dds, 88)[0]
    return w, h, mips, (fourcc.decode("latin1") if pf_flags & 4 else "RGB%d" % bits), bits


def dds_payload(dds):
    """Header plus all mip levels, as far as the header describes them (padding after it is ignored)."""
    w, h, mips, fmt, bits = dds_info(dds)
    total = 128
    for _ in range(min(mips, 16)):
        if fmt.startswith("DXT"):
            total += max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * (8 if fmt == "DXT1" else 16)
        else:
            total += w * h * max(bits, 8) // 8
        w, h = max(1, w // 2), max(1, h // 2)
    return min(total, len(dds))


def dds_key(payload):
    """FNV-1a 64."""
    x = 0xCBF29CE484222325
    for b in payload:
        x = ((x ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % x


# --- upscaling -------------------------------------------------------------------------------------

def wrap_pad(a, pad):
    return np.pad(a, ((pad, pad), (pad, pad)) + ((0, 0),) * (a.ndim - 2), mode="wrap")


def sharpness_weight(a, size):
    """0 where the source changes gradually from pixel to pixel, 1 where it changes sharply (an edge or
    detail the low resolution blurred), at the output size. `a`: h x w x c floats 0..255."""
    gx = np.abs(np.roll(a, -1, 1) - np.roll(a, 1, 1)) / 2
    gy = np.abs(np.roll(a, -1, 0) - np.roll(a, 1, 0)) / 2
    g = np.maximum(gx, gy).max(axis=2)
    w = np.clip((g - SOFT_LO) / (SOFT_HI - SOFT_LO), 0, 1)
    w = w * w * (3 - 2 * w)
    # the pixels next to an edge belong to it too
    w = np.maximum.reduce([np.roll(np.roll(w, dy, 0), dx, 1) for dy in (-1, 0, 1) for dx in (-1, 0, 1)])
    return np.asarray(Image.fromarray((w * 255).astype(np.uint8)).resize(size, Image.BILINEAR), np.float32) / 255


def mix(src, esr, size):
    """Alpha: src (L), esr = Real-ESRGAN result at `size` or None. Per pixel between Lanczos (gradual
    changes in the source) and Real-ESRGAN (sharp ones)."""
    smooth = np.asarray(src.resize(size, Image.LANCZOS), np.float32)
    if esr is None:
        return smooth
    a = np.asarray(src, np.float32)
    w = sharpness_weight(a if a.ndim == 3 else a[..., None], size)
    e = np.asarray(esr, np.float32)
    if smooth.ndim == 3:
        w = w[..., None]
    return smooth + (e - smooth) * w


def blur(a, sigma):
    """Gaussian blur of an h x w (x c) float array, wrapping at the edges."""
    radius = int(sigma * 3) + 1
    k = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma) ** 2)
    k /= k.sum()
    for axis in (0, 1):
        a = sum(np.roll(a, i, axis) * k[i + radius] for i in range(-radius, radius + 1))
    return a


def colour_mix(src, esr, size, scale):
    """Colour: the low frequencies (local average colour and brightness) of the Lanczos upscale, the
    high ones (detail, edges) of Real-ESRGAN, which otherwise shifts brightness and contrast."""
    smooth = np.asarray(src.resize(size, Image.LANCZOS), np.float32)
    if esr is None:
        return smooth
    e = np.asarray(esr, np.float32)
    sigma = scale * 0.7
    return blur(smooth, sigma) + (e - blur(e, sigma))


def encode_dds(img, with_alpha):
    """Full mip chain, DXT5 with alpha else DXT1, encoded per level by Pillow."""
    fmt = "DXT5" if with_alpha else "DXT1"
    w, h = img.size
    blobs = []
    lw, lh, level = w, h, img
    while True:
        b = io.BytesIO()
        level.save(b, "DDS", pixel_format=fmt)
        blobs.append(b.getvalue()[128:])
        if lw == 1 and lh == 1:
            break
        lw, lh = max(1, lw // 2), max(1, lh // 2)
        level = img.resize((lw, lh), Image.LANCZOS)
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000  # caps height width pixelformat mipmapcount linearsize
    header = struct.pack("<4sIIIIIII", b"DDS ", 124, flags, h, w, len(blobs[0]), 0, len(blobs))
    header += b"\0" * 44
    header += struct.pack("<II4sIIIII", 32, 0x4, fmt.encode(), 0, 0, 0, 0, 0)
    header += struct.pack("<IIIII", 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)  # texture complex mipmap
    return header + b"".join(blobs)


def load_texture(dat, offset, length):
    with open(dat, "rb") as f:
        f.seek(offset)
        data = f.read(length)
    img = Image.open(io.BytesIO(data))
    img.load()
    return img.convert("RGBA")


def build_one(job):
    key, dat, offset, length, scale, esr_dir, out_path = job
    img = load_texture(dat, offset, length)
    w, h = img.size
    size = (w * scale, h * scale)
    rgba = np.asarray(img)
    has_alpha = rgba[..., 3].min() < 255

    def esrgan(name):
        p = os.path.join(esr_dir, name)
        if not os.path.exists(p):
            return None
        pad = min(PAD, w, h) * 4
        im = Image.open(p).crop((pad, pad, pad + w * 4, pad + h * 4))
        return im if scale == 4 else im.resize(size, Image.LANCZOS)

    colour = colour_mix(img.convert("RGB"), esrgan(key + "_c.png"), size, scale)
    out = np.empty((size[1], size[0], 4), np.uint8)
    out[..., :3] = np.clip(colour + 0.5, 0, 255)
    if has_alpha:
        e = esrgan(key + "_a.png")
        alpha = mix(img.getchannel("A"), e.convert("L") if e else None, size)
        out[..., 3] = np.clip(alpha + 0.5, 0, 255)
    else:
        out[..., 3] = 255
    data = encode_dds(Image.fromarray(out, "RGBA"), has_alpha)
    with open(out_path, "wb") as f:
        f.write(data)
    return key, "%dx%d" % size, len(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dat", required=True)
    ap.add_argument("--realesrgan", required=True, help="folder with realesrgan-ncnn-vulkan.exe")
    ap.add_argument("--out", required=True)
    ap.add_argument("--scale", type=int, choices=(2, 4), default=4, help="per side")
    ap.add_argument("--files", default="all", help="file set (%s) or a path prefix in bionicle.dat" % ", ".join(FILE_SETS))
    ap.add_argument("--model", default="realesrgan-x4plus")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    args = ap.parse_args()

    prefixes = FILE_SETS.get(args.files, [args.files])
    out_tex, esr_dir = os.path.join(args.out, "textures"), os.path.join(args.out, "esrgan")
    esr_in = os.path.join(args.out, "esrgan_in")
    for d in (out_tex, esr_dir, esr_in):
        os.makedirs(d, exist_ok=True)

    # 1. find the textures (first file wins for a texture used in several)
    textures = {}  # key -> (offset in dat, payload length, source, scale)
    lanczos_only = set()
    with open(args.dat, "rb") as f:
        for path, (off, size, _psize, _flags) in read_table(args.dat):
            low = path.lower()
            if not any(low.startswith(p) for p in prefixes) or any(low.startswith(p) for p in EXCLUDE):
                continue
            f.seek(off)
            data = f.read(size)
            for i, n in find_textures(data):
                key = dds_key(data[i:i + n])
                if key not in textures:
                    scale = next((s for p, s in SCALE_OVERRIDE.items() if low.startswith(p)), args.scale)
                    textures[key] = (off + i, n, "%s @%x" % (path, i), scale)
                    if any(low.startswith(p) for p in LANCZOS_ONLY):
                        lanczos_only.add(key)
    print("%d unique textures" % len(textures), flush=True)

    # 2. Real-ESRGAN for what is not cached yet
    todo = 0
    for key, (offset, n, source, scale) in textures.items():
        if key in lanczos_only or os.path.exists(os.path.join(esr_dir, key + "_c.png")):
            continue
        try:
            img = load_texture(args.dat, offset, n)
        except Exception as e:
            print("skip %s: %s" % (source, e))
            continue
        if min(img.size) < MIN_SIZE:
            continue
        pad = min(PAD, *img.size)
        rgba = np.asarray(img)
        Image.fromarray(wrap_pad(rgba[..., :3], pad)).save(os.path.join(esr_in, key + "_c.png"))
        if rgba[..., 3].min() < 255:
            a = wrap_pad(rgba[..., 3], pad)
            Image.fromarray(np.stack([a] * 3, -1)).save(os.path.join(esr_in, key + "_a.png"))
        todo += 1
    if todo:
        print("Real-ESRGAN (%s) on %d textures..." % (args.model, todo), flush=True)
        exe = os.path.join(args.realesrgan, "realesrgan-ncnn-vulkan.exe")
        r = subprocess.run([exe, "-i", esr_in, "-o", esr_dir, "-n", args.model, "-s", "4", "-f", "png"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        if r.returncode != 0:
            sys.exit("realesrgan failed:\n" + r.stdout[-2000:])
        for name in os.listdir(esr_in):
            os.remove(os.path.join(esr_in, name))

    # 3. mix, mipmaps, DXT
    jobs = [(key, args.dat, offset, n, scale, esr_dir, os.path.join(out_tex, key + ".dds"))
            for key, (offset, n, source, scale) in textures.items()]
    manifest, done = [], 0
    with concurrent.futures.ProcessPoolExecutor(args.jobs) as pool:
        futures = {pool.submit(build_one, j): j[0] for j in jobs}
        for fut in concurrent.futures.as_completed(futures):
            key = futures[fut]
            try:
                k, size, nbytes = fut.result()
                manifest.append("%s\t%s\t%s\t%d" % (k, textures[k][2], size, nbytes))
            except Exception as e:
                print("failed %s (%s): %s" % (key, textures[key][2], e))
            done += 1
            if done % 250 == 0:
                print("%d / %d" % (done, len(jobs)), flush=True)
    with open(os.path.join(args.out, "manifest.tsv"), "w", encoding="utf-8") as f:
        f.write("key\tsource\tsize\tbytes\n" + "\n".join(sorted(manifest)) + "\n")
    total = sum(int(m.split("\t")[3]) for m in manifest)
    print("%d textures, %.1f MB -> %s" % (len(manifest), total / 2**20, out_tex))


if __name__ == "__main__":
    main()
