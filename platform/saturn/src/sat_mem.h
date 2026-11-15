/*
 * Where Meguarace keeps things in the Saturn's RAM.
 *
 * High work RAM (1 MiB, 0x06000000, the fastest): the program, the
 * screen and the decode buffers (they're in its BSS).
 * Low work RAM (1 MiB, 0x00200000): the CD buffers, below; the rest of it
 * (and the 4 MiB RAM cartridge, when there is one) is for the Lua game.
 * Both are cached: the SH-2 cache is write-through and the CD data is
 * moved by the CPU, so nothing goes stale.
 */
#ifndef SAT_MEM_H
#define SAT_MEM_H

#include <stdint.h>

#define SAT_LWRAM_BASE       0x00200000UL

#define SAT_STREAM_BUF       ((uint8_t*)(SAT_LWRAM_BASE + 0x00000))
#define SAT_STREAM_BUF_SIZE  (96u * 1024u)
#define SAT_BOUNCE_BUF       ((uint8_t*)(SAT_LWRAM_BASE + 0x18000))
#define SAT_BOUNCE_BUF_SIZE  (32u * 1024u)
/* Free for the game from here to the end of low work RAM. */
#define SAT_LWRAM_FREE       (SAT_LWRAM_BASE + 0x20000)
#define SAT_LWRAM_END        (SAT_LWRAM_BASE + 0x100000)

#endif
