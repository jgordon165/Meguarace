-- The career, as RACEPRG.EXE runs it between races (0x4DB3..0x52CB, 0x5353).
-- Pure Lua: it decides which videos play and where you go next; the menu
-- plays them. Videos are numbered as in the EXE's table at DS:0x59BC.
--
-- The rules:
--   * Fail a round (not qualified, or wrecked) and the career is over: the
--     Hall of Fame, then everything starts again from round 1 (0x516D,
--     0x6A7C). There are no retries.
--   * Season 1 is NOVICE, rounds 1-14. Qualifying on round 14 (LST) starts
--     season 2 on HARDENED from round 1 (0x50D3). Destroying every opponent
--     on LST in season 2 completes the game (0x4F92, 0x5267).
--   * Bonus tracks: three clean rounds in a row (every opponent destroyed)
--     send you to HOLD (0x50E8). Failing to qualify with at least 800 points
--     and three clean rounds banked gives TOKY instead of game over, at most
--     3 times (0x502B). After a bonus track you go on to the next round.
--   * After completing the game the track choice is free (`completed`).
--   * Cars: a new career has JOSE, LUIS and OUZBEL (DS:0x103C, copied to
--     [0x101C] by 0x6A7C); destroying every opponent on a championship
--     round unlocks one more (0x51AB, DS:0x104C + 32 x round).

local C = {}

-- Video numbers -> files (DS:0x59BC), the ones the career uses.
C.VIDEO = {
    [0x20] = "HALL1.HNM", [0x23] = "INTRO.HNM", [0x25] = "FIN.HNM",
    [0x30] = "B64.HNM", [0x31] = "B01.HNM", [0x32] = "B09.HNM", [0x33] = "B17.HNM",
    [0x34] = "B29.HNM", [0x35] = "B24.HNM", [0x36] = "B04.HNM", [0x37] = "B12.HNM",
    [0x38] = "B27.HNM", [0x39] = "B20.HNM", [0x3A] = "B26.HNM", [0x3B] = "B14.HNM",
    [0x3C] = "B23.HNM", [0x3D] = "B07.HNM", [0x3E] = "B30.HNM", [0x3F] = "B92.HNM",
    [0x40] = "B32.HNM", [0x41] = "B34.HNM", [0x42] = "B35.HNM", [0x43] = "B36.HNM",
    [0x44] = "B37.HNM", [0x45] = "B38.HNM", [0x46] = "B39.HNM", [0x47] = "B40.HNM",
    [0x48] = "B41.HNM", [0x49] = "B42.HNM", [0x4A] = "B43.HNM", [0x4B] = "B44.HNM",
    [0x4C] = "B45.HNM", [0x4D] = "B46.HNM", [0x4E] = "B47.HNM", [0x4F] = "B48.HNM",
    [0x50] = "B65.HNM", [0x51] = "B66.HNM", [0x52] = "BA2.HNM", [0x53] = "BB1.HNM",
    [0x54] = "BB2.HNM", [0x55] = "VWBT2.HNM", [0x56] = "VWBT3.HNM", [0x57] = "VWBT4.HNM",
    [0x58] = "VWBT5.HNM", [0x59] = "B79.HNM", [0x5A] = "B80.HNM", [0x5B] = "BA3.HNM",
    [0x5C] = "BA5.HNM", [0x5D] = "BA9.HNM", [0x5E] = "B82.HNM", [0x5F] = "AZUR.HNM",
    [0x60] = "B99.HNM", [0x61] = "BA1.HNM", [0x62] = "B98.HNM", [0x63] = "VWBT.HNM",
    [0x64] = "BB3.HNM", [0x65] = "BB4.HNM", [0x66] = "BB5.HNM", [0x67] = "BB6.HNM",
    [0x68] = "BB7.HNM", [0x69] = "B83.HNM", [0x6A] = "B84.HNM", [0x6B] = "B85.HNM",
    [0x6C] = "B86.HNM", [0x6D] = "B88.HNM", [0x6F] = "B89.HNM", [0x70] = "CREDIT.HNM",
}

C.ROUNDS = 14                  -- the championship; 15 = TOKY, 16 = HOLD (bonus)
C.TOKY, C.HOLD = 15, 16
C.QUALIFY_POINTS = 1200
C.BONUS_POINTS = 800

-- Scenery families: per-family records ([0x1256] -> 0x124C..0x1254) and the
-- base of its three "how you did here before" clips (DS:0x1274).
local FAMILY = { "NS", "MV", "FC", "NG", "SJ", "NS", "MV", "NG", "FC", "SJ", "MV", "FC", "NS", "NG", "NG", nil }
local FAMILY_CLIP = { NS = 0x42, MV = 0x45, FC = 0x48, NG = 0x4E, SJ = 0x4B }
C.FAMILY = FAMILY

-- Menu monitors (1..8, data.MENU_CARS) open at the start, and the one each
-- championship round unlocks when you destroy every opponent there.
C.START_CARS = { 1, 3, 4 }                                   -- JO, LU, OU
C.UNLOCK = { 8, 5, 2, 7, 6, 8, 5, 7, 2, 6, 5, 2, 8, 7 }    -- RA MA HO OM PA ...

local function start_cars()
    local u = { false, false, false, false, false, false, false, false }
    for _, m in ipairs(C.START_CARS) do u[m] = true end
    return u
end

function C.new(level)
    return {
        unlocked = start_cars(),   -- [0x101C] monitors you can pick
        round = 1, main_round = 1, level = level or "NOVICE", total = 0,
        cleared_rounds = 0,        -- [0x69E0] rounds with every opponent destroyed
        hold_streak = 0,           -- [0x69B2] clean rounds in a row (HOLD at 3)
        toky_bank = 0,             -- [0x69B4] clean rounds since the last TOKY
        toky_visits = 0,           -- [0x69B6]
        families = {},             -- family -> { attempts, clears }
        completed = false,         -- [0x69AA]: game finished, free track choice
        last_outcome = nil,
    }
end

-- The clips before a round (0x4E48..0x4E85): the channel ident (VWBT on a
-- new start or game over, a random VWBT2-4 after moving on: 0x5151), then,
-- until the game is completed, Lance on this round (0x31 + round), on this
-- scenery (0x5353) and B64.
-- `how`: "new" (a new career: the show's INTRO first, 0x4E38), "boot" (the
-- intro has just played), or "moved_on".
function C.pre_round(st, how, rand)
    rand = rand or math.random
    local v = {}
    if how == "new" then v[1] = 0x23 end
    v[#v + 1] = (how == "moved_on") and (0x55 + rand(3) - 1) or 0x63
    if not st.completed then
        v[#v + 1] = 0x30 + st.round
        local fam = st.round <= C.ROUNDS and FAMILY[st.round]   -- TOKY/HOLD have none (-1)
        if fam then
            local rec = st.families[fam] or { attempts = 0, clears = 0 }
            local k = rec.attempts == 0 and 0 or (rec.clears < 1 and 1 or 2)
            v[#v + 1] = FAMILY_CLIP[fam] + k - 1
        end
        v[#v + 1] = 0x30
    end
    return v
end

-- Before a race on round 1 on NOVICE: B66 (0x4EBB).
function C.before_race(st)
    if st.round == 1 and st.level == "NOVICE" then return { 0x51 } end
    return {}
end

-- After a race. result = { outcome = "won"|"laps"|"wrecked"|"quit", left,
-- points }. "quit" (ESC in the race, [0x68CB] = -4) ends the career with no
-- clip; its points still count, except on HOLD (0x4F78).
-- Returns { videos = {...}, action = "next" | "gameover" | "complete" } and
-- updates the career (the round to play next, records, score).
function C.after_race(st, result, rand)
    rand = rand or math.random
    local r = st.round
    local points, left, outcome = result.points or 0, result.left or 0, result.outcome
    local videos = {}
    st.last_outcome = outcome
    -- A clean championship round unlocks a car (0x4F4B -> 0x51AB).
    if left == 0 and r <= C.ROUNDS then
        st.unlocked = st.unlocked or start_cars()
        st.unlocked[C.UNLOCK[r]] = true
    end

    local function set_round(bx)                     -- 0x50E1
        if r ~= C.HOLD and st.hold_streak >= 3 then bx = C.HOLD; st.hold_streak = 0 end
        st.round = bx
        if bx <= C.ROUNDS then st.main_round = bx end
        return { videos = videos, action = "next" }
    end
    local function advance(bx)                       -- 0x50D3: past round 14, season 2
        if bx > C.ROUNDS then bx = 1; st.level = "HARDENED" end
        return set_round(bx)
    end
    local function next_round()                      -- 0x5064
        if r == C.HOLD or r == C.TOKY then           -- back to the championship, one on
            st.main_round = st.main_round + 1
            return advance(st.main_round)
        end
        if outcome == "won" or (left <= 1 and points >= C.QUALIFY_POINTS) then return advance(r + 1) end
        return nil
    end
    local function toky_chance()                     -- 0x502B / 0x50A9
        if points >= C.BONUS_POINTS and st.toky_bank >= 3 then
            st.toky_bank = 0
            st.toky_visits = st.toky_visits + 1
            if st.toky_visits <= 3 and r ~= C.TOKY then return set_round(C.TOKY) end
        end
        return nil
    end
    local function gameover()
        return { videos = videos, action = "gameover" }
    end

    -- HOLD always counts as cleared (0x4F5B). A wreck there scores nothing
    -- and goes straight on; otherwise the points count and Lance comments.
    if r == C.HOLD then
        if outcome == "wrecked" then return next_round() end
        if outcome == "quit" then return gameover() end
        st.total = st.total + points
        videos = C.clean_round_clips(points, rand)
        return next_round()
    end
    st.total = st.total + points
    -- Season 2's last round: only destroying everyone counts (0x4F92).
    if r == C.ROUNDS and st.level == "HARDENED" then
        if outcome == "won" then return C.complete(st, videos, rand) end
        points = 0
    end
    -- Records per scenery family (0x4FBE).
    local fam = FAMILY[r]
    if fam then
        local rec = st.families[fam] or { attempts = 0, clears = 0 }
        rec.attempts = rec.attempts + 1
        if left == 0 then rec.clears = rec.clears + 1 end
        st.families[fam] = rec
    end
    if left == 0 then
        st.toky_bank = st.toky_bank + 1
        st.cleared_rounds = st.cleared_rounds + 1
        st.hold_streak = st.hold_streak + 1          -- 0x52D2 (not on HOLD)
        videos = C.clean_round_clips(points, rand)
        return next_round() or gameover()
    end
    st.hold_streak = 0                               -- 0x4FF1
    if outcome == "laps" then
        if left <= 1 and points >= C.QUALIFY_POINTS then
            videos = { 0x5B }                        -- qualified on points: BA3
            return next_round() or gameover()
        end
        local t = toky_chance()
        if t then return t end
        videos = { 0x59 }                            -- not qualified: B79
        return gameover()
    end
    if outcome == "wrecked" then videos = { 0x5A } end   -- destroyed: B80
    return gameover()
end

-- Lance on a clean round, by points (0x52D6): BA9 under 800; BA5, BB1 and
-- one of BB3-BB6 under 1200; else BB2 and one of B83-B86.
function C.clean_round_clips(points, rand)
    rand = rand or math.random
    if points < C.BONUS_POINTS then return { 0x5D } end
    if points < C.QUALIFY_POINTS then return { 0x5C, 0x53, 0x64 + rand(4) - 1 } end
    return { 0x54, 0x69 + rand(4) - 1 }
end

-- Game over (0x516D): the Hall of Fame with your score, then a new career.
-- Returns the Hall of Fame entry to add.
function C.gameover(st)
    local entry = { score = st.total, round = st.round, level = st.level }
    C.reset(st, st.level)
    return entry
end

-- A new career on `level` (0x6A7C): game over, or a level picked from the
-- menu's level panel (0x590B).
function C.reset(st, level)
    for k, v in pairs(C.new(level)) do st[k] = v end
end

-- The menu's focus (RACEPRG 0x561E): 0..7 the monitors, 8..10 the buttons
-- (level, options, Hall of Fame). Arrows step it by `d` (+1 right or down,
-- -1 left or up), wrapping, past monitors whose car is locked.
function C.step_focus(f, d, unlocked)
    for _ = 1, 11 do
        f = f + d
        if f < 0 then f = 10 elseif f > 10 then f = 0 end
        if f > 7 or unlocked[f + 1] then return f end
        if d == 0 then d = 1 end
    end
    return 8
end

-- The game completed (0x5267): Hall of Fame, then B82, a BB3-BB6 clip, B89,
-- FIN and the credits; the score starts again and the track choice is free.
function C.complete(st, videos, rand)
    rand = rand or math.random
    videos = videos or {}
    for _, v in ipairs({ 0x5E, 0x64 + rand(4) - 1, 0x6F, 0x25 }) do videos[#videos + 1] = v end
    return { videos = videos, action = "complete", credits = true }
end

function C.after_complete(st)
    st.completed = true
    st.total = 0
end

-- Quitting from the options (0x5113): Lance's farewell, then the credits.
--   B99 after a wreck, BA1 with fewer than 4 clean rounds, else B98 + AZUR.
function C.farewell(st)
    if st.last_outcome == "wrecked" then return { 0x60 } end
    if st.cleared_rounds < 4 then return { 0x61 } end
    return { 0x62, 0x5F }
end

function C.file(id) return C.VIDEO[id] end

return C
