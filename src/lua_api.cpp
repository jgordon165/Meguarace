#include "lua_api.h"
#include "engine.h"
#include "hd/hdconfig.h"
#include "hd/hdpack.h"
#include "hd/sprpack.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

static Engine& E(lua_State* L) {
    return *static_cast<Engine*>(lua_touserdata(L, lua_upvalueindex(1)));
}

static void pushBytes(lua_State* L, const std::vector<uint8_t>& v) {
    lua_pushlstring(L, reinterpret_cast<const char*>(v.data()), v.size());
}

// ---------------------------------------------------------------- fs ------
// All paths are relative to the player's game folder, case-insensitive.

static int fs_mounted(lua_State* L) { lua_pushboolean(L, E(L).data.mounted()); return 1; }
static int fs_root(lua_State* L) { lua_pushstring(L, E(L).data.root().string().c_str()); return 1; }
static int fs_exists(lua_State* L) { lua_pushboolean(L, E(L).data.exists(luaL_checkstring(L, 1))); return 1; }

static int fs_size(lua_State* L) {
    auto s = E(L).data.size(luaL_checkstring(L, 1));
    if (s) lua_pushinteger(L, static_cast<lua_Integer>(*s)); else lua_pushnil(L);
    return 1;
}

static int fs_read(lua_State* L) {
    auto d = E(L).data.read(luaL_checkstring(L, 1));
    if (!d) { lua_pushnil(L); lua_pushstring(L, "file not found"); return 2; }
    pushBytes(L, *d);
    return 1;
}

static int fs_read_range(lua_State* L) {
    auto d = E(L).data.readRange(luaL_checkstring(L, 1),
                                 static_cast<uint64_t>(luaL_checkinteger(L, 2)),
                                 static_cast<uint64_t>(luaL_checkinteger(L, 3)));
    if (!d) { lua_pushnil(L); lua_pushstring(L, "file not found"); return 2; }
    pushBytes(L, *d);
    return 1;
}

static int fs_list(lua_State* L) {
    auto entries = E(L).data.list(luaL_optstring(L, 1, ""));
    lua_createtable(L, static_cast<int>(entries.size()), 0);
    int i = 1;
    for (const auto& e : entries) {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, e.name.c_str());               lua_setfield(L, -2, "name");
        lua_pushboolean(L, e.isDir);                     lua_setfield(L, -2, "dir");
        lua_pushinteger(L, static_cast<lua_Integer>(e.size)); lua_setfield(L, -2, "size");
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// --------------------------------------------------------------- gfx ------

static uint8_t idx(lua_State* L, int n) { return static_cast<uint8_t>(luaL_checkinteger(L, n) & 255); }
static int ci(lua_State* L, int n) { return static_cast<int>(luaL_checkinteger(L, n)); }

static int gfx_width(lua_State* L)  { lua_pushinteger(L, E(L).video.width());  return 1; }
static int gfx_height(lua_State* L) { lua_pushinteger(L, E(L).video.height()); return 1; }
static int gfx_clear(lua_State* L)  { E(L).video.clear(static_cast<uint8_t>(luaL_optinteger(L, 1, 0))); return 0; }
static int gfx_pset(lua_State* L)   { E(L).video.pset(ci(L, 1), ci(L, 2), idx(L, 3)); return 0; }
static int gfx_pget(lua_State* L)   { lua_pushinteger(L, E(L).video.pget(ci(L, 1), ci(L, 2))); return 1; }
static int gfx_fill(lua_State* L)   { E(L).video.fill(ci(L, 1), ci(L, 2), ci(L, 3), ci(L, 4), idx(L, 5)); return 0; }
static int gfx_line(lua_State* L)   { E(L).video.line(ci(L, 1), ci(L, 2), ci(L, 3), ci(L, 4), idx(L, 5)); return 0; }
static int gfx_text(lua_State* L)   { E(L).video.text(ci(L, 1), ci(L, 2), luaL_checkstring(L, 3), static_cast<uint8_t>(luaL_optinteger(L, 4, 15))); return 0; }

// gfx.blit(x, y, w, h, pixels [, transparentIndex])
static int gfx_blit(lua_State* L) {
    size_t len = 0;
    const char* px = luaL_checklstring(L, 5, &len);
    int key = static_cast<int>(luaL_optinteger(L, 6, -1));
    E(L).video.blit(ci(L, 1), ci(L, 2), ci(L, 3), ci(L, 4),
                    reinterpret_cast<const uint8_t*>(px), len, key);
    return 0;
}

// gfx.palette(rgbBytes [, first=0 [, sixBit=true]])
static int gfx_palette(lua_State* L) {
    size_t len = 0;
    const char* p = luaL_checklstring(L, 1, &len);
    int first = static_cast<int>(luaL_optinteger(L, 2, 0));
    bool six = lua_isnoneornil(L, 3) ? true : lua_toboolean(L, 3);
    E(L).video.setPalette(reinterpret_cast<const uint8_t*>(p), len, first, six);
    return 0;
}

static int gfx_setcolor(lua_State* L) {
    E(L).video.setColor(ci(L, 1), idx(L, 2), idx(L, 3), idx(L, 4));
    return 0;
}

// ---- display modes ----
// The game draws in 320x200 (gfx.width(), gfx.height()) in every mode; the
// mode only changes how that is shown. See Video::Mode.
//   gfx.displays() -> { "native", "800x600", ... } the modes available now
//   gfx.display()  -> name, picture width, height, hd (HD pictures showing)
//   gfx.set_display(name) -> true, or false if not available
//   gfx.to_display(x, y) -> the point's position in the display picture, in
//                           its pixels (for anything drawn at display resolution)
static int gfx_displays(lua_State* L) {
    lua_newtable(L);
    int n = 0;
    for (const auto& m : Video::modes())
        if (E(L).video.modeAvailable(m)) { lua_pushstring(L, m.name); lua_rawseti(L, -2, ++n); }
    return 1;
}
static int gfx_display(lua_State* L) {
    const Video& v = E(L).video;
    lua_pushstring(L, v.mode().name);
    lua_pushinteger(L, v.pictureW());
    lua_pushinteger(L, v.pictureH());
    lua_pushboolean(L, E(L).video.hdOn());
    return 4;
}
static int gfx_set_display(lua_State* L) {
    lua_pushboolean(L, E(L).video.setMode(luaL_checkstring(L, 1)));
    return 1;
}
static int gfx_to_display(lua_State* L) {
    const Video& v = E(L).video;
    double x = luaL_checknumber(L, 1), y = luaL_checknumber(L, 2);
    lua_pushnumber(L, (x + v.wing()) * v.pictureW() / v.canvasWidth());
    lua_pushnumber(L, y * v.pictureH() / v.height());
    return 2;
}

// ---- 16:9 ----
// gfx.aspect([a]) -> "4:3" or "16:9", after setting it if given. 16:9 is for
// the HD resolutions: the canvas gets a wing on each side of the game's
// 320x200 screen (gfx.wing() pixels, 53). The game keeps drawing in its own
// coordinates; x = -gfx.wing() is the left edge, 320 + gfx.wing() the right.
// gfx.ambient([on]) -> the wings' fill where nothing is drawn: a blurred,
// darkened stretch of the picture (on), or black.
static int gfx_aspect(lua_State* L) {
    Video& v = E(L).video;
    if (!lua_isnoneornil(L, 1)) {
        std::string a = luaL_checkstring(L, 1);
        if (a != "4:3" && a != "16:9") return luaL_argerror(L, 1, "\"4:3\" or \"16:9\"");
        v.setWide(a == "16:9");
    }
    lua_pushstring(L, v.wide() ? "16:9" : "4:3");
    return 1;
}
static int gfx_wing(lua_State* L) { lua_pushinteger(L, E(L).video.wing()); return 1; }
static int gfx_ambient(lua_State* L) {
    Video& v = E(L).video;
    if (!lua_isnoneornil(L, 1)) v.setAmbient(lua_toboolean(L, 1) != 0);
    lua_pushboolean(L, v.ambient());
    return 1;
}

// gfx.hd_pictures([on]) -> whether the HD pictures (packs) are shown, after
// setting it if `on` is given. Off: the original videos and menus, line
// doubled and interpolated (the "line doubled" HD style; F10 switches it too).
static int gfx_hd_pictures(lua_State* L) {
    Video& v = E(L).video;
    if (!lua_isnoneornil(L, 1)) v.setHdOn(lua_toboolean(L, 1) != 0);
    lua_pushboolean(L, v.hdPicturesOn());
    return 1;
}

// gfx.smoothing([level]) -> the video smoothing (0 off, 1 subtle, 2 more, 3 max),
// after setting it if a level is given. HD resolutions only; GPU only.
static int gfx_smoothing(lua_State* L) {
    Video& v = E(L).video;
    if (!lua_isnoneornil(L, 1)) v.setSmoothing(int(luaL_checkinteger(L, 1)));
    lua_pushinteger(L, v.smoothing());
    return 1;
}

// gfx.video_repeat([n]) -> how many times the video layer repeats each
// original pixel before interpolating: 0 as many as fit (line doubled) or 3
// (line tripled), after setting it if `n` is given.
static int gfx_video_repeat(lua_State* L) {
    Video& v = E(L).video;
    if (!lua_isnoneornil(L, 1)) v.setVideoRepeat(int(luaL_checkinteger(L, 1)));
    lua_pushinteger(L, v.videoRepeat());
    return 1;
}

// gfx.nearest(r, g, b) -> the palette index closest to that colour (1..255).
static int gfx_nearest(lua_State* L) {
    lua_pushinteger(L, E(L).video.nearest(int(luaL_checkinteger(L, 1)), int(luaL_checkinteger(L, 2)),
                                          int(luaL_checkinteger(L, 3))));
    return 1;
}

static int gfx_screenshot(lua_State* L) {
    lua_pushboolean(L, E(L).video.saveBmp(luaL_checkstring(L, 1)));
    return 1;
}

// ------------------------------------------------------------- audio ------

// audio.music(name [, loop=true]) plays <name>.wav from the --music folder
// (a recording to use instead of an FM song, e.g. "NEWSAN"); a number plays
// trackNN.wav. Returns false if the file isn't there.
static int audio_music(lua_State* L) {
    auto& e = E(L);
    bool loop = lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2);
    char name[64];
    if (lua_type(L, 1) == LUA_TNUMBER)
        std::snprintf(name, sizeof name, "track%02d.wav", ci(L, 1));
    else
        std::snprintf(name, sizeof name, "%s.wav", luaL_checkstring(L, 1));
    auto p = e.music.resolve(name);
    lua_pushboolean(L, p && e.audio.playMusicWav(p->string(), loop));
    return 1;
}

static int audio_stop_music(lua_State* L) { E(L).audio.stopMusic(); return 0; }
static int audio_music_playing(lua_State* L) { lua_pushboolean(L, E(L).audio.musicPlaying()); return 1; }
static int audio_music_volume(lua_State* L) { E(L).audio.setMusicVolume(static_cast<float>(luaL_checknumber(L, 1))); return 0; }

// audio.pcm(bytes, rate [, bits=8 [, channels=1 [, volume=1 [, loop=false]]]]) -> voice id
static int audio_pcm(lua_State* L) {
    size_t len = 0;
    const char* d = luaL_checklstring(L, 1, &len);
    int id = E(L).audio.playPcm(reinterpret_cast<const uint8_t*>(d), len, ci(L, 2),
                                static_cast<int>(luaL_optinteger(L, 3, 8)),
                                static_cast<int>(luaL_optinteger(L, 4, 1)),
                                static_cast<float>(luaL_optnumber(L, 5, 1.0)),
                                lua_toboolean(L, 6));
    lua_pushinteger(L, id);
    return 1;
}

// audio.song(data [, loop=true [, gm]]): plays an unpacked HERAD song
// (dat.read of NEWSAN.HSQ etc.) in the music style. `gm`, for the SoundFont
// style: { [instrument] = GM program 0..127, or 128 + a GM drum key }, the
// song's instruments counted from 0. Returns false if it won't parse.
static int audio_song(lua_State* L) {
    size_t len = 0;
    const char* d = luaL_checklstring(L, 1, &len);
    bool loop = lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2) != 0;
    std::vector<int> gm;
    if (lua_istable(L, 3)) {
        lua_pushnil(L);
        while (lua_next(L, 3)) {
            if (lua_isinteger(L, -2) && lua_isinteger(L, -1)) {
                const lua_Integer i = lua_tointeger(L, -2), v = lua_tointeger(L, -1);
                if (i >= 0 && i < 256 && v >= 0 && v < 256) {
                    if (gm.size() <= size_t(i)) gm.resize(size_t(i) + 1, -1);
                    gm[size_t(i)] = int(v);
                }
            }
            lua_pop(L, 1);
        }
    }
    lua_pushboolean(L, E(L).audio.playSong(reinterpret_cast<const uint8_t*>(d), len, loop, gm));
    return 1;
}

// audio.music_style([style]) -> "original", "stereo", "soundfont" or "enhanced", after
// setting it if given (the song playing carries on in the new style).
static int audio_music_style(lua_State* L) {
    static const char* const NAMES[] = {"original", "stereo", "soundfont", "enhanced", nullptr};
    Audio& a = E(L).audio;
    if (!lua_isnoneornil(L, 1)) a.setMusicStyle(MusicStyle(luaL_checkoption(L, 1, nullptr, NAMES)));
    lua_pushstring(L, NAMES[int(a.musicStyle())]);
    return 1;
}

// audio.soundfont() -> the .sf2 the SoundFont style plays, or nil if none.
static int audio_soundfont(lua_State* L) {
    const std::string p = E(L).audio.soundFontPath();
    if (p.empty()) lua_pushnil(L); else lua_pushstring(L, p.c_str());
    return 1;
}
static int audio_stop_song(lua_State* L) { E(L).audio.stopSong(); return 0; }
static int audio_fade_song(lua_State* L) { E(L).audio.fadeSong(luaL_optnumber(L, 1, 1.5)); return 0; }
static int audio_song_playing(lua_State* L) { lua_pushboolean(L, E(L).audio.songPlaying()); return 1; }
static int audio_song_volume(lua_State* L) { E(L).audio.setSongVolume(static_cast<float>(luaL_checknumber(L, 1))); return 0; }

static int audio_stop(lua_State* L) { E(L).audio.stopVoice(ci(L, 1)); return 0; }
static int audio_stop_all(lua_State* L) { E(L).audio.stopAll(); return 0; }

// ------------------------------------------------------------- input ------

static int input_down(lua_State* L)     { lua_pushboolean(L, E(L).input.down(luaL_checkstring(L, 1)));     return 1; }
static int input_pressed(lua_State* L)  { lua_pushboolean(L, E(L).input.pressed(luaL_checkstring(L, 1)));  return 1; }
static int input_released(lua_State* L) { lua_pushboolean(L, E(L).input.released(luaL_checkstring(L, 1))); return 1; }
static int input_any(lua_State* L)      { lua_pushboolean(L, E(L).input.anyPressed()); return 1; }
// Text entry: input.text_start() / text_stop(), input.text() -> what was
// typed this frame, input.paste() -> the clipboard's text ("" if none).
static int input_text_start(lua_State* L) { E(L).input.textStart(); return 0; }
static int input_text_stop(lua_State* L)  { E(L).input.textStop(); return 0; }
static int input_text(lua_State* L)       { lua_pushstring(L, E(L).input.text().c_str()); return 1; }
static int input_paste(lua_State* L) {
    char* t = SDL_HasClipboardText() ? SDL_GetClipboardText() : nullptr;
    lua_pushstring(L, t ? t : "");
    SDL_free(t);
    return 1;
}
static int input_key(lua_State* L) {
    auto k = E(L).input.firstPressed();
    if (k.empty()) lua_pushnil(L); else lua_pushstring(L, k.c_str());
    return 1;
}

// --------------------------------------------------------------- sys ------

// ---- HD folder (Options > HD FOLDER) ----
static std::string userDir() {
    char* base = SDL_GetPrefPath("Meguarace", "Meguarace");
    std::string r = base ? base : "";
    SDL_free(base);
    return r;
}

bool loadHd(Engine& e) {
    e.hdDir.clear();
    std::string user = userDir();
    if (e.hdOff || user.empty()) { e.video.disableHd(); return false; }
    std::string dir = hd::packFolder(user);
    std::error_code ec;
    bool old = false;
    if (std::filesystem::is_directory(dir, ec)) {
        for (const auto& f : std::filesystem::directory_iterator(dir, ec)) {
            if (f.path().extension() != ".hdv") continue;
            hd::PackHeader h;
            if (hd::Pack::peek(f.path().string(), h)) {
                if (!e.video.enableHd(h.pictureW, h.pictureH)) break;
                e.hdDir = dir;
                e.hdUpscaler = hd::upscalerName(h.upscaler);
                return true;
            }
            old = true;
        }
    }
    if (old) std::fprintf(stderr, "HD packs in %s are from an older hd-conversion-utility: run it again\n", dir.c_str());
    e.video.disableHd();
    return false;
}

// sys.hd, sys.hd_path, sys.hd_width, sys.hd_height: what loadHd found.
static void setHdFields(lua_State* L, Engine& e) {
    lua_pushboolean(L, e.video.hdAvailable()); lua_setfield(L, -2, "hd");
    lua_pushstring(L, e.hdDir.c_str());        lua_setfield(L, -2, "hd_path");
    lua_pushinteger(L, e.video.hdPictureW());  lua_setfield(L, -2, "hd_width");
    lua_pushinteger(L, e.video.hdPictureH());  lua_setfield(L, -2, "hd_height");
    lua_pushstring(L, e.video.hdAvailable() ? e.hdUpscaler.c_str() : "");
    lua_setfield(L, -2, "hd_upscaler");
}

// sys.hd_folder() -> the setting ("" = the default), the folder it means.
// sys.set_hd_folder(folder) -> packs found: saves the setting ("" = back to
// the default), looks there again and updates sys.hd and the rest. Packs are
// made by hd-conversion-utility, which writes to the same setting's folder.
static int sys_hd_folder(lua_State* L) {
    std::string user = userDir();
    lua_pushstring(L, user.empty() ? "" : hd::configuredFolder(user).c_str());
    lua_pushstring(L, user.empty() ? "" : hd::packFolder(user).c_str());
    return 2;
}
static int sys_set_hd_folder(lua_State* L) {
    std::string folder = luaL_checkstring(L, 1);
    Engine& e = E(L);
    bool saved = false;
    std::string user = userDir();
    saved = !user.empty() && hd::setConfiguredFolder(user, folder);
    bool found = saved && loadHd(e);
    lua_getglobal(L, "sys");
    setHdFields(L, e);
    lua_pop(L, 1);
    lua_pushboolean(L, found);
    lua_pushboolean(L, saved);
    return 2;
}

// sys.save(name, text) / sys.load(name) -> text|nil: small files in the
// per-user preferences folder (settings, high scores). Names are plain
// file names; anything else is refused.
static std::string prefFile(const char* name) {
    std::string n = name;
    if (n.empty() || n.find_first_of("/\\:") != std::string::npos || n[0] == '.') return {};
    char* base = SDL_GetPrefPath("Meguarace", "Meguarace");
    if (!base) return {};
    std::string path = std::string(base) + n;
    SDL_free(base);
    return path;
}
static int sys_save(lua_State* L) {
    size_t len = 0;
    std::string path = prefFile(luaL_checkstring(L, 1));
    const char* data = luaL_checklstring(L, 2, &len);
    bool ok = false;
    if (!path.empty()) {
        // Created readable and writable by the owner only (CodeQL finding).
        // On Windows the file lives in the user's profile, whose ACLs already
        // restrict it; the CRT only distinguishes read-only from writable.
#ifdef _WIN32
        int fd = _open(path.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
#endif
        if (fd >= 0) {
#ifdef _WIN32
            FILE* f = _fdopen(fd, "wb");
#else
            FILE* f = fdopen(fd, "wb");
#endif
            if (f) {
                ok = std::fwrite(data, 1, len, f) == len;
                ok = (std::fclose(f) == 0) && ok;
            } else {
#ifdef _WIN32
                _close(fd);
#else
                close(fd);
#endif
            }
        }
    }
    lua_pushboolean(L, ok);
    return 1;
}
static int sys_load(lua_State* L) {
    std::string path = prefFile(luaL_checkstring(L, 1));
    FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
    if (!f) { lua_pushnil(L); return 1; }
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

static int sys_ticks(lua_State* L) { lua_pushinteger(L, SDL_GetTicks64()); return 1; }
static int sys_quit(lua_State* L)  { E(L).quit = true; E(L).exitCode = int(luaL_optinteger(L, 1, 0)); return 0; }
static int sys_log(lua_State* L) {
    std::fprintf(stdout, "[lua] %s\n", luaL_tolstring(L, 1, nullptr));
    std::fflush(stdout);
    return 0;
}

// --------------------------------------------------------------- dat ------
// Files inside MEGARACE.DAT, by name ("NS11.HNM"), case-insensitive.

static int dat_open(lua_State* L) {
    auto& e = E(L);
    bool ok = e.dat.open(e.data, luaL_optstring(L, 1, "MEGARACE.DAT"));
    lua_pushboolean(L, ok);
    return 1;
}

static int dat_list(lua_State* L) {
    const auto& es = E(L).dat.entries();
    lua_createtable(L, int(es.size()), 0);
    for (size_t i = 0; i < es.size(); ++i) {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, es[i].name.c_str());   lua_setfield(L, -2, "name");
        lua_pushinteger(L, es[i].size);          lua_setfield(L, -2, "size");
        lua_pushinteger(L, es[i].offset);        lua_setfield(L, -2, "offset");
        lua_rawseti(L, -2, int(i + 1));
    }
    return 1;
}

static int dat_exists(lua_State* L) { lua_pushboolean(L, E(L).dat.find(luaL_checkstring(L, 1)) != nullptr); return 1; }

static int dat_size(lua_State* L) {
    auto* en = E(L).dat.find(luaL_checkstring(L, 1));
    if (en) lua_pushinteger(L, en->size); else lua_pushnil(L);
    return 1;
}

// dat.read(name [, unpack=true]): HSQ files come back decompressed by default.
static int dat_read(lua_State* L) {
    auto d = E(L).dat.read(luaL_checkstring(L, 1));
    if (!d) { lua_pushnil(L); lua_pushstring(L, "not in archive"); return 2; }
    bool unpack = lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2);
    if (unpack && isHsq(d->data(), d->size())) {
        try { *d = unHsq(d->data(), d->size()); }
        catch (const std::exception& ex) { lua_pushnil(L); lua_pushstring(L, ex.what()); return 2; }
    }
    pushBytes(L, *d);
    return 1;
}

static int dat_read_range(lua_State* L) {
    auto* en = E(L).dat.find(luaL_checkstring(L, 1));
    if (!en) { lua_pushnil(L); return 1; }
    auto d = E(L).dat.readRange(*en, uint32_t(luaL_checkinteger(L, 2)), uint32_t(luaL_checkinteger(L, 3)));
    if (!d) { lua_pushnil(L); return 1; }
    pushBytes(L, *d);
    return 1;
}

// --------------------------------------------------------------- hnm ------
// local v = hnm.open("NS11.HNM")
// v:frames() v:sound_rate() v:decode(i) -> info  v:draw([ox, oy]) v:apply_palette()
// v:pixels() v:sound()

struct HnmUd {
    HnmVideo video;
    HnmVideo::Frame frame;
    Engine* eng;
    int index = -1;                 // last decoded frame
    std::unique_ptr<hd::Pack> pack; // HD pack for this video, if the player made one
    std::string name;
    // Frames drawn from the pack, pixels of those shown original because the
    // game had changed their colour, frames without a picture (MEGARACE_HD_STATS=1).
    long hdDraws = 0, recoloured = 0, pictureMisses = 0;
    ~HnmUd() {
        if (pack && std::getenv("MEGARACE_HD_STATS"))
            std::printf("HD %s: %ld frames from the pack (%ld pixels recoloured by the game, shown original), "
                        "%ld original (no picture)\n", name.c_str(), hdDraws, recoloured, pictureMisses);
    }
};
static const char* kHnmMeta = "megarace.hnm";

static HnmUd* checkHnm(lua_State* L) { return static_cast<HnmUd*>(luaL_checkudata(L, 1, kHnmMeta)); }

static int hnm_gc(lua_State* L) { checkHnm(L)->~HnmUd(); return 0; }
static int hnm_frames(lua_State* L) { lua_pushinteger(L, checkHnm(L)->video.frameCount()); return 1; }
static int hnm_rate(lua_State* L) { lua_pushinteger(L, checkHnm(L)->video.soundRate()); return 1; }

static int hnm_decode(lua_State* L) {
    HnmUd* h = checkHnm(L);
    int i = int(luaL_checkinteger(L, 2));
    bool ok = false;
    try { ok = h->video.decode(i, h->frame); }
    catch (const std::exception& ex) { return luaL_error(L, "hnm frame %d: %s", i, ex.what()); }
    if (!ok) { lua_pushnil(L); return 1; }
    h->index = i;
    const auto& f = h->frame;
    lua_createtable(L, 0, 10);
    lua_pushboolean(L, f.hasVideo);       lua_setfield(L, -2, "video");
    lua_pushinteger(L, f.w);              lua_setfield(L, -2, "w");
    lua_pushinteger(L, f.h);              lua_setfield(L, -2, "h");
    lua_pushinteger(L, f.x);              lua_setfield(L, -2, "x");
    lua_pushinteger(L, f.y);              lua_setfield(L, -2, "y");
    lua_pushboolean(L, f.overlay);        lua_setfield(L, -2, "overlay");
    lua_pushboolean(L, f.paletteChanged); lua_setfield(L, -2, "palette_changed");
    if (f.subtitleCue == 0x40) { lua_pushstring(L, "next");  lua_setfield(L, -2, "subtitle"); }
    if (f.subtitleCue == 0x20) { lua_pushstring(L, "clear"); lua_setfield(L, -2, "subtitle"); }
    lua_pushinteger(L, lua_Integer(f.sound.size())); lua_setfield(L, -2, "sound_bytes");
    for (const auto& [tag, body] : f.chunks) {   // game data: "pt", "kl"
        pushBytes(L, body);
        lua_setfield(L, -2, tag.c_str());
    }
    return 1;
}

// Draws a decoded frame's pixels: from the HD pack when there is a picture
// for that frame (pixels whose colour the game has changed since, in a fade
// or a recoloured index, show the original), else the original pixels.
static void drawFrame(HnmUd* h, int index, int ox, int oy, int fx, int fy, int w, int ht,
                      const uint8_t* px, size_t len, int key) {
    Video& v = h->eng->video;
    if (h->pack && v.hdOn()) {
        if (const auto* pic = h->pack->frame(index)) {
            h->recoloured += v.blitHd(ox, oy, fx, fy, w, ht, px, len, key, pic->data(), h->pack->hdWidth(),
                                      h->pack->hdHeight(), h->video.palette().data());
            ++h->hdDraws;
            return;
        }
        ++h->pictureMisses;
    }
    v.blitVideo(ox + fx, oy + fy, w, ht, px, len, key);
}

static int hnm_draw(lua_State* L) {
    HnmUd* h = checkHnm(L);
    const auto& f = h->frame;
    if (!f.hasVideo) return 0;
    int ox = int(luaL_optinteger(L, 2, 0)), oy = int(luaL_optinteger(L, 3, 0));
    drawFrame(h, h->index, ox, oy, f.x, f.y, f.w, f.h, f.pixels.data(), f.pixels.size(),
              f.overlay ? 0 : -1);
    return 0;
}

// v:blit(i, x, y, w, h, pixels [, key]): gfx.blit for frame i of this video,
// decoded earlier (the player decodes ahead and draws later). x, y are the
// frame's own position (decode's info.x, info.y). Uses the HD pack like :draw.
static int hnm_blit(lua_State* L) {
    HnmUd* h = checkHnm(L);
    int i = int(luaL_checkinteger(L, 2));
    int x = int(luaL_checkinteger(L, 3)), y = int(luaL_checkinteger(L, 4));
    int w = int(luaL_checkinteger(L, 5)), ht = int(luaL_checkinteger(L, 6));
    size_t len = 0;
    const char* px = luaL_checklstring(L, 7, &len);
    int key = int(luaL_optinteger(L, 8, -1));
    drawFrame(h, i, 0, 0, x, y, w, ht, reinterpret_cast<const uint8_t*>(px), len, key);
    return 0;
}

// v:hd() -> true when this video plays from an HD pack
static int hnm_hd(lua_State* L) {
    lua_pushboolean(L, checkHnm(L)->pack != nullptr);
    return 1;
}

static int hnm_apply_palette(lua_State* L) {
    HnmUd* h = checkHnm(L);
    const auto& p = h->video.palette();
    // Index 0 stays as the video defines it; MegaRace palettes start at 1.
    h->eng->video.setPalette(p.data(), p.size(), 0, false);
    return 0;
}

// v:chunks(i) -> { pt = bytes, kl = bytes } without decoding the picture
static int hnm_chunks(lua_State* L) {
    HnmUd* h = checkHnm(L);
    std::map<std::string, std::vector<uint8_t>> c;
    if (!h->video.dataChunks(int(luaL_checkinteger(L, 2)), c)) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 2);
    for (const auto& [tag, body] : c) { pushBytes(L, body); lua_setfield(L, -2, tag.c_str()); }
    return 1;
}

static int hnm_pixels(lua_State* L) { pushBytes(L, checkHnm(L)->frame.pixels); return 1; }
static int hnm_sound(lua_State* L) { pushBytes(L, checkHnm(L)->frame.sound); return 1; }

static int hnm_open(lua_State* L) {
    Engine& e = E(L);
    const char* name = luaL_checkstring(L, 1);
    auto d = e.dat.read(name);
    if (!d) d = e.data.read(name);   // also allow loose files
    if (!d) { lua_pushnil(L); lua_pushstring(L, "not found"); return 2; }
    auto* h = static_cast<HnmUd*>(lua_newuserdatauv(L, sizeof(HnmUd), 0));
    new (h) HnmUd();
    h->eng = &e;
    luaL_setmetatable(L, kHnmMeta);
    const uint32_t crc = hd::crc32(d->data(), d->size());
    if (!h->video.load(std::move(*d))) { lua_pushnil(L); lua_pushstring(L, "bad HNM"); return 2; }
    // <hd folder>/<NAME>.hdv, made from exactly these bytes, at the session's
    // size. Only plain archive names (B01.HNM) map to a pack.
    const std::string stem = hd::safeVideoStem(std::filesystem::path(name).filename().string());
    if (!e.hdDir.empty() && e.video.hdAvailable() && !stem.empty()) {
        std::filesystem::path p = std::filesystem::path(e.hdDir) / (stem + ".hdv");
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) {
            auto pack = std::make_unique<hd::Pack>();
            const auto& ph = pack->header();
            if (!pack->open(p.string(), crc))
                std::fprintf(stderr, "HD pack %s ignored: %s\n", p.string().c_str(), pack->error().c_str());
            else if (ph.pictureW != e.video.hdPictureW() || ph.pictureH != e.video.hdPictureH() ||
                     ph.width != e.video.width() || ph.height != e.video.height())
                std::fprintf(stderr, "HD pack %s ignored: made at %dx%d, the others are %dx%d\n",
                             p.string().c_str(), ph.pictureW, ph.pictureH, e.video.hdPictureW(),
                             e.video.hdPictureH());
            else if (int(ph.frames) != h->video.frameCount())
                std::fprintf(stderr, "HD pack %s ignored: frame count differs\n", p.string().c_str());
            else {
                h->pack = std::move(pack);
                h->name = stem;
            }
        }
    }
    return 1;
}

// --------------------------------------------------------------- spr ------
// local s = spr.open("JO_A")        -- JO_A.HSQ from MEGARACE.DAT (unpacked)
// s:count()  s:size(i) -> w, h (nil for data entries)  s:apply_palette([shift])
// s:draw(i, x, y [, scale=1 [, flip=false]])   (i is 0-based, x/y top-left)
// s:pixels(i) -> bytes

struct SprUd {
    SpriteSet set;
    Engine* eng;
    // HD sprite pack (menu sets, made by hd-conversion-utility), if any.
    std::unique_ptr<hd::SpritePackData> hd;
    std::string name;
    long hdDraws = 0, recoloured = 0;
    ~SprUd() {
        if (hd && std::getenv("MEGARACE_HD_STATS"))
            std::printf("HD %s: %ld sprites from the pack (%ld pixels recoloured by the game, shown original)\n",
                        name.c_str(), hdDraws, recoloured);
    }
};
static const char* kSprMeta = "megarace.spr";
static SprUd* checkSpr(lua_State* L) { return static_cast<SprUd*>(luaL_checkudata(L, 1, kSprMeta)); }

static int spr_gc(lua_State* L) { checkSpr(L)->~SprUd(); return 0; }
static int spr_count(lua_State* L) { lua_pushinteger(L, lua_Integer(checkSpr(L)->set.count())); return 1; }

static int spr_size(lua_State* L) {
    const auto* img = checkSpr(L)->set.image(size_t(luaL_checkinteger(L, 2)));
    if (!img) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, img->w);
    lua_pushinteger(L, img->h);
    return 2;
}

static int spr_apply_palette(lua_State* L) {
    SprUd* s = checkSpr(L);
    const auto& p = s->set.paletteBlock();
    s->eng->video.applyPaletteBlocks(p.data(), p.size(), int(luaL_optinteger(L, 2, 0)));
    return 0;
}

static int spr_draw(lua_State* L) {
    SprUd* s = checkSpr(L);
    const size_t i = size_t(luaL_checkinteger(L, 2));
    const auto* img = s->set.image(i);
    if (!img) return 0;
    const int x = int(luaL_checknumber(L, 3)), y = int(luaL_checknumber(L, 4));
    const double scale = luaL_optnumber(L, 5, 1.0);
    const bool flip = lua_toboolean(L, 6);
    Video& v = s->eng->video;
    // From the HD pack at scale 1, when the screen shows the colours it was
    // made in (colour packs) or always (indexed packs, coloured as drawn).
    if (s->hd && scale == 1.0 && v.hdOn() && i < s->hd->images.size()) {
        const auto& h = s->hd->images[i];
        if (!h.px.empty() && h.w == img->w && h.h == img->h) {
            const bool indexed = s->hd->kind == 1;
            if (v.spriteHd(img->pixels.data(), img->w, img->h, x, y, flip, h.px.data(), h.hdW, h.hdH, indexed,
                           s->hd->palette.data(), &s->recoloured)) {
                ++s->hdDraws;
                return 0;
            }
        }
    }
    v.sprite(img->pixels.data(), img->w, img->h, x, y, scale, flip);
    return 0;
}

static int spr_pixels(lua_State* L) {
    const auto* img = checkSpr(L)->set.image(size_t(luaL_checkinteger(L, 2)));
    if (!img) { lua_pushnil(L); return 1; }
    pushBytes(L, img->pixels);
    return 1;
}

static int spr_open(lua_State* L) {
    Engine& e = E(L);
    std::string name = luaL_checkstring(L, 1);
    if (name.find('.') == std::string::npos) name += ".HSQ";
    auto d = e.dat.read(name);
    if (!d) { lua_pushnil(L); lua_pushstring(L, "not found"); return 2; }
    const uint32_t crc = hd::crc32(d->data(), d->size());   // of the set as stored (HD packs match it)
    try {
        if (isHsq(d->data(), d->size())) *d = unHsq(d->data(), d->size());
        auto* s = static_cast<SprUd*>(lua_newuserdatauv(L, sizeof(SprUd), 0));
        new (s) SprUd();
        s->eng = &e;
        luaL_setmetatable(L, kSprMeta);
        if (!s->set.load(*d)) { lua_pushnil(L); lua_pushstring(L, "not a sprite set"); return 2; }
    } catch (const std::exception& ex) {
        lua_pushnil(L); lua_pushstring(L, ex.what()); return 2;
    }
    // <hd folder>/<NAME>.hds, made from exactly these bytes, at the session's size.
    std::string stem = hd::safeSpriteStem(name);
    if (!e.hdDir.empty() && e.video.hdAvailable() && !stem.empty()) {
        std::filesystem::path p = std::filesystem::path(e.hdDir) / (stem + ".hds");
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) {
            auto pack = std::make_unique<hd::SpritePackData>();
            std::string err;
            SprUd* s = static_cast<SprUd*>(lua_touserdata(L, -1));
            if (!hd::readSpritePack(p.string(), crc, *pack, err))
                std::fprintf(stderr, "HD sprites %s ignored: %s\n", p.string().c_str(), err.c_str());
            else if (pack->pictureW != e.video.hdPictureW() || pack->pictureH != e.video.hdPictureH() ||
                     pack->images.size() != s->set.count())
                std::fprintf(stderr, "HD sprites %s ignored: they don't match the videos or the set\n",
                             p.string().c_str());
            else {
                s->hd = std::move(pack);
                s->name = stem;
            }
        }
    }
    return 1;
}

// gfx.clip(lines [, y_offset=0]): lines[row] = {x0,x1, ...} (rows 0-based);
// sprites are drawn only inside the spans. gfx.unclip() removes the mask.
static int gfx_clip(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    std::vector<std::vector<int>> rows(256);
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        if (lua_isinteger(L, -2) && lua_istable(L, -1)) {
            lua_Integer r = lua_tointeger(L, -2);
            if (r >= 0 && r < 256) {
                auto& v = rows[size_t(r)];
                lua_Integer n = luaL_len(L, -1);
                for (lua_Integer k = 1; k <= n; ++k) {
                    lua_rawgeti(L, -1, k);
                    v.push_back(int(lua_tointeger(L, -1)));
                    lua_pop(L, 1);
                }
            }
        }
        lua_pop(L, 1);
    }
    E(L).video.setClip(std::move(rows), int(luaL_optinteger(L, 2, 0)));
    return 0;
}
static int gfx_unclip(lua_State* L) { E(L).video.clearClip(); return 0; }

// audio.stream_reset() / stream_push(bytes, rate[, bits, ch]) / stream_queued() / stream_played()
static int audio_available(lua_State* L) { lua_pushboolean(L, E(L).audio.available()); return 1; }
static int audio_stream_reset(lua_State* L) { E(L).audio.streamReset(); return 0; }
static int audio_stream_push(lua_State* L) {
    size_t len = 0;
    const char* d = luaL_checklstring(L, 1, &len);
    E(L).audio.streamPush(reinterpret_cast<const uint8_t*>(d), len, ci(L, 2),
                          int(luaL_optinteger(L, 3, 8)), int(luaL_optinteger(L, 4, 1)));
    return 0;
}
// audio.stream_voice_filter(v): hiss reduction for the stream until the next
// stream_reset, with the noise measured over video v's whole soundtrack
// (the announcer's clips; see voice_filter.h). Returns false if v has no
// usable sound. audio.stream_voice_bypass(b): the original sound instead
// (same timing), e.g. to compare.
static int audio_stream_voice_filter(lua_State* L) {
    HnmUd* h = checkHnm(L);
    int rate = 0;
    auto pcm = h->video.soundTrack(&rate);
    if (rate <= 0) rate = 11111;
    auto noise = VoiceFilter::profile(pcm.data(), pcm.size(), rate);
    bool ok = !noise.empty();
    if (ok) E(L).audio.streamVoiceFilter(std::move(noise), rate);
    lua_pushboolean(L, ok);
    return 1;
}
// audio.stream_end(): no more pushes for this stream (the hiss filter lets
// its last samples out, so the sound is as long as the video's).
static int audio_stream_end(lua_State* L) { E(L).audio.streamEnd(); return 0; }
static int audio_stream_voice_bypass(lua_State* L) {
    E(L).audio.streamVoiceBypass(lua_toboolean(L, 1) != 0);
    return 0;
}
static int audio_stream_queued(lua_State* L) { lua_pushnumber(L, E(L).audio.streamQueuedSeconds()); return 1; }
static int audio_stream_played(lua_State* L) {
    auto& a = E(L).audio;
    lua_pushnumber(L, a.deviceRate() ? double(a.streamPlayedFrames()) / a.deviceRate() : 0.0);
    return 1;
}

static void addLib(lua_State* L, Engine& e, const char* name, const luaL_Reg* fns) {
    lua_newtable(L);
    lua_pushlightuserdata(L, &e);
    luaL_setfuncs(L, fns, 1);
    lua_setglobal(L, name);
}

void registerLuaApi(lua_State* L, Engine& e) {
    static const luaL_Reg fs[] = {
        {"mounted", fs_mounted}, {"root", fs_root}, {"exists", fs_exists}, {"size", fs_size},
        {"read", fs_read}, {"read_range", fs_read_range}, {"list", fs_list}, {nullptr, nullptr}};
    static const luaL_Reg gfx[] = {
        {"width", gfx_width}, {"height", gfx_height}, {"clear", gfx_clear}, {"pset", gfx_pset},
        {"pget", gfx_pget}, {"fill", gfx_fill}, {"line", gfx_line},
        {"clip", gfx_clip}, {"unclip", gfx_unclip}, {"text", gfx_text}, {"blit", gfx_blit},
        {"palette", gfx_palette}, {"setcolor", gfx_setcolor}, {"screenshot", gfx_screenshot},
        {"displays", gfx_displays}, {"display", gfx_display}, {"set_display", gfx_set_display},
        {"to_display", gfx_to_display}, {"nearest", gfx_nearest}, {"hd_pictures", gfx_hd_pictures}, {"smoothing", gfx_smoothing}, {"video_repeat", gfx_video_repeat},
        {"aspect", gfx_aspect}, {"wing", gfx_wing}, {"ambient", gfx_ambient},
        {nullptr, nullptr}};
    static const luaL_Reg audio[] = {
        {"music", audio_music}, {"stop_music", audio_stop_music},
        {"music_playing", audio_music_playing}, {"music_volume", audio_music_volume},
        {"pcm", audio_pcm}, {"stop", audio_stop}, {"stop_all", audio_stop_all},
        {"stream_reset", audio_stream_reset}, {"stream_push", audio_stream_push},
        {"stream_queued", audio_stream_queued}, {"stream_played", audio_stream_played},
        {"stream_voice_filter", audio_stream_voice_filter}, {"stream_voice_bypass", audio_stream_voice_bypass},
        {"stream_end", audio_stream_end},
        {"available", audio_available},
        {"song", audio_song}, {"stop_song", audio_stop_song}, {"fade_song", audio_fade_song},
        {"song_playing", audio_song_playing}, {"song_volume", audio_song_volume},
        {"music_style", audio_music_style}, {"soundfont", audio_soundfont},
        {nullptr, nullptr}};
    static const luaL_Reg dat[] = {
        {"open", dat_open}, {"list", dat_list}, {"exists", dat_exists}, {"size", dat_size},
        {"read", dat_read}, {"read_range", dat_read_range}, {nullptr, nullptr}};
    static const luaL_Reg hnmlib[] = {{"open", hnm_open}, {nullptr, nullptr}};
    static const luaL_Reg sprlib[] = {{"open", spr_open}, {nullptr, nullptr}};
    static const luaL_Reg sprMethods[] = {
        {"count", spr_count}, {"size", spr_size}, {"apply_palette", spr_apply_palette},
        {"draw", spr_draw}, {"pixels", spr_pixels}, {nullptr, nullptr}};
    static const luaL_Reg hnmMethods[] = {
        {"frames", hnm_frames}, {"sound_rate", hnm_rate}, {"decode", hnm_decode},
        {"draw", hnm_draw}, {"apply_palette", hnm_apply_palette}, {"pixels", hnm_pixels},
        {"sound", hnm_sound}, {"chunks", hnm_chunks}, {"hd", hnm_hd}, {"blit", hnm_blit}, {nullptr, nullptr}};
    static const luaL_Reg input[] = {
        {"down", input_down}, {"pressed", input_pressed}, {"released", input_released},
        {"any", input_any}, {"key", input_key},
        {"text_start", input_text_start}, {"text_stop", input_text_stop}, {"text", input_text},
        {"paste", input_paste},
        {nullptr, nullptr}};
    static const luaL_Reg sys[] = {
        {"ticks", sys_ticks}, {"quit", sys_quit}, {"log", sys_log},
        {"save", sys_save}, {"load", sys_load},
        {"hd_folder", sys_hd_folder}, {"set_hd_folder", sys_set_hd_folder}, {nullptr, nullptr}};

    addLib(L, e, "fs", fs);
    addLib(L, e, "gfx", gfx);
    addLib(L, e, "audio", audio);
    addLib(L, e, "input", input);
    addLib(L, e, "sys", sys);
    addLib(L, e, "dat", dat);
    addLib(L, e, "hnm", hnmlib);
    addLib(L, e, "spr", sprlib);

    luaL_newmetatable(L, kSprMeta);
    lua_newtable(L);
    lua_pushlightuserdata(L, &e);
    luaL_setfuncs(L, sprMethods, 1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, spr_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    luaL_newmetatable(L, kHnmMeta);
    lua_newtable(L);
    lua_pushlightuserdata(L, &e);
    luaL_setfuncs(L, hnmMethods, 1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, hnm_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    lua_getglobal(L, "sys");
    lua_pushstring(L, e.dataArg.c_str());  lua_setfield(L, -2, "data_path");
    lua_pushstring(L, e.musicArg.c_str()); lua_setfield(L, -2, "music_path");
    lua_pushboolean(L, e.music.mounted()); lua_setfield(L, -2, "music_mounted");
    // HD content: whether packs were found, their folder and picture size,
    // and the --display given on the command line ("" if none).
    setHdFields(L, e);
    lua_pushstring(L, e.displayArg.c_str());   lua_setfield(L, -2, "display_arg");
    lua_pop(L, 1);
}
