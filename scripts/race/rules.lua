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
    [6] = "MALUS 6", [7] = "SKID", [8] = "BONUS 8", [9] = "MALUS 9", [10] = "MALUS 10",
    [17] = "ENERGY +", [18] = "POINTS +10", [19] = "SUPER SPEED", [20] = "WEAPON 1",
    [21] = "WEAPON 3", [22] = "WEAPON 2", [23] = "BONUS 23", [24] = "BONUS 24", [25] = "BOOST",
}

function R.is_bonus(code) return code >= 17 end

function R.new()
    return {
        speed = 0,              -- [0xC9E]
        energy = 200,           -- [0x6911]
        points = 0,             -- [0x690D] (raised by code 18, lowered by code 2)
        weapon = 0,             -- [0xD5A]
        drain = 50,             -- [0x68DB]: next code-1 drain / code-17 gain size
        tick = 0,               -- [0x7038]
        brake_until_slow = false,   -- code 3, [0x68CF]
        boost_until = -1,           -- code 19/25, [0x6903]/[0x6907]
        timers = {},                -- other timed effects: code -> end tick
        slide = nil,                -- code 4/7 slide: { steps, drift }
        message = nil, message_until = -1,
    }
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
        s.weapon = 1
    elseif code == 22 then
        s.weapon = 2; s.energy = math.min(R.ENERGY_MAX, s.energy + 60)
    elseif code == 21 then
        s.weapon = 3; s.energy = math.min(R.ENERGY_MAX, s.energy + 120)
    elseif code == 19 then
        if s.boost_until <= s.tick then s.boost_until = s.tick + 400 end
    elseif code == 25 then
        s.boost_until = s.tick + 100
    elseif code == 10 then
        s.timers[10] = s.tick + 800
    elseif code == 6 or code == 9 then
        s.timers[code] = s.tick + 200                     -- [0x812] ticks; per car, not yet read
    elseif code == 8 or code == 23 or code == 24 then
        s.timers[code] = s.tick + 200
    end
    say(s, R.TILE_LABEL[code] or ("TILE " .. code))
    return R.TILE_SOUND[code]
end

-- How fast a rival moves relative to you, in sections per second. The track
-- video advances at a fixed rate whenever you're moving (RACEPRG 0x0739:
-- the next frame is fetched whenever speed > 0), so speed doesn't change
-- the scenery: it changes who gains on whom. Positive = rival pulls away.
function R.rival_relative(video_fps, player_speed, rival_speed)
    return video_fps * (rival_speed - player_speed) / R.MAX_SPEED
end

return R
