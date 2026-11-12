/*
 * Lua on the Saturn: Lua 5.4 built 32-bit (32-bit integers, single-precision
 * floats: the SH-2 has no FPU), with its heap in low work RAM and, when one
 * is plugged in, the RAM cartridge.
 *
 * The game's scripts come from SCRIPTS.PAK on the disc (the disc builder
 * packs scripts/): `require` finds modules there. The engine API isn't on
 * the Saturn yet (docs/saturn.md, milestone 3), so gfx, audio, sys, ... are
 * stand-ins for now: every function exists and does nothing.
 *
 * A disc built with --lua-test also carries the PC's Lua test suite
 * (tests/lua/main.lua); sat_lua_selftest runs it and keeps what it prints.
 */
#include <yaul.h>

#include <string.h>

#include <mm/tlsf.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include "sat_cd.h"
#include "sat_lua.h"
#include "sat_mem.h"
#include "sat_video.h"

/* ---- heap ---- */

static tlsf_t _tlsf;
static size_t _heap_size, _in_use, _peak;

static void* _alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    (void)ud;
    if (nsize == 0) {
        if (ptr) { _in_use -= osize; tlsf_free(_tlsf, ptr); }
        return NULL;
    }
    void* p = tlsf_realloc(_tlsf, ptr, nsize);
    if (p) {
        _in_use += nsize - (ptr ? osize : 0);
        if (_in_use > _peak) _peak = _in_use;
    }
    return p;
}

/* ---- output: kept as lines, for the screen ---- */

#define LOG_LINES 64
#define LOG_COLS  40
static char _log[LOG_LINES][LOG_COLS + 1];
static int _log_n, _col;

/* FNV-1a over every byte Lua printed: the emulator test compares it with
 * the same suite's output on the PC (tests/saturn/lua_stub_runner.c). */
static uint32_t _hash = 2166136261u;
static void _hash_bytes(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) { _hash ^= (uint8_t)s[i]; _hash *= 16777619u; }
}
uint32_t sat_lua_log_hash(void) { return _hash; }

static void _put(char c) {
    if (_log_n == 0) _log_n = 1;
    char* line = _log[(_log_n - 1) % LOG_LINES];
    if (c == '\n' || _col == LOG_COLS) {
        _log_n++;
        _col = 0;
        _log[(_log_n - 1) % LOG_LINES][0] = 0;
        if (c == '\n') return;
        line = _log[(_log_n - 1) % LOG_LINES];
    }
    line[_col++] = c;
    line[_col] = 0;
}

static void _out(const char* s, size_t n) { for (size_t i = 0; i < n; i++) _put(s[i]); }

void sat_lua_write(const char* s, size_t n) { _hash_bytes(s, n); _out(s, n); }

void sat_lua_writeerr(const char* fmt, const char* p) {
    for (; *fmt; fmt++) {
        if (fmt[0] == '%' && fmt[1] == 's') { sat_lua_write(p, strlen(p)); fmt++; }
        else sat_lua_write(fmt, 1);
    }
}

int sat_lua_log_count(void) { return _log_n; }
const char* sat_lua_log_line(int i) {
    if (i < 0 || i >= _log_n || i < _log_n - LOG_LINES) return "";
    return _log[i % LOG_LINES];
}

/* ---- SCRIPTS.PAK ---- */

static uint8_t* _pak;
static uint32_t _pak_size, _pak_count;

static uint32_t _be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static const uint8_t* _pak_find(const char* name, uint32_t* size) {
    for (uint32_t i = 0; i < _pak_count; i++) {
        const uint8_t* e = _pak + 8 + i * 64;
        if (strncmp((const char*)e, name, 56) == 0) {
            const uint32_t off = _be32(e + 56), n = _be32(e + 60);
            if (off > _pak_size || n > _pak_size - off) return NULL;
            *size = n;
            return _pak + off;
        }
    }
    return NULL;
}

static int _searcher(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    uint32_t n;
    const uint8_t* src = _pak_find(name, &n);
    if (!src) {
        lua_pushfstring(L, "no module '%s' in SCRIPTS.PAK", name);
        return 1;
    }
    /* Named as on the PC (scripts/race/rules.lua), so messages match. */
    char chunk[80];
    snprintf(chunk, sizeof chunk, "@scripts/%s.lua", name);
    for (char* c = chunk + 9; *c && c[1]; c++) if (*c == '.' && strcmp(c, ".lua")) *c = '/';
    if (luaL_loadbuffer(L, (const char*)src, n, chunk) != LUA_OK) return lua_error(L);
    lua_pushstring(L, name);
    return 2;
}

/* ---- stand-ins for the engine API ---- */

static int _quit_code = -1;

static int _nothing(lua_State* L) { (void)L; return 0; }

static int _stub_index(lua_State* L) {
    lua_pushcfunction(L, _nothing);
    return 1;
}

static int _sys_quit(lua_State* L) { _quit_code = (int)luaL_optinteger(L, 1, 0); return 0; }
static int _sys_log(lua_State* L) {
    size_t n;
    const char* s = luaL_tolstring(L, 1, &n);
    sat_lua_write(s, n);
    sat_lua_write("\n", 1);
    return 0;
}
static int _sys_ticks(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)(sat_vblanks() * 1000u / (uint32_t)sat_vblank_rate()));
    return 1;
}
static int _os_time(lua_State* L) { lua_pushinteger(L, (lua_Integer)(sat_vblanks() / (uint32_t)sat_vblank_rate())); return 1; }
static int _os_clock(lua_State* L) { lua_pushnumber(L, (lua_Number)sat_vblanks() / (lua_Number)sat_vblank_rate()); return 1; }
static int _os_getenv(lua_State* L) { lua_pushnil(L); return 1; }

static void _stub_table(lua_State* L, const char* name) {
    lua_newtable(L);
    lua_newtable(L);
    lua_pushcfunction(L, _stub_index);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_setglobal(L, name);
}

static lua_State* _L;

int sat_lua_init(void* cart, size_t cart_size) {
    _tlsf = tlsf_pool_create((void*)SAT_LWRAM_FREE, SAT_LWRAM_END - SAT_LWRAM_FREE);
    _heap_size = SAT_LWRAM_END - SAT_LWRAM_FREE;
    if (cart && cart_size) {
        tlsf_pool_add(_tlsf, cart, cart_size);
        _heap_size += cart_size;
    }

    uint32_t fad, size;
    if (sat_cd_find("SCRIPTS.PAK", &fad, &size) != 0) return -1;
    _pak = tlsf_malloc(_tlsf, size + 1);
    if (!_pak || sat_cd_read(fad, 0, _pak, size) != 0) return -2;
    if (memcmp(_pak, "MGPK", 4) != 0) return -3;
    _pak_size = size;
    _pak_count = _be32(_pak + 4);
    _in_use = _peak = 0;

    lua_State* L = lua_newstate(_alloc, NULL);
    if (!L) return -4;
    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base},       {LUA_LOADLIBNAME, luaopen_package},
        {LUA_COLIBNAME, luaopen_coroutine}, {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8}, {LUA_DBLIBNAME, luaopen_debug},
        {NULL, NULL}};
    for (const luaL_Reg* l = libs; l->func; l++) {
        luaL_requiref(L, l->name, l->func, 1);
        lua_pop(L, 1);
    }
    /* require: SCRIPTS.PAK first. */
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "searchers");
    for (lua_Integer i = (lua_Integer)luaL_len(L, -1); i >= 2; i--) {
        lua_rawgeti(L, -1, i);
        lua_rawseti(L, -2, i + 1);
    }
    lua_pushcfunction(L, _searcher);
    lua_rawseti(L, -2, 2);
    lua_pop(L, 2);

    /* A small os table, and the engine stand-ins. */
    lua_newtable(L);
    lua_pushcfunction(L, _os_time);   lua_setfield(L, -2, "time");
    lua_pushcfunction(L, _os_clock);  lua_setfield(L, -2, "clock");
    lua_pushcfunction(L, _os_getenv); lua_setfield(L, -2, "getenv");
    lua_setglobal(L, "os");
    static const char* const api[] = {"gfx", "audio", "dat", "fs", "spr", "hnm", "input", "sys", NULL};
    for (int i = 0; api[i]; i++) _stub_table(L, api[i]);
    lua_getglobal(L, "sys");
    lua_pushcfunction(L, _sys_quit);  lua_setfield(L, -2, "quit");
    lua_pushcfunction(L, _sys_log);   lua_setfield(L, -2, "log");
    lua_pushcfunction(L, _sys_ticks); lua_setfield(L, -2, "ticks");
    lua_pop(L, 1);
    _L = L;
    return 0;
}

int sat_lua_has_selftest(void) {
    uint32_t n;
    return _pak && _pak_find("@test", &n) != NULL;
}

int sat_lua_selftest(sat_lua_report* r) {
    memset(r, 0, sizeof *r);
    r->modules = (int)_pak_count;
    r->heap = _heap_size;
    if (!_L) return -1;
    lua_State* L = _L;
    uint32_t n;
    const uint8_t* src = _pak_find("@test", &n);
    if (!src) return -1;
    const uint32_t t0 = sat_vblanks();
    int rc = luaL_loadbuffer(L, (const char*)src, n, "@tests/lua/main.lua");
    if (rc == LUA_OK) rc = lua_pcall(L, 0, 0, 0);
    if (rc == LUA_OK) {
        lua_getglobal(L, "game");
        lua_getfield(L, -1, "init");
        rc = lua_pcall(L, 0, 0, 0);
        if (rc == LUA_OK) lua_pop(L, 1);
    }
    if (rc != LUA_OK) {
        size_t len;
        const char* msg = lua_tolstring(L, -1, &len);
        sat_lua_write("lua error: ", 11);
        sat_lua_write(msg ? msg : "?", msg ? len : 1);
        sat_lua_write("\n", 1);
        lua_settop(L, 0);
    }
    r->vblanks = sat_vblanks() - t0;
    lua_gc(L, LUA_GCCOLLECT);
    r->in_use = _in_use;
    r->peak = _peak;
    r->pak = _pak_size;
    r->exit_code = rc != LUA_OK ? -2 : _quit_code;
    return 0;
}
