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
-- JOYSTICK and MOUSE answer with a KONTROL6 message at (160, 130) until a
-- key (0x5EBF). This port only has the keyboard, so they get the
-- original's "not available" messages, and KEYBOARD (the control already)
-- opens the key setup (scenes/keysetup.lua). The original's save slots
-- aren't ported: LOAD and SAVE use the port's one save.

local scenes = require("core.scenes")
local save   = require("game.save")
local career = require("game.career")
local S = { overlay = true }

local KINDS = {
    level   = { res = "KONTROL", panel = 1, x = 160, y = 95, first = 2, count = 2, step = 16 },
    options = { res = "KONTROL", panel = 0, x = 160, y = 108, first = 6, count = 6, step = 16,
                acts = { "load", "save", "keyboard", "joystick", "mouse", "quit" } },
    tracks  = { res = "CONTROL2", panel = 0, x = 160, y = 105, first = 1, count = 16, step = 9 },
}
S.KINDS = KINDS

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
    if a == "load" then
        local loaded = save.load()
        for k, v in pairs(loaded) do st[k] = v end
        return self:close(true)                            -- 0x61CD: the menu is rebuilt
    elseif a == "save" then
        self.note = save.store(st) and "SAVED" or "SAVE FAILED"
    elseif a == "keyboard" then                          -- already the control: set up keys (0x5FD6)
        scenes.pop()                                     -- over the menu alone (0x5FEB)
        scenes.push(require("scenes.keysetup"), st)
    elseif a == "joystick" then self.msg = MSG_NO_JOYSTICK
    elseif a == "mouse" then self.msg = MSG_NO_MOUSE
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
    self:act(self.k.acts[self.sel])
end

function S:update()
    if self.msg then                                       -- a message waits for a key
        if input.any() then self.msg = nil end
        return
    end
    if input.pressed("Escape") then scenes.pop(); return end
    local n = self.k.count
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
    if self.msg and self.msgs then centred(self.msgs, self.msg, MSG_X, MSG_Y) end
    if self.note then
        gfx.setcolor(120, 255, 210, 60)
        gfx.fill(160 - #self.note * 2 - 3, 176, #self.note * 4 + 6, 9, 0)
        gfx.text(160 - #self.note * 2, 178, self.note, 120)
    end
end

return S
