-- Plays an HNM video from MEGARACE.DAT. Videos with a soundtrack are
-- clocked by the audio (the picture follows the sound, as on the original
-- CD player); silent ones (the track videos) run at the original 12.5 fps.
--
--   scenes.push(require("scenes.player"), "INTRO.HNM", { on_done = fn, loop = false })
--
-- ENTER/SPACE skip the video; ESC skips it and tells on_done("all"), which a
-- sequence takes as "skip the rest" (RACEPRG 0x5C98: one key ends the clip,
-- the other the whole run of clips). opts.mute plays the picture only (the
-- credits roll over the music). opts.hold keeps the last frame up for that
-- many seconds, or until a key, once the video has played to its end (the
-- car showcases: 0x5687).
--
-- The announcer's clips (Lance, B*.HNM) play through the port's hiss filter
-- (src/voice_filter.h) unless S.voice_filter is off. V switches it off and
-- on while a clip plays, to compare; S.on_voice_filter(on) is told (the menu
-- saves it). With S.lance_smoother on, they also play one SMOOTHING level
-- softer than the other videos (MAX at most), put back when the clip ends.

local scenes = require("core.scenes")
local ui     = require("core.ui")
local music  = require("game.music")
local subs   = require("formats.subtitles")
local S = {}

-- Subtitle language code (formats/subtitles.lua), or nil for none. Set by
-- the menu from the saved settings.
S.language = nil
-- Hiss filter on the announcer's clips (set by the menu from the settings).
S.voice_filter = true
S.on_voice_filter = nil
-- Lance's clips one smoothing level softer (set by the menu from the settings).
S.lance_smoother = false

-- The announcer's clips: every B*.HNM (career.VIDEO 0x30..0x6F).
function S.is_voice(name)
    return name:upper():match("^B%w+%.HNM$") ~= nil
end

local SILENT_FPS = 12.5
local PREBUFFER  = 0.25    -- seconds of audio queued ahead of the picture

function S:enter(name, opts)
    self.name, self.opts = name, opts or {}
    self.v = assert(hnm.open(name))
    self.count = self.v:frames()
    self.decoded = -1
    self.shown = -1
    self.clock = 0
    self.sound = false
    self.bytesBefore = {}     -- sound bytes queued before each frame (A/V sync)
    self.queuedBytes = 0
    self.paused = false
    self.queue = {}
    self.lines = S.language and subs.for_video(subs.load(S.language), name) or nil
    self.line = 0
    self.showing, self.cleared = nil, false
    self.holding = nil
    gfx.clear(0)
    audio.stream_reset()
    self.voice = S.is_voice(name) and audio.stream_voice_filter(self.v)
    if self.voice then audio.stream_voice_bypass(not S.voice_filter) end
    self.smoothing = nil
    if S.is_voice(name) and S.lance_smoother then
        self.smoothing = gfx.smoothing()
        gfx.smoothing(math.min(self.smoothing + 1, 3))
    end
    self:decodeAhead()
end

function S:leave()
    if self.smoothing then gfx.smoothing(self.smoothing); self.smoothing = nil end
    audio.stream_reset()
    ui.install()
end

-- Decode frames until enough audio is queued (or the video ends).
function S:decodeAhead()
    while self.decoded + 1 < self.count do
        if self.sound and audio.stream_queued() > PREBUFFER and self.decoded > self.shown then break end
            if not self.sound and self.decoded > self.shown + 1 then break end
        local i = self.decoded + 1
        local info = self.v:decode(i)
        self.bytesBefore[i] = self.queuedBytes
        if info.sound_bytes > 0 and audio.available() and not self.opts.mute then
            -- With an audio device, the sound is the clock. Without one
            -- (no sound card, or it failed to open), fall back to the timer
            -- instead of waiting for audio that will never play.
            if not self.sound then music.stop() end   -- the video brings its own sound
            self.sound = true
            local rate = self.v:sound_rate()
            audio.stream_push(self.v:sound(), rate > 0 and rate or 11111)
            self.queuedBytes = self.queuedBytes + info.sound_bytes
        end
        self.decoded = i
        if i == self.count - 1 and self.sound then audio.stream_end() end
        if info.video then info.pixels = self.v:pixels() end
        self.queue[i] = info
        if not self.sound then break end
    end
end

-- Composite frames shown..target in order (overlay frames build on the
-- previous picture, so none may be skipped).
function S:present(target)
    for i = self.shown + 1, target do
        local info = self.queue[i]
        if info then
            if i == 0 or info.palette_changed then
                self.v:apply_palette()
                -- The videos leave colour 255 black; subtitles are white in it.
                if self.lines then gfx.setcolor(255, 255, 255, 255) end
            end
            if info.video then
                -- v:blit is gfx.blit that knows the frame, so it can show the HD picture.
                self.v:blit(i, info.x, info.y, info.w, info.h, info.pixels, info.overlay and 0 or -1)
            end
            if self.lines and info.subtitle then      -- the frame's cue (RACEPRG 0xD24F)
                if info.subtitle == "next" then
                    self.line = self.line + 1
                    self.showing = self.lines[self.line]
                else
                    self.showing = nil
                end
                self.cleared = true
            end
            if self.opts.on_frame then self.opts.on_frame(i, info) end
            self.queue[i] = nil
        end
    end
    self.shown = target
end

function S:finish(how)
    local cb = self.opts.on_done
    scenes.pop()
    if cb then cb(how) end
end

function S:update(dt)
    if self.holding then
        self.holding = self.holding - dt
        if input.any() or self.holding <= 0 then return self:finish() end
        return
    end
    if input.pressed("Escape") then return self:finish("all") end
    if input.pressed("Return") or input.pressed("Space") then return self:finish() end
    if input.pressed("P") then self.paused = not self.paused end
    if input.pressed("V") and self.voice then
        S.voice_filter = not S.voice_filter
        audio.stream_voice_bypass(not S.voice_filter)
        sys.log("voice filter " .. (S.voice_filter and "on" or "off"))
        if S.on_voice_filter then S.on_voice_filter(S.voice_filter) end
    end
    if self.paused then return end

    local target
    if self.sound then
        local rate = self.v:sound_rate()
        local played = audio.stream_played() * (rate > 0 and rate or 11111)
        target = self.shown
        while target + 1 <= self.decoded and (self.bytesBefore[target + 1] or 0) <= played do
            target = target + 1
        end
    else
        self.clock = self.clock + dt
        target = math.floor(self.clock * SILENT_FPS)
    end
    self:decodeAhead()
    target = math.min(target, self.decoded)
    if target > self.shown then self:present(target) end

    if self.shown >= self.count - 1 and (not self.sound or audio.stream_queued() <= 0) then
        if self.opts.loop then
            self:enter(self.name, self.opts)
        elseif self.opts.hold then
            self.holding = self.opts.hold
        else
            self:finish()
        end
    end
end

function S:draw()
    -- The framebuffer holds the latest composited frame; only the subtitle
    -- line is drawn again on top (the frames cover its rows).
    if self.showing then
        subs.clear()
        subs.draw(self.showing)
    elseif self.cleared then
        subs.clear()
        self.cleared = false
    end
    if self.opts.hud then
        gfx.fill(0, 191, 320, 9, 0)
        gfx.text(2, 193, string.format("%s  %d/%d", self.name, self.shown + 1, self.count), 15)
    end
end

return S
