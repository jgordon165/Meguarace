#!/bin/sh
# Boots the Saturn build in an emulator on a synthetic MEGARACE.DAT and checks
# the picture and sound (no game data and no Sega BIOS needed).
#
#   tests/saturn/run_emulator_test.sh <saturn build dir> <work dir> [yabause checkout]
#
# The build dir is platform/saturn/build (A.BIN, IP.BIN). The work dir gets
# the test disc, the screenshots and the audio. Without a Yabause checkout,
# one is cloned into the work dir (github.com/libretro/yabause), patched
# (tests/saturn/patch_yabause.py) and built.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
build=$(cd "$1" && pwd)
mkdir -p "$2"
work=$(cd "$2" && pwd)
yab=${3:-$work/yabause}

if [ ! -f "$yab/yabause/src/libretro/yabause_libretro.so" ]; then
    [ -d "$yab" ] || git clone --depth 1 https://github.com/libretro/yabause.git "$yab"
    python3 "$here/patch_yabause.py" "$yab/yabause/src/cs2.c"
    make -C "$yab/yabause/src/libretro" -j"$(nproc 2>/dev/null || echo 2)" HAVE_SSE=0 >/dev/null
fi
core="$yab/yabause/src/libretro/yabause_libretro.so"

cc -O2 -I"$yab/yabause/src/libretro/libretro-common/include" -o "$work/lrhost" "$here/lrhost.c" -ldl
mkdir -p /tmp/lrsys

python3 "$here/make_test_dat.py" "$work/cd" >/dev/null
python3 "$here/../../tools/saturn_disc.py" --data "$work/cd" --build "$build" --out "$work/megatest" >/dev/null

shots=""
f=20
while [ $f -le 700 ]; do shots="$shots shot@$f"; f=$((f + 10)); done
rm -f "$work"/run_*.ppm
# shellcheck disable=SC2086
"$work/lrhost" "$core" "$work/megatest.cue" 720 "$work/run" $shots
python3 "$here/check_emulator.py" "$work/run"

# Lua: the PC's Lua test suite on the Saturn's Lua (a disc built with
# --lua-test), against the same suite on the PC with the same set-up.
lua="$here/../../platform/saturn/third_party/lua"
[ -f "$lua/lua.h" ] || "$here/../../platform/saturn/deps.sh"
lua_srcs=$(ls "$lua"/*.c | grep -vE '/(lua|luac|onelua|ltests|linit|liolib|loslib)\.c$')
# shellcheck disable=SC2086
cc -O1 -w -I"$lua" -o "$work/lua_stub_runner" "$here/lua_stub_runner.c" $lua_srcs -lm
(cd "$here/../.." && "$work/lua_stub_runner" scripts tests/lua/main.lua) > "$work/lua_pc.txt"
python3 "$here/../../tools/saturn_disc.py" --data "$work/cd" --build "$build" --out "$work/luatest" --lua-test >/dev/null
rm -f "$work"/lua_*.ppm
"$work/lrhost" "$core" "$work/luatest.cue" 601 "$work/lua" shot@600
python3 "$here/check_emulator.py" --lua "$work/lua_00600.ppm" "$work/lua_pc.txt"
