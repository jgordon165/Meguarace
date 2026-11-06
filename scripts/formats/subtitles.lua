-- Subtitles (MEGA<LANG>.SUB) and the font they're drawn in (DFCHAR.BIN).
--
-- A .SUB file is a table of u16 offsets (the first is the table's size),
-- one per group, then the groups: lines of { u8 length, text, 0xFF }. A
-- group belongs to one video (RACEPRG table at DS:0x158C, video numbers ->
-- files at DS:0x59BC). The video says when: a frame whose header has bit
-- 0x4000 shows the next line, 0x2000 clears it (0xD24F).
--
-- DFCHAR.BIN: 256 widths, then 9-byte glyphs (one byte per row, high bit
-- left) from character 0. A line is drawn on rows 191-199, from
-- x = 160 - 2.25 * length, each character opaque (colour 255 on 0) in a
-- cell as wide as its width (0xD287, 0xD2C8).

local M = {}

M.LANGUAGES = {
    { code = "AMER", name = "ENGLISH (US)" }, { code = "ENGL", name = "ENGLISH (UK)" },
    { code = "FREN", name = "FRANCAIS" }, { code = "GERM", name = "DEUTSCH" },
    { code = "ITAL", name = "ITALIANO" }, { code = "SPAN", name = "ESPANOL" },
}

-- Group k (from 0) -> its video.
M.GROUP_VIDEO = {
    "INTRO.HNM", "B64.HNM", "B65.HNM", "B66.HNM", "B01.HNM", "B04.HNM",
    "B07.HNM", "B09.HNM", "B12.HNM", "B14.HNM", "B17.HNM", "B20.HNM",
    "B23.HNM", "B24.HNM", "B26.HNM", "B27.HNM", "B29.HNM", "B30.HNM",
    "B32.HNM", "B34.HNM", "B35.HNM", "B36.HNM", "B37.HNM", "B38.HNM",
    "B39.HNM", "B40.HNM", "B41.HNM", "B42.HNM", "B43.HNM", "B44.HNM",
    "B45.HNM", "B46.HNM", "B47.HNM", "B48.HNM", "B79.HNM", "B80.HNM",
    "B82.HNM", "B83.HNM", "B84.HNM", "B85.HNM", "B86.HNM", "B88.HNM",
    "B89.HNM", "B92.HNM", "B98.HNM", "B99.HNM", "BA1.HNM", "BA2.HNM",
    "BA3.HNM", "BA5.HNM", "BA9.HNM", "BB1.HNM", "BB2.HNM", "BB3.HNM",
    "BB4.HNM", "BB5.HNM", "BB6.HNM", "BB7.HNM", "VWBT2.HNM", "VWBT3.HNM",
    "VWBT4.HNM", "VWBT5.HNM",
}

M.ROW = 191
M.HEIGHT = 9

local function u16(d, i) return d:byte(i) | d:byte(i + 1) << 8 end

-- Parses a .SUB file into { [group] = { lines } } (groups from 0).
function M.parse(d)
    if not d or #d < 2 then return nil end
    local n = u16(d, 1) // 2
    if n < 1 or n * 2 > #d then return nil end
    local offs = {}
    for i = 1, n do offs[i] = u16(d, 2 * i - 1) end
    local groups = {}
    for g = 1, n do
        local p, e = offs[g] + 1, (offs[g + 1] or #d) + 1
        local lines = {}
        while p < e and p <= #d do
            local len = d:byte(p)
            lines[#lines + 1] = d:sub(p + 1, p + len)
            p = p + 1 + len + 1                      -- text, then 0xFF
        end
        groups[g - 1] = lines
    end
    return groups
end

-- Lines for a video file, or nil.
function M.for_video(groups, video)
    if not groups then return nil end
    video = video:upper()
    for g, name in ipairs(M.GROUP_VIDEO) do
        if name == video then return groups[g - 1] end
    end
    return nil
end

local cache = {}
function M.load(code)
    if cache[code] == nil then
        local f = "MEGA" .. code .. ".SUB"
        cache[code] = (dat.exists(f) and M.parse(dat.read(f, false))) or false
    end
    return cache[code] or nil
end

-- The font: widths and glyph bitmaps.
local font
function M.font()
    if font == nil then
        local d = dat.exists("DFCHAR.BIN") and dat.read("DFCHAR.BIN", false)
        font = d and #d >= 256 and { data = d, glyphs = {} } or false
    end
    return font or nil
end

function M.start_x(text) return math.max(0, 160 - (#text * 9) // 4) end

-- Pixels (a w x 9 string, colours fg / 0) for one character, or nil.
local function glyph(f, c, fg)
    local key = c * 256 + fg
    local g = f.glyphs[key]
    if g == nil then
        local w = f.data:byte(c + 1)
        local base = 256 + c * 9
        g = false
        if w > 0 and base + 9 <= #f.data then
            local px = {}
            for r = 0, 8 do
                local bits = f.data:byte(base + r + 1)
                for x = 0, w - 1 do
                    px[#px + 1] = string.char((x < 8 and (bits & (0x80 >> x)) ~= 0) and fg or 0)
                end
            end
            g = { w = w, px = table.concat(px) }
        end
        f.glyphs[key] = g
    end
    return g or nil
end

function M.clear() gfx.fill(0, M.ROW, 320, M.HEIGHT, 0) end

function M.draw(text, fg)
    local f = M.font()
    if not f then return end
    fg = fg or 255
    local x = M.start_x(text)
    for i = 1, #text do
        local g = glyph(f, text:byte(i), fg)
        if g then
            if x + g.w <= 320 then gfx.blit(x, M.ROW, g.w, M.HEIGHT, g.px, -1) end
            x = x + g.w
        end
    end
end

return M
