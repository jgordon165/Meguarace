-- Lua test suite, run by the engine itself (headless):
--   megarace --scripts tests/lua --frames 1     (MEGARACE_SCRIPTS = path to scripts/)
-- Exits non-zero on failure. Tests marked [data] run only when MEGARACE_DATA
-- points at a MegaRace CD (or copy); CI has no game data and skips them.

local scripts = os.getenv("MEGARACE_SCRIPTS") or "scripts"
package.path = scripts .. "/?.lua;" .. package.path

local failures, checks, skipped = 0, 0, 0
local function check(cond, msg)
    checks = checks + 1
    if not cond then
        failures = failures + 1
        print("FAIL: " .. msg)
    end
end

local tests = {}
local function test(name, fn) tests[#tests + 1] = { name = name, fn = fn } end

-- kl occlusion masks -------------------------------------------------------

test("kl: single layer, implicit left edge", function()
    local track = require("formats.track")
    -- count 1; entry: section 5, rows 0..2, start 0, data at 8 (as on the disc)
    -- op 0x39 = 3 rows, 2 boundaries, left at x=0, right explicit; deltas 100
    local kl = string.char(1, 5, 0, 2, 0, 0, 8, 0, 0x39, 100, 100, 100)
    local L = track.decode_kl(kl)
    check(#L == 1 and L[1].section == 5, "one layer at section 5")
    for row = 0, 2 do
        local s = L[1].lines[row]
        check(s and s[1] == 0 and s[2] == 100, "row " .. row .. " is [0,100)")
    end
    check(L[1].lines[3] == nil, "row 3 not part of the layer")
end)

test("kl: varint, 6 boundaries, empty rows", function()
    local track = require("formats.track")
    -- rows 0..2: op 0x10 = 1 empty row; op 0x1F = 1 row, 6 boundaries, both
    -- ends explicit, deltas 10,20,30,40,50,60 => 10,30,60,100,150,210
    -- then op 0x15 = 1 row, 2 boundaries, left explicit, right implicit: 250
    local kl = string.char(1, 0, 0, 2, 0, 0, 8, 0,
        0x10,
        0x1F, 10, 20, 30, 40, 50, 60,
        0x15, 0x00, 0x05)                 -- varint: 0 then 5 => 261
    local L = track.decode_kl(kl)[1]
    check(L.lines[0] == nil, "row 0 empty")
    local r1 = L.lines[1]
    check(r1 and #r1 == 6 and r1[1] == 10 and r1[6] == 210, "row 1 has 3 spans")
    local r2 = L.lines[2]
    check(r2 and r2[1] == 261 and r2[2] == 320, "row 2 [261,320) via varint")
end)

test("kl: layer choice by distance", function()
    local track = require("formats.track")
    local T = setmetatable({ video = { chunks = function()
        -- two 8-byte entries (sections 4 and 9), then one full-width row each
        return { kl = string.char(2, 4, 0, 0, 0, 0, 16, 0, 0, 9, 0, 0, 0, 0, 17, 0, 0x11, 0x11) }
    end } }, track)
    check(T:mask(0, 3) == nil, "closer than first layer: unmasked")
    check(T:mask(0, 4).section == 4, "at first layer")
    check(T:mask(0, 8).section == 4, "between layers")
    check(T:mask(0, 40).section == 9, "beyond second layer")
end)

-- projection -----------------------------------------------------------------

test("projection: straight-ahead camera", function()
    local track = require("formats.track")
    -- Camera at origin looking down -x (heading 0): m = sin 0, cos 1.
    local cam = { x = 0, y = 0, z = 0, m = { 0, 16384, 0, 0, 16384, 16384, 0, 0 },
                  depth_bias = 0, roll_sin = 0, roll_cos = 4096 }
    local sx, sy, z = track.project(nil, cam, -100, 0, 0)
    check(z and math.abs(z - 100) < 1e-6, "depth 100")
    check(math.abs(sx - 160) < 1e-6, "centred horizontally")
    check(math.abs(sy - 131 * 200 / 262) < 1e-6, "on the horizon line")
    local rx = track.project(nil, cam, -100, 50, 0)
    check(rx > 160, "+y is to the right")
    local _, uy = track.project(nil, cam, -100, 0, 10)
    check(uy < sy, "+z is up the screen")
    check(track.project(nil, cam, 100, 0, 0) == nil, "behind the camera is culled")
end)

-- cars ---------------------------------------------------------------------

test("cars: angle to image, mirroring", function()
    local cars = require("formats.cars")
    check(#cars.ANGLE_FRAME == 96, "96 angle steps")
    local f, flip = cars.frame_for(0);    check(f == 0 and not flip, "straight behind")
    f, flip = cars.frame_for(-5);         check(f == 2 and flip, "negative angle mirrors")
    f = cars.frame_for(20);               check(f == 8, "angle 20 -> image 8")
    f = cars.frame_for(500);              check(f == 16, "clamped to head-on")
end)

-- race rules -------------------------------------------------------------------

test("rules: lanes are fixed screen bands", function()
    local rules = require("race.rules")
    check(rules.lane_for_x(0) == 0 and rules.lane_for_x(59) == 0, "0-59 lane 0")
    check(rules.lane_for_x(60) == 1 and rules.lane_for_x(114) == 1, "60-114 lane 1")
    check(rules.lane_for_x(160) == 2, "centre lane 2")
    check(rules.lane_for_x(249) == 3 and rules.lane_for_x(250) == 4, "250+ lane 4")
end)

test("rules: throttle, coasting, cap", function()
    local rules = require("race.rules")
    local s = rules.new()
    for _ = 1, 10 do rules.step(s, { accel = true }) end
    check(s.speed == 40, "+4 per tick accelerating")
    rules.step(s, {})
    check(s.speed == 39, "-1 per tick coasting")
    rules.step(s, { brake = true })
    check(s.speed == 35, "-4 per tick braking")
    for _ = 1, 100 do rules.step(s, { accel = true }) end
    check(s.speed == rules.MAX_SPEED, "capped at 180")
    for _ = 1, 100 do rules.step(s, { brake = true }) end
    check(s.speed == 0, "never below 0")
end)

test("rules: tiles", function()
    local rules = require("race.rules")
    local s = rules.new("RA")
    s.energy = 100
    check(rules.apply_tile(s, 17) == "ENERGYP" and s.energy == 150, "17: energy +50, then +70")
    check(rules.apply_tile(s, 17) and s.energy == 220, "second 17: +70")
    check(rules.apply_tile(s, 1) == "MALUS9" and s.energy == 150, "1: drain 70 (last size), then 50")
    rules.apply_tile(s, 18); rules.apply_tile(s, 18)
    check(s.points == 20, "18: points +10")
    rules.apply_tile(s, 2)
    check(s.points == 10, "2: points -10")
    rules.apply_tile(s, 21)
    check(s.weapon == 3, "21: weapon 3")
    check(rules.apply_tile(s, 5) == "PLUARME" and s.weapon == 0 and s.energy == 0, "5: weapon and energy lost")
    s.speed = 100
    rules.apply_tile(s, 19)
    rules.step(s, {})
    check(s.speed == rules.BOOST_SPEED, "19: super speed")
    for _ = 1, 398 do rules.step(s, {}) end         -- ticks 2..399 after the tile
    check(s.speed == rules.BOOST_SPEED, "super speed holds for 400 ticks")
    for _ = 1, 25 do rules.step(s, {}) end
    check(s.speed <= rules.MAX_SPEED, "then bleeds back under the cap (5 per tick)")
    s.speed = 170
    rules.apply_tile(s, 3)
    for _ = 1, 3 do rules.step(s, { accel = true }) end
    check(s.speed < 170, "3: forced braking even while accelerating")
    check(rules.apply_tile(s, 0) == nil, "0: nothing")
end)

test("rules: per-car weapons", function()
    local rules = require("race.rules")
    local jo, ra = rules.new("JO"), rules.new("RA")
    check(jo.weapon == 1 and ra.weapon == 1, "every car starts with weapon 1")
    check(jo.armour == 120 and ra.armour == 70, "armour from the car table")
    rules.apply_tile(jo, 21)
    check(jo.weapon == 1, "JO can't carry weapon 3: drops to 1 (no gun 2 either)")
    rules.apply_tile(jo, 20)
    check(jo.weapon == 1 and not jo.super, "JO can't keep the charge")
    rules.apply_tile(ra, 20)
    check(ra.weapon == 1 and ra.super, "20: charged weapon 1")
    local pa = rules.new("PA")
    rules.apply_tile(pa, 21)
    check(pa.weapon == 2, "PA: weapon 3 drops to 2")
    rules.apply_tile(ra, 5)
    check(ra.weapon == 0 and not rules.fire(ra), "no weapon: can't fire")
end)

test("rules: fire cost, shot frames, hits", function()
    local rules = require("race.rules")
    local s = rules.new("RA")                       -- power 20
    check(rules.fire_cost(s) == 10, "weapon 1: power / 2")
    s.super = true
    check(rules.fire_cost(s) == 80, "charged weapon 1: x8")
    s.super = false; s.weapon = 2
    check(rules.fire_cost(s) == 14, "weapon 2: +4")
    s.weapon = 1
    s.energy = 15
    check(rules.fire(s) and s.energy == 5 and s.shot, "fire spends energy")
    check(s.shot.muzzle == 11 and s.shot.frame == 14, "muzzle 11, bolt 14")
    local ends
    for _ = 1, 10 do ends = ends or rules.shot_step(s) end
    check(ends == "done" and s.shot == nil, "a miss ends after bolt frame 17")
    s.energy = 5
    check(rules.fire(s) and s.energy == 0, "energy never below 0")
    check(not rules.fire(s), "no energy: can't fire")
    s.energy = 400
    local rival = { damage = 0 }
    for i = 1, 4 do
        rules.fire(s)
        check(not rules.hit(s, rival), "hit " .. i .. " doesn't destroy")
        check(not rules.hit(s, rival), "one hit per shot")
    end
    check(rival.damage == 80 and s.points == 20, "20 damage and 5 points per hit")
    rules.fire(s)
    check(rules.hit(s, rival) and rival.destroyed, "fifth hit destroys (health 100)")
    -- charged shot: destroys on the impact's last frame
    local r2 = { damage = 0 }
    s.weapon, s.super = 1, true
    rules.fire(s); rules.hit(s, r2)
    local res
    for _ = 1, 10 do res = res or rules.shot_step(s) end
    check(res == "kill", "charged hit: kill at the end of the impact")
    local o, dx, dy = rules.shot_aim(-6)
    check(o == -20 and dx == -30 and dy == -8, "aim mirrors with the angle")
end)

test("rules: rival shots, armour, THRILL, tiles 6 and 9", function()
    local rules = require("race.rules")
    local s = rules.new("RA")                       -- armour 70, timer 800
    check(rules.rival_fire(s) and s.energy == 399, "rival fires; costs you 1 energy (of 400)")
    check(not rules.rival_fire(s), "one rival shot at a time")
    local res
    for _ = 1, 10 do res = res or rules.rival_shot_step(s) end
    check(res == "done" and s.armour == 70, "a miss does nothing")
    for i = 1, 3 do
        rules.rival_fire(s); rules.rival_hit(s)
        res = nil
        for _ = 1, 10 do res = res or rules.rival_shot_step(s) end
        check(res == "hit" and s.armour == 70 - 20 * i, "hit " .. i .. ": armour -20")
    end
    rules.rival_fire(s); rules.rival_hit(s)
    res = nil
    for _ = 1, 10 do res = res or rules.rival_shot_step(s) end
    check(res == "destroyed" and s.destroyed and s.armour == 0, "fourth hit destroys RA")
    check(not rules.rival_fire(s), "no more shots at a wreck")

    local t = rules.new("JO")
    check(t.thrill == 60, "THRILL starts at 60")
    for _ = 1, 10 do rules.step(t, {}) end
    check(t.thrill == 50, "and sinks 1 a tick")
    rules.bump(t)
    for _ = 1, 10 do rules.step(t, {}) end
    check(t.thrill == 60, "a bump raises it by 10")
    rules.fire(t); rules.hit(t, { damage = 0 })
    for _ = 1, 64 do rules.step(t, {}) end
    check(t.thrill == 124, "a hit raises it by 64")

    rules.apply_tile(t, 9)
    check(t.timers[9] == t.tick + 800, "9: gauges out for the car's timer (200 x 4)")
    rules.apply_tile(t, 6)
    check(t.timers[6] == t.tick + 800, "6: radar jammed as long")
end)

test("hud: gauge frames, radar, per-car tables", function()
    local hud, rules = require("race.hud"), require("race.rules")
    local g = hud.GAUGES.speed.JO                   -- { 155, 17, 11, 13 }
    check(hud.frame(g, 0, 22) == 11 and hud.frame(g, 22, 22) == 23, "first..last frame")
    check(hud.frame(g, 11, 22) == 17, "half way")
    check(hud.frame(g, 99, 22) == 23 and hud.frame(g, -5, 22) == 11, "clamped")
    check(hud.blip(0) == 0 and hud.blip(7) == 4, "ahead: 0..4")
    check(hud.blip(-1) == 6 and hud.blip(-20) == 9, "behind: 6..9")
    local rd = hud.RADAR.JO                         -- x 151, width 77
    check(hud.blip_x(rd, 0.5, 20) == 151, "centre of the road: centre of the radar")
    check(hud.blip_x(rd, 0, 20) == 151 - 38 + 10, "kept inside the screen")
    for name, tbl in pairs(hud.GAUGES) do
        for _, code in ipairs(hud.ORDER) do check(tbl[code] and #tbl[code] == 4, name .. " for " .. code) end
    end
    local s = rules.new("OU")
    s.speed = 180; s.energy = 450
    local v = hud.values(s)
    check(v.speed == 18 and v.energy == 450 and v.armour == 70 and v.weapon == 1, "values")
    check(v.lamp_a == 1 and v.lamp_b == 1, "lamps lit while not charged")
    rules.apply_tile(s, 9)
    v = hud.values(s)
    check(v.speed == 0 and v.energy == 0 and v.armour == 0, "tile 9: every gauge reads 0")
end)

test("rival: kinds, movement, AI states, qualifying", function()
    local V = require("race.rival")
    check(V.kind_for(1) == "centre" and V.kind_for(2) == "weaver" and V.kind_for(5) == "rusher", "kind by opponents left")
    check(V.OPPONENTS[1] == 4 and V.OPPONENTS[5] == 6 and V.OPPONENTS[16] == 7, "opponents per round")
    -- movement table (0x42C6)
    local a, b = V.step_rate(5); check(a == 0, "under 10 apart: no movement")
    a, b = V.step_rate(-15); check(a == 1 and b == 10, "10..19: an eighth every 10 ticks")
    a, b = V.step_rate(45); check(a == 1 and b == 2, "40..59: every 2 ticks")
    a, b = V.step_rate(100); check(a == 2 and b == 1, "60+: 2 a tick")
    a, b = V.step_rate(200); check(a == 8 and b == 1, "180+: a section a tick")
    local r = V.new(5, 40, 0)
    check(r.kind == "rusher" and r.state == "rush" and r.speed == 230, "a rusher starts at 230")
    for _ = 1, 20 do V.tick(r, 180) end
    check(r.pos == 40 + 10, "50 faster: an eighth every 2 ticks")
    -- rusher: rush until 21 sections ahead, then keep near you
    V.decide(r, { player_speed = 180 }); check(r.state == "rush", "still rushing at 50/8")
    r.pos = 170; V.decide(r, { player_speed = 180 }); check(r.state == "chase", "21 sections: chase")
    V.decide(r, { player_speed = 180 }); check(r.speed == 80, "far ahead: 100 slower than you")
    r.pos = 20; V.decide(r, { player_speed = 180 }); check(r.speed == 150, "2.5 sections: 30 slower")
    V.decide(r, { player_speed = 90 }); check(r.speed == 140, "you're slow: 50 faster")
    r.pos = -1; V.decide(r, { player_speed = 180 }); check(r.state == "behind", "passed: behind")
    check(V.decide(r, { player_speed = 180, lined_up = true, shot_free = true }) and r.speed == 160, "just behind, lined up: fires, at 160")
    r.pos = -5
    check(not V.decide(r, { player_speed = 180, lined_up = true, shot_free = true }), "more than 4/8 behind: no shot")
    r.pos = -3
    check(not V.decide(r, { player_speed = 180, lined_up = false, shot_free = true }), "not lined up: no shot")
    -- weaver
    local w = V.new(2, 100, 180)
    V.decide(w, { player_speed = 180 }); check(w.state == "chase", "weaver chases")
    w.pos = 39; V.decide(w, { player_speed = 180 }); check(w.state == "close" and w.steer == -1, "within 5 sections: close, steering left")
    w.pos = -5; check(V.decide(w, { player_speed = 180, lined_up = true, shot_free = true }), "fires from -5/8")
    w.pos = -11; check(not V.decide(w, { player_speed = 180, lined_up = true, shot_free = true }), "not from -11/8")
    w.pos = 41; V.decide(w, { player_speed = 180 }); check(w.state == "chase", "back to chasing at 5 sections")
    -- left behind
    local l = V.new(2, -60, 100)
    local res
    for _ = 1, 400 do res = res or V.tick(l, 100) end
    check(res == "lost", "400 ticks 6+ sections behind: out")
    check(V.decision_interval("HARDENED") == 50 and V.decision_interval("NOVICE") == 100, "AI every 100 ticks, 50 on HARDENED")
    check(V.qualifies("won", 0, 0), "all destroyed: qualified")
    check(V.qualifies("laps", 1, 1200) and not V.qualifies("laps", 1, 1199) and not V.qualifies("laps", 2, 5000), "laps: at most 1 left and 1200 points")
    check(not V.qualifies("wrecked", 0, 9999), "wrecked: not qualified")
end)

test("career: clips, game over, seasons, bonus tracks, finale", function()
    local C = require("game.career")
    local one = function() return 1 end           -- rand(n) -> 1: the first of each choice
    local st = C.new("NOVICE")
    local pre = C.pre_round(st, "new", one)
    check(pre[1] == 0x23 and pre[2] == 0x63 and pre[3] == 0x31 and pre[4] == 0x41 and pre[5] == 0x30,
          "new career: INTRO, VWBT, round 1 (B01), first visit to NS (B34), B64")
    check(C.before_race(st)[1] == 0x51, "round 1 on NOVICE: B66 first")

    -- a clean round with 900 points: BA5, BB1, BB3; on to round 2
    local out = C.after_race(st, { outcome = "won", left = 0, points = 900 }, one)
    check(out.action == "next" and st.round == 2 and st.total == 900, "won: next round, points scored")
    check(out.videos[1] == 0x5C and out.videos[2] == 0x53 and out.videos[3] == 0x64, "800-1199: BA5, BB1, BB3")
    pre = C.pre_round(st, "moved_on", one)
    check(pre[1] == 0x55 and pre[2] == 0x32 and pre[3] == 0x44, "moved on: VWBT2, B09, first visit to MV (B37)")

    -- qualified on points after the laps: BA3
    out = C.after_race(st, { outcome = "laps", left = 1, points = 1300 }, one)
    check(out.action == "next" and out.videos[1] == 0x5B and st.round == 3, "laps, 1 left, 1300: BA3, qualified")
    check(st.hold_streak == 0, "not clean: the HOLD streak resets")

    -- not qualified: B79 and game over; the career resets, the level stays
    out = C.after_race(st, { outcome = "laps", left = 3, points = 500 }, one)
    check(out.action == "gameover" and out.videos[1] == 0x59, "not qualified: B79, game over")
    local e = C.gameover(st)
    check(e.score == 900 + 1300 + 500 and st.round == 1 and st.total == 0, "Hall of Fame entry, career reset")

    -- wrecked: B80
    out = C.after_race(st, { outcome = "wrecked", left = 2, points = 100 }, one)
    check(out.action == "gameover" and out.videos[1] == 0x5A, "wrecked: B80")
    C.gameover(st)

    -- three clean rounds in a row -> HOLD, then back to the next round
    for r = 1, 3 do C.after_race(st, { outcome = "won", left = 0, points = 1500 }, one) end
    check(st.round == C.HOLD and st.main_round == 3, "three clean rounds: HOLD")
    out = C.after_race(st, { outcome = "laps", left = 0, points = 300 }, one)
    check(out.action == "next" and st.round == 4, "after HOLD: round 4")
    check(st.families.NS.attempts == 1 and st.families.NS.clears == 1, "records per scenery family")
    pre = C.pre_round(st, "moved_on", one)
    check(pre[3] == 0x4D, "round 4, first visit to its scenery: B46")

    -- TOKY: fail to qualify with 800+ points and three clean rounds banked
    check(st.toky_bank == 3, "three clean rounds banked")
    out = C.after_race(st, { outcome = "laps", left = 3, points = 850 }, one)
    check(out.action == "next" and st.round == C.TOKY and st.toky_bank == 0, "a second chance: TOKY")
    out = C.after_race(st, { outcome = "won", left = 0, points = 1500 }, one)
    check(st.round == 5, "after TOKY: past the round you failed")

    -- season 2: qualifying on round 14 goes to round 1 on HARDENED
    local s2 = C.new("NOVICE"); s2.round, s2.main_round = 14, 14
    C.after_race(s2, { outcome = "laps", left = 1, points = 1200 }, one)
    check(s2.round == 1 and s2.level == "HARDENED", "season 2 on HARDENED")
    -- on HARDENED, LST needs every opponent destroyed
    s2.round = 14
    out = C.after_race(s2, { outcome = "laps", left = 1, points = 5000 }, one)
    check(out.action == "gameover", "HARDENED LST on points: not enough")
    s2 = C.new("HARDENED"); s2.round = 14
    out = C.after_race(s2, { outcome = "won", left = 0, points = 100 }, one)
    check(out.action == "complete" and out.videos[1] == 0x5E and out.videos[#out.videos] == 0x25, "finale: B82 .. FIN")
    C.after_complete(s2)
    check(s2.completed and s2.total == 0, "completed: free choice, score reset")
    check(#C.pre_round(s2, "new", one) == 2, "after the finale only the intro and the ident")

    -- farewell
    local f = C.new(); f.last_outcome = "wrecked"
    check(C.farewell(f)[1] == 0x60, "quit after a wreck: B99")
    f.last_outcome = "won"; check(C.farewell(f)[1] == 0x61, "few clean rounds: BA1")
    f.cleared_rounds = 4; local fw = C.farewell(f); check(fw[1] == 0x62 and fw[2] == 0x5F, "else B98 + AZUR")
end)

test("subtitles: .SUB groups and line placement", function()
    local subs = require("formats.subtitles")
    -- two groups: { "HI", "THERE" } and { "BYE" }
    local g1 = string.char(2) .. "HI" .. "\xff" .. string.char(5) .. "THERE" .. "\xff"
    local g2 = string.char(3) .. "BYE" .. "\xff"
    local function u16(v) return string.char(v & 255, v >> 8) end
    local d = u16(4) .. u16(4 + #g1) .. g1 .. g2
    local groups = subs.parse(d)
    check(groups and #groups[0] == 2 and groups[0][2] == "THERE" and groups[1][1] == "BYE", "groups and lines")
    check(subs.for_video(groups, "intro.hnm")[1] == "HI", "group 0 is INTRO")
    check(subs.for_video(groups, "B64.HNM")[1] == "BYE", "group 1 is B64")
    check(subs.for_video(groups, "NS11.HNM") == nil, "no group: no subtitles")
    check(subs.start_x("Welcome to NewSan!") == 160 - (18 * 9) // 4, "x = 160 - 2.25 * length")
    check(#subs.GROUP_VIDEO == 62, "62 subtitled videos")
end)

test("voc: decodes a type-1 block", function()
    local voc = require("formats.voc")
    local hdr = "Creative Voice File\x1a" .. string.char(26, 0, 0x0a, 0x01, 0x29, 0x11)
    local pcm = string.char(128, 200, 50, 128)
    local blk = string.char(1, #pcm + 2, 0, 0, 166, 0) .. pcm
    local out, rate = voc.decode(hdr .. blk .. string.char(0))
    check(out == pcm, "samples")
    check(rate == 11111, "rate from time constant 166")
    check(voc.decode("nope") == nil, "rejects non-VOC")
    local _, low = voc.decode(hdr .. string.char(1, #pcm + 2, 0, 0, 6, 0) .. pcm .. string.char(0))
    check(low == voc.FALLBACK_RATE, "implausible 4 kHz rate replaced")
end)

-- save file / menu state -------------------------------------------------

test("save: serialize and parse round trip", function()
    local save = require("game.save")
    local st = { level = "HARDENED", car = 3, round = 5, total = 4200,
                 hall = { { name = "BIG BOB", score = 8000, track = "NEW FAC-1", level = "HARD" } } }
    local back = save.parse(save.serialize(st))
    check(back and back.level == "HARDENED" and back.round == 5 and back.total == 4200, "fields survive")
    check(back.hall[1].name == "BIG BOB" and back.hall[1].score == 8000, "hall survives")
    check(save.parse("os.exit()") == nil, "code in a save file is not run")
    check(save.parse("{ x = os }") == nil or save.parse("{ x = os }").x == nil, "no globals in a save file")
end)

test("save: hall of fame keeps the top 7", function()
    local save = require("game.save")
    local st = { hall = {} }
    for i = 1, 8 do save.add_score(st, { name = "R" .. i, score = i * 100 }) end
    local rank = save.add_score(st, { name = "NEW", score = 450 })
    check(rank == 5, "450 ranks 5th")
    check(#st.hall == 7 and st.hall[7].score == 300, "lowest dropped")
    check(save.add_score(st, { name = "LOW", score = 1 }) == nil, "too low: not entered")
end)

-- bytes reader ----------------------------------------------------------------

test("core.bytes reader", function()
    local bytes = require("core.bytes")
    local r = bytes.new(string.char(0x34, 0x12, 0xFF, 0xFF, 0x41, 0x42, 0, 0))
    check(r:u16() == 0x1234, "u16")
    check(r:s16() == -1, "s16")
    check(r:cstr(4) == "AB", "cstr")
    check(r:left() == 0, "consumed")
end)

-- [data] tests against the player's own CD -----------------------------------

local data = os.getenv("MEGARACE_DATA")

test("[data] archive and intro videos", function()
    if not data then skipped = skipped + 1; return "skip" end
    check(dat.open("MEGARACE.DAT"), "MEGARACE.DAT opens")
    check(#dat.list() == 261, "261 files in the archive")
    for _, n in ipairs({ "MINDSCAP.HNM", "CRYO.HNM", "INTRO.HNM" }) do
        local v = hnm.open(n)
        check(v ~= nil, n .. " opens")
        if v then
            local ok = true
            for i = 0, math.min(v:frames(), 200) - 1 do
                if not v:decode(i) then ok = false break end
            end
            check(ok, n .. " decodes")
        end
    end
end)

test("[data] car sprites", function()
    if not data then skipped = skipped + 1; return "skip" end
    local cars = require("formats.cars")
    for _, code in ipairs(cars.CODES) do
        local c = cars.open(code)
        check(c.body:count() == 17, code .. "_A has 17 views")
        local w, h = c.body:size(0)
        check(w and w > 40 and h > 20, code .. " rear view has a sensible size")
        check(c.dash and c.dash:size(0) == 160, "T" .. code .. "_2 dashboard half is 160 wide")
        local r = cars.open_rival(code)
        check(r.body:size(10) ~= nil, cars.RIVAL_FILE[code] .. " has 11 rear views")
    end
end)

test("[data] tile sounds exist", function()
    if not data then skipped = skipped + 1; return "skip" end
    local rules, voc = require("race.rules"), require("formats.voc")
    for code, name in pairs(rules.TILE_SOUND) do
        local pcm, rate = voc.decode(dat.read(name .. ".VOC", false))
        check(pcm and #pcm > 100 and rate > 5000, name .. ".VOC (tile " .. code .. ")")
    end
end)

test("[data] weapon sprites and sounds", function()
    if not data then skipped = skipped + 1; return "skip" end
    local voc = require("formats.voc")
    local fx, boom = spr.open("FUMEE"), spr.open("EXPLOSE")
    check(fx and fx:count() == 23, "FUMEE: 23 images (muzzle 11-13, bolt 14-17, impact 18-22)")
    check(boom and boom:count() == 24, "EXPLOSE: 24 images")
    for _, name in ipairs({ "LASER", "CHOCTIR", "EXPLOD2" }) do
        local pcm = voc.decode(dat.read(name .. ".VOC", false))
        check(pcm and #pcm > 100, name .. ".VOC")
    end
end)

test("music: song per round and per track video", function()
    local music = require("game.music")
    check(#music.SONGS == 6 and #music.ROUND_SONG == 16, "6 songs, 16 rounds")
    check(music.song_for_video("NS11") == 1 and music.song_for_video("NS12") == 1, "NS1 laps: NEWSAN")
    check(music.song_for_video("FC21") == 3, "FC2: DETRITUS (Factory Land)")
    check(music.song_for_video("HOLD") == 6 and music.song_for_video("TOKY") == 6, "last two: PAGA")
    check(music.song_for_video("NOPE") == nil, "unknown video: no song")
end)

test("[data] the six FM songs", function()
    if not data then skipped = skipped + 1; return "skip" end
    local music = require("game.music")
    for _, s in ipairs(music.SONGS) do
        local bytes = dat.read(s.file .. ".HSQ")
        check(bytes and #bytes > 30000, s.file .. ".HSQ unpacks")
        if audio.available() then
            check(audio.song(bytes), s.file .. " plays")
            check(audio.song_playing(), s.file .. " is playing")
        end
    end
    audio.stop_song()
    check(not audio.song_playing(), "stop_song stops it")
    check(not audio.song("not a song"), "junk is refused")
end)

test("[data] every dashboard's gauge images", function()
    if not data then skipped = skipped + 1; return "skip" end
    local hud = require("race.hud")
    for _, code in ipairs(hud.ORDER) do
        local set = spr.open("T" .. code)
        check(set, "T" .. code)
        if set then
            local n = set:count()
            for name, tbl in pairs(hud.GAUGES) do
                local g = tbl[code]
                check(g[3] + g[4] - 1 < n, string.format("%s %s frames %d..%d of %d", code, name, g[3], g[3] + g[4] - 1, n))
            end
            local rd = hud.RADAR[code]
            check(rd[3] + 12 < n, code .. " radar frames")
        end
    end
    local z = spr.open("ZONES_")
    check(z and z:count() == 21, "ZONES_: 20 icons + scanner panel")
    for _, f in ipairs({ "FONTE1.RAW", "FONTE2.RAW", "FONTE6_6.RAW" }) do
        check(dat.exists(f), f)
    end
end)

test("[data] every career clip", function()
    if not data then skipped = skipped + 1; return "skip" end
    local C = require("game.career")
    for id, file in pairs(C.VIDEO) do check(dat.exists(file), string.format("video 0x%02X %s", id, file)) end
end)

test("[data] subtitles: every language, cues match lines", function()
    if not data then skipped = skipped + 1; return "skip" end
    local subs = require("formats.subtitles")
    for _, l in ipairs(subs.LANGUAGES) do
        local g = subs.load(l.code)
        check(g and g[0] and #g[0] > 100, "MEGA" .. l.code .. ".SUB: the intro's lines")
    end
    check(subs.font(), "DFCHAR.BIN")
    local g = subs.load("AMER")
    for _, v in ipairs({ "B01.HNM", "B64.HNM", "BA5.HNM" }) do
        local h, n = hnm.open(v), 0
        for i = 0, h:frames() - 1 do if h:decode(i).subtitle == "next" then n = n + 1 end end
        check(n == #subs.for_video(g, v), v .. ": one cue per line")
    end
end)

test("[data] game data from RACEPRG.EXE", function()
    if not data then skipped = skipped + 1; return "skip" end
    local gd = require("game.data")
    local names = gd.track_names()
    check(names[1] == "NEW SAN-1" and names[16] == "HOLDER", "track display names")
    local hall = gd.default_hall()
    check(#hall == 7 and hall[1].name == "LANCE BOYLE" and hall[1].score == 12000, "default Hall of Fame: 7 places")
    local last = gd.default_last_run()
    check(last and last.name == "ENFORCER SCORE", "the 8th record is your run's line")
    for _, c in ipairs(gd.MENU_CARS) do check(dat.exists(c.video .. ".HNM"), c.video .. ".HNM exists") end
    local p = spr.open("PANEL2")
    check(p and p:count() == 31, "PANEL2 has 31 entries")
    local f = require("ui.font").open("FONTHALL")
    check(f and f:glyph("A") == 65, "FONTHALL is ASCII-indexed")
end)

test("[data] track NS11", function()
    if not data then skipped = skipped + 1; return "skip" end
    local track = require("formats.track")
    local T = track.open("NS11")
    check(#T.sections == 1527, "1527 sections")
    check(T.lap == 476, "lap of 476 sections")
    local layers = 0
    for i = 0, T.frames - 1, 37 do
        local c = T.video:chunks(i)
        layers = layers + #track.decode_kl(c and c.kl)
    end
    check(layers > 0, "occlusion masks decode")
end)

-- runner -------------------------------------------------------------------

game = {}
function game.init()
    for _, t in ipairs(tests) do
        local ok, err = pcall(t.fn)
        if not ok then
            failures = failures + 1
            print("ERROR in " .. t.name .. ": " .. tostring(err))
        end
    end
    print(string.format("%d tests, %d checks, %d failures, %d skipped (no MEGARACE_DATA)",
        #tests, checks, failures, skipped))
    sys.quit(failures == 0 and 0 or 1)
end
function game.update() end
function game.draw() end
