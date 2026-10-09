#!/usr/bin/env python3
"""Generates res/app.ico: a mac-style rounded square (blue -> purple gradient) with a white magnifying glass.

Pure Python (no PIL): every size is rendered from signed distance functions with analytic anti-aliasing, so
small sizes stay crisp instead of being downscaled from 256 px. Sizes < 256 are stored as 32-bit BMP entries
(maximum compatibility), 256 as PNG.

Usage: python3 scripts/make_icon.py [out.ico] [--preview preview.png]
"""
import math
import os
import struct
import sys
import zlib

SIZES = (16, 20, 24, 32, 48, 64, 256)

# Gradient stops along the top-left -> bottom-right diagonal.
STOPS = ((0.0, (56, 140, 255)), (0.5, (104, 98, 250)), (1.0, (178, 74, 238)))


def lerp(a, b, t):
    return a + (b - a) * t


def gradient(t):
    t = min(1.0, max(0.0, t))
    for (t0, c0), (t1, c1) in zip(STOPS, STOPS[1:]):
        if t <= t1:
            k = (t - t0) / (t1 - t0)
            return tuple(lerp(c0[i], c1[i], k) for i in range(3))
    return STOPS[-1][1]


def sd_round_rect(px, py, cx, cy, half, r):
    qx = abs(px - cx) - (half - r)
    qy = abs(py - cy) - (half - r)
    outside = math.hypot(max(qx, 0.0), max(qy, 0.0))
    return outside + min(max(qx, qy), 0.0) - r


def sd_capsule(px, py, ax, ay, bx, by, r):
    pax, pay = px - ax, py - ay
    bax, bay = bx - ax, by - ay
    h = min(1.0, max(0.0, (pax * bax + pay * bay) / (bax * bax + bay * bay)))
    return math.hypot(pax - bax * h, pay - bay * h) - r


def cov(d, soft=1.0):
    """Coverage of a pixel whose center is at signed distance d (px) from an edge."""
    return min(1.0, max(0.0, 0.5 - d / soft))


def render(s):
    m = s / 32.0                      # outer margin
    side = s - 2 * m
    half = side / 2
    cx = cy = s / 2
    radius = side * 0.225
    # Magnifier geometry; strokes get a pixel minimum so 16/20 px stay legible.
    gx = gy = m + side * 0.44
    ring_r = side * 0.215
    ring_w = max(side * 0.085, 1.45)
    handle_w = max(side * 0.12, 2.1)
    k = (ring_r + ring_w * 0.5) / math.sqrt(2)
    hx0, hy0 = gx + k, gy + k
    hx1 = hy1 = m + side * 0.765
    shadow = s >= 32
    sh_off, sh_soft = side * 0.022, max(side * 0.035, 1.0)

    def glass(px, py):
        ring = abs(math.hypot(px - gx, py - gy) - ring_r) - ring_w / 2
        return min(ring, sd_capsule(px, py, hx0, hy0, hx1, hy1, handle_w / 2))

    px_out = []
    for y in range(s):
        row = []
        for x in range(s):
            px, py = x + 0.5, y + 0.5
            a_bg = cov(sd_round_rect(px, py, cx, cy, half, radius))
            if a_bg <= 0.0:
                row.append((0, 0, 0, 0))
                continue
            u, v = (px - m) / side, (py - m) / side
            r, g, b = gradient((u + v) / 2)
            sheen = 0.16 * max(0.0, 1.0 - v * 1.6) ** 2  # soft top highlight
            r, g, b = (lerp(c, 255, sheen) for c in (r, g, b))
            if shadow:
                a_sh = 0.28 * cov(glass(px, py - sh_off), sh_soft * 2)
                r, g, b = (lerp(c, t, a_sh) for c, t in zip((r, g, b), (30, 16, 80)))
            a_gl = cov(glass(px, py))
            r, g, b = (lerp(c, 255, a_gl) for c in (r, g, b))
            row.append((round(r), round(g), round(b), round(255 * a_bg)))
        px_out.append(row)
    return px_out


def png_bytes(img):
    s = len(img)
    raw = b"".join(b"\x00" + bytes(c for p in row for c in p) for row in img)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", s, s, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def bmp_bytes(img):
    s = len(img)
    mask_stride = ((s + 31) // 32) * 4
    xor = bytearray()
    mask = bytearray()
    for row in reversed(img):  # bottom-up
        bits = bytearray(mask_stride)
        for x, (r, g, b, a) in enumerate(row):
            xor += bytes((b, g, r, a))
            if a == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    header = struct.pack("<IiiHHIIiiII", 40, s, 2 * s, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0)
    return header + bytes(xor) + bytes(mask)


def write_ico(path, images):
    entries, blobs = [], []
    offset = 6 + 16 * len(images)
    for img in images:
        s = len(img)
        data = png_bytes(img) if s >= 256 else bmp_bytes(img)
        dim = 0 if s >= 256 else s
        entries.append(struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset))
        blobs.append(data)
        offset += len(data)
    with open(path, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, len(images)))
        f.write(b"".join(entries))
        f.write(b"".join(blobs))


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "res", "app.ico")
    preview = None
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == "--preview" and args:
            preview = args.pop(0)
        else:
            out = a
    images = [render(s) for s in SIZES]
    write_ico(out, images)
    if preview:
        with open(preview, "wb") as f:
            f.write(png_bytes(images[-1]))
    print(f"wrote {out} ({os.path.getsize(out)} bytes, sizes {', '.join(map(str, SIZES))})")


if __name__ == "__main__":
    main(sys.argv[1:])
