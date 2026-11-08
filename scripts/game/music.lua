-- The game's music: six FM songs in MEGARACE.DAT (HERAD format), played on
-- an emulated OPL2 chip by the engine (audio.song). Which song plays where
-- is read from RACEPRG.EXE:
--   song id n  = resource 0x89 + n (0xC402): 1 NEWSAN ... 6 PAGA
--   each round = DS:0x266[round] + 1 (0x02AA, at the start of a race)
--   song 6     = after the post-race videos (0x5198), faded out with the
--                driver's fade (0xC35E, 300 driver ticks = 1.5 s)
-- Asking for the song that is already playing doesn't restart it (0xC33C).
--
-- A recording can replace any song: put <NAME>.wav (e.g. NEWSAN.wav) in the
-- --music folder.
--
-- Styles (Options > ADVANCED > MUSIC, audio.music_style): ORIGINAL, the OPL2
-- as on a Sound Blaster; STEREO FM, the same FM in stereo with EQ and reverb;
-- SOUNDFONT, the notes played by a General MIDI SoundFont; ENHANCED (the
-- default), a dance remix on that SoundFont (layered synths, dance drums,
-- sidechain, a delay in time with the song).

local M = {}

-- Names from TCRF's list of the game's music IDs.
M.SONGS = {
    { file = "NEWSAN",   title = "NewSan" },
    { file = "MAEVA",    title = "Maeva" },
    { file = "DETRITUS", title = "Factory Land" },
    { file = "LENNY",    title = "Fractalian Space" },
    { file = "ALARME",   title = "Terminal City" },
    { file = "PAGA",     title = "SkyHolder" },
}

-- Song per championship round (data.TRACKS order), DS:0x266 + 1.
M.ROUND_SONG = { 1, 2, 3, 4, 5, 1, 2, 4, 3, 5, 2, 3, 1, 4, 6, 6 }

M.AFTER_RACE = 6
M.FADE = 1.5

-- SOUNDFONT: the engine picks a GM instrument for each FM instrument from
-- its sound (src/formats/gm_map.cpp). To change one, list it here by the
-- song's instrument number (from 0): a GM program 0..127 (e.g. 81 = saw
-- lead), or M.DRUM + a GM drum key (e.g. M.DRUM + 38 = snare).
M.DRUM = 128
M.GM = {
    -- NEWSAN = { [4] = 81, [13] = M.DRUM + 36 },
}

M.STYLES = { "original", "stereo", "soundfont", "enhanced" }
M.LABELS = { original = "ORIGINAL", stereo = "STEREO FM", soundfont = "SOUNDFONT", enhanced = "ENHANCED" }

M.enabled = true
local current = nil          -- song id playing, or nil

function M.song_for_round(round) return M.ROUND_SONG[round] end

function M.song_for_video(video)
    local data = require("game.data")
    for i, t in ipairs(data.TRACKS) do
        if t.video == video then return M.ROUND_SONG[i] end
    end
    -- Second-lap variants (NS12 etc.) share their track's song.
    local base = video and video:match("^(.-)2$")
    if base then return M.song_for_video(base .. "1") end
    return nil
end

function M.play(id)
    if not M.enabled or not id then return false end
    if id == current and (audio.song_playing() or audio.music_playing()) then return true end
    local song = M.SONGS[id]
    if not song then return false end
    audio.stop_song()
    audio.stop_music()
    current = nil
    if audio.music(song.file) then current = id; return true end      -- recorded override
    local bytes = dat.exists(song.file .. ".HSQ") and dat.read(song.file .. ".HSQ")
    if bytes and audio.song(bytes, true, M.GM[song.file]) then current = id; return true end
    return false
end

function M.stop(fade)
    if fade then audio.fade_song(M.FADE) else audio.stop_song() end
    audio.stop_music()
    current = nil
end

function M.playing() return current end

-- The music style: "original", "stereo", "soundfont" or "enhanced" (others,
-- and "edm", its name for a while: enhanced).
function M.set_style(style)
    if style == "edm" then style = "enhanced" end
    local ok = false
    for _, s in ipairs(M.STYLES) do ok = ok or s == style end
    return audio.music_style(ok and style or "enhanced")
end

function M.set_enabled(on)
    M.enabled = on
    if not on then M.stop() end
end

return M
