#!/usr/bin/env python3
"""Reference decoders for MegaRace track data (see docs/formats.md, Track data).

  python3 -I tools/track.py info  <MEGARACE.DAT> NS11       # sections, lap, masks
  python3 -I tools/track.py check <MEGARACE.DAT>            # decode every kl chunk

The engine's version is scripts/formats/track.lua.
"""
import os
import struct
import sys
import importlib.util

_here = os.path.dirname(os.path.abspath(__file__))
def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(_here, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod
cryo = _load("cryo")

TAGS = (b"pl", b"sd", b"pt", b"kl")


def frame_chunks(hnm):
    """Yields {tag: bytes} per frame (game-data chunks only)."""
    hsz = struct.unpack_from("<H", hnm, 0)[0]
    p = hsz
    while p + 2 <= len(hnm):
        fsz = struct.unpack_from("<H", hnm, p)[0]
        if fsz < 2 or p + fsz > len(hnm):
            break
        end, q, out = p + fsz, p + 2, {}
        while q + 4 <= end:
            tag, size = hnm[q:q + 2], struct.unpack_from("<H", hnm, q + 2)[0]
            if tag in TAGS:
                out[tag.decode()] = hnm[q + 4:q + size]
            elif (struct.unpack_from("<H", hnm, q)[0] & 0x1FF) > 320 and size >= 4 and q + size < end:
                out["kl"] = hnm[q + 4:q + size]          # damaged tag, NGL1 frame 2185
            else:
                break
            q += size
        yield out
        p = end


def sections(hnm):
    out = []
    for c in frame_chunks(hnm):
        pt = c.get("pt", b"")
        for off in range(0, len(pt) - 57, 58):
            out.append(struct.unpack_from("<29h", pt, off))
    return out


def project(cam, p):
    """RACEPRG.EXE 0x6D58-0x6FD2. Returns (x, y) on the 320x200 screen or None."""
    dx, dy, dz = p[0] - cam[0], p[1] - cam[1], p[2] - cam[2]
    X = (-dx * cam[3] + dy * cam[4]) / 16384
    Y = (-dx * cam[5] - dy * cam[6] + dz * cam[7]) / 16384
    Z = (-dx * cam[8] - dy * cam[9] - dz * cam[10]) / 16384 + cam[11] / 8
    if Z <= 1:
        return None
    Xr = (cam[13] * X - cam[12] * Y) / 4096
    Yr = (cam[12] * X + cam[13] * Y) / 4096
    return 160 + Xr * 2.3 * 88 / Z, (131 - Yr * 230 / Z) * 200 / 262


def _varint(d, p):
    b = d[p]
    return (d[p + 1] + 256, p + 2) if b == 0 else (b, p + 1)


def decode_kl(kl):
    """-> [ {section, lines: {row: [x0, x1, x0, x1, ...]}, end} ]"""
    layers = []
    for i in range(kl[0] if kl else 0):
        sec, _r0, r1, start, off = struct.unpack_from("<BBBHH", kl, 1 + 8 * i)
        p, row, lines = off, start // 320, {}
        while row <= r1 and p < len(kl):
            op = kl[p]; p += 1
            cnt = (op >> 4) or 16
            if op & 3 == 0:
                row += cnt
                continue
            k = {1: 2, 2: 4, 3: 6}[op & 3]
            left, right = (op >> 2) & 1, (op >> 3) & 1
            for _ in range(cnt):
                xs, x = ([] if left else [0]), 0
                for _ in range(k - (not left) - (not right)):
                    dlt, p = _varint(kl, p); x += dlt; xs.append(x)
                if not right:
                    xs.append(320)
                lines[row] = xs
                row += 1
        layers.append({"section": sec, "lines": lines, "end": p})
    return layers


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    entries = {e["name"]: e for e in cryo.read_dir(argv[2])}
    def read(name):
        e = entries[name]
        with open(argv[2], "rb") as f:
            f.seek(e["offset"])
            return f.read(e["size"])
    if argv[1] == "info":
        name = argv[3].upper()
        hnm = read(name + ".HNM")
        secs = sections(hnm)
        znn = read(name + ".ZNN") if name + ".ZNN" in entries else b""
        lap = len(znn) // 5 if znn else len(secs)
        print(f"{name}: {len(secs)} sections, lap {lap} sections, "
              f"{len(secs) - 99} frames, {sum(1 for b in znn if b)} lane tiles")
    elif argv[1] == "check":
        total = bad = 0
        for name in entries:
            if not name.endswith(".HNM"):
                continue
            for c in frame_chunks(read(name)):
                if "kl" in c and c["kl"]:
                    total += 1
                    if max(L["end"] for L in decode_kl(c["kl"])) != len(c["kl"]):
                        bad += 1
        print(f"{total} kl chunks, {bad} not decoded exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
