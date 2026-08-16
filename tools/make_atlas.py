#!/usr/bin/env python3
"""Bake the Kenney CC0 sprites into gavaga's atlas.

Reads the source PNGs in assets/kenney/, scales each into a fixed cell, lays
them out in the same order as the GV_SPR_* enum, and writes:

    assets/gavaga_atlas.png     the atlas as a PNG, for looking at
    src/gv_atlas_bmp.h          the same image as a C array, for the game

The game gets a BMP rather than the PNG deliberately. SDL decodes BMP on its
own; PNG would mean SDL_image, and that means libpng and zlib built and linked
on three platforms - a large dependency to carry for one 96 KB image. Embedding
it also keeps the binary self-contained, with nothing to install beside it.

Pure standard library on purpose: this runs anywhere python3 does, and a build
tool that needs its own dependency tree is a build tool people stop running.
Every source is 8-bit RGBA and non-interlaced, which is the easy case.

    python3 tools/make_atlas.py
"""
import os
import struct
import sys
import zlib

CELL = 32          # atlas cell, in texels
COLS = 8           # must match GV_ATLAS_COLS
OVERSAMPLE = 2     # texels per logical pixel: a 32px cell draws 16px tall

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "assets", "kenney")


# --- PNG in ---------------------------------------------------------------
def read_png(path):
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")

    w = h = None
    idat = bytearray()
    i = 8
    while i < len(d):
        (n,) = struct.unpack(">I", d[i:i + 4])
        tag = d[i + 4:i + 8]
        body = d[i + 8:i + 8 + n]
        if tag == b"IHDR":
            w, h, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if (depth, colour, interlace) != (8, 6, 0):
                raise ValueError(f"{path}: want 8-bit RGBA non-interlaced, "
                                 f"got depth={depth} colour={colour} interlace={interlace}")
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            break
        i += 12 + n

    raw = zlib.decompress(bytes(idat))
    stride = w * 4
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        f = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos + stride]); pos += stride
        if f == 1:      # Sub
            for x in range(4, stride):
                line[x] = (line[x] + line[x - 4]) & 0xFF
        elif f == 2:    # Up
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif f == 3:    # Average
            for x in range(stride):
                a = line[x - 4] if x >= 4 else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif f == 4:    # Paeth
            for x in range(stride):
                a = line[x - 4] if x >= 4 else 0
                b = prev[x]
                c = prev[x - 4] if x >= 4 else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        elif f != 0:
            raise ValueError(f"{path}: bad filter {f}")
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, out


def write_bmp(path, w, h, rgba):
    """32-bit BGRA, bottom-up, BITMAPV4HEADER so the alpha channel is declared.

    A plain BITMAPINFOHEADER has nowhere to say which bits are alpha, and SDL
    then loads the sprites fully opaque - every cell a black square.
    """
    rows = []
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            p = (y * w + x) * 4
            row += bytes([rgba[p + 2], rgba[p + 1], rgba[p], rgba[p + 3]])
        rows.append(bytes(row))
    pixels = b"".join(rows)

    hdr = struct.pack("<IiiHHIIiiII", 108, w, h, 1, 32, 3, len(pixels), 2835, 2835, 0, 0)
    hdr += struct.pack("<IIII", 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    hdr += b"BGRs"                      # bV4CSType
    hdr += b"\x00" * 36                 # endpoints
    hdr += struct.pack("<III", 0, 0, 0)  # gamma
    off = 14 + len(hdr)
    fh = b"BM" + struct.pack("<IHHI", off + len(pixels), 0, 0, off)
    open(path, "wb").write(fh + hdr + pixels)


def write_png(path, w, h, rgba):
    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(tag, body):
        c = tag + body
        return struct.pack(">I", len(body)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b""))


# --- scaling --------------------------------------------------------------
def tint_scale(px, tint, alpha):
    """Recolour towards `tint` and scale alpha. Used to turn one starburst into
    an explosion that cools as it expands."""
    out = bytearray(px)
    for i in range(0, len(out), 4):
        a = out[i + 3]
        if not a:
            continue
        out[i] = (out[i] * (255 - tint[3]) + tint[0] * tint[3]) // 255
        out[i + 1] = (out[i + 1] * (255 - tint[3]) + tint[1] * tint[3]) // 255
        out[i + 2] = (out[i + 2] * (255 - tint[3]) + tint[2] * tint[3]) // 255
        out[i + 3] = min(255, int(a * alpha))
    return out


def scale_into(src_w, src_h, src, cell, squash=1.0, fill=0.94):
    """Box-filter into a `cell` square, keeping aspect and centring.

    Colour is averaged weighted by alpha. Averaging straight RGB instead pulls
    the transparent black outside a sprite into its edges and leaves every
    outline muddy.
    """
    fit = min(cell / src_w, cell / src_h) * fill   # a texel of air round the edge
    dw = max(1, int(round(src_w * fit)))
    dh = max(1, int(round(src_h * fit * squash)))
    ox, oy = (cell - dw) // 2, (cell - dh) // 2

    out = bytearray(cell * cell * 4)
    for y in range(dh):
        y0, y1 = y * src_h // dh, max(y * src_h // dh + 1, (y + 1) * src_h // dh)
        for x in range(dw):
            x0, x1 = x * src_w // dw, max(x * src_w // dw + 1, (x + 1) * src_w // dw)
            r = g = b = a = n = 0
            for sy in range(y0, y1):
                base = (sy * src_w) * 4
                for sx in range(x0, x1):
                    p = base + sx * 4
                    sa = src[p + 3]
                    r += src[p] * sa
                    g += src[p + 1] * sa
                    b += src[p + 2] * sa
                    a += sa
                    n += 1
            if n == 0:
                continue
            o = ((oy + y) * cell + (ox + x)) * 4
            if a:
                out[o] = min(255, r // a)
                out[o + 1] = min(255, g // a)
                out[o + 2] = min(255, b // a)
            out[o + 3] = a // n
    return out


def flip_v(cell, px):
    out = bytearray(len(px))
    for y in range(cell):
        s = (cell - 1 - y) * cell * 4
        out[y * cell * 4:(y + 1) * cell * 4] = px[s:s + cell * 4]
    return out


# --- what goes where ------------------------------------------------------
# (file, flip_vertically, squash) in GV_SPR_* order. Kenney draws every ship
# nose-up; the enemies fly down the screen, so theirs are flipped. The squash
# on the B frames is the animation: there is no second frame in the pack, and a
# ship that breathes reads better than one that is simply static.
# (file, flip, squash, fill, tint, alpha). Tint is (r, g, b, strength).
#
# The pack has no explosion sequence - its "fire" sprites are flame plumes,
# which read as a jet exhaust rather than a ship coming apart. The four boom
# frames are instead one starburst grown and cooled: white-hot and small, then
# yellow, then orange, then a dim red ghost. Same source art, same licence.
BURST = "star3.png"
SPRITES = [
    ("playerShip1_blue.png",   False, 1.00, 0.94, None, 1.0),   # PLAYER
    ("enemyBlue3.png",         True,  1.00, 0.94, None, 1.0),   # GRUNT_A
    ("enemyBlue3.png",         True,  0.88, 0.94, None, 1.0),   # GRUNT_B
    ("enemyRed1.png",          True,  1.00, 0.94, None, 1.0),   # GUARD_A
    ("enemyRed1.png",          True,  0.88, 0.94, None, 1.0),   # GUARD_B
    ("ufoGreen.png",           False, 1.00, 0.94, None, 1.0),   # FLAGSHIP_A
    ("ufoGreen.png",           False, 0.90, 0.94, None, 1.0),   # FLAGSHIP_B
    ("ufoYellow.png",          False, 1.00, 0.94, None, 1.0),   # FLAGSHIP_HURT_A
    ("ufoYellow.png",          False, 0.90, 0.94, None, 1.0),   # FLAGSHIP_HURT_B
    ("playerShip1_red.png",    False, 1.00, 0.94, None, 1.0),   # CAPTIVE
    ("laserBlue01.png",        False, 1.00, 0.94, None, 1.0),   # PSHOT
    ("laserRed01.png",         True,  1.00, 0.94, None, 1.0),   # ESHOT
    (BURST,                    False, 1.00, 0.42, (255, 248, 210, 200), 1.00),  # BOOM0
    (BURST,                    False, 1.00, 0.68, (255, 205, 90, 205), 1.00),   # BOOM1
    (BURST,                    False, 1.00, 0.92, (255, 140, 45, 210), 0.85),   # BOOM2
    (BURST,                    False, 1.00, 1.00, (215, 70, 30, 225), 0.45),    # BOOM3
    ("playerLife1_blue.png",   False, 1.00, 0.94, None, 1.0),   # LIFE
    ("star1.png",              False, 1.00, 0.80, None, 1.0),   # BADGE
    ("star2.png",              False, 1.00, 0.88, None, 1.0),   # BADGE5
    ("star3.png",              False, 1.00, 0.94, None, 1.0),   # BADGE10
    ("enemyGreen4.png",        True,  1.00, 0.94, None, 1.0),   # SENTINEL_A
    ("enemyGreen4.png",        True,  0.88, 0.94, None, 1.0),   # SENTINEL_B
    ("enemyBlack5.png",        True,  1.00, 0.94, None, 1.0),   # DARTER_A
    ("enemyBlack5.png",        True,  0.88, 0.94, None, 1.0),   # DARTER_B
]


def main():
    rows = (len(SPRITES) + COLS - 1) // COLS
    aw, ah = COLS * CELL, rows * CELL
    atlas = bytearray(aw * ah * 4)

    for i, (name, flip, squash, fill, tint, alpha) in enumerate(SPRITES):
        path = os.path.join(SRC, name)
        if not os.path.exists(path):
            sys.exit(f"missing source sprite: {path}")
        w, h, px = read_png(path)
        cellpx = scale_into(w, h, px, CELL, squash, fill)
        if tint or alpha != 1.0:
            cellpx = tint_scale(cellpx, tint or (0, 0, 0, 0), alpha)
        if flip:
            cellpx = flip_v(CELL, cellpx)

        cx, cy = (i % COLS) * CELL, (i // COLS) * CELL
        for y in range(CELL):
            d = ((cy + y) * aw + cx) * 4
            s = y * CELL * 4
            atlas[d:d + CELL * 4] = cellpx[s:s + CELL * 4]

    out_png = os.path.join(ROOT, "assets", "gavaga_atlas.png")
    write_png(out_png, aw, ah, atlas)

    out_bmp = os.path.join(ROOT, "assets", "gavaga_atlas.bmp")
    write_bmp(out_bmp, aw, ah, atlas)
    blob = open(out_bmp, "rb").read()

    hdr = os.path.join(ROOT, "src", "gv_atlas_bmp.h")
    with open(hdr, "w") as f:
        f.write("// gv_atlas_bmp.h - GENERATED by tools/make_atlas.py, do not edit.\n"
                "//\n"
                "// The sprite atlas as an embedded BMP. BMP because SDL decodes it without\n"
                "// SDL_image, and embedded because the game should stay one file you can\n"
                "// copy anywhere.\n"
                "//\n"
                "// Art: Space Shooter Redux by Kenney (kenney.nl), CC0. The sources it was\n"
                "// built from, and the licence, are in assets/kenney/.\n"
                "#ifndef GV_ATLAS_BMP_H\n#define GV_ATLAS_BMP_H\n\n"
                f"#define GV_ATLAS_BMP_WIDTH  {aw}\n"
                f"#define GV_ATLAS_BMP_HEIGHT {ah}\n"
                f"#define GV_ATLAS_BMP_CELL {CELL}\n"
                f"#define GV_ATLAS_OVERSAMPLE {OVERSAMPLE}\n\n"
                f"static const unsigned char GV_ATLAS_BMP[{len(blob)}] = {{\n")
        for i in range(0, len(blob), 16):
            f.write("    " + ",".join(f"0x{b:02x}" for b in blob[i:i + 16]) + ",\n")
        f.write("};\n\n#endif // GV_ATLAS_BMP_H\n")

    print(f"{len(SPRITES)} sprites -> {aw}x{ah} atlas, {len(blob)} bytes of BMP")
    print(f"  {out_png}")
    print(f"  {out_bmp}")
    print(f"  {hdr}")


if __name__ == "__main__":
    main()
