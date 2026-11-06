-- Display mode setting. The game draws in 320x200 in every mode (every
-- position, mask and layout table is in the original's pixels); the mode
-- decides how that is shown: "native" (the original 320x200 at 4:3) or a
-- 4:3 picture of 800x600, 1024x768 or 1920x1080 (1440x1080, pillarboxed)
-- with the HD videos. HD modes exist only when megarace-hd has made packs
-- (sys.hd); otherwise only native is offered.

local M = {}

-- Label for the options screen: "320X200" or "1920X1080 HD".
function M.label(name, _, _, hd)
    if name == "native" then return "320X200" end
    return name:upper() .. (hd and " HD" or "")
end

-- Applies the saved mode at start, unless --display was given.
function M.apply_saved(state)
    if sys.display_arg ~= "" or not state.display then return end
    if not gfx.set_display(state.display) then
        sys.log("display mode " .. tostring(state.display) .. " not available; using " .. (gfx.display()))
    end
end

-- Steps to the next (dir 1) or previous (-1) available mode; returns its name.
function M.cycle(dir)
    local list, cur = gfx.displays(), gfx.display()
    local at = 1
    for i, n in ipairs(list) do if n == cur then at = i end end
    local name = list[(at - 1 + dir) % #list + 1]
    gfx.set_display(name)
    return name
end

return M
