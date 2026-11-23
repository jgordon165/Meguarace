-- An on-screen keyboard, for typing a name with a controller (the Xbox 360
-- has no keyboard): the Hall of Fame's name and a saved game's.
--
--   D-pad / stick   move between the keys
--   A               type the key (SPACE, DEL and DONE do what they say)
--   B / BACK        rub out a letter; with nothing typed, leave (as ESC)
--   X               a space (where the name takes one)
--   START           done (as ENTER)
--
-- The pad arrives as keys (platform/xenon/sdl/SDL_xenon_video.c): A and
-- START are Return, START is P too, B and BACK are Escape, X is Space. The
-- keys are laid out in rows of ten; the last row is SPACE (if `chars` has
-- one), DEL and DONE. Pure Lua apart from draw; tested.

local M = {}

M.COLS = 10
M.LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

-- Whether this machine types with a controller.
function M.active() return sys.platform == "xenon" or sys.platform == "wiiu" end

-- The keys for `chars` (the characters a name takes; default A-Z and 0-9):
-- rows of up to COLS characters, then the special row.
function M.layout(chars)
    chars = chars or M.LETTERS
    local rows, row = {}, {}
    local space = false
    for i = 1, #chars do
        local c = chars:sub(i, i)
        if c == " " then
            space = true
        else
            row[#row + 1] = c
            if #row == M.COLS then rows[#rows + 1] = row; row = {} end
        end
    end
    if #row > 0 then rows[#rows + 1] = row end
    local special = {}
    if space then special[#special + 1] = "SPACE" end
    special[#special + 1] = "DEL"
    special[#special + 1] = "DONE"
    rows[#rows + 1] = special
    return rows
end

-- A keyboard for a name of up to `max` characters from `chars`.
function M.new(max, chars)
    local k = { max = max, chars = chars or M.LETTERS, row = 1, col = 1 }
    k.rows = M.layout(k.chars)
    return setmetatable(k, { __index = M })
end

function M:key() return self.rows[self.row][self.col] end

-- Moves the cursor: dr, dc of -1, 0 or 1, wrapping. Between rows of
-- different lengths the cursor keeps its place across the width (where a
-- move left or right last put it), so up and back down comes back.
function M:move(dr, dc)
    if dc ~= 0 then
        local n = #self.rows[self.row]
        self.col = (self.col - 1 + dc) % n + 1
        self.x = (self.col - 0.5) / n
    end
    if dr ~= 0 then
        self.x = self.x or (self.col - 0.5) / #self.rows[self.row]
        self.row = (self.row - 1 + dr) % #self.rows + 1
        local to = self.rows[self.row]
        self.col = math.min(#to, math.max(1, math.floor(self.x * #to) + 1))
    end
end

-- What a key does to `name`: the new name and "done" or nil.
function M:press(name, key)
    if key == "DONE" then return name, "done" end
    if key == "DEL" then return name:sub(1, -2) end
    local c = key == "SPACE" and " " or key
    if #name >= self.max then return name end
    if c == " " and (#name == 0 or not self.chars:find(" ", 1, true)) then return name end
    return name .. c
end

-- One frame: the name after this frame's keys, and "done" (START, or DONE
-- pressed: the screen keeps the name), "cancel" (B with nothing typed: the
-- screen leaves) or nil.
function M:update(name)
    if input.pressed("P") then return name, "done" end               -- START (also Return: P first)
    if input.pressed("Up") then self:move(-1, 0) end
    if input.pressed("Down") then self:move(1, 0) end
    if input.pressed("Left") then self:move(0, -1) end
    if input.pressed("Right") then self:move(0, 1) end
    if input.pressed("Return") then return self:press(name, self:key()) end
    if input.pressed("Space") then return self:press(name, "SPACE") end
    if input.pressed("Backspace") then return name:sub(1, -2) end
    if input.pressed("Escape") then
        if #name > 0 then return name:sub(1, -2) end
        return name, "cancel"
    end
    return name
end

-- Draws the keyboard in the lower part of the 320x200 screen with `font`
-- (ui.font), the cursor's key lit.
M.TOP, M.ROW_H, M.CELL_W = 118, 13, 24

function M:draw(font)
    local bg, edge = gfx.nearest(12, 8, 34), gfx.nearest(255, 60, 210)
    local lit, dim = gfx.nearest(0, 200, 240), gfx.nearest(40, 28, 80)
    local w = M.COLS * M.CELL_W
    local x0, y0 = 160 - w // 2, M.TOP
    local h = #self.rows * M.ROW_H + 14
    gfx.fill(x0 - 6, y0 - 5, w + 12, h, edge)
    gfx.fill(x0 - 5, y0 - 4, w + 10, h - 2, bg)
    for r, row in ipairs(self.rows) do
        local cw = w // #row
        for c, key in ipairs(row) do
            local x, y = x0 + (c - 1) * cw, y0 + (r - 1) * M.ROW_H
            local on = r == self.row and c == self.col
            gfx.fill(x + 1, y, cw - 2, M.ROW_H - 2, on and lit or dim)
            local tw = font and font:width(key) or #key * 4
            if font then font:draw(x + (cw - tw) // 2, y + 2, key)
            else gfx.text(x + (cw - tw) // 2, y + 3, key, edge) end
        end
    end
    local hint = "A TYPE   B DELETE   X SPACE   START DONE"
    if not self.chars:find(" ", 1, true) then hint = "A TYPE   B DELETE   START DONE" end
    gfx.text(160 - #hint * 2, y0 + #self.rows * M.ROW_H + 1, hint, edge)
end

return M
