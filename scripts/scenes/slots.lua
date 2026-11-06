-- The options' SAVE and LOAD screens (RACEPRG 0x642A, 0x61CB), over the
-- menu. game/slots.lua keeps the files.
--
-- SAVE: CONTROL4 image 1 at (160, 90); the name, 8 characters at most,
-- in FONTOPT from (128, 98), 8 pixels apart, a "_" where the next one
-- goes. Letters and digits, BACKSPACE, RETURN saves, ESC leaves.
-- LOAD: CONTROL4 image 0 at (160, 100); six names at a time from
-- (132, 80), one every 10 rows, the chosen one in FONTOPT2. UP/DOWN move
-- (the list scrolls), RETURN loads and plays BA2, ESC leaves.

local scenes = require("core.scenes")
local slots  = require("game.slots")
local font   = require("ui.font")
local S = { overlay = true }

S.ROWS = 6

function S:enter(mode, state, done)
    self.mode, self.state, self.done = mode, state, done
    self.panel = spr.open("CONTROL4")
    self.fonts = { font.open("FONTOPT", 8), font.open("FONTOPT2", 8) }
    self.name = ""
    self.top, self.sel = 0, 0                    -- [0x6A12] first shown, [0x6A0C] row
    self.names = slots.list(state)
end

local function draw_text(f, x, y, s, list)
    if not f then return end
    for i = 1, #s do
        local c = s:byte(i)
        if list and c > 0x5A then c = 0x3E end   -- the list shows past Z as > (0x6401)
        if c ~= 32 and c < f.count then f.set:draw(c, x, y) end
        x = x + 8
    end
end

local function panel(set, i, cy)
    local w, h = set:size(i)
    if w then set:draw(i, 160 - w // 2, cy - h // 2) end
end

-- One step through the list (0x62DF..0x633A).
function S.move(top, sel, d, count)
    if d < 0 then
        sel = sel - 1
        if sel < 0 then sel = 0; top = math.max(0, top - 1) end
    elseif top + sel + 1 < count then
        sel = sel + 1
        if sel >= S.ROWS then sel = S.ROWS - 1; top = top + 1 end
    end
    return top, sel
end

function S:finish(ok)
    local cb = self.done
    scenes.pop()
    if cb then cb(ok) end
end

function S:update()
    if self.mode == "save" then
        local k = input.key()
        if not k then return end
        if k == "Escape" then return self:finish(false) end
        if k == "Return" or k == "Keypad Enter" then
            if #self.name == 0 then return end           -- 0x6533: nothing typed
            return self:finish(slots.store(self.state, self.name))
        end
        if k == "Backspace" then self.name = self.name:sub(1, -2); return end
        local c = #k == 1 and k:upper() or (k:match("^Keypad (%d)$"))
        if c and slots.char_ok(c) and #self.name < slots.MAX_NAME then self.name = self.name .. c end
        return
    end
    if input.pressed("Escape") then return self:finish(false) end
    if input.pressed("Up") then self.top, self.sel = S.move(self.top, self.sel, -1, #self.names) end
    if input.pressed("Down") then self.top, self.sel = S.move(self.top, self.sel, 1, #self.names) end
    if input.pressed("Return") or input.pressed("Space") then
        local name = self.names[self.top + self.sel + 1]
        if name then return self:finish(slots.fetch(self.state, name)) end
    end
end

function S:draw()
    if self.mode == "save" then
        if self.panel then panel(self.panel, 1, 90) end
        local s = self.name
        if #s < slots.MAX_NAME then s = s .. "_" end
        draw_text(self.fonts[1], 128, 98, s)
        return
    end
    if self.panel then panel(self.panel, 0, 100) end
    for r = 0, S.ROWS - 1 do
        local name = self.names[self.top + r + 1]
        if name then draw_text(self.fonts[r == self.sel and 2 or 1], 132, 80 + 10 * r, name, true) end
    end
end

return S
