-- Typing a name with a controller (the Xbox 360 has no keyboard): UP/DOWN
-- turn the last letter through the characters, RIGHT adds a letter, LEFT
-- takes one off (RETURN, ESC and the rest stay with the screen). Pure Lua,
-- tested.

local M = {}

M.CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

-- The name after one step: "up", "down", "add" or "del". `chars` is the set
-- (default M.CHARS; a space may be in it), `max` the length.
function M.step(name, what, max, chars)
    chars = chars or M.CHARS
    if what == "del" then return name:sub(1, -2) end
    if what == "add" then
        if #name >= max then return name end
        return name .. chars:sub(1, 1)
    end
    if #name == 0 then return chars:sub(1, 1) end
    local last = name:sub(-1)
    local i = chars:find(last, 1, true) or 1
    i = what == "up" and (i % #chars + 1) or ((i - 2) % #chars + 1)
    return name:sub(1, -2) .. chars:sub(i, i)
end

-- Whether this machine types with a controller.
function M.active() return sys.platform == "xenon" end

-- One frame of controller typing on `name`; returns the new name, or nil if
-- no pad key was pressed.
function M.update(name, max, chars)
    if input.pressed("Up") then return M.step(name, "up", max, chars) end
    if input.pressed("Down") then return M.step(name, "down", max, chars) end
    if input.pressed("Right") then return M.step(name, "add", max, chars) end
    if input.pressed("Left") then return M.step(name, "del", max, chars) end
    return nil
end

return M
