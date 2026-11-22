"""Draws the port's Xbox 360 dashboard artwork (Aurora, FreeStyle) from its own
icon design (tools/make_icon.py: an original design, no game art), in the
sizes the Xbox 360 dashboards use:

  icon.png        64 x 64     the title's tile icon
  banner.png      420 x 96    the wide banner
  boxart.png      900 x 600   the full cover: back | spine | front
  background.png  1280 x 720  behind the game's details

Written to platform/xenon/aurora/ (the .xex package carries them).
Run: python3 tools/make_aurora_art.py
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import make_icon  # noqa: E402

OUT = os.path.join(ROOT, "platform", "xenon", "aurora")
NAME = "MEGUARACE"
TAG = "A PORT FOR YOUR OWN CD"

FONT_PATHS = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/Library/Fonts/Arial Bold.ttf",
    "C:/Windows/Fonts/arialbd.ttf",
]


def font(size):
    for p in FONT_PATHS:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def sky(w, h):
    """The icon's night sky, violet toward the bottom, with a neon grid floor."""
    im = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / h
        d.line([(0, y), (w, y)], fill=(int(18 + 70 * t), int(10 + 12 * t), int(48 + 60 * t), 255))
    hy = int(h * 0.62)
    d.rectangle([0, hy, w, h], fill=(14, 8, 30, 255))
    vx = w // 2
    for k in range(-12, 13):
        d.line([(vx, hy), (vx + k * w // 6, h)], fill=(0, 230, 255, 90), width=max(1, h // 240))
    for k in range(1, 9):
        y = hy + (h - hy) * (k / 9) ** 1.8
        d.line([(0, y), (w, y)], fill=(255, 40, 200, 110), width=max(1, h // 200 + k // 3))
    # The striped sun on the horizon.
    r = int(h * 0.22)
    d.pieslice([vx - r, hy - r, vx + r, hy + r], 180, 360, fill=(255, 170, 40, 255))
    for i, y in enumerate(range(hy - int(r * 0.7), hy, max(3, r // 6))):
        t = y / h
        d.rectangle([vx - r - 4, y, vx + r + 4, y + max(1, r // 30) + i],
                    fill=(int(18 + 70 * t), int(10 + 12 * t), int(48 + 60 * t), 255))
    return im


def glow_text(im, xy, text, size, fill=(255, 255, 255, 255), glow=(255, 60, 210, 255), anchor="mm"):
    f = font(size)
    layer = Image.new("RGBA", im.size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).text(xy, text, font=f, fill=glow, anchor=anchor)
    layer = layer.filter(ImageFilter.GaussianBlur(max(2, size // 8)))
    im.alpha_composite(layer)
    im.alpha_composite(layer)
    ImageDraw.Draw(im).text(xy, text, font=f, fill=fill, anchor=anchor,
                            stroke_width=max(1, size // 20), stroke_fill=(40, 10, 70, 255))


def fit(text, size, width):
    while size > 8 and font(size).getlength(text) > width:
        size -= 1
    return size


def emblem(px):
    return make_icon.draw().resize((px, px), Image.LANCZOS)


def icon():
    return make_icon.draw().resize((64, 64), Image.LANCZOS)


def banner():
    w, h = 420, 96
    im = sky(w, h)
    im.alpha_composite(emblem(84), (8, 6))
    glow_text(im, (100 + (w - 100) // 2, 38), NAME, fit(NAME, 44, w - 116))
    band = Image.new("RGBA", im.size, (0, 0, 0, 0))
    ImageDraw.Draw(band).rounded_rectangle([118, 68, w - 12, 90], radius=8, fill=(14, 8, 30, 210))
    im.alpha_composite(band)
    ImageDraw.Draw(im).text((100 + (w - 100) // 2, 79), TAG, font=font(14), fill=(150, 240, 255, 255), anchor="mm")
    return im


def boxart():
    # A full cover: back (left 425), spine (50), front (right 425).
    w, h, face, spine = 900, 600, 425, 50
    im = Image.new("RGBA", (w, h))
    front = sky(face, h)
    front.alpha_composite(emblem(300), ((face - 300) // 2, 150))
    glow_text(front, (face // 2, 75), NAME, fit(NAME, 70, face - 40))
    ImageDraw.Draw(front).text((face // 2, 530), TAG, font=font(20), fill=(150, 240, 255, 255), anchor="mm")
    back = sky(face, h).transpose(Image.FLIP_LEFT_RIGHT)
    glow_text(back, (face // 2, 70), NAME, fit(NAME, 46, face - 60))
    lines = ["Futuristic racing,", "ported to C++ and Lua.", "", "Plays from your own MegaRace CD:",
             "no game files are included.", "", "github.com/jgordon165/Meguarace"]
    panel = Image.new("RGBA", back.size, (0, 0, 0, 0))
    ImageDraw.Draw(panel).rounded_rectangle([24, 130, face - 24, 150 + len(lines) * 34], radius=18,
                                            fill=(14, 8, 30, 215), outline=(255, 60, 210, 255), width=3)
    back.alpha_composite(panel)
    bd = ImageDraw.Draw(back)
    size = min(fit(t, 20, face - 80) for t in lines if t)
    for i, t in enumerate(lines):
        bd.text((face // 2, 160 + i * 34), t, font=font(size), fill=(235, 235, 255, 255), anchor="mm")
    sp = Image.new("RGBA", (spine, h), (24, 12, 52, 255))
    sl = Image.new("RGBA", (h, spine), (0, 0, 0, 0))
    ImageDraw.Draw(sl).text((h // 2, spine // 2), NAME, font=font(30), fill=(255, 255, 255, 255), anchor="mm")
    sp.alpha_composite(sl.rotate(270, expand=True))
    ImageDraw.Draw(sp).line([(0, 0), (0, h)], fill=(255, 60, 210, 255), width=3)
    ImageDraw.Draw(sp).line([(spine - 1, 0), (spine - 1, h)], fill=(255, 60, 210, 255), width=3)
    im.alpha_composite(back, (0, 0))
    im.alpha_composite(sp, (face, 0))
    im.alpha_composite(front, (face + spine, 0))
    return im


def background():
    w, h = 1280, 720
    im = sky(w, h)
    glow_text(im, (w // 2, 150), NAME, 120)
    im.alpha_composite(emblem(260), ((w - 260) // 2, 300))
    return im


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, make in (("icon", icon), ("banner", banner), ("boxart", boxart), ("background", background)):
        path = os.path.join(OUT, name + ".png")
        make().convert("RGB" if name != "icon" else "RGBA").save(path, optimize=True)
        print(path)


if __name__ == "__main__":
    main()
