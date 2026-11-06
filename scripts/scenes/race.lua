-- A race: the game's rules (race/rules.lua) on a track video.
--   UP accelerate   DOWN brake   LEFT/RIGHT steer   SPACE/CTRL fire   ESC quit
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
local FX_FPS = 12.5         -- shot and explosion frames per second (one per video frame)
local SHOT_RANGE = 12       -- sections ahead a shot can reach (this port's value)
local GUN_SPREAD = { { 0 }, { -6, 6 }, { -6, 0, 6 } }   -- bolts per weapon (RACEPRG 0x2EF3)

-- Colours 112..127 belong to the effects: FUMEE and EXPLOSE draw with 112..126
-- and EXPLOSE carries their palette. The overlay text borrows two of those
-- (near-white 126, red-orange 118) and colour 127, which no sprite uses.
local C_TEXT, C_WARN, C_GOOD = 126, 118, 127

function S:enter(name, opts)
    opts = opts or {}
    ui.install()
    self.T = track.open(name)
    self.rules = rules.new(opts.car or "JO")
    self.pos = 0.0
    self.carx, self.view, self.steer = 160, 0, 0
    self.car = cars.open(opts.car or "JO")
    self.rival = cars.open_rival(opts.rival or "RA")
    self.zones = spr.open("ZONES")
    self.fx = spr.open("FUMEE")              -- muzzle flash, bolt, impact
    self.boom = spr.open("EXPLOSE")          -- destroyed rival
    self.fx_t = 0
    -- Rival speeds are this port's placeholders; HARDENED makes them quicker.
    local scale = (opts.level == "HARDENED") and 1.06 or 0.94
    self.rivals = {}
    for k, def in ipairs({ { 6, 0.25, 150 }, { 14, 0.75, 162 }, { 26, 0.5, 172 } }) do
        self.rivals[k] = { ahead = def[1], u = def[2], speed = def[3] * scale, bump = 0, damage = 0 }
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
    for _, r in ipairs(self.rivals) do if not r.out and r.ahead > 0 then p = p + 1 end end
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
        self.entrants = 1
        for _, r in ipairs(self.rivals) do if not r.out then self.entrants = self.entrants + 1 end end
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

    if input.pressed("Space") or input.pressed("Left Ctrl") or input.pressed("Right Ctrl") then
        if rules.fire(s) then voc.play("LASER") end
    end
    self.fx_t = self.fx_t + dt
    while self.fx_t >= 1 / FX_FPS do
        self.fx_t = self.fx_t - 1 / FX_FPS
        self:effects_frame()
    end

    -- Rivals gain or drop back by speed difference; bumping costs speed.
    for _, r in ipairs(self.rivals) do
        r.ahead = r.ahead + rules.rival_relative(TRACK_FPS, s.speed, r.speed) * dt
        r.bump = math.max(0, r.bump - dt)
        if not r.destroyed and r.ahead > 0.5 and r.ahead < 2.5 and r.bump == 0 and r.x and math.abs(r.x - self.carx) < 45 then
            s.speed = math.max(0, s.speed - 40)
            r.ahead = r.ahead + 1.5
            r.bump = 0.6
            voc.play("CHOC4")
        end
    end

    self.spin_t = self.spin_t + dt * (s.speed / 60)
    if self.spin_t > 0.06 then self.spin_t = 0; self.spin = 1 - self.spin end
end

-- Where the shot is this frame: muzzle and centre bolt (nil before the
-- first draw has placed the car).
function S:shot_position()
    if not self.cary then return nil end
    local off, dx, dy = rules.shot_aim(self.view)
    local mx, my = self.carx + off, self.cartop + 6
    return mx, my, mx + dx, my + dy
end

-- One effects frame: the shot moves on and is tested against the rivals,
-- explosions advance (RACEPRG 0x2E14, 0x2F93, 0x4688).
function S:effects_frame()
    local s = self.rules
    if s.shot then
        local result = rules.shot_step(s)
        if result == "kill" and s.shot_target then
            self:destroy(s.shot_target)
        end
        if result then s.shot_target = nil end
    end
    local sh = s.shot
    if sh and not sh.hit then
        local _, _, bx = self:shot_position()
        if bx then
            for _, r in ipairs(self.rivals) do
                if r.box and not r.destroyed and r.ahead <= SHOT_RANGE
                   and bx >= r.box[1] and bx <= r.box[1] + r.box[3] then
                    s.shot_target = r
                    voc.play("CHOCTIR")
                    if rules.hit(s, r) then self:destroy(r) end
                    break
                end
            end
        end
    end
    for _, r in ipairs(self.rivals) do
        if r.boom and not r.out then
            r.boom = r.boom + 1
            if r.box then s.points = s.points + rules.EXPLOSION_POINTS end
            if r.boom >= rules.EXPLOSION_FRAMES then r.out = true end
        end
    end
end

-- A rival at full damage blows up and falls back (RACEPRG 0x3E98: EXPLOD2,
-- its speed set to yours - 20); after the explosion it is out of the race.
function S:destroy(r)
    if r.boom then return end
    r.destroyed, r.damage, r.boom = true, rules.RIVAL_HEALTH, 0
    r.speed = math.max(0, self.rules.speed - 20)
    voc.play("EXPLOD2")
end

-- Damage bar over a hit rival: (100 - damage) / 2 pixels, segmented every
-- 4th pixel, in dashboard colours (RACEPRG 0x0FC3..0x101E).
local function health_bar(cx, y, damage)
    local x, y = math.floor(cx) - 26, math.floor(y)
    gfx.fill(x, y, 52, 6, 74)
    local n = (rules.RIVAL_HEALTH - damage) // 2
    for i = 0, n - 1 do
        local c = (i % 4 == 3) and 74 or (i < 16 and 77 or 76)
        gfx.fill(x + 1 + i, y + 1, 1, 4, c)
    end
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

-- Draws sprite i of a set centred on (x, y).
function S:draw_fx(set, i, x, y, scale)
    local w, h = set:size(i)
    if not w then return end
    scale = scale or 1
    set:draw(i, math.floor(x - w * scale / 2), math.floor(y - h * scale / 2), scale)
end

-- Muzzle flash at the car and the bolts ahead of it, one per gun.
function S:draw_shot()
    local sh = self.rules.shot
    if not sh or not self.fx then return end
    local mx, my, bx, by = self:shot_position()
    if not mx then return end
    if sh.muzzle <= 13 then self:draw_fx(self.fx, sh.muzzle, mx, my) end
    if not sh.hit and sh.frame < rules.SHOT.bolt_end then
        for _, d in ipairs(GUN_SPREAD[sh.guns] or GUN_SPREAD[1]) do
            self:draw_fx(self.fx, sh.frame, bx + d, by)
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
            if self.boom then self.boom:apply_palette() end
        end
        self.frame = f
    end
    gfx.clear(0)
    T.video:draw(0, track.VIDEO_Y)
    if not self.boom then
        gfx.setcolor(C_TEXT, 235, 235, 240)
        gfx.setcolor(C_WARN, 255, 80, 60)
    end
    gfx.setcolor(C_GOOD, 90, 255, 120)

    local cam = T:camera(f)
    local base = f + 1
    local plx, ply, prx, pry = edges(T, cam, base + PLAYER_AHEAD)
    local ref = plx and math.max(1, prx - plx) or 1

    -- Rivals, far to near, hidden behind scenery by the kl masks.
    table.sort(self.rivals, function(a, b) return a.ahead > b.ahead end)
    local sh = s.shot
    for _, r in ipairs(self.rivals) do
        r.x, r.box = nil, nil
        local ahead = math.floor(r.ahead)
        if not r.out and ahead > PLAYER_AHEAD and ahead < track.WINDOW - 2 then
            local lx, ly, rx, ry = edges(T, cam, base + ahead)
            if lx then
                local x, y = lx + (rx - lx) * r.u, ly + (ry - ly) * r.u
                local scale = math.max(0.05, (rx - lx) / ref)
                local sec = T.sections[base + ahead]
                local dh = ((sec.heading - cam.heading + 1440) % 2880) - 1440
                local angle = (dh / 8) * 96 / 180 + (160 - x) / 8 * scale
                local L = T:mask(f, ahead)
                if L then gfx.clip(L.lines, track.VIDEO_Y) end
                local fx_scale = math.max(0.4, math.min(1, scale * 1.5))
                local bl, bt, bw, bh
                if not r.boom or r.boom < 4 then          -- the car vanishes into the blast
                    bl, bt, bw, bh = self.rival:draw(math.floor(angle), x, y, scale)
                end
                if bl then r.box = { bl, bt, bw, bh } else r.box = { x - 20 * scale, y - 30 * scale, 40 * scale, 30 * scale } end
                local cy = r.box[2] + r.box[4] / 2
                if sh and sh.hit and s.shot_target == r and self.fx then
                    self:draw_fx(self.fx, sh.frame, x, cy, fx_scale)
                end
                if r.boom and self.boom then
                    self:draw_fx(self.boom, math.min(r.boom, rules.EXPLOSION_FRAMES - 1), x, cy, fx_scale)
                end
                gfx.unclip()
                if r.damage > 0 and not r.destroyed then health_bar(x, r.box[2] - 8, r.damage) end
                r.x = x
            end
        end
    end

    if plx then
        local margin = math.min(30, (prx - plx) / 4)          -- keep the body on the road
        self.road_l, self.road_r = plx + margin, prx - margin
        self.carx = math.max(self.road_l, math.min(self.road_r, self.carx))
        local y = ply + (pry - ply) * (self.carx - plx) / math.max(1, prx - plx)
        local _, top = self.car:draw(math.floor(self.view + 3 * self.steer), self.carx, y, 1.0,
                                     s.speed > 0 and self.spin or nil)
        self.cary, self.cartop = y, top or (y - 30)
        self:draw_shot()
    end

    self.car:draw_dashboard()
    self:draw_scanner(base)

    -- Readouts (the dashboard gauges themselves aren't drawn yet).
    local lap = T.lap > 0 and math.min(3, f // T.lap + 1) or 1
    gfx.fill(0, 0, 320, 8, 0)
    local left = 0
    for _, r in ipairs(self.rivals) do if not r.out then left = left + 1 end end
    gfx.text(2, 1, string.format("LAP %d/3  POS %d/%d  SPD %3d  EN %3d  WPN %s  PTS %d",
        lap, self:place(), left + 1, s.speed // 10, s.energy,
        s.weapon == 0 and "-" or (s.weapon .. (s.super and "*" or "")), s.points), C_TEXT)

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
        local t1, t2 = "END OF RACE", string.format("YOU FINISHED %d OF %d", self.result, self.entrants)
        local t3 = "PRESS ENTER"
        gfx.fill(90, 64, 140, 38, 0)
        gfx.text(160 - #t1 * 2, 68, t1, C_TEXT)
        gfx.text(160 - #t2 * 2, 80, t2, self.result == 1 and C_GOOD or C_WARN)
        gfx.text(160 - #t3 * 2, 92, t3, C_TEXT)
    end
end

return S
