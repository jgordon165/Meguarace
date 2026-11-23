// MegaRace port: C++ host. Owns the window, audio device and input, and runs
// the game logic written in Lua (scripts/). Original assets are loaded at
// runtime from the player's own copy of the game; nothing from the original
// is compiled in or shipped.
//
// Usage: megarace --data <game folder> [--music <ripped tracks folder>]
//                 [--scale N] [--scripts <dir>] [--no-hd]
//                 [--display native|800x600|1024x768|1920x1080]
//                 [--frames N --screenshot out.bmp]   (headless testing)

#include "engine.h"
#include "lua_api.h"
#include "paths.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <SDL.h>
#ifdef MEGARACE_XENON_LAYOUT
extern "C" void xp_exit(int status);   // platform/xenon: ends the title (back to the dashboard)
#endif
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include "fsx.h"
#include <string>


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
        else if (a == "--no-hd")      noHd = true;
        else if (a == "--hd")         eng.hdArg = next();
        else if (a == "--display")    eng.displayArg = next();
        else if (a == "--classic")    eng.classic = true;
        else if (a == "--help" || a == "-h") {
            std::printf("megarace --data <your MegaRace CD or install folder> "
                        "[--music <folder with track02.wav...>] [--scale N]"
                        " [--hd <folder>] [--no-hd]"
                        " [--display native|800x600|1024x768|1920x1080]"
                        " [--classic]"
                        "\n"
                        "  --classic  play as the original for this session: 320x200, 4:3,\n"
                        "             the FM music and sounds, no smoothing (settings untouched)\n"
                        "  --hd       the HD packs' folder for this session, instead of\n"
                        "             Options > ADVANCED > HD FOLDER (not saved)\n");
            return 0;
        }
    }

    if (scripts.empty()) scripts = paths::base() + "scripts";
#ifdef MEGARACE_WIIU_LAYOUT
    // The Wii U starts with no arguments: the CD's files in data/ and the HD
    // packs in hd/, in the port's folder on the SD card (paths.h).
    if (eng.dataArg.empty()) eng.dataArg = paths::root() + "data";
    if (eng.hdArg.empty() && fsx::isDir(paths::root() + "hd")) eng.hdArg = paths::root() + "hd";
    std::printf("data: %s\n", eng.dataArg.c_str());
#endif
#ifdef MEGARACE_XENON_LAYOUT
    // The Xbox 360 starts with no arguments: everything is in the port's
    // folder on the USB stick (README, "Xbox 360"): data/ the CD's files,
    // hd/ the HD packs if any.
    {
        char* base = SDL_GetBasePath();
        const std::string root = base ? base : "./";
        SDL_free(base);
        if (eng.dataArg.empty()) eng.dataArg = fsx::join(root, "data");
        if (eng.hdArg.empty() && fsx::isDir(fsx::join(root, "hd"))) eng.hdArg = fsx::join(root, "hd");
        std::printf("data: %s\n", eng.dataArg.c_str());
    }
#endif
    if (eng.dataArg.empty()) eng.dataArg = "data";
    // --classic: the original's 320x200 (unless --display says otherwise).
    if (eng.classic && eng.displayArg.empty()) eng.displayArg = "native";
    if (eng.musicArg.empty()) eng.musicArg = fsx::join(eng.dataArg, "music");
    eng.data.mount(eng.dataArg);
    eng.music.mount(eng.musicArg);
    {
        // The SoundFont music style's .sf2: next to the game (the bundled
        // GeneralUser GS), the user folder's soundfont/, or the --music folder.
        const std::string pref = paths::pref();
        eng.audio.setSoundFontDirs({
            fsx::join(paths::base(), "soundfont"),
            pref.empty() ? std::string() : fsx::join(pref, "soundfont"),
            eng.musicArg});
    }

    const bool headless = maxFrames >= 0;
    // Audio is optional: without a working sound device the game runs silent
    // (videos then run on the timer instead of the sound clock).
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        std::fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
#ifndef MEGARACE_XENON_LAYOUT
    // Controllers (Input maps them to keys). Optional: a build without
    // joystick support just has the keyboard. (The Xbox 360's pads are keys
    // already, from its own video driver.)
    if (!headless && SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        std::fprintf(stderr, "controllers unavailable: %s\n", SDL_GetError());
#endif
    eng.hdOff = noHd;
    if (!eng.video.init(320, 200, scale, headless)) return 1;
    // SDL starts with text input on; the game only wants it while a text
    // line is being edited (input.text_start).
    SDL_StopTextInput();
    // HD packs from --hd, else the folder set in Options > HD FOLDER (hd.cfg).
    if (loadHd(eng))
        std::printf("HD videos (%dx%d) from %s (F10 turns them off and on)\n", eng.video.hdPictureW(),
                    eng.video.hdPictureH(), eng.hdDir.c_str());
    // Display mode: --display, else the largest that fits the desktop. The
    // game's saved setting, if any, is applied by the Lua side at start
    // (unless --display was given). Without HD packs: native.
    {
        std::string want = eng.displayArg.empty() ? eng.video.bestMode() : eng.displayArg;
        if (!eng.video.setMode(want)) {
            if (!eng.displayArg.empty())
                std::fprintf(stderr, "--display %s: %s\n", want.c_str(),
                             eng.video.hdAvailable() ? "unknown mode" : "needs HD packs (hd-conversion-utility)");
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
            eng.input.handle(ev);
        }
        // F10 (Y on a controller): HD pictures on/off.
        if (eng.input.pressed("F10") && eng.video.hdAvailable() && eng.video.mode().hd) {
            eng.video.setHdOn(!eng.video.hdOn());
            std::printf("HD %s\n", eng.video.hdOn() ? "on" : "off");
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
#ifdef MEGARACE_XENON_LAYOUT
    // The 360: a black frame, then the title ends with the sound stopped and
    // given back, not torn down under the presenter and GPU (xp_exit).
    eng.audio.setPaused(true);
    eng.video.presentBlack();
    xp_exit(eng.exitCode);
#endif
    lua_close(L);
    eng.audio.shutdown();
    eng.video.shutdown();
    SDL_Quit();
    return eng.exitCode;
}
