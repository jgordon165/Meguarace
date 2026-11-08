-- Sound effects that the ENHANCED music style replaces with its own,
-- synthesized here (no samples). Other styles play the game's own .VOC
-- effects.
--
--   shot        a plasma zap: an FM sweep down from 2.4 kHz on an inharmonic
--               ratio, doubled and detuned in stereo, a click, a sub thump,
--               a ring-mod zing, ping-pong echoes (three pitches take turns)
--   explosion   a sub boom, a roaring noise burst that darkens, crackle and a
--               falling digital swoosh, with a stereo tail      (EXPLOD2)
--   hit         an electric metallic impact: a crack, a ring-mod clang,
--               sparks                                           (CHOCTIR)
--   thud        a heavy armoured thud: a sub thump and a resonant clunk (CHOC4)
--   crash       a grinding metal crash: comb-filtered noise and a thump (CHOCRR2)
--   sparks      plasma sparks: crackles and fluttering hiss over a rising
--               whine                                            (CRRR3)
--   pickup      a shimmering FM-bell arpeggio, pitched per bonus (BONUS...)
--   powerup     a rising sweep into a chime                      (BONUS11)
--   energy      a warm rising hum and a chime                    (ENERGYP)
--   powerdown   a detuned buzz falling in pitch                  (MALUS..., PLUARME)
--
-- They're made on first use, or ahead: sfx.prepare_step() makes one at a
-- time (the race calls it during its countdown); sfx.play(name) plays the
-- game's effect by its .VOC name.

local voc = require("formats.voc")

local M = {}

local RATE = 44100
local TAU = 2 * math.pi
local sin, exp, abs, floor, min, max = math.sin, math.exp, math.abs, math.floor, math.min, math.max

-- ---- building blocks ---------------------------------------------------------

local function rng(seed)
    local s = seed
    return function()
        s = (s * 1103515245 + 12345) % 2147483648
        return s / 1073741824 - 1
    end
end

-- Renders dur seconds: fn(t, i) -> left, right. Echoes { {delay_s, gain, cross} }
-- feed back from the output (cross: to the other side). Peak-normalized to
-- `level`, packed as 16-bit stereo.
local function render(dur, fn, echoes, level)
    local n = floor(RATE * dur)
    local L, R = {}, {}
    for i = 1, n do
        local l, r = fn((i - 1) / RATE, i)
        L[i], R[i] = l, r
    end
    for _, e in ipairs(echoes or {}) do
        local d, g = floor(RATE * e[1]), e[2]
        for i = d + 1, n do
            if e[3] then L[i], R[i] = L[i] + g * R[i - d], R[i] + g * L[i - d]
            else L[i], R[i] = L[i] + g * L[i - d], R[i] + g * R[i - d] end
        end
    end
    local peak = 1e-6
    for i = 1, n do peak = max(peak, abs(L[i]), abs(R[i])) end
    local k = (level or 0.8) * 32767 / peak
    local out = {}
    for i = 1, n do out[i] = string.pack("<i2i2", floor(L[i] * k + 0.5), floor(R[i] * k + 0.5)) end
    return table.concat(out)
end

-- One-pole low-pass with a cutoff that can change per sample.
local function lowpass()
    local y = 0
    return function(x, hz)
        local a = 1 - exp(-TAU * hz / RATE)
        y = y + a * (x - y)
        return y
    end
end

local function highpass()
    local lp = lowpass()
    return function(x, hz) return x - lp(x, hz) end
end

-- A two-pole resonant band-pass (state-variable filter).
local function bandpass(q)
    local lo, bp = 0, 0
    return function(x, hz)
        local f = 2 * sin(math.pi * min(hz, RATE / 6) / RATE)
        lo = lo + f * bp
        local hi = x - lo - bp / q
        bp = bp + f * hi
        return bp
    end
end

local function env(t, attack, decay) return min(1, t / attack) * exp(-t / decay) end

-- ---- the effects -----------------------------------------------------------

local function zap(pitch, seed)
    local noise = rng(seed)
    local ph1, ph2, mph1, mph2, zph, sph = 0, 0, 0, 0, 0, 0
    local hp = highpass()
    return render(0.62, function(t)
        if t > 0.34 then return 0, 0 end
        local f = pitch * 2400 * (170 / 2400) ^ min(1, t / 0.28)
        local index = 0.5 + 6 * exp(-t / 0.05)
        local e = env(t, 0.002, 0.09)
        mph1 = mph1 + TAU * f * 1.414 / RATE
        mph2 = mph2 + TAU * f * 1.41 * 1.012 / RATE
        ph1 = ph1 + TAU * f / RATE
        ph2 = ph2 + TAU * f * 1.012 / RATE
        local v1 = sin(ph1 + index * sin(mph1))
        local v2 = sin(ph2 + index * sin(mph2))
        zph = zph + TAU * 3600 * pitch / RATE
        local zing = sin(zph) * sin(ph1 * 0.5) * exp(-t / 0.02)
        local click = hp(noise(), 2500) * exp(-t / 0.006)
        sph = sph + TAU * (40 + 50 * exp(-t / 0.02)) / RATE
        local thump = sin(sph) * exp(-t / 0.035)
        local mid = 0.18 * zing + 0.2 * click + 0.6 * thump
        return e * (0.62 * v1 + 0.25 * v2) + mid, e * (0.25 * v1 + 0.62 * v2) + mid
    end, { { 0.085, 0.32, true }, { 0.17, 0.16 } })
end

local function explosion()
    local nl, nr = rng(71), rng(72)
    local lpl, lpr = lowpass(), lowpass()
    local sph, swph = 0, 0
    local crack = rng(73)
    return render(1.7, function(t)
        -- The boom: a sine falling from 70 to 28 Hz.
        sph = sph + TAU * (28 + 42 * exp(-t / 0.12)) / RATE
        local boom = sin(sph) * env(t, 0.004, 0.45)
        -- The roar: noise whose low-pass closes from 6 kHz to 250 Hz.
        local cut = 250 + 5800 * exp(-t / 0.18)
        local roar = env(t, 0.003, 0.5)
        local rl, rr = lpl(nl(), cut) * roar, lpr(nr(), cut * 0.9) * roar
        -- Crackle: sparse sharp clicks, fading.
        local c = crack()
        local crackle = (abs(c) > 0.985 and c or 0) * exp(-t / 0.4) * 2.2
        -- The swoosh: a digital tone sweeping down.
        swph = swph + TAU * (1800 * exp(-t / 0.25) + 60) / RATE
        local sw = sin(swph + 3 * sin(swph * 0.5)) * env(t, 0.01, 0.2) * 0.25
        return 1.1 * boom + 1.6 * rl + crackle + sw, 1.1 * boom + 1.6 * rr - crackle * 0.6 + sw
    end, { { 0.11, 0.35, true }, { 0.23, 0.2 } }, 0.85)
end

local function hit()
    local noise, sp = rng(81), rng(82)
    local hp = highpass()
    local a, b = 0, 0
    return render(0.42, function(t)
        local crack = hp(noise(), 3000) * env(t, 0.0005, 0.012)
        a = a + TAU * 1230 / RATE
        b = b + TAU * 1710 / RATE
        local clang = sin(a) * sin(b) * env(t, 0.001, 0.08) + sin(a * 2.76) * env(t, 0.001, 0.04) * 0.4
        local s = sp()
        local sparks = (abs(s) > 0.97 and s or 0) * exp(-t / 0.12)
        return 0.9 * crack + 0.8 * clang + sparks, 0.9 * crack + 0.7 * clang - sparks
    end, { { 0.06, 0.25, true } })
end

local function thud()
    local noise = rng(91)
    local bp, lp = bandpass(9), lowpass()
    local ph = 0
    return render(0.45, function(t)
        ph = ph + TAU * (45 + 45 * exp(-t / 0.03)) / RATE
        local thump = sin(ph) * env(t, 0.002, 0.09)
        local n = noise()
        local clunk = bp(n, 380) * env(t, 0.001, 0.07) * 3
        local body = lp(n, 900) * env(t, 0.001, 0.03)
        local v = 1.2 * thump + clunk + body
        return v, v * 0.92
    end, { { 0.045, 0.2, true } })
end

local function crash()
    local nl, nr = rng(101), rng(102)
    local combL, combR, pl, pr = {}, {}, 0, 0
    local dl, dr = floor(RATE / 230), floor(RATE / 310)        -- metal resonances
    for i = 1, dl do combL[i] = 0 end
    for i = 1, dr do combR[i] = 0 end
    local ph = 0
    local hp = highpass()
    return render(0.75, function(t)
        local e = env(t, 0.002, 0.18)
        local grind = 1 + 0.6 * sin(TAU * 31 * t)                 -- a rough grind
        local x = hp(nl(), 600) * e * grind
        pl = pl % dl + 1
        local yl = x + 0.75 * combL[pl]; combL[pl] = yl
        local xr = hp(nr(), 600) * e * grind
        pr = pr % dr + 1
        local yr = xr + 0.75 * combR[pr]; combR[pr] = yr
        ph = ph + TAU * (50 + 40 * exp(-t / 0.03)) / RATE
        local thump = sin(ph) * env(t, 0.002, 0.08) * 1.4
        return 0.5 * yl + thump, 0.5 * yr + thump
    end)
end

local function sparks()
    local noise, cr = rng(111), rng(112)
    local hp = highpass()
    local ph = 0
    return render(0.8, function(t)
        local e = env(t, 0.01, 0.3)
        local flutter = 0.5 + 0.5 * sin(TAU * 23 * t + 3 * sin(TAU * 7 * t))
        local hiss = hp(noise(), 4000) * e * flutter * 0.6
        local c = cr()
        local crackle = (abs(c) > 0.96 and c or 0) * e * 1.5
        ph = ph + TAU * (2000 + 2500 * t) / RATE
        local whine = sin(ph) * e * 0.12
        return hiss + crackle + whine, hiss - crackle * 0.7 + whine
    end, { { 0.07, 0.25, true } })
end

-- FM bell notes: { {start_s, freq}, ... }.
local function bells(notes, dur, bright)
    local ph, mph = {}, {}
    for i = 1, #notes do ph[i], mph[i] = 0, 0 end
    return render(dur, function(t)
        local l, r = 0, 0
        for i, nt in ipairs(notes) do
            local tt = t - nt[1]
            if tt >= 0 then
                local f = nt[2]
                ph[i] = ph[i] + TAU * f / RATE
                mph[i] = mph[i] + TAU * f * 3.5 / RATE           -- bell-like ratio
                local v = sin(ph[i] + bright * exp(-tt / 0.15) * sin(mph[i])) * env(tt, 0.002, 0.35)
                local pan = (i % 2 == 0) and 0.7 or 0.3
                l, r = l + v * (1 - pan), r + v * pan
            end
        end
        return l, r
    end, { { 0.12, 0.3, true }, { 0.24, 0.15 } })
end

local function pickup(root)
    local f = function(semi) return root * 2 ^ (semi / 12) end
    return bells({ { 0, f(0) }, { 0.06, f(4) }, { 0.12, f(7) }, { 0.18, f(12) } }, 0.9, 2.5)
end

local function powerup()
    local ph, mph = 0, 0
    local chime = 0
    return render(1.0, function(t)
        local f = 220 * 2 ^ (min(t, 0.5) / 0.5 * 2.5)            -- up 2.5 octaves in half a second
        ph = ph + TAU * f / RATE
        mph = mph + TAU * f * 2.01 / RATE
        local sweep = sin(ph + 1.5 * sin(mph)) * (t < 0.5 and min(1, t / 0.05) or exp(-(t - 0.5) / 0.12)) * 0.7
        local c = 0
        if t > 0.5 then
            chime = chime + TAU * 1760 / RATE
            c = sin(chime + 2 * exp(-(t - 0.5) / 0.1) * sin(chime * 3.5)) * env(t - 0.5, 0.002, 0.25)
        end
        local v = sweep + c
        return v * (0.6 + 0.4 * sin(TAU * 3 * t)), v * (0.6 - 0.4 * sin(TAU * 3 * t))
    end, { { 0.1, 0.3, true } })
end

local function energy()
    local p1, p2, chime = 0, 0, 0
    return render(0.9, function(t)
        local f = 110 * 2 ^ (min(t, 0.45) / 0.45)                -- up an octave
        p1 = p1 + TAU * f / RATE
        p2 = p2 + TAU * f * 1.006 / RATE
        local e = min(1, t / 0.05) * (t < 0.45 and 1 or exp(-(t - 0.45) / 0.15))
        local hum = (sin(p1) + 0.5 * sin(2 * p1) + 0.3 * sin(3 * p2)) * e * 0.5
        local c = 0
        if t > 0.4 then
            chime = chime + TAU * 1320 / RATE
            c = sin(chime + 1.5 * sin(chime * 2)) * env(t - 0.4, 0.002, 0.25) * 0.6
        end
        return hum + c, hum * 0.9 + c
    end, { { 0.09, 0.25, true } })
end

local function powerdown()
    local p1, p2, p3 = 0, 0, 0
    return render(0.75, function(t)
        local f = 520 * (60 / 520) ^ min(1, t / 0.6)             -- falling
        local wob = 1 + 0.03 * sin(TAU * 9 * t)
        p1 = p1 + TAU * f * wob / RATE
        p2 = p2 + TAU * f * 1.02 / RATE
        p3 = p3 + TAU * f * 0.98 / RATE
        local e = env(t, 0.005, 0.3)
        -- A buzzy, saw-like tone from FM, detuned three ways.
        local v1, v2, v3 = sin(p1 + 2.2 * sin(p1)), sin(p2 + 2.2 * sin(p2)), sin(p3 + 2.2 * sin(p3))
        return e * (0.6 * v1 + 0.5 * v2), e * (0.6 * v1 + 0.5 * v3)
    end)
end

-- ---- which sound for which effect --------------------------------------------

local MAKERS = {
    explosion = explosion, hit = hit, thud = thud, crash = crash, sparks = sparks,
    powerup = powerup, energy = energy, powerdown = powerdown,
    pickup_lo = function() return pickup(784) end,               -- G5
    pickup_mid = function() return pickup(988) end,              -- B5
    pickup_hi = function() return pickup(1175) end,              -- D6
}

-- The game's .VOC names -> the ENHANCED sound.
M.REPLACES = {
    EXPLOD2 = "explosion", CHOCTIR = "hit", CHOC4 = "thud", CHOCRR2 = "crash", CRRR3 = "sparks",
    BONUS11 = "powerup", ENERGYP = "energy",
    BONUS1 = "pickup_mid", BONUS10 = "pickup_lo", BONUS14 = "pickup_hi", BONUS15 = "pickup_mid",
    BONUS16 = "pickup_hi",
    MALUS2 = "powerdown", MALUS6 = "powerdown", MALUS7 = "powerdown", MALUS9 = "powerdown",
    MALUS11 = "powerdown", PLUARME = "powerdown",
}

-- How loud each plays (they're all made to the same peak).
local LEVEL = {
    explosion = 1.0, crash = 0.85, thud = 0.85, hit = 0.75, sparks = 0.5,
    powerup = 0.7, energy = 0.65, powerdown = 0.6, pickup_lo = 0.55, pickup_mid = 0.55, pickup_hi = 0.55,
}

local made = {}
local shots, turn = nil, 0

local function enhanced() return audio.music_style() == "enhanced" end

-- The pre-made zaps (made on first use).
function M.shot_sounds()
    if not shots then
        shots = {}
        for v = 1, 3 do shots[v] = zap(({ 1.0, 0.94, 1.06 })[v], 1234 + v) end
    end
    return shots
end

-- One sound by its ENHANCED name ("explosion"...), made once.
function M.sound(name)
    if made[name] == nil then
        local f = MAKERS[name]
        made[name] = f and f() or false
    end
    return made[name] or nil
end

-- Makes one ENHANCED sound not made yet; true when all are (or the style is
-- off). The race calls it each frame of its countdown, so no frame stalls.
function M.prepare_step()
    if not enhanced() then return true end
    if not shots then M.shot_sounds(); return false end
    for name in pairs(MAKERS) do
        if made[name] == nil then M.sound(name); return false end
    end
    return true
end

-- Makes every ENHANCED sound now (only in the ENHANCED style).
function M.prepare()
    if not enhanced() then return end
    M.shot_sounds()
    for name in pairs(MAKERS) do M.sound(name) end
end

-- Plays the game's effect by its .VOC name: the ENHANCED sound if the style
-- is on and there is one, else the game's own.
function M.play(name, volume)
    local mine = enhanced() and M.REPLACES[name]
    local pcm = mine and M.sound(mine)
    if not pcm then return voc.play(name, volume) end
    return audio.pcm(pcm, RATE, 16, 2, (volume or 1.0) * (LEVEL[mine] or 0.8))
end

-- A shot: the zap in the ENHANCED style, else the game's LASER.
function M.shot(volume)
    if not enhanced() then return voc.play("LASER", volume) end
    local s = M.shot_sounds()
    turn = turn % #s + 1
    return audio.pcm(s[turn], RATE, 16, 2, (volume or 1.0) * 0.8)
end

return M
