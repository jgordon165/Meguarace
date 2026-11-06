-- The key setup (RACEPRG 0x5FD6): KEYBOARD in the options when the
-- keyboard is already the control (in this port, always).
--
-- CONTROL4 image 2 at (160, 100); six rows of 13 characters, 6 pixels
-- apart from x = 125, rows from y = 80 every 10 (0x5D51, 0x60FF). Rows are
-- in FONTOPT; the row being asked for in FONTOPT2, its key name in
-- FONTOPT3. Press a key for each control; on the last row any key keeps
-- them, ESC (any time) puts the old ones back. See game/keys.lua.

local scenes = require("core.scenes")
local save   = require("game.save")
local keys   = require("game.keys")
local font   = require("ui.font")
local S = { overlay = true }

S.X, S.Y, S.STEP, S.ROW = 125, 80, 6, 10

function S:enter(state)
    self.state = state
    self.panel = spr.open("CONTROL4")
    self.fonts = { font.open("FONTOPT", S.STEP), font.open("FONTOPT2", S.STEP), font.open("FONTOPT3", S.STEP) }
    local cur = keys.of(state)
    self.old = {}
    for k, v in pairs(cur) do self.old[k] = v end
    self.new = {}
    for k, v in pairs(cur) do self.new[k] = v end
    self.row = 1
end

function S:update()
    local k = input.key()
    if not k then return end
    if k == "Escape" then                              -- 0x60A5: the old keys back
        self.state.keys = self.old
        scenes.pop()
        return
    end
    if self.row == 6 then                              -- <OK OR ESC>: keep them (0x6AF2)
        self.state.keys = self.new
        save.store(self.state)
        scenes.pop()
        return
    end
    if not keys.accepts(k) then return end
    self.new[keys.ACTIONS[self.row]] = k
    self.row = self.row + 1
end

local function draw_text(f, x, y, s)
    for i = 1, #s do
        local c = s:byte(i)
        if c ~= 32 and c < f.count then f.set:draw(c, x, y) end
        x = x + S.STEP
    end
end

function S:draw()
    if self.panel then
        local w, h = self.panel:size(2)
        if w then self.panel:draw(2, 160 - w // 2, 100 - h // 2) end
    end
    for i = 1, 6 do
        local text = keys.row(i, self.new)
        local y = S.Y + (i - 1) * S.ROW
        local f = self.fonts[i == self.row and 2 or 1]
        if f then
            if i == self.row and i < 6 then
                draw_text(f, S.X, y, text:sub(1, 9))
                if self.fonts[3] then draw_text(self.fonts[3], S.X + 9 * S.STEP, y, text:sub(10)) end
            else
                draw_text(f, S.X, y, text)
            end
        end
    end
end

return S
