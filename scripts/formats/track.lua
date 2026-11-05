-- MegaRace track data, read from the player's MEGARACE.DAT.
-- Formats are documented in docs/formats.md ("Track data").
--
--   local T = track.open("NS11")      -- NS11.HNM video + NS11.ZNN lane grid
--   T.sections[i]                     -- section under the camera at frame i
--   T:project(cam, x, y, z)           -- world point -> screen (200-line screen)
--   T:mask(frame, section_distance)   -- occlusion spans for a car that far ahead
--   T:tile(section, lane)             -- ZNN lane code (0 = nothing)

local M = {}
M.__index = M

M.VIDEO_Y = 24          -- the 152-line video sits centred on the 200-line screen
M.LANES   = 5
M.WINDOW  = 100         -- sections the game keeps ahead of the camera

-- pt record: 29 signed 16-bit values (58 bytes).
local PT = "<" .. string.rep("i2", 29)

local function section(rec, off)
    local v = { string.unpack(PT, rec, off) }
    return {
        x = v[1], y = v[2], z = v[3],                  -- camera position
        m = { v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11] },  -- rotation (16384 = 1.0)
        depth_bias = v[12],                            -- added to depth as /8
        roll_sin = v[13], roll_cos = v[14],            -- view roll (4096 = 1.0)
        road = {                                       -- cross-section, outer-left .. outer-right
            { v[15], v[16], v[17] }, { v[18], v[19], v[20] },
            { v[21], v[22], v[23] }, { v[24], v[25], v[26] },
        },
        roll = v[27],       -- 1/50 degree, used to tilt sprites
        heading = v[28],    -- 2880 per turn
        pitch = v[29],      -- 1/8 degree
    }
end

function M.open(name)
    local video = assert(hnm.open(name .. ".HNM"))
    local T = setmetatable({ name = name, video = video, sections = {}, frames = video:frames() }, M)

    -- Frame 0 carries the first 100 sections; every later frame appends one.
    for i = 0, T.frames - 1 do
        local c = video:chunks(i)
        local pt = c and c.pt
        if pt then
            for off = 1, #pt - 57, 58 do T.sections[#T.sections + 1] = section(pt, off) end
        end
    end

    -- ZNN: one row of 5 lane codes per section of a lap.
    local znn = dat.read(name .. ".ZNN", false)
    T.lanes = znn or ""
    T.lap = znn and (#znn // M.LANES) or #T.sections
    return T
end

-- Section under the camera when frame `i` (0-based) is on screen.
function M:camera(i) return self.sections[i + 1] end

-- The game's own transform (RACEPRG.EXE 0x6D58-0x6FD2). Returns screen x, y
-- on the 320x200 screen and the depth, or nil if behind the camera.
function M:project(c, px, py, pz)
    local dx, dy, dz = px - c.x, py - c.y, pz - c.z
    local m = c.m
    local X = (-dx * m[1] + dy * m[2]) / 16384
    local Y = (-dx * m[3] - dy * m[4] + dz * m[5]) / 16384
    local Z = (-dx * m[6] - dy * m[7] - dz * m[8]) / 16384 + c.depth_bias / 8
    if Z <= 1 then return nil end
    local Xr = (c.roll_cos * X - c.roll_sin * Y) / 4096
    local Yr = (c.roll_sin * X + c.roll_cos * Y) / 4096
    return 160 + Xr * 2.3 * 88 / Z, (131 - Yr * 230 / Z) * 200 / 262, Z
end

-- World point across the road at section s: u = 0 (left inner edge) .. 1
-- (right inner edge). Lanes are the 5 equal strips between the inner edges.
function M:across(s, u)
    local a, b = s.road[2], s.road[3]
    return a[1] + (b[1] - a[1]) * u, a[2] + (b[2] - a[2]) * u, a[3] + (b[3] - a[3]) * u
end

function M:lane_point(s, lane)    -- lane 0..4, centre of the strip
    return self:across(s, (lane + 0.5) / M.LANES)
end

-- ZNN code at absolute section index (1-based) and lane 0..4.
function M:tile(si, lane)
    if self.lap == 0 or #self.lanes == 0 then return 0 end
    local row = (si - 1) % self.lap
    return self.lanes:byte(row * M.LANES + lane + 1) or 0
end

-- kl occlusion masks -----------------------------------------------------

local function varint(d, p)
    local b = d:byte(p)
    if b == 0 then return d:byte(p + 1) + 256, p + 2 end
    return b, p + 1
end

local BOUNDS = { [1] = 2, [2] = 4, [3] = 6 }

-- Decodes a kl chunk into layers { section, lines = { [row] = {x0,x1, x0,x1...} } }.
-- Spans are where a car at or beyond `section` may be drawn; gaps are scenery
-- in front of it. Rows are video rows (0..151).
function M.decode_kl(kl)
    local layers = {}
    if not kl or #kl == 0 then return layers end
    local n = kl:byte(1)
    for i = 0, n - 1 do
        local sec, _, r1, start, off = string.unpack("<BBBI2I2", kl, 2 + 8 * i)
        local p = off + 1
        local row = start // 320
        local lines = {}
        while row <= r1 and p <= #kl do
            local op = kl:byte(p); p = p + 1
            local cnt = (op >> 4); if cnt == 0 then cnt = 16 end
            if op & 3 == 0 then
                row = row + cnt
            else
                local K = BOUNDS[op & 3]
                local left, right = (op >> 2) & 1 == 1, (op >> 3) & 1 == 1
                local explicit = K - (left and 0 or 1) - (right and 0 or 1)
                for _ = 1, cnt do
                    local xs, x = {}, 0
                    if not left then xs[1] = 0 end
                    for _ = 1, explicit do
                        local d; d, p = varint(kl, p); x = x + d; xs[#xs + 1] = x
                    end
                    if not right then xs[#xs + 1] = 320 end
                    lines[row] = xs
                    row = row + 1
                end
            end
        end
        layers[#layers + 1] = { section = sec, lines = lines }
    end
    return layers
end

-- Occlusion layer for frame i and a car `ahead` sections in front of the
-- camera, or nil when nothing hides it (same rule as RACEPRG 0xB520).
function M:mask(i, ahead)
    local c = self.video:chunks(i)
    local layers = M.decode_kl(c and c.kl)
    local pick
    for _, L in ipairs(layers) do
        if ahead >= L.section then pick = L else break end
    end
    return pick
end

return M
