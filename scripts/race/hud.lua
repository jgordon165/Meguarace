-- The race dashboard, as RACEPRG.EXE draws it (0x10A6..0x13C9, 0x150A).
--
-- Every gauge is a strip of frames in the car's T<XX>.HSQ file. Per car the
-- EXE keeps { x, y, first frame, frame count } (8 bytes, indexed by the car
-- table order JO RA OU PA LU OM HO MA), and draws
--   frame = first + value * (count - 1) / max       (0x1283)
-- centred on x, with its top at y + 152 (the dashboard's first line).
-- While tile 9's timer runs ([0x68E7]) every gauge reads 0.
--
-- Pure layout and arithmetic here; race.lua does the drawing.

local H = {}

H.DASH_Y = 152

H.ORDER = { "JO", "RA", "OU", "PA", "LU", "OM", "HO", "MA" }

local function per_car(t)
    local out = {}
    for i, code in ipairs(H.ORDER) do out[code] = t[i] end
    return out
end

-- Gauge tables (DS offset, value, maximum).
H.GAUGES = {
    -- [0xC9A] dashboard speed (speed / 10), max 22 (DS:0x530)
    speed = per_car({ { 155, 17, 11, 13 }, { 120, 15, 12, 12 }, { 160, 15, 9, 6 }, { 55, 27, 10, 9 },
                      { 136, 17, 11, 6 }, { 101, 21, 10, 7 }, { 153, 7, 12, 7 }, { 166, 12, 8, 6 } }),
    -- [0x6911] energy, max 450 (DS:0x570)
    energy = per_car({ { 155, 23, 11, 13 }, { 138, 15, 12, 12 }, { 160, 25, 9, 6 }, { 41, 33, 10, 9 },
                       { 136, 33, 29, 6 }, { 111, 21, 10, 7 }, { 170, 7, 12, 7 }, { 151, 29, 8, 6 } }),
    -- [0xD2E] armour, max = the car's armour (DS:0x4F0)
    armour = per_car({ { 155, 29, 11, 13 }, { 157, 15, 12, 12 }, { 160, 35, 9, 6 }, { 33, 40, 10, 9 },
                       { 184, 33, 35, 6 }, { 121, 21, 10, 7 }, { 187, 7, 12, 7 }, { 180, 29, 8, 6 } }),
    -- [0xD32] THRILL meter, max 128 (DS:0x4B0): the audience's excitement;
    -- rises after bumps and hits, sinks back
    thrill = per_car({ { 262, 3, 2, 9 }, { 37, 3, 2, 10 }, { 21, 1, 2, 7 }, { 286, 11, 2, 8 },
                      { 254, 4, 2, 8 }, { 280, 16, 2, 8 }, { 57, 3, 2, 10 }, { 115, 7, 2, 6 } }),
    -- [0xD5A] weapon 0..3 (DS:0x630)
    weapon = per_car({ { 160, 39, 28, 4 }, { 195, 21, 28, 4 }, { 218, 28, 19, 4 }, { 124, 23, 23, 4 },
                       { 185, 16, 21, 4 }, { 214, 15, 21, 4 }, { 234, 4, 23, 4 }, { 12, 34, 18, 4 } }),
    -- two lamps, lit (frame 1) unless weapon 1 is charged (DS:0x670, 0x6B0)
    lamp_a = per_car({ { 187, 38, 32, 2 }, { 195, 32, 32, 2 }, { 197, 35, 23, 2 }, { 128, 40, 27, 2 },
                       { 208, 6, 25, 2 }, { 223, 31, 25, 2 }, { 236, 12, 27, 2 }, { 32, 35, 22, 2 } }),
    lamp_b = per_car({ { 133, 38, 34, 2 }, { 195, 32, 34, 2 }, { 197, 25, 25, 2 }, { 122, 33, 29, 2 },
                       { 113, 5, 27, 2 }, { 207, 31, 27, 2 }, { 236, 12, 29, 2 }, { 26, 25, 24, 2 } }),
    -- one frame, always shown (value [0x68E1], code 23's flag; DS:0x5B0)
    badge = per_car({ { 259, 16, 24, 1 }, { 40, 19, 24, 1 }, { 52, 20, 15, 1 }, { 163, 19, 19, 1 },
                      { 257, 23, 17, 1 }, { 175, 24, 17, 1 }, { 60, 20, 19, 1 }, { 50, 11, 14, 1 } }),
}

-- Opponents-left digit, small font (DS:0x6F0); not shown on the last track.
H.OPPONENTS_AT = per_car({ { 200, 7 }, { 186, 8 }, { 188, 5 }, { 127, 11 },
                           { 186, 5 }, { 166, 7 }, { 194, 34 }, { 213, 18 } })

-- Tile scanner (DS:0x730): ZONES_ frame 20 with its left edge at x; the
-- icons are centred at x + 9 + 10 * lane, rows 20, 10 and 0 below y.
H.SCANNER_AT = per_car({ { 9, 9 }, { 255, 8 }, { 256, 7 }, { 190, 10 },
                         { 16, 7 }, { 34, 10 }, { 249, 6 }, { 258, 5 } })

-- Rival radar (DS:0x770): { x, y, first frame, width }. Frames: first+2 the
-- screen, first+0/1 the jammed blink, first+3+d the rival blip, d = 0..4
-- sections ahead, 5..9 behind (0x12F7).
H.RADAR = per_car({ { 151, 7, 36, 77 }, { 150, 8, 36, 64 }, { 154, 5, 27, 60 }, { 80, 11, 31, 74 },
                    { 155, 6, 41, 60 }, { 207, 7, 29, 58 }, { 147, 34, 31, 68 }, { 206, 8, 26, 52 } })

H.SPEED_MAX = 22
H.THRILL_MAX = 128

-- Frame for a gauge value (0x1283).
function H.frame(g, value, max)
    value = math.max(0, math.min(max, math.floor(value)))
    return g[3] + (value * (g[4] - 1)) // max
end

-- Radar blip frame offset for a rival `rel` sections ahead of you (negative
-- = behind): 0..4 ahead, 5..9 behind (0x133F).
function H.blip(rel)
    local d = math.floor(rel)
    if d >= 4 then return 4 end
    if d >= 0 then return d end
    return math.min(9, -d + 5)
end

-- Blip centre: the rival's place across the road (u = 0 left .. 1 right)
-- mapped onto the radar width, kept inside the screen (0x132C..0x13A3).
function H.blip_x(radar, u, blip_w)
    local x, w = radar[1], radar[4]
    local cx = x + math.floor((u - 0.5) * w)
    local half = blip_w // 2
    local lo, hi = x - w // 2 + half, x + w // 2 - half
    return math.max(lo, math.min(hi, cx))
end

-- Values the gauges show this frame; all 0 while tile 9 runs.
function H.values(s)
    local out = {
        speed = math.min(H.SPEED_MAX, s.speed // 10), energy = s.energy,
        armour = s.armour, thrill = s.thrill or 0, weapon = s.weapon,
        lamp_a = s.super and 0 or 1, lamp_b = s.super and 0 or 1, badge = 0,
    }
    if s.timers and s.timers[9] then
        for k in pairs(out) do out[k] = 0 end
    end
    return out
end

function H.max_for(name, s)
    if name == "speed" then return H.SPEED_MAX end
    if name == "energy" then return 450 end
    if name == "armour" then return math.max(1, s.car.armour) end
    if name == "thrill" then return H.THRILL_MAX end
    if name == "weapon" then return 3 end
    return 1
end

return H
