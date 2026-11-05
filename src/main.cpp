// MegaRace port: C++ host. Owns the window, audio device and input, and runs
// the game logic written in Lua (scripts/). Original assets are loaded at
// runtime from the player's own copy of the game; nothing from the original
// is compiled in or shipped.
//
// Usage: megarace --data <game folder> [--music <ripped tracks folder>]
//                 [--scale N] [--scripts <dir>]
//                 [--frames N --screenshot out.bmp]   (headless testing)

#include "engine.h"
#include "lua_api.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static bool callLua(lua_State* L, const char* fn, double arg, bool withArg) {
    lua_getglobal(L, "game");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return true; }
    lua_getfield(L, -1, fn);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return true; }
    if (withArg) lua_pushnumber(L, arg);
    if (lua_pcall(L, withArg ? 1 : 0, 0, 0) != LUA_OK) {
        std::fprintf(stderr, "lua error in game.%s: %s\n", fn, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    Engine eng;
    std::string scripts;
    std::string shot;
    int scale = 3;
    long maxFrames = -1;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if      (a == "--data")       eng.dataArg = next();
        else if (a == "--music")      eng.musicArg = next();
        else if (a == "--scripts")    scripts = next();
        else if (a == "--scale")      scale = std::max(1, std::atoi(next().c_str()));
        else if (a == "--frames")     maxFrames = std::atol(next().c_str());
        else if (a == "--screenshot") shot = next();
        else if (a == "--help" || a == "-h") {
            std::printf("megarace --data <your MegaRace CD or install folder> "
                        "[--music <folder with track02.wav...>] [--scale N]\n");
            return 0;
        }
    }

    if (scripts.empty()) {
        char* base = SDL_GetBasePath();
        scripts = (base ? std::string(base) : std::string("./")) + "scripts";
        SDL_free(base);
    }
    if (eng.dataArg.empty()) eng.dataArg = "data";
    if (eng.musicArg.empty()) eng.musicArg = (fs::path(eng.dataArg) / "music").string();
    eng.data.mount(eng.dataArg);
    eng.music.mount(eng.musicArg);

    const bool headless = maxFrames >= 0;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!eng.video.init(320, 200, scale, headless)) return 1;
    eng.audio.init();   // non-fatal: the game runs silent without a device

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    registerLuaApi(L, eng);

    // require() finds modules in the scripts folder.
    lua_getglobal(L, "package");
    lua_pushstring(L, (scripts + "/?.lua;" + scripts + "/?/init.lua").c_str());
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);

    std::string mainLua = scripts + "/main.lua";
    if (luaL_dofile(L, mainLua.c_str()) != LUA_OK) {
        std::fprintf(stderr, "failed to load %s: %s\n", mainLua.c_str(), lua_tostring(L, -1));
        return 1;
    }
    if (!callLua(L, "init", 0, false)) return 1;
    if (eng.quit) {                     // init may finish everything (test runs)
        lua_close(L);
        eng.audio.shutdown();
        eng.video.shutdown();
        SDL_Quit();
        return eng.exitCode;
    }

    Uint64 last = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    long frame = 0;

    while (!eng.quit) {
        eng.input.beginFrame();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) eng.quit = true;
            eng.input.handle(ev);
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = headless ? 1.0 / 70.0 : (now - last) / freq;
        last = now;
        if (dt > 0.1) dt = 0.1;   // don't explode after a stall

        if (!callLua(L, "update", dt, true)) break;
        if (!callLua(L, "draw", 0, false)) break;
        eng.video.present();

        if (headless && ++frame >= maxFrames) break;
    }

    if (!shot.empty() && !eng.video.saveBmp(shot))
        std::fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());

    callLua(L, "shutdown", 0, false);
    lua_close(L);
    eng.audio.shutdown();
    eng.video.shutdown();
    SDL_Quit();
    return eng.exitCode;
}
