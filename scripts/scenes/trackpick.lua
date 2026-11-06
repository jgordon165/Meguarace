-- Free track choice, open once the game has been completed (RACEPRG 0x55C7:
-- with [0x69AA] set the race button asks for a track).
--   UP/DOWN choose   ENTER race   ESC back

local scenes = require("core.scenes")
local data   = require("game.data")
local S = { overlay = true }

local C_TEXT, C_HI = 121, 120

function S:enter(current, on_pick)
    self.sel = current or 1
    self.on_pick = on_pick
    self.names = data.track_names()
end

function S:update()
    local n = #self.names
    if input.pressed("Escape") then scenes.pop(); return end
    if input.pressed("Down") then self.sel = self.sel % n + 1 end
    if input.pressed("Up") then self.sel = (self.sel - 2) % n + 1 end
    if input.pressed("Return") or input.pressed("Space") then
        local cb, sel = self.on_pick, self.sel
        scenes.pop()
        if cb then cb(sel) end
    end
end

function S:draw()
    gfx.setcolor(C_TEXT, 235, 235, 240)
    gfx.setcolor(C_HI, 255, 210, 60)
    local h = #self.names * 8 + 16
    gfx.fill(96, 20, 128, h, 0)
    gfx.text(128, 24, "CHOOSE A TRACK", C_TEXT)
    for i, name in ipairs(self.names) do
        gfx.text(104, 26 + i * 8, string.format("%2d %s", i, name), i == self.sel and C_HI or C_TEXT)
    end
end

return S
