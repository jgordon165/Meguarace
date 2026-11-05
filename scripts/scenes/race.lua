-- A race: the game's rules (race/rules.lua) on a track video.
--   UP accelerate   DOWN brake   LEFT/RIGHT steer   ESC quit
--
-- From RACEPRG.EXE: the video advances at a fixed rate whenever you're
-- moving; your speed decides how you gain on the rivals. The lane is the
-- screen band under your car, and the tile in it takes effect as you reach
-- each section. The race is the length of the race video (three laps).

local scenes = require("core.scenes")
local ui     = require("core.ui")
local track  = require("formats.track")
local cars   = require("formats.cars")
local voc    = require("formats.voc")
local rules  = require("race.rules")
local S = {}

local TRACK_FPS = 12.5      -- sections per second while moving (the videos' rate)
local PLAYER_AHEAD = 1      -- sections between the camera and your car
local STEER = 140           -- screen pixels per second
local COUNTDOWN = 2.5       -- seconds of "GET READY"

-- Overlay colours live in 112..127, which nothing in a race uses (video 1-64,
-- dashboard 72-111, cars 128-255).
local C_TEXT, C_WARN, C_GOOD = 120, 121, 122

function S:enter(name, opts)
    opts = opts or {}
    ui.install()
    self.T = track.open(name)
    self.rules = rules.new()
    self.pos = 0.0
    self.carx, self.view, self.steer = 160, 0, 0
    self.car = cars.open(opts.car or "JO")
    self.rival = cars.open_rival(opts.rival or "RA")
    self.zones = spr.open("ZONES")
    -- Rival speeds are this port's placeholders; HARDENED makes them quicker.
    local scale = (opts.level == "HARDENED") and 1.06 or 0.94
    self.rivals = {}
    for k, def in ipairs({ { 6, 0.25, 150 }, { 14, 0.75, 162 }, { 26, 0.5, 172 } }) do
        self.rivals[k] = { ahead = def[1], u = def[2], speed = def[3] * scale, bump = 0 }
    end
    self.title = opts.title
    self.on_done = opts.on_done
    self.ticks = 0
    self.clock = -COUNTDOWN
    self.last_section = nil
    self.finished = false
    self.spin, self.spin_t = 0, 0
    self.frame = -1
end

function S:leave() ui.install(); audio.stop_all() end

function S:place()
    local p = 1
    for _, r in ipairs(self.rivals) do if r.ahead > 0 then p = p + 1 end end
    return p
end

function S:update(dt)
    if input.pressed("Escape") then scenes.pop(); return end
    self.clock = self.clock + dt
    if self.finished then
        if input.pressed("Return") or input.pressed("Space") then
            local cb, result = self.on_done, { place = self.result, points = self.rules.points }
            scenes.pop()
            if cb then cb(result) end
        end
        return
    end
    if self.clock < 0 then return end                     -- countdown

    local s = self.rules
    self.steer = 0
    if input.down("Left") then self.carx = self.carx - STEER * dt; self.steer = 1 end
    if input.down("Right") then self.carx = self.carx + STEER * dt; self.steer = -1 end

    -- Game ticks.
    self.ticks = self.ticks + dt * rules.TICKS_PER_SECOND
    while self.ticks >= 1 do
        self.ticks = self.ticks - 1
        local _, drift = rules.step(s, { accel = input.down("Up"), brake = input.down("Down") })
        self.carx = self.carx + drift
        -- Eased view angle toward (160 - x)/8 (RACEPRG 0x24A5).
        local target = (160 - self.carx) / 8
        local stepv = s.speed // 40 + 1
        if self.view < target then self.view = math.min(target, self.view + stepv)
        elseif self.view > target then self.view = math.max(target, self.view - stepv) end
    end
    -- Stay on the road: between the inner edges where the car is (set in draw).
    if self.road_l then self.carx = math.max(self.road_l, math.min(self.road_r, self.carx)) end

    -- Scenery: fixed rate while moving.
    local moving = s.speed > 0
    if moving then self.pos = self.pos + TRACK_FPS * dt end
    if self.pos >= self.T.frames - 1 then
        self.pos = self.T.frames - 1
        self.finished = true
        self.result = self:place()
        voc.play(self.result == 1 and "BONUS11" or "MALUS2")
        return
    end

    -- Tile under the car, once per section.
    local si = math.floor(self.pos) + 1 + PLAYER_AHEAD
    if si ~= self.last_section then
        self.last_section = si
        local lane = rules.lane_for_x(self.carx)
        local code = self.T:tile(si, lane)
        if code ~= 0 then
            local sound = rules.apply_tile(s, code, self.steer)
            if sound then voc.play(sound) end
        end
    end

    -- Rivals gain or drop back by speed difference; bumping costs speed.
    for _, r in ipairs(self.rivals) do
        r.ahead = r.ahead + rules.rival_relative(TRACK_FPS, s.speed, r.speed) * dt
        r.bump = math.max(0, r.bump - dt)
        if r.ahead > 0.5 and r.ahead < 2.5 and r.bump == 0 and r.x and math.abs(r.x - self.carx) < 45 then
            s.speed = math.max(0, s.speed - 40)
            r.ahead = r.ahead + 1.5
            r.bump = 0.6
            voc.play("CHOC4")
        end
    end

    self.spin_t = self.spin_t + dt * (s.speed / 60)
    if self.spin_t > 0.06 then self.spin_t = 0; self.spin = 1 - self.spin end
end

local function edges(T, cam, si)
    local sec = T.sections[si]
    if not sec then return nil end
    local lx, ly = T:project(cam, table.unpack(sec.road[2]))
    local rx, ry = T:project(cam, table.unpack(sec.road[3]))
    if not lx or not rx then return nil end
    return lx, ly, rx, ry
end

-- Tile scanner on the dashboard: the next three rows ahead, five lanes,
-- drawn with the game's ZONES icons (RACEPRG 0x10C7..0x115D).
function S:draw_scanner(base)
    if not self.zones then return end
    for row = 0, 2 do
        local si = base + PLAYER_AHEAD + 1 + row * 2
        for lane = 0, 4 do
            local code = self.T:tile(si, lane)
            if code ~= 0 then
                local icon = (code >= 17) and (math.min(code, 25) - 7) or (code - 1)
                self.zones:draw(icon, 14 + lane * 10, 182 - row * 10)
            end
        end
    end
end

function S:draw()
    local T, s = self.T, self.rules
    local f = math.floor(self.pos)
    if f ~= self.frame then
        local info = T.video:decode(f)
        if self.frame < 0 or f == 0 or info.palette_changed then
            T.video:apply_palette()
            self.car:apply_palette()
            self.rival:apply_palette()
        end
        self.frame = f
    end
    gfx.clear(0)
    T.video:draw(0, track.VIDEO_Y)
    gfx.setcolor(C_TEXT, 235, 235, 240)
    gfx.setcolor(C_WARN, 255, 80, 60)
    gfx.setcolor(C_GOOD, 90, 255, 120)

    local cam = T:camera(f)
    local base = f + 1
    local plx, ply, prx, pry = edges(T, cam, base + PLAYER_AHEAD)
    local ref = plx and math.max(1, prx - plx) or 1

    -- Rivals, far to near, hidden behind scenery by the kl masks.
    table.sort(self.rivals, function(a, b) return a.ahead > b.ahead end)
    for _, r in ipairs(self.rivals) do
        r.x = nil
        local ahead = math.floor(r.ahead)
        if ahead > PLAYER_AHEAD and ahead < track.WINDOW - 2 then
            local lx, ly, rx, ry = edges(T, cam, base + ahead)
            if lx then
                local x, y = lx + (rx - lx) * r.u, ly + (ry - ly) * r.u
                local scale = math.max(0.05, (rx - lx) / ref)
                local sec = T.sections[base + ahead]
                local dh = ((sec.heading - cam.heading + 1440) % 2880) - 1440
                local angle = (dh / 8) * 96 / 180 + (160 - x) / 8 * scale
                local L = T:mask(f, ahead)
                if L then gfx.clip(L.lines, track.VIDEO_Y) end
                self.rival:draw(math.floor(angle), x, y, scale)
                gfx.unclip()
                r.x = x
            end
        end
    end

    if plx then
        local margin = math.min(30, (prx - plx) / 4)          -- keep the body on the road
        self.road_l, self.road_r = plx + margin, prx - margin
        self.carx = math.max(self.road_l, math.min(self.road_r, self.carx))
        local y = ply + (pry - ply) * (self.carx - plx) / math.max(1, prx - plx)
        self.car:draw(math.floor(self.view + 3 * self.steer), self.carx, y, 1.0,
                      s.speed > 0 and self.spin or nil)
    end

    self.car:draw_dashboard()
    self:draw_scanner(base)

    -- Readouts (the dashboard gauges themselves aren't drawn yet).
    local lap = T.lap > 0 and math.min(3, f // T.lap + 1) or 1
    gfx.fill(0, 0, 320, 8, 0)
    gfx.text(2, 1, string.format("LAP %d/3   POS %d/%d   SPD %3d   EN %3d   PTS %d",
        lap, self:place(), #self.rivals + 1, s.speed // 10, s.energy, s.points), C_TEXT)

    if s.message and s.tick < s.message_until then
        local w = #s.message * 4
        gfx.fill(160 - w // 2 - 3, 20, w + 6, 9, 0)
        gfx.text(160 - w // 2, 22, s.message, s.message:find("+", 1, true) and C_GOOD or C_TEXT)
    end

    if self.clock < 0 then
        local t = "GET READY"
        gfx.fill(100, 70, 120, 13 + (self.title and 10 or 0), 0)
        gfx.text(160 - #t * 2, 74, t, C_TEXT)
        if self.title then gfx.text(160 - #self.title * 2, 84, self.title, C_GOOD) end
    elseif self.clock < 1 then
        gfx.text(156, 74, "GO", C_GOOD)
    end
    if self.finished then
        local t1, t2 = "END OF RACE", string.format("YOU FINISHED %d OF %d", self.result, #self.rivals + 1)
        local t3 = "PRESS ENTER"
        gfx.fill(90, 64, 140, 38, 0)
        gfx.text(160 - #t1 * 2, 68, t1, C_TEXT)
        gfx.text(160 - #t2 * 2, 80, t2, self.result == 1 and C_GOOD or C_WARN)
        gfx.text(160 - #t3 * 2, 92, t3, C_TEXT)
    end
end

return S
