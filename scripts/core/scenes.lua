-- Scene stack. A scene is a table with optional enter(...), leave(),
-- update(dt), draw(). push() overlays (e.g. a hex viewer over the file list),
-- switch() replaces everything.

local M = {}
local stack = {}

local function call(s, fn, ...)
    if s and s[fn] then return s[fn](s, ...) end
end

function M.switch(scene, ...)
    while #stack > 0 do call(table.remove(stack), "leave") end
    stack[1] = scene
    call(scene, "enter", ...)
end

function M.push(scene, ...)
    stack[#stack + 1] = scene
    call(scene, "enter", ...)
end

function M.pop()
    call(table.remove(stack), "leave")
end

function M.depth() return #stack end

function M.update(dt) call(stack[#stack], "update", dt) end

function M.draw()
    -- Draw the top scene only; overlays redraw what they need.
    call(stack[#stack], "draw")
end

return M
