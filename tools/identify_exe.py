#!/usr/bin/env python3
"""Identify a DOS executable before decompiling it.

Reports: real-mode MZ vs. 32-bit LE/LX (DOS extender), known packers
(LZEXE, PKLITE, EXEPACK, ...), the compiler/extender from embedded strings,
and the overlay size (data appended after the load image, common in DOS
games). This decides how Ghidra should load it:

  * exe-mz, unpacked  -> Ghidra "Old-style DOS Executable (MZ)", x86:LE:16:Real Mode
  * exe-le32          -> needs an LE loader (Ghidra has none built in; see
                         re/README.md for the options)
  * packed            -> unpack first (UNP / dump from DOSBox), then re-run this

Usage: python3 -I tools/identify_exe.py <path to the game's EXE>
"""
import struct
import sys

PACKERS = [
    (b"LZ09", "LZEXE 0.90"), (b"LZ91", "LZEXE 0.91"),
    (b"PKLITE", "PKLITE"), (b"PKlite", "PKLITE"),
    (b"RNC", "Rob Northen Compression (data)"),
    (b"diet", "DIET"), (b"WWP", "WWPACK"), (b"UPX!", "UPX"),
    (b"Packed file is corrupt", "Microsoft EXEPACK"),
    (b"aPLib", "aPLib"),
]
TOOLCHAIN = [
    (b"DOS/4G", "DOS/4G(W) extender (Watcom)"),
    (b"WATCOM", "Watcom C/C++"),
    (b"Borland", "Borland C/C++ / Turbo C"),
    (b"Turbo Pascal", "Turbo Pascal"),
    (b"Microsoft C", "Microsoft C"),
    (b"MS Run-Time Library", "Microsoft C runtime"),
    (b"PMODE", "PMODE extender"),
    (b"Phar Lap", "Phar Lap extender"),
    (b"DOS32", "DOS32 extender"),
    (b"CauseWay", "CauseWay extender"),
    (b"GO32", "DJGPP / GO32"),
]


def main(path: str) -> int:
    data = open(path, "rb").read()
    print(f"file      : {path}")
    print(f"size      : {len(data)} bytes")
    if data[:2] not in (b"MZ", b"ZM"):
        print("format    : not an MZ executable")
        return 1

    (e_cblp, e_cp, e_crlc, e_cparhdr, e_minalloc, e_maxalloc,
     e_ss, e_sp, _csum, e_ip, e_cs, e_lfarlc, e_ovno) = struct.unpack_from("<13H", data, 2)
    image = e_cp * 512 - (512 - e_cblp if e_cblp else 0)
    header = e_cparhdr * 16
    print(f"MZ header : {header} bytes, {e_crlc} relocations, entry CS:IP={e_cs:04X}:{e_ip:04X}"
          f", SS:SP={e_ss:04X}:{e_sp:04X}")
    print(f"load image: {image - header} bytes (MZ says total {image})")
    if len(data) > image:
        print(f"overlay   : {len(data) - image} bytes appended after the MZ image")

    kind = "real-mode 16-bit MZ"
    if len(data) >= 0x40:
        off = struct.unpack_from("<I", data, 0x3C)[0]
        if 0 < off < len(data) - 2:
            sig = data[off:off + 2]
            if sig in (b"LE", b"LX"):
                kind = f"32-bit {sig.decode()} (protected mode via DOS extender), header at 0x{off:X}"
            elif sig == b"PE":
                kind = f"PE (Windows / PE-based extender) at 0x{off:X}"
            elif sig == b"NE":
                kind = f"NE (16-bit Windows/OS2) at 0x{off:X}"
    print(f"format    : {kind}")

    found = [name for sig, name in PACKERS if sig in data[:0x4000]]
    print(f"packer    : {', '.join(found) if found else 'none detected'}")
    tools = sorted({name for sig, name in TOOLCHAIN if sig in data})
    print(f"toolchain : {', '.join(tools) if tools else 'unknown'}")

    # Interesting strings help find the asset loaders in the disassembly.
    exts = set()
    for ext in (b".HNM", b".HSQ", b".PAL", b".VOC", b".RAW", b".DAT", b".LBM", b".SPR", b".TRK"):
        if ext in data.upper():
            exts.add(ext.decode())
    if exts:
        print(f"file refs : {', '.join(sorted(exts))} (search these strings in Ghidra)")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
