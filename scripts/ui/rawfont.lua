-- The race's status fonts: FONTE1.RAW (digits), FONTE2.RAW (':' and up:
-- letters) and FONTE6_6.RAW (small digits). Each is a 320-pixel-wide strip,
-- one glyph after another (RACEPRG 0x1600):
--   FONTE1/FONTE2  8 x 12 glyphs; glyph k starts at column 8k - 1 (k > 0),
--                  FONTE2's glyph is (character - ':')
--   FONTE6_6       6 x 6 glyphs at column 6k
-- Pixels are colours 79 and 255. Text over the dashboard (y >= 152) clears
-- the glyph box to black first; over the picture it is transparent.

local M = {}

local STRIDE = 320
local cache = {}

local function load(name)
    if cache[name] == nil then
        local d = dat.exists(name) and dat.read(name, false)
        cache[name] = d or false
    end
    return cache[name] or nil
end

-- `white` replaces colour 255 (which belongs to the rival car's palette in
-- this port); 79 is a dashboard colour and stays.
local glyphs = {}
local function glyph(file, k, w, h, offset, white)
    local key = file .. ":" .. k .. ":" .. white
    local g = glyphs[key]
    if g == nil then
        local d = load(file)
        g = false
        if d then
            local col = offset(k)
            local px = {}
            for y = 0, h - 1 do
                for x = 0, w - 1 do
                    local i = y * STRIDE + col + x + 1
                    local c = (i >= 1 and i <= #d) and d:byte(i) or 0
                    if c == 255 then c = white end
                    px[#px + 1] = string.char(c)
                end
            end
            g = table.concat(px)
        end
        glyphs[key] = g
    end
    return g or nil
end

local big = function(k) return k > 0 and 8 * k - 1 or 0 end
local small = function(k) return 6 * k end

-- Draws text in the 8 x 12 fonts, 10 pixels per character (RACEPRG 0x15C3).
function M.draw(x, y, text, white)
    white = white or 15
    local opaque = y >= 152
    for i = 1, #text do
        local b = text:byte(i)
        if b ~= 32 then
            local file, k
            if b >= 48 and b < 58 then file, k = "FONTE1.RAW", b - 48
            else file, k = "FONTE2.RAW", math.max(0, b - 58) end
            local g = glyph(file, k, 8, 12, big, white)
            if g then gfx.blit(x, y, 8, 12, g, opaque and -1 or 0) end
        end
        x = x + 10
    end
end

-- Small digits, 6 pixels apart (RACEPRG 0x159C with the 6 x 6 font).
function M.draw_small(x, y, digits, white)
    white = white or 15
    local opaque = y >= 152
    for i = 1, #digits do
        local k = digits:byte(i) - 48
        if k < 0 or k > 9 then k = 0 end
        local g = glyph("FONTE6_6.RAW", k, 6, 6, small, white)
        if g then gfx.blit(x, y, 6, 6, g, opaque and -1 or 0) end
        x = x + 6
    end
end

return M
