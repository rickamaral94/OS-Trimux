#!/usr/bin/env python3
"""Draws the TriMux start screen (sdcard/TriMux/share/splash.png).

The menu shows it while it starts and while it indexes games, with the
version and a status line written below the artwork at run time. Drawn from
code (no external artwork): a dark gradient, three overlapping triangles
("Tri") and the wordmark in DejaVu Sans Bold. Run again after changing it:

    python3 tools/make_splash.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(ROOT, "sdcard", "TriMux", "share", "splash.png")
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FONT_REG = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
W, H = 1024, 768
BG_TOP, BG_BOTTOM = (0x0b, 0x10, 0x16), (0x16, 0x1f, 0x2b)   # dark theme bg / panel
ACCENT = (0x2e, 0x86, 0xde)                                  # theme accent
TRIANGLES = [(0x2e, 0x86, 0xde), (0x1a, 0xbc, 0x9c), (0x9b, 0x59, 0xb6)]
TEXT = (0xee, 0xf2, 0xf5)
DIM = (0x8d, 0x9a, 0xa8)


def gradient():
    img = Image.new("RGB", (W, H))
    px = img.load()
    for y in range(H):
        t = y / (H - 1)
        row = tuple(int(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t) for i in range(3))
        for x in range(W):
            px[x, y] = row
    return img


def glow(img, center, radius, color, strength):
    layer = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(layer)
    cx, cy = center
    d.ellipse((cx - radius, cy - radius, cx + radius, cy + radius), fill=color)
    layer = layer.filter(ImageFilter.GaussianBlur(radius * 0.6))
    return Image.blend(img, Image.composite(layer, img, layer.convert("L")), strength)


def triangle(cx, cy, size, angle):
    pts = []
    for k in range(3):
        a = math.radians(angle + 120 * k - 90)
        pts.append((cx + size * math.cos(a), cy + size * math.sin(a)))
    return pts


def main():
    img = gradient()
    img = glow(img, (W // 2, 330), 260, ACCENT, 0.35)
    over = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    # logo: three translucent "play" triangles, overlapping left to right
    lx, ly, size = W // 2 - 8, 240, 70
    for k, col in enumerate(TRIANGLES):
        cx = lx + (k - 1) * 46
        d.polygon(triangle(cx, ly, size, 90), fill=col + (190,))
    img = Image.alpha_composite(img.convert("RGBA"), over)
    d = ImageDraw.Draw(img)
    # wordmark: "Tri" + "Mux"
    font = ImageFont.truetype(FONT, 132)
    a, b = "Tri", "Mux"
    wa = d.textlength(a, font=font)
    wb = d.textlength(b, font=font)
    x0 = (W - (wa + wb)) / 2
    y0 = 370
    d.text((x0, y0), a, font=font, fill=TEXT)
    d.text((x0 + wa, y0), b, font=font, fill=ACCENT)
    # underline accent bar
    bar_w = 120
    d.rounded_rectangle(((W - bar_w) / 2, y0 + 168, (W + bar_w) / 2, y0 + 174), radius=3, fill=ACCENT)
    tag = ImageFont.truetype(FONT_REG, 26)
    line = "para TrimUI Brick Pro"
    d.text(((W - d.textlength(line, font=tag)) / 2, y0 + 192), line, font=tag, fill=DIM)
    img.convert("RGB").save(OUT, optimize=True)
    print(OUT)


if __name__ == "__main__":
    main()
