-- ADVANCED: the port's own settings, opened from the OPTIONS panel (the
-- original has none of these, so there's no panel art: a box in the game's
-- FONTOPT font over the menu).
--
--   RESOLUTION    the display mode: 320x200, or 800x600 / 1024x768 / 1920x1080
--                 with the HD videos (game/display.lua). LEFT/RIGHT or ENTER.
--   ASPECT        4:3, or 16:9: the screen gets a wing either side of the
--                 game's 320x200 (the videos and the dashboard stay 4:3 in
--                 the middle; the race's LAPS and score move out to the
--                 edges). HD resolutions only.
--   SIDES         16:9's wings where nothing is drawn: AMBIENT (a blurred,
--                 darkened stretch of the picture) or BLACK.
--   HD STYLE      what the HD modes show:
--                   the packs' upscale (REAL-ESRGAN when hd-conversion-utility
--                   used it), made from your own CD; or
--                   LINE DOUBLED: the original videos and menus, each pixel
--                   repeated and only the seams interpolated (no packs needed).
--                 LEFT/RIGHT or ENTER. F10 switches it while playing.
--   SMOOTHING     OFF, SUBTLE or MORE: softens every video a little in the HD
--                 resolutions, on the GPU (no cost to the frame rate). The
--                 cars, dashboard, text and menus stay sharp.
--   HD FOLDER     where the packs are; ENTER edits it (type, or paste with
--                 CTRL+V; ENTER keeps it, ESC leaves it as it was; empty = the
--                 default, your Meguarace user folder).
--   VOICE FILTER  the hiss filter on Lance's clips (V while one plays).
--
-- ESC goes back to the OPTIONS panel. Everything is saved as it changes.

local scenes  = require("core.scenes")
local save    = require("game.save")
local font    = require("ui.font")
local display = require("game.display")

local S = { overlay = true }

local ITEMS = { "resolution", "aspect", "sides", "style", "smoothing", "folder", "voice" }
local LABELS = { resolution = "RESOLUTION", aspect = "ASPECT", sides = "SIDES", style = "HD STYLE",
                 smoothing = "SMOOTHING", folder = "HD FOLDER", voice = "VOICE FILTER" }
local SMOOTHING = { [0] = "OFF", "SUBTLE", "MORE" }
local TOP, H = 30, 126
local TITLE_Y, FIRST_Y, STEP = 36, 52, 13

-- The box: as wide as the screen allows (16:9 adds the wings).
local function box()
    local wing = gfx.wing()
    local b = { x = 20 - wing, y = TOP, w = 280 + 2 * wing, h = H }
    b.value_x = b.x + 14 + 13 * 8            -- after the longest label
    b.value_chars = (b.x + b.w - 8 - b.value_x) // 8
    return b
end
local NOTE_Y = 191

-- st: the saved state (the menu's); back(): reopens the OPTIONS panel.
function S:enter(st, back)
    self.state, self.back = st, back
    self.sel = 1
    self.note = nil
    self.editing = nil
    self.font = font.open("FONTOPT", 8)
end

function S:leave()
    if self.editing then input.text_stop() end
end

-- What HD STYLE shows now.
local function style_label()
    if not sys.hd then return "LINE DOUBLED" end
    if not gfx.hd_pictures() then return "LINE DOUBLED" end
    local by = sys.hd_upscaler
    if by == "" or by == nil then return "HD PACKS" end
    if by == "REAL-ESRGAN" then return by end
    return by .. " PACKS"                    -- made without Real-ESRGAN
end

function S:value(what)
    if what == "resolution" then return display.label(gfx.display())
    elseif what == "style" then return style_label()
    elseif what == "smoothing" then return SMOOTHING[gfx.smoothing()]
    elseif what == "aspect" then return gfx.aspect()
    elseif what == "sides" then return gfx.ambient() and "AMBIENT" or "BLACK"
    elseif what == "voice" then return self.state.voice_filter == false and "OFF" or "ON"
    end
    local v = self.editing and (self.editing .. "_") or (sys.hd_folder())
    if v == "" then v = "DEFAULT" end
    local room = box().value_chars
    if #v > room then v = ".." .. v:sub(-(room - 2)) end   -- the end of a long path
    return v
end

function S:change(what, dir)
    local st = self.state
    if what == "resolution" then
        if not sys.hd then self.note = "HD MODES NEED HD VIDEOS: RUN HD-CONVERSION-UTILITY"; return end
        st.display = display.cycle(dir)
    elseif what == "style" then
        if not sys.hd then self.note = "NO HD VIDEOS YET: RUN HD-CONVERSION-UTILITY"; return end
        local packs = not gfx.hd_pictures()
        gfx.hd_pictures(packs)
        st.hd_style = packs and "upscaled" or "line_doubled"
    elseif what == "aspect" then
        st.aspect = gfx.aspect(gfx.aspect() == "16:9" and "4:3" or "16:9")
        if (gfx.display()) == "native" then self.note = "16:9 SHOWS IN THE HD RESOLUTIONS" end
    elseif what == "sides" then
        st.ambient = gfx.ambient(not gfx.ambient())
        if gfx.wing() == 0 then self.note = "THE SIDES SHOW IN 16:9" end
    elseif what == "smoothing" then
        st.smoothing = gfx.smoothing((gfx.smoothing() + dir) % 3)
        if (gfx.display()) == "native" then self.note = "SMOOTHING SHOWS IN THE HD RESOLUTIONS" end
    elseif what == "voice" then
        st.voice_filter = st.voice_filter == false
        require("scenes.player").voice_filter = st.voice_filter
    elseif what == "folder" then
        self.editing = (sys.hd_folder())
        input.text_start()
        return
    end
    save.store(st)
end

-- Takes off the last character (UTF-8: its continuation bytes too).
local function rubout(s)
    local n = #s
    while n > 0 and (s:byte(n) & 0xC0) == 0x80 do n = n - 1 end
    return s:sub(1, math.max(n - 1, 0))
end

function S:finish_edit(keep)
    input.text_stop()
    local folder = self.editing:match("^%s*(.-)%s*$")
    self.editing = nil
    if not keep then return end
    local found, saved = sys.set_hd_folder(folder)
    if not saved then self.note = "COULD NOT SAVE THE SETTING"
    elseif found then
        self.note = "HD VIDEOS FOUND"
        display.apply_saved(self.state)
    else self.note = "NO HD VIDEOS THERE: RUN HD-CONVERSION-UTILITY" end
end

function S:update_edit()
    if input.pressed("Escape") then return self:finish_edit(false) end
    if input.pressed("Return") then return self:finish_edit(true) end
    if input.pressed("Backspace") then self.editing = rubout(self.editing); return end
    local typed = input.text()
    if (input.down("Left Ctrl") or input.down("Right Ctrl")) and input.pressed("V") then
        typed = input.paste():gsub("[\r\n].*", "")
    end
    typed = typed:gsub("%c", "")
    if #self.editing + #typed <= 255 then self.editing = self.editing .. typed end
end

function S:update()
    if self.editing then return self:update_edit() end
    if input.pressed("Escape") then
        scenes.pop()
        if self.back then self.back() end
        return
    end
    local n = #ITEMS
    if input.pressed("Down") then self.sel = self.sel % n + 1; self.note = nil end
    if input.pressed("Up") then self.sel = (self.sel - 2) % n + 1; self.note = nil end
    local what = ITEMS[self.sel]
    if input.pressed("Return") or input.pressed("Space") or input.pressed("Right") then self:change(what, 1)
    elseif input.pressed("Left") and what ~= "folder" then self:change(what, -1) end
end

function S:draw()
    local black, yellow, grey = 0, gfx.nearest(255, 210, 60), gfx.nearest(150, 150, 160)
    local b = box()
    gfx.fill(b.x, b.y, b.w, b.h, black)
    gfx.line(b.x, b.y, b.x + b.w - 1, b.y, grey)
    gfx.line(b.x, b.y + b.h - 1, b.x + b.w - 1, b.y + b.h - 1, grey)
    gfx.line(b.x, b.y, b.x, b.y + b.h - 1, grey)
    gfx.line(b.x + b.w - 1, b.y, b.x + b.w - 1, b.y + b.h - 1, grey)
    local function text(x, y, s)
        if self.font then self.font:draw(x, y, s) else gfx.text(x, y + 2, s, yellow) end
    end
    text(160 - #"ADVANCED" * 4, TITLE_Y, "ADVANCED")
    for i, what in ipairs(ITEMS) do
        local y = FIRST_Y + (i - 1) * STEP
        if i == self.sel then gfx.fill(b.x + 6, y + 3, 3, 3, yellow) end   -- cursor
        text(b.x + 14, y, LABELS[what])
        text(b.value_x, y, self:value(what))
    end
    local hint = self.editing and "TYPE OR CTRL+V   ENTER KEEP   ESC CANCEL"
                              or "LEFT/RIGHT CHANGE   ENTER EDIT   ESC BACK"
    gfx.text(160 - #hint * 2, b.y + b.h - 10, hint, grey)
    if self.note then
        gfx.fill(160 - #self.note * 2 - 3, NOTE_Y, #self.note * 4 + 6, 9, black)
        gfx.text(160 - #self.note * 2, NOTE_Y + 2, self.note, yellow)
    end
end

return S
