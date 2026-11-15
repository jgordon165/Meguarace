#!/usr/bin/env python3
"""Patches the libretro Yabause core for the Saturn tests.

libyaul reads CD sector data 16 bits at a time from the CD block's DTR
register (0x25890000), as the hardware allows. Yabause only serves sector
data through its 32-bit path (0x25818000) and returns zeros at 0x25890000,
so any libyaul program reads blank sectors there. This adds the 16-bit read
on top of the 32-bit path. Test harness only: the Saturn build itself
needs no change.

    python tests/saturn/patch_yabause.py <yabause checkout>/yabause/src/cs2.c
"""
import sys

path = sys.argv[1]
src = open(path, "rb").read().decode("latin-1")
nl = "\r\n" if "\r\n" in src else "\n"
s = src.replace("\r\n", "\n")
marker = "Meguarace test patch"
if marker in s:
    print("already patched")
    sys.exit(0)
old = """u16 FASTCALL Cs2ReadWord(SH2_struct *sh, u32 addr) {
  u16 val = 0;
  addr &= 0xFFFFF; // fix me(I should really have proper mapping)

  switch(addr) {
"""
new = """u16 FASTCALL Cs2ReadWord(SH2_struct *sh, u32 addr) {
  u16 val = 0;
  static int dtr_half = 0;
  static u32 dtr_long = 0;
  addr &= 0xFFFFF; // fix me(I should really have proper mapping)

  /* Meguarace test patch: 16-bit sector data reads from DTR (0x25890000),
   * as libyaul does on real hardware, served from the 32-bit data path. */
  if (addr == 0x90000 && Cs2Area->datatranstype != CDB_DATATRANSTYPE_INVALID) {
    if (!dtr_half) { dtr_long = Cs2ReadLong(sh, 0x18000); dtr_half = 1; return (u16)(dtr_long >> 16); }
    dtr_half = 0;
    return (u16)dtr_long;
  }

  switch(addr) {
"""
if old not in s:
    sys.exit("Cs2ReadWord not found as expected in %s" % path)
s = s.replace(old, new, 1)
open(path, "wb").write(s.replace("\n", nl).encode("latin-1"))
print("patched", path)
