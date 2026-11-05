# MegaRace port

[![CI-build](https://github.com/jgordon165/Meguarace/actions/workflows/ci-build.yml/badge.svg)](https://github.com/jgordon165/Meguarace/actions/workflows/ci-build.yml)
[![code-analysis](https://github.com/jgordon165/Meguarace/actions/workflows/code-analysis.yml/badge.svg)](https://github.com/jgordon165/Meguarace/actions/workflows/code-analysis.yml)

A modern reimplementation host for Cryo's *MegaRace* (1993, DOS): a C++ engine
(SDL2) running game logic written in Lua.

**No original game code or data is included.** The engine loads every asset at
runtime from the player's own copy of the game, the same way ScummVM and
OpenRCT2 work. Original files are git-ignored as a safety net.

## Build

Requires CMake 3.20+ and a C++20 compiler. SDL2 and Lua 5.4 are downloaded
and built automatically.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Windows (Visual Studio 2022): the same commands work from a Developer prompt,
or open the folder in Visual Studio.

## Run

Point the engine at your MegaRace CD (a mounted disc image works too), or a
folder holding a copy of it. Only `MEGARACE.DAT` is required:

```
build\Release\megarace.exe --data V:\
```

It plays the original boot sequence (Mindscape, Cryo, the intro with Lance
Boyle) straight from `MEGARACE.DAT`, then opens the **asset explorer**: every
file inside the archive. Enter plays a video, opens a race track in the
**track viewer**, or hex-views a file (HSQ files shown decompressed); V shows
a file as raw 8-bit pixels.

The track viewer drives along a track video using its decoded data: Up/Down
for speed, Left/Right for lane, A/Z to move the stand-in car further ahead
or closer, and E/T/M to toggle the road edges, lane tiles and occlusion mask
overlays. Esc or Enter skips a
video. Set `MEGARACE_SKIP_INTRO=1` to skip the boot sequence.

Tested against the US v1.0.1 CD. The disc has no CD audio tracks; music and
speech are inside the videos.

Headless test run: `--frames 60 --screenshot out.bmp`.

## Tests and CI

```
ctest --test-dir build -C Release --output-on-failure
```

runs the format decoders against hand-built inputs, the Lua modules (run by
the engine headless), and a boot check. None of them need game data. To
also run the tests against your CD, set `MEGARACE_DATA` before configuring:

```
MEGARACE_DATA=V:\ cmake -S . -B build      # PowerShell: $env:MEGARACE_DATA="V:\"
```

GitHub Actions (same layout as the Aleph One fork):

| Workflow | When | What |
|---|---|---|
| `CI-build` | every push | Linux (GCC, Clang), macOS x64 + arm64, Windows x64 + x86; build and test |
| `code-analysis` | push/PR to `master` | CodeQL security-and-quality on our C++ |
| `release-all` | manual | runs `release-linux`, `release-windows`, `release-mac-osx` |
| `release-*` | manual | tested build, packaged as `meguarace-<platform>-<version>` (engine, scripts, docs; no game data); Windows PDBs; universal macOS, signed and notarized if the `PROD_MACOS_*` secrets are set |

## Status

| Area | State |
|---|---|
| `MEGARACE.DAT` archive | Done |
| HSQ decompression | Done (all 91 files) |
| HNM video + soundtrack | Done (all 109 videos, 65,234 frames) |
| Track data: camera and road per section, occlusion masks, lane tiles | Done (exact transform from RACEPRG.EXE) |
| Sprites / UI graphics (HSQ contents), fonts, subtitles, VOC effects | To do |
| Menus, racing, AI, weapons | To do (Lua) |

Format notes: [docs/formats.md](docs/formats.md).

## Layout

```
src/            C++ host: window, framebuffer, audio mixer, input, Lua API
src/formats/    C++ decoders: MEGARACE.DAT, HSQ, HNM video
scripts/        Lua game code
  main.lua        entry point (game.init/update/draw)
  core/           scene stack, binary reader, UI helpers, asset manifest
  formats/        track.lua (sections, projection, masks, lane tiles); sniff.lua identifies files
  scenes/         video player, track viewer, explorer, hex view, raw image view, no-data screen
tools/          identify_exe.py, cryo.py (archive/HSQ), hnm.py (reference video decoder)
docs/           formats.md: everything verified about the file formats
re/             Ghidra headless pipeline (output stays local, git-ignored)
```

## Lua API

| Table | Functions |
|---|---|
| `fs` | `mounted() root() exists(p) size(p) read(p) read_range(p, off, n) list(dir)`: paths relative to your game folder, case-insensitive |
| `gfx` | 320×200 indexed framebuffer: `width() height() clear(i) pset(x,y,i) pget(x,y) fill(x,y,w,h,i) line(x0,y0,x1,y1,i) blit(x,y,w,h,pixels[,key]) text(x,y,s[,i]) palette(rgb[,first[,sixbit]]) setcolor(i,r,g,b) screenshot(path)` |
| `audio` | `pcm(bytes,rate[,bits,ch,vol,loop]) stop(id) stop_all()`, streaming `stream_reset() stream_push(bytes,rate) stream_queued() stream_played()`, optional `music(track)` for WAV tracks |
| `dat` | `open([path]) list() exists(name) size(name) read(name[,unpack=true]) read_range(name,off,n)`: files inside `MEGARACE.DAT` |
| `hnm` | `open(name)` → video with `:frames() :sound_rate() :decode(i) :chunks(i) :draw([x,y]) :apply_palette() :pixels() :sound()`; `decode` returns `{w,h,x,y,overlay,video,palette_changed,sound_bytes,pt,kl}` |
| `input` | `down(key) pressed(key) released(key)`: SDL key names (`"Left"`, `"Return"`, `"Space"`, `"A"`) |
| `sys` | `ticks() quit() log(v)` and `data_path music_path music_mounted` |

