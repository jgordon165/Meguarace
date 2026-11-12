#!/usr/bin/env python3
"""Builds a Sega Saturn disc image of Meguarace from your own MegaRace CD.

No game data comes with the Saturn build. This tool puts the Saturn
program (IP.BIN and A.BIN, from the `saturn` workflow's zip or your own
`make` in platform/saturn) together with MEGARACE.DAT from your copy of the
game, and writes a disc image:

    python tools/saturn_disc.py --data D:\\ --build meguarace-saturn --out megarace

makes megarace.iso and megarace.cue (one data track, MODE1/2048). Play the
.cue in an emulator, put it on an optical drive emulator (Satiator, Fenrir,
Rhea/Phoebe, MODE), or burn it to a CD-R for a Saturn that boots discs
(a modchip, or Pseudo Saturn Kai on an Action Replay cartridge).

Standard library only, so it runs wherever Python 3.8+ does.

The image is plain ISO 9660 level 1:
  sectors 0-15   the system area: IP.BIN (the Saturn boot header), padded
  sector 16      primary volume descriptor ("SEGA SEGASATURN")
  sector 17      volume descriptor set terminator
  18, 19         path tables (L and M)
  20             root directory
  21...          files, A.BIN first (the BIOS loads the first file as the
                 program; IP.BIN gives its load address)
"""
import argparse
import datetime
import os
import shutil
import struct
import sys

SECTOR = 2048
SYSTEM_SECTORS = 16
FIRST_FILE = 21

# Copied from the player's CD, in this order after the program. Only
# MEGARACE.DAT is required; RACEPRG.EXE carries the default Hall of Fame
# and the car tables the Lua game reads.
GAME_FILES = ["MEGARACE.DAT", "RACEPRG.EXE"]


def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def achars(s, n):
    s = s.upper().encode("ascii", "replace")[:n]
    return s + b" " * (n - len(s))


def dir_date(t):
    return bytes([t.year - 1900, t.month, t.day, t.hour, t.minute, t.second, 0])


def vol_date(t):
    if t is None:
        return b"0" * 16 + b"\x00"
    return t.strftime("%Y%m%d%H%M%S00").encode("ascii") + b"\x00"


def dir_record(name, lba, size, is_dir, t):
    ident = name if isinstance(name, bytes) else name.encode("ascii")
    length = 33 + len(ident)
    if length % 2:
        length += 1
    rec = bytearray(length)
    rec[0] = length
    rec[2:10] = both32(lba)
    rec[10:18] = both32(size)
    rec[18:25] = dir_date(t)
    rec[25] = 2 if is_dir else 0
    rec[28:32] = both16(1)
    rec[32] = len(ident)
    rec[33:33 + len(ident)] = ident
    return bytes(rec)


PAK_MAGIC = b"MGPK"
PAK_NAME = 56


def write_pak(out_path, modules):
    """SCRIPTS.PAK: the game's Lua modules for the Saturn, big-endian.
    u8 magic[4] "MGPK", u32 count, then count entries
    { char name[56] ("race.rules"), u32 offset, u32 size }, then the sources."""
    names = sorted(modules)
    head = PAK_MAGIC + struct.pack(">I", len(names))
    offset = len(head) + 64 * len(names)
    table, blobs = bytearray(), bytearray()
    for name in names:
        data = modules[name]
        enc = name.encode("ascii")
        if len(enc) >= PAK_NAME:
            sys.exit("module name too long for SCRIPTS.PAK: %s" % name)
        table += enc.ljust(PAK_NAME, b"\0") + struct.pack(">II", offset + len(blobs), len(data))
        blobs += data
    with open(out_path, "wb") as f:
        f.write(head + bytes(table) + bytes(blobs))
    return len(names), len(head) + len(table) + len(blobs)


def lua_modules(scripts_dir):
    """Every .lua under scripts/, by module name (core/scenes.lua -> core.scenes)."""
    mods = {}
    for root, _dirs, files in os.walk(scripts_dir):
        for fn in files:
            if fn.endswith(".lua"):
                path = os.path.join(root, fn)
                rel = os.path.relpath(path, scripts_dir)[:-4].replace(os.sep, ".")
                mods[rel] = open(path, "rb").read()
    return mods


def find_file(folder, name):
    """Case-insensitive lookup in one folder (a CD is often lower-cased on Linux)."""
    try:
        for entry in os.listdir(folder):
            if entry.upper() == name.upper():
                path = os.path.join(folder, entry)
                if os.path.isfile(path):
                    return path
    except OSError:
        pass
    return None


def build(files, ip_bin, out_iso, title="MEGUARACE"):
    """files: [(iso_name, source_path)] in disc order; the first is the program."""
    now = datetime.datetime.now().replace(microsecond=0)
    ip = open(ip_bin, "rb").read()
    if len(ip) > SYSTEM_SECTORS * SECTOR:
        sys.exit("IP.BIN is larger than the 32 KiB system area")
    if not ip.startswith(b"SEGA SEGASATURN"):
        sys.exit("%s doesn't look like a Saturn IP.BIN" % ip_bin)

    layout = []
    lba = FIRST_FILE
    for name, path in files:
        size = os.path.getsize(path)
        if size >= 1 << 32:
            sys.exit("%s is too large for ISO 9660" % path)
        layout.append((name, path, lba, size))
        lba += (size + SECTOR - 1) // SECTOR
    total = lba

    # Root directory: ".", "..", then the files sorted by name (ISO 9660
    # requires sorted records; the program, A.BIN, sorts first anyway).
    root = bytearray()
    root += dir_record(b"\x00", 20, SECTOR, True, now)
    root += dir_record(b"\x01", 20, SECTOR, True, now)
    for name, _path, flba, size in sorted(layout, key=lambda e: e[0]):
        root += dir_record(name + ";1", flba, size, False, now)
    if len(root) > SECTOR:
        sys.exit("too many files for one root directory sector")
    root += b"\x00" * (SECTOR - len(root))

    # Path tables: just the root.
    l_path = bytes([1, 0]) + struct.pack("<I", 20) + struct.pack("<H", 1) + b"\x00\x00"
    m_path = bytes([1, 0]) + struct.pack(">I", 20) + struct.pack(">H", 1) + b"\x00\x00"

    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = achars("SEGA SEGASATURN", 32)
    pvd[40:72] = achars(title, 32)
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SECTOR)
    pvd[132:140] = both32(len(l_path))
    pvd[140:144] = struct.pack("<I", 18)
    pvd[148:152] = struct.pack(">I", 19)
    pvd[156:190] = dir_record(b"\x00", 20, SECTOR, True, now)
    pvd[190:318] = achars("", 128)
    pvd[318:446] = achars("MEGUARACE", 128)
    pvd[446:574] = achars("MEGUARACE", 128)
    pvd[574:702] = achars("MEGUARACE SATURN DISC BUILDER", 128)
    pvd[702:739] = achars("", 37)
    pvd[739:776] = achars("", 37)
    pvd[776:813] = achars("", 37)
    pvd[813:830] = vol_date(now)
    pvd[830:847] = vol_date(now)
    pvd[847:864] = vol_date(None)
    pvd[864:881] = vol_date(None)
    pvd[881] = 1

    term = bytearray(SECTOR)
    term[0] = 255
    term[1:6] = b"CD001"
    term[6] = 1

    with open(out_iso, "wb") as o:
        o.write(ip + b"\x00" * (SYSTEM_SECTORS * SECTOR - len(ip)))
        o.write(pvd)
        o.write(term)
        o.write(l_path + b"\x00" * (SECTOR - len(l_path)))
        o.write(m_path + b"\x00" * (SECTOR - len(m_path)))
        o.write(root)
        for name, path, flba, size in layout:
            assert o.tell() == flba * SECTOR
            with open(path, "rb") as f:
                shutil.copyfileobj(f, o, 1 << 20)
            pad = (-size) % SECTOR
            if pad:
                o.write(b"\x00" * pad)
            print("  %-13s %10d bytes at sector %d" % (name, size, flba))
        assert o.tell() == total * SECTOR
    return total


def write_cue(out_cue, iso_name):
    with open(out_cue, "w", newline="\r\n") as c:
        c.write('FILE "%s" BINARY\n' % iso_name)
        c.write("  TRACK 01 MODE1/2048\n")
        c.write("    INDEX 01 00:00:00\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--data", required=True,
                    help="your MegaRace CD (or a folder with a copy of it)")
    ap.add_argument("--build", default=None,
                    help="folder with the Saturn build's IP.BIN and A.BIN "
                         "(default: platform/saturn/build next to this tool)")
    ap.add_argument("--out", default="megarace",
                    help="output name, without extension (default: megarace)")
    ap.add_argument("--scripts", default=None,
                    help="the game's Lua scripts (default: scripts/ next to this tool, "
                         "or the scripts folder of the zip)")
    ap.add_argument("--lua-test", action="store_true",
                    help="also put the Lua test suite (tests/lua/main.lua) on the disc; "
                         "the Saturn runs it at boot and shows the result")
    args = ap.parse_args(argv)

    here = os.path.dirname(os.path.abspath(__file__))
    build_dir = args.build or os.path.join(here, "..", "platform", "saturn", "build")
    ip_bin = find_file(build_dir, "IP.BIN")
    program = find_file(build_dir, "A.BIN")
    if not ip_bin or not program:
        sys.exit("IP.BIN and A.BIN not found in %s: build platform/saturn first, "
                 "or pass --build with the folder from meguarace-saturn.zip" % build_dir)

    files = [("A.BIN", program)]
    for name in GAME_FILES:
        path = find_file(args.data, name)
        if path:
            files.append((name, path))
        elif name == "MEGARACE.DAT":
            sys.exit("MEGARACE.DAT not found in %s" % args.data)
        else:
            print("note: %s not found in %s (optional)" % (name, args.data))
    scripts = args.scripts
    if not scripts:
        for cand in (os.path.join(here, "scripts"), os.path.join(here, "..", "scripts")):
            if os.path.isdir(cand):
                scripts = cand
                break
    if scripts and os.path.isdir(scripts):
        mods = lua_modules(scripts)
        if args.lua_test:
            test = os.path.join(here, "..", "tests", "lua", "main.lua")
            if not os.path.isfile(test):
                test = os.path.join(here, "tests", "lua", "main.lua")
            mods["@test"] = open(test, "rb").read()
        pak = args.out + ".scripts.pak"
        n, size = write_pak(pak, mods)
        files.append(("SCRIPTS.PAK", pak))
        print("SCRIPTS.PAK: %d Lua modules, %d bytes" % (n, size))
    else:
        print("note: no Lua scripts found (pass --scripts); the disc plays videos only")
    for txt in ("ABS.TXT", "BIB.TXT", "CPY.TXT"):
        path = find_file(build_dir, txt)
        if path:
            files.append((txt, path))

    out_iso = args.out + ".iso"
    out_cue = args.out + ".cue"
    print("Writing %s" % out_iso)
    sectors = build(files, ip_bin, out_iso)
    write_cue(out_cue, os.path.basename(out_iso))
    mib = sectors * SECTOR / (1024 * 1024)
    print("Done: %s (%d sectors, %.1f MiB) and %s" % (out_iso, sectors, mib, out_cue))
    if mib > 700:
        print("note: larger than a 700 MB CD-R; fine for an optical drive emulator")


if __name__ == "__main__":
    main()
