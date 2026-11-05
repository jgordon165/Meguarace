-- Hall of Fame: HALL1.HNM backdrop, the table in the FONTHALL font.
-- Entries come from the save file, seeded with the defaults stored in the
-- player's RACEPRG.EXE (BIG BOB, KING KOOL, ...).
--   ENTER / ESC back

local scenes = require("core.scenes")
local font   = require("ui.font")
local S = {}

function S:enter(state, highlight)
    self.state, self.highlight = state, highlight
    self.bg = assert(hnm.open("HALL1.HNM"))
    self.bg:decode(0)
    self.bg:apply_palette()
    self.font = font.open("FONTHALL", 8)
    self.t = 0
end

function S:update(dt)
    self.t = self.t + dt
    if input.pressed("Return") or input.pressed("Escape") or input.pressed("Space") then scenes.pop() end
end

function S:draw()
    gfx.clear(0)
    self.bg:draw(0, 0)
    local f = self.font
    if not f then return end
    local hall = self.state.hall or {}
    -- Dark band behind the table, below the "HALL OF FAME" title.
    gfx.setcolor(120, 0, 0, 0)
    local y0 = 62
    for i, e in ipairs(hall) do
        local y = y0 + (i - 1) * 15
        if not (i == self.highlight and (self.t * 3) % 1 > 0.6) then
            f:draw(8, y, string.format("%d", i))
            f:draw(24, y, e.name:sub(1, 14))
            f:draw(140, y, string.format("%05d", e.score))
            f:draw(188, y, (e.track or ""):sub(1, 9))
            f:draw(268, y, e.level or "")
        end
    end
    if #hall == 0 then f:draw_centered(160, 100, "NO RECORDS YET") end
end

return S
