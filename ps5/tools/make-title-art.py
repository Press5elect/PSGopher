#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# PSGopher by Press5elect - the title's pictures on the console's home screen:
#
#   sce_sys/pic0.dds   the picture behind the title when it is selected (3840x2160)
#   sce_sys/pic1.dds   the picture while the title loads (3840x2160)
#
# Both show the Gopher64 gopher (vendor/gopher64/data/icon/gopher64.png, 1024x1024, the largest
# size Gopher64 ships, used at 1000 px and never enlarged; GPL-3.0, Gopher64's), "PSGopher" in
# Bricolage Grotesque, "by Press5elect" and the address github.com/Press5elect in JetBrains Mono,
# on the Live Board's dark green-black with ps5/art/board.png dimmed behind. The left of the picture
# and its bottom quarter are left empty: the console draws its own menu there.
#
# The DDS header and the small BC7 encoder (mode 6) are Crux2's ps5/tools/make-title-art.py
# (Crux2 by Press5elect), which has them from PS5_RetroArch's tools/make-title-art.py (Copyright
# (C) 2026 Mihawk-99, GPL-3.0-or-later), whose header the console has shown.
# PSGopher: from OGBox db63d46 (ps5/tools/make-title-art.py).
#
#   python3 ps5/tools/make-title-art.py [--out DIR] [--preview DIR]
#
# Needs numpy and Pillow. --out names another folder than ps5/sce_sys; --preview also writes the
# two pictures as PNG files there. The fonts are looked for in .deps/src (bricolage, jetbrains-mono).
import argparse
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

WIDTH, HEIGHT = 3840, 2160
ROOT = Path(__file__).resolve().parent.parent.parent
GOPHER = ROOT / "vendor" / "gopher64" / "data" / "icon" / "gopher64.png"
BOARD = ROOT / "ps5" / "art" / "board.png"
DISPLAY_FONT = ROOT / ".deps" / "src" / "bricolage" / "fonts" / "ttf" / "BricolageGrotesque-ExtraBold.ttf"
MONO_FONT = ROOT / ".deps" / "src" / "jetbrains-mono" / "fonts" / "ttf" / "JetBrainsMono-SemiBold.ttf"
SCE_SYS = ROOT / "ps5" / "sce_sys"
GREEN = (79, 240, 224)       # the Live Board's accent (--game)
INK = (232, 244, 242)
SOFT = (150, 190, 186)
LINK = "github.com/Press5elect"


def background(dim: float) -> Image.Image:
    """The Live Board's dark green-black, with the board art dimmed and softened behind."""
    board = Image.open(BOARD).convert("RGB")
    k = WIDTH / board.width
    board = board.resize((WIDTH, round(board.height * k)), Image.LANCZOS)
    top = (board.height - HEIGHT) // 2
    board = board.crop((0, top, WIDTH, top + HEIGHT)).filter(ImageFilter.GaussianBlur(2.5))
    base = np.asarray(board, dtype=np.float64) * dim
    yy, xx = np.mgrid[0:HEIGHT, 0:WIDTH].astype(np.float64)
    # the Live Board's glow at the right of centre, a dark vignette, and the left kept dark
    d = np.hypot((xx - WIDTH * 0.64) / (WIDTH * 0.45), (yy - HEIGHT * 0.45) / (HEIGHT * 0.6))
    glow = np.clip(1.0 - d, 0, 1) ** 2
    shade = 0.35 + 0.65 * np.clip(xx / (WIDTH * 0.45), 0, 1)
    base *= shade[..., None]
    floor = np.array([3, 8, 11], dtype=np.float64)
    tint = np.array([6, 30, 36], dtype=np.float64)
    out = floor + base + glow[..., None] * tint
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))


def fit_font(path: Path, text: str, size: int, max_width: int) -> ImageFont.FreeTypeFont:
    while True:
        font = ImageFont.truetype(str(path), size)
        if font.getlength(text) <= max_width or size <= 8:
            return font
        size -= 4


def compose(quiet: bool) -> Image.Image:
    img = background(0.30 if quiet else 0.42).convert("RGBA")
    gopher = Image.open(GOPHER).convert("RGBA")
    side = 1000 if not quiet else 760
    gopher = gopher.resize((side, side), Image.LANCZOS) if side != gopher.width else gopher
    cy = 900
    gx, gy = 1480, cy - side // 2
    # a soft glow under the gopher
    halo = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(halo).ellipse((gx - 60, gy - 60, gx + side + 60, gy + side + 60), fill=GREEN + (60,))
    img.alpha_composite(halo.filter(ImageFilter.GaussianBlur(110)))
    img.alpha_composite(gopher, (gx, gy))

    tx = gx + side + 130
    room = WIDTH - 140 - tx
    title = fit_font(DISPLAY_FONT, "PSGopher", 340, room)
    by = fit_font(DISPLAY_FONT, "by Press5elect", 130, room)
    link = fit_font(MONO_FONT, LINK, 100, room - 120)
    d = ImageDraw.Draw(img)
    ty = cy - 330
    d.text((tx, ty), "PSGopher", font=title, fill=INK)
    ty2 = ty + title.size + 90
    d.text((tx, ty2), "by Press5elect", font=by, fill=GREEN)
    ly = ty2 + by.size + 110
    box = d.textbbox((tx, ly), LINK, font=link)
    pad = 36
    d.rounded_rectangle((box[0] - pad, box[1] - pad, box[2] + pad, box[3] + pad), radius=28,
                        fill=(6, 24, 28, 235), outline=GREEN + (200,), width=5)
    d.text((tx, ly), LINK, font=link, fill=INK)
    return img.convert("RGB")


def draw_selected() -> Image.Image:
    return compose(False)


def draw_loading() -> Image.Image:
    return compose(True)


# ---- BC7 mode 6 and the DDS header (PS5_RetroArch's tools/make-title-art.py) ----------------------
# One subset, RGBA endpoints of 7 bits and a p-bit, 4-bit indices.
WEIGHTS = np.array([0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64], dtype=np.int64)


def quantise(e: np.ndarray) -> np.ndarray:
    """Endpoint colours to the 7 stored bits (the p-bit is 1, for alpha 255)."""
    return np.clip(np.round((e - 1.0) / 2.0), 0, 127).astype(np.int64)


def palette(q0: np.ndarray, q1: np.ndarray) -> np.ndarray:
    """The 16 colours two quantised endpoints give, (blocks, 16, 3)."""
    e0, e1 = (q0 * 2 + 1)[:, None, :], (q1 * 2 + 1)[:, None, :]
    w = WEIGHTS[None, :, None]
    return ((64 - w) * e0 + w * e1 + 32) >> 6


def choose(px: np.ndarray, q0: np.ndarray, q1: np.ndarray):
    """Each pixel's best index for these endpoints, and the block's error."""
    pal = palette(q0, q1)
    err = ((px[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1)
    idx = err.argmin(-1)
    return idx, np.take_along_axis(err, idx[..., None], -1)[..., 0].sum(-1)


def encode_blocks(px: np.ndarray) -> bytes:
    """BC7 mode 6 for blocks of 16 RGB pixels (blocks, 16, 3), alpha 255."""
    pf = px.astype(np.float64)
    mean = pf.mean(1)
    cen = pf - mean[:, None, :]
    cov = np.einsum('nki,nkj->nij', cen, cen)
    axis = np.ones((len(px), 3)) / np.sqrt(3.0)
    for _ in range(8):
        axis = np.einsum('nij,nj->ni', cov, axis)
        norm = np.sqrt((axis * axis).sum(1, keepdims=True))
        axis = np.where(norm > 1e-9, axis / np.maximum(norm, 1e-9), 1.0 / np.sqrt(3.0))
    proj = np.einsum('nki,ni->nk', cen, axis)
    q0 = quantise(mean + proj.min(1, keepdims=True) * axis)
    q1 = quantise(mean + proj.max(1, keepdims=True) * axis)
    idx, err = choose(px, q0, q1)
    # One least-squares refit of the endpoints to the chosen indices
    w = WEIGHTS[idx].astype(np.float64) / 64.0
    a, b, c = ((1 - w) ** 2).sum(1), ((1 - w) * w).sum(1), (w * w).sum(1)
    d0 = ((1 - w)[..., None] * pf).sum(1)
    d1 = (w[..., None] * pf).sum(1)
    det = a * c - b * b
    ok = det > 1e-9
    safe = np.where(ok, det, 1.0)[:, None]
    r0 = quantise(np.where(ok[:, None], (c[:, None] * d0 - b[:, None] * d1) / safe, mean))
    r1 = quantise(np.where(ok[:, None], (a[:, None] * d1 - b[:, None] * d0) / safe, mean))
    idx2, err2 = choose(px, r0, r1)
    better = (err2 < err)[:, None]
    q0, q1 = np.where(better, r0, q0), np.where(better, r1, q1)
    idx = np.where(better, idx2, idx)
    # The first pixel's index is stored in 3 bits: swap the ends if it needs 4
    swap = idx[:, 0] >= 8
    q0, q1 = np.where(swap[:, None], q1, q0), np.where(swap[:, None], q0, q1)
    idx = np.where(swap[:, None], 15 - idx, idx)

    lo = np.full(len(px), 1 << 6, dtype=np.uint64)   # mode 6
    hi = np.zeros(len(px), dtype=np.uint64)
    pos = 7

    def put(value: np.ndarray, bits: int):
        nonlocal pos
        for bit in range(bits):
            v = ((value >> bit) & 1).astype(np.uint64)
            if pos < 64:
                lo[:] |= v << np.uint64(pos)
            else:
                hi[:] |= v << np.uint64(pos - 64)
            pos += 1

    for ch in range(3):
        put(q0[:, ch], 7)
        put(q1[:, ch], 7)
    alpha = np.full(len(px), 127, dtype=np.int64)
    put(alpha, 7)
    put(alpha, 7)
    one = np.ones(len(px), dtype=np.int64)
    put(one, 1)   # P0
    put(one, 1)   # P1
    put(idx[:, 0], 3)
    for i in range(1, 16):
        put(idx[:, i], 4)
    out = np.empty(len(px) * 2, dtype="<u8")
    out[0::2], out[1::2] = lo, hi
    return out.tobytes()


def bc7(img: np.ndarray) -> bytes:
    """The picture as BC7 blocks, row of blocks by row of blocks."""
    rgb = np.clip(np.round(img), 0, 255).astype(np.int64)
    by, bx = HEIGHT // 4, WIDTH // 4
    blocks = rgb.reshape(by, 4, bx, 4, 3).transpose(0, 2, 1, 3, 4).reshape(by * bx, 16, 3)
    step = bx * 16
    return b"".join(encode_blocks(blocks[i:i + step]) for i in range(0, len(blocks), step))


def dds_header() -> bytes:
    """The DDS header of the launcher art the console has shown: DX10, BC7_UNORM
    (98), 2D, one level, straight alpha."""
    DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PIXELFORMAT = 0x1, 0x2, 0x4, 0x1000
    DDSD_MIPMAPCOUNT, DDSD_LINEARSIZE = 0x20000, 0x80000
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT | DDSD_LINEARSIZE
    linear = WIDTH * HEIGHT               # BC7: 16 bytes a 4x4 block, one byte a pixel
    header = struct.pack("<4sIIIIIII", b"DDS ", 124, flags, HEIGHT, WIDTH, linear, 1, 1)
    header += b"\0" * 44                  # reserved
    header += struct.pack("<II4sIIIII", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
    header += struct.pack("<IIIII", 0x1000, 0, 0, 0, 0)   # caps: texture
    header += struct.pack("<IIIII", 98, 3, 0, 1, 1)       # BC7_UNORM, TEXTURE2D, array 1, straight alpha
    assert len(header) == 148
    return header


def decode_mode6(data: bytes) -> np.ndarray:
    """The picture back from the blocks above (mode 6 only): the check on the encoder."""
    words = np.frombuffer(data, dtype="<u8")
    lo, hi = words[0::2], words[1::2]

    def bits(position: int, count: int) -> np.ndarray:
        out = np.zeros(len(lo), dtype=np.int64)
        for bit in range(count):
            p = position + bit
            v = ((lo >> np.uint64(p)) if p < 64 else (hi >> np.uint64(p - 64))) & np.uint64(1)
            out |= v.astype(np.int64) << bit
        return out

    assert (bits(0, 7) == 64).all(), "a block is not mode 6"
    position = 7
    ends = np.zeros((len(lo), 2, 3), dtype=np.int64)
    for ch in range(3):
        for end in range(2):
            ends[:, end, ch] = bits(position, 7)
            position += 7
    position += 14 + 2  # alpha endpoints, the two p-bits (both 1)
    indices = np.zeros((len(lo), 16), dtype=np.int64)
    indices[:, 0] = bits(position, 3)
    position += 3
    for i in range(1, 16):
        indices[:, i] = bits(position, 4)
        position += 4
    pal = palette(ends[:, 0], ends[:, 1])
    px = np.take_along_axis(pal, indices[..., None].repeat(3, -1), 1)
    by, bx = HEIGHT // 4, WIDTH // 4
    return px.reshape(by, bx, 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(HEIGHT, WIDTH, 3)


def main() -> int:
    global SCE_SYS
    parser = argparse.ArgumentParser(description="The title's pictures for the console's menu, from the Gopher64 logo.")
    parser.add_argument("--out", type=Path, default=SCE_SYS, help="where to write them (default ps5/sce_sys)")
    parser.add_argument("--preview", type=Path, help="also write the pictures as PNG files here")
    args = parser.parse_args()
    SCE_SYS, preview = args.out, args.preview
    if preview:
        preview.mkdir(parents=True, exist_ok=True)

    SCE_SYS.mkdir(parents=True, exist_ok=True)
    for name, picture in (("pic0", draw_selected()), ("pic1", draw_loading())):
        img = np.asarray(picture, dtype=np.float64)
        blocks = bc7(img)
        (SCE_SYS / f"{name}.dds").write_bytes(dds_header() + blocks)
        error = decode_mode6(blocks).astype(np.float64) - np.round(img)
        psnr = 10.0 * np.log10(255.0 * 255.0 / max(float((error ** 2).mean()), 1e-9))
        print(f"sce_sys/{name}.dds: {148 + len(blocks)} bytes, {psnr:.1f} dB against the picture")
        if psnr < 38.0:
            print(f"{name}: the compressed picture is too far from the original", file=sys.stderr)
            return 1
        if preview:
            picture.save(preview / f"{name}.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
