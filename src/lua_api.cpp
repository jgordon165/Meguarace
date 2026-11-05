#include "lua_api.h"
#include "engine.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <cstdio>
#include <string>

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

static int gfx_screenshot(lua_State* L) {
    lua_pushboolean(L, E(L).video.saveBmp(luaL_checkstring(L, 1)));
    return 1;
}

// ------------------------------------------------------------- audio ------

// audio.music(track [, loop=true]) plays trackNN.wav from the music folder.
static int audio_music(lua_State* L) {
    auto& e = E(L);
    int track = ci(L, 1);
    bool loop = lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2);
    char name[32];
    std::snprintf(name, sizeof name, "track%02d.wav", track);
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

static int audio_stop(lua_State* L) { E(L).audio.stopVoice(ci(L, 1)); return 0; }
static int audio_stop_all(lua_State* L) { E(L).audio.stopAll(); return 0; }

// ------------------------------------------------------------- input ------

static int input_down(lua_State* L)     { lua_pushboolean(L, E(L).input.down(luaL_checkstring(L, 1)));     return 1; }
static int input_pressed(lua_State* L)  { lua_pushboolean(L, E(L).input.pressed(luaL_checkstring(L, 1)));  return 1; }
static int input_released(lua_State* L) { lua_pushboolean(L, E(L).input.released(luaL_checkstring(L, 1))); return 1; }

// --------------------------------------------------------------- sys ------

static int sys_ticks(lua_State* L) { lua_pushinteger(L, SDL_GetTicks64()); return 1; }
static int sys_quit(lua_State* L)  { E(L).quit = true; return 0; }
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

struct HnmUd { HnmVideo video; HnmVideo::Frame frame; Engine* eng; };
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
    const auto& f = h->frame;
    lua_createtable(L, 0, 10);
    lua_pushboolean(L, f.hasVideo);       lua_setfield(L, -2, "video");
    lua_pushinteger(L, f.w);              lua_setfield(L, -2, "w");
    lua_pushinteger(L, f.h);              lua_setfield(L, -2, "h");
    lua_pushinteger(L, f.x);              lua_setfield(L, -2, "x");
    lua_pushinteger(L, f.y);              lua_setfield(L, -2, "y");
    lua_pushboolean(L, f.overlay);        lua_setfield(L, -2, "overlay");
    lua_pushboolean(L, f.paletteChanged); lua_setfield(L, -2, "palette_changed");
    lua_pushinteger(L, lua_Integer(f.sound.size())); lua_setfield(L, -2, "sound_bytes");
    for (const auto& [tag, body] : f.chunks) {   // game data: "pt", "kl"
        pushBytes(L, body);
        lua_setfield(L, -2, tag.c_str());
    }
    return 1;
}

static int hnm_draw(lua_State* L) {
    HnmUd* h = checkHnm(L);
    const auto& f = h->frame;
    if (!f.hasVideo) return 0;
    int ox = int(luaL_optinteger(L, 2, 0)), oy = int(luaL_optinteger(L, 3, 0));
    h->eng->video.blit(ox + f.x, oy + f.y, f.w, f.h, f.pixels.data(), f.pixels.size(),
                       f.overlay ? 0 : -1);
    return 0;
}

static int hnm_apply_palette(lua_State* L) {
    HnmUd* h = checkHnm(L);
    const auto& p = h->video.palette();
    // Index 0 stays as the video defines it; MegaRace palettes start at 1.
    h->eng->video.setPalette(p.data(), p.size(), 0, false);
    return 0;
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
    if (!h->video.load(std::move(*d))) { lua_pushnil(L); lua_pushstring(L, "bad HNM"); return 2; }
    return 1;
}

// audio.stream_reset() / stream_push(bytes, rate[, bits, ch]) / stream_queued() / stream_played()
static int audio_stream_reset(lua_State* L) { E(L).audio.streamReset(); return 0; }
static int audio_stream_push(lua_State* L) {
    size_t len = 0;
    const char* d = luaL_checklstring(L, 1, &len);
    E(L).audio.streamPush(reinterpret_cast<const uint8_t*>(d), len, ci(L, 2),
                          int(luaL_optinteger(L, 3, 8)), int(luaL_optinteger(L, 4, 1)));
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
        {"pget", gfx_pget}, {"fill", gfx_fill}, {"text", gfx_text}, {"blit", gfx_blit},
        {"palette", gfx_palette}, {"setcolor", gfx_setcolor}, {"screenshot", gfx_screenshot},
        {nullptr, nullptr}};
    static const luaL_Reg audio[] = {
        {"music", audio_music}, {"stop_music", audio_stop_music},
        {"music_playing", audio_music_playing}, {"music_volume", audio_music_volume},
        {"pcm", audio_pcm}, {"stop", audio_stop}, {"stop_all", audio_stop_all},
        {"stream_reset", audio_stream_reset}, {"stream_push", audio_stream_push},
        {"stream_queued", audio_stream_queued}, {"stream_played", audio_stream_played},
        {nullptr, nullptr}};
    static const luaL_Reg dat[] = {
        {"open", dat_open}, {"list", dat_list}, {"exists", dat_exists}, {"size", dat_size},
        {"read", dat_read}, {"read_range", dat_read_range}, {nullptr, nullptr}};
    static const luaL_Reg hnmlib[] = {{"open", hnm_open}, {nullptr, nullptr}};
    static const luaL_Reg hnmMethods[] = {
        {"frames", hnm_frames}, {"sound_rate", hnm_rate}, {"decode", hnm_decode},
        {"draw", hnm_draw}, {"apply_palette", hnm_apply_palette}, {"pixels", hnm_pixels},
        {"sound", hnm_sound}, {nullptr, nullptr}};
    static const luaL_Reg input[] = {
        {"down", input_down}, {"pressed", input_pressed}, {"released", input_released},
        {nullptr, nullptr}};
    static const luaL_Reg sys[] = {
        {"ticks", sys_ticks}, {"quit", sys_quit}, {"log", sys_log}, {nullptr, nullptr}};

    addLib(L, e, "fs", fs);
    addLib(L, e, "gfx", gfx);
    addLib(L, e, "audio", audio);
    addLib(L, e, "input", input);
    addLib(L, e, "sys", sys);
    addLib(L, e, "dat", dat);
    addLib(L, e, "hnm", hnmlib);

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
    lua_pop(L, 1);
}
