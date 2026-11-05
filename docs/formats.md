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
| `pt` | ★ Track videos only. 58–5,800 bytes per frame; per-frame track geometry for gameplay (next to decode) |
| `kl` | ★ Track videos only. Roughly 200–600 bytes per frame; probably collision/zones |

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

## Still to decode

- `pt` / `kl` chunks and `.ZNN`: track geometry and collision for gameplay.
- HSQ graphics (`_JOS.HSQ`, `CASQUE.HSQ`, `PANEL2.HSQ`...): sprite/image layouts.
- `.SUB` subtitle timing (text is plain; a header of offsets precedes it).
- `.RAW` fonts.
