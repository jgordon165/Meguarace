# HD mode and display modes

MegaRace's videos and menus are 320×200. HD mode shows them upscaled to
1080p in a higher display mode: the videos, plus the menu graphics (the main
menu, the options panels, the Hall of Fame and save/key panels, and the menu
fonts). The rest of the game's own drawing (cars, dashboard, race text) is
drawn sharp on top, and gameplay doesn't change at all.

## Settings: Options > ADVANCED

In the main menu's OPTIONS panel, press Down past QUIT to reach ADVANCED and
press Enter. Everything there is saved as it changes, and Esc goes back.

The defaults: the largest resolution that fits the desktop (1920×1080 on a
1080p screen), 16:9 with AMBIENT sides, LINE TRIPLED, SMOOTHING MORE (and one
level more, MAX, on Lance's clips), the default HD folder and the voice filter on. Without packs the game starts in
320×200, where only the voice filter applies.

| Setting | |
|---|---|
| RESOLUTION | 320×200 (the original), 800×600, 1024×768 or 1920×1080; see below |
| ASPECT | 4:3, or 16:9 (HD resolutions): the screen gets a wing either side of the game's 320×200 (53 game pixels each). The videos stay 4:3 in the middle, since their pictures have nothing at the sides. The race dashboard (the cockpit, gauges and scanner) stretches across the whole width, and the race's LAPS and score move out to the screen's edges, and the ADVANCED box uses the full width. 1920×1080 fills the screen; 1024×768 and 800×600 show a letterboxed 1024×576 / 800×450. |
| SIDES | 16:9's wings where nothing is drawn: AMBIENT, a blurred and darkened stretch of the picture (its 32×20 average, stretched by the GPU), or BLACK. |
| HD STYLE | what the HD resolutions show: the packs' upscale (**REAL-ESRGAN** when `hd-conversion-utility` used it), or **LINE DOUBLED**: the original videos and menus with each pixel repeated and only the seams interpolated. **LINE TRIPLED** is the same with each pixel repeated 3×3 at most before the interpolation, so the seams blend wider: softer than LINE DOUBLED, sharper than the smoothing. Neither needs packs. F10 switches between them while playing. |
| SMOOTHING | OFF, SUBTLE, MORE or MAX: softens every video in the HD resolutions. Line-doubled videos repeat each pixel to a smaller whole-number size before the final linear step (1080p line doubled: 4×3 instead of 6×5 for SUBTLE; line tripled: 2×2 for SUBTLE, 1×1 for MORE), so the seams blend wider; MAX filters the original pixels down below their size and back up, the softest. It covers every video, Lance's clips included. HD pictures pass through a smaller copy. It's all done by the GPU (render-target copies), so it costs no frame time. The cars, dashboard, text and menus stay sharp. |
| LANCE VIDEOS | **1 SMOOTHER** (the default): Lance's clips play one SMOOTHING level softer than the other videos (SUBTLE when SMOOTHING is OFF, MAX at most), or **SAME**. |
| HD FOLDER | where the packs are (Enter edits it; see below) |
| MUSIC | ORIGINAL, STEREO FM, SOUNDFONT or ENHANCED (the default: a dance remix of the songs): see [music.md](music.md) |
| VOICE FILTER | the hiss filter on Lance's clips (also V while one plays) |

## Display modes

| Mode | Window | Picture | |
|---|---|---|---|
| `native` | 320×200 × `--scale` | 4:3 | the original look; the only mode without HD videos |
| `800x600` | 800×600 | 800×600 | HD videos, scaled down |
| `1024x768` | 1024×768 | 1024×768 | HD videos, scaled down |
| `1920x1080` | 1920×1080 (whole screen when it doesn't fit a window) | 1440×1080, pillarboxed | HD videos at full size |

Pick one in **Options > ADVANCED > RESOLUTION** (Left/Right or Enter). It's saved in
`meguarace.sav`, or you can give it with `--display <mode>`, which takes
precedence over the saved one. Without a setting the game picks the largest
mode that fits your desktop. When there are no HD packs, only `native` is
offered and a saved HD mode is ignored.

**The game always works in the original 320×200 pixels.** Every position,
mask, hit test and layout table read from RACEPRG.EXE is in those
coordinates, and `gfx.width()` / `gfx.height()` are 320 and 200 in every
mode, so gameplay is identical at every resolution. A mode only decides how
that screen is shown. Anything that does need display pixels asks the
engine: `gfx.display()` returns the mode and its picture size, and
`gfx.to_display(x, y)` maps a game point to display pixels. In `native`
those are the original 320×200 shown at 4:3.

In 16:9 the canvas is 426×200: the game's 320×200 in the middle and a wing
of `gfx.wing()` (53) pixels each side. Drawing still uses game coordinates,
so x = `-gfx.wing()` is the left edge and `320 + gfx.wing()` the right; code
that never looks at `gfx.wing()` draws exactly where it did. The 16:9
pixels keep the original's shape: 426 game pixels span 16:9 of 200 rows,
just as 320 span 4:3.

## Two programs

- **The game** (`megarace`) always has the display modes, the HD FOLDER
  setting and HD video playback. Every game build and package has them; it
  just shows the original videos until there are packs.
- **`hd-conversion-utility`** makes the packs. It's a separate utility with
  its own build option (`-DMEGARACE_HD_CONVERTER=ON`), its own CI
  (`hd-converter`) and its own packages (`release-hd-converter`). Its package
  unpacks into the same `Meguarace/` folder as the game, but the game
  doesn't need it to run.

## Where the HD videos come from

**They are made on the player's machine, from the player's own CD.** An
upscaled video is still the original game's content: changing its resolution
or format doesn't change who owns it. So the HD videos are handled exactly
like the rest of the game data:

- they are never committed (`*.hdv` is git-ignored),
- CI never makes them,
- no build ships them; release packages fail if one is staged.

What gets shipped is the **converter**, `hd-conversion-utility`, in its own
package with the Real-ESRGAN upscaler it uses. The game, the converter and
Real-ESRGAN are all freely redistributable.

## Making the HD videos

```
hd-conversion-utility --data V:\
```

This writes one `.hdv` pack per video and one `.hds` pack per menu sprite set
to the **HD folder**, and the game reads them from there. The folder is a
setting in the game: **Options > ADVANCED > HD FOLDER**. Press Enter, type or paste (Ctrl+V) a
folder, then Enter to keep it or Esc to leave it as it was. An empty line
means the default: `hd/` in your Meguarace user folder, next to
`meguarace.sav`. Set the folder first, for example on a drive with room for
the 3.5–4 GB, then run `hd-conversion-utility`. It writes to the same place, because
it reads the same setting (`hd.cfg` in the user folder).

Run it once; running it again only redoes videos that changed, or packs made
by an older version.

Neither program takes a file or program path on its command line. Pack
file names are only ever the archive's own video names (`B01.HNM` gives
`B01.hdv`), checked so that nothing in an archive can point outside the
folder. Real-ESRGAN only runs from the `realesrgan/` folder next to
`hd-conversion-utility`, started directly rather than through a shell, and only with
a model from a fixed list.

Videos are converted **once, at the largest mode**: 1440×1080, the 4:3
picture of 1080p. 1024×768 and 800×600 scale that down on the GPU while
playing, so there's only one set of packs.

| Option | |
|---|---|
| `--upscaler realesrgan` | AI upscaling that adds detail: clean outlines, sharp bevels (the default when `realesrgan/` is next to `hd-conversion-utility`). It uses the GPU through Vulkan, upscaling 4× to 1280×800, which is then resized to 1440×1080. The utility's package bundles it, and so do builds from source (`MEGARACE_FETCH_REALESRGAN`, on by default with the converter). |
| `--upscaler sharp` | Line doubling and interpolating: each original pixel repeated, only the seams blended. The original, cleanly enlarged; no detail added (the default without Real-ESRGAN). This is also what the game's LINE DOUBLED style shows without any packs. |
| `--upscaler smooth` | A Catmull-Rom interpolation: softer, a little blurry. |
| `--model NAME` | Real-ESRGAN model: `realesr-animevideov3` (default), `realesrgan-x4plus` or `realesrgan-x4plus-anime`; its files go in `realesrgan/models`. |
| `--quality Q\|lossless` | WebP quality (default 90). |
| `--only PATTERN` | Only some videos, e.g. `--only 'N*.HNM'`. |
| `--list` | List the videos and their frame counts. |

The disc holds 109 videos and 65,234 frames. Measured on the US v1.0.1 disc
with the built-in upscaler: the 1440×1080 packs come to roughly 3.5–4 GB
(NS11 alone is 62 MB). A run at the old 640×400 size took 14 minutes on 2
cores and made 1.4 GB; 1440×1080 takes a few times longer per frame, and a
desktop with more cores goes proportionally faster. Real-ESRGAN's speed
depends on the GPU.

## Playing

```
megarace --data V:\                        # packs from the HD folder setting, saved display mode
megarace --data V:\ --display 1024x768     # this mode, this time
megarace --data V:\ --no-hd                # original videos, native mode
```

**F10** switches between the HD packs and LINE DOUBLED (or TRIPLED) within a display
mode, to compare them; the saved choice is Options > ADVANCED > HD STYLE.

## Menu graphics

`hd-conversion-utility` also upscales the menu sprite sets: PANEL2, KONTROL,
KONTROL6, CONTROL2, CONTROL4, FONTOPT, FONTOPT2, FONTOPT3 and FONTHALL. That
takes a few seconds and about 0.5 MB in all. There are two kinds:

- **colour** sets carry a palette that covers every colour they use
  (PANEL2, KONTROL, CONTROL2). They're upscaled like the videos, with alpha
  for their edges.
- **indexed** sets are drawn in the colours of the screen they're on
  (CONTROL4, KONTROL6, the fonts). Each colour's region is traced smooth, and
  every HD pixel keeps its palette index, so the engine colours them as it
  draws and they're always right.

In both, the engine checks pixel by pixel: where the game has changed a
colour since the picture was made (a fade, an index recoloured for a
highlight), those pixels show the original, so colours are always as the
game means them. The same check applies to the videos.

## How it works

- **Pack (`.hdv`)**: one per HNM video. For every frame it holds the whole
  composited screen as a 1440×1080 WebP picture (square pixels; the
  original's 320×200 is shown at 4:3), as the original shows it after that frame.
  Overlay frames are drawn over the previous picture, as on the original.
  Frames that don't change the picture point back to the last one. The
  header stores the CRC-32 of the source `.HNM`, so a pack only loads over
  exactly the video it was made from. Layout: `src/hd/hdpack.h`.
- **Engine**: the 320×200 indexed framebuffer is unchanged. Masks, hit tests,
  `gfx.pget` and Lua screenshots work as before. Alongside it, each pixel
  records whether it last came from an HD video. In a display mode the GPU
  draws two layers:
  - the **HD layer**: the video pictures at 1440×1080, scaled linearly to
    the mode's picture;
  - the **video layer**: videos shown from their original pixels (no pack,
    or LINE DOUBLED), scaled the same way as the game layer, or softer with
    SMOOTHING;
  - the **game layer**: the 320×200 framebuffer, transparent where a pixel
    shows the HD or video layer. It is scaled sharp: nearest to a whole multiple of
    320×200 (1600×1200 for 1080p), then linearly to the picture, so every
    original pixel comes out the same size.
- **Palette guard**: an HD picture has the video's own colours baked in. If
  the game has changed any colour the frame uses (a fade, a recolour), that
  frame draws from the original pixels instead, so it always looks the way
  the game means it to.
- **Lua**: `sys.hd` (packs found), `sys.hd_width` / `sys.hd_height` (their
  picture size), `sys.hd_path`, `sys.display_arg`; `sys.hd_folder()` and
  `sys.set_hd_folder(folder)` (the setting; setting it looks for packs
  again straight away); `input.text_start()`, `text_stop()`, `text()` and
  `paste()` for typing it; `gfx.displays()`,
  `gfx.display()`, `gfx.set_display(name)`, `gfx.to_display(x, y)`;
  `v:hd()` on a video (true when it plays from a pack), and
  `v:blit(i, x, y, w, h, pixels, key)`, which is `gfx.blit` for frame i of
  the video. The cutscene player uses `v:blit` because it decodes frames
  ahead and draws them later. The setting itself lives in
  `scripts/game/display.lua`.
- **Diagnostics**: set `MEGARACE_HD_STATS=1` to print, for each video, how
  many frames came from the pack and how many fell back to the original.

## Builds

| Workflow | What |
|---|---|
| `CI-build`, `release-*` | the game, HD playback included |
| `hd-converter` | the converter's CI: builds `hd-conversion-utility` and runs its tests on Linux, macOS (arm64) and Windows x64, on pushes that touch it |
| `release-hd-converter` | the converter's packages, run by hand: `hd-conversion-utility-linux-x64-<version>.tar.gz`, `-windows-x64-<version>.zip` and `-macos-universal-<version>.zip` (signed and notarized if the secrets are set) |

A converter package holds `hd-conversion-utility(.exe)`, `docs/hd.md`, and
`realesrgan/`: Real-ESRGAN ncnn-vulkan from the pinned upstream v0.2.5.0
release, with only the `realesr-animevideov3` models, and its licence note.
It never holds an HD pack; packaging fails if one gets staged.

Build the converter yourself with
`cmake -S . -B build -DMEGARACE_HD_CONVERTER=ON` and
`cmake --build build --target hd-conversion-utility`.

Licences: libwebp (BSD-3) is compiled into both programs, and
stb_image / stb_image_write (public domain or MIT) into the converter. Real-ESRGAN ncnn-vulkan is MIT; Real-ESRGAN and its
models are BSD-3. The upstream project has no separate licence statement for
the model weights, so check its terms before a public release.

## Not done yet

- HD race sprites and dashboard (cars, scanner, HUD are still drawn at
  320×200 and scaled up sharp; the menus are done).
- Frame prefetching. Each HD frame is decoded when it is drawn (a few
  milliseconds at x2, more at x4); a decoder thread would take that off the
  frame time.
- A fullscreen switch for the smaller modes (1080p goes fullscreen by
  itself when it doesn't fit in a window).

Tested on the US v1.0.1 disc: all 109 videos convert. Cutscenes, the menu
and a 3,000-frame demo race on NS11 draw every frame from the packs, and
each HD frame matches its original (a mean difference of about 1.3/255 per
channel, from compression and filtering).
