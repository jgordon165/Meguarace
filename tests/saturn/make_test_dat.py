#!/usr/bin/env python3
"""Writes a synthetic MEGARACE.DAT for testing the Saturn build without the game.

The videos are test patterns made here (colour bars, a moving square, a
440 Hz tone), in MegaRace's own formats: the DAT archive, HNM (1) videos
with HSQ-packed frames, overlay frames, palette chunks and VOC sound. They
use the real archive names the Saturn program looks for (MINDSCAP.HNM,
CRYO.HNM, INTRO.HNM) so the boot sequence runs. Nothing from the game.

    python tests/saturn/make_test_dat.py out_folder [--long]

--long adds TOKY.HNM, a 32-second stress test for CD streaming.

Each video's pixels follow a rule tests/saturn/check_emulator.py checks on
the emulator's screenshots.
"""
import math
import os
import struct
import sys

W, H = 320, 200
RATE = 11111                      # VOC sample-rate byte 0xA6
SAMPLES_PER_FRAME = 889           # 12.5 fps


class HsqWriter:
    """HSQ (Cryo LZ77) encoder: the mirror of the decoder's bit queue."""

    def __init__(self):
        self.out = bytearray()
        self.slot = 0
        self.used = 16

    def bit(self, b):
        if self.used == 16:
            self.slot = len(self.out)
            self.out += b"\x00\x00"
            self.used = 0
        if b:
            w = self.out[self.slot] | self.out[self.slot + 1] << 8
            w |= 1 << self.used
            self.out[self.slot] = w & 0xFF
            self.out[self.slot + 1] = w >> 8
        self.used += 1

    def literal(self, v):
        self.bit(1)
        self.out.append(v)

    def match(self, count, offset):          # count 3..257, offset -1..-8192
        self.bit(0)
        self.bit(1)
        c = count - 2
        w = (offset + 8192) << 3
        if c <= 7:
            w |= c
            self.out += bytes([w & 0xFF, w >> 8])
        else:
            self.out += bytes([w & 0xFF, w >> 8, c])

    def finish(self, unpacked):
        self.bit(0)
        self.bit(1)
        self.out += b"\x00\x00\x00"
        hdr = bytearray([unpacked & 0xFF, unpacked >> 8, 0, 0, 0, 0])
        packed = len(self.out) + 6
        hdr[3] = packed & 0xFF
        hdr[4] = (packed >> 8) & 0xFF
        hdr[5] = (0xAB - sum(hdr[:5])) & 0xFF
        return bytes(hdr) + bytes(self.out)


def hsq(data, row=W):
    """Greedy: runs of one byte (offset -1), repeats of the previous row, literals."""
    w = HsqWriter()
    i, n = 0, len(data)
    while i < n:
        best_len, best_off = 0, 0
        if i >= 1:
            k = 0
            while i + k < n and k < 257 and data[i + k] == data[i - 1]:
                k += 1
            if k > best_len:
                best_len, best_off = k, -1
        if i >= row:
            k = 0
            while i + k < n and k < 257 and data[i + k] == data[i - row + k]:
                k += 1
            if k > best_len:
                best_len, best_off = k, -row
        if best_len >= 3:
            w.match(best_len, best_off)
            i += best_len
        else:
            w.literal(data[i])
            i += 1
    return w.finish(n)


def chunk(tag, body):
    return tag + struct.pack("<H", len(body) + 4) + body


def video_chunk(w, h, mode, data):
    return struct.pack("<I", (w & 0x1FF) | h << 16 | mode << 24) + data


def palette_block(ranges):
    out = bytearray()
    for start, colours in ranges:
        out += bytes([start, len(colours) & 0xFF])
        for r, g, b in colours:
            out += bytes([r, g, b])
    out += b"\xff\xff"
    return bytes(out)


def voc_start(samples):
    head = bytearray(b"Creative Voice File\x1a")
    head += struct.pack("<HHH", 26, 0x010A, 0x1129)
    block = bytes([1]) + struct.pack("<I", len(samples) + 2)[:3] + bytes([0xA6, 0])
    return bytes(head) + block + samples


def tone(start, n, freq=440.0):
    return bytes(int(128 + 90 * math.sin(2 * math.pi * freq * (start + i) / RATE)) for i in range(n))


def hnm(frames, pal_ranges):
    """frames: list of superchunk bodies (without their u16 size)."""
    header_pal = palette_block(pal_ranges)
    header = struct.pack("<H", 2 + len(header_pal)) + header_pal
    out = bytearray(header)
    for body in frames:
        if len(body) + 2 > 0xFFFF:
            raise ValueError("superchunk too large: %d" % len(body))
        out += struct.pack("<H", len(body) + 2) + body
    return bytes(out)


# ---- the pictures -----------------------------------------------------------

BARS = 8


def bars_frame(k):
    """Eight vertical bars (indices 1..8) scrolling 4 px per frame, a white
    (index 15) 32x32 square moving down-right, index 0 on the bottom 8 rows."""
    px = bytearray(W * H)
    for y in range(H):
        for x in range(W):
            px[y * W + x] = 1 + ((x + 4 * k) // 40) % BARS
    sx, sy = (k * 6) % (W - 32), (k * 3) % (H - 40)
    for y in range(sy, sy + 32):
        px[y * W + sx: y * W + sx + 32] = b"\x0f" * 32
    for y in range(H - 8, H):
        px[y * W:(y + 1) * W] = bytes(W)
    return bytes(px)


BAR_COLOURS = [(63, 0, 0), (63, 32, 0), (63, 63, 0), (0, 63, 0),
               (0, 63, 63), (0, 0, 63), (40, 0, 63), (63, 0, 40)]


def mindscap():
    pal = [(0, [(0, 0, 20)] + BAR_COLOURS), (15, [(63, 63, 63)])]
    frames = []
    for k in range(50):
        snd = tone(k * SAMPLES_PER_FRAME, SAMPLES_PER_FRAME)
        body = chunk(b"sd", voc_start(snd) if k == 0 else snd)
        body += video_chunk(W, H, 0xFE, hsq(bars_frame(k)))
        frames.append(body)
    return hnm(frames, pal)


def cryo():
    """Silent: a full grey ramp frame, then overlay squares with holes."""
    ramp = [(i // 4, i // 4, i // 4) for i in range(256)]
    ramp[0] = (0, 0, 40)                     # index 0: blue, must show in the picture
    first = bytearray(W * H)
    for y in range(H):
        for x in range(W):
            first[y * W + x] = (x * 255) // (W - 1) if y >= 20 else 0
    frames = [video_chunk(W, H, 0xFE, hsq(bytes(first)))]
    for k in range(1, 25):
        sq = bytearray([255] * (40 * 40))
        for y in range(15, 25):                  # a transparent band through it
            sq[y * 40:(y + 1) * 40] = bytes(40)
        x, y = 10 + k * 10, 60
        frames.append(video_chunk(40, 40, 0xFF, hsq_overlay(bytes(sq), x, y, 40, 40)))
    return hnm(frames, [(0, ramp)])


def hsq_overlay(px, x, y, w, h):
    """An HSQ overlay frame: x, y as u16 then PackBits pixels (CREDIT.HNM style)."""
    pb = bytearray()
    i = 0
    while i < len(px):
        j = i
        while j < len(px) and j - i < 128 and px[j] == px[i]:
            j += 1
        if j - i >= 2:
            pb += bytes([257 - (j - i), px[i]])
            i = j
        else:
            j = i
            while j < len(px) and j - i < 128 and (j + 1 >= len(px) or px[j] != px[j + 1]):
                j += 1
            j = max(j, i + 1)
            pb += bytes([j - i - 1]) + px[i:j]
            i = j
    raw = struct.pack("<HH", x, y) + bytes(pb)
    assert len(raw) < w * h, "PackBits must be smaller than the frame for the overlay form"
    w8 = HsqWriter()
    for b in raw:
        w8.literal(b)
    return w8.finish(len(raw))


def intro():
    """Sound, and a palette change at frame 10 (bars turn grey)."""
    pal = [(0, [(0, 0, 20)] + BAR_COLOURS), (15, [(63, 63, 63)])]
    frames = []
    for k in range(25):
        snd = tone(k * SAMPLES_PER_FRAME, SAMPLES_PER_FRAME, 660.0)
        body = chunk(b"sd", voc_start(snd) if k == 0 else snd)
        if k == 10:
            body += chunk(b"pl", palette_block([(1, [(32, 32, 32)] * BARS)]))
        body += video_chunk(W, H, 0xFE, hsq(bars_frame(0)))
        frames.append(body)
    return hnm(frames, pal)


def long_video(frames=400):
    """A stress test for streaming: 32 s at 12.5 fps, each frame with a
    160x100 patch of noise (incompressible: ~20 KB a superchunk, more than
    twice the real game's average rate) over the bars, and a 550 Hz tone.
    The square's position still gives the frame number."""
    import random
    rnd = random.Random(7)
    pal = [(0, [(0, 0, 20)] + BAR_COLOURS), (15, [(63, 63, 63)]),
           (16, [(i % 64, (i * 7) % 64, (i * 13) % 64) for i in range(200)])]
    out = []
    for k in range(frames):
        px = bytearray(bars_frame(k))
        for y in range(90, 190):
            px[y * W + 150: y * W + 310] = bytes(16 + rnd.randrange(200) for _ in range(160))
        snd = tone(k * SAMPLES_PER_FRAME, SAMPLES_PER_FRAME, 550.0)
        body = chunk(b"sd", voc_start(snd) if k == 0 else snd)
        body += video_chunk(W, H, 0xFE, hsq(bytes(px)))
        out.append(body)
    return hnm(out, pal)


def dat(files):
    count = len(files) + 2          # and two unused slots, as the real table has
    dir_size = 2 + count * 25
    out = bytearray(struct.pack("<H", count))
    offset = dir_size
    entries = bytearray()
    for name, data in files:
        e = name.encode("ascii").ljust(16, b"\0")
        entries += e + struct.pack("<II", len(data), offset) + b"\0"
        offset += len(data)
    entries += bytes(25 * 2)
    out += entries
    for _name, data in files:
        out += data
    return bytes(out)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out_dir = args[0] if args else "."
    os.makedirs(out_dir, exist_ok=True)
    files = [("MINDSCAP.HNM", mindscap()), ("CRYO.HNM", cryo()), ("INTRO.HNM", intro()),
             ("PANEL2.HSQ", HsqWriter().finish(0))]
    if "--long" in sys.argv:
        files.append(("TOKY.HNM", long_video()))
    data = dat(files)
    with open(os.path.join(out_dir, "MEGARACE.DAT"), "wb") as f:
        f.write(data)
    for name, d in files:
        print("  %-13s %7d bytes" % (name, len(d)))
    print("MEGARACE.DAT %d bytes" % len(data))


if __name__ == "__main__":
    main()
