-- Scene stack. A scene is a table with optional enter(...), leave(),
-- resume() (called when the scene above it is popped), update(dt), draw().
-- push() overlays, switch() replaces everything. A scene with
-- `overlay = true` is drawn on top of the scene below it.

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
    call(stack[#stack], "resume")
end

function M.depth() return #stack end
function M.top() return stack[#stack] end

function M.update(dt) call(stack[#stack], "update", dt) end

-- A scene with hd_menu = true is a menu screen: drawn (or under an overlay
-- that's drawn), it marks the frame (gfx.menu_frame), so MENUS = HD shows its
-- HD packs whatever HD STYLE says.
function M.draw()
    local n = #stack
    if gfx and gfx.menu_frame then
        local top = stack[n]
        local under = top and top.overlay and n > 1 and stack[n - 1]
        gfx.menu_frame((top and top.hd_menu) or (under and under.hd_menu) or false)
    end
    if n == 0 then return end
    if stack[n].overlay and n > 1 then call(stack[n - 1], "draw") end
    call(stack[n], "draw")
end

return M
