-- Game facts read at runtime from the player's RACEPRG.EXE, if it's on the
-- disc: the track display names and the default Hall of Fame. Without the
-- EXE, the port falls back to the track file names and an empty table.

local M = {}

-- Track order (RACEPRG.EXE's HNM name table) and the files of each.
M.TRACKS = {
    { key = "NS1", video = "NS11" }, { key = "MV1", video = "MV11" },
    { key = "FC1", video = "FC11" }, { key = "NGL", video = "NGL1" },
    { key = "SUB", video = "SUB1" }, { key = "NS2", video = "NS21" },
    { key = "MV2", video = "MV21" }, { key = "PAR", video = "PAR1" },
    { key = "FC2", video = "FC21" }, { key = "JNK", video = "JNK1" },
    { key = "MV3", video = "MV31" }, { key = "FC3", video = "FC31" },
    { key = "NS3", video = "NS31" }, { key = "LST", video = "LST1" },
    { key = "TOKY", video = "TOKY" }, { key = "HOLD", video = "HOLD" },
}

-- Rival gang per round, in the order of RACEPRG.EXE's _XXX.HSQ name table.
M.RIVAL_ORDER = { "RA", "JO", "OM", "OU", "PA", "LU", "HO", "MA" }

-- Main-menu monitors, top-left to bottom-right (matches the I_<NAME>.HNM
-- showcase videos and PANEL2's thumbnails 0..7).
M.MENU_CARS = {
    { code = "JO", video = "I_JOSE" },   { code = "HO", video = "I_HOOPER" },
    { code = "LU", video = "I_LUIS" },   { code = "OU", video = "I_OUZBEL" },
    { code = "MA", video = "I_MARIA" },  { code = "PA", video = "I_PALOMA" },
    { code = "OM", video = "I_OMEGA" },  { code = "RA", video = "I_RAMON" },
}

local exe
local function load_exe()
    if exe == nil then exe = fs.exists("RACEPRG.EXE") and fs.read("RACEPRG.EXE") or false end
    return exe
end

local function trim(s) return (s:gsub("%z.*", ""):gsub("%s+$", "")) end

-- 16 display names ("NEW SAN-1", ...), from the table after "EASYHARD".
function M.track_names()
    local names = {}
    local d = load_exe()
    local at = d and d:find("EASYHARD", 1, true)
    for i = 1, #M.TRACKS do
        local n = at and trim(d:sub(at + 8 + (i - 1) * 16, at + 8 + i * 16 - 1)) or ""
        names[i] = (n ~= "" and n:match("^[%w%s%-]+$")) and n or M.TRACKS[i].video
    end
    return names
end

-- Default Hall of Fame: 8 entries of { u16 score, name[18], "00000 ",
-- track[10], level[4] } starting at "BIG BOB" - 2.
-- The EXE's table at DS:0x1344 holds 8 records of 40 bytes: the 7 places
-- of the Hall of Fame, then the line for your own run (0x145C, written at
-- every game over: RACEPRG 0x693C). default_hall() returns the 7 places,
-- sorted; default_last_run() the 8th record ("ENFORCER SCORE").
local function records()
    local out = {}
    local d = load_exe()
    local at = d and d:find("BIG BOB", 1, true)
    if not at then return out end
    local p = at - 2
    for _ = 1, 8 do
        if p + 39 > #d then break end
        local score = d:byte(p) | d:byte(p + 1) << 8
        local text = d:sub(p + 2, p + 39)
        local name = trim(text:sub(1, 18))
        if name == "" or not name:match("^[%w%s%.]+$") then break end
        out[#out + 1] = { name = name, score = score, track = trim(text:sub(25, 34)),
                          level = trim(text:sub(35, 38)) }
        p = p + 40
    end
    return out
end

function M.default_hall()
    local out = {}
    for i, r in ipairs(records()) do if i <= 7 then out[#out + 1] = r end end
    table.sort(out, function(a, b) return a.score > b.score end)
    return out
end

function M.default_last_run() return records()[8] end

return M
