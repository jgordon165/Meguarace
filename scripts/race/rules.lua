-- MegaRace race rules, taken from RACEPRG.EXE. Pure Lua: no engine calls,
-- so it is unit-tested without game data (tests/lua/main.lua).
--
-- Units follow the game: speed is the internal value [0xC9E], 0..180 for
-- normal driving (the dashboard shows it / 10); time is in game ticks.
-- Where the meaning of a value isn't known yet, the name says so.

local R = {}

R.TICKS_PER_SECOND = 25       -- game logic rate used by this port (see docs)
R.MAX_SPEED   = 180           -- [0xC9E] cap; above it speed bleeds off 5/tick
R.BOOST_SPEED = 280           -- code 19 / 25: forced speed while active
R.ENERGY_MAX  = 450           -- [0x6911], EN gauge maximum (RACEPRG 0x117A)

-- Lane under the car, from its screen x (RACEPRG 0x2031). Not from the road:
-- the five lanes are fixed screen bands.
R.LANE_EDGES = { 60, 115, 195, 250 }
function R.lane_for_x(x)
    for i, edge in ipairs(R.LANE_EDGES) do
        if x < edge then return i - 1 end
    end
    return 4
end

-- Sound for each tile code (DS:0xE47 -> sound name table at DS:0x56A6).
R.TILE_SOUND = {
    [1] = "MALUS9", [2] = "MALUS11", [5] = "PLUARME", [6] = "MALUS7", [8] = "BONUS16",
    [9] = "MALUS6", [10] = "MALUS2", [17] = "ENERGYP", [18] = "BONUS1", [19] = "BONUS11",
    [20] = "BONUS10", [21] = "BONUS10", [22] = "BONUS10", [23] = "BONUS15", [24] = "BONUS14",
}

-- Short labels for the on-screen message. Names for codes whose effect is
-- only a flag or timer in the EXE (not traced further yet) say so.
R.TILE_LABEL = {
    [1] = "ENERGY DRAIN", [2] = "POINTS -10", [3] = "BRAKE", [4] = "SPIN", [5] = "WEAPON LOST",
    [6] = "RADAR JAMMED", [7] = "SKID", [8] = "BONUS 8", [9] = "GAUGES OUT", [10] = "MALUS 10",
    [17] = "ENERGY +", [18] = "POINTS +10", [19] = "SUPER SPEED", [20] = "CHARGED SHOT",
    [21] = "WEAPON 3", [22] = "WEAPON 2", [23] = "BONUS 23", [24] = "BONUS 24", [25] = "BOOST",
}

function R.is_bonus(code) return code >= 17 end

-- Per-car table at DS:0x7B0, 10 bytes per car, indexed by [0x81E] and copied
-- to [0x800]..[0x812] at the start of a race (RACEPRG 0x038C). The index
-- order is the T<code> dashboard order (resource 0x24 + [0x81A]: TJO, TRA,
-- TOU, TPA, TLU, TOM, THO, TMA), i.e. formats/cars.lua CODES.
--   guns     [0x800..0x804]  which of weapons 1, 2, 3 the car can carry
--   super    [0x806]         can keep weapon 1's "destroy on hit" charge
--   armour   [0x808]         starting value of the third gauge [0xD2E]
--   power    [0x80A]         damage per hit, and the base of the fire cost
--   handling [0x80C]         (read at 0x1A02/0x1E66/0x27A8; not traced yet)
--   flag24, flag23 [0x80E], [0x810]   per-car switches for codes 24/23
--   timer    [0x812]         x4 at race start: length of codes 6/9's effect
local CAR_TABLE = {
    JO = { 1, 0, 0, 0, 120,  8, 5, 1, 1, 200 },
    RA = { 1, 1, 1, 1,  70, 20, 4, 0, 0, 200 },
    OU = { 1, 1, 1, 1,  70, 20, 6, 1, 1, 200 },
    PA = { 1, 1, 0, 0, 120, 16, 6, 0, 0, 100 },
    LU = { 1, 1, 1, 0, 100, 10, 6, 1, 1, 200 },
    OM = { 1, 1, 0, 0,  80, 10, 4, 1, 1, 100 },
    HO = { 1, 1, 0, 0, 110, 10, 4, 0, 0, 100 },
    MA = { 1, 1, 1, 0, 100, 16, 2, 1, 1, 200 },
}

function R.car_stats(code)
    local t = CAR_TABLE[code] or CAR_TABLE.RA
    return {
        code = CAR_TABLE[code] and code or "RA",
        guns = { t[1] == 1, t[2] == 1, t[3] == 1 }, super = t[4] == 1,
        armour = t[5], power = t[6], handling = t[7],
        flag24 = t[8] == 1, flag23 = t[9] == 1, timer = t[10] * 4,
    }
end

-- Weapons ------------------------------------------------------------------
-- The shot (RACEPRG 0x2D1B fire, 0x2E14 per frame, 0x2F93 hit test) uses the
-- FUMEE sprite set: a muzzle flash at the car (frames 11..13) and the bolt a
-- little ahead of it (14..17). If the bolt is over the rival, it stops there
-- and plays the impact (18..22); otherwise the shot ends after frame 17.
R.RIVAL_HEALTH  = 100          -- [0x814]: damage [0xD30] that destroys the rival
R.HIT_POINTS    = 5            -- [0x68D5]: points paid out per hit
R.EXPLOSION_FRAMES = 24        -- EXPLOSE frames played over a destroyed rival
R.EXPLOSION_POINTS = 5         -- per explosion frame (RACEPRG 0x4699)
R.SHOT = { muzzle = 11, bolt = 14, bolt_end = 18, impact_end = 23 }

-- Muzzle offset from the car and bolt offset from the muzzle, by the car's
-- view angle [0xCA4] (|angle| thresholds 2,3,5,7,9; tables at 0x2DD5 and
-- DS:0xD86). The sign follows the angle: the shot heads toward the centre.
R.SHOT_AIM = {
    { below = 2,   muzzle = 0,  dx = 0,  dy = -17, range = 40 },
    { below = 3,   muzzle = 4,  dx = 8,  dy = -14, range = 38 },
    { below = 5,   muzzle = 12, dx = 18, dy = -10, range = 35 },
    { below = 7,   muzzle = 20, dx = 30, dy = -8,  range = 34 },
    { below = 9,   muzzle = 40, dx = 40, dy = -7,  range = 33 },
    { below = 1e9, muzzle = 48, dx = 55, dy = -6,  range = 32 },
}
-- `range` is how far ahead the shot reaches, in eighths of a section: the
-- hit test wants the rival 0..range ahead (0x2FB2: [0xE0B] against the
-- table's third value).
function R.shot_aim(angle)
    local a = math.abs(angle)
    local sign = angle < 0 and -1 or 1
    for _, e in ipairs(R.SHOT_AIM) do
        if a < e.below then return sign * e.muzzle, sign * e.dx, e.dy, e.range end
    end
end

-- Weapon 3 needs gun 3, else it drops to 2, then 1, then none; the weapon-1
-- charge needs `super` (RACEPRG 0x1FF1, run after every tile).
function R.cap_weapon(s)
    local w, g = s.weapon, s.car.guns
    if w == 3 and not g[3] then w = 2 end
    if w == 2 and not g[2] then w = 1 end
    if w == 1 and not g[1] then w = 0 end
    s.weapon = w
    if not s.car.super then s.super = false end
end

-- Energy per shot: power/2, x8 for a charged weapon 1, +4 for weapons 2, 3.
function R.fire_cost(s)
    local c = s.car.power // 2
    if s.weapon == 1 and s.super then c = c * 8 end
    if s.weapon >= 2 then c = c + 4 end
    return c
end

-- Fire button pressed (RACEPRG 0x2962): needs a weapon and energy left. A
-- new shot replaces one still in flight. Returns true if it fired.
function R.fire(s)
    if s.weapon == 0 or s.energy <= 0 then return false end
    s.energy = math.max(0, s.energy - R.fire_cost(s))
    s.shot = { muzzle = R.SHOT.muzzle, frame = R.SHOT.bolt, hit = false,
               guns = s.weapon, super = s.weapon == 1 and s.super }
    return true
end

-- Advances the shot one frame. Returns "done" when it ends, and "kill" when
-- a charged weapon-1 shot finishes on a rival (RACEPRG 0x2E49).
function R.shot_step(s)
    local sh = s.shot
    if not sh then return nil end
    sh.muzzle = sh.muzzle + 1
    if sh.muzzle == 14 then sh.muzzle = 15 end
    sh.frame = sh.frame + 1
    if not sh.hit and sh.frame >= R.SHOT.bolt_end then s.shot = nil; return "done" end
    if sh.frame >= R.SHOT.impact_end then
        s.shot = nil
        return sh.super and "kill" or "done"
    end
    return nil
end

-- The bolt reached the rival: it stops and plays the impact, the rival takes
-- `power` damage and you get HIT_POINTS. Returns true if the rival is now
-- destroyed. (The game moves the damage across one unit per gun drawn per
-- frame, [0x818]; this port applies it at once.)
function R.hit(s, rival)
    local sh = s.shot
    if not sh or sh.hit then return false end
    sh.hit, sh.frame = true, R.SHOT.bolt_end
    s.points = s.points + R.HIT_POINTS
    s.thrill_pending = s.thrill_pending + 0x40              -- 0x2FEC
    local destroyed = R.damage(rival, s.car.power)
    if destroyed then s.thrill_pending = 5 end             -- 0x3EAD
    return destroyed
end

-- A bump into a rival (RACEPRG 0x3BFF): THRILL rises by 10.
function R.bump(s) s.thrill_pending = 10 end

-- The rival's shot at you (RACEPRG 0x4711, 0x4779, 0x491A). The rival's AI
-- (race/rival.lua) decides when it fires: from just behind you, lined up
-- with your car, with no shot of its in flight. Same frames as yours; a hit
-- lands when it is 5..9 eighths of a section behind and still lined up,
-- and the impact's end costs 20 armour.
R.RIVAL_HIT_BEHIND  = { 5, 9 }      -- eighths of a section
R.RIVAL_SHOT_DAMAGE = 20

function R.rival_fire(s)
    if s.rshot or s.destroyed then return false end
    s.energy = math.max(0, s.energy - 1)                  -- 0x4711 takes 1 from your energy
    s.rshot = { muzzle = R.SHOT.muzzle, frame = R.SHOT.bolt, hit = false }
    return true
end

function R.rival_hit(s)
    local sh = s.rshot
    if not sh or sh.hit then return false end
    sh.hit, sh.frame = true, R.SHOT.bolt_end
    return true
end

-- Returns "done", "hit" (armour lost) or "destroyed" when the shot ends.
function R.rival_shot_step(s)
    local sh = s.rshot
    if not sh then return nil end
    sh.muzzle = sh.muzzle + 1
    if sh.muzzle == 14 then sh.muzzle = 15 end
    sh.frame = sh.frame + 1
    if not sh.hit and sh.frame >= R.SHOT.bolt_end then s.rshot = nil; return "done" end
    if sh.frame >= R.SHOT.impact_end then
        s.rshot = nil
        s.armour = s.armour - R.RIVAL_SHOT_DAMAGE
        if s.armour <= 0 then
            s.armour, s.destroyed = 0, true
            return "destroyed"
        end
        return "hit"
    end
    return nil
end

function R.damage(rival, n)
    if rival.destroyed then return false end
    rival.damage = math.min(R.RIVAL_HEALTH, (rival.damage or 0) + n)
    if rival.damage >= R.RIVAL_HEALTH then rival.destroyed = true; return true end
    return false
end

function R.new(car)
    local stats = R.car_stats(car)
    local s = {
        car = stats,
        speed = 0,              -- [0xC9E]
        energy = 400,           -- [0x6911], 400 at the start (0x0561)
        points = 0,             -- [0x690D] (raised by code 18, lowered by code 2)
        armour = stats.armour,  -- [0xD2E]
        weapon = 1,             -- [0xD5A]: every car starts with weapon 1 (DS:0xD66)
        super = false,          -- [0xD5E]: weapon 1 charged (code 20)
        shot = nil,
        rshot = nil,            -- the rival's shot at you ([0xDAC])
        thrill = 60,            -- [0xD32] THRILL meter (starts at 60, 0x0573)
        thrill_pending = 0,      -- [0xD34] still to add to it
        thrill_fall = 0,         -- [0xD36]
        destroyed = false,      -- armour ran out ([0xD26])
        drain = 50,             -- [0x68DB]: next code-1 drain / code-17 gain size
        tick = 0,               -- [0x7038]
        brake_until_slow = false,   -- code 3, [0x68CF]
        boost_until = -1,           -- code 19/25, [0x6903]/[0x6907]
        timers = {},                -- other timed effects: code -> end tick
        slide = nil,                -- code 4/7 slide: { steps, drift }
        message = nil, message_until = -1,
    }
    R.cap_weapon(s)
    return s
end

-- One game tick. input: { accel = bool, brake = bool }.
-- Returns the speed and any lateral drift (pixels) from a slide.
function R.step(s, input)
    s.tick = s.tick + 1
    -- Keyboard throttle (RACEPRG 0x2CC9): +4 accelerating, -4 braking, -1 coasting.
    local delta = input.accel and 4 or (input.brake and -4 or -1)
    if s.brake_until_slow then
        delta = -4
        if s.speed <= 130 then s.brake_until_slow = false end
    end
    local v = math.max(0, s.speed + delta)
    if s.tick < s.boost_until then
        v = R.BOOST_SPEED                                  -- code 19/25 hold
    elseif v > R.MAX_SPEED then
        v = math.max(R.MAX_SPEED, s.speed - 5)             -- above the cap: bleed 5/tick
    end
    s.speed = v

    for code, t in pairs(s.timers) do
        if s.tick >= t then s.timers[code] = nil end
    end

    -- THRILL meter (RACEPRG 0x1C19): pending amounts raise it one a tick,
    -- otherwise it sinks back one a tick.
    if s.thrill_pending == 0 then s.thrill_fall = 1 end
    if s.thrill_pending > 0 then
        s.thrill_pending = s.thrill_pending - 1
        s.thrill = s.thrill + 1
        if s.thrill >= 127 then s.thrill = 124 end
    end
    if s.thrill_fall > 0 then
        s.thrill_fall = s.thrill_fall - 1
        s.thrill = math.max(0, s.thrill - 1)
    end

    local drift = 0
    if s.slide then
        drift = s.slide.drift
        s.slide.steps = s.slide.steps - 1
        if s.slide.steps <= 0 then s.slide = nil end
    end
    return s.speed, drift
end

local function say(s, text, ticks)
    s.message, s.message_until = text, s.tick + (ticks or 40)
end

-- Applies the tile under the car (RACEPRG 0x1D94..0x1FF1). `turn` is the
-- current steering direction (-1, 0, 1), used by the spin. Returns the
-- sound name to play, if any.
function R.apply_tile(s, code, turn)
    if code == 0 then return nil end
    local display = s.speed // 10                         -- [0xC9A]
    if code == 17 then                                    -- energy +
        s.energy = math.min(R.ENERGY_MAX, s.energy + s.drain); s.drain = 70
    elseif code == 1 then                                 -- energy drain
        s.energy = math.max(0, s.energy - s.drain); s.drain = 50
    elseif code == 2 then
        if s.points >= 10 then s.points = s.points - 10 end
    elseif code == 18 then
        s.points = s.points + 10
    elseif code == 3 then
        s.brake_until_slow = true
    elseif code == 7 or (code == 4 and display <= 15) then
        if display >= 8 and not s.slide then s.slide = { steps = 12, drift = 2 } end
    elseif code == 4 then
        if not s.slide then s.slide = { steps = 16, drift = (turn or 0) >= 0 and 5 or -5 } end
    elseif code == 5 then
        s.weapon = 0; s.energy = 0
    elseif code == 20 then
        s.weapon = 1; s.super = true
    elseif code == 22 then
        s.weapon = 2; s.super = false; s.energy = math.min(R.ENERGY_MAX, s.energy + 60)
    elseif code == 21 then
        s.weapon = 3; s.super = false; s.energy = math.min(R.ENERGY_MAX, s.energy + 120)
    elseif code == 19 then
        if s.boost_until <= s.tick then s.boost_until = s.tick + 400 end
    elseif code == 25 then
        s.boost_until = s.tick + 100
    elseif code == 10 then
        s.timers[10] = s.tick + 800
    elseif code == 6 or code == 9 then
        -- 6 jams the radar ([0x68EB]), 9 blanks the gauges ([0x68E7]), for
        -- the car's [0x812] ticks (RACEPRG 0x1F44..0x1F6B).
        s.timers[code] = s.tick + s.car.timer
    elseif code == 8 or code == 23 or code == 24 then
        s.timers[code] = s.tick + 200
    end
    R.cap_weapon(s)
    say(s, R.TILE_LABEL[code] or ("TILE " .. code))
    return R.TILE_SOUND[code]
end


return R
