-- Settings, championship progress and the Hall of Fame, kept in the
-- per-user preferences folder (sys.save / sys.load) as a Lua table literal.

local M = {}

M.FILE = "meguarace.sav"

local function serialize(v, indent)
    indent = indent or ""
    local t = type(v)
    if t == "number" or t == "boolean" then return tostring(v) end
    if t == "string" then return string.format("%q", v) end
    if t ~= "table" then return "nil" end
    local keys = {}
    for k in pairs(v) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    local parts = {}
    for _, k in ipairs(keys) do
        local key = type(k) == "string" and k:match("^[%a_][%w_]*$") and k or ("[" .. serialize(k) .. "]")
        parts[#parts + 1] = indent .. "  " .. key .. " = " .. serialize(v[k], indent .. "  ")
    end
    return "{\n" .. table.concat(parts, ",\n") .. "\n" .. indent .. "}"
end
M.serialize = serialize

function M.parse(text)
    if not text then return nil end
    local chunk = load("return " .. text, "save", "t", {})   -- data only: empty environment
    if not chunk then return nil end
    local ok, v = pcall(chunk)
    return ok and type(v) == "table" and v or nil
end

function M.defaults()
    return {
        level = "NOVICE",          -- or "HARDENED" (CONTROL.HSQ)
        car = 1,                   -- main-menu monitor 1..8
        round = 1,                 -- next track, 1..16
        total = 0,                 -- championship score so far
        music = true,              -- FM music on (M in the main menu)
        subtitles = "AMER",        -- subtitle language or "OFF" (S in the main menu)
        display = nil,             -- display mode (options); nil: the largest that fits
        hd_style = "line_tripled", -- HD modes: "upscaled" (the packs), "line_doubled" or "line_tripled" (Options > ADVANCED)
        smoothing = 1,             -- video smoothing in the HD modes: 0 off, 1 subtle, 2 more, 3 max
        sharpen = "light",         -- after the smoothing: "off", "light" or "strong" (an unsharp mask, GPU)
        aspect = "16:9",           -- HD modes: "4:3" or "16:9" (wings either side of the game's screen)
        sides = "space",           -- 16:9 wings: "ambient" (blurred picture), "black", "carbon", "space" or "cyberpunk"
        lance_smoother = false,    -- Lance's clips one smoothing level softer than the other videos (SAME: no)
        track_speed = "faster",    -- track video speed in races: "normal", "fast" (+25%), "faster" (+33%), "fastest" (+50%)
        track_color = "intense",   -- track video in races: "off", "vivid" or "intense" (more colour and contrast)
        music_style = "enhanced",  -- "original" (OPL2), "stereo" (FM), "soundfont" or "enhanced" (the EDM remix; Options > ADVANCED)
        menus = "hd",              -- menu screens: "hd" (their HD packs, whatever hd_style says) or "original" (as hd_style)
        dashboard = "hd",          -- race dashboard: "hd" (its HD packs, when made) or "original" (the original pixels)
        messages = "enhanced",     -- race messages (SKID...): "enhanced" (big, outlined, no box) or "classic"
        voice_filter = true,       -- hiss filter on Lance's clips (V while one plays)
        hall = nil,                -- filled from the EXE's defaults on first run
    }
end

function M.load()
    local s = M.parse(sys.load(M.FILE)) or {}
    local d = M.defaults()
    for k, v in pairs(d) do if s[k] == nil then s[k] = v end end
    return s
end

function M.store(s) return sys.save(M.FILE, serialize(s)) end

M.HALL_PLACES = 7               -- RACEPRG 0x68C1: seven places, then your run

-- Inserts a result into the Hall of Fame. Returns its rank or nil.
function M.add_score(s, entry)
    s.hall = s.hall or {}
    table.insert(s.hall, entry)
    table.sort(s.hall, function(a, b) return a.score > b.score end)
    while #s.hall > M.HALL_PLACES do table.remove(s.hall) end
    for i, e in ipairs(s.hall) do if e == entry then return i end end
    return nil
end

return M
