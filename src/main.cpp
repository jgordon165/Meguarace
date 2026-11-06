// MegaRace port: C++ host. Owns the window, audio device and input, and runs
// the game logic written in Lua (scripts/). Original assets are loaded at
// runtime from the player's own copy of the game; nothing from the original
// is compiled in or shipped.
//
// Usage: megarace --data <game folder> [--music <ripped tracks folder>]
//                 [--scale N] [--scripts <dir>] [--hd <packs folder> | --no-hd]
//                 [--display native|800x600|1024x768|1920x1080]
//                 [--frames N --screenshot out.bmp]   (headless testing)

#include "engine.h"
#include "lua_api.h"
#ifdef MEGARACE_HD
#include "hd/hdpack.h"
#endif

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

#ifdef MEGARACE_HD
// The HD packs folder and their picture size: --hd, else the packs
// megarace-hd made in the user folder. All packs share one size (the first
// pack found decides; packs of another size are skipped when opened).
static bool findHd(std::string& dir, int& picW, int& picH) {
    const bool asked = !dir.empty();
    if (dir.empty()) {
        char* p = SDL_GetPrefPath("Meguarace", "Meguarace");
        if (p) dir = (fs::path(p) / "hd").string();
        SDL_free(p);
    }
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) {
        if (asked) std::fprintf(stderr, "--hd: no such folder %s\n", dir.c_str());
        dir.clear();
        return false;
    }
    bool old = false;
    for (const auto& f : fs::directory_iterator(dir, ec)) {
        if (f.path().extension() != ".hdv") continue;
        hd::PackHeader h;
        if (hd::Pack::peek(f.path().string(), h)) { picW = h.pictureW; picH = h.pictureH; return true; }
        old = true;
    }
    if (old) std::fprintf(stderr, "HD packs in %s are from an older megarace-hd: run it again\n", dir.c_str());
    else if (asked) std::fprintf(stderr, "--hd: no HD packs in %s (make them with megarace-hd)\n", dir.c_str());
    dir.clear();
    return false;
}
#endif

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
    int scale = 3;      // native mode's window size
    long maxFrames = -1;
    bool noHd = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if      (a == "--data")       eng.dataArg = next();
        else if (a == "--music")      eng.musicArg = next();
        else if (a == "--scripts")    scripts = next();
        else if (a == "--scale")      scale = std::max(1, std::atoi(next().c_str()));
        else if (a == "--frames")     maxFrames = std::atol(next().c_str());
        else if (a == "--screenshot") shot = next();
        else if (a == "--hd")         eng.hdDir = next();
        else if (a == "--no-hd")      noHd = true;
        else if (a == "--display")    eng.displayArg = next();
        else if (a == "--help" || a == "-h") {
            std::printf("megarace --data <your MegaRace CD or install folder> "
                        "[--music <folder with track02.wav...>] [--scale N]"
#ifdef MEGARACE_HD
                        " [--hd <HD packs folder> | --no-hd]"
                        " [--display native|800x600|1024x768|1920x1080]"
#endif
                        "\n");
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
    // Audio is optional: without a working sound device the game runs silent
    // (videos then run on the timer instead of the sound clock).
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        std::fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    int picW = 0, picH = 0;
#ifdef MEGARACE_HD
    if (noHd || !findHd(eng.hdDir, picW, picH)) eng.hdDir.clear();
#else
    (void)noHd;
#endif
    if (!eng.video.init(320, 200, scale, headless)) return 1;
    if (!eng.hdDir.empty()) {
        if (eng.video.enableHd(picW, picH))
            std::printf("HD videos (%dx%d) from %s (F10 turns them off and on)\n", picW, picH, eng.hdDir.c_str());
        else
            eng.hdDir.clear();
    }
    // Display mode: --display, else the largest that fits the desktop. The
    // game's saved setting, if any, is applied by the Lua side at start
    // (unless --display was given). Without HD packs: native.
    {
        std::string want = eng.displayArg.empty() ? eng.video.bestMode() : eng.displayArg;
        if (!eng.video.setMode(want)) {
            if (!eng.displayArg.empty())
                std::fprintf(stderr, "--display %s: %s\n", want.c_str(),
                             eng.video.hdAvailable() ? "unknown mode" : "needs HD packs (megarace-hd)");
            eng.video.setMode("native");
        }
    }
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
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F10 && !ev.key.repeat &&
                eng.video.hdAvailable() && eng.video.mode().hd) {
                eng.video.setHdOn(!eng.video.hdOn());
                std::printf("HD %s\n", eng.video.hdOn() ? "on" : "off");
            }
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

    if (!shot.empty() && !eng.video.saveBmp(shot, true))
        std::fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());

    callLua(L, "shutdown", 0, false);
    lua_close(L);
    eng.audio.shutdown();
    eng.video.shutdown();
    SDL_Quit();
    return eng.exitCode;
}
