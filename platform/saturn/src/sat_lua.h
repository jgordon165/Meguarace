/* Lua on the Saturn (sat_lua.c). */
#ifndef SAT_LUA_H
#define SAT_LUA_H

#include <stddef.h>
#include <stdint.h>

/* Creates the Lua state, its heap in low work RAM plus `cart` (the RAM
 * cartridge's area, or NULL), and loads SCRIPTS.PAK. 0 on success. */
int sat_lua_init(void* cart, size_t cart_size);

int sat_lua_has_selftest(void);

typedef struct {
    int modules;          /* in SCRIPTS.PAK */
    int exit_code;        /* what the tests passed to sys.quit (-2: a Lua error) */
    uint32_t vblanks;     /* how long they took */
    size_t heap, in_use, peak, pak;
} sat_lua_report;

/* Runs the PC's Lua test suite (on a disc built with --lua-test). */
int sat_lua_selftest(sat_lua_report* r);

/* What Lua printed, line by line. */
int sat_lua_log_count(void);
const char* sat_lua_log_line(int i);
/* FNV-1a of everything Lua printed. */
uint32_t sat_lua_log_hash(void);

#endif
