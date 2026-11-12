# Meguarace for the Sega Saturn (libyaul). See docs/saturn.md.
#
#   source ~/.yaul.env        # YAUL_INSTALL_ROOT etc.
#   make                      # build/A.BIN, build/IP.BIN (+ a data-less test image)
#
# Then make the playable disc from your own MegaRace CD:
#   python ../../tools/saturn_disc.py --data <your CD> --out megarace

ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
  $(error Undefined YAUL_INSTALL_ROOT (install root directory): source your .yaul.env)
endif

include $(YAUL_INSTALL_ROOT)/share/build.pre.mk

# Third-party code (deps.sh fetches it): Lua 5.4.7, printf, musl's float maths.
TP:= third_party
ifeq ($(wildcard $(TP)/lua/lua.h),)
  $(shell ./deps.sh >&2)
endif

LUA_SRCS:= $(addprefix $(TP)/lua/, \
	lapi.c lauxlib.c lbaselib.c lcode.c lcorolib.c lctype.c ldblib.c ldebug.c \
	ldo.c ldump.c lfunc.c lgc.c llex.c lmathlib.c lmem.c loadlib.c lobject.c \
	lopcodes.c lparser.c lstate.c lstring.c lstrlib.c ltable.c ltablib.c ltm.c \
	lundump.c lutf8lib.c lvm.c lzio.c)

MUSL_SRCS:= $(addprefix $(TP)/musl/src/math/, \
	floorf.c ceilf.c fabsf.c fmodf.c sqrtf.c sqrt_data.c powf.c powf_data.c \
	expf.c exp2f_data.c logf.c logf_data.c log2f.c log2f_data.c log10f.c \
	sinf.c cosf.c tanf.c asinf.c acosf.c atanf.c atan2f.c frexpf.c ldexpf.c \
	scalbnf.c __sindf.c __cosdf.c __tandf.c __rem_pio2f.c __rem_pio2_large.c \
	__math_oflowf.c __math_uflowf.c __math_invalidf.c __math_divzerof.c \
	__math_xflowf.c __math_invalid.c floor.c scalbn.c fabs.c sqrt.c)

SH_PROGRAM:= meguarace
SH_SRCS:= \
	src/main.c \
	src/sat_audio.c \
	src/sat_cd.c \
	src/sat_hnm.c \
	src/sat_lua.c \
	src/sat_pad.c \
	src/sat_video.c \
	../../src/formats/cryo_c.c \
	libc/sat_libc.c \
	libc/setjmp.sx \
	$(TP)/printf/src/printf/printf.c \
	$(LUA_SRCS) \
	$(MUSL_SRCS)

# libc/include first: it fills the gaps in libyaul's C library (math.h,
# setjmp.h, locale.h, time.h) for Lua and musl.
SH_CFLAGS+= -O2 -Ilibc/include -I. -Isrc -I../../src -I$(TP)/lua -I$(TP)/printf/src \
	-I$(TP)/musl/src/internal -include sat_lua_prelude.h \
	-Wno-shadow -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
	-Wno-implicit-fallthrough -Wno-bad-function-cast -Wno-pedantic -Wno-unused-but-set-variable \
	-Wno-strict-aliasing -Wno-null-dereference -Wno-unused-function -Wno-float-conversion \
	-Wno-parentheses -Wno-missing-braces -Wno-maybe-uninitialized -Wno-array-bounds \
	-Wno-attributes -Wno-old-style-declaration -Wno-type-limits -Wno-cast-function-type \
	-std=gnu11

IP_VERSION:= V0.100
IP_RELEASE_DATE:= 20261112
IP_AREAS:= JTUBKAEL
IP_PERIPHERALS:= JAMKST
IP_TITLE:= MEGUARACE
IP_MASTER_STACK_ADDR:= 0x06100000
IP_SLAVE_STACK_ADDR:= 0x06001E00
IP_1ST_READ_ADDR:= 0x06004000
IP_1ST_READ_SIZE:= 0

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk

# The two files the disc builder needs, under fixed names.
.PHONY: disc-files
all: disc-files
disc-files: $(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso
	$(ECHO)cp $(SH_BUILD_PATH)/$(SH_PROGRAM).bin $(SH_BUILD_PATH)/A.BIN
	$(ECHO)for txt in ABS.TXT BIB.TXT CPY.TXT; do cp cd/$$txt $(SH_BUILD_PATH)/$$txt; done
