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
