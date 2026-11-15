#!/bin/sh
# Fetches the third-party code the Saturn build compiles in, at pinned
# versions, into platform/saturn/third_party (git-ignored):
#   lua     Lua 5.4.7, the same as the PC build (MIT)
#   printf  Eyal Rozenberg's printf: the formatting Lua needs (floats); libyaul's
#           printf has none (MIT)
#   musl    the float maths of the musl C library: libyaul has no libm (MIT)
# make runs this when third_party is missing.
set -eu
cd "$(dirname "$0")"
mkdir -p third_party
cd third_party

get() {   # dir url commit-or-tag
    if [ -d "$1/.git" ]; then return; fi
    git init -q "$1"
    (cd "$1" && git remote add origin "$2" && git fetch -q --depth 1 origin "$3" && git checkout -q FETCH_HEAD)
}

get lua    https://github.com/lua/lua.git        v5.4.7
get printf https://github.com/eyalroz/printf.git 22318ed6bd87b0d597b4a4868d5b00daf66540fd
get musl   https://github.com/kraj/musl.git      3e699121d78610e42a4eb36c3420486f2bfb5217

# Lua for the SH-2, which has no FPU: 32-bit integers and single-precision
# floats (LUA_32BITS; luaconf.h sets it unconditionally, so in this copy).
sed -i.orig 's/^#define LUA_32BITS[[:space:]]*0$/#define LUA_32BITS	1/' lua/luaconf.h
grep -q '^#define LUA_32BITS[[:space:]]*1$' lua/luaconf.h
