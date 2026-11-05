-- Track viewer: drive along a track video using its decoded data.
--   UP/DOWN  speed      LEFT/RIGHT  change lane   A/Z  marker distance
--   E  road edges   T  lane tiles (ZNN)   M  occlusion mask for the marker
--   PGUP/PGDN  jump 100 sections   ESC back
-- The marker is a stand-in for the player's car until sprites are decoded.

local scenes = require("core.scenes")
local ui     = require("core.ui")
local track  = require("formats.track")
local S = {}

local VY = track.VIDEO_Y
local AHEAD = 12          -- the marker rides this many sections in front of the camera
local MAXSPEED = 3.0      -- sections per frame at 12.5 fps; >1 skips frames

local C_TEXT, C_EDGE, C_TILE_BONUS, C_TILE_MALUS, C_CAR, C_MASK = 249, 250, 251, 252, 253, 254

function S:enter(name)
    ui.install()
    self.T = track.open(name)
    self.pos = 0.0          -- in sections (== frames)
    self.speed = 1.0
    self.lane = 2
    self.ahead = AHEAD
    self.show = { edges = true, tiles = true, mask = false }
    self.frame = -1
    sys.log(string.format("track %s: %d frames, %d sections, lap %d sections",
        name, self.T.frames, #self.T.sections, self.T.lap))
end

function S:leave() ui.install() end

function S:update(dt)
    if input.pressed("Escape") then scenes.pop(); return end
    if input.down("Up") then self.speed = math.min(MAXSPEED, self.speed + dt) end
    if input.down("Down") then self.speed = math.max(0, self.speed - dt * 2) end
    if input.pressed("Left") then self.lane = math.max(0, self.lane - 1) end
    if input.pressed("Right") then self.lane = math.min(track.LANES - 1, self.lane + 1) end
    if ui.repeating("A", dt) then self.ahead = math.min(track.WINDOW - 2, self.ahead + 1) end
    if ui.repeating("Z", dt) then self.ahead = math.max(1, self.ahead - 1) end
    if input.pressed("E") then self.show.edges = not self.show.edges end
    if input.pressed("T") then self.show.tiles = not self.show.tiles end
    if input.pressed("M") then self.show.mask = not self.show.mask end
    if input.pressed("PageDown") then self.pos = self.pos + 100 end
    if input.pressed("PageUp") then self.pos = math.max(0, self.pos - 100) end

    self.pos = self.pos + self.speed * 12.5 * dt
    local last = self.T.frames - 1
    if self.pos > last then self.pos = self.pos - (self.T.lap > 0 and self.T.lap or last) end
end

local function colours()
    gfx.setcolor(C_TEXT, 230, 230, 240)
    gfx.setcolor(C_EDGE, 255, 220, 0)
    gfx.setcolor(C_TILE_BONUS, 60, 255, 90)
    gfx.setcolor(C_TILE_MALUS, 255, 60, 60)
    gfx.setcolor(C_CAR, 0, 200, 255)
    gfx.setcolor(C_MASK, 255, 0, 255)
end

function S:draw()
    local T, f = self.T, math.floor(self.pos)
    if f ~= self.frame then
        local info = T.video:decode(f)
        if f == 0 or self.frame < 0 or info.palette_changed then T.video:apply_palette() end
        self.frame = f
    end
    gfx.clear(0)
    T.video:draw(0, VY)
    colours()

    local cam = T:camera(f)
    local base = f + 1                     -- 1-based index of the camera's section
    local last = math.min(#T.sections, base + track.WINDOW - 1)

    if self.show.edges then
        for p = 1, 4 do
            local px, py
            for si = base + 1, last do
                local r = T.sections[si].road[p]
                local x, y = T:project(cam, r[1], r[2], r[3])
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

    -- Stand-in car, clipped by the occlusion mask exactly as the game clips sprites.
    local si = base + self.ahead
    if T.sections[si] then
        local x, y, z = T:project(cam, T:lane_point(T.sections[si], self.lane))
        if x then
            local w, h = math.floor(4000 / z), math.floor(2000 / z)
            local L = T:mask(f, self.ahead)
            for row = math.floor(y - h), math.floor(y) do
                local vr = row - VY
                local spans = L and L.lines[vr]
                local x0, x1 = math.floor(x - w / 2), math.floor(x + w / 2)
                if not L then
                    gfx.fill(x0, row, x1 - x0, 1, C_CAR)
                elseif spans then
                    for k = 1, #spans - 1, 2 do
                        local a, b = math.max(x0, spans[k]), math.min(x1, spans[k + 1])
                        if b > a then gfx.fill(a, row, b - a, 1, C_CAR) end
                    end
                end
            end
            if self.show.mask and L then
                for vr, spans in pairs(L.lines) do
                    for k = 1, #spans - 1, 2 do gfx.pset(spans[k], vr + VY, C_MASK); gfx.pset(spans[k + 1] - 1, vr + VY, C_MASK) end
                end
            end
        end
    end

    gfx.fill(0, 0, 320, 9, 0)
    gfx.text(2, 2, string.format("%s  SEC %d/%d  LAP %d  SPD %.1f  LANE %d  AHEAD %d",
        T.name, f + 1, #T.sections, (T.lap > 0) and (f // T.lap + 1) or 1, self.speed, self.lane, self.ahead), C_TEXT)
    gfx.fill(0, 191, 320, 9, 0)
    gfx.text(2, 193, "ARROWS DRIVE  A/Z DIST  E T M OVERLAYS  ESC", C_TEXT)
end

return S
