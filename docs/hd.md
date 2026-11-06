# HD mode and display modes

MegaRace's videos are 320×200. HD mode plays upscaled versions of them in a
higher display mode. The game's own drawing (cars, dashboard, text, menus)
stays pixel-exact on top, and gameplay doesn't change at all.

## Display modes

| Mode | Window | Picture | |
|---|---|---|---|
| `native` | 320×200 × `--scale` | 4:3 | the original look; the only mode without HD videos |
| `800x600` | 800×600 | 800×600 | HD videos, scaled down |
| `1024x768` | 1024×768 | 1024×768 | HD videos, scaled down |
| `1920x1080` | 1920×1080 (whole screen when it doesn't fit a window) | 1440×1080, pillarboxed | HD videos at full size |

Pick one in **Options > DISPLAY** (Left/Right or Enter). It's saved in
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
(and in builds without HD) those are the original 320×200 shown at 4:3.

This work lives on the `release/hd` branch for now. It is planned to merge
into `master` as an option (`-DMEGARACE_HD`, off by default there).

## Where the HD videos come from

**They are made on the player's machine, from the player's own CD.** An
upscaled video is still the original game's content: changing its resolution
or format doesn't change who owns it. So the HD videos are handled exactly
like the rest of the game data:

- they are never committed (`*.hdv` is git-ignored),
- CI never makes them,
- no build ships them; release packages fail if one is staged.

What the HD builds ship is the **converter**, `megarace-hd`, plus the
Real-ESRGAN upscaler it uses. The engine, the converter and Real-ESRGAN are
all freely redistributable.

## Making the HD videos

```
megarace-hd --data V:\
```

This writes one `.hdv` pack per video to your Meguarace user folder (the
same place as `meguarace.sav`, under `hd/`). The game finds them there by
itself. Run it once; running it again only redoes videos that changed, or
packs made by an older version.

Videos are converted **once, at the largest mode**: 1440×1080, the 4:3
picture of 1080p. 1024×768 and 800×600 scale that down on the GPU while
playing, so there's only one set of packs.

| Option | |
|---|---|
| `--upscaler realesrgan\|builtin` | Real-ESRGAN (GPU, Vulkan) when it is bundled or given with `--realesrgan`: it upscales 4× to 1280×800, which is then resized to 1440×1080. Otherwise a built-in Catmull-Rom resize straight to 1440×1080, which runs anywhere. |
| `--realesrgan <path>` | A `realesrgan-ncnn-vulkan` program to use. |
| `--model NAME` | Real-ESRGAN model (default `realesr-animevideov3`). It must do 4×. |
| `--quality Q\|lossless` | WebP quality (default 90). |
| `--only PATTERN` | Only some videos, e.g. `--only 'N*.HNM'`. |
| `--out <folder>` | Somewhere else (then run the game with `--hd <folder>`). |
| `--list` | List the videos and their frame counts. |

The disc holds 109 videos and 65,234 frames. Measured on the US v1.0.1 disc
with the built-in upscaler: the 1440×1080 packs come to roughly 3.5–4 GB
(NS11 alone is 62 MB). A run at the old 640×400 size took 14 minutes on 2
cores and made 1.4 GB; 1440×1080 takes a few times longer per frame, and a
desktop with more cores goes proportionally faster. Real-ESRGAN's speed
depends on the GPU.

## Playing

```
megarace --data V:\                        # packs in your user folder, saved display mode
megarace --data V:\ --display 1024x768     # this mode, this time
megarace --data V:\ --hd D:\packs          # packs somewhere else
megarace --data V:\ --no-hd                # original videos, native mode
```

**F10** switches the HD videos on and off within a display mode, to
compare them with the original videos.

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
  - the **game layer**: the 320×200 framebuffer, transparent where a pixel
    shows the HD layer. It is scaled sharp: nearest to a whole multiple of
    320×200 (1600×1200 for 1080p), then linearly to the picture, so every
    original pixel comes out the same size.
- **Palette guard**: an HD picture has the video's own colours baked in. If
  the game has changed any colour the frame uses (a fade, a recolour), that
  frame draws from the original pixels instead, so it always looks the way
  the game means it to.
- **Lua**: `sys.hd` (packs found), `sys.hd_width` / `sys.hd_height` (their
  picture size), `sys.hd_path`, `sys.display_arg`; `gfx.displays()`,
  `gfx.display()`, `gfx.set_display(name)`, `gfx.to_display(x, y)`;
  `v:hd()` on a video (true when it plays from a pack), and
  `v:blit(i, x, y, w, h, pixels, key)`, which is `gfx.blit` for frame i of
  the video. The cutscene player uses `v:blit` because it decodes frames
  ahead and draws them later. The setting itself lives in
  `scripts/game/display.lua`.
- **Diagnostics**: set `MEGARACE_HD_STATS=1` to print, for each video, how
  many frames came from the pack and how many fell back to the original.

## Builds

| Workflow | Package |
|---|---|
| `release-hd-linux` | `meguarace-hd-linux-x64-<version>.tar.gz` |
| `release-hd-windows` | `meguarace-hd-windows-x64-<version>.zip` |
| `release-hd-mac-osx` | `meguarace-hd-macos-universal-<version>.zip` (signed and notarized if the secrets are set) |
| `release-hd-all` | all three |

Each one is the standard package plus `megarace-hd` and `realesrgan/`:
Real-ESRGAN ncnn-vulkan from the pinned upstream v0.2.5.0 release, with only
the `realesr-animevideov3` models, and its licence note. The standard
`release-*` workflows build with `-DMEGARACE_HD=OFF`, so they are unchanged.

Licences: libwebp (BSD-3) and stb_image / stb_image_write (public domain
or MIT) are compiled in. Real-ESRGAN ncnn-vulkan is MIT; Real-ESRGAN and its
models are BSD-3. The upstream project has no separate licence statement for
the model weights, so check its terms before a public release.

## Not done yet

- HD sprites and dashboards (car sprites, HUD and fonts are still drawn at
  320×200 and scaled up sharp).
- Frame prefetching. Each HD frame is decoded when it is drawn (a few
  milliseconds at x2, more at x4); a decoder thread would take that off the
  frame time.
- A fullscreen switch for the smaller modes (1080p goes fullscreen by
  itself when it doesn't fit in a window).

Tested on the US v1.0.1 disc: all 109 videos convert. Cutscenes, the menu
and a 3,000-frame demo race on NS11 draw every frame from the packs, and
each HD frame matches its original (a mean difference of about 1.3/255 per
channel, from compression and filtering).
