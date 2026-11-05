#!/usr/bin/env python3
"""Reference decoder for MegaRace sprite sets (see docs/formats.md, Graphics).

  python3 -I tools/sprites.py list  <MEGARACE.DAT> JO_A
  python3 -I tools/sprites.py sheet <MEGARACE.DAT> JO_A out.png [extra palette files...]

`sheet` needs Pillow. Pixels with colour 0 are shown magenta. Rival sets
(_JOS etc.) carry no palette: pass the matching <XX>_A to borrow its colours
(moved up by 64), e.g.  sheet DAT _JOS out.png JO_A
"""
import os
import struct
import sys
import importlib.util

_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("cryo", os.path.join(_here, "cryo.py"))
cryo = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cryo)


def packbits(d, want):
    out, p = bytearray(), 0
    while p < len(d) and len(out) < want:
        b = d[p]; p += 1
        if b & 0x80:
            if p >= len(d):
                break
            out += bytes([d[p]]) * (257 - b); p += 1
        else:
            out += d[p:p + b + 1]; p += b + 1
    return bytes(out[:want]).ljust(want, b"\0")


def load(data):
    """-> (palette_block, [ (w, h, pixels) | None ])"""
    tbl = struct.unpack_from("<H", data, 0)[0]
    first = struct.unpack_from("<H", data, tbl)[0]
    n = first // 2
    offs = [tbl + struct.unpack_from("<H", data, tbl + 2 * i)[0] for i in range(n)]
    images = []
    for i, o in enumerate(offs):
        end = offs[i + 1] if i + 1 < n else len(data)
        wf, h = struct.unpack_from("<HB", data, o)
        w = wf & 0x7FFF
        body = data[o + 4:end]
        if w == 0 or h == 0:
            images.append(None)
        elif wf & 0x8000:
            images.append((w, h, packbits(body, w * h)))
        elif len(body) >= w * h:
            images.append((w, h, body[:w * h]))
        else:
            images.append(None)
    return data[2:tbl], images


def apply_palette(pal, block, shift=0):
    i = 0
    while i + 2 <= len(block):
        s, c = block[i], block[i + 1]; i += 2
        if s == 0xFF and c == 0xFF:
            break
        c = c or 256
        for k in range(c):
            if i + 3 > len(block):
                break
            at = s + k + (shift if s >= 128 else 0)
            if 0 < at < 256:
                pal[at] = tuple((v & 63) * 255 // 63 for v in block[i:i + 3])
            i += 3


def read_unpacked(dat, entries, name):
    if "." not in name:
        name += ".HSQ"
    e = entries[name.upper()]
    with open(dat, "rb") as f:
        f.seek(e["offset"])
        d = f.read(e["size"])
    return cryo.unhsq(d) if cryo.is_hsq(d) else d


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    cmd, dat, name = argv[1], argv[2], argv[3]
    entries = {e["name"].upper(): e for e in cryo.read_dir(dat)}
    block, images = load(read_unpacked(dat, entries, name))
    if cmd == "list":
        print(f"{name}: {len(images)} entries, palette block {len(block)} bytes")
        for i, im in enumerate(images):
            print(f"  {i:3d}  " + (f"{im[0]}x{im[1]}" if im else "data"))
    elif cmd == "sheet":
        from PIL import Image
        pal = [(i, i, i) for i in range(256)]
        apply_palette(pal, block)
        for extra in argv[5:]:
            eb, _ = load(read_unpacked(dat, entries, extra))
            apply_palette(pal, eb, 64 if name.startswith("_") else 0)
        ims = [im for im in images if im]
        W = 640
        x = y = rowh = 0
        pos = []
        for w, h, _ in ims:
            if x + w > W:
                x, y, rowh = 0, y + rowh + 2, 0
            pos.append((x, y)); x += w + 2; rowh = max(rowh, h)
        sheet = Image.new("RGB", (W, y + rowh), (40, 40, 40))
        for (w, h, px), p in zip(ims, pos):
            im = Image.new("RGB", (w, h))
            im.putdata([pal[c] if c else (255, 0, 255) for c in px])
            sheet.paste(im, p)
        sheet.save(argv[4])
        print(f"wrote {argv[4]} ({len(ims)} images)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
