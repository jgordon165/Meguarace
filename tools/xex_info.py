"""Prints what an .xex says about itself: its Title ID (hex and as letters),
media ID, version and module flags, from the XEX2 header's execution info
(optional header 0x00040006).
Run: python3 tools/xex_info.py megarace.xex
"""
import struct
import sys

EXECUTION_INFO = 0x00040006


def info(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, flags, _pe, _res, _sec, count = struct.unpack_from(">4sIIIII", data, 0)
    if magic != b"XEX2":
        raise SystemExit(f"{path}: not an XEX2 file ({magic!r})")
    for i in range(count):
        key, value = struct.unpack_from(">II", data, 24 + i * 8)
        if key == EXECUTION_INFO:
            media, version, base, title = struct.unpack_from(">IIII", data, value)
            letters = title.to_bytes(4, "big").decode("ascii", "replace")
            return {"title_id": f"{title:08X}", "letters": letters, "media_id": f"{media:08X}",
                    "version": f"{version:08X}", "base_version": f"{base:08X}", "module_flags": f"{flags:08X}"}
    raise SystemExit(f"{path}: no execution info header")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        r = info(p)
        print(f"{p}: Title ID {r['title_id']} (\"{r['letters']}\"), media ID {r['media_id']}, "
              f"version {r['version']}, module flags {r['module_flags']}")
