#!/usr/bin/env python3
"""Generates res/app.ico: the clay pixel mascot on a warm charcoal rounded square.

The mascot is pixel art, so every icon size is drawn natively with an integer cell size (no blurry
downscaling). Requires Pillow (dev-only; the app itself has no dependencies).

Usage: python3 scripts/make_icon.py [out.ico] [--preview preview.png]
"""
import sys

from PIL import Image, ImageDraw

SIZES = (16, 20, 24, 32, 48, 64, 256)

BG = (0x26, 0x26, 0x24, 255)
CLAY = (0xD9, 0x77, 0x57, 255)
CLAY_LIGHT = (0xE0, 0x8A, 0x6D, 255)
CLAY_DARK = (0xC4, 0x66, 0x47, 255)
LIMB = (0xC9, 0x6A, 0x4B, 255)
EYE = (0x1F, 0x1E, 0x1D, 255)

# Standing pose (the app shows it hanging from the search bar).
SPRITE = (
    ".ttttttttttt.",
    ".bbbebbbebbb.",
    "abbbebbbebbba",
    ".bbbbbbbbbbb.",
    ".bbbbbbbbbbb.",
    ".ddddddddddd.",
    "..a.a...a.a..",
    "..a.a...a.a..",
)
COLORS = {"t": CLAY_LIGHT, "b": CLAY, "d": CLAY_DARK, "a": LIMB, "e": EYE}


def render(size):
    # Supersample the rounded background, then paste crisp pixel art on top.
    ss = 4
    bg = Image.new("RGBA", (size * ss, size * ss), (0, 0, 0, 0))
    inset = max(0, round(size * 0.04)) * ss
    ImageDraw.Draw(bg).rounded_rectangle(
        (inset, inset, size * ss - 1 - inset, size * ss - 1 - inset), radius=size * ss * 0.22, fill=BG
    )
    img = bg.resize((size, size), Image.LANCZOS)

    w, h = len(SPRITE[0]), len(SPRITE)
    cell = max(1, int(size * 0.8) // w)
    ox = (size - w * cell) // 2
    oy = (size - h * cell) // 2 + (cell if size >= 32 else 0) // 2
    d = ImageDraw.Draw(img)
    for r, row in enumerate(SPRITE):
        for c, ch in enumerate(row):
            if ch in COLORS:
                x, y = ox + c * cell, oy + r * cell
                d.rectangle((x, y, x + cell - 1, y + cell - 1), fill=COLORS[ch])
    return img


def main():
    args = sys.argv[1:]
    preview = None
    if "--preview" in args:
        i = args.index("--preview")
        preview = args[i + 1]
        del args[i : i + 2]
    out = args[0] if args else "res/app.ico"
    images = [render(s) for s in SIZES]
    images[-1].save(out, format="ICO", sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    if preview:
        images[-1].save(preview)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
