-- Main menu: PANEL1.HNM backdrop with PANEL2.HSQ overlays.
--   ARROWS move   ENTER choose   ESC options   M music on/off   F12 asset explorer
--
-- Monitors (two columns of four) are the eight cars: ENTER picks the car and
-- plays its showcase video. Buttons: green = race the next round, yellow =
-- Hall of Fame, blue = options.
--
-- Overlay positions were found by matching PANEL2's images against the
-- PANEL1 picture (bars and buttons match pixel for pixel; the car
-- thumbnails sit on the monitor screens next to their bars).

local scenes = require("core.scenes")
local data   = require("game.data")
local save   = require("game.save")
local music  = require("game.music")
local S = {}

local THUMB = { { 47, 3 }, { 222, 3 }, { 26, 33 }, { 237, 33 }, { 20, 65 }, { 242, 65 }, { 27, 99 }, { 230, 99 } }
local BAR   = { { 102, 4 }, { 206, 4 }, { 84, 33 }, { 223, 33 }, { 80, 65 }, { 226, 65 }, { 92, 100 }, { 213, 100 } }
local BUTTON = { { 58, 146 }, { 133, 146 }, { 211, 146 } }   -- green, yellow, blue
local BAR_LIT, BAR_OFF, BUTTON_LIT, BUTTON_OFF = 8, 16, 24, 27
local C_FOCUS = 120

-- Focus: 1..8 monitors, 9..11 buttons.
local function move(f, dx, dy)
    if f <= 8 then
        local col, row = (f - 1) % 2, (f - 1) // 2
        if dx ~= 0 then col = math.max(0, math.min(1, col + dx)) end
        if dy > 0 and row == 3 then return col == 0 and 9 or 11 end
        row = math.max(0, math.min(3, row + dy))
        return row * 2 + col + 1
    end
    local b = f - 8
    if dy < 0 then return b == 1 and 7 or (b == 3 and 8 or 7) end
    return 8 + math.max(1, math.min(3, b + dx))
end

function S:enter()
    self.state = self.state or save.load()
    if not self.state.hall then
        self.state.hall = data.default_hall()
        save.store(self.state)
    end
    self.bg = assert(hnm.open("PANEL1.HNM"))
    self.panel = assert(spr.open("PANEL2"))
    self.focus = self.focus or 9
    self.t = 0
    self:restore()
end

-- Redraws backdrop palette + overlays (after returning from another scene).
function S:restore()
    self.want_music = true             -- started on the next update, once the menu is on top
    self.bg:decode(0)
    self.bg:apply_palette()
    self.panel:apply_palette()
end

function S:start_round()
    local st = self.state
    local round = data.TRACKS[st.round]
    local car = data.MENU_CARS[st.car].code
    -- Rivals: the gang for this round, skipping your own model.
    local order = {}
    for _, c in ipairs(data.RIVAL_ORDER) do if c ~= car then order[#order + 1] = c end end
    local rival = order[(st.round - 1) % #order + 1]
    local idents = { "VWBT.HNM", "VWBT2.HNM", "VWBT3.HNM", "VWBT4.HNM", "VWBT5.HNM" }
    local ident = idents[(st.round - 1) % #idents + 1]
    local me = self
    scenes.push(require("scenes.player"), ident, { on_done = function()
        scenes.push(require("scenes.race"), round.video, {
            car = car, rival = rival, level = st.level, total = st.total, round = st.round,
            title = data.track_names()[st.round],
            on_done = function(result) me:race_done(result) end,
        })
    end })
end

-- Championship (RACEPRG 0x4F8B): the race's points add to your score; a
-- wreck on the last track scores nothing (0x4F6F). You move on to the next
-- track if you qualified: every opponent destroyed, or the laps run out
-- with at most one left and 1200 points (race/rival.lua).
function S:race_done(result)
    local st = self.state
    local gained = result.points or 0
    if result.outcome == "wrecked" and st.round == #data.TRACKS then gained = 0 end
    st.total = st.total + gained
    local names = data.track_names()
    local msg
    if result.qualified then
        if st.round == #data.TRACKS then
            local rank = save.add_score(st, { name = "ENFORCER", score = st.total,
                track = names[st.round], level = st.level == "HARDENED" and "HARD" or "EASY" })
            st.round, st.total = 1, 0
            save.store(st)
            scenes.push(require("scenes.player"), "FIN.HNM", { on_done = function()
                scenes.push(require("scenes.hall"), st, rank)
            end })
            return
        end
        st.round = st.round + 1
        msg = string.format("QUALIFIED  +%d", gained)
    elseif result.outcome == "wrecked" then
        msg = string.format("DESTROYED  +%d  TRY AGAIN", gained)
    else
        msg = string.format("%d OPPONENTS LEFT  +%d  TRY AGAIN", result.left or 0, gained)
    end
    save.store(st)
    self.note, self.note_t = msg, 4
    self:restore()
end

function S:update(dt)
    self.t = self.t + dt
    if self.want_music then
        self.want_music = false
        music.set_enabled(self.state.music ~= false)
        music.play(music.AFTER_RACE)   -- song 6, as after the post-race videos (0x5198)
    end
    if self.note_t then self.note_t = self.note_t - dt; if self.note_t <= 0 then self.note = nil end end
    if input.pressed("F12") then scenes.push(require("scenes.explorer"), self.report); return end
    if input.pressed("Escape") then scenes.push(require("scenes.options"), self.state); return end
    if input.pressed("M") then
        self.state.music = self.state.music == false
        save.store(self.state)
        music.set_enabled(self.state.music)
        if self.state.music then music.play(music.AFTER_RACE) end
        self.note, self.note_t = self.state.music and "MUSIC ON" or "MUSIC OFF", 2
        return
    end
    local f = self.focus
    if input.pressed("Left") then f = move(f, -1, 0) end
    if input.pressed("Right") then f = move(f, 1, 0) end
    if input.pressed("Up") then f = move(f, 0, -1) end
    if input.pressed("Down") then f = move(f, 0, 1) end
    self.focus = f
    if input.pressed("Return") or input.pressed("Space") then
        if f <= 8 then
            self.state.car = f
            save.store(self.state)
            scenes.push(require("scenes.player"), data.MENU_CARS[f].video .. ".HNM")
        elseif f == 9 then
            self:start_round()
        elseif f == 10 then
            scenes.push(require("scenes.hall"), self.state)
        else
            scenes.push(require("scenes.options"), self.state)
        end
    end
end

-- Called when a pushed scene (video, race, hall, options) returns.
function S:resume() self:restore() end

function S:draw()
    gfx.clear(0)
    self.bg:draw(0, 0)
    local car = self.state.car
    for i = 1, 8 do
        self.panel:draw(i - 1, THUMB[i][1], THUMB[i][2])
        self.panel:draw((i == car and BAR_LIT or BAR_OFF) + i - 1, BAR[i][1], BAR[i][2])
    end
    for b = 1, 3 do
        local lit = self.focus == 8 + b and (self.t * 3) % 1 < 0.6
        self.panel:draw((lit and BUTTON_LIT or BUTTON_OFF) + b - 1, BUTTON[b][1], BUTTON[b][2])
    end
    if self.focus <= 8 then
        local i = self.focus
        local w, h = self.panel:size(i - 1)
        if (self.t * 3) % 1 < 0.6 then
            gfx.setcolor(C_FOCUS, 255, 210, 60)
            local x, y = THUMB[i][1] - 1, THUMB[i][2] - 1
            gfx.line(x, y, x + w + 1, y, C_FOCUS); gfx.line(x, y + h + 1, x + w + 1, y + h + 1, C_FOCUS)
            gfx.line(x, y, x, y + h + 1, C_FOCUS); gfx.line(x + w + 1, y, x + w + 1, y + h + 1, C_FOCUS)
        end
    end
    -- Round and score on the globe's strip.
    gfx.setcolor(C_FOCUS + 1, 235, 235, 240)
    local names = data.track_names()
    local line = string.format("ROUND %d  %s  SCORE %d", self.state.round, names[self.state.round], self.state.total)
    gfx.fill(160 - #line * 2 - 3, 136, #line * 4 + 6, 8, 0)
    gfx.text(160 - #line * 2, 137, line, C_FOCUS + 1)
    if self.note then
        gfx.fill(160 - #self.note * 2 - 3, 126, #self.note * 4 + 6, 8, 0)
        gfx.text(160 - #self.note * 2, 127, self.note, C_FOCUS)
    end
end

return S
