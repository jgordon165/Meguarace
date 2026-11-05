#!/usr/bin/env python3
"""Reference decoder for Cryo HNM (1) video as used by MegaRace.

Format notes: https://wiki.multimedia.cx/index.php/HNM_(1)
This Python version is for verifying the format against the player's own
files; the engine's real-time decoder is the C++ port in src/formats/hnm.cpp.

  python3 -I tools/hnm.py info  <file.HNM>
  python3 -I tools/hnm.py dump  <file.HNM> <outdir> [every_n]   # PNG frames
"""
import os
import struct
import sys
import importlib.util

_spec = importlib.util.spec_from_file_location("cryo", os.path.join(os.path.dirname(__file__), "cryo.py"))
cryo = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cryo)


def parse_palette(d, p, pal):
    while True:
        start, count = d[p], d[p + 1]
        p += 2
        if start == 0xFF and count == 0xFF:
            return p
        if count == 0:
            count = 256
        for i in range(count):
            r, g, b = d[p:p + 3]
            p += 3
            if start + i < 256:
                pal[start + i] = ((r & 63) << 2 | (r & 63) >> 4,
                                  (g & 63) << 2 | (g & 63) >> 4,
                                  (b & 63) << 2 | (b & 63) >> 4)


class Reader:
    """Byte stream with an MSB-first 16-bit bit queue and a nibble cache."""

    def __init__(self, data, pos):
        self.d, self.p = data, pos
        self.queue, self.nbits = 0, 0
        self.nib = None

    def byte(self):
        b = self.d[self.p]
        self.p += 1
        return b

    def bit(self):
        if self.nbits == 0:
            self.queue = self.d[self.p] | self.d[self.p + 1] << 8
            self.p += 2
            self.nbits = 16
        self.nbits -= 1
        return (self.queue >> self.nbits) & 1

    def nibble(self, slot=0):
        # The codebook and the raster each keep their own pending low nibble
        # (verified against MegaRace data: a shared cache desynchronises).
        if self.nib is None:
            self.nib = [None, None]
        if self.nib[slot] is None:
            b = self.byte()
            self.nib[slot] = b & 0x0F
            return b >> 4
        n, self.nib[slot] = self.nib[slot], None
        return n


def unpack_173(data, pos):
    """Returns (pixels, x, y, end_pos)."""
    frame_size, cb_size, flags = struct.unpack_from("<HHB", data, pos)
    pos += 6
    x = y = 0
    if not flags & 4:
        x, y = struct.unpack_from("<HH", data, pos)
        pos += 4
    base = 128 if flags & 0x40 else 0
    r = Reader(data, pos)

    cb = bytearray()
    while len(cb) < cb_size:
        tag = r.byte()
        if tag & 0x80:
            temp = r.nibble()
            offset = (tag & 0x7F) << 1 | (temp & 1)
            count = (temp >> 1) + 2
            start = len(cb) - offset - 1
            for i in range(count):
                cb.append(cb[start + i])
        else:
            cb.append(tag + base if tag else 0)

    out = bytearray()
    ci = 0
    long_runs = bool(flags & 0x80)

    def nib_len():
        n = r.nibble(1)
        return (r.byte() + 16 if n == 0 else n) + 4

    while len(out) < frame_size:
        while r.bit() == 0:
            out.append(cb[ci]); ci += 1
            if len(out) >= frame_size:
                break
        if len(out) >= frame_size:
            break
        px = cb[ci]; ci += 1
        if long_runs:
            if r.bit() == 0:
                n = nib_len()
            elif r.bit() == 0:
                n = 2
            elif r.bit() == 0:
                n = 3
            else:
                n = 4
        else:
            if r.bit() == 0:
                n = 2
            elif r.bit() == 0:
                n = 3
            elif r.bit() == 0:
                n = 4
            else:
                n = nib_len()
        out.extend(bytes([px]) * n)
    return bytes(out[:frame_size]), x, y, r.p


def frames(path):
    """Yields dicts: video (w,h,mode,pixels,x,y), chunks {tag: bytes}, palette updates."""
    d = open(path, "rb").read()
    hsz = struct.unpack_from("<H", d, 0)[0]
    pal = [(0, 0, 0)] * 256
    parse_palette(d, 2, pal)
    p = hsz
    while p + 2 <= len(d):
        fsz = struct.unpack_from("<H", d, p)[0]
        if fsz == 0:
            break
        end = p + fsz
        q = p + 2
        chunks = {}
        while True:
            tag = d[q:q + 2]
            hdr = struct.unpack_from("<I", d, q)[0]
            # Typed chunks carry one of a few two-letter tags; anything else is
            # the trailing video chunk (its header bits 12-14 are not always
            # zero in MegaRace, e.g. B01.HNM frame 7, so don't rely on that).
            size = struct.unpack_from("<H", d, q + 2)[0]
            if tag not in (b"pl", b"sd", b"pt", b"kl"):
                # Damaged tag (NGL1.HNM frame 2185): impossible video width,
                # so skip it by its size field.
                if hdr & 0x1FF > 320 and size >= 4 and q + size < end:
                    q += size
                    continue
                break
            body = d[q + 4:q + size]
            chunks.setdefault(tag.decode("latin1"), []).append(body)
            if tag == b"pl":
                parse_palette(body, 0, pal)
            q += size
        w = hdr & 0x1FF
        h = (hdr >> 16) & 0xFF
        mode = hdr >> 24
        vid = None
        if w and h:
            z = q + 4
            method = sum(d[z:z + 6]) & 0xFF
            if method == 0xAD:
                px, x, y, used = unpack_173(d, z)
            elif method == 0xAB:
                px, x, y, used = cryo.unhsq(d[z:end]), 0, 0, end
            else:
                raise ValueError(f"unknown method {method} at {z}")
            vid = dict(w=w, h=h, mode=mode, x=x, y=y, pixels=px, slack=end - used)
        yield dict(video=vid, chunks=chunks, palette=list(pal))
        p = end


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    path = argv[2]
    if argv[1] == "info":
        n = 0
        slack = []
        for f in frames(path):
            n += 1
            v = f["video"]
            if v:
                slack.append(v["slack"])
            if n <= 5 and v:
                print(f"frame {n-1}: {v['w']}x{v['h']} at {v['x']},{v['y']} mode {v['mode']:02X}"
                      f" chunks {sorted(f['chunks'])} slack {v['slack']}")
        print(f"{n} frames; unused bytes per frame: min {min(slack)} max {max(slack)}")
    elif argv[1] == "dump":
        from PIL import Image
        out = argv[3]
        every = int(argv[4]) if len(argv) > 4 else 1
        os.makedirs(out, exist_ok=True)
        screen = bytearray(320 * 200)
        for i, f in enumerate(frames(path)):
            v = f["video"]
            if v:
                for row in range(v["h"]):
                    for col in range(v["w"]):
                        c = v["pixels"][row * v["w"] + col]
                        if v["mode"] == 0xFF and c == 0:
                            continue
                        X, Y = v["x"] + col, v["y"] + row
                        if X < 320 and Y < 200:
                            screen[Y * 320 + X] = c
            if i % every == 0:
                img = Image.frombytes("P", (320, 200), bytes(screen))
                img.putpalette([c for rgb in f["palette"] for c in rgb])
                img.save(os.path.join(out, f"{os.path.basename(path)}_{i:04d}.png"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
