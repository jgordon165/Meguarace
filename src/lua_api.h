#pragma once
struct lua_State;
struct Engine;

// Registers the global tables fs, gfx, audio, input and sys.
void registerLuaApi(lua_State* L, Engine& engine);
