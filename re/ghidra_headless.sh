#!/usr/bin/env bash
# Import the game's executable into a local Ghidra project and run auto-analysis,
# then export decompiled C for every function into re/output/.
#
#   GHIDRA_HOME=/path/to/ghidra_11.x re/ghidra_headless.sh /path/to/your/MEGARACE.EXE [processor]
#
# processor defaults to 16-bit real mode. For an unpacked 32-bit build use
# "x86:LE:32:default". Output stays in re/ (git-ignored): decompiled original
# code is reference material for writing the port, not something to publish.
set -euo pipefail

EXE="${1:?usage: $0 <exe> [processor]}"
PROC="${2:-x86:LE:16:Real Mode}"
HERE="$(cd "$(dirname "$0")" && pwd)"
: "${GHIDRA_HOME:?set GHIDRA_HOME to your Ghidra install}"

mkdir -p "$HERE/ghidra_project" "$HERE/output"

"$GHIDRA_HOME/support/analyzeHeadless" \
    "$HERE/ghidra_project" megarace \
    -import "$EXE" -overwrite \
    -processor "$PROC" \
    -scriptPath "$HERE/scripts" \
    -postScript ExportDecompiled.java "$HERE/output"

echo "decompiled C written to $HERE/output/"
