-- Plays an HNM video from MEGARACE.DAT. Videos with a soundtrack are
-- clocked by the audio (the picture follows the sound, as on the original
-- CD player); silent ones (the track videos) run at the original 12.5 fps.
--
--   scenes.push(require("scenes.player"), "INTRO.HNM", { on_done = fn, loop = false })

local scenes = require("core.scenes")
local ui     = require("core.ui")
local S = {}

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
    gfx.clear(0)
    audio.stream_reset()
    self:decodeAhead()
end

function S:leave()
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
        if info.sound_bytes > 0 then
            self.sound = true
            local rate = self.v:sound_rate()
            audio.stream_push(self.v:sound(), rate > 0 and rate or 11111)
            self.queuedBytes = self.queuedBytes + info.sound_bytes
        end
        self.decoded = i
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
            if i == 0 or info.palette_changed then self.v:apply_palette() end
            if info.video then
                gfx.blit(info.x, info.y, info.w, info.h, info.pixels, info.overlay and 0 or -1)
            end
            if self.opts.on_frame then self.opts.on_frame(i, info) end
            self.queue[i] = nil
        end
    end
    self.shown = target
end

function S:finish()
    local cb = self.opts.on_done
    scenes.pop()
    if cb then cb() end
end

function S:update(dt)
    if input.pressed("Escape") or input.pressed("Return") or input.pressed("Space") then
        return self:finish()
    end
    if input.pressed("P") then self.paused = not self.paused end
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
        else
            self:finish()
        end
    end
end

function S:draw()
    -- The framebuffer holds the latest composited frame; nothing to redraw.
    if self.opts.hud then
        gfx.fill(0, 191, 320, 9, 0)
        gfx.text(2, 193, string.format("%s  %d/%d", self.name, self.shown + 1, self.count), 15)
    end
end

return S
