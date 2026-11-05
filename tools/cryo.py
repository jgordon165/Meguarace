#!/usr/bin/env python3
"""Analysis helpers for Cryo's MEGARACE.DAT archive and HSQ compression.

Reads from YOUR copy of the game. Extracted files go to a local folder for
study; they are never part of the port's repository.

  python3 -I tools/cryo.py list    <MEGARACE.DAT>             # directory
  python3 -I tools/cryo.py stats   <MEGARACE.DAT>             # counts by extension
  python3 -I tools/cryo.py extract <MEGARACE.DAT> <outdir> [glob] [--unpack]

DAT layout (little endian):
  u16 count
  count x { char name[16] (NUL padded); u32 size; u32 offset; u8 flag }
  file data at absolute offsets

HSQ layout:
  6-byte header: u16 unpacked_size, u8 0, u16 packed_size, u8 checksum
  (the six header bytes sum to 0xAB mod 256), then an LZ77 stream driven
  by a 16-bit little-endian bit queue.
"""
import fnmatch
import os
import struct
import sys
from collections import Counter


def read_dir(path):
    with open(path, "rb") as f:
        (count,) = struct.unpack("<H", f.read(2))
        raw = f.read(count * 25)
    entries = []
    for i in range(count):
        name, size, offset, flag = struct.unpack_from("<16sIIB", raw, i * 25)
        name = name.split(b"\0", 1)[0].decode("ascii", "replace")
        if name:
            entries.append({"name": name, "size": size, "offset": offset, "flag": flag})
    return entries


def is_hsq(data):
    return len(data) >= 6 and data[2] == 0 and (sum(data[:6]) & 0xFF) == 0xAB


def unhsq(data):
    """Decompress an HSQ buffer. Raises ValueError on a malformed stream."""
    if not is_hsq(data):
        raise ValueError("not an HSQ stream")
    unpacked = data[0] | data[1] << 8
    src = memoryview(data)
    pos = 6
    out = bytearray()
    queue = 0

    def bit():
        nonlocal queue, pos
        b = queue & 1
        queue >>= 1
        if queue == 0:
            queue = src[pos] | src[pos + 1] << 8
            pos += 2
            b = queue & 1
            queue = (queue >> 1) | 0x8000
        return b

    while True:
        if bit():
            out.append(src[pos]); pos += 1
            continue
        if bit():
            word = src[pos] | src[pos + 1] << 8
            pos += 2
            count = word & 7
            offset = (word >> 3) - 8192
            if count == 0:
                count = src[pos]; pos += 1
                if count == 0:
                    break
            count += 2
        else:
            count = (bit() << 1 | bit()) + 2
            offset = src[pos] - 256
            pos += 1
        start = len(out) + offset
        if start < 0:
            raise ValueError("back-reference before start of output")
        for i in range(count):
            out.append(out[start + i])
    if unpacked and len(out) != unpacked:
        # sizes > 64K wrap in the 16-bit header; only flag a real mismatch
        if len(out) & 0xFFFF != unpacked:
            raise ValueError(f"size mismatch: header {unpacked}, got {len(out)}")
    return bytes(out)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd, dat = argv[1], argv[2]
    entries = read_dir(dat)
    if cmd == "list":
        for e in entries:
            print(f"{e['offset']:10d} {e['size']:10d} {e['flag']:3d}  {e['name']}")
    elif cmd == "stats":
        c = Counter(os.path.splitext(e["name"])[1].upper() for e in entries)
        sz = Counter()
        for e in entries:
            sz[os.path.splitext(e["name"])[1].upper()] += e["size"]
        print(f"{len(entries)} files")
        for ext, n in c.most_common():
            print(f"  {ext or '(none)':6s} {n:5d} files {sz[ext] / 1048576:8.1f} MB")
    elif cmd == "extract":
        out = argv[3]
        pat = argv[4] if len(argv) > 4 and not argv[4].startswith("--") else "*"
        unpack = "--unpack" in argv
        os.makedirs(out, exist_ok=True)
        with open(dat, "rb") as f:
            for e in entries:
                if not fnmatch.fnmatch(e["name"].upper(), pat.upper()):
                    continue
                f.seek(e["offset"])
                data = f.read(e["size"])
                name = e["name"]
                if unpack and is_hsq(data):
                    try:
                        data = unhsq(data)
                        name = os.path.splitext(name)[0] + ".UNP"
                    except ValueError as ex:
                        print(f"  {name}: {ex}")
                with open(os.path.join(out, name), "wb") as o:
                    o.write(data)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
