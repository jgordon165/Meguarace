-- Main menu (RACEPRG 0x54C9): PANEL1.HNM with PANEL2.HSQ images over it.
--   ARROWS move   ENTER race / open   SPACE or ESC car showcase
--   M music on/off   S subtitles   F12 asset explorer (the last three are this port's)
--
-- Focus runs 0..10 ([0x69FA]): the eight monitors (cars), then the buttons
-- left, middle, right. Any arrow steps it (right/down +1, left/up -1),
-- wrapping and skipping cars you haven't unlocked (game/career.lua).
--   * ENTER on a monitor races the next round with that car; once the game
--     is completed it asks for a track first (0x55AB). SPACE or ESC plays
--     the car's showcase, video 0x27 + focus (0x5662).
--   * ENTER on a button: left = level (a new career), middle = options,
--     right = Hall of Fame (0x57F8; scenes/options.lua).
-- Drawing (0x569C): an unlocked monitor shows its car (image m) and its name
-- bar, lit when focused (8 + m, else 16 + m); a locked one stays dark. The
-- buttons are 27 + b, or 24 + b when focused. Every image is centred on its
-- point in DS:0xFA4 (0x57AE).
--
-- The career between races (Lance Boyle's clips, game over, the second
-- season, bonus tracks, the finale) is game/career.lua, from RACEPRG.EXE.
--
-- Attract mode (RACEPRG 0x555F, 0x4E9E): left alone for 3000 ticks the menu
-- plays B65 (Lance: "only about a zillion viewers out there..."); left alone
-- again it runs a demo race on the first track with OMEGA's car, which any
-- key ends. Then the show starts again from the intro.

local scenes = require("core.scenes")
local data   = require("game.data")
local save   = require("game.save")
local music  = require("game.music")
local career = require("game.career")
local sequence = require("core.sequence")
local subs   = require("formats.subtitles")
local S = {}

-- Centres of PANEL2's images 0..29 (DS:0xFA4): car pictures, lit bars,
-- dark bars, lit buttons, dark buttons.
S.CENTRE = {
    { 72, 18 }, { 247, 18 }, { 53, 48 }, { 264, 48 }, { 48, 83 }, { 272, 83 }, { 59, 120 }, { 261, 120 },
    { 107, 19 }, { 212, 20 }, { 90, 48 }, { 229, 48 }, { 86, 81 }, { 232, 81 }, { 99, 120 }, { 220, 119 },
    { 107, 19 }, { 212, 20 }, { 90, 48 }, { 229, 48 }, { 86, 81 }, { 232, 81 }, { 99, 120 }, { 220, 119 },
    { 86, 168 }, { 161, 168 }, { 239, 168 }, { 86, 168 }, { 161, 168 }, { 239, 168 },
}
local BAR_LIT, BAR_OFF, BUTTON_LIT, BUTTON_OFF = 8, 16, 24, 27
local C_NOTE = 120
local IDLE_SECONDS = 3000 / 25      -- 3000 ticks ([0x69EC], 0x555A) at the port's 25 ticks a second
local SHOWCASE_HOLD = 800 / 25      -- a showcase played through holds its last frame 800 ticks (0x5687)

-- Career fields for saves from before the career existed.
local function ensure_career(st)
    for k, v in pairs(career.new(st.level)) do if st[k] == nil then st[k] = v end end
    if st.round > #data.TRACKS then st.round = 1 end
    if type(st.unlocked) ~= "table" or #st.unlocked ~= 8 then st.unlocked = career.new().unlocked end
    -- Saves from before the Hall of Fame had 7 places held the EXE's 8th
    -- record ("ENFORCER SCORE", your run's line) as a place.
    if st.hall then
        for i = #st.hall, 1, -1 do
            if st.hall[i].name == "ENFORCER SCORE" then table.remove(st.hall, i) end
        end
        while #st.hall > save.HALL_PLACES do table.remove(st.hall) end
    end
end

local function files(ids)
    local out = {}
    for _, id in ipairs(ids) do out[#out + 1] = career.file(id) end
    return out
end

local function apply_subtitles(st)
    local code = st.subtitles
    local player = require("scenes.player")
    player.language = (code and code ~= "OFF") and code or nil
    -- The hiss filter on Lance's clips; V while one plays switches it, saved here.
    player.voice_filter = st.voice_filter ~= false
    player.lance_smoother = st.lance_smoother ~= false
    player.on_voice_filter = function(on)
        st.voice_filter = on
        save.store(st)
    end
end

function S:enter()
    self.state = self.state or save.load()
    ensure_career(self.state)
    apply_subtitles(self.state)
    if not self.state.hall then
        self.state.hall = data.default_hall()
        save.store(self.state)
    end
    self.bg = assert(hnm.open("PANEL1.HNM"))
    self.panel = assert(spr.open("PANEL2"))
    self.focus = self.focus or 0
    self.t = 0
    self:rebuild()
    self:restore()
end

-- After a new career or a load: keep the focus off locked cars.
function S:rebuild()
    local st = self.state
    if self.focus <= 7 and not st.unlocked[self.focus + 1] then
        self.focus = career.step_focus(self.focus, 1, st.unlocked)
    end
    if not st.unlocked[st.car or 1] then st.car = career.START_CARS[1] end
end

-- Redraws backdrop palette + overlays (after returning from another scene).
function S:restore()
    self.idle = 0
    self.want_music = true             -- started on the next update, once the menu is on top
    self.bg:decode(0)
    self.bg:apply_palette()
    self.panel:apply_palette()
end

-- The clips before a round, then back to the menu. how: "boot" (after the
-- logos: the show's intro first), "new" (a new career) or "moved_on".
function S:pre_round(how, lead)
    local ids = career.pre_round(self.state, how == "boot" and "new" or how)
    local list = lead and { table.unpack(lead) } or {}
    for _, f in ipairs(files(ids)) do list[#list + 1] = f end
    sequence.play(list)
end

-- ENTER on monitor `m` (1..8).
function S:start_round(m)
    local st = self.state
    st.car = m
    save.store(st)
    if st.completed then                                 -- free choice after the finale
        scenes.push(require("scenes.options"), "tracks", st, function(r)
            st.round = r
            self:launch()
        end)
        return
    end
    self:launch()
end

function S:launch()
    local st = self.state
    local round = data.TRACKS[st.round]
    local car = data.MENU_CARS[st.car].code
    -- Rival model: the gang for this round, skipping your own model.
    local order = {}
    for _, c in ipairs(data.RIVAL_ORDER) do if c ~= car then order[#order + 1] = c end end
    local rival = order[(st.round - 1) % #order + 1]
    local me = self
    sequence.play(files(career.before_race(st)), function()
        scenes.push(require("scenes.race"), round.video, {
            car = car, rival = rival, level = st.level, total = st.total, round = st.round,
            keys = require("game.keys").of(st),
            title = data.track_names()[st.round],
            on_done = function(result) me:race_done(result) end,
        })
    end)
end

-- A Hall of Fame entry for a finished career; the hall asks for the name.
local function hall_entry(e)
    local names = data.track_names()
    return { score = e.score, track = names[e.round] or "", level = e.level == "HARDENED" and "HARD" or "EASY" }
end

-- After a race (RACEPRG 0x4F2F..0x52CB): Lance's clips for how it went,
-- then the next round, game over or the finale.
function S:race_done(result)
    local st = self.state
    self.attract_seen = false                            -- 0x4F23
    local out = career.after_race(st, result)
    save.store(st)
    local me = self
    sequence.play(files(out.videos), function()
        if out.action == "next" then
            me:pre_round("moved_on")
        elseif out.action == "gameover" then
            local entry = hall_entry(career.gameover(st))
            save.store(st)
            scenes.push(require("scenes.hall"), st, nil, function() me:pre_round("new") end, entry)
        else                                             -- the game completed
            local entry = hall_entry({ score = st.total, round = st.round, level = st.level })
            career.after_complete(st)
            save.store(st)
            scenes.push(require("scenes.hall"), st, nil, function()
                sequence.play(files(career.complete(st, {})), function() me:credits(function() me:pre_round("new") end) end)
            end, entry)
        end
    end)
end

-- The credits roll over SkyHolder, which then fades (RACEPRG 0x5198).
function S:credits(done)
    music.play(music.AFTER_RACE)
    sequence.play({ career.file(0x70) }, function()
        music.stop(true)
        if done then done() end
    end, { mute = true })
end

-- Quit from the options (RACEPRG 0x5113): Lance's farewell, the credits, exit.
function S:farewell()
    local me = self
    sequence.play(files(career.farewell(self.state)), function()
        me:credits(function() sys.quit() end)
    end)
end

-- Attract mode: B65 the first time, a demo race the next (0x4EA5..0x4EEA).
function S:attract()
    local st, me = self.state, self
    if not self.attract_seen then
        self.attract_seen = true
        sequence.play({ career.file(0x50) })
        return
    end
    self.attract_seen = false
    scenes.push(require("scenes.race"), data.TRACKS[1].video, {
        demo = true, car = "OM", rival = "LU", level = st.level, total = 0, round = 1,
        on_done = function()
            st.car = 1                                   -- the car choice resets ([0x69FA] = 0)
            me.focus = 0
            me:rebuild()
            me:pre_round("new")
        end,
    })
end

function S:update(dt)
    self.t = self.t + dt
    if input.any() then self.idle = 0 else self.idle = (self.idle or 0) + dt end
    if self.idle >= IDLE_SECONDS then
        self.idle = 0
        return self:attract()
    end
    if self.want_music then
        self.want_music = false
        music.set_enabled(self.state.music ~= false)
        music.play(music.AFTER_RACE)   -- song 6, as after the post-race videos (0x5198)
    end
    if self.note_t then self.note_t = self.note_t - dt; if self.note_t <= 0 then self.note = nil end end
    if input.pressed("F12") then scenes.push(require("scenes.explorer"), self.report); return end
    if input.pressed("S") then                           -- OFF -> AMER -> ENGL -> ... -> OFF
        local st, langs = self.state, subs.LANGUAGES
        local idx = 0
        for i, l in ipairs(langs) do if l.code == st.subtitles then idx = i end end
        idx = (idx + 1) % (#langs + 1)
        st.subtitles = idx == 0 and "OFF" or langs[idx].code
        save.store(st)
        apply_subtitles(st)
        self.note, self.note_t = "SUBTITLES " .. (idx == 0 and "OFF" or langs[idx].name), 2
        return
    end
    if input.pressed("M") then
        self.state.music = self.state.music == false
        save.store(self.state)
        music.set_enabled(self.state.music)
        if self.state.music then music.play(music.AFTER_RACE) end
        self.note, self.note_t = self.state.music and "MUSIC ON" or "MUSIC OFF", 2
        return
    end
    local f, un = self.focus, self.state.unlocked
    if input.pressed("Right") or input.pressed("Down") then f = career.step_focus(f, 1, un) end
    if input.pressed("Left") or input.pressed("Up") then f = career.step_focus(f, -1, un) end
    self.focus = f
    if input.pressed("Return") then
        if f <= 7 then self:start_round(f + 1)
        elseif f == 8 then scenes.push(require("scenes.options"), "level", self.state)
        elseif f == 9 then scenes.push(require("scenes.options"), "options", self.state)
        else scenes.push(require("scenes.hall"), self.state) end
    elseif (input.pressed("Space") or input.pressed("Escape")) and f <= 7 then
        scenes.push(require("scenes.player"), data.MENU_CARS[f + 1].video .. ".HNM",
            { hold = SHOWCASE_HOLD })
    end
end

-- Called when a pushed scene (video, race, hall, options) returns.
function S:resume() self:restore() end

local function centred(set, i)
    local w, h = set:size(i)
    local c = S.CENTRE[i + 1]
    if w and c then set:draw(i, c[1] - w // 2, c[2] - h // 2) end
end

function S:draw()
    gfx.clear(0)
    self.bg:draw(0, 0)
    local p, st = self.panel, self.state
    for m = 0, 7 do
        if st.unlocked[m + 1] then
            centred(p, m)
            centred(p, (self.focus == m and BAR_LIT or BAR_OFF) + m)
        end
    end
    for b = 0, 2 do
        centred(p, (self.focus == 8 + b and BUTTON_LIT or BUTTON_OFF) + b)
    end
    -- This port's: level, round and score on the globe's strip.
    -- In the palette's own nearest colours: changing an entry would also
    -- recolour the artwork's pixels that use it.
    local white, yellow = gfx.nearest(235, 235, 240), gfx.nearest(255, 210, 60)
    local names = data.track_names()
    local line = string.format("%s  ROUND %d  %s  SCORE %d", st.level, st.round, names[st.round] or "", st.total)
    gfx.fill(160 - #line * 2 - 3, 136, #line * 4 + 6, 8, 0)
    gfx.text(160 - #line * 2, 137, line, white)
    if self.note then
        gfx.fill(160 - #self.note * 2 - 3, 126, #self.note * 4 + 6, 8, 0)
        gfx.text(160 - #self.note * 2, 127, self.note, yellow)
    end
end

return S
