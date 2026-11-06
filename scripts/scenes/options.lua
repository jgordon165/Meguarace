-- Options: CONTROL.HSQ panels over the menu.
--   UP/DOWN choose   ENTER select   ESC back
--
-- CONTROL images: 0 OPTIONS panel, 1 LEVEL panel, then pairs (off, lit):
-- NOVICE 2/3, HARDENED 4/5, LOAD 6/7, SAVE 8/9, KEYBOARD 10/11,
-- JOYSTICK 12/13, MOUSE 14/15, QUIT 16/17. The layout here is this port's;
-- the original positions aren't read from the EXE yet.

local scenes = require("core.scenes")
local save   = require("game.save")
local S = { overlay = true }

local PANEL = { x = 52, y = 22 }        -- OPTIONS panel (86x123)
local LEVEL = { x = 168, y = 22 }       -- LEVEL panel (89x62)

-- Items: image pair, position, action.
local ITEMS = {
    { img = 10, x = PANEL.x + 8, y = PANEL.y + 22, act = "input" },     -- KEYBOARD
    { img = 12, x = PANEL.x + 8, y = PANEL.y + 36, act = "input_na" },  -- JOYSTICK
    { img = 14, x = PANEL.x + 8, y = PANEL.y + 50, act = "input_na" },  -- MOUSE
    { img = 6,  x = PANEL.x + 8, y = PANEL.y + 70, act = "load" },      -- LOAD
    { img = 8,  x = PANEL.x + 8, y = PANEL.y + 84, act = "save" },      -- SAVE
    { img = 16, x = PANEL.x + 8, y = PANEL.y + 104, act = "quit" },     -- QUIT
    { img = 2,  x = LEVEL.x + 10, y = LEVEL.y + 24, act = "novice" },   -- NOVICE
    { img = 4,  x = LEVEL.x + 10, y = LEVEL.y + 40, act = "hardened" }, -- HARDENED
}

function S:enter(state)
    self.state = state
    self.set = assert(spr.open("CONTROL"))
    self.set:apply_palette()
    self.sel = 1
    self.note = nil
end

function S:act(a)
    local st = self.state
    if a == "novice" then st.level = "NOVICE"; save.store(st)
    elseif a == "hardened" then st.level = "HARDENED"; save.store(st)
    elseif a == "save" then self.note = save.store(st) and "SAVED" or "SAVE FAILED"
    elseif a == "load" then
        local loaded = save.load()
        for k, v in pairs(loaded) do st[k] = v end
        self.note = "LOADED"
    elseif a == "input_na" then self.note = "KEYBOARD ONLY FOR NOW"
    elseif a == "quit" then
        scenes.pop()
        require("scenes.menu"):farewell()     -- Lance's farewell and the credits (0x5113)
    end
end

function S:update()
    if input.pressed("Escape") then scenes.pop(); return end
    if input.pressed("Up") then self.sel = (self.sel - 2) % #ITEMS + 1 end
    if input.pressed("Down") then self.sel = self.sel % #ITEMS + 1 end
    if input.pressed("Return") or input.pressed("Space") then self:act(ITEMS[self.sel].act) end
end

function S:draw()
    local s = self.set
    s:draw(0, PANEL.x, PANEL.y)
    s:draw(1, LEVEL.x, LEVEL.y)
    for i, it in ipairs(ITEMS) do
        local on = i == self.sel
            or (it.act == "novice" and self.state.level == "NOVICE")
            or (it.act == "hardened" and self.state.level == "HARDENED")
            or it.act == "input"
        s:draw(it.img + (on and 1 or 0), it.x, it.y)
    end
    -- Cursor beside the selected item.
    local it = ITEMS[self.sel]
    gfx.setcolor(120, 255, 210, 60)
    gfx.fill(it.x - 5, it.y + 3, 3, 3, 120)
    if self.note then
        gfx.fill(160 - #self.note * 2 - 3, 160, #self.note * 4 + 6, 9, 0)
        gfx.text(160 - #self.note * 2, 162, self.note, 120)
    end
end

return S
