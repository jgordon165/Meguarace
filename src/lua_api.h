#pragma once
struct lua_State;
struct Engine;

// Registers the global tables fs, gfx, audio, input and sys.
void registerLuaApi(lua_State* L, Engine& engine);

// HD mode: finds the packs in the HD folder setting (hd.cfg in the user
// folder) and turns the HD layer on or off to match. Returns whether packs
// were found. Does nothing (false) with --no-hd.
bool loadHd(Engine& engine);
