# The Sega Saturn port

The Saturn build lives in `platform/saturn/` and is built with
[libyaul](https://github.com/yaul-org/libyaul) (GCC 14.3 for the SH-2). As on
every other platform, no game data is shipped: the player makes the disc
from their own MegaRace CD with `tools/saturn_disc.py`.

## Status

**Milestone 1: the platform layer and video (done).** The Saturn program
boots from the disc, finds `MEGARACE.DAT`, plays the boot sequence
(MINDSCAP, CRYO, INTRO) with its sound, then opens a video explorer that
plays any HNM in the archive. It also detects a RAM cartridge (1 or 4 MiB).
Verified in an emulator on synthetic data (below), not yet on a console with
the real disc.

**Next, in order:**

| | Milestone | What it takes |
|---|---|---|
| 2 | Lua on the Saturn | Lua 5.4 built 32-bit (`LUA_32BITS`: 32-bit integers, single-precision floats; the SH-2 has no FPU), the scripts precompiled and stripped on the PC by the disc builder, a small libc layer (`setjmp`, `strtod`, number formatting). Heap in low work RAM, plus the RAM cart when there is one. Measure memory and speed with the menu scripts. |
| 3 | Engine API | The Lua API of `src/lua_api.cpp` (`gfx`, `audio`, `dat`, `spr`, `hnm`, `input`, `sys`, `fs`) on the platform layer. Classic mode only: no HD packs, 16:9, smoothing, SoundFont or voice filter. Saves in backup RAM. |
| 4 | Menus and career | The main menu, options, Hall of Fame, Lance's clips: mostly the scripts as they are. |
| 5 | Races | Track video at gameplay speed (forward-only streaming fits; the track data chunks `pt`/`kl` come with the frames), cars and dashboards composited onto the picture. Profile on hardware; move hot Lua paths to C. |
| 6 | Music | The six HERAD songs while the CD streams video. Options: an OPL2 emulation on the slave SH-2, or the disc builder converting the songs to SCSP FM sequences for the 68000. |

## Making a disc

```
python tools/saturn_disc.py --data D:\ --build platform/saturn/build --out megarace
```

writes `megarace.iso` and `megarace.cue`: one MODE1/2048 data track holding
IP.BIN (the system area), A.BIN (the program, the first file) and the
player's `MEGARACE.DAT` (and `RACEPRG.EXE` if present, for the game later).
It runs on the emulators, on optical drive emulators, and burned to a CD-R
for a console that boots CD-Rs. `platform/saturn/README-saturn.txt` is the
player's guide; it ships in `meguarace-saturn.zip` (the `saturn` workflow).

## Building

```
platform/saturn/toolchain/build-toolchain.sh      # once: GCC + binutils + libyaul into /opt/tool-chains/sh2eb-elf
source ~/.yaul.env
make -C platform/saturn                           # build/A.BIN, build/IP.BIN
```

The tool-chain script builds the same versions as libyaul's own
crosstool-ng configuration (GCC 14.3.0, binutils 2.44, no C library:
libyaul has its own) and pins libyaul to the commit the port was tested
with. On Windows, libyaul's MSYS2 packages work too.

libyaul's C++ has no exceptions and no standard library, so the engine's
C++ decoders can't be built there. `src/formats/cryo_c.{h,c}` is a
freestanding C version of the DAT, HSQ and HNM decoders (no allocation,
error codes instead of exceptions). `tests/test_formats.cpp` checks it
against the C++ decoders on hand-built inputs and on thousands of randomly
damaged ones.

## How it works

| Part | File | |
|---|---|---|
| Picture | `sat_video.c` | NBG0 is a 512×256 bitmap in 256-colour mode: the DOS game's 8-bit picture as it is. Two pages (VRAM A0, A1) swap at vblank. Palette index 0 is a real colour, so the 12-line borders come from VDP2 window 0, which limits NBG0 to the 200 picture lines; the rest shows the black back screen. |
| Sound | `sat_audio.c` | The 68000 is off. One SCSP slot loops over a 32 KiB ring of 8-bit PCM in sound RAM (about 3 s at 11 kHz), pitched from the stream's rate (44.1 kHz × 2^OCT × (1 + FNS/1024)), and the game writes ahead of it. The play position is counted in vblanks. |
| CD | `sat_cd.c` | The disc's root directory (libyaul cdfs) and the DAT directory. Videos stream: one play command covers the whole entry, and sectors move from the CD block's buffer into a 96 KiB buffer whenever the game asks for data or waits for a frame. |
| Video | `sat_hnm.c` | As the PC player: a video with sound is clocked by it (frame *k* shows once the sound before it has played), a silent one runs at 12.5 fps, and overlay frames build on the previous picture. Sound is queued as far ahead as the CD buffer and the ring allow, so slow reads don't starve the SCSP. |
| Pad | `sat_pad.c` | Port 1, polled at vblank. |

Memory (`sat_mem.h`): high work RAM holds the program, the 320×200 screen
and the decode buffers. Low work RAM holds the CD buffers, and the rest of
it (about 900 KiB) is left for the Lua heap, as is the RAM cartridge.

## Testing without the game or a BIOS

```
tests/saturn/run_emulator_test.sh platform/saturn/build /tmp/saturn-test
```

1. `tests/saturn/make_test_dat.py` writes a synthetic `MEGARACE.DAT`: test
   patterns in MegaRace's own formats (HSQ-packed frames, overlay frames, a
   palette chunk, VOC sound at 11,111 Hz), under the boot videos' names.
2. The disc builder makes an image of it.
3. The image boots in Yabause (the libretro core, with its HLE BIOS: no
   Sega BIOS involved), driven by `tests/saturn/lrhost.c`, a headless
   libretro frontend that saves screenshots and the audio.
4. `tests/saturn/check_emulator.py` checks the result: the 440 Hz tone for
   MINDSCAP's 4.0 s, silence for CRYO, 660 Hz for INTRO; each picture within
   a frame of its sound; index 0 in its own colour and the black borders;
   overlay transparency; the palette change; the explorer at the end.

Yabause needs one patch for libyaul programs (`tests/saturn/patch_yabause.py`):
it returns zeros for 16-bit sector reads from the CD block's DTR register,
which libyaul uses and the hardware supports. `make_test_dat.py --long` adds
a 32-second stress video at about 250 KB/s, more than twice the real game's
average rate (about 94 KB/s). In the emulator it plays with no gaps in the
sound and no drift.

Things only a console (or an accurate emulator with a real BIOS, such as
Mednafen or Ymir) can tell: decode speed of the real videos' method-173
frames on a 28.6 MHz SH-2, and the CD block's timing.
