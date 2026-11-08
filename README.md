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

It opens on the port's own title screen: MEGARACE in chrome over a synthwave
night, all drawn by code, with the music playing. ENTER goes on, and ESC
skips the boot videos too. Then it plays the original boot sequence
(Mindscape, Cryo, the intro with Lance Boyle) straight from `MEGARACE.DAT`,
then the **main menu**:

- Arrows move between the car monitors and the three buttons. You start with
  three cars; destroying every opponent on a championship round unlocks
  another. Enter on a monitor races the next round with that car; Space (or
  Esc) plays its showcase video.
- The career is the original's: Lance Boyle
  introduces each round and reacts to how you did; fail to qualify (or get
  wrecked) and it's game over and the Hall of Fame. Season 1 is Novice,
  season 2 Hardened; clean rounds earn the bonus tracks HOLD and TOKY;
  finish season 2 for the finale, after which you can pick any track.
  In a run of clips Enter skips one, Esc skips the rest. Lance's clips
  play through a hiss filter (see below); V switches it off and on while
  one plays, so you can compare, and the choice is saved.
- The left button picks the **level** (Novice or Hardened), which starts a
  new career. The middle one opens the **options**: load, save, input, quit.
  The right one shows the **Hall of Fame** (seeded with the defaults stored
  in your `RACEPRG.EXE`). Make it at the end of a career and you type your name.
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
menus. Options > ADVANCED > MUSIC picks the style: ORIGINAL (the OPL2 as on a
Sound Blaster), STEREO FM (the same FM on an emulated OPL3, its tracks
spread across the stereo field, with EQ and a small room reverb),
SOUNDFONT (the songs' notes on a General MIDI SoundFont via
[TinySoundFont](https://github.com/schellingb/TinySoundFont); the game comes
with [GeneralUser GS](https://www.schristiancollins.com/generaluser.php)) or
ENHANCED, the default (a dance remix on that SoundFont: supersaw leads, acid and sub bass, a
four-on-the-floor kick, sidechain pumping and a delay in time with the song).
Details: [docs/music.md](docs/music.md). To use a recording instead (say, from a real Sound Blaster), put
`NEWSAN.wav`, `MAEVA.wav`, `DETRITUS.wav`, `LENNY.wav`, `ALARME.wav` or
`PAGA.wav` in a folder and pass it with `--music <folder>`.

**Lance's voice.** His clips' 8-bit soundtracks carry a steady hiss (about
−35 dB) under the voice, plus a DC offset and low rumble. The port cleans
them as they play (`src/voice_filter.h`): a high-pass at 80 Hz takes the
offset and rumble, then a spectral noise reduction (a Wiener filter over
512-sample windows) turns the hiss down. It measures each clip's own hiss
over the whole clip before it starts. On the US v1.0.1 disc that leaves the
hiss between words 12–18 dB lower, with the speech within about 0.6 dB of
the original. Nothing on disk changes; the other videos' sound (music,
effects) isn't filtered.

The icon (`res/meguarace.ico`, also the window icon) is the port's own
design, drawn by `tools/make_icon.py`, not art from the game.

Settings, progress and high scores are saved in your user preferences folder
(`meguarace.sav`). Options → Save and Load keep named saves there as well,
as the original does.

In a race: Up accelerates, Down brakes, Left/Right steer, Space fires (F1
shows a debug line). Options → Keyboard sets your own keys, on the
original's key setup screen.
The dashboard is each car's own: speed, energy and armour (SP/EN/DM)
gauges, the THRILL meter, the weapon, the radar with the nearest rival and
the tile scanner, all placed as RACEPRG.EXE places them. Rivals behind you
shoot back; run out of armour and you're destroyed. Drive over the lane tiles for their effects (energy, points, weapons,
super speed, skids, spins). The opponents come one at a time, each with the
original's AI: destroy them all to win the round, or finish the laps with at
most one left and 1200 points to qualify. Points come from hits, explosions and tiles,
and count in on the score one a tick. Esc gives up the race, which as in the
original ends the career. Each car has its own guns, armour and firepower, read from
the game's car table.

The track viewer drives along a track video with the game's own cars: your
car (with its dashboard) and three rivals that hide behind scenery the way
they do in the game. Up/Down for speed, Left/Right to steer, C to change your
car, R to change the rivals, and E/T/M/H to toggle the road edges, lane
tiles, occlusion mask and info line. Esc or Enter skips a
video. Set `MEGARACE_SKIP_INTRO=1` to skip the title screen and the boot sequence.

Tested against the US v1.0.1 CD. The disc has no CD audio tracks: speech and
the videos' sound are in the video files, and the in-game music is FM songs
(see above).

Headless test run: `--frames 60 --screenshot out.bmp`.

## HD videos and display modes

The game plays upscaled videos at 1440×1080 (the 4:3 picture of 1080p). You
make them on your own machine, from your own CD, with
`hd-conversion-utility`, a separate utility with its own package and
workflow. No HD video is
ever committed or shipped, because an upscaled video is still the game's
content. It upscales the menu graphics too. Run `hd-conversion-utility --data V:\`
once, then use **Options > ADVANCED** (Down past QUIT). By default the game
starts at the largest resolution that fits, in 16:9 with AMBIENT sides, LINE
TRIPLED and SMOOTHING MORE:
- RESOLUTION: 800×600, 1024×768 or 1920×1080, or 320×200 for the original
  (also `--display`)
- ASPECT: 4:3 or 16:9 (the videos stay 4:3 in the middle; the race dashboard
  stretches across the whole width, and LAPS and score move to the edges) and SIDES: the wings blurred
  (AMBIENT), BLACK, CARBON fibre, SPACE (a nebula and a ringed planet) or
  CYBERPUNK (a neon city in the rain)
- HD STYLE: the packs (Real-ESRGAN), or LINE DOUBLED: the originals with each
  pixel repeated and the seams interpolated, or LINE TRIPLED: each pixel 3×3,
  softer (F10 switches while playing)
- SMOOTHING: OFF, SUBTLE, MORE or MAX, a soft touch on every video, done on the
  GPU (no cost to the frame rate); everything else stays sharp
- LANCE VIDEOS: Lance's clips one SMOOTHING level softer than the rest
  (the default), or SAME
- HD FOLDER: where the packs go (the default is your user folder)
- MUSIC: ORIGINAL, STEREO FM, SOUNDFONT or ENHANCED (the default, a dance remix) ([docs/music.md](docs/music.md))
- VOICE FILTER: the hiss filter on Lance's clips

The game always works in 320×200 coordinates, so gameplay is the
same in every mode. Details: [docs/hd.md](docs/hd.md).

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
| `hd-converter` | pushes touching the converter, or manually | `hd-conversion-utility` (the HD video converter): Linux, macOS arm64, Windows x64; build and test |
| `release-hd-converter` | manual | the converter's own packages, `hd-conversion-utility-<platform>-<version>`, with Real-ESRGAN; never HD videos ([docs/hd.md](docs/hd.md)) |

## Status

| Area | State |
|---|---|
| `MEGARACE.DAT` archive | Done |
| HSQ decompression | Done (all 91 files) |
| HNM video + soundtrack | Done (all 109 videos, 65,234 frames) |
| Track data: camera and road per section, occlusion masks, lane tiles | Done (exact transform from RACEPRG.EXE) |
| Car sprites, dashboards, palettes, rival cars, spinning wheels | Done (from RACEPRG.EXE) |
| Collisions with the rival, walls (scraping, crashes), every armour loss | Done (from RACEPRG.EXE) |
| FM music: the six HERAD songs on an emulated OPL2, song per track | Done (register output identical to AdPlug's HERAD player on all six) |
| Racing: speed, lanes, lane tiles and their sounds, laps | Done (rules from RACEPRG.EXE; a few tile effects still placeholders) |
| Main menu, car showcase, championship rounds, Hall of Fame, options, save file | Done (menu and panels laid out and behaving as RACEPRG.EXE) |
| Weapons: firing, energy cost, per-car guns, hits, damage, explosions | Done (from RACEPRG.EXE) |
| Dashboard: gauges, THRILL, lamps, radar, scanner, opponents, status line | Done (per-car layout tables from RACEPRG.EXE) |
| Rivals shooting back, armour, being destroyed | Done (from RACEPRG.EXE) |
| Rivals one at a time with the original AI, opponents per round, laps, qualifying, scoring | Done (from RACEPRG.EXE) |
| Career: Lance's clips before and after rounds, game over, two seasons, HOLD/TOKY bonus tracks, finale, free track choice | Done (from RACEPRG.EXE) |
| Subtitles in six languages, timed by the videos' own cues | Done (from RACEPRG.EXE) |
| Hiss filter on Lance's clips (V switches it) | Done (the port's own; 12–18 dB less hiss) |
| Hall of Fame: seven places, your run's line, name entry | Done (from RACEPRG.EXE) |
| Attract mode: Lance's idle clip, the demo race | Done (from RACEPRG.EXE) |
| Handling: curves push you outward, capped per car | Done (from RACEPRG.EXE) |

Format notes: [docs/formats.md](docs/formats.md).

## Layout

```
src/            C++ host: window, framebuffer, audio mixer, FM music (fm.cpp), input, Lua API
src/formats/    C++ decoders: MEGARACE.DAT, HSQ, HNM video, HERAD music
src/hd/         HD mode: .hdv packs, compositing, the hd-conversion-utility converter
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
| `gfx` | 320×200 indexed framebuffer: `width() height() clear(i) pset(x,y,i) pget(x,y) fill(x,y,w,h,i) line(x0,y0,x1,y1,i) clip(lines[,yoff]) unclip() blit(x,y,w,h,pixels[,key]) text(x,y,s[,i]) palette(rgb[,first[,sixbit]]) setcolor(i,r,g,b) screenshot(path)`; `nearest(r,g,b)` (closest palette colour); display modes `displays() display() set_display(name) to_display(x,y) hd_pictures([on]) smoothing([level]) video_repeat([n])`, 16:9 `aspect([a]) wing() sides([style]) ambient([on]) hud_begin() hud_end()` (drawing stays 320×200 in every mode) |
| `audio` | `pcm(bytes,rate[,bits,ch,vol,loop]) stop(id) stop_all()`, streaming `stream_reset() stream_push(bytes,rate) stream_queued() stream_played() stream_end()`, the hiss filter `stream_voice_filter(video) stream_voice_bypass(b)`, FM songs `song(bytes[,loop[,gm]]) stop_song() fade_song([s]) song_playing() song_volume(v)`, styles `music_style([s]) soundfont()` (original, stereo, soundfont, enhanced), optional `music(name)` for WAV recordings |
| `dat` | `open([path]) list() exists(name) size(name) read(name[,unpack=true]) read_range(name,off,n)`: files inside `MEGARACE.DAT` |
| `spr` | `open(name)` → sprite set with `:count() :size(i) :apply_palette([shift]) :draw(i,x,y[,scale[,flip]]) :pixels(i)` (index 0 transparent) |
| `hnm` | `open(name)` → video with `:frames() :sound_rate() :decode(i) :chunks(i) :draw([x,y]) :apply_palette() :pixels() :sound() :hd() :blit(i,x,y,w,h,pixels[,key])`; `decode` returns `{w,h,x,y,overlay,video,palette_changed,sound_bytes,pt,kl}` |
| `input` | `down(key) pressed(key) released(key) any()`: SDL key names (`"Left"`, `"Return"`, `"Space"`, `"A"`); text entry `text_start() text_stop() text() paste()` |
| `sys` | `ticks() quit([code]) log(v) save(name, text) load(name)` and `data_path music_path music_mounted hd hd_width hd_height hd_path hd_upscaler display_arg`, HD folder setting `hd_folder() set_hd_folder(folder)` |

