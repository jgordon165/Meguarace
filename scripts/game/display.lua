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

-- TRACK SPEED (Options > ADVANCED): the track video's speed in races.
M.TRACK_SPEED_ORDER = { "normal", "fast", "faster", "fastest" }
M.TRACK_SPEEDS = { normal = 1.0, fast = 1.25, faster = 1.33, fastest = 1.5 }
M.TRACK_SPEED_LABELS = { normal = "NORMAL", fast = "FAST +25%", faster = "FASTER +33%", fastest = "FASTEST +50%" }

-- SHARPEN (Options > ADVANCED): after the smoothing.
M.SHARPEN_ORDER = { "off", "light", "strong" }
M.SHARPEN = { off = 0, light = 1, strong = 2 }

-- TRACK COLOR (Options > ADVANCED): the grade on the track video in races.
M.TRACK_COLOR_ORDER = { "off", "vivid", "intense" }
M.TRACK_COLORS = { off = 0, vivid = 1, intense = 2 }

-- --classic: the settings as the original game has them, for this session
-- only. The saved settings aren't changed: these are applied over a copy.
M.CLASSIC = {
    hd_style = "line_doubled", smoothing = 0, video_repeat = 0, sides = "black", aspect = "4:3",
    music_style = "original", track_speed = "normal", track_color = "off", sharpen = "off", messages = "classic",
    voice_filter = false, lance_smoother = false,
}

-- The settings in effect: the saved ones, or with --classic those over a copy.
function M.effective(state)
    if not sys.classic then return state end
    local s = {}
    for k, v in pairs(state) do s[k] = v end
    for k, v in pairs(M.CLASSIC) do s[k] = v end
    s.display = "native"
    return s
end

-- Applies the saved HD style and mode at start (the mode unless --display
-- was given). HD style "line_doubled": the HD modes show the original
-- videos and menus line doubled and interpolated instead of the packs;
-- "line_tripled": the same with each pixel repeated 3x3, softer.
function M.apply_saved(state)
    state = M.effective(state)
    local original = state.hd_style == "line_doubled" or state.hd_style == "line_tripled"
    gfx.hd_pictures(not original)
    gfx.video_repeat(state.hd_style == "line_tripled" and 3 or 0)
    gfx.smoothing(tonumber(state.smoothing) or 0)
    gfx.sharpen(M.SHARPEN[state.sharpen] or 0)
    -- (Older saves have ambient = false for black sides.)
    local sides = state.sides or (state.ambient == false and "black") or "ambient"
    if not pcall(gfx.sides, sides) then gfx.sides("ambient") end
    gfx.aspect(state.aspect == "16:9" and "16:9" or "4:3")
    require("game.music").set_style(state.music_style)
    require("scenes.race").track_speed = M.TRACK_SPEEDS[state.track_speed] or 1.33
    require("scenes.race").track_color = M.TRACK_COLORS[state.track_color] or 2
    require("scenes.race").big_messages = state.messages ~= "classic"
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
