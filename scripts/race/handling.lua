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

-- Your steering (RACEPRG 0x2833..0x2947, every tick). dir: -1 left, 1
-- right, 0 none. st = { lean, vel, ticks }: the lean [0x691B] (-12..12,
-- against the direction; it turns the car's picture), the sideways speed
-- [0x6941] (pixels a tick) and the steering ticks [0x6939].
-- Steering doubles the speed (halved first if it points the other way)
-- and adds a step of speed/80 (at least 1), capped at speed/20 (at most 9;
-- halved at 180 and over when the curve push is against it). Let go, the
-- lean and the speed go back toward 0 one a tick; on a car whose handling
-- is over 5 the speed only eases when the lean is even. Nothing changes
-- standing still or in a slide ([0x6933]).
local function toward_zero(v)
    if v == 0 then return 0 end
    v = v - 1
    if v < 0 then v = v + 2 end
    return v
end
H.toward_zero = toward_zero

function H.steer(st, dir, speed, handling, push)
    local s10 = speed // 10
    if s10 == 0 then return end
    if dir == 0 then
        st.lean = toward_zero(st.lean)
        if handling > 5 and st.lean % 2 == 1 then return end
        st.ticks = 0
        st.vel = toward_zero(st.vel)
        return
    end
    st.lean = math.max(-12, math.min(12, st.lean + (dir < 0 and 1 or -1)))
    local cap = math.min(s10 // 2, 9)
    local step = math.max(s10 // 8, 1)
    local v = st.vel
    if (v < 0) ~= (dir < 0) then
        if v == -1 then v = 0 end
        v = v // 2
    end
    v = 2 * v
    if push ~= 0 and (push < 0) ~= (v < 0) and speed >= 180 then cap = cap // 2 end
    v = math.max(-cap, math.min(cap, v + dir * step))
    st.vel = v
    st.ticks = st.ticks + 1
end

-- The body lean [0x691D] (0x1A17): a curve push of 6 or more tips it 2
-- (against the push's side, -6..6), then it eases back 1. The car's
-- picture is the view angle less both leans (0x1B84).
function H.body_lean(b, p)
    if p >= 6 then b = b + 2 end
    if p <= -6 then b = b - 2 end
    b = math.max(-6, math.min(6, b))
    return toward_zero(b)
end

-- Attract mode's steering: -1 left, 1 right, 0 none.
function H.demo_steer(curv, x)
    if curv > 11 or curv < -11 then return 0 end
    if x <= 130 then return 1 end
    if x >= 190 then return -1 end
    return 0
end

return H
