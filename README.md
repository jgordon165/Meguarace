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
Boyle) straight from `MEGARACE.DAT`, then the **main menu**:

- Arrows move between the eight car monitors and the three buttons; Enter on
  a monitor picks that car and plays its showcase video.
- Green starts the next round. The career is the original's: Lance Boyle
  introduces each round and reacts to how you did; fail to qualify (or get
  wrecked) and it's game over and the Hall of Fame. Season 1 is Novice,
  season 2 Hardened; clean rounds earn the bonus tracks HOLD and TOKY;
  finish season 2 for the finale, after which you can pick any track.
  In a run of clips Enter skips one, Esc skips the rest.
- Yellow shows the **Hall of Fame** (seeded with the defaults stored in your
  `RACEPRG.EXE`). Make it at the end of a career and you type your name.
  Blue (or Esc) opens the **options**: Novice/Hardened, save, load, quit.
- Leave the menu alone for two minutes and Lance butts in; leave it again
  and a demo race runs (any key ends it).
- M turns the music on or off. S cycles the subtitles: off, English (US),
  English (UK), French, German, Italian, Spanish.
- F12 opens the **asset explorer** (every file in the archive: play videos,
  race any track, D for the track debug viewer, V for raw pixels).

**Music.** The disc has no CD audio: MegaRace's music is six FM-synthesizer
songs inside `MEGARACE.DAT`, written for the AdLib / Sound Blaster chip. The
port plays them on an emulated OPL2 chip ([ymfm](https://github.com/aaronsgiles/ymfm)),
each race with the song the original gives that track, and SkyHolder in the
menus. To use a recording instead (say, from a real Sound Blaster), put
`NEWSAN.wav`, `MAEVA.wav`, `DETRITUS.wav`, `LENNY.wav`, `ALARME.wav` or
`PAGA.wav` in a folder and pass it with `--music <folder>`.

Settings, progress and high scores are saved in your user preferences folder
(`meguarace.sav`).

In a race: Up accelerates, Down brakes, Left/Right steer, Space or Ctrl
fires (F1 shows a debug line).
The dashboard is each car's own: speed, energy and armour (SP/EN/DM)
gauges, the THRILL meter, the weapon, the radar with the nearest rival and
the tile scanner, all placed as RACEPRG.EXE places them. Rivals behind you
shoot back; run out of armour and you're destroyed. Drive over the lane tiles for their effects (energy, points, weapons,
super speed, skids, spins). The opponents come one at a time, each with the
original's AI: destroy them all to win the round, or finish the laps with at
most one left and 1200 points to qualify. Each car has its own guns, armour and firepower, read from
the game's car table.

The track viewer drives along a track video with the game's own cars: your
car (with its dashboard) and three rivals that hide behind scenery the way
they do in the game. Up/Down for speed, Left/Right to steer, C to change your
car, R to change the rivals, and E/T/M/H to toggle the road edges, lane
tiles, occlusion mask and info line. Esc or Enter skips a
video. Set `MEGARACE_SKIP_INTRO=1` to skip the boot sequence.

Tested against the US v1.0.1 CD. The disc has no CD audio tracks: speech and
the videos' sound are in the video files, and the in-game music is FM songs
(see above).

Headless test run: `--frames 60 --screenshot out.bmp`.

## HD mode (`release/hd` branch)

Upscaled videos, made on your machine from your own CD by the bundled
`megarace-hd` tool. No HD video is ever committed or shipped, because an
upscaled video is still the game's content. Run `megarace-hd --data V:\`
once, then play as usual (F10 switches HD on and off). Details, options and
how it works: [docs/hd.md](docs/hd.md).

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
| `CI-build` | every push, or manually (choose all / linux / mac-osx / windows) | Linux (GCC, Clang), macOS x64 + arm64, Windows x64 + x86; build and test |
| `code-analysis` | push/PR to `master`, or manually | CodeQL security-and-quality on our C++ |
| `release-all` | manual | runs `release-linux`, `release-windows`, `release-mac-osx` |
| `release-*` | manual | tested build, packaged as `meguarace-<platform>-<version>` (engine, scripts, docs; no game data); Windows PDBs; universal macOS, signed and notarized if the `PROD_MACOS_*` secrets are set |
| `release-hd-*`, `release-hd-all` | manual | the HD builds, `meguarace-hd-<platform>-<version>`: the same plus `megarace-hd` and Real-ESRGAN; never HD videos ([docs/hd.md](docs/hd.md)) |

## Status

| Area | State |
|---|---|
| `MEGARACE.DAT` archive | Done |
| HSQ decompression | Done (all 91 files) |
| HNM video + soundtrack | Done (all 109 videos, 65,234 frames) |
| Track data: camera and road per section, occlusion masks, lane tiles | Done (exact transform from RACEPRG.EXE) |
| Car sprites, dashboards, palettes, rival cars | Done (wheel offsets for side views still to do) |
| FM music: the six HERAD songs on an emulated OPL2, song per track | Done (register output identical to AdPlug's HERAD player on all six) |
| Racing: speed, lanes, lane tiles and their sounds, laps | Done (rules from RACEPRG.EXE; a few tile effects still placeholders) |
| Main menu, car showcase, championship rounds, Hall of Fame, options, save file | Done (layouts matched to the original art; scoring and options layout are the port's own) |
| Weapons: firing, energy cost, per-car guns, hits, damage, explosions | Done (from RACEPRG.EXE) |
| Dashboard: gauges, THRILL, lamps, radar, scanner, opponents, status line | Done (per-car layout tables from RACEPRG.EXE) |
| Rivals shooting back, armour, being destroyed | Done (from RACEPRG.EXE) |
| Rivals one at a time with the original AI, opponents per round, laps, qualifying, scoring | Done (from RACEPRG.EXE) |
| Career: Lance's clips before and after rounds, game over, two seasons, HOLD/TOKY bonus tracks, finale, free track choice | Done (from RACEPRG.EXE) |
| Subtitles in six languages, timed by the videos' own cues | Done (from RACEPRG.EXE) |
| Hall of Fame: seven places, your run's line, name entry | Done (from RACEPRG.EXE) |
| Attract mode: Lance's idle clip, the demo race | Done (from RACEPRG.EXE; the demo's steering is the port's) |

Format notes: [docs/formats.md](docs/formats.md).

## Layout

```
src/            C++ host: window, framebuffer, audio mixer, FM music (fm.cpp), input, Lua API
src/formats/    C++ decoders: MEGARACE.DAT, HSQ, HNM video, HERAD music
src/hd/         HD mode: .hdv packs, compositing, the megarace-hd converter
scripts/        Lua game code
  main.lua        entry point (game.init/update/draw)
  core/           scene stack, binary reader, UI helpers, asset manifest
  formats/        track.lua (sections, projection, masks, lane tiles), cars.lua (car sprites,
                  dashboards, rivals), voc.lua (sound effects); sniff.lua identifies files
  race/           rules.lua: race rules from RACEPRG.EXE (pure Lua, unit-tested)
  game/           data.lua (track names, default Hall of Fame from your EXE), save.lua,
                  music.lua (which song plays where)
  ui/             font.lua (the game's sprite fonts)
  scenes/         menu, race, hall, options, video player, track viewer, explorer, hex view,
                  raw image view, no-data screen
tools/          identify_exe.py, cryo.py (archive/HSQ), hnm.py (reference video decoder)
docs/           formats.md: everything verified about the file formats
re/             Ghidra headless pipeline (output stays local, git-ignored)
```

## Lua API

| Table | Functions |
|---|---|
| `fs` | `mounted() root() exists(p) size(p) read(p) read_range(p, off, n) list(dir)`: paths relative to your game folder, case-insensitive |
| `gfx` | 320×200 indexed framebuffer: `width() height() clear(i) pset(x,y,i) pget(x,y) fill(x,y,w,h,i) line(x0,y0,x1,y1,i) clip(lines[,yoff]) unclip() blit(x,y,w,h,pixels[,key]) text(x,y,s[,i]) palette(rgb[,first[,sixbit]]) setcolor(i,r,g,b) screenshot(path)` |
| `audio` | `pcm(bytes,rate[,bits,ch,vol,loop]) stop(id) stop_all()`, streaming `stream_reset() stream_push(bytes,rate) stream_queued() stream_played()`, FM songs `song(bytes[,loop]) stop_song() fade_song([s]) song_playing() song_volume(v)`, optional `music(name)` for WAV recordings |
| `dat` | `open([path]) list() exists(name) size(name) read(name[,unpack=true]) read_range(name,off,n)`: files inside `MEGARACE.DAT` |
| `spr` | `open(name)` → sprite set with `:count() :size(i) :apply_palette([shift]) :draw(i,x,y[,scale[,flip]]) :pixels(i)` (index 0 transparent) |
| `hnm` | `open(name)` → video with `:frames() :sound_rate() :decode(i) :chunks(i) :draw([x,y]) :apply_palette() :pixels() :sound() :hd() :blit(i,x,y,w,h,pixels[,key])`; `decode` returns `{w,h,x,y,overlay,video,palette_changed,sound_bytes,pt,kl}` |
| `input` | `down(key) pressed(key) released(key) any()`: SDL key names (`"Left"`, `"Return"`, `"Space"`, `"A"`) |
| `sys` | `ticks() quit([code]) log(v) save(name, text) load(name)` and `data_path music_path music_mounted hd_scale hd_path` |

