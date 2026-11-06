-- Race controls and the original's key setup (RACEPRG 0x5FC1..0x6178).
-- Pure Lua, tested.
--
-- The game keeps a row per control at DS:0x1297: a scancode, then 13
-- characters, the label and a 4-character key name ("LEFT    :... ").
-- The original's defaults are , . A Z SPACE; this port's are the arrows and
-- SPACE. Setting up asks for the five keys in turn; the sixth row is
-- "<OK OR ESC>": any key keeps them, ESC (at any point) puts the old ones
-- back (0x60A9, 0x60D4). P is refused (it pauses), and so are control
-- characters other than RETURN and the shift keys (0x6026..0x6038).
--
-- Key names as the original shows them (0x611D): a character for letters
-- and digits, the arrows as glyphs 24..27 of FONTOPT, and ESP, RET, SFTD,
-- SFTG, CTLR, ALT for space, return, right and left shift, ctrl and alt.

local M = {}

M.ACTIONS = { "left", "right", "accel", "brake", "fire" }
M.LABELS = { "LEFT    :", "RIGHT   :", "SPEED UP:", "STOP    :", "FIRE    :" }
M.LAST_ROW = "<OK OR ESC>  "
M.DEFAULT = { left = "Left", right = "Right", accel = "Up", brake = "Down", fire = "Space" }

local SPECIAL = {
    ["Up"] = "\24   ", ["Down"] = "\25   ", ["Right"] = "\26   ", ["Left"] = "\27   ",
    ["Space"] = "ESP ", ["Return"] = "RET ", ["Keypad Enter"] = "RET ",
    ["Right Shift"] = "SFTD", ["Left Shift"] = "SFTG",
    ["Left Ctrl"] = "CTLR", ["Right Ctrl"] = "CTLR",
    ["Left Alt"] = "ALT ", ["Right Alt"] = "ALT ",
}

-- The bindings in a state, filling in any that are missing.
function M.of(st)
    st.keys = type(st.keys) == "table" and st.keys or {}
    for k, v in pairs(M.DEFAULT) do
        if type(st.keys[k]) ~= "string" then st.keys[k] = v end
    end
    return st.keys
end

-- 4 characters for a key name.
function M.display(name)
    if not name then return "... " end
    if SPECIAL[name] then return SPECIAL[name] end
    local s = name:upper()
    if #s == 1 then return s .. "   " end
    return (s:gsub(" ", "") .. "    "):sub(1, 4)
end

-- Whether a key can be bound (P pauses; ESC cancels; the rest the
-- original can't read are refused).
function M.accepts(name)
    if not name or name == "P" or name == "Escape" then return false end
    if name == "Tab" or name == "Backspace" then return false end
    return true
end

-- The 13 characters of row i (1..6).
function M.row(i, keys)
    if i == 6 then return M.LAST_ROW end
    return M.LABELS[i] .. M.display(keys and keys[M.ACTIONS[i]])
end

return M
