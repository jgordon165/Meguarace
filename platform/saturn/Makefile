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

SH_PROGRAM:= meguarace
SH_SRCS:= \
	src/main.c \
	src/sat_audio.c \
	src/sat_cd.c \
	src/sat_hnm.c \
	src/sat_pad.c \
	src/sat_video.c \
	../../src/formats/cryo_c.c

SH_CFLAGS+= -O2 -I. -Isrc -I../../src -Wno-shadow

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
