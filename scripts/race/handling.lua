-- Curves push the car outward; each car's handling caps the push
-- (RACEPRG 0x1838..0x1A40, 0x2290..0x236A). Pure Lua, tested.
--
-- Per video frame the game takes the heading change from the section under
-- the camera to the next one ([0x6B58], 0x6DCC: pt word 27, 2880 a turn,
-- wrapped), halves it, and averages the last four into the curvature
-- [0xCFC]. Some stretches have none: NGL's first 420 frames, all of FC3,
-- TOKY and HOLD (DS:0x2D6 per round, 0x6E6A).
--
-- The push [0x691F] (0x19CB): none within +-5; otherwise curvature x
-- (speed / 10) / 50, none while you steer at 150 or less, capped at the
-- car's handling [0x80C] (2..6: lower holds the road better). A heading
-- that rises bends the road left on screen, so the push is to the right.
--
-- Each tick (0x2290) the push moves you when you aren't steering, and once
-- more at 160 and over on a sharp curve (+-11 and beyond). Near the edge it
-- pushes toward, it eases: 1 pixel within 60 of it, none within 30
-- (0x232F). No push while you skid or scrape a wall.
--
-- The same code steers attract mode's car ([0x6808], 0x22FD): on the
-- straight (curvature within +-11) toward x 130..190; on curves not at all.

local H = {}

H.FULL_TURN = 2880
H.EXEMPT = {                      -- round -> frames with no curvature (DS:0x2D6)
    [4] = { 0, 420 }, [12] = { 0, 2000 }, [15] = { 0, 2000 }, [16] = { 0, 2000 },
}

function H.heading_delta(cur, nxt)
    local a, b = nxt, cur
    if a > 2160 and b <= 720 then a = a - H.FULL_TURN end
    if b > 2160 and a <= 720 then b = b - H.FULL_TURN end
    return a - b
end

-- Feeds one frame's heading change into the 4-sample history; returns the
-- curvature. (The game's sar rounds toward minus infinity, as // does.)
function H.curvature(hist, delta)
    table.insert(hist, 1, delta // 2)
    hist[5] = nil
    local sum = 0
    for i = 1, #hist do sum = sum + hist[i] end
    return sum // 4
end

-- Curvature input for frame f (camera on section f + 1) of round `round`.
function H.frame_delta(T, f, round)
    local ex = H.EXEMPT[round]
    if ex and f >= ex[1] and f <= ex[2] then return 0 end
    local a, b = T.sections[f + 1], T.sections[f + 2]
    if not a or not b then return 0 end
    return H.heading_delta(a.heading, b.heading)
end

local function idiv(a, b)                      -- x86 idiv: toward zero
    local q = a // b
    if q < 0 and q * b ~= a then q = q + 1 end
    return q
end

function H.push(curv, speed, steering, handling)
    if curv >= -5 and curv <= 5 then return 0 end
    local p = idiv(curv * (speed // 10), 50)
    if steering and speed <= 150 then p = 0 end
    return math.max(-handling, math.min(handling, p))
end

-- The push near the screen edges (0x232F).
function H.ease_at_edge(p, x)
    if p < 0 and x <= 60 then return x <= 30 and 0 or -1 end
    if p > 0 and x >= 260 then return x >= 290 and 0 or 1 end
    return p
end

-- Pixels to move this tick.
function H.tick_dx(p, x, curv, speed, steering)
    p = H.ease_at_edge(p, x)
    local dx = 0
    if not steering then dx = dx + p end
    if speed >= 160 and (curv >= 11 or curv <= -11) then dx = dx + p end
    return dx
end

-- Attract mode's steering: -1 left, 1 right, 0 none.
function H.demo_steer(curv, x)
    if curv > 11 or curv < -11 then return 0 end
    if x <= 130 then return 1 end
    if x >= 190 then return -1 end
    return 0
end

return H
