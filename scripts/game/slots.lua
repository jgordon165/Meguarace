-- Named saves, as the options' LOAD and SAVE make them (RACEPRG 0x61CB,
-- 0x642A). Pure Lua apart from store / fetch, tested.
--
-- The original writes NAME.SAV in its folder: up to 8 letters and digits,
-- typed on the CONTROL4 panel. It holds the career (points, score, level,
-- round and the rest), the key table and the cars you can pick (0x658A);
-- the Hall of Fame and the settings stay outside. LOAD lists every *.SAV
-- (up to 50) and, once one is loaded, plays BA2 (0x6352).
--
-- This port keeps each as slot_NAME.sav in its preferences folder, with
-- the list of names in the main save (`slots`), newest last.

local career = require("game.career")
local save   = require("game.save")

local M = {}

M.MAX_NAME = 8
M.MAX_SLOTS = 50
M.LOADED_VIDEO = 0x52                    -- BA2.HNM

-- A character the name may take (0x64C6..0x64EB): A-Z, 0-9 (lower case raised).
function M.char_ok(c)
    return c:match("^[A-Z0-9]$") ~= nil
end

function M.valid(name)
    return type(name) == "string" and #name >= 1 and #name <= M.MAX_NAME and name:match("^[A-Z0-9]+$") ~= nil
end

function M.file(name) return "slot_" .. name .. ".sav" end

-- The fields a slot keeps.
function M.fields()
    local f = { "car", "keys" }
    for k in pairs(career.new()) do f[#f + 1] = k end
    table.sort(f)
    return f
end

function M.snapshot(st)
    local out = {}
    for _, k in ipairs(M.fields()) do out[k] = st[k] end
    return out
end

function M.restore(st, data)
    for _, k in ipairs(M.fields()) do
        if data[k] ~= nil then st[k] = data[k] end
    end
end

-- Adds a name to the list (moved to the end if it was there).
function M.remember(st, name)
    st.slots = type(st.slots) == "table" and st.slots or {}
    for i = #st.slots, 1, -1 do if st.slots[i] == name then table.remove(st.slots, i) end end
    st.slots[#st.slots + 1] = name
    while #st.slots > M.MAX_SLOTS do table.remove(st.slots, 1) end
end

function M.list(st)
    return type(st.slots) == "table" and st.slots or {}
end

function M.store(st, name)
    if not M.valid(name) then return false end
    if not sys.save(M.file(name), save.serialize(M.snapshot(st))) then return false end
    M.remember(st, name)
    return save.store(st)
end

function M.fetch(st, name)
    local data = M.valid(name) and save.parse(sys.load(M.file(name)))
    if not data then return false end
    M.restore(st, data)
    save.store(st)
    return true
end

return M
