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

The disc has a single data track: no Red Book CD audio. Music and speech
live in the video files' `sd` chunks.

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

## Still to decode

- `.ZNN` code meanings (which bonus/penalty each value is).
- Wheel-frame offsets for car images 11–16.
- The scaling tables in `_XXX.HSQ` and the game's own rival-car scale rule.
- The six graphics files with the table at the end.
- Lane direction (is lane 0 left or right).
- HSQ graphics (`_JOS.HSQ`, `CASQUE.HSQ`, `PANEL2.HSQ`...): sprite/image layouts.
- `.SUB` subtitle timing (text is plain; a header of offsets precedes it).
- `.RAW` fonts.
