-- Race controls on an Xbox 360 controller (Options > CONTROLLER, the port's
-- own: the original has KEYBOARD, JOYSTICK and MOUSE). Pure Lua, tested.
--
-- The same five controls as the keys (game/keys.lua), each bound to a
-- button: "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS" (the
-- sticks pressed in), or "UP", "DOWN", "LEFT", "RIGHT" (the D-pad or the
-- left stick). START pauses and BACK gives up, so they can't be bound.
-- Setting up asks for the five buttons in turn, as the key setup does; on
-- the last row any button keeps them, BACK (at any point) puts the old ones
-- back.

local keys = require("game.keys")
local M = {}

M.ACTIONS = keys.ACTIONS
M.LABELS = keys.LABELS
M.LAST_ROW = "<OK OR BACK> "
M.DEFAULT = { left = "LEFT", right = "RIGHT", accel = "RT", brake = "LT", fire = "X" }
M.BUTTONS = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS",
              "UP", "DOWN", "LEFT", "RIGHT", "START", "BACK" }

local VALID = {}
for _, b in ipairs(M.BUTTONS) do VALID[b] = true end

-- Whether this machine has the controller setup (the Xbox 360).
function M.active() return sys.platform == "xenon" end

-- The bindings in a state (st.pad), filling in any missing or unknown.
function M.of(st)
    st.pad = type(st.pad) == "table" and st.pad or {}
    for k, v in pairs(M.DEFAULT) do
        if not M.accepts(st.pad[k]) then st.pad[k] = v end
    end
    return st.pad
end

-- Whether a button can be bound (START pauses, BACK gives up).
function M.accepts(name)
    return type(name) == "string" and VALID[name] == true and name ~= "START" and name ~= "BACK"
end

-- 4 characters for a button (the arrows as FONTOPT's glyphs 24..27).
local ARROWS = { UP = "\24   ", DOWN = "\25   ", RIGHT = "\26   ", LEFT = "\27   " }
function M.display(name)
    if not name then return "... " end
    return ARROWS[name] or (name .. "    "):sub(1, 4)
end

-- The 13 characters of row i (1..6).
function M.row(i, binds)
    if i == 6 then return M.LAST_ROW end
    return M.LABELS[i] .. M.display(binds and binds[M.ACTIONS[i]])
end

-- The buttons a set of bindings uses, once each (for input.pad_take).
function M.used(binds)
    local out, seen = {}, {}
    for _, a in ipairs(M.ACTIONS) do
        local b = binds[a]
        if b and not seen[b] then seen[b] = true; out[#out + 1] = b end
    end
    return out
end

return M
