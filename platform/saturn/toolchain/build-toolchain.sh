#!/bin/sh
# Builds the SH-2 tool-chain libyaul needs (sh2eb-elf: GCC 14.3 for C and
# C++, binutils), then libyaul and its tools, into $PREFIX (default
# /opt/tool-chains/sh2eb-elf). The same versions and options as libyaul's
# own crosstool-ng configuration (github.com/yaul-org/libyaul-build-scripts),
# without crosstool-ng.
#
#   platform/saturn/toolchain/build-toolchain.sh [work dir]
#
# Needs a host C/C++ compiler, make, texinfo, flex, bison, xorriso and the
# GMP, MPFR and MPC development packages (Ubuntu: libgmp-dev libmpfr-dev
# libmpc-dev). About 30 minutes on four cores. Afterwards:
#   source ~/.yaul.env
set -eu

PREFIX=${PREFIX:-/opt/tool-chains/sh2eb-elf}
TARGET=sh2eb-elf
BINUTILS=${BINUTILS:-2.44}
GCC=${GCC:-14.3.0}
# libyaul is pinned to the commit the Saturn build was tested with.
LIBYAUL_COMMIT=${LIBYAUL_COMMIT:-6012f79f237773378c8014e70d8998ad95a38d98}
GNU_MIRROR=${GNU_MIRROR:-https://ftpmirror.gnu.org/gnu}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}

work=${1:-$PWD/sh2-toolchain-work}
mkdir -p "$work"
cd "$work"

fetch() {   # url
    f=$(basename "$1")
    [ -f "$f" ] || curl -fsSL --retry 3 -o "$f" "$1"
    tar xf "$f"
}

if [ ! -x "$PREFIX/bin/$TARGET-as" ]; then
    fetch "$GNU_MIRROR/binutils/binutils-$BINUTILS.tar.xz"
    mkdir -p build-binutils && cd build-binutils
    "../binutils-$BINUTILS/configure" --target=$TARGET --prefix="$PREFIX" \
        --disable-nls --disable-werror --disable-gdb --disable-sim --enable-lto --enable-plugins
    make -j"$JOBS"
    make install
    cd ..
fi
export PATH="$PREFIX/bin:$PATH"

if [ ! -x "$PREFIX/bin/$TARGET-gcc" ]; then
    fetch "$GNU_MIRROR/gcc/gcc-$GCC/gcc-$GCC.tar.xz"
    mkdir -p build-gcc && cd build-gcc
    # As libyaul's configuration: C and C++, no C library (libyaul has its
    # own), freestanding libstdc++ headers only.
    "../gcc-$GCC/configure" --target=$TARGET --prefix="$PREFIX" \
        --with-cpu=m2 --with-endian=big --disable-multilib \
        --enable-languages=c,c++ --without-headers --disable-nls --disable-shared \
        --disable-threads --disable-libssp --disable-libquadmath --disable-libgomp \
        --disable-libatomic --disable-hosted-libstdcxx --disable-libstdcxx-pch \
        --disable-decimal-float --disable-initfini-array --enable-lto --disable-bootstrap
    make -j"$JOBS" all-gcc all-target-libgcc
    make install-gcc install-target-libgcc
    cd ..
fi

# The environment file libyaul's makefiles read (written every time: a
# cached tool-chain comes without it).
cat > "$HOME/.yaul.env" <<ENV
export YAUL_INSTALL_ROOT=$PREFIX
export YAUL_PROG_SH_PREFIX=
export YAUL_ARCH_SH_PREFIX=$TARGET
export YAUL_ARCH_M68K_PREFIX=m68keb-elf
export YAUL_BUILD_ROOT=$work/libyaul
export YAUL_BUILD=build
export YAUL_OPTION_MALLOC_IMPL=tlsf
export SILENT=1
export NOCOLOR=1
export DEBUG_RELEASE=
export MAKE_ISO_XORRISOFS=$(command -v xorrisofs || echo /usr/bin/xorrisofs)
ENV
# shellcheck disable=SC1091
. "$HOME/.yaul.env"

if [ ! -f "$PREFIX/share/build.pre.mk" ]; then
    if [ ! -d libyaul ]; then
        git init -q libyaul
        (cd libyaul && git remote add origin https://github.com/yaul-org/libyaul.git \
            && git fetch -q --depth 1 origin "$LIBYAUL_COMMIT" && git checkout -q FETCH_HEAD)
    fi
    (cd libyaul && git submodule update --init --depth 1)
    (cd libyaul && make install-release && make install-tools)
fi

echo "Tool-chain ready in $PREFIX. Run: source ~/.yaul.env"
