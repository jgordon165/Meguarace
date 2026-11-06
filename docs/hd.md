# HD mode

MegaRace's videos are 320×200. HD mode plays upscaled versions of them. The
game's own drawing (cars, dashboard, text, menus) stays pixel-exact on top,
and gameplay doesn't change at all.

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
itself. Run it once; running it again only redoes videos that changed or
were made at another scale.

| Option | |
|---|---|
| `--scale 2\|3\|4` | Upscale factor (default 2). All packs in a folder must share one scale. |
| `--upscaler realesrgan\|builtin` | Real-ESRGAN (GPU, Vulkan) when it is bundled or given with `--realesrgan`; otherwise a built-in Catmull-Rom resize that runs anywhere. |
| `--realesrgan <path>` | A `realesrgan-ncnn-vulkan` program to use. |
| `--model NAME` | Real-ESRGAN model (default `realesr-animevideov3`, which does x2, x3 and x4). |
| `--quality Q\|lossless` | WebP quality (default 90). |
| `--only PATTERN` | Only some videos, e.g. `--only 'N*.HNM'`. |
| `--out <folder>` | Somewhere else (then run the game with `--hd <folder>`). |
| `--list` | List the videos and their frame counts. |

The disc holds 109 videos and about 65,000 frames, so a full run takes a
while: the built-in upscaler uses every CPU core, and Real-ESRGAN's speed
depends on the GPU.

## Playing

```
megarace --data V:\               # uses the packs in your user folder, if any
megarace --data V:\ --hd D:\packs # packs somewhere else
megarace --data V:\ --no-hd       # original videos
```

**F10** switches HD on and off while playing. When HD is on, the window's
default size is the HD scale or 3×, whichever is larger.

## How it works

- **Pack (`.hdv`)**: one per HNM video. For every frame it holds the whole
  composited screen at K× (WebP), as the original shows it after that frame.
  Overlay frames are drawn over the previous picture, as on the original.
  Frames that don't change the picture point back to the last one. The
  header stores the CRC-32 of the source `.HNM`, so a pack only loads over
  exactly the video it was made from. Layout: `src/hd/hdpack.h`.
- **Engine**: the 320×200 indexed framebuffer is unchanged. Masks, hit tests,
  `gfx.pget` and Lua screenshots work as before. Alongside it, each pixel
  records whether it last came from an HD video. `present()` composes a K×
  screen from both: HD pixels from the pack, and everything else from the
  palette, scaled up (`src/hd/compose.h`).
- **Palette guard**: an HD picture has the video's own colours baked in. If
  the game has changed any colour the frame uses (a fade, a recolour), that
  frame draws from the original pixels instead, so it always looks the way
  the game means it to.
- **Lua**: `sys.hd_scale` (0 without HD), `sys.hd_path`, `v:hd()` on a
  video (true when it plays from a pack), and `v:blit(i, x, y, w, h, pixels,
  key)`: `gfx.blit` for frame i of the video, which the cutscene player uses
  because it decodes frames ahead and draws them later.
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
- An HD switch in the in-game options menu (needed for the merge into
  `master`).
- Checking on the real disc. The converter and engine are tested on
  hand-built videos (`tests/test_hd.cpp`), not yet on all 109 videos.
