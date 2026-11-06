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
| `<XX>_B`, `<XX>_C` | wheel frames drawn over the body, alternated to spin the wheels. Bottom-left aligned on images 0–10; offsets for 11–16 are not decoded yet |
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
| `CONTROL.HSQ` | options: 0 OPTIONS panel, 1 LEVEL panel, then (off, lit) pairs NOVICE 2/3, HARDENED 4/5, LOAD 6/7, SAVE 8/9, KEYBOARD 10/11, JOYSTICK 12/13, MOUSE 14/15, QUIT 16/17. `KONTROL`/`QONTROL` are other languages |
| `FONTHALL.HSQ` | 9×9 font, glyph index = ASCII code (96 glyphs; DOS arrows at 24–27) |
| `VWBT*.HNM` | channel idents (played before each race in this port) |

Overlay positions on `PANEL1`, found by matching `PANEL2`'s images against
the backdrop pixel for pixel (bars and buttons match exactly):

| | Slot 1–8 (left, right per row, top to bottom) |
|---|---|
| bar | (102,4) (206,4) (84,33) (223,33) (80,65) (226,65) (92,100) (213,100) |
| thumbnail | (47,3) (222,3) (26,33) (237,33) (20,65) (242,65) (27,99) (230,99) |
| buttons | green (58,146), yellow (133,146), blue (211,146) |

Monitor order = thumbnail order = showcase videos: JOSE, HOOPER, LUIS,
OUZBEL, MARIA, PALOMA, OMEGA, RAMON (confirmed by comparing each
thumbnail with its video).

From RACEPRG.EXE (read at runtime from the player's copy):
- **Default Hall of Fame:** 8 × { u16 score, name[18], "00000 ", track[10],
  level[4] }, starting 2 bytes before "BIG BOB".
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
| 2 | MALUS11 | `[0x690D]` − 10 (if ≥ 10); called "points" here |
| 3 | — | forced braking until speed ≤ 130 (`[0x68CF]`) |
| 4 | — | spin: steering sequence + 5 px/tick drift (speed > 15), else as 7 |
| 5 | PLUARME | weapon lost, energy 0 |
| 6 | MALUS7 | radar jammed: `[0x68EB]` for `[0x812]` ticks (the radar blinks instead of showing the rival) |
| 7 | — | skid: steering sequence + 2 px/tick drift (speed ≥ 8) |
| 8 | BONUS16 | flag `[0x68DD]` (not traced yet) |
| 9 | MALUS6 | gauges out: `[0x68E7]` for `[0x812]` ticks (every dashboard gauge reads 0) |
| 10 | MALUS2 | timer `[0x68FB]`, 800 ticks (not traced yet) |
| 17 | ENERGYP | energy + `[0x68DB]` (then 70) |
| 18 | BONUS1 | `[0x690D]` + 10 |
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

| Car | gun 1 | gun 2 | gun 3 | charge | armour | power | `[0x80C]` | `[0x80E]` | `[0x810]` | `[0x812]` |
|---|---|---|---|---|---|---|---|---|---|---|
| JO | 1 | 0 | 0 | 0 | 120 | 8 | 5 | 1 | 1 | 200 |
| RA | 1 | 1 | 1 | 1 | 70 | 20 | 4 | 0 | 0 | 200 |
| OU | 1 | 1 | 1 | 1 | 70 | 20 | 6 | 1 | 1 | 200 |
| PA | 1 | 1 | 0 | 0 | 120 | 16 | 6 | 0 | 0 | 100 |
| LU | 1 | 1 | 1 | 0 | 100 | 10 | 6 | 1 | 1 | 200 |
| OM | 1 | 1 | 0 | 0 | 80 | 10 | 4 | 1 | 1 | 100 |
| HO | 1 | 1 | 0 | 0 | 110 | 10 | 4 | 0 | 0 | 100 |
| MA | 1 | 1 | 1 | 0 | 100 | 16 | 2 | 1 | 1 | 200 |

Armour starts the third gauge `[0xD2E]`. Power is the damage per hit and the
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

Other armour losses, not in the port yet: −8 at the end of some slides
(0x2136, `[0x6933]`) and −1 per smoke cycle while `[0x68FF]` is set (0x3098).

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
`[0x68CB]` = −3); the port still has three on the road at once.

**Status line** (0x150A), in the RAW fonts, over the picture: `LAPS:` at
(2, 6), the laps left `[0x68C5]` (3; 2 on HOLD) at (50, 6), and the score
`[0x6A02]` + `[0x690D]` in six digits at (250, 6).

**RAW fonts** (0x1600): 320-pixel-wide strips. `FONTE1` (digits) and
`FONTE2` (`:` onwards, so letters) are 8 × 12 glyphs, glyph *k* at column
8*k* − 1 (0 for *k* = 0), 10 pixels apart; `FONTE6_6` is 6 × 6 digits at
column 6*k*, 6 apart. Pixels are colours 79 and 255; over the dashboard the
glyph box is cleared to black first.

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
- `[0x80C]` (likely handling) and the units of the shot range `[0xE0B]`.
- Rival AI (the state machine around 0x34C5…0x37C7) and collisions (the port's are placeholders).
- One rival at a time and the opponents count as the way to win (see Dashboard).
- The original options-menu layout and championship scoring.
- Wheel-frame offsets for car images 11–16.
- The scaling tables in `_XXX.HSQ` and the game's own rival-car scale rule.
- The six graphics files with the table at the end.
- Lane direction (is lane 0 left or right).
- HSQ graphics (`_JOS.HSQ`, `CASQUE.HSQ`, `PANEL2.HSQ`...): sprite/image layouts.
- `.SUB` subtitle timing (text is plain; a header of offsets precedes it).
- `.RAW` fonts.
