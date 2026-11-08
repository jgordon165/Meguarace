-- The port's title screen, before the boot videos: MEGARACE in chrome over
-- a synthwave night. A striped sun sets behind a neon skyline, a grid road
-- scrolls toward you with a hover car on it, rain falls and the title
-- glitches now and then. Everything is drawn here by code in its own
-- palette (no game art); the music plays in the chosen style.
--
--   scenes.push(require("scenes.title"), { on_done = function(skip_all) ... end })
--
-- ENTER/SPACE go on to the boot videos (on_done(false)); ESC skips those
-- too (on_done(true)). It goes on by itself after a while.

local scenes = require("core.scenes")
local music  = require("game.music")

local S = {}

local HORIZON = 112
local SECONDS = 14            -- then on to the videos by itself

-- Palette ranges.
local SKY, SKY_N = 1, 48
local SUN, SUN_N = 49, 32
local FAR, NEAR, RIM_PINK, RIM_CYAN = 81, 82, 83, 84
local AMBER, CYAN, AMBER_DIM, NEON, NEON_GLOW, CYAN_GLOW = 97, 98, 99, 100, 101, 102
local GROUND, GROUND_N = 113, 32
local GRID, GRID_N = 145, 16
local CHROME, CHROME_N = 161, 32
local OUTLINE, SHADOW = 193, 194
local ROAD, EDGE, DASH, BODY, BODY_HI, TAIL, TAIL_GLOW, UNDERGLOW, GLASS = 201, 202, 203, 204, 205, 206, 207, 208, 209
local RAIN, WHITE, STAR = 211, 212, 213

local function lerp(a, b, t) return a + (b - a) * t end
local function ramp(stops, t)            -- stops: { {t, r, g, b}, ... }
    for i = 1, #stops - 1 do
        local a, b = stops[i], stops[i + 1]
        if t <= b[1] then
            local k = (t - a[1]) / math.max(1e-6, b[1] - a[1])
            return lerp(a[2], b[2], k), lerp(a[3], b[3], k), lerp(a[4], b[4], k)
        end
    end
    local z = stops[#stops]
    return z[2], z[3], z[4]
end

local function make_palette()
    local pal = {}
    for i = 0, 255 do pal[i] = { 0, 0, 0 } end
    local function fill(first, n, stops)
        for i = 0, n - 1 do pal[first + i] = { ramp(stops, i / (n - 1)) } end
    end
    fill(SKY, SKY_N, { { 0, 6, 3, 20 }, { 0.55, 50, 8, 75 }, { 0.85, 170, 30, 120 }, { 1, 255, 80, 140 } })
    fill(SUN, SUN_N, { { 0, 255, 235, 100 }, { 0.5, 255, 140, 70 }, { 1, 255, 40, 150 } })
    pal[FAR], pal[NEAR] = { 70, 22, 90 }, { 12, 6, 28 }
    pal[RIM_PINK], pal[RIM_CYAN] = { 200, 40, 160 }, { 40, 200, 230 }
    pal[AMBER], pal[CYAN], pal[AMBER_DIM] = { 255, 190, 70 }, { 90, 235, 255 }, { 120, 80, 40 }
    pal[NEON], pal[NEON_GLOW], pal[CYAN_GLOW] = { 255, 60, 200 }, { 110, 20, 90 }, { 20, 90, 110 }
    fill(GROUND, GROUND_N, { { 0, 45, 6, 55 }, { 1, 6, 0, 10 } })
    fill(GRID, GRID_N, { { 0, 70, 6, 70 }, { 1, 255, 70, 225 } })
    fill(CHROME, CHROME_N, { { 0, 235, 255, 255 }, { 0.3, 120, 200, 255 }, { 0.52, 30, 50, 190 },
                             { 0.56, 255, 255, 255 }, { 0.7, 255, 150, 70 }, { 1, 255, 40, 150 } })
    pal[OUTLINE], pal[SHADOW] = { 0, 255, 255 }, { 170, 0, 110 }
    pal[ROAD], pal[EDGE], pal[DASH] = { 14, 8, 22 }, { 40, 240, 255 }, { 255, 255, 255 }
    pal[BODY], pal[BODY_HI], pal[GLASS] = { 36, 34, 64 }, { 110, 105, 170 }, { 10, 12, 30 }
    pal[TAIL], pal[TAIL_GLOW], pal[UNDERGLOW] = { 255, 30, 70 }, { 130, 10, 45 }, { 0, 190, 230 }
    pal[RAIN], pal[WHITE], pal[STAR] = { 140, 210, 255 }, { 255, 255, 255 }, { 210, 210, 255 }
    local bytes = {}
    for i = 0, 255 do
        local c = pal[i]
        bytes[#bytes + 1] = string.char(math.floor(c[1] + 0.5), math.floor(c[2] + 0.5), math.floor(c[3] + 0.5))
    end
    gfx.palette(table.concat(bytes), 0, false)
end

-- 7x9 letters, futuristic and blocky.
local GLYPHS = {
    M = { "11000011", "11100111", "11111111", "11011011", "11000011", "11000011", "11000011", "11000011", "11000011" },
    E = { "11111111", "11111111", "11000000", "11111100", "11111100", "11000000", "11000000", "11111111", "11111111" },
    G = { "01111111", "11111111", "11000000", "11000000", "11001111", "11000011", "11000011", "11111111", "01111110" },
    A = { "00111100", "01111110", "11100111", "11000011", "11111111", "11111111", "11000011", "11000011", "11000011" },
    R = { "11111110", "11111111", "11000011", "11000011", "11111110", "11111100", "11001110", "11000111", "11000011" },
    C = { "01111111", "11111111", "11000000", "11000000", "11000000", "11000000", "11000000", "11111111", "01111111" },
}
local SCALE, GAP = 4, 3

-- The title, pre-drawn: rows of palette indices (0: transparent).
local function make_title(word)
    local gw, gh = 8 * SCALE, 9 * SCALE
    local shear = gh // 3                      -- italic: the top leans right
    local w = #word * (gw + GAP) - GAP + shear + 6
    local h = gh + 6
    local px = {}
    for i = 1, w * h do px[i] = 0 end
    local solid = {}
    for li = 1, #word do
        local g = GLYPHS[word:sub(li, li)]
        local ox = (li - 1) * (gw + GAP)
        for gy = 0, gh - 1 do
            local row = g[gy // SCALE + 1]
            local lean = (gh - 1 - gy) // 3
            for gx = 0, gw - 1 do
                if row:sub(gx // SCALE + 1, gx // SCALE + 1) == "1" then
                    solid[(gy + 1) * w + ox + gx + lean + 2] = true
                end
            end
        end
    end
    local function at(i) return solid[i] end
    -- Shadow (down-right), outline, then the chrome face by row.
    for i in pairs(solid) do
        local s = i + 3 * w + 3
        if s <= w * h and not at(s) then px[s] = SHADOW end
    end
    for i in pairs(solid) do
        for _, d in ipairs({ -1, 1, -w, w }) do
            local n = i + d
            if n >= 1 and n <= w * h and not at(n) then px[n] = OUTLINE end
        end
    end
    for i in pairs(solid) do
        local y = (i - 1) // w - 1
        px[i] = CHROME + math.max(0, math.min(CHROME_N - 1, y * CHROME_N // gh))
    end
    local rows = {}
    for y = 0, h - 1 do
        local r = {}
        for x = 1, w do r[x] = string.char(px[y * w + x]) end
        rows[y + 1] = table.concat(r)
    end
    return { w = w, h = h, rows = rows }
end

-- The still background: sky, stars, sun, skyline, ground (W x 200).
local function make_background(W, x0)
    local rnd = 12345
    local function rand()
        rnd = (rnd * 1103515245 + 12345) % 2147483648
        return rnd / 2147483648
    end
    local px = {}
    local function put(x, y, c)
        local X = x - x0
        if X >= 0 and X < W and y >= 0 and y < 200 then px[y * W + X + 1] = c end
    end
    for y = 0, 199 do
        local c = y < HORIZON and SKY + math.min(SKY_N - 1, math.floor((y / HORIZON) ^ 1.6 * SKY_N))
                  or GROUND + math.min(GROUND_N - 1, (y - HORIZON) * GROUND_N // (200 - HORIZON))
        for X = 0, W - 1 do px[y * W + X + 1] = c end
    end
    for _ = 1, 70 do put(x0 + math.floor(rand() * W), math.floor(rand() * 60), STAR) end
    -- The sun: a disc, its lower part cut by bands that widen downward.
    local cx, r = 160, 46
    for y = HORIZON - r, HORIZON - 1 do
        local dy = HORIZON - 1 - y                       -- 0 at the horizon
        local band = dy < 26 and (dy % 7) < (26 - dy) // 6
        if not band then
            local half = math.floor(math.sqrt(math.max(0, r * r - (y - HORIZON + 4) ^ 2)))
            local c = SUN + math.min(SUN_N - 1, (y - (HORIZON - r)) * SUN_N // r)
            for x = cx - half, cx + half do put(x, y, c) end
        end
    end
    -- Skyline: a hazy far row, then a near row with windows and neon.
    for layer = 1, 2 do
        local x = x0 - math.floor(rand() * 10)
        while x < x0 + W do
            local bw = 8 + math.floor(rand() * 18)
            local bh = layer == 1 and 18 + math.floor(rand() * 34) or 8 + math.floor(rand() * 26)
            if math.abs(x + bw / 2 - 160) < 44 then bh = math.floor(bh * (layer == 1 and 0.3 or 0.45)) end   -- low before the sun
            for y = HORIZON - bh, HORIZON - 1 do
                for xx = x, x + bw - 1 do
                    local c = layer == 1 and FAR or NEAR
                    if layer == 2 and (xx - x) % 3 == 1 and (y - HORIZON + bh) % 4 == 2 and rand() < 0.35 then
                        c = rand() < 0.7 and AMBER or (rand() < 0.5 and CYAN or AMBER_DIM)
                    end
                    if layer == 2 and (xx == x or xx == x + bw - 1) then c = (x % 2 == 0) and RIM_PINK or RIM_CYAN end
                    put(xx, y, c)
                end
            end
            if layer == 1 and rand() < 0.4 then                   -- an antenna
                for y = HORIZON - bh - 6, HORIZON - bh - 1 do put(x + bw // 2, y, FAR) end
            end
            if layer == 2 and rand() < 0.35 and bh > 14 then       -- a vertical neon sign
                local sx = x + bw // 2
                for y = HORIZON - bh + 3, HORIZON - bh + 3 + math.min(12, bh - 6) do
                    put(sx - 1, y, NEON_GLOW); put(sx, y, NEON); put(sx + 1, y, NEON_GLOW)
                end
            end
            x = x + bw + math.floor(rand() * 3)
        end
    end
    local rows = {}
    for y = 0, 199 do
        local r = {}
        for X = 1, W do r[X] = string.char(px[y * W + X]) end
        rows[#rows + 1] = table.concat(r)
    end
    return table.concat(rows)
end

function S:enter(opts)
    self.opts = opts or {}
    self.t = 0
    self.glitch = 0
    make_palette()
    self.wing = gfx.wing()
    self.W = 320 + 2 * self.wing
    self.bg = make_background(self.W, -self.wing)
    self.title = make_title("MEGARACE")
    self.drops = {}
    for i = 1, 70 do
        self.drops[i] = { x = math.random() * (self.W + 40) - self.wing, y = math.random() * 200, v = 120 + math.random() * 80 }
    end
    music.play(1)
end

function S:resume() make_palette() end

function S:finish(skip_all)
    if self.done then return end
    self.done = true
    music.stop(true)
    scenes.pop()
    if self.opts.on_done then self.opts.on_done(skip_all) end
end

function S:update(dt)
    self.t = self.t + dt
    if input.pressed("Escape") then return self:finish(true) end
    if input.pressed("Return") or input.pressed("Space") or self.t > SECONDS then return self:finish(false) end
    -- Glitch: now and then, a few frames of torn title.
    if self.glitch > 0 then self.glitch = self.glitch - dt
    elseif math.random() < dt * 0.35 then self.glitch = 0.12 + math.random() * 0.1 end
    -- The neon sign flickers.
    if math.random() < dt * 2 then gfx.setcolor(NEON, 120, 30, 100) else gfx.setcolor(NEON, 255, 60, 200) end
    for _, d in ipairs(self.drops) do
        d.y = d.y + d.v * dt
        d.x = d.x - d.v * dt * 0.2
        if d.y > 200 then d.y = d.y - 210; d.x = math.random() * (self.W + 40) - self.wing end
    end
end

-- Depth 0 (at the bottom of the screen) .. large (at the horizon) -> row.
local function row_at(d) return HORIZON + math.floor(87 * 1.2 / (d + 1.2)) end

function S:draw()
    local x0, W = -self.wing, self.W
    gfx.blit(x0, 0, W, 200, self.bg)
    local phase = (self.t * 1.6) % 1
    -- The grid: lines across, coming toward you, and lines to the vanishing point.
    for n = 0, 22 do
        local d = n - phase
        if d > 0 then
            local y = row_at(d)
            local c = GRID + math.max(0, math.min(GRID_N - 1, math.floor(GRID_N * (1 - d / 23))))
            gfx.line(x0, y, x0 + W - 1, y, c)
        end
    end
    for k = -16, 16 do
        gfx.line(160, HORIZON, 160 + k * 34, 199, GRID + GRID_N - 4)
    end
    -- The road, edges and centre dashes.
    for y = HORIZON + 1, 199 do
        local half = math.floor((y - HORIZON) * 52 / 87)
        gfx.line(160 - half, y, 160 + half, y, ROAD)
        gfx.pset(160 - half, y, EDGE)
        gfx.pset(160 + half, y, EDGE)
    end
    for n = 0, 14 do
        local d = n * 1.5 - (self.t * 2.4) % 1.5
        if d > 0.05 then
            local y1, y2 = row_at(d + 0.55), row_at(d)
            if y2 > y1 then gfx.fill(160, y1, 1, y2 - y1, DASH) end
        end
    end
    -- The car, from behind, bobbing a little.
    local bob = math.floor(math.sin(self.t * 5) * 1.2 + 0.5)
    local cy = 166 + bob
    for x = 128, 192 do gfx.pset(x, cy + 24, UNDERGLOW) end
    gfx.fill(134, cy + 25, 52, 2, UNDERGLOW)
    gfx.fill(124, cy - 9, 72, 3, BODY_HI)                    -- spoiler
    gfx.fill(130, cy - 6, 3, 8, BODY); gfx.fill(187, cy - 6, 3, 8, BODY)
    gfx.fill(140, cy - 4, 40, 8, GLASS)                      -- cabin
    gfx.line(140, cy - 4, 179, cy - 4, BODY_HI)
    gfx.fill(126, cy + 4, 68, 18, BODY)                      -- body
    gfx.line(126, cy + 4, 193, cy + 4, BODY_HI)
    gfx.fill(129, cy + 9, 22, 3, TAIL); gfx.fill(169, cy + 9, 22, 3, TAIL)
    gfx.fill(129, cy + 12, 22, 1, TAIL_GLOW); gfx.fill(169, cy + 12, 22, 1, TAIL_GLOW)
    gfx.fill(152, cy + 14, 16, 4, GLASS)                     -- exhaust
    -- Rain.
    for _, d in ipairs(self.drops) do
        local x, y = math.floor(d.x), math.floor(d.y)
        gfx.line(x, y, x - 1, y + 4, RAIN)
    end
    -- The title, torn sideways while it glitches.
    local T = self.title
    local tx, ty = 160 - T.w // 2, 18
    for r = 1, T.h do
        local shift = 0
        if self.glitch > 0 and (r + math.floor(self.t * 60)) % 7 < 2 then shift = math.random(-6, 6) end
        gfx.blit(tx + shift, ty + r - 1, T.w, 1, T.rows[r], 0)
    end
    if math.floor(self.t * 2) % 2 == 0 then
        local s = "PRESS ENTER"
        gfx.fill(160 - #s * 2 - 4, 63, #s * 4 + 7, 9, ROAD)
        gfx.text(160 - #s * 2, 65, s, WHITE)
    end
end

return S
