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

**Milestone 2: Lua on the Saturn (the VM: done).** Lua 5.4.7 runs on the
SH-2, built 32-bit (`LUA_32BITS`: 32-bit integers and single-precision
floats; the SH-2 has no FPU). The disc builder packs `scripts/` into
`SCRIPTS.PAK` and `require` loads modules from it. The heap is the free part
of low work RAM (896 KiB) plus the RAM cart (4 MiB) when one is plugged in.
The PC's Lua test suite runs on it (a disc built with `--lua-test`): 42 of
the game's modules, 51 tests, in about 3 s. They use 373 KiB of heap (470
KiB at the peak), so a stock Saturn has room for them. Its output is
byte-for-byte what the same suite prints on the PC with the same set-up,
and the 32-bit numbers change nothing in it. The 29 checks that fail on
both are the display tests: the engine API is only stand-ins on the Saturn
so far.

Still to do for milestone 2: precompile the scripts on the PC (a luac that
writes the SH-2's byte order and 32-bit sizes) to save the parse time and
memory at boot, and profile the menu and race scripts on hardware.

**Next, in order:**

| | Milestone | What it takes |
|---|---|---|
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
player's guide; it ships in `meguarace-saturn-<version>.zip` (below).

## Building

```
platform/saturn/toolchain/build-toolchain.sh      # once: GCC + binutils + libyaul into /opt/tool-chains/sh2eb-elf
source ~/.yaul.env
make -C platform/saturn                           # build/A.BIN, build/IP.BIN
make -C platform/saturn test                      # boot it in an emulator on synthetic data and check it
make -C platform/saturn package                   # dist/meguarace-saturn-<version>.zip, what releases ship
make -C platform/saturn disc DATA=/mnt/d          # megarace.iso + .cue from your own CD (LUA_TEST=1 adds the Lua tests)
```

The tool-chain script builds the same versions as libyaul's own
crosstool-ng configuration (GCC 14.3.0, binutils 2.44, no C library:
libyaul has its own), into `$PREFIX` (default `/opt/tool-chains/sh2eb-elf`),
and pins libyaul to the commit the port was tested with. It needs a C/C++
compiler, make, texinfo, flex, bison, xorriso and the GMP, MPFR and MPC
headers (Ubuntu: `build-essential texinfo flex bison xorriso libgmp-dev
libmpfr-dev libmpc-dev`; `python3-numpy python3-pil` for `make test`), and
takes about half an hour. `make` fetches the third-party code itself
(`deps.sh`).

On Windows, build in WSL (Ubuntu): the same commands. The disc builder
itself runs on Windows' own Python, so only the build needs WSL. libyaul's
MSYS2 packages work too, for the build without `make test`.

### The pipeline

| Workflow | What it does for the Saturn |
|---|---|
| `CI-build` (job `saturn`) | every push; or manually, platform `saturn`. Tool-chain, build, `make test` (emulator), `make package`. Keeps the zip, and the emulator's screenshots and sound for a failed test. |
| `release-saturn` | manual, or from `release-all`. The same, with the release's version: `meguarace-saturn-<version>.zip`. |

Two composite actions hold the shared steps:
`.github/actions/setup-saturn` (host packages; the tool-chain, cached by
the hash of `build-toolchain.sh`, so it's built once, about 25 minutes,
and restored in seconds after that; the third-party code, cached by
`deps.sh`) and `.github/actions/test-saturn` (Yabause, pinned, patched
and cached; `make test`). Changing a version means editing the script
that pins it: the caches follow.

Lua needs more C library than libyaul has. `platform/saturn/libc/` fills
the gaps: `setjmp`/`longjmp` in SH-2 assembly; `strtod`/`strtof`; the "C"
locale; and headers for musl's single-precision maths (`math.h`,
`endian.h`). `deps.sh` fetches the third-party code at pinned versions:
Lua 5.4.7, Eyal Rozenberg's printf (libyaul's `printf` has no floating
point: `%f` is 16.16 fixed point) and musl's float maths. All three are
MIT-licensed.

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
| Lua | `sat_lua.c` | The Lua state, its TLSF heap, `require` from `SCRIPTS.PAK`, stand-ins for the engine API, and the test runner for `--lua-test` discs. |

Memory (`sat_mem.h`): high work RAM holds the program (about 240 KiB of
code with Lua), the 320×200 screen and the decode buffers. Low work RAM
holds the CD buffers, and the rest of it (896 KiB) is the Lua heap, together
with the RAM cartridge when one is plugged in.

## Testing without the game or a BIOS

```
make -C platform/saturn test        # = tests/saturn/run_emulator_test.sh build test-run
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

5. The same image built with `--lua-test` runs the PC's Lua test suite on
   the Saturn. The Saturn draws a hash of everything Lua printed on its
   result screen, and the check compares it with the same suite run on the
   PC with the same set-up (`tests/saturn/lua_stub_runner.c`, built from
   the Saturn's copy of Lua).

Yabause needs one patch for libyaul programs (`tests/saturn/patch_yabause.py`):
it returns zeros for 16-bit sector reads from the CD block's DTR register,
which libyaul uses and the hardware supports. `make_test_dat.py --long` adds
a 32-second stress video at about 250 KB/s, more than twice the real game's
average rate (about 94 KB/s). In the emulator it plays with no gaps in the
sound and no drift.

Things only a console (or an accurate emulator with a real BIOS, such as
Mednafen or Ymir) can tell: decode speed of the real videos' method-173
frames on a 28.6 MHz SH-2, and the CD block's timing.
