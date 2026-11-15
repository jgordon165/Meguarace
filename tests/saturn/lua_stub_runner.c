/* The Saturn's Lua set-up (platform/saturn/src/sat_lua.c) on the PC: the
 * same libraries, the same stand-ins for the engine API, modules from
 * scripts/. The emulator test runs the Lua test suite here and on the
 * Saturn and compares what each printed (tests/saturn/run_emulator_test.sh).
 * Built with the Saturn's copy of Lua (32-bit numbers).
 *
 *   lua_stub_runner <scripts dir> <test file>      prints what Lua printed,
 *                                                  then "quit <code>, mem <KiB> KiB" */
#include <stdio.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
static int quit_code = -1;
static int nothing(lua_State* L) { (void)L; return 0; }
static int stub_index(lua_State* L) { lua_pushcfunction(L, nothing); return 1; }
static int sys_quit(lua_State* L) { quit_code = (int)luaL_optinteger(L, 1, 0); return 0; }
static int sys_log(lua_State* L) { puts(luaL_tolstring(L, 1, NULL)); return 0; }
static int os_getenv(lua_State* L) { lua_pushnil(L); return 1; }
static int os_zero(lua_State* L) { lua_pushinteger(L, 0); return 1; }
int main(int argc, char** argv) {
    lua_State* L = luaL_newstate();
    static const luaL_Reg libs[] = {{"_G", luaopen_base}, {"package", luaopen_package}, {"coroutine", luaopen_coroutine},
        {"table", luaopen_table}, {"string", luaopen_string}, {"math", luaopen_math}, {"utf8", luaopen_utf8}, {"debug", luaopen_debug}, {0, 0}};
    for (const luaL_Reg* l = libs; l->func; l++) { luaL_requiref(L, l->name, l->func, 1); lua_pop(L, 1); }
    lua_getglobal(L, "package"); lua_pushfstring(L, "%s/?.lua", argv[1]); lua_setfield(L, -2, "path"); lua_pop(L, 1);
    lua_newtable(L);
    lua_pushcfunction(L, os_zero); lua_setfield(L, -2, "time");
    lua_pushcfunction(L, os_zero); lua_setfield(L, -2, "clock");
    lua_pushcfunction(L, os_getenv); lua_setfield(L, -2, "getenv");
    lua_setglobal(L, "os");
    const char* api[] = {"gfx", "audio", "dat", "fs", "spr", "hnm", "input", "sys", 0};
    for (int i = 0; api[i]; i++) { lua_newtable(L); lua_newtable(L); lua_pushcfunction(L, stub_index); lua_setfield(L, -2, "__index"); lua_setmetatable(L, -2); lua_setglobal(L, api[i]); }
    lua_getglobal(L, "sys");
    lua_pushcfunction(L, sys_quit); lua_setfield(L, -2, "quit");
    lua_pushcfunction(L, sys_log); lua_setfield(L, -2, "log");
    lua_pop(L, 1);
    int rc = luaL_loadfile(L, argv[2]);
    if (!rc) rc = lua_pcall(L, 0, 0, 0);
    if (!rc) { lua_getglobal(L, "game"); lua_getfield(L, -1, "init"); rc = lua_pcall(L, 0, 0, 0); }
    if (rc) printf("lua error: %s\n", lua_tostring(L, -1));
    printf("quit %d, mem %d KiB\n", quit_code, lua_gc(L, LUA_GCCOUNT, 0));
    return 0;
}
