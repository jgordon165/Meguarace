-- Collisions with the rival (RACEPRG 0x3A8F..0x3CF9). Pure Lua, tested.
--
-- Every tick: the rival is in reach when it's 0..7 eighths of a section
-- ahead of you or 1..4 behind, and its screen span overlaps yours. If it
-- moved this tick it's a rear-end collision ("ahead": you ran into it;
-- "behind": it ran into you) and it goes back to where it was; otherwise
-- it's alongside ("left" or "right": which side of you it's on).
--
--   ahead   your speed -20 and forced braking (down to 130), its speed +40,
--           a boost ends; THRILL 10. It skids. On HOLD both cars are done.
--   behind  your speed +30, its speed -20; THRILL 30. You skid, and lose 8
--           armour when the skid ends (0x2136).
--   side    you're pushed 7 pixels away and it 7 the other way, both easing
--           off by 1 a tick; THRILL 50. It skids.
-- A skid (0x3C72) is a short sway script with a 2-pixel-a-tick slide away
-- from the other car: yours DS:0x896 (or 0x888), the rival's 0x8A6/0x89E.
--
-- The walls (0x23B2, 0x21F8, 0x301B). Your car is held between the road's
-- edges; pinned at one, you're scraping it ([0x68FF]): the scrape sound
-- loops and FUMEE smoke (frames 6-10) puffs on that side, each cycle of it
-- costing 1 armour. Touch a wall at speed over 100 after more than 6 ticks
-- of steering ([0x6939]) and you crash into it: CHOCRR2, the 0x888 sway
-- with a slide of 9 pixels a tick away from the wall, easing off by 1 every
-- other section (0x2271), and 8 armour when it ends (0x2136).

local K = {}

K.SKID = {
    player = { [1] = { -2, -4, -7, -9, -7, -4, -2, -4, -5, -6, -5, -4, -2 },  -- 0x888
               [2] = { 2, 4, 7, 9, 7, 4, 2 } },                                  -- 0x896
    rival  = { [1] = { 2, 4, 7, 9, 7, 4, 2 },                                    -- 0x8A6
               [2] = { -2, -4, -7, -9, -7, -4, -2 } },                           -- 0x89E
}
K.PUSH = 7
K.SKID_ARMOUR = 8
K.WALL_SLIDE = 9
K.WALL_SPEED = 100
K.WALL_STEER_TICKS = 6
K.SMOKE_FIRST, K.SMOKE_END = 6, 11       -- FUMEE frames 6..10, then round again

-- rel: the rival's distance in eighths (race/rival.lua `pos`); moved: did it
-- move this tick; rival / player: screen spans { left, right }.
function K.classify(rel, moved, rival, player, destroyed)
    if destroyed then return nil end
    local along
    if rel >= 0 and rel <= 7 then along = "ahead"
    elseif rel >= -4 and rel < 0 then along = "behind"
    else return nil end
    local side
    if rival[2] < player[1] then return nil
    elseif rival[2] <= player[2] then side = "left"
    elseif rival[1] <= player[2] then side = "right"
    else return nil end
    if not moved then return side end
    return along
end

-- Applies a collision. s: race/rules state; r: the rival; rival_right: is
-- the rival to your right; hold: on HOLD. Returns true the first tick of a
-- contact (to play CHOC4 once).
function K.respond(s, r, kind, rival_right, hold)
    local first = not s.contact
    s.contact = kind
    local function skid(who, script)
        if who == "player" then
            if s.skid then return end
            s.skid = { steps = K.SKID.player[script], i = 0, dx = rival_right and -2 or 2, armour = true }
        else
            if r.skid then return end
            r.skid = { steps = K.SKID.rival[script], i = 0, dx = rival_right and 2 or -2 }
        end
    end
    if kind == "behind" then                                    -- 0x3B98
        r.speed = r.speed - 20
        s.speed = s.speed + 30
        skid("player", 2)
        s.thrill_pending = 30
    elseif kind == "ahead" then                                 -- 0x3BC5
        s.boost_until = -1
        s.speed = math.max(0, s.speed - 20)
        s.brake_until_slow = true
        r.speed = r.speed + 40
        if hold then s.armour = 0; r.damage = 100 end
        skid("rival", 2)
        s.thrill_pending = 10
    else                                                        -- 0x3C11
        local dir = (kind == "left") and 1 or -1                -- away from the rival
        s.push = K.PUSH * dir
        r.push = -K.PUSH * dir
        skid("rival", kind == "left" and 1 or 2)
        s.thrill_pending = 50
    end
    return first
end

function K.clear(s) s.contact = nil end

-- One tick at a wall. wall: "left", "right" or nil; steer_ticks: ticks of
-- steering in a row. Returns "crash" when you hit it.
function K.wall(s, wall, steer_ticks)
    s.scraping = wall
    if not wall then return nil end
    if s.skid or s.wall_latched then return nil end         -- one crash per push (0x6939 = 1000)
    if steer_ticks > K.WALL_STEER_TICKS and s.speed > K.WALL_SPEED then
        s.wall_latched = true
        s.skid = { steps = K.SKID.player[1], i = 0, armour = true,
                   dx = wall == "left" and K.WALL_SLIDE or -K.WALL_SLIDE, ease = true }
        return "crash"
    end
    return nil
end

-- Steering released: the next push into a wall can crash again.
function K.release(s) s.wall_latched = false end

-- One frame of scraping smoke; returns the FUMEE frame to draw. A finished
-- cycle costs 1 armour (0x3086..0x3098).
function K.smoke(s)
    s.smoke = (s.smoke or K.SMOKE_FIRST) + 1
    if s.smoke >= K.SMOKE_END then
        s.smoke = K.SMOKE_FIRST
        s.armour = math.max(0, s.armour - 1)
    end
    return s.smoke
end

-- One tick of pushes and skids. Returns your x change and sway, and the
-- rival's x change (pixels).
function K.tick(s, r)
    local dx, sway, rdx = 0, 0, 0
    local function ease(v) if v > 0 then return v - 1 elseif v < 0 then return v + 1 end return 0 end
    if s.push and s.push ~= 0 then dx = dx + s.push; s.push = ease(s.push) end
    if r and r.push and r.push ~= 0 then rdx = rdx + r.push; r.push = ease(r.push) end
    local k = s.skid
    if k then
        k.i = k.i + 1
        if k.i > #k.steps then
            s.skid = nil
            if k.armour then s.armour = math.max(0, s.armour - K.SKID_ARMOUR) end   -- 0x2136
        else
            sway, dx = k.steps[k.i], dx + k.dx
            -- A wall bounce eases off by 1 every other tick (the game: every
            -- other section, 0x2271).
            if k.ease and k.i % 2 == 0 and k.dx ~= 0 then k.dx = k.dx > 0 and k.dx - 1 or k.dx + 1 end
        end
    end
    local q = r and r.skid
    if q then
        q.i = q.i + 1
        if q.i > #q.steps then r.skid = nil else rdx = rdx + q.dx end
    end
    return dx, sway, rdx
end

return K
