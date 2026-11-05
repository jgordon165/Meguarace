-- Track viewer: drive along a track video with the game's own cars.
--   UP/DOWN  speed        LEFT/RIGHT  steer        C  change car   R  change rivals
--   E  road edges   T  lane tiles (ZNN)   M  occlusion mask   H  info line
--   PGUP/PGDN  jump 100 sections   ESC back
--
-- Placement follows RACEPRG.EXE: the player's car rides 1 section ahead of
-- the camera, sits on the line between the projected inner road edges
-- (0x1882, 0x1A89) at full size, and picks its image from the view angle
-- (0x1B45). Rival cars are scaled by how wide the road is where they are,
-- angled by their heading relative to the camera, and hidden behind
-- scenery with the frame's kl occlusion masks.

local scenes = require("core.scenes")
local ui     = require("core.ui")
local track  = require("formats.track")
local cars   = require("formats.cars")
local S = {}

local PLAYER_AHEAD = 1    -- sections between the camera and the player's car
local MAXSPEED = 3.0      -- sections per video frame (12.5 fps); >1 skips frames
local STEER = 140         -- screen pixels per second

local C_TEXT, C_EDGE, C_TILE_BONUS, C_TILE_MALUS, C_MASK = 249, 250, 251, 252, 254

function S:enter(name)
    ui.install()
    self.T = track.open(name)
    self.pos = 0.0           -- camera section (== video frame), fractional
    self.speed = 1.0
    self.carx = 160          -- player's car, screen x
    self.view = 0            -- eased view angle (RACEPRG [0xCDE])
    self.steer = 0
    self.spin, self.spin_t = 0, 0
    self.car_i = 1
    self.car = cars.open(cars.CODES[self.car_i])
    -- Three rivals ahead, in different lanes, at slightly different speeds.
    -- All rivals on a track are the same model (they share colours 192..255).
    self:set_rival(2)
    self.rivals = {}
    for k, def in ipairs({ { 8, 0.25, 0.92 }, { 22, 0.75, 1.0 }, { 45, 0.5, 1.08 } }) do
        self.rivals[k] = { pos = def[1], u = def[2], speed = def[3] }
    end
    self.show = { edges = false, tiles = true, mask = false, info = true }
    self.frame = -1
    sys.log(string.format("track %s: %d frames, %d sections, lap %d sections",
        name, self.T.frames, #self.T.sections, self.T.lap))
end

function S:set_rival(i)
    self.rival_i = i
    self.rival = cars.open_rival(cars.CODES[i])
    self.frame = -1                           -- re-apply palettes
end

function S:leave() ui.install() end

function S:update(dt)
    if input.pressed("Escape") then scenes.pop(); return end
    if input.down("Up") then self.speed = math.min(MAXSPEED, self.speed + dt) end
    if input.down("Down") then self.speed = math.max(0, self.speed - dt * 2) end
    self.steer = 0
    if input.down("Left") then self.carx = self.carx - STEER * dt; self.steer = 1 end
    if input.down("Right") then self.carx = self.carx + STEER * dt; self.steer = -1 end
    if input.pressed("C") then
        self.car_i = self.car_i % #cars.CODES + 1
        self.car = cars.open(cars.CODES[self.car_i])
        self.frame = -1                       -- re-apply palettes
    end
    if input.pressed("R") then self:set_rival(self.rival_i % #cars.CODES + 1) end
    if input.pressed("E") then self.show.edges = not self.show.edges end
    if input.pressed("T") then self.show.tiles = not self.show.tiles end
    if input.pressed("M") then self.show.mask = not self.show.mask end
    if input.pressed("H") then self.show.info = not self.show.info end
    if input.pressed("PageDown") then self.pos = self.pos + 100 end
    if input.pressed("PageUp") then self.pos = math.max(0, self.pos - 100) end

    local adv = self.speed * 12.5 * dt
    self.pos = self.pos + adv
    for _, r in ipairs(self.rivals) do r.pos = r.pos + r.speed * 12.5 * dt end

    -- Loop the race video by whole laps (the camera path repeats each lap).
    local lap = self.T.lap > 0 and self.T.lap or (self.T.frames - 1)
    if self.pos > self.T.frames - 1 then
        self.pos = self.pos - lap
        for _, r in ipairs(self.rivals) do r.pos = r.pos - lap end
    end
    -- Keep rivals within sight: drop back behind the camera -> respawn ahead.
    for _, r in ipairs(self.rivals) do
        local ahead = r.pos - self.pos
        if ahead < 2 then r.pos = self.pos + 60 + math.random(0, 30) end
        if ahead > track.WINDOW - 3 then r.pos = self.pos + 5 end
    end

    -- Eased view angle, one step per tick toward (160 - x)/8, plus steering lean.
    local target = (160 - self.carx) / 8
    if self.view < target then self.view = math.min(target, self.view + 1)
    elseif self.view > target then self.view = math.max(target, self.view - 1) end

    self.spin_t = self.spin_t + dt * self.speed
    if self.spin_t > 0.06 then self.spin_t = 0; self.spin = 1 - self.spin end
end

local function colours()
    gfx.setcolor(C_TEXT, 230, 230, 240)
    gfx.setcolor(C_EDGE, 255, 220, 0)
    gfx.setcolor(C_TILE_BONUS, 60, 255, 90)
    gfx.setcolor(C_TILE_MALUS, 255, 60, 60)
    gfx.setcolor(C_MASK, 255, 0, 255)
end

-- Screen x/y of the inner road edges at absolute section si (1-based).
local function edges(T, cam, si)
    local s = T.sections[si]
    if not s then return nil end
    local lx, ly = T:project(cam, table.unpack(s.road[2]))
    local rx, ry = T:project(cam, table.unpack(s.road[3]))
    if not lx or not rx then return nil end
    return lx, ly, rx, ry
end

function S:draw()
    local T, f = self.T, math.floor(self.pos)
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
    colours()

    local cam = T:camera(f)
    local base = f + 1                     -- 1-based index of the camera's section
    local last = math.min(#T.sections, base + track.WINDOW - 1)

    if self.show.edges then
        for p = 1, 4 do
            local px, py
            for si = base + 1, last do
                local x, y = T:project(cam, table.unpack(T.sections[si].road[p]))
                if x and px then gfx.line(math.floor(px), math.floor(py), math.floor(x), math.floor(y), C_EDGE) end
                px, py = x, y
            end
        end
    end

    if self.show.tiles then
        for si = last, base + 1, -1 do       -- far to near
            for lane = 0, track.LANES - 1 do
                local code = T:tile(si, lane)
                if code ~= 0 then
                    local x, y, z = T:project(cam, T:lane_point(T.sections[si], lane))
                    if x then
                        local r = math.max(1, math.floor(600 / z))
                        gfx.fill(math.floor(x - r), math.floor(y - r / 2), 2 * r, r,
                                 (code & 16) ~= 0 and C_TILE_MALUS or C_TILE_BONUS)
                    end
                end
            end
        end
    end

    -- Reference road width where the player's car sits (scale 1.0).
    local plx, ply, prx, pry = edges(T, cam, base + PLAYER_AHEAD)
    local ref = plx and math.max(1, prx - plx) or 1

    -- Rivals, far to near, clipped by the occlusion mask for their distance.
    table.sort(self.rivals, function(a, b) return a.pos > b.pos end)
    for _, r in ipairs(self.rivals) do
        local ahead = math.floor(r.pos - self.pos)
        local si = base + ahead
        local lx, ly, rx, ry = edges(T, cam, si)
        if lx and ahead > PLAYER_AHEAD then
            local x = lx + (rx - lx) * r.u
            local y = ly + (ry - ly) * r.u
            local scale = math.max(0.05, (rx - lx) / ref)
            local dh = ((T.sections[si].heading - cam.heading + 1440) % 2880) - 1440
            local angle = (dh / 8) * 96 / 180 + (160 - x) / 8 * scale
            local L = T:mask(f, ahead)
            if L then gfx.clip(L.lines, track.VIDEO_Y) end
            self.rival:draw(math.floor(angle), x, y, scale)
            gfx.unclip()
            if self.show.mask and L then
                for vr, spans in pairs(L.lines) do
                    for k = 1, #spans - 1, 2 do
                        gfx.pset(spans[k], vr + track.VIDEO_Y, C_MASK)
                        gfx.pset(spans[k + 1] - 1, vr + track.VIDEO_Y, C_MASK)
                    end
                end
            end
        end
    end

    -- Player's car: on the line between the inner edges, at full size.
    if plx then
        self.carx = math.max(plx, math.min(prx, self.carx))
        local y = ply + (pry - ply) * (self.carx - plx) / math.max(1, prx - plx)
        self.car:draw(math.floor(self.view + 3 * self.steer), self.carx, y, 1.0,
                      self.speed > 0 and self.spin or nil)
    end

    self.car:draw_dashboard()

    if self.show.info then
        gfx.fill(0, 0, 320, 8, 0)
        gfx.text(2, 1, string.format("%s %s VS %s  SEC %d/%d  LAP %d  SPD %.1f",
            T.name, self.car.name, self.rival.name, f + 1, #T.sections,
            (T.lap > 0) and (f // T.lap + 1) or 1, self.speed), C_TEXT)
    end
end

return S
