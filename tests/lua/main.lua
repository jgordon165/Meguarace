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
    local s = rules.new()
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

test("rules: rivals gain or drop back by speed difference", function()
    local rules = require("race.rules")
    check(rules.rival_relative(12.5, 180, 150) < 0, "faster player: rival drops back")
    check(rules.rival_relative(12.5, 100, 150) > 0, "slower player: rival pulls away")
    check(rules.rival_relative(12.5, 150, 150) == 0, "same speed: no change")
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
