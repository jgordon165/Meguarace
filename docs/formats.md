# MegaRace file formats

Verified against the US v1.0.1 CD (`MEGARACE.DAT`, 515,357,744 bytes).
All values little-endian.

## CD layout

| File | Role |
|---|---|
| `MEGARACE.DAT` | Archive holding all game data (below) |
| `RACEPRG.EXE` | The game: 16-bit real-mode MZ, not packed (89,672 bytes) |
| `MEGARACE.EXE` | 696-byte launcher stub |
| `INSTDOS.EXE`, `INSTALL.EXE` | Installer, writes `MEGARACE.BAT` with driver switches |
| `RACE.LBM` | Installer artwork (IFF ILBM) |
| `EACDEMO/` | Unrelated Electronic Arts demo; ignored |

The disc has a single data track: no Red Book CD audio. Speech and the
videos' sound live in the video files' `sd` chunks; the in-game music is six
FM songs in `MEGARACE.DAT` (see Music).

Launch switches seen in the readme (`RACEPRG AMR ADL SDB2225 JOY... HIM 386 WRIC:\MEGARACE\`)
select sound, music and input drivers and the install path.

## MEGARACE.DAT

```
u16 count                      (2621 slots; 261 used, the rest are empty)
count × {
    char name[16]              NUL-padded 8.3 name
    u32  size
    u32  offset                absolute offset into MEGARACE.DAT
    u8   flag                  always 0 on this disc
}
```

Same layout as Cryo's *Dune* (CD).

### Contents by type

| Ext | Files | MB | What |
|---|---|---|---|
| HNM | 109 | 489.5 | All video: tracks, Lance Boyle clips, intro, enemy showcases, credits, ending |
| HSQ | 91 | 0.8 | Compressed: sound/video drivers (`DF*`, `CR*`), graphics, sprites, enemy cars |
| ZNN | 30 | 0.1 | Per-track data, paired with each track video (not yet decoded) |
| VOC | 21 | 0.1 | Sound effects (Creative Voice) |
| SUB | 6 | 0.1 | Subtitles: ENGL, AMER, FREN, GERM, ITAL, SPAN |
| RAW | 3 | — | Fonts (`FONTE1`, `FONTE2`, `FONTE6_6`) |
| BIN | 1 | — | `DFCHAR.BIN` |

### Naming

- **Tracks:** `NS11`/`NS12` (`.HNM` + `.ZNN`), first and second half of each.
  Prefixes: `NS` New San, `MV` Maeva, `FC` New Fac, `NGL` NG Loop, `SUB` Suburb,
  `PAR`, `JNK`, `LST`, `TOKY`, `HOLD`. The high-score table in the EXE uses
  the long names.
- **Enemies:** `_JOS`, `_HOO`, `_LUI`, `_OUZ`, `_MAR`, `_PAL`, `_OME`, `_RAM`
  (`.HSQ`, likely car sprites); `I_<NAME>.HNM` showcase videos;
  `JO_A/B/C.HSQ` etc. per-enemy sets; `T<xx>.HSQ` / `T<xx>_2.HSQ` per-enemy.
- **Host clips:** `B01`…`BB7.HNM`, Lance Boyle commentary; `VWBT*.HNM` channel idents.
- **UI:** `CASQUE.HSQ` (helmet/cockpit), `PANEL2.HSQ`, `CONTROL.HSQ`,
  `PALOMBRE.HSQ` (shadow palette, 378 bytes).

## HSQ compression

```
u16 unpacked_size
u8  0
u16 packed_size
u8  salt                       the six header bytes sum to 0xAB (mod 256)
...LZ77 stream
```

16-bit little-endian bit queue consumed from the LOW bit, refilled when empty.

```
loop:
  bit 1           -> literal byte
  bit 0, bit 1    -> u16 w: count = w & 7, offset = (w >> 3) - 8192
                     if count == 0: count = byte; if 0 -> END
                     count += 2
  bit 0, bit 0    -> count = (bit << 1 | bit) + 2, offset = byte - 256
  copy count bytes from out[len + offset] (overlap allowed)
```

All 91 HSQ files decompress to their declared size.

## HNM (1) video

Reference: <https://wiki.multimedia.cx/index.php/HNM_(1)>. MegaRace-specific
findings are marked ★.

```
header:  u16 header_size
         palette ranges { u8 start, u8 count (0 = 256), rgb[count] 6-bit } until FF FF
         0xFF padding
         u32 superchunk offsets (relative to the first superchunk)
superchunk: u16 size, typed chunks..., video chunk (to end of superchunk)
typed chunk: char tag[2], u16 size (including the 4-byte header), data
```

| Tag | Content |
|---|---|
| `pl` | Palette ranges |
| `sd` | Sound: the chunks together form one VOC file. 8-bit unsigned mono; rate from the VOC block (`sr` 0xA6 = 11,111 Hz) |
| `pt` | ★ Track videos only: track sections with camera and road geometry (see Track data) |
| `kl` | ★ Track videos only: occlusion masks for car sprites (see Track data) |
| `mm` | ★ Recognised by RACEPRG.EXE (0xCFE7); not seen in any MegaRace video |

Frame rate: ★ about 12.5 fps (sound bytes / 11,111 Hz across a video).
Track videos have no sound and are advanced by gameplay speed in the game.

★ Bits 12–14 of the video header are NOT always zero (B01.HNM frame 7), so
identify typed chunks by their tag, not by those bits.

★ One chunk on the disc has a damaged tag (`NGL1.HNM` frame 2185, a `kl`
chunk tagged `f8 4b`). Its size field is intact; a decoder can detect it
because, read as a video header, it would claim a width over 320.

### Video chunk

```
u32: bits 0-8 width, 9-15 flags, 16-23 height, 24-31 mode
     mode 0xFE = replace, 0xFF = overlay (index 0 transparent)
then compressed data; the sum of its first 6 bytes selects the method
```

- **0xAB:** plain HSQ (above). Used by `MINDSCAP.HNM`.
- **0xAD:** codebook + RLE raster. Used by everything else.

```
u16 frame_size, u16 codebook_size, u8 flags, u8 salt
if !(flags & 4): u16 x, u16 y
flags & 0x40: add 128 to non-zero codebook literals
flags & 0x80: long-runs raster variant
```

★ Bit order and nibble handling (the wiki is ambiguous; confirmed by every
frame consuming its data exactly):
- Bit queue: 16-bit little-endian words, read from the HIGH bit down.
- Nibbles: high nibble first.
- The codebook decoder and the raster decoder keep SEPARATE pending
  nibbles.

Codebook:
```
tag = byte
if tag & 0x80: t = nibble(codebook); offset = (tag & 0x7F) << 1 | (t & 1)
               count = (t >> 1) + 2; copy from cb[len - offset - 1]
else:          append tag (+ base if non-zero)
```

Raster:
```
while out < frame_size:
  while bit == 0: emit next codebook byte
  px = next codebook byte
  long runs : bit0 -> nibble length | 10 -> 2 | 110 -> 3 | 111 -> 4
  short runs: 0 -> 2 | 10 -> 3 | 110 -> 4 | 111 -> nibble length
  nibble length = nibble(raster) or, if 0, byte + 16; then + 4
  emit px × length
```

Validation: all 109 videos, 65,234 frames decode, using up their data exactly
(0–2 bytes of padding).

## Track data

Verified by projecting the data onto the video frames, and by reading the
game's own code in RACEPRG.EXE (code offsets below are within the MZ load
image; the data segment is paragraph 0x0F44).

### Track videos

Each track has a race video (`NS11`, `FC11`, `SUB1`, …: three laps) and a
single-lap video (`NS12`, `FC12`, `SUB2`, …). Both follow the same camera
path: one video frame = one track **section**. `HOLD` and `TOKY` have a
single video. Lap lengths in sections: NS1 476, FC1 806, SUB 809, NGL 858,
MV1 802, TOKY 801 (= rows in the `.ZNN`).

Every track frame is a full 320×152 keyframe (mode 0xFE), so frames can be
skipped freely at speed. The game draws the video at y=0 with the dashboard
below it, and adds a per-track offset to projected y (see Screen layout).

### `pt`: sections

58-byte records (29 × s16). Frame 0 carries 100 records (sections 0–99);
every later frame carries one more. The game keeps a 100-section window
(RACEPRG 0xCF5E: shift down by 58 bytes, append), so at frame *f* the
camera is at section *f* and the window holds sections *f*..*f*+99.

| Field | Meaning |
|---|---|
| 0–2 | Camera position x, y, z (z up) |
| 3–10 | Rotation, 16384 = 1.0 (see transform) |
| 11 | Depth bias; the transform adds field/8 |
| 12, 13 | View roll sin, cos; 4096 = 1.0 |
| 14–25 | Road cross-section: 4 points (x,y,z) outer-left, inner-left, inner-right, outer-right |
| 26 | Bank angle, 1/50 degree (wraps at 18000); tilts car sprites |
| 27 | Heading, 2880 per turn |
| 28 | Pitch, 1/8 degree |

World → screen, exactly as RACEPRG 0x6D58–0x6FD2 (per-track world scale
at DS:0x3E6 is 1/1 for every track):

```
d  = P - cam
X  = (-dx·f3 + dy·f4) / 16384
Y  = (-dx·f5 - dy·f6 + dz·f7) / 16384
Z  = (-dx·f8 - dy·f9 - dz·f10) / 16384 + f11/8
X' = (f13·X - f12·Y) / 4096        Y' = (f12·X + f13·Y) / 4096
sx = 160 + X'·2.3·88 / Z           sy = (131 - Y'·230 / Z)·200/262 + [0x426]  (per-track, see Screen layout)
```

Car sprites are placed by evaluating this for a point on a road section
ahead (RACEPRG 0x6D58 takes the section index and a cross-section point).

### `kl`: occlusion masks

Copied to DS:0x15D8 each frame; used by RACEPRG 0xB520 / 0xB11A when
drawing a car. Spans are where a car may be drawn; gaps are scenery in
front of it (e.g. the inner barrier of a hairpin).

```
u8 count (1-3)
count × { u8 section, u8 first_row, u8 last_row (inclusive), u16 start (row·320), u16 data_offset }
```

For a car *n* sections ahead, use the last layer whose `section` ≤ *n*;
if *n* is below the first layer's `section`, the car is not masked.

Layer data is a list of opcodes, each describing 1–16 rows of the same shape:

```
op: bits 0-1 = boundaries per row: 0 (row has no spans), 2, 4, 6
    bit 2    = first boundary explicit (else x=0)
    bit 3    = last boundary explicit (else x=320)
    bits 4-7 = row count (0 = 16)
explicit boundaries: cumulative deltas from x=0, one byte each
                     (0 means: next byte + 256)
boundaries pair up as [x0,x1) spans
```

All 31,277 `kl` chunks on the disc decode with no bytes left over. One
chunk (`NGL1.HNM` frame 2185) has a damaged tag (`f8 4b`) but is a normal
`kl` mask.

### `.ZNN`: lane tiles

One row of 5 bytes per section of a lap: the five lanes across the road
between the inner edges (cross-section points 1 and 2). 0 = nothing.
Values fall in two families, 1–10 and 17–25 (bit 4 set). They match the
on-track bonus/penalty tiles (sounds `BONUS*.VOC`, `MALUS*.VOC`); the exact
meaning of each code still has to be read from the EXE.

## Graphics (sprite sets)

Most `.HSQ` graphics files unpack to the same container:

```
u16 table_offset
palette ranges, bytes 2 .. table_offset (same encoding as HNM: start, count, 6-bit RGB; FF FF ends)
u16 offsets[n], relative to table_offset; n = offsets[0] / 2
entry: u16 width (bit 15 = PackBits), u8 height, u8 (always 0xFF), pixels
```

- Raw entries are `width × height` bytes. Packed entries use PackBits: a byte
  `n < 0x80` copies the next `n + 1` bytes, and `n ≥ 0x80` repeats the next
  byte `257 − n` times.
- Index 0 is transparent.
- Entries with width 0 are data tables, not images. The 9 tables in each
  `_XXX.HSQ` (195 bytes each) look like sprite-scaling tables.
- ★ A sprite palette sets colour 0 too. Ignore that: the videos draw black
  with colour 0.

The files in RACEPRG.EXE's own name table that use a different layout,
with the table near the end (`DETRITUS`, `ALARME`, `LENNY`, `MAEVA`,
`NEWSAN`, `PAGA`), are not decoded yet.

### Palette layout during a race

| Colours | Owner |
|---|---|
| 0 | black (video) / transparent (sprites) |
| 1–64 | track video (checked on every track) |
| 65–71 | shared car shading |
| 72–111 | dashboard (`T<XX>_2`, `T<XX>`) |
| 112–127 | effects: `FUMEE` and `EXPLOSE` draw with 112–126; `EXPLOSE` carries the palette (fire colours, 127 black) |
| 128–191 | the player's car (`<XX>_A/B/C`) |
| 192–255 | rival cars (`_XXX`): the same model's `<XX>_A` palette moved up by 64 |

So one player car and one rival model can be on screen together: all rivals
on a track are the same car.

### Cars

Codes in game order: `JO` Jose, `RA` Ramon, `OU` Ouzbel, `PA` Paloma, `LU` Luis,
`OM` Omega, `HO` Hooper, `MA` Maria.

| File | Contents |
|---|---|
| `<XX>_A` | 17 body images, from straight behind (0) round to head-on (16), plus the palette |
| `<XX>_B`, `<XX>_C` | wheel frames drawn over the body to spin the wheels: the same image number, centred on the body's x and level with its bottom, on every view (0x2594). Only while moving; the phase `[0xCDA]` steps 2, 1, 0 every 36 − 2 × (speed / 10) ticks, 0 drawing the body alone (0x25F8) |
| `_JOS _RAM _OUZ _PAL _LUI _OME _HOO _MAR` | rival version: 11 rear views (0–10) in colours 192–255, plus 9 data tables |
| `T<XX>_2` | dashboard: two 160×47/48 halves, drawn at y = 152 |
| `T<XX>` | dashboard gauge pieces |
| `I_<NAME>.HNM` | showcase video of each car |

Image choice (RACEPRG 0x1B45): `angle` = (160 − car x) / 8, eased one step per
tick, minus the turn and skid values. The image is `DS:0x93C[|angle|]` (96
entries, 0..16), mirrored when `angle` is negative.

Player car placement (RACEPRG 0x1882, 0x1A89):
- 1 section ahead of the camera (`DS:0x42C` per track: 1 section, 0/8).
- Bottom edge on the line between the projected inner road edges
  (cross-section points 1 and 2) at the car's screen x.
- Full size.

### Other graphics

| File | Contents |
|---|---|
| `CASQUE` | the racer in a helmet: two 160×152 halves |
| `CONTROL`, `KONTROL`, `QONTROL` | options menu in different languages |
| `PANEL2` | car selection: car thumbnails, buttons, bars (full palette) |
| `EXPLOSE` | explosion animation (24 frames, own palette) |
| `FUMEE` | smoke and debris |
| `ZONES`, `ZONES_` | 21 lane-tile icons |
| `PALOMBRE` | car shadow |
| `FONTOPT`, `FONTHALL`, `FONTEOPT`… | fonts, 9×9 |

### Screen layout during a race

The video is drawn at y = 0 (rows 0–151) and the dashboard at rows 152–199.
Projected y gets the per-track offset `[0x426]` = `DS:0x2B0[track] + 3`: −21
on every track except LST (−45). Track order in the EXE: NS1 MV1 FC1 NGL SUB
NS2 MV2 PAR FC2 JNK MV3 FC3 NS3 LST TOKY HOLD.

## Menus

| Asset | Use |
|---|---|
| `PANEL1.HNM` | main-menu backdrop (one frame): eight monitors with red bars, globe, three buttons |
| `PANEL2.HSQ` | overlays: 0–7 car thumbnails, 8–15 lit bars, 16–23 bars as on the backdrop, 24–26 buttons highlighted, 27–29 buttons as on the backdrop, 30 palette strip |
| `I_<NAME>.HNM` | car showcase videos |
| `HALL1.HNM` | Hall of Fame backdrop |
| `KONTROL.HSQ` | the menu's panels (resource 0x4D): 0 OPTIONS panel, 1 LEVEL panel, then (off, lit) pairs NOVICE 2/3, HARDENED 4/5, LOAD 6/7, SAVE 8/9, KEYBOARD 10/11, JOYSTICK 12/13, MOUSE 14/15, QUIT 16/17. `CONTROL` holds the same in English, `QONTROL` in Spanish |
| `KONTROL6.HSQ` | input messages (0x5F): 0 keyboard now active, 1 joystick needs INSTALL, 2 joystick now active, 3 mouse now active, 4 no mouse driver |
| `CONTROL2.HSQ` | track choice (0x58): 0 panel, then (off, lit) pairs for the 16 tracks, 1/2 … 31/32 |
| `FONTHALL.HSQ` | 9×9 font, glyph index = ASCII code (96 glyphs; DOS arrows at 24–27) |
| `VWBT*.HNM` | channel idents (played before each race in this port) |

**The menu** (RACEPRG 0x54C9). Every `PANEL2` image is centred on its
point in DS:0xFA4 ({x, y} per image, drawn at x − w/2, y − h/2: 0x57AE):

| Images | Centres |
|---|---|
| 0–7 cars | (72,18) (247,18) (53,48) (264,48) (48,83) (272,83) (59,120) (261,120) |
| 8–15, 16–23 bars | (107,19) (212,20) (90,48) (229,48) (86,81) (232,81) (99,120) (220,119) |
| 24–26, 27–29 buttons | (86,168) (161,168) (239,168) |

The focus `[0x69FA]` runs 0–10: the monitors, then the buttons left, middle,
right. Right and Down step it +1, Left and Up −1, wrapping, past monitors
whose car is locked (0x561E). Drawing (0x569C): an unlocked monitor shows
its car (image m) and its bar, 8 + m when focused, else 16 + m; a locked one
keeps the backdrop's empty screen. A button is 24 + b when focused, else
27 + b.

Keys (0x59F5; scancodes in `[0x703F + code]`): Enter on a monitor races the
next round with that car, or once the game is completed (`[0x69AA]`) asks for
the track first (0x55AB); Space or Esc plays the showcase, video 0x27 +
monitor (0x5662). Enter on a button opens its panel (0x57F8): left = level,
middle = options, right = the Hall of Fame (0x548C).

**Cars you can pick** (`[0x101C]`, a word per monitor): a new career copies
DS:0x103C, which opens JOSE, LUIS and OUZBEL (0x6A7C, also at game over).
After a race where every opponent was destroyed on rounds 1–14 (0x4F4B),
0x51AB ORs in DS:0x104C + 32 × round, which opens one more: round 1 RAMON,
2 MARIA, 3 HOOPER, 4 OMEGA, 5 PALOMA, 6 RAMON, 7 MARIA, 8 OMEGA, 9 HOOPER,
10 PALOMA, 11 MARIA, 12 HOOPER, 13 RAMON, 14 OMEGA.

**Panels** (0x57F8, 0x5991): a list over the menu. Items are (off, lit)
pairs from `[0x6A00]`, centred on x = 160, from y = 100 − 4 × count, one
every 16 rows (9 for the tracks). Up/Down move (wrapping), Enter or Space
selects, Esc closes.

| Panel | Picture | Items |
|---|---|---|
| level | KONTROL 1 at (160,95) | NOVICE, HARDENED (from 2). Starts with the current level; picking one sets `[0x68CD]` and starts a new career (0x6A7C) |
| options | KONTROL 0 at (160,108) | LOAD 0x61CB, SAVE 0x642A, KEYBOARD 0x5FC1, JOYSTICK 0x5EBF, MOUSE 0x5EF8, QUIT 0x5113 (from 6) |
| tracks | CONTROL2 0 at (160,105) | the 16 tracks (from 1); the choice is the round |

KEYBOARD, JOYSTICK and MOUSE show a KONTROL6 message at (160,130) until a
key; KEYBOARD when the keyboard is already active opens the key setup
instead. The port has only the keyboard, so KEYBOARD always opens the key
setup and JOYSTICK and MOUSE get messages 1 and 4.

**Key setup** (0x5FD6…0x6178): the controls are rows at DS:0x1297 (a
second set for Spanish at 0x12EB), each a scancode and 13 characters: label
and a 4-character key name (`LEFT    :... `). Defaults `,` `.` A Z SPACE;
the sixth row is `<OK OR ESC>`. The screen is CONTROL4 image 2 at
(160,100) with the rows from (125,80), 6 pixels a character and 10 a row,
in FONTOPT; the row being asked for is in FONTOPT2 and its key name in
FONTOPT3. A key per row in turn (lower case raised; P and control
characters other than RETURN refused); on the last row any key keeps them
(0x6AF2), ESC at any point restores the copy taken at the start (0x60A9,
0x60D4). Names (0x611D): the character, the arrows as glyphs 24–27, and
ESP, RET, SFTD, SFTG, CTLR, ALT for space, return, right and left shift,
ctrl and alt. The port keeps its own defaults (arrows and SPACE) and shows
the current keys rather than the table's dots. LOAD and SAVE use the port's one
save rather than the original's slots. After a car showcase the original
holds the last frame for up to 800 ticks or a key (0x5687); the port goes
straight back.

Monitor order = thumbnail order = showcase videos: JOSE, HOOPER, LUIS,
OUZBEL, MARIA, PALOMA, OMEGA, RAMON (confirmed by comparing each
thumbnail with its video).

From RACEPRG.EXE (read at runtime from the player's copy):
- **Default Hall of Fame:** 8 × { u16 score, name[18], "00000 ", track[10],
  level[4] }, starting 2 bytes before "BIG BOB" (DS:0x1344): the 7 places,
  then the line for your own run ("ENFORCER SCORE", DS:0x145C). See Hall of
  Fame.
- **Track display names:** 16 × 16 bytes after "EASYHARD": NEW SAN-1,
  MAEVA -1, NEW FAC-1, NG LOOP, SUB-URB 1, NEW SAN 2, MAEVA 2, PARTICLE,
  NEW FAC 2, JUNKYARD, MAEVA3, NEWFAC3, NEW SAN 3, LASTDRIVE, TOKYO, HOLDER.
- **Rival order:** `_RAM _JOS _OME _OUZ _PAL _LUI _HOO _MAR`.
- **Second file set:** a parallel `_RAF _JOF … _MAF` set is named in the
  EXE; only `_LUF.HSQ` is on the disc.

## Race rules (RACEPRG.EXE)

Implemented in `scripts/race/rules.lua`; addresses are in the MZ load image.

### Scenery and speed

- The race loop fetches the next video frame whenever speed > 0 (0x0739 →
  0xCBBE). Fetching is paced only by the CD stream, not by speed, so the
  scenery moves at a fixed rate and stops at speed 0. This port uses the
  videos' 12.5 fps.
- Speed `[0xC9E]` runs 0–180; the dashboard shows it ÷ 10 (`[0xC9A]`). Above
  180 it drops 5 per tick.
- Keyboard throttle (0x2CC9): +4 per tick accelerating, −4 braking, −1
  coasting. Joystick and mouse set a target (0x2959, 0x2A37).
- Speed decides who gains on whom: the rival's section is `[0xDFC]`, the
  camera's is `[0x6957]` = video frame − 1.
- Game time is `[0x7038]` ticks. The tick rate isn't confirmed; the port
  uses 25 per second.

### Lanes and tiles

- Lane = the screen band under the car's x `[0xCB6]` (0x2031): 0–59, 60–114,
  115–194, 195–249, 250–320. It is not measured across the road.
- The ZNN buffer is at `[0x696C]`; the lap length in rows is `[0x8FA]`. The
  row under the car is copied to `0xCE2–0xCE6`, and the effect dispatcher at
  0x1D94 applies it.
- Sound per code: `DS:0xE47` (word index, −1 = none) into the name table at
  `DS:0x56A6`.

| Code | Sound | Effect (variable) |
|---|---|---|
| 1 | MALUS9 | energy − drain size `[0x68DB]` (then drain size = 50) |
| 2 | MALUS11 | points − 10, counted out one a tick (see Points) |
| 3 | — | forced braking until speed ≤ 130 (`[0x68CF]`) |
| 4 | — | spin: steering sequence + 5 px/tick drift (speed > 15), else as 7 |
| 5 | PLUARME | weapon lost, energy 0 |
| 6 | MALUS7 | radar jammed: `[0x68EB]` for `[0x812]` ticks (the radar blinks instead of showing the rival) |
| 7 | — | skid: steering sequence + 2 px/tick drift (speed ≥ 8) |
| 8 | BONUS16 | flag `[0x68DD]` (not traced yet) |
| 9 | MALUS6 | gauges out: `[0x68E7]` for `[0x812]` ticks (every dashboard gauge reads 0) |
| 10 | MALUS2 | timer `[0x68FB]`, 800 ticks (not traced yet) |
| 17 | ENERGYP | energy + `[0x68DB]` (then 70) |
| 18 | BONUS1 | points + 10, counted in one a tick (see Points) |
| 19 | BONUS11 | super speed: speed held at 280 for 400 ticks (`[0x6903]`) |
| 20 | BONUS10 | weapon 1, charged (`[0xD5E]`): a hit destroys the rival |
| 21 | BONUS10 | weapon 3, energy + 120 |
| 22 | BONUS10 | weapon 2, energy + 60 |
| 23 | BONUS15 | flag `[0x68E1]` if the car allows it (`[0x810]`) |
| 24 | BONUS14 | flag `[0x68EF]` if the car allows it (`[0x80E]`) |
| 25 | — | boost: speed 280 for 100 ticks, resets the slide state |

Energy `[0x6911]` is the EN gauge, max 450 (0x117A). Firing costs energy
(see Weapons).

After every tile (0x1FF1) the weapon is limited to what the car can carry:
3 drops to 2 without gun 3, 2 to 1 without gun 2, 1 to none without gun 1,
and the weapon-1 charge is cleared if the car can't keep it.

### Per-car table

DS:0x7B0, 10 bytes per car, copied to `[0x800]`–`[0x812]` at the start of a
race (0x038C). It is indexed by `[0x81E]`, the same index that picks the car's
`T<XX>` file (resource 0x24 + `[0x81A]`: TJO, TRA, TOU, TPA, TLU, TOM, THO,
TMA), so the order is JO, RA, OU, PA, LU, OM, HO, MA.

| Car | gun 1 | gun 2 | gun 3 | charge | armour | power | handling | `[0x80E]` | `[0x810]` | `[0x812]` |
|---|---|---|---|---|---|---|---|---|---|---|
| JO | 1 | 0 | 0 | 0 | 120 | 8 | 5 | 1 | 1 | 200 |
| RA | 1 | 1 | 1 | 1 | 70 | 20 | 4 | 0 | 0 | 200 |
| OU | 1 | 1 | 1 | 1 | 70 | 20 | 6 | 1 | 1 | 200 |
| PA | 1 | 1 | 0 | 0 | 120 | 16 | 6 | 0 | 0 | 100 |
| LU | 1 | 1 | 1 | 0 | 100 | 10 | 6 | 1 | 1 | 200 |
| OM | 1 | 1 | 0 | 0 | 80 | 10 | 4 | 1 | 1 | 100 |
| HO | 1 | 1 | 0 | 0 | 110 | 10 | 4 | 0 | 0 | 100 |
| MA | 1 | 1 | 1 | 0 | 100 | 16 | 2 | 1 | 1 | 200 |

Handling `[0x80C]` caps how far a curve pushes you (see Handling). Armour
starts the third gauge `[0xD2E]`. Power is the damage per hit and the
base of the fire cost. `[0x812]` is multiplied by 4 at the start.

### Weapons

Every car starts with weapon 1 (DS:0xD66 is all zero, raised to 1).

- **Fire** (0x2962, keyboard): needs a weapon and energy > 0, once per press
  (`[0xDAE]` latch). Plays `LASER`. A new shot replaces one in flight.
- **Cost** (0x2D1B): power ÷ 2; × 8 for a charged weapon 1; + 4 for weapons
  2 and 3. Energy doesn't go below 0.
- **Shot** (0x2E14, once per drawn frame): `FUMEE.HSQ` (resource 0). A muzzle
  flash at the car, frames 11–13, and the bolt, frames 14–17, a little ahead
  of it. Weapon 1 fires one bolt, 2 fires two (x ± 6), 3 fires three
  (0x2EF3). The muzzle offset (0, 4, 12, 20, 40, 48) and the bolt offset from
  the muzzle ((0,−17), (8,−14), (18,−10), (30,−8), (40,−7), (55,−6), with
  the x sign following the angle) are picked by the car's view angle
  `[0xCA4]` at thresholds 2, 3, 5, 7, 9 (0x2DD5, DS:0xD86).
- **Hit** (0x2F93): the bolt's x is inside the rival's screen span
  `[0xDC8]`–`[0xDCA]` and the rival is in range (`[0xE0B]` between 0 and the
  angle table's third value, 32–40). The bolt stops on the rival and plays
  the impact, frames 18–22, with `CHOCTIR`. The rival's damage `[0xD30]`
  gains power (moved across one unit per gun drawn per frame, via
  `[0x818]`), and 5 points are paid out (`[0x68D5]`).
- **Charged weapon 1**: at the end of an impact the rival's damage is set to
  the maximum (0x2E49).
- **Destroyed rival** (0x3E98): damage ≥ `[0x814]` (100, set at 0x0592) plays
  `EXPLOD2`, the rival's speed drops to yours − 20, and `EXPLOSE.HSQ`
  (resource 0x5D) plays its 24 frames over it (0x4688), paying 5 points per
  frame while it is on screen. A rival that stays 6+ sections behind for 400
  ticks is also counted destroyed (0x3E7F). At the end of a race a destroyed
  rival lowers `[0x68C7]` (0x0A3B).
- **Damage bar** (0x0FC3): 52 × 6 in colour 74, with (100 − damage) ÷ 2
  pixels of 77 (first 16) and 76, every 4th pixel 74.

**The rival fires back** (0x4711, 0x4779, 0x491A) with the same frames
(`[0xD50]`/`[0xD54]`, flag `[0xDAC]`). It fires from 3–10 sections behind you
(`[0xE0B]` −3…−10, a few AI states use −4) when its x is inside your car's
span `[0xCC4]`–`[0xCC6]` and none of its shots is in flight; firing takes 1
from *your* energy. The bolt hits if the rival is then 5–9 sections behind
and still lined up; at the end of the impact you lose 20 armour (0x4801).
At 0 armour (0x1940) `EXPLOD2` plays, `EXPLOSE` plays over your car (gone
after frame 15) and the race is lost (`[0x68CB]` = −2).

Other armour losses: −8 when a skid ends with `[0x6933]` set (0x2136: after a
collision skid or a wall crash), −1 per smoke cycle while scraping a wall
(0x3098). See Collisions and walls.

### Handling

Curves push the car outward (0x1838…0x1A40, 0x2290…0x236A;
`scripts/race/handling.lua`). Each video frame the heading change from the
camera's section to the next (pt word 27, 2880 a turn, wrapped: 0x6DCC,
`[0x6B58]`) is halved and the last four are averaged into the curvature
`[0xCFC]`. NGL's first 420 frames and all of FC3, TOKY and HOLD have none
(DS:0x2D6 per round × 4, 0x6E6A). A rising heading bends the road left on
screen.

The push `[0x691F]` (0x19CB) is 0 for a curvature within ±5, otherwise
curvature × (speed / 10) / 50; 0 while you steer at 150 or less; capped at
the car's handling `[0x80C]` (MA 2 … JO 5, OU/PA/LU 6). Each tick it moves
you when you aren't steering, and once more at 160 and over on a sharp
curve (beyond ±11). Toward an edge it eases: 1 pixel within 60 of it, none
within 30 (x 60/30 on the left, 260/290 on the right: 0x232F). No push
during a skid or at a wall. A push of 6 or more tips the body lean
`[0x691D]` by 2 against it (±6), and each frame it eases back 1 (0x1A17).

**Steering** (0x2833…0x2947, every tick; keys `,` and `.` by default,
DS:0x1297): the direction tips a lean `[0x691B]` (±12, against the
direction) and builds a sideways speed `[0x6941]` that moves the car each
tick (0x2290, with the curve push and collision pushes). The old speed,
halved if it points the other way (−1 counts as 0), is doubled, a step of
speed/80 (at least 1) added, and the result capped at speed/20, at most 9
(halved at 180 and over when the curve push is against it). Each steering
tick adds one to `[0x6939]` (the wall-crash count). Let go, the lean goes
back toward 0 one a tick, and so do the speed and the count, except on a
car with handling over 5 while the lean is odd. Nothing happens below speed
10 or in a slide (`[0x6933]`). A skid, a slide or a crash stops the
sideways speed (0x1A82, 0x1E84, 0x2252, 0x3C21).

The car's picture (0x1B84) is the eased view angle `[0xCDE]` less the two
leans, through the table at DS:0x93C; a rival's (0x4574) is its own view
angle plus the curve under it `[0xE23]` less its steering lean, within ±90.

### Collisions and walls

**The rival** (0x3A8F…0x3CF9, `scripts/race/collision.lua`): it's in reach
0–7 eighths ahead or 1–4 behind with the two cars' screen spans
overlapping. If it moved this tick it's a rear-end collision and it goes
back to where it was; otherwise it's alongside.

| `[0x694F]` | Contact | Effect |
|---|---|---|
| 4 | you ran into it | your speed −20 and braking down to 130, its speed +40, a boost ends; THRILL 10; it skids. On HOLD both cars are finished |
| 8 | it ran into you | your speed +30, its −20; THRILL 30; you skid |
| 1 / other | alongside, left / right | 7-pixel pushes apart easing by 1 a tick; THRILL 50; it skids |

A skid (0x3C72) is a sway script with a 2-pixel-a-tick slide away from the
other car (yours DS:0x896 or 0x888, the rival's 0x8A6/0x89E); CHOC4 plays.

**The walls.** Your car's x `[0xCB6]` stays within the track's limits:
DS:0x31A[round × 4] on the left (0 on every track except JNK: 60) and 320
minus it on the right (0x0311, `[0xCD2]`/`[0xCD6]`), not the road's edges.
Held at one, you're scraping (0x23B8: `[0x68FF]`, `[0x6933]`): sound 6
(CRRR3) loops and FUMEE frames 6–10 puff 20 pixels out on that side
(0x302A), each cycle costing 1 armour. If you've steered for more than 6
ticks in a row (`[0x6939]`) and your speed is over 100, you crash (0x2212):
CHOCRR2, the 0x888 sway with a slide of 9 pixels a tick away from the wall
that eases by 1 on every tick of an odd video frame (0x2271: `[0x6957]` & 1, two ticks in four), and −8 armour when it ends
(0x2136). One crash per push: the counter is set to 1000 until you let go
of the steering.

This port runs shot and explosion frames at 12.5 per second, applies a hit's
damage at once, uses 12 sections as the range, draws the damage bar over the
rival, and takes a rival out of the race once its explosion has played.

★ `MALUS2.VOC`, `MALUS7.VOC` and `BONUS15.VOC` declare a time constant of
6 (4 kHz). Every other effect is 7.5–9.6 kHz, so this port plays those three
at 8 kHz.

### Dashboard

Every dashboard image is drawn by the sprite routine 0x7041, which centres
on x; y is the top. Dashboard positions are relative to row 152.

**Gauges** (0x1283) are strips of frames in the car's `T<XX>.HSQ`. Each has
a table of 8 cars × { x, y, first frame, frame count } (car order JO RA OU
PA LU OM HO MA), and shows frame `first + value × (count − 1) / max`, with
the value clamped to 0…max. While tile 9's timer `[0x68E7]` runs, every
gauge reads 0.

| Table | Gauge | Value | Max |
|---|---|---|---|
| DS:0x530 | SP, speed | `[0xC9A]` (speed ÷ 10) | 22 |
| DS:0x570 | EN, energy | `[0x6911]` | 450 |
| DS:0x4F0 | DM, armour | `[0xD2E]` | the car's armour `[0x808]` |
| DS:0x4B0 | THRILL | `[0xD32]` | 128 |
| DS:0x630 | weapon | `[0xD5A]` | 3 |
| DS:0x670, 0x6B0 | two lamps | lit unless weapon 1 is charged `[0xD5E]` | 1 |
| DS:0x5B0 | one-frame badge | `[0x68E1]`, code 23's flag (count 1: always the same frame) | 1 |

**THRILL** `[0xD32]` (the TV audience's excitement) starts at 60. Pending
amounts in `[0xD34]` raise it 1 a tick (capped: 127 → 124); otherwise it
sinks 1 a tick (0x1C19). Bumps set 10–50 (0x3BB3…0x3C60), your hit on a
rival adds 64 (0x2FEC), a destroyed rival sets 5 (0x3EAD).

**Radar** (0x12F7, DS:0x770: { x, y, first frame, width }): frame first + 2
is the screen. On it, the rival's blip: frame first + 3 + d, d = sections
ahead (0–4) or 5 + sections behind (capped at 9), at x + (rival x − road
left − road width / 2) × width / road width (`[0x68F7]`, `[0x68F9]`),
kept inside the screen. While tile 6's timer `[0x68EB]` runs it blinks
frames first + 0/1 instead; for 50 ticks after a rival is destroyed (`[0x68EB]`
= 1) it shows no blip.

**Tile scanner** (0x0EA2, 0x10A6, DS:0x730: { x, y }): `ZONES_` frame 20 is
the panel, its left edge at x. The next three ZNN rows are `ZONES_` icons
centred at x + 9 + 10 × lane, rows at y + 20, + 10 and + 0. The icon is
`code − 1` for codes 1–10 and `code − 7` for 17–25.

**Opponents** (DS:0x6F0: { x, y }, x = 0 for none): `[0x68C7]` in the 6×6
font. Not drawn on the last track. `[0x68C7]` starts at DS:0x846[round]
(4, 5, 5, 5, 6, 6, then 7): the number of rivals to beat. The original
races them one at a time and you win when it reaches 0 (0x3FE8,
`[0x68CB]` = −3); see Rivals.

**Status line** (0x150A), in the RAW fonts, over the picture: `LAPS:` at
(2, 6), the laps left `[0x68C5]` (3; 2 on HOLD) at (50, 6), and the score
`[0x6A02]` + `[0x690D]` in six digits at (250, 6).

**RAW fonts** (0x1600): 320-pixel-wide strips. `FONTE1` (digits) and
`FONTE2` (`:` onwards, so letters) are 8 × 12 glyphs, glyph *k* at column
8*k* − 1 (0 for *k* = 0), 10 pixels apart; `FONTE6_6` is 6 × 6 digits at
column 6*k*, 6 apart. Pixels are colours 79 and 255; over the dashboard the
glyph box is cleared to black first.

## Rivals

**One at a time.** A race has `[0x68C7]` opponents (DS:0x846[round]: 4, 5,
5, 5, 6, 6, then 7; HOLD draws 2–8). The first starts 5 sections ahead
(0x04F5; 28 on HOLD) at speed 0. When one has been destroyed and its
explosion has played, the next arrives 28 sections ahead at your speed and
the count drops (0x3F89). At 0 the round is won.

**Distance** `[0xE0B]` = `[0xDF2]` × 8 + `[0xDF6]` (0x4405): eighths of a
section ahead of your car (the per-track offset `[0x42A]` is 0), clamped to
−16…200 for the AI. `[0xDF2]` stays within ±30 sections.

**Movement** (0x42C6), every tick: with a speed difference dv the rival
moves `step` eighths every `every` ticks, toward you if it's slower:

| dv | < 10 | 10–19 | 20–29 | 30–39 | 40–59 | 60–179 | 180+ | destroyed |
|---|---|---|---|---|---|---|---|---|
| step / every | 0 / 16 | 1 / 10 | 1 / 6 | 1 / 4 | 1 / 2 | 2 / 1 | 8 / 1 | 2 / 1 |

Its speed is limited to 1…`[0xDB2]` (230). On HOLD it is 0: the rivals are
parked.

**AI** (0x3467): every 100 ticks (50 when `[0x68CD]` = 2, HARDENED), only
while you're moving and never on HOLD, the rival's state handler
(cs:0x36F1) runs. The handler for each new rival comes from cs:0x36F7
indexed by the opponents left: 1 → C, 2 → B, more → A.

- *Chase* (0x36AD), shared: speed = yours − k, k = 100 when more than 60
  eighths ahead, 50 above 30, 30 above 8, else 10; k = −50 if you're at
  100 or less or spinning. Limit 230.
- **A** (0x34B4): speed 230 until 170 eighths (21 sections) ahead, then
  chase; once you pass it (< 0), it holds 160 and fires when it's within
  4 eighths behind and lined up; ahead again → chase.
- **B** (0x351F): chase until within 40 eighths, then 160, steering left;
  from 7 eighths ahead it steers off the walls (0x3666); fires from 3–10
  eighths behind; 40 ahead → chase again.
- **C** (0x357F): as B, but from 7 eighths it holds the middle of the road
  (x 140–180, 0x367F).
- (A fourth type, 0x35DF/0x35F6, blocks your line and speeds up near you; the
  table only uses it for more opponents than any round has.)

Steering `[0xE03]` (−1/0/1) only acts within 50 eighths (0x4213). Each
tick (0x4210) it tips a lean `[0x691C]` (−6…6, against the direction) and
builds a sideways speed `[0x6943]`: the old speed, halved if it points the
other way, is doubled, the step added (speed/40, plus 4 when that is under
2; 4 from 100 on), capped at ±speed/10 (at most 10) and halved, so it tops
out at 5 pixels. Let go, the lean goes back toward 0 one a tick and the
speed by one whenever the lean is even.

The rival's place `[0xDBA]` is a screen x at its depth, between the road's
inner edges `[0xE2D]`/`[0xE31]` less half its width (0x3B21). Every 6
ticks (0x38EF, when the road there is 32 pixels or wider) it moves: within
50 eighths by its sideways speed (none when the lean points the other way)
plus a collision bump `[0x6947]`; up to 150, a pixel toward the middle of
the road; from 150 on it is put in the middle every tick (0x3A18). Hitting
an edge resets the lean (0x39EC). The curve under the rival (`[0xE23]`,
0x3F06) only feeds its tilt `[0x691E]`, not its place. The port moves the
rival by these pixels but keeps its place as a fraction of the road, so on
a curve it follows the road where the original's stays put on screen.

**Left behind:** 6+ sections behind for 400 ticks and it counts as destroyed
(0x3E4D…0x3E8D); on HOLD, as soon as it's 6 behind.

**End of the race** (0x4F4B…0x5064): `[0x68CB]` −3 every opponent
destroyed, −1 laps run out (`[0x68C5]`: 3, HOLD 2), −2 wrecked. The race's
points `[0x690D]` add to the score `[0x6A02]` (none for a wreck on HOLD).
ESC gives up (−4, 0x0A2B): the points count (not on HOLD, 0x4F78) and the
career ends with no clip. However the race ends, a rival destroyed but still
exploding counts as gone (0x0A3B: its damage `[0xD30]` at the limit `[0x814]`).

**Points** `[0x690D]` come from three places: 5 per hit, 5 per frame of a
rival's explosion (0x4699, at once), and the tiles (+10 code 18, −10 code
2). Hits and tiles don't land at once (0x1C5D): the amount waits in
`[0x68D5]` and moves one a tick, counting in (`[0x68D1]`) up to 65000 or out
(`[0x68D3]`) down to 10. A payment (0x2FF1, 0x1F10) first adds whatever was
still due; a charge (0x1EE1) first takes it off, if you have that many,
even when it was a payment still counting in. Whatever is still due when the
race ends is lost. There is no lap, time or finishing bonus.
You go on to the next track after −3, or after −1 with at most one opponent
left and 1200 points or more. Energy starts at 400 (0x0561).

The port runs the AI exactly as above. Its own choice: the steering rate
(0.012 road widths a tick).

## Career

Everything between races runs from 0x4DB3 (code in `scripts/game/career.lua`).

**Videos by number** (`[0x69E4]`, played by 0x53B1): the table at DS:0x59BC
holds a flags word and a file name per number. 0x00–0x1F are the track
videos, 0x20 HALL1, 0x21 CRYO, 0x23 INTRO, 0x24 MINDSCAP, 0x25 FIN, 0x26
PANEL1, 0x27–0x2E the car showcases, 0x30–0x6F Lance Boyle's clips
(`B*.HNM`), the idents (0x55–0x58 VWBT2–5, 0x63 VWBT) and 0x70 CREDIT. In a
run of clips one key ends the clip and the other the whole run
(`[0x69E8]`, 0x5C98).

**Before a round** (0x4E38…0x4E85): INTRO (on a new career), the ident
(VWBT on a new career, a random VWBT2–4 after moving on: 0x5151), then,
until the game is completed: Lance on the round, video 0x31 + round
(B01, B09, B17, B29, B24, B04, B12, B27, B20, B26, B14, B23, B07, B30, B92,
B32), Lance on its scenery (0x5353) and B64. The scenery clip is
`DS:0x1274[round] + k − 1`, k = 0 on your first race there, 1 if you've
raced there without a clean round, 2 after a clean round. Records are per
scenery family (`[0x1256][round]` → 0x124C NS, 0x124E MV, 0x1250 FC, 0x1252
SUB/JNK, 0x1254 NGL/PAR/LST/TOKY): B34–B36, B37–B39, B40–B42, B43–B45,
B46–B48. Before a race on round 1 on NOVICE: B66 (0x4EBB).

**After a race** (0x4F2F…0x5151):

| Result | Clips | Then |
|---|---|---|
| every opponent destroyed, under 800 points | BA9 | next round |
| … 800–1199 | BA5, BB1, one of BB3–BB6 | next round |
| … 1200+ | BB2, one of B83–B86 | next round |
| laps run out, ≤ 1 left, 1200+ | BA3 | next round |
| laps run out, otherwise | B79 | game over (or TOKY, below) |
| wrecked | B80 | game over |

The race's points add to the score `[0x6A02]` first.

**Game over** (0x516D): the Hall of Fame (HALL1, 0x54A7), then the whole
career resets (0x6A7C: round, score, records, streaks; not the level) and the
show starts again from INTRO. There are no retries.

**Seasons.** A career starts on NOVICE (`[0x68CD]` = 1). Moving on from
round 14 (LST) starts round 1 on HARDENED (0x50D3). On HARDENED, LST counts
only if every opponent is destroyed (laps and wrecks score 0 there, 0x4F92);
doing it completes the game.

**Bonus tracks.**
- *HOLD* (round 16): after three clean rounds in a row (`[0x69B2]`, reset by
  any round that isn't clean), the next round is HOLD (0x50E8). HOLD always
  counts as cleared; a wreck there scores nothing.
- *TOKY* (round 15): a round you fail on the laps with 800+ points, when
  you've banked three clean rounds since the last TOKY (`[0x69B4]`), sends you
  to TOKY instead of game over, at most three times (`[0x69B6]`, 0x502B).
- After either, you go on to the round after the one you last played in the
  championship (`[0x69F6]` + 1).

**The finale** (0x5267): the Hall of Fame, then B82, one of BB3–BB6, B89,
FIN, and the credits (CREDIT over SkyHolder, 0x5198). The score resets and
`[0x69AA]` is set: from then on the round clips are skipped and the race
button lets you pick any track (0x55C7).

**Attract mode** (0x555F, 0x4E9E): when the menu has been left alone for
3000 ticks (`[0x69EC]`) it returns with `[0x69F2]` set. The first time
(`[0x69DC]` = 0) B65 plays and the menu comes back; the next time a demo race
runs (`[0x69E6]`): the first track (0x06EC), car entry 6 of DS:0x826 (OMEGA,
0x0262), rival `_LUI` (`[0x866]` = 0x31, 0x0255). The demo drives at full
throttle unless spinning (0x2D0F); on the straight (curvature within ±11) it
steers toward x 130–190, on curves not at all (`[0x6808]`, 0x22FD); it fires
when the rival is 10–20 eighths ahead and within 50 pixels across, at most
every 30 ticks (0x2C02);
`PRESS BUTTON TO START` blinks at (60, 32) every 50 ticks (0x14B8). Any key
ends it (0x082A); then the car choice resets and the show starts again from
INTRO (0x4F2F). `[0x69DC]` resets after every real race (0x4F23).

**Quitting** (options, 0x5113): B99 after a wreck, BA1 with fewer than four
clean rounds, else B98 and AZUR; then the credits.

## Subtitles

`MEGA<LANG>.SUB` (AMER, ENGL, FREN, GERM, ITAL, SPAN): u16 offsets, the
first being the table's size (63 entries, the last the end), then the
groups, each a run of lines `{ u8 length, text, 0xFF }`. Group *k* belongs to
the video numbered `DS:0x158C[k] − 1` (62 videos: INTRO, B64, B65, B66, the
round and scenery clips, the outcome clips, VWBT2–5; 0x5BF2). The language
is the install's choice (`[0x5145]` & 0x1C, 0x5233).

**Timing is in the videos:** the first word of a frame's video header
carries two bits above the 9-bit width: 0x4000 shows the next line, 0x2000
clears it (0xCEC5 → 0xD24F). Every subtitled video has exactly one 0x4000
cue per line.

**Drawing** (0xD287): rows 191–199, starting at x = 160 − 2.25 × length,
each character from `DFCHAR.BIN` in a cell as wide as its width, colour 255
on 0. `DFCHAR.BIN` is 256 widths, then 9-byte glyphs (a byte per row, high
bit left) from character 0. The accented letters sit on ASCII punctuation
(the French "à" is `{`). The videos leave colour 255 black; the port sets it
white while subtitles are on.

## Hall of Fame

Seven places and a line for your run, 40 bytes each (DS:0x1344…0x145C),
kept in a file. At game over (0x6847) the table is sorted; a score at least
the 7th place's earns a place (0x68C1). The name entry (0x66D6) shows
CONTROL4 frame 3 centred on (160, 90) and types in the FONTOPT font from
(96, 98), 8 pixels apart, with a `_` cursor: up to 16 characters, A–Z (lower
case raised), 0–9 and space, BACKSPACE; ENTER keeps it (one character at
least), ESC leaves the table as it was. The new place gets the score, the
track's name (DS:0x148C + 16 × track) and EASY/HARD; the 8th line always
gets your run (0x693C).

## Music

Six songs, HSQ-packed: `NEWSAN`, `MAEVA`, `DETRITUS`, `LENNY`, `ALARME` and
`PAGA`. They are **HERAD** files, the FM music system Stéphane Picq used for
Cryo (also in Dune), "SDB" version 2: one OPL2 chip, nine voices. The
`DF*.HSQ` / `CR*.HSQ` files are the game's sound drivers (AdLib, Sound
Blaster, Tandy, PC speaker, ...); the port doesn't need them. `DFMID.HSQ`
and `NEWFAC1.LOP` are named in the EXE but aren't on this disc.

**File layout** (after unpacking):

| Offset | Meaning |
|---|---|
| 0x00 | u16 offset of the instrument table |
| 0x02 | u16 × 21 track offsets (+2 = position in the file; 0 = no more tracks). The first is 0x32 (SDB, OPL2) or 0x52 (AGD, OPL3) |
| 0x2C | u16 loop start measure |
| 0x2E | u16 loop end measure |
| 0x30 | u16 loop count (0 = forever) |
| 0x32 | u16 speed |

A track runs from its offset to the next one (the last to the instrument
table). Instruments are 40 bytes each to the end of the file: mode (−1 =
keymap), operator settings for modulator and carrier, then the macro bytes
(velocity/aftertouch sensitivity of the output levels and feedback, pitch
slide, transpose). A keymap picks an instrument per note from C2 up.

**Tracks** are MIDI-like, one per voice: a variable-length delta time (7
bits per byte, high bit = more) then an event. `8x` note off (version 2: one
byte, the note), `9x` note on (note, velocity), `Cx` program, `Dx`
aftertouch (ignored in version 2), `Ex` pitch bend (centre 0x40), `FF` end.
Notes are MIDI numbers; the driver plays note − 24 with F-numbers 343, 364,
385, 408, 433, 459, 486, 515, 546, 579, 614, 650 for C..B.

**Timing.** The driver's timer runs at 200.299 Hz. Each interrupt subtracts
256 from a counter and, when it goes negative, adds the song's speed and
plays one sequencer tick, so a song runs at 200.299 × 256 / speed ticks per
second (MegaRace: 833–1208, about 42–62 ticks/s). A measure is 96 ticks.
Version-2 songs repeat forever: with a loop count set, the whole song; with
none, the marked measures (an intro, then the loop).

| Song | Title (TCRF) | Speed | Instruments | Loop |
|---|---|---|---|---|
| NEWSAN | NewSan | 992 | 48 | whole song |
| MAEVA | Maeva | 833 | 42 | whole song |
| DETRITUS | Factory Land | 958 | 48 | measures 13–104 |
| LENNY | Fractalian Space | 867 | 52 | measures 13–105 |
| ALARME | Terminal City | 967 | 46 | whole song |
| PAGA | SkyHolder | 1208 | 47 | measures 5–85 |

**Which song plays where** (RACEPRG.EXE): song *n* is resource 0x89 + *n*
(0xC402), so 1 = NEWSAN … 6 = PAGA. A race starts DS:0x266[round] + 1
(0x02AA): rounds 1–16 play 1, 2, 3, 4, 5, 1, 2, 4, 3, 5, 2, 3, 1, 4, 6, 6.
After the post-race videos the game starts song 6 (0x5198) and later fades
it with the driver's fade (0xC35E: 300 driver ticks, 1.5 s). Asking for the
song already playing doesn't restart it (0xC33C). The port also plays song 6
in the main menu (what the original plays there isn't traced).

The port's player (src/formats/herad.cpp) was written from these notes and
checked against AdPlug's HERAD player (Stas'M et al., the reference for the
format): for all six songs, every chip register write matches at every
timer tick. AdPlug leaves looping switched off; the port's loop follows the
header as described above.

## Still to decode

- What the timers and flags of codes 6, 8, 9, 10, 23 and 24 do.
- The rival's tilt on curves (`[0x691E]`, 0x3F34), not drawn yet.
- The original's save slots and key redefinition.
- The scaling tables in `_XXX.HSQ` and the game's own rival-car scale rule.
- The six graphics files with the table at the end.
- Lane direction (is lane 0 left or right).
- HSQ graphics (`_JOS.HSQ`, `CASQUE.HSQ`, `PANEL2.HSQ`...): sprite/image layouts.
- `.RAW` fonts.
