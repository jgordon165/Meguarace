-- Sound effects that the ENHANCED music style replaces with its own,
-- synthesized here (no samples): the shot is a plasma zap. An FM voice on an
-- inharmonic ratio sweeps down from 2.4 kHz with its brightness falling,
-- doubled and detuned across the stereo field. A noise click and a sub
-- thump give it its attack, a metallic ring-mod zing its sparkle, and two
-- ping-pong echoes its tail. Three slightly different pitches take turns,
-- so a volley isn't one sound repeated. Other styles play the game's LASER.

local voc = require("formats.voc")

local M = {}

local RATE = 44100
local VARIANTS = 3

local function synth_shot(pitch, seed)
    local n = math.floor(RATE * 0.62)
    local L, R = {}, {}
    for i = 1, n do L[i], R[i] = 0, 0 end
    local rnd = seed
    local function noise()
        rnd = (rnd * 1103515245 + 12345) % 2147483648
        return rnd / 1073741824 - 1
    end
    local tau = 2 * math.pi
    local ph1, ph2, mph1, mph2, zph, sph = 0, 0, 0, 0, 0, 0
    local hp_prev, hp_out = 0, 0
    local body = math.floor(RATE * 0.34)
    for i = 0, body - 1 do
        local t = i / RATE
        -- The sweep: exponential from 2.4 kHz to 170 Hz.
        local f = pitch * 2400 * (170 / 2400) ^ math.min(1, t / 0.28)
        local index = 0.5 + 6 * math.exp(-t / 0.05)               -- bright at first
        local env = math.min(1, t / 0.002) * math.exp(-t / 0.09)
        mph1 = mph1 + tau * f * 1.414 / RATE
        mph2 = mph2 + tau * f * 1.41 * 1.012 / RATE
        ph1 = ph1 + tau * f / RATE
        ph2 = ph2 + tau * f * 1.012 / RATE                         -- detuned double
        local v1 = math.sin(ph1 + index * math.sin(mph1))
        local v2 = math.sin(ph2 + index * math.sin(mph2))
        -- The zing: a ring-modulated 3.6 kHz shimmer, gone in 60 ms.
        zph = zph + tau * 3600 * pitch / RATE
        local zing = math.sin(zph) * math.sin(ph1 * 0.5) * math.exp(-t / 0.02)
        -- Attack: a high-passed noise click (6 ms) and a sub thump (90 -> 40 Hz).
        local nz = noise()
        hp_out = 0.85 * (hp_out + nz - hp_prev)
        hp_prev = nz
        local click = hp_out * math.exp(-t / 0.006)
        sph = sph + tau * (40 + 50 * math.exp(-t / 0.02)) / RATE
        local thump = math.sin(sph) * math.exp(-t / 0.035)
        local mid = 0.18 * zing + 0.2 * click + 0.6 * thump
        L[i + 1] = L[i + 1] + env * (0.62 * v1 + 0.25 * v2) + mid
        R[i + 1] = R[i + 1] + env * (0.25 * v1 + 0.62 * v2) + mid
    end
    -- Two echoes, alternating sides.
    local d1, d2 = math.floor(RATE * 0.085), math.floor(RATE * 0.17)
    for i = n, 1, -1 do
        if i > d1 then R[i] = R[i] + 0.32 * L[i - d1] end
        if i > d2 then L[i] = L[i] + 0.16 * L[i - d2] end
    end
    local peak = 1e-6
    for i = 1, n do peak = math.max(peak, math.abs(L[i]), math.abs(R[i])) end
    local k = 0.8 * 32767 / peak
    local out = {}
    for i = 1, n do
        out[i] = string.pack("<i2i2", math.floor(L[i] * k + 0.5), math.floor(R[i] * k + 0.5))
    end
    return table.concat(out)
end

local shots, turn = nil, 0

-- The pre-made zaps (made on first use: about a tenth of a second).
function M.shot_sounds()
    if not shots then
        shots = {}
        for v = 1, VARIANTS do shots[v] = synth_shot(({ 1.0, 0.94, 1.06 })[v], 1234 + v) end
    end
    return shots
end

-- A shot: the zap in the ENHANCED style, else the game's LASER.
function M.shot(volume)
    if audio.music_style() ~= "enhanced" then return voc.play("LASER", volume) end
    local s = M.shot_sounds()
    turn = turn % #s + 1
    return audio.pcm(s[turn], RATE, 16, 2, (volume or 1.0) * 0.8)
end

return M
