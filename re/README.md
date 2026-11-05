# Reverse engineering workflow

Everything produced here (Ghidra project, decompiled C) is **git-ignored**.
It's reference material for writing the port, not part of it. The port's
own code in `src/` and `scripts/` is written fresh from what we learn.

## 1. Identify the executable

```
python3 -I tools/identify_exe.py /path/to/your/cd/MEGARACE.EXE
```

| Result | What to do |
|---|---|
| real-mode MZ, no packer | Run `ghidra_headless.sh` as is (16-bit real mode). |
| packed (LZEXE, PKLITE, EXEPACK) | Unpack first with UNP, or dump the unpacked image from DOSBox's debugger, then re-identify. |
| 32-bit LE/LX (DOS/4GW etc.) | Ghidra has no built-in LE loader. Either install a community LE/LX loader extension, or convert the image with a LE-to-ELF/PE converter, then load as `x86:LE:32:default`. |
| big overlay | The game keeps code or data appended to the EXE; note the offset for later. |

## 2. Decompile

```
GHIDRA_HOME=~/ghidra_11.x re/ghidra_headless.sh /path/to/MEGARACE.EXE
```

Produces `re/output/all_functions.c` and `re/output/functions.tsv`.

## 3. Map subsystems, then rewrite

Work from the data inward:

1. **File access**: find calls to DOS `int 21h` open/read (AH=3Dh/3Fh) and the strings naming asset files. That shows each loader and therefore each format.
2. **Formats**: document each in `docs/formats/<name>.md`, confirm it with the asset explorer (`V` raw view, hex view), then write the decoder in `scripts/formats/` (light formats) or `src/formats/` (video).
3. **Game logic**: car physics, AI, collision with the video track, weapons, scoring and menus become Lua modules under `scripts/`, written from your notes on the decompiled behaviour rather than translated line by line.

Tools worth having alongside: DOSBox-X with the debugger (watch memory while the real game runs), and a hex editor.
