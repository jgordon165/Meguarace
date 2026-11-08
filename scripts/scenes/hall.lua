-- Hall of Fame: HALL1.HNM backdrop, the table in the FONTHALL font.
-- Entries come from the save file, seeded with the defaults stored in the
-- player's RACEPRG.EXE (BIG BOB, KING KOOL, ...).
--   ENTER / ESC back
--
-- Seven places (RACEPRG 0x68C1); the 8th line is your last run (0x693C,
-- the EXE's "ENFORCER SCORE" until you've played).
-- After a career (RACEPRG 0x6847): a score at least the 7th one's earns a
-- place. You type your name on the CONTROL4 panel in the FONTOPT font
-- (0x66D6): up to 16 letters, digits or spaces, BACKSPACE to rub out,
-- ENTER to keep it (one letter at least), ESC to stay out of the table.

local scenes = require("core.scenes")
local font   = require("ui.font")
local save   = require("game.save")
local S = { hd_menu = true }

S.NAME_MAX = 16

-- Would `score` make the table?
function S.qualifies(hall, score)
    if #hall < save.HALL_PLACES then return true end
    return score >= hall[save.HALL_PLACES].score
end

function S:enter(state, highlight, on_done, entry)
    self.state, self.highlight, self.on_done, self.entry = state, highlight, on_done, entry
    self.bg = assert(hnm.open("HALL1.HNM"))
    self.bg:decode(0)
    self.bg:apply_palette()
    self.font = font.open("FONTHALL", 8)
    self.typing_font = font.open("FONTOPT", 8)
    self.panel = spr.open("CONTROL4")
    self.t = 0
    self.typing = entry and S.qualifies(state.hall or {}, entry.score) or false
    self.name = ""
    if entry then state.last_run = entry end                   -- the 8th line (0x693C)
end

local KEYS = {}
for c = string.byte("A"), string.byte("Z") do KEYS[#KEYS + 1] = string.char(c) end
for c = string.byte("0"), string.byte("9") do KEYS[#KEYS + 1] = string.char(c) end

function S:update_typing()
    if input.pressed("Escape") then self.typing = false; return end
    if input.pressed("Return") then
        if #self.name > 0 then
            self.entry.name = self.name
            self.highlight = save.add_score(self.state, self.entry)
            save.store(self.state)
            self.typing = false
        end
        return
    end
    if input.pressed("Backspace") then self.name = self.name:sub(1, -2); return end
    if #self.name >= S.NAME_MAX then return end
    if input.pressed("Space") then self.name = self.name .. " "; return end
    for _, k in ipairs(KEYS) do
        if input.pressed(k) then self.name = self.name .. k; return end
    end
end

function S:update(dt)
    self.t = self.t + dt
    if self.typing then return self:update_typing() end
    if input.pressed("Return") or input.pressed("Escape") or input.pressed("Space") then
        local cb = self.on_done
        scenes.pop()
        if cb then cb() end
    end
end

local function row(f, y, rank, e)
    if rank then f:draw(8, y, tostring(rank)) end
    f:draw(24, y, (e.name or ""):sub(1, 14))
    f:draw(140, y, string.format("%05d", e.score))
    f:draw(188, y, (e.track or ""):sub(1, 9))
    f:draw(268, y, e.level or "")
end

function S:draw()
    gfx.clear(0)
    self.bg:draw(0, 0)
    local f = self.font
    if not f then return end
    local hall = self.state.hall or {}
    local y0 = 62
    for i, e in ipairs(hall) do
        if i <= save.HALL_PLACES and not (i == self.highlight and (self.t * 3) % 1 > 0.6) then
            row(f, y0 + (i - 1) * 15, i, e)
        end
    end
    if #hall == 0 then f:draw_centered(160, 100, "NO RECORDS YET") end
    local last = self.state.last_run or require("game.data").default_last_run()
    if last then row(f, y0 + save.HALL_PLACES * 15, 8, last) end     -- your run (0x693C)
    if self.typing then
        if self.panel then                              -- centred on (160, 90), as 0x59CC draws
            local w, h = self.panel:size(3)
            if w then self.panel:draw(3, 160 - w // 2, 90 - h // 2) end
        end
        local tf = self.typing_font or f
        local shown = self.name .. (((self.t * 3) % 1 < 0.6) and "_" or "")
        tf:draw(96, 98, shown)
    end
end

return S
