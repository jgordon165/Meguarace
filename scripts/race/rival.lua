-- The rival: MegaRace races its opponents one at a time (RACEPRG.EXE).
-- Pure Lua, unit-tested; scenes/race.lua draws it.
--
-- Distance is in eighths of a track section ahead of your car ([0xE0B] =
-- [0xDF2] * 8 + [0xDF6], 0x4405); negative is behind. Speeds are in the
-- player's units (0..180, rivals up to 230).
--
-- Each tick the rival creeps toward or away from you by the speed
-- difference (0x42C6). Every 100 ticks (50 on HARDENED, 0x3481) its AI state
-- picks a speed, a steering direction and whether to fire (0x34B4..0x36F0).
-- Three kinds of rival, by how many opponents are left including it
-- (table at cs:0x36F7, 0x3FD2):
--   "rusher"  (3+ left)  starts at 230 until 21 sections ahead, then keeps
--                        near you; once you pass it, it sits at 160 just
--                        behind and fires when it's within half a section
--   "weaver"  (2 left)   keeps near you; within 5 sections it drops to 160,
--                        steers off the walls and fires from just behind
--   "centre"  (1 left)   the same, but holds the middle of the road

local V = {}

V.START_AHEAD = 5 * 8           -- first rival: 5 sections ahead (0x04F5)
V.NEXT_AHEAD = 28 * 8           -- each next one: 28 sections ahead (0x3FBA)
V.LIMIT = 30 * 8                -- the rival stays within 30 sections (0x43C0)
V.MAX_SPEED = 230               -- [0xDB2]
V.LOST_BEHIND = -6 * 8          -- 6+ sections behind ...
V.LOST_TICKS = 400              -- ... for 400 ticks and it's out (0x3E4D)

-- Opponents to beat per round (DS:0x846); HOLD draws 2..8 at random (0x3FDC).
V.OPPONENTS = { 4, 5, 5, 5, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7 }

function V.kind_for(left)
    if left == 1 then return "centre" end
    if left == 2 then return "weaver" end
    return "rusher"
end

function V.new(left, ahead8, speed)
    local kind = V.kind_for(left)
    local r = {
        kind = kind,
        state = kind == "rusher" and "rush" or "chase",
        pos = ahead8 or V.START_AHEAD,   -- eighths ahead of you
        speed = speed or 0, max = V.MAX_SPEED,
        counter = 0, steer = 0, behind_ticks = 0,
        lean = 0, vel = 0, step_at = 0,  -- steering: [0x691C], [0x6943], [0xE37]
        u = 0.25,                        -- across the road, 0 left .. 1 right (starts at x 80: 0x04B9)
        damage = 0,
    }
    if kind == "rusher" then r.speed = V.MAX_SPEED end   -- 0x34B9
    return r
end

-- Keep-near-you speed (0x36AD): slower than you by 100/50/30/10 as it gets
-- closer ahead; faster by 50 if you're slow (100 or less) or spinning.
function V.chase_speed(r, player_speed, spinning)
    local k = 10
    if r.pos > 8 then k = 30 end
    if r.pos > 30 then k = 50 end
    if r.pos > 60 then k = 100 end
    if player_speed <= 100 or spinning then k = -50 end
    r.max = V.MAX_SPEED
    return player_speed - k
end

-- One AI decision. ctx: { player_speed, spinning, lined_up (bool), x (the
-- rival's screen x at your car), shot_free (bool) }. Returns true if the
-- rival fires now.
function V.decide(r, ctx)
    local fire = false
    local p = r.pos
    local function in_window(lo, hi) return p >= lo and p <= hi and ctx.lined_up and ctx.shot_free end
    if r.state == "rush" then
        if p >= 170 then r.state = "chase" end
    elseif r.state == "chase" then
        r.speed = V.chase_speed(r, ctx.player_speed, ctx.spinning)
        if r.kind == "rusher" then
            if p < 0 then r.state = "behind" end
        elseif p < 40 then
            r.state = "close"
            r.steer = -1
        end
    elseif r.state == "behind" then                 -- rusher, after you've passed it
        r.speed = 160
        if p >= -4 and ctx.lined_up and ctx.shot_free then fire = true end
        if p >= 0 then r.state = "chase" end
    elseif r.state == "close" then                  -- weaver / centre, near you
        r.speed = 160
        if p >= 7 then
            if r.kind == "weaver" then                  -- off the walls (0x3666)
                if r.u < 0.15 then r.steer = 1 elseif r.u > 0.85 then r.steer = -1 end
            else                                        -- the middle (0x367F: x 140..180)
                if ctx.x and ctx.x >= 180 then r.steer = -1 elseif ctx.x and ctx.x <= 140 then r.steer = 1 end
            end
        end
        if in_window(-10, -3) then fire = true end
        if p >= 40 then r.state = "chase" end
    end
    return fire
end

-- Movement per tick (0x42C6): with a speed difference dv it moves `step`
-- eighths every `every` ticks, toward you if it's slower.
function V.step_rate(dv, destroyed)
    if destroyed then return 2, 1 end
    dv = math.abs(dv)
    if dv >= 180 then return 8, 1 end
    if dv >= 60 then return 2, 1 end
    if dv >= 40 then return 1, 2 end
    if dv >= 30 then return 1, 4 end
    if dv >= 20 then return 1, 6 end
    if dv >= 10 then return 1, 10 end
    return 0, 16
end

-- One tick: speed limits, movement, steering. Returns "lost" once it has
-- been left 6+ sections behind for 400 ticks.
function V.tick(r, player_speed)
    r.speed = math.max(1, math.min(r.max, r.speed))
    local step, every = V.step_rate(r.speed - player_speed, r.destroyed)
    r.counter = r.counter + 1
    if r.counter >= every then
        r.counter = 0
        local dir = (r.speed > player_speed) and 1 or ((r.speed < player_speed) and -1 or 0)
        if r.destroyed then dir = (r.speed >= player_speed) and 1 or -1 end
        local p = r.pos + dir * step
        if p > -V.LIMIT and p < V.LIMIT then r.pos = p end
    end
    V.steer_tick(r)
    if r.pos <= V.LOST_BEHIND then
        r.behind_ticks = r.behind_ticks + 1
        if r.behind_ticks >= V.LOST_TICKS then return "lost" end
    else
        r.behind_ticks = 0
    end
    return nil
end

-- Steering (RACEPRG 0x4210, every tick). The AI's direction [0xE03] only
-- counts within 50 eighths. Steering tips a lean [0x691C] (-6..6, against
-- the direction) and builds a sideways speed [0x6943]: each tick it is
-- doubled (halved first when it points the other way), the step added
-- (speed/40, +4 under 80; 4 at 100 and over), capped at +-(speed/10, at
-- most 10) and halved again, so it tops out at 5. Let go, the lean returns
-- toward 0 a tick at a time and the speed by 1 whenever the lean is even.
local function toward_zero(v)
    if v == 0 then return 0 end
    v = v - 1
    if v < 0 then v = v + 2 end
    return v
end

function V.steer_tick(r)
    local dir = r.pos < 50 and r.steer or 0
    if dir == 0 then
        r.lean = toward_zero(r.lean)
        if r.lean % 2 == 0 then r.vel = toward_zero(r.vel) end
        return
    end
    r.lean = math.max(-6, math.min(6, r.lean + (dir < 0 and 1 or -1)))
    local s10 = math.floor(r.speed) // 10                                  -- [0xDB4]
    local cap, step = s10, s10 // 4
    if step < 2 then step = step + 4 end
    if s10 >= 10 then cap, step = 10, 4 end
    local v = r.vel
    if (v < 0) ~= (dir < 0) then v = v // 2 end                -- sar
    v = 2 * v + (dir < 0 and -step or step)
    v = math.max(-cap, math.min(cap, v))
    r.vel = v // 2                                              -- sar
end

-- The sideways move (0x38EF), once every 6 ticks while the road at the
-- rival is 32 pixels wide or more ([0xDEA]). Returns pixels at the rival's
-- depth. Within 50 eighths: its sideways speed, unless the lean says the
-- other way. Further: a pixel toward the middle of the road (`off`: its
-- pixels from the middle). From 150 on it is simply kept in the middle
-- (0x3A18), every tick.
function V.side_step(r, tick, width, off)
    if tick <= r.step_at then return 0 end
    r.step_at = tick + 5
    if width < 32 then return 0 end
    if r.pos >= 50 then
        local d = (-math.floor(off)) & ~1                                   -- and 0xFFFE
        if d == 0 then return 0 end
        return d > 0 and 1 or -1
    end
    local v = r.vel
    if r.lean > 0 and v >= 0 then v = 0 end
    if r.lean < 0 and v < 0 then v = 0 end
    return v
end

function V.decision_interval(level) return level == "HARDENED" and 50 or 100 end

-- End of a race (RACEPRG 0x4F4B..0x5064): "won" (every opponent
-- destroyed), "wrecked", or "laps" (time ran out). After the laps you still
-- qualify with at most one opponent left and 1200 race points.
V.QUALIFY_POINTS = 1200
function V.qualifies(outcome, left, points)
    if outcome == "won" then return true end
    if outcome == "laps" then return left <= 1 and points >= V.QUALIFY_POINTS end
    return false
end

return V
