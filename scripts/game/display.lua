-- Display mode setting. The game draws in 320x200 in every mode (every
-- position, mask and layout table is in the original's pixels); the mode
-- decides how that is shown: "native" (the original 320x200 at 4:3) or a
-- 4:3 picture of 800x600, 1024x768 or 1920x1080 (1440x1080, pillarboxed)
-- with the HD videos. HD modes exist only when hd-conversion-utility has made packs
-- (sys.hd); otherwise only native is offered.

local M = {}

-- Label for the options screen: "320X200" or "1920X1080 HD".
function M.label(name, _, _, hd)
    if name == "native" then return "320X200" end
    return name:upper() .. (hd and " HD" or "")
end

-- Applies the saved HD style and mode at start (the mode unless --display
-- was given). HD style "line_doubled": the HD modes show the original
-- videos and menus line doubled and interpolated instead of the packs;
-- "line_tripled": the same with each pixel repeated 3x3, softer.
function M.apply_saved(state)
    local original = state.hd_style == "line_doubled" or state.hd_style == "line_tripled"
    gfx.hd_pictures(not original)
    gfx.video_repeat(state.hd_style == "line_tripled" and 3 or 0)
    gfx.smoothing(tonumber(state.smoothing) or 0)
    -- (Older saves have ambient = false for black sides.)
    local sides = state.sides or (state.ambient == false and "black") or "ambient"
    if not pcall(gfx.sides, sides) then gfx.sides("ambient") end
    gfx.aspect(state.aspect == "16:9" and "16:9" or "4:3")
    require("game.music").set_style(state.music_style)
    require("scenes.race").track_speed = state.track_speed == "fast" and 1.25 or 1.0
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
