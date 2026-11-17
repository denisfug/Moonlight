#!/usr/bin/env python3
"""Generates the original Moonlight HB icon (512x512 icon0.png) and a 1920x1080 pic1.png backdrop.
Usage: python3 tools/make_icon.py <out_dir>   (needs Pillow and assets/fonts/Outfit-Bold.ttf)"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "assets", "fonts", "Outfit-Bold.ttf")


def night(w, h):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            t = (x / w + y / h) / 2
            px[x, y] = (int(14 + 10 * (1 - t)), int(12 + 6 * (1 - t)), int(40 - 18 * t))
    d = ImageDraw.Draw(img)
    rnd = random.Random(7)
    for _ in range(int(w * h / 4000)):
        x, y = rnd.randrange(w), rnd.randrange(h)
        r = rnd.choice([1, 1, 1, 2])
        a = rnd.randrange(90, 230)
        d.ellipse([x - r, y - r, x + r, y + r], fill=(a, a, 255))
    return img


def moon(size):
    """Crescent: violet->blue disc with an offset disc cut out, plus a soft glow."""
    s = size * 4
    grad = Image.new("RGB", (s, s))
    gp = grad.load()
    for y in range(s):
        for x in range(s):
            t = (x + y) / (2 * s)
            gp[x, y] = (int(167 - 111 * t), int(139 + 13 * t), int(250 + 5 * t))
    mask = Image.new("L", (s, s), 0)
    md = ImageDraw.Draw(mask)
    md.ellipse([0, 0, s - 1, s - 1], fill=255)
    off = int(s * 0.30)
    md.ellipse([off, -int(s * 0.12), off + s, s - int(s * 0.12)], fill=0)
    out = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    out.paste(grad, (0, 0), mask)
    return out.resize((size, size), Image.LANCZOS)


def place_moon(img, cx, cy, size):
    glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    r = size * 0.62
    gd.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(139, 92, 246, 110))
    glow = glow.filter(ImageFilter.GaussianBlur(size * 0.18))
    base = img.convert("RGBA")
    base = Image.alpha_composite(base, glow)
    m = moon(size)
    base.alpha_composite(m, (int(cx - size / 2), int(cy - size / 2)))
    return base


def icon(out):
    img = place_moon(night(512, 512), 256, 210, 250)
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(FONT, 66)
    text = "MOONLIGHT"
    w = d.textlength(text, font=f)
    d.text(((512 - w) / 2, 362), text, font=f, fill=(238, 236, 255))
    fb = ImageFont.truetype(FONT, 34)
    bw = d.textlength("HB", font=fb) + 36
    x0 = (512 - bw) / 2
    d.rounded_rectangle([x0, 440, x0 + bw, 484], radius=22, fill=(110, 110, 245))
    d.text((x0 + 18, 441), "HB", font=fb, fill=(255, 255, 255))
    img.convert("RGB").save(os.path.join(out, "icon0.png"))


def backdrop(out):
    img = place_moon(night(1920, 1080), 1500, 380, 520)
    img.convert("RGB").save(os.path.join(out, "pic1.png"))


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "sce_sys")
    os.makedirs(out, exist_ok=True)
    icon(out)
    backdrop(out)
    print("wrote", os.path.join(out, "icon0.png"), os.path.join(out, "pic1.png"))
