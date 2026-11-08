-- The menu's panels (RACEPRG 0x57F8): a list over the menu, from KONTROL.HSQ
-- (or CONTROL2.HSQ for the tracks).
--   UP/DOWN choose   ENTER or SPACE select   ESC back
--
--   "level"   (left button)   KONTROL 1 at (160, 95): NOVICE, HARDENED.
--             Picking one starts a new career on it (0x590B, 0x6A7C).
--   "options" (middle button) KONTROL 0 at (160, 108): LOAD, SAVE, KEYBOARD,
--             JOYSTICK, MOUSE, QUIT (0x61CB, 0x642A, 0x5FC1, 0x5EBF,
--             0x5EF8, 0x5113).
--   "tracks"  (a monitor, once the game is completed) CONTROL2 0 at
--             (160, 105): the 16 tracks.
--
-- Items are pairs of images (off, lit) from `first`, centred on x = 160,
-- one every 16 rows (9 for the tracks) from y = 100 - 4 x count; every image
-- is centred on its point (0x5991, 0x59CC).
--
-- KEYBOARD, JOYSTICK and MOUSE answer with a KONTROL6 message at (160, 130)
-- until a key (0x5EBF). This port only has the keyboard, so JOYSTICK and
-- MOUSE get the original's "not available" messages. The original's key
-- redefinition (KEYBOARD when the keyboard is already active) and its save
-- slots aren't ported: LOAD and SAVE use the port's one save.
--
-- The port's own addition, on the OPTIONS panel only: an ADVANCED line
-- under the panel, in the game's FONTOPT font (the original has no such
-- settings, so no panel art). Down past QUIT selects it; ENTER opens the
-- ADVANCED screen (scenes/advanced.lua: resolution, HD style, HD folder,
-- voice filter), and ESC there comes back here.

local scenes = require("core.scenes")
local save   = require("game.save")
local career = require("game.career")
local font   = require("ui.font")
local S = { overlay = true, hd_menu = true }

local KINDS = {
    level   = { res = "KONTROL", panel = 1, x = 160, y = 95, first = 2, count = 2, step = 16 },
    options = { res = "KONTROL", panel = 0, x = 160, y = 108, first = 6, count = 6, step = 16,
                acts = { "load", "save", "keyboard", "joystick", "mouse", "quit" } },
    tracks  = { res = "CONTROL2", panel = 0, x = 160, y = 105, first = 1, count = 16, step = 9 },
}
S.KINDS = KINDS

-- The port's lines (options panel only), under the panel; notes go below.
local EXTRA = { "advanced" }
local EXTRA_Y = { advanced = 174 }
local NOTE_Y = 186

-- KONTROL6 messages.
local MSG_NO_JOYSTICK, MSG_NO_MOUSE = 1, 4       -- (0 is "keyboard now active")
local MSG_X, MSG_Y = 160, 130

-- Item centres for a kind: { x, y } per item.
function S.layout(kind)
    local k = KINDS[kind]
    local out, y = {}, 100 - 4 * k.count
    for i = 1, k.count do
        out[i] = { 160, y }
        y = y + k.step
    end
    return out
end

-- kind, the career state, and for "tracks" the callback with the round.
function S:enter(kind, state, on_pick)
    self.kind, self.state, self.on_pick = kind, state, on_pick
    self.k = KINDS[kind]
    self.set = assert(spr.open(self.k.res))
    self.set:apply_palette()
    self.msgs = spr.open("KONTROL6")
    self.at = S.layout(kind)
    -- Options panel: its items, then the DISPLAY line.
    -- (No ADVANCED in classic mode: the original's panel.)
    self.n = self.k.count + (kind == "options" and not sys.classic and #EXTRA or 0)
    self.font = kind == "options" and font.open("FONTOPT", 8) or nil
    self.msg, self.note = nil, nil
    if kind == "level" then
        self.sel = state.level == "HARDENED" and 2 or 1   -- [0x68CD] - 1
    elseif kind == "tracks" then
        self.sel = 1                                       -- [0x69FC] = 0
    else
        self.sel = 1
    end
end

local function centred(set, i, x, y)
    local w, h = set:size(i)
    if w then set:draw(i, x - w // 2, y - h // 2) end
end

function S:close(changed)
    scenes.pop()
    if changed then
        local menu = require("scenes.menu")
        if menu.rebuild then menu:rebuild() end
    end
end

function S:act(a)
    local st = self.state
    if a == "load" then                                  -- the saves (0x61CB, scenes/slots.lua)
        scenes.pop()
        scenes.push(require("scenes.slots"), "load", st, function(ok)
            if not ok then return end
            local menu = require("scenes.menu")
            menu:rebuild()                                 -- 0x61CD: the menu is rebuilt
            require("core.sequence").play({ career.file(require("game.slots").LOADED_VIDEO) })   -- BA2 (0x6352)
        end)
    elseif a == "save" then                              -- a name for it (0x642A)
        scenes.pop()
        scenes.push(require("scenes.slots"), "save", st)
    elseif a == "keyboard" then                          -- already the control: set up keys (0x5FD6)
        scenes.pop()                                     -- over the menu alone (0x5FEB)
        scenes.push(require("scenes.keysetup"), st)
    elseif a == "joystick" then self.msg = MSG_NO_JOYSTICK
    elseif a == "mouse" then self.msg = MSG_NO_MOUSE
    elseif a == "advanced" then                          -- the port's settings, in place of this panel
        local kind, pick = self.kind, self.on_pick
        scenes.pop()
        scenes.push(require("scenes.advanced"), st, function()
            scenes.push(S, kind, st, pick)
            S.sel = S.n                                  -- back on the ADVANCED line
        end)
    elseif a == "quit" then
        scenes.pop()
        require("scenes.menu"):farewell()                  -- Lance's farewell and the credits (0x5113)
    end
end

function S:select()
    local kind, st = self.kind, self.state
    if kind == "level" then
        career.reset(st, self.sel == 2 and "HARDENED" or "NOVICE")
        save.store(st)
        return self:close(true)
    elseif kind == "tracks" then
        local cb, sel = self.on_pick, self.sel
        scenes.pop()
        if cb then cb(sel) end
        return
    end
    if self.sel > self.k.count then return self:act(EXTRA[self.sel - self.k.count]) end
    self:act(self.k.acts[self.sel])
end

function S:update()
    if self.msg then                                       -- a message waits for a key
        if input.any() then self.msg = nil end
        return
    end
    if input.pressed("Escape") then scenes.pop(); return end
    local n = self.n
    if input.pressed("Down") then self.sel = self.sel % n + 1; self.note = nil end
    if input.pressed("Up") then self.sel = (self.sel - 2) % n + 1; self.note = nil end
    if input.pressed("Return") or input.pressed("Space") then self:select() end
end

function S:draw()
    local k, set = self.k, self.set
    centred(set, k.panel, k.x, k.y)
    for i, p in ipairs(self.at) do
        centred(set, k.first + 2 * (i - 1) + (i == self.sel and 1 or 0), p[1], p[2])
    end
    for i, what in ipairs(self.n > k.count and EXTRA or {}) do
        local y = EXTRA_Y[what]
        local label = what == "advanced" and "ADVANCED..." or what
        local w = #label * 8
        local x = 160 - w // 2
        gfx.fill(x - 8, y - 2, w + 12, 11, 0)
        if self.sel == k.count + i then
            gfx.fill(x - 6, y + 3, 3, 3, gfx.nearest(255, 210, 60))   -- cursor: the line is selected
        end
        if self.font then self.font:draw(x, y, label)
        else gfx.text(x, y + 2, label, 121) end
    end
    if self.msg and self.msgs then centred(self.msgs, self.msg, MSG_X, MSG_Y) end
    if self.note then
        local yellow = gfx.nearest(255, 210, 60)
        gfx.fill(160 - #self.note * 2 - 3, NOTE_Y, #self.note * 4 + 6, 9, 0)
        gfx.text(160 - #self.note * 2, NOTE_Y + 2, self.note, yellow)
    end
end

return S
