-- Car graphics from the player's MEGARACE.DAT.
--
-- Each of the eight cars has:
--   <XX>_A.HSQ  17 body images, from straight behind (0) round to head-on (16);
--               its palette sets colours 128..190
--   <XX>_B/_C   wheel frames drawn over the body, alternated to spin the wheels
--   T<XX>_2.HSQ the car's dashboard: two 160x48 halves (colours 72..111)
--   T<XX>.HSQ   dashboard gauge pieces
-- The track videos only use colours 1..64, so all of these can be on screen
-- together.

local M = {}
M.__index = M

-- Rival versions: <_XXX>.HSQ holds the 11 views from behind (all you see of a
-- car you're chasing) drawn with colours 192..255, i.e. the full car's
-- palette moved up by 64, so a rival can share the screen with the
-- player's car. They have no palette of their own and no wheel frames.
M.RIVAL_FILE = { JO = "_JOS", RA = "_RAM", OU = "_OUZ", PA = "_PAL",
                 LU = "_LUI", OM = "_OME", HO = "_HOO", MA = "_MAR" }
M.RIVAL_SHIFT = 64
M.RIVAL_FRAMES = 10   -- images 0..10

-- Car codes in the game's order (names from the enemy files I_<NAME>.HNM).
M.CODES = { "JO", "RA", "OU", "PA", "LU", "OM", "HO", "MA" }
M.NAMES = { JO = "JOSE", RA = "RAMON", OU = "OUZBEL", PA = "PALOMA",
            LU = "LUIS", OM = "OMEGA", HO = "HOOPER", MA = "MARIA" }

-- View angle -> body image, RACEPRG.EXE DS:0x93C. Index is |angle|; a
-- negative angle uses the mirrored image (RACEPRG 0x1B45).
M.ANGLE_FRAME = {
    0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,8,8,8,
    9,9,9,9,9,9,9,9,9,9, 10,10,10,10,10,10,10,10,10,10,
    11,11,11,11,11,11,11,11,11,11, 12,12,12,12,12,12,12,12,12,12,
    13,13,13,13,13,13,13,13,13,13, 14,14,14,14,14,14,14,14,14,
    15,15,15,15,15,15,15,15,15,15,15, 16,16,16,16,16,
}

-- Wheel overlays (RACEPRG 0x2594..0x25F5): the same image number from
-- <XX>_B or _C, centred on the body's x and level with its bottom, on every
-- view; only while moving (the body already shows the wheels standing still).

function M.open(code)
    local c = setmetatable({ code = code, name = M.NAMES[code] or code }, M)
    c.body   = assert(spr.open(code .. "_A"))
    c.wheels = { spr.open(code .. "_B"), spr.open(code .. "_C") }
    c.dash   = spr.open("T" .. code .. "_2")
    c.gauges = spr.open("T" .. code)
    return c
end

-- A rival car: same drawing interface, rear views only.
function M.open_rival(code)
    local c = setmetatable({ code = code, name = M.NAMES[code] or code, rival = true }, M)
    c.body = assert(spr.open(M.RIVAL_FILE[code]))
    c.palette_from = assert(spr.open(code .. "_A"))
    c.wheels = {}
    return c
end

function M:apply_palette()
    if self.rival then
        self.palette_from:apply_palette(M.RIVAL_SHIFT)
        return
    end
    self.body:apply_palette()
    for _, w in ipairs(self.wheels) do if w then w:apply_palette() end end
    if self.dash then self.dash:apply_palette() end
    if self.gauges then self.gauges:apply_palette() end
end

function M.frame_for(angle)
    local i = math.min(math.abs(angle), #M.ANGLE_FRAME - 1)
    return M.ANGLE_FRAME[math.floor(i) + 1], angle < 0
end

-- Draws the car with its bottom-centre at (x, y). `spin` alternates the
-- wheel frames (0 or 1); nil draws the body only. Returns the drawn box:
-- left, top, width, height.
function M:draw(angle, x, y, scale, spin)
    scale = scale or 1
    local f, flip = M.frame_for(angle)
    if self.rival then f = math.min(f, M.RIVAL_FRAMES) end
    local w, h = self.body:size(f)
    if not w then return end
    local left, top = x - w * scale / 2, y - h * scale
    self.body:draw(f, math.floor(left), math.floor(top), scale, flip)
    local wheel = spin and self.wheels[spin + 1]
    if wheel then
        local ww, wh = wheel:size(f)
        if ww then
            wheel:draw(f, math.floor(x - ww * scale / 2), math.floor(top + (h - wh) * scale), scale, flip)
        end
    end
    return left, top, w * scale, h * scale
end

-- DASHBOARD (Options > ADVANCED): HD draws the dashboard sets from their HD
-- packs (made by hd-conversion-utility) when there are some; ORIGINAL (or
-- no packs) the original pixels.
M.hd_dashboard = true

-- Points a set's draws at its HD pack, or not, as DASHBOARD says.
function M.dashboard_hd(set)
    if set and set.use_hd then set:use_hd(M.hd_dashboard) end
end

function M:draw_dashboard()
    M.dashboard_hd(self.gauges)   -- (drawn after this, by the race)
    if not self.dash then return end
    M.dashboard_hd(self.dash)
    local w0 = self.dash:size(0)
    self.dash:draw(0, 0, 152)
    if w0 then self.dash:draw(1, w0, 152) end
end

return M
