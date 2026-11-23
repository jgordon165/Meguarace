"""Draws the port's Wii U Menu artwork from its own icon design (the same
original design as tools/make_aurora_art.py: no game art), in the sizes a
Wii U homebrew bundle (.wuhb) takes:

  icon.png        128 x 128   the Wii U Menu icon
  tv-splash.png   1280 x 720  the TV while the game starts
  drc-splash.png  854 x 480   the GamePad while the game starts

Written to platform/wiiu/ (CMake puts them in meguarace.wuhb).
Run: python3 tools/make_wiiu_art.py
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import make_aurora_art as art  # noqa: E402

OUT = os.path.join(ROOT, "platform", "wiiu")


def icon():
    return art.emblem(128)


def splash(w, h):
    im = art.sky(w, h)
    art.glow_text(im, (w // 2, h * 150 // 720), art.NAME, art.fit(art.NAME, h * 120 // 720, w - 80))
    e = h * 260 // 720
    im.alpha_composite(art.emblem(e), ((w - e) // 2, h * 290 // 720))
    return im


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, make in (("icon", icon), ("tv-splash", lambda: splash(1280, 720)),
                       ("drc-splash", lambda: splash(854, 480))):
        path = os.path.join(OUT, name + ".png")
        make().convert("RGBA" if name == "icon" else "RGB").save(path, optimize=True)
        print(path)


if __name__ == "__main__":
    main()
