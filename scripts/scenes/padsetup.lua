-- The controller setup (Options > CONTROLLER on the Xbox 360, the port's
-- own): the key setup's panel and rows (scenes/keysetup.lua), with
-- controller buttons. Press a button for each control in turn; on the last
-- row any button keeps them, BACK (any time) puts the old ones back. See
-- game/pad.lua.

local scenes   = require("core.scenes")
local save     = require("game.save")
local pad      = require("game.pad")
local keysetup = require("scenes.keysetup")
local font     = require("ui.font")
local S = { overlay = true, hd_menu = true }

function S:enter(state)
    self.state = state
    self.panel = spr.open("CONTROL4")
    local step = keysetup.STEP
    self.fonts = { font.open("FONTOPT", step), font.open("FONTOPT2", step), font.open("FONTOPT3", step) }
    local cur = pad.of(state)
    self.old, self.new = {}, {}
    for k, v in pairs(cur) do self.old[k] = v; self.new[k] = v end
    self.row = 1
end

-- One button (the scene's logic, tested): returns "cancel", "keep" or nil.
function S:press(b)
    if b == "BACK" then                                -- the old buttons back
        self.state.pad = self.old
        return "cancel"
    end
    if self.row == 6 then                              -- <OK OR BACK>: keep them
        self.state.pad = self.new
        return "keep"
    end
    if not pad.accepts(b) then return nil end
    self.new[pad.ACTIONS[self.row]] = b
    self.row = self.row + 1
end

function S:update()
    local b = input.pad_button()
    if not b then return end
    local what = self:press(b)
    if what == "keep" then save.store(self.state) end
    if what then scenes.pop() end
end

function S:draw()
    keysetup.draw_rows(self, function(i) return pad.row(i, self.new) end)
end

return S
