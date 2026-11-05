-- Sprite fonts from MEGARACE.DAT (FONTHALL, FONTOPT, ...): one 9x9 image
-- per character, glyph index = ASCII code (DOS code page: 24..27 are the
-- arrows). FONTHALL has 96 glyphs, so it covers codes 0..95: digits, A-Z and
-- punctuation, no lower case.

local M = {}
M.__index = M

function M.open(name, advance)
    local set = spr.open(name)
    if not set then return nil end
    return setmetatable({ set = set, count = set:count(), advance = advance or 8 }, M)
end

function M:glyph(ch)
    local i = string.byte(ch)
    if i < 0 or i >= self.count then return nil end
    return i
end

function M:width(s) return #s * self.advance end

function M:draw(x, y, s)
    s = tostring(s):upper()
    for k = 1, #s do
        local ch = s:sub(k, k)
        local g = ch ~= " " and self:glyph(ch)
        if g then self.set:draw(g, x, y) end
        x = x + self.advance
    end
end

function M:draw_centered(cx, y, s)
    self:draw(cx - self:width(s) // 2, y, s)
end

return M
