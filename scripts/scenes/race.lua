-- A race: the game's rules (race/rules.lua) on a track video.
--   UP accelerate   DOWN brake   LEFT/RIGHT steer   SPACE fire (keys: game/keys.lua)   F1 debug line   ESC give up (game over, as in the original)
--
-- From RACEPRG.EXE: the video advances at a fixed rate whenever you're
-- moving; your speed decides how you gain on the rival. The lane is the
-- screen band under your car, and the tile in it takes effect as you reach
-- each section.
--
-- The opponents come one at a time (race/rival.lua): destroy the one on the
-- road and the next arrives. Destroy them all to win the round; when the
-- laps run out (3, 2 on HOLD) you qualify with at most one left and 1200
-- points.

local scenes = require("core.scenes")
local ui     = require("core.ui")
local track  = require("formats.track")
local cars   = require("formats.cars")
local voc    = require("formats.voc")
local rules  = require("race.rules")
local rivalai = require("race.rival")
local collide = require("race.collision")
local handling = require("race.handling")
local hud    = require("race.hud")
local font   = require("ui.rawfont")
local music  = require("game.music")
local S = {}

local TRACK_FPS = 12.5      -- sections per second while moving (the videos' rate)
local PLAYER_AHEAD = 1      -- sections between the camera and your car
local RIVAL_U = { 0.1, 0.9 }  -- the rival stays this far across the road (edges less half its width, 0x3B21)
local COUNTDOWN = 2.5       -- seconds of "GET READY"
local FX_FPS = 12.5         -- shot and explosion frames per second (one per video frame)
local GUN_SPREAD = { { 0 }, { -6, 6 }, { -6, 0, 6 } }   -- bolts per weapon (RACEPRG 0x2EF3)

-- Colours 112..127 belong to the effects: FUMEE and EXPLOSE draw with 112..126
-- and EXPLOSE carries their palette. The overlay text borrows two of those
-- (near-white 126, red-orange 118) and colour 127, which no sprite uses.
local C_TEXT, C_WARN, C_GOOD = 126, 118, 127

function S:enter(name, opts)
    opts = opts or {}
    ui.install()
    self.T = track.open(name)
    self.rules = rules.new(opts.car or "JO")
    self.pos = 0.0
    self.carx, self.view, self.steer = 160, 0, 0
    self.steering, self.body_lean = { lean = 0, vel = 0, ticks = 0 }, 0
    self.car = cars.open(opts.car or "JO")
    self.rival = cars.open_rival(opts.rival or "RA")
    self.zones = spr.open("ZONES_") or spr.open("ZONES")   -- scanner panel + icons
    self.fx = spr.open("FUMEE")              -- muzzle flash, bolt, impact
    self.boom = spr.open("EXPLOSE")          -- destroyed rival
    self.fx_t = 0
    self.title = opts.title
    self.hold = name == "HOLD"                -- the last track: parked rivals, no AI
    self.last_round = self.hold               -- no opponents digit there
    self.level = opts.level
    self.laps = self.hold and 2 or 3          -- [0x68C5]
    -- Opponents to beat (DS:0x846; HOLD draws 2..8).
    local round = opts.round or 1
    self.left = self.hold and math.random(2, 8) or (rivalai.OPPONENTS[round] or 4)
    self.rivals = {}
    self:spawn(rivalai.new(self.left, self.hold and rivalai.NEXT_AHEAD or rivalai.START_AHEAD, 0))
    self.next_decision = 0
    self.total = opts.total or 0              -- championship score before this race
    self.debug = false
    self.x_limit = name:match("^JNK") and 60 or 0
    self.round = opts.round or 1
    self.curv, self.curv_hist, self.curv_frame = 0, {}, -1
    self.demo = opts.demo                     -- attract mode: the game drives (RACEPRG [0x69E6])
    self.demo_fire_at = 0
    self.on_done = opts.on_done
    self.keys = opts.keys or require("game.keys").DEFAULT   -- game/keys.lua
    self.ticks = 0
    self.clock = -COUNTDOWN
    self.last_section = nil
    self.finished = false
    self.spin, self.spin_t = 0, 0
    self.frame = -1
    -- Everything else a race leaves behind starts clear, so a race after a
    -- lost one (the scene is reused) begins as a new one.
    self.result, self.wreck_frame, self.radar_quiet_until = nil, nil, nil
    self.at_wall, self.scrape_sound, self.smoke_frame, self.sway = nil, false, nil, 0
    self.was_skid, self.was_slide = false, false
    self.road_w, self.carbox, self.cartop, self.cary = nil, nil, nil, nil
    music.play(music.song_for_video(name))          -- the round's song (RACEPRG 0x02AA)
end

function S:leave() ui.install(); audio.stop_all(); music.stop(true) end

-- Puts rival r on the road (the only one: they come one at a time).
function S:spawn(r)
    r.bump = 0
    r.ahead = PLAYER_AHEAD + r.pos / 8       -- sections ahead of the camera, for drawing
    self.rivals = { r }
    self.current = r
end

function S:update(dt)
    if self.demo and input.any() then return self:end_demo() end   -- any key (0x082A)
    if input.pressed("Escape") then                   -- giving up: [0x68CB] = -4 (0x0A2B)
        if not self.finished then self:finish("quit") end
        local cb, result = self.on_done, self.result
        scenes.pop()
        if cb then cb(result) end
        return
    end
    if input.pressed("F1") then self.debug = not self.debug end
    self.clock = self.clock + dt
    if self.finished then
        if self.demo then return self:end_demo() end
        if input.pressed("Return") or input.pressed("Space") then
            local cb, result = self.on_done, self.result
            scenes.pop()
            if cb then cb(result) end
        end
        return
    end
    if self.clock < 0 then return end                     -- countdown

    local s = self.rules
    local alive = not s.destroyed
    self.steer = 0
    local K = self.keys
    local left, right = input.down(K.left), input.down(K.right)
    if self.demo then left, right = self:autopilot_steer() end
    if alive and left then self.steer = self.steer + 1 end
    if alive and right then self.steer = self.steer - 1 end   -- both cancel (0x2860)
    if self.steer == 0 then collide.release(s) end

    -- Game ticks.
    self.ticks = self.ticks + dt * rules.TICKS_PER_SECOND
    while self.ticks >= 1 do
        self.ticks = self.ticks - 1
        -- Wheels (0x25F8): every 36 - 2 x (speed / 10) ticks the phase [0xCDA]
        -- steps 2, 1, 0, 2, ...: phase 0 is the body alone, 1 and 2 add
        -- the _B and _C wheel images.
        self.spin_t = self.spin_t + 1
        if self.spin_t >= 36 - 2 * (s.speed // 10) then
            self.spin_t = 0
            self.spin = self.spin - 1
            if self.spin < 0 then self.spin = 2 end
        end
        local accel, brake = input.down(K.accel), input.down(K.brake)
        if self.demo then accel, brake = s.slide == nil, false end   -- full throttle unless spinning (0x2D0F)
        local _, drift = rules.step(s, { accel = alive and accel, brake = not alive or brake })
        if self.demo then self:autopilot_fire() end
        self.carx = self.carx + drift
        -- Curves push you outward, up to the car's handling (race/handling.lua).
        local steering = self.steer ~= 0
        local p = handling.push(self.curv, s.speed, steering, s.car.handling)
        self.body_lean = handling.body_lean(self.body_lean, p)
        if s.skid or s.scraping then p = 0 end
        -- Your steering (0x2833): a sideways speed that builds and eases.
        local st = self.steering
        -- A skid or a slide starting stops it (0x1A82, 0x1E84, 0x2252, 0x3C21).
        if (s.skid and not self.was_skid) or (s.slide and not self.was_slide) then st.vel = 0 end
        self.was_skid, self.was_slide = s.skid ~= nil, s.slide ~= nil
        if not s.slide then handling.steer(st, -self.steer, s.speed, s.car.handling, p) end
        self.carx = self.carx + st.vel
        self.carx = self.carx + handling.tick_dx(p, self.carx, self.curv, s.speed, steering)
        -- Walls: steering ticks in a row ([0x6939]), then scrape or crash.
        if alive and collide.wall(s, self.at_wall, st.ticks) == "crash" then voc.play("CHOCRR2") end
        self:rival_tick()
        -- Eased view angle toward (160 - x)/8 (RACEPRG 0x24A5).
        local target = (160 - self.carx) / 8
        local stepv = s.speed // 40 + 1
        if self.view < target then self.view = math.min(target, self.view + stepv)
        elseif self.view > target then self.view = math.max(target, self.view - stepv) end
    end
    -- The car's x stays within the track's limits (RACEPRG 0x0311: DS:0x31A
    -- per round, 0 except 60 on JNK; the right one is 320 minus it, [0xCD2]
    -- and [0xCD6]). Pinned at one, you're at the wall (0x23B8).
    self.at_wall = nil
    local lim = self.x_limit
    if self.carx <= lim then self.carx, self.at_wall = lim, "left"
    elseif self.carx >= 320 - lim then self.carx, self.at_wall = 320 - lim, "right" end
    if self.at_wall and s.speed > 0 and alive then
        if not self.scrape_sound then voc.play("CRRR3"); self.scrape_sound = true end
    else
        self.scrape_sound = false
    end

    -- Scenery: fixed rate while moving.
    local moving = s.speed > 0
    if moving then self.pos = self.pos + TRACK_FPS * dt end
    -- Curvature: once per video frame (RACEPRG 0x1838).
    local fnow = math.floor(self.pos)
    while self.curv_frame < fnow do
        self.curv_frame = self.curv_frame + 1
        self.curv = handling.curvature(self.curv_hist, handling.frame_delta(self.T, self.curv_frame, self.round))
    end
    local last = self.T.frames - 1
    if self.T.lap > 0 then last = math.min(last, self.laps * self.T.lap) end
    if self.pos >= last then
        self.pos = last
        self:finish("laps")
        return
    end

    -- Tile under the car, once per section.
    local si = math.floor(self.pos) + 1 + PLAYER_AHEAD
    if si ~= self.last_section then
        self.last_section = si
        local lane = rules.lane_for_x(self.carx)
        local code = self.T:tile(si, lane)
        if code ~= 0 then
            local sound = rules.apply_tile(s, code, self.steer)
            if sound then voc.play(sound) end
        end
    end

    if alive and input.pressed(self.keys.fire) then
        if rules.fire(s) then voc.play("LASER") end
    end
    self.fx_t = self.fx_t + dt
    while self.fx_t >= 1 / FX_FPS do
        self.fx_t = self.fx_t - 1 / FX_FPS
        self:effects_frame()
    end

    local r = self.current
    if r then r.ahead = PLAYER_AHEAD + r.pos / 8 end

end

-- Attract mode's driver (the game's): on the straight it steers toward
-- x 130..190, on curves not at all ([0x6808], 0x22FD); it fires with the
-- rival 10-20 eighths ahead and within 50 pixels across, at most every 30
-- ticks (0x2C02).
function S:autopilot_steer()
    local d = handling.demo_steer(self.curv or 0, self.carx)
    return d < 0, d > 0
end

function S:autopilot_fire()
    local s, r = self.rules, self.current
    if not r or r.destroyed or not r.x_lane then return end
    if r.pos <= 10 or r.pos >= 20 or math.abs(r.x_lane - self.carx) > 50 then return end
    if s.tick < self.demo_fire_at then return end
    self.demo_fire_at = s.tick + 30
    if rules.fire(s) then voc.play("LASER") end
end

function S:end_demo()
    local cb = self.on_done
    scenes.pop()
    if cb then cb({ demo = true }) end
end

-- Collisions with the rival (race/collision.lua, RACEPRG 0x3A8F), then a
-- tick of pushes and skids.
function S:collisions(r, prev)
    local s = self.rules
    if self.carbox and r.x_lane and not s.destroyed then
        local b = self.carbox
        local half = b[3] / 2
        local span = r.box and { r.box[1], r.box[1] + r.box[3] } or { r.x_lane - half, r.x_lane + half }
        local kind = collide.classify(r.pos, r.pos ~= prev, span, { b[1], b[1] + b[3] }, r.destroyed)
        if kind then
            if kind == "ahead" or kind == "behind" then r.pos = prev end
            if collide.respond(s, r, kind, r.x_lane > self.carx, self.hold) then voc.play("CHOC4") end
        else
            collide.clear(s)
        end
    end
    local dx, sway, rdx = collide.tick(s, r, math.max(0, self.frame))
    self.carx = self.carx + dx
    self.sway = sway
    if rdx ~= 0 and self.road_w and self.road_w > 0 then
        r.u = math.max(0.05, math.min(0.95, r.u + rdx / self.road_w))
    end
    if s.armour <= 0 and not s.destroyed then               -- 0x1940: out of armour
        s.armour, s.destroyed = 0, true
        self:wreck()
    end
end

-- Is rival r lined up with your car (its place across the road inside your
-- car's width, [0xCC4]..[0xCC6])?
function S:lined_up(r)
    if not r or not self.carbox or not r.x_lane then return false end
    return r.x_lane >= self.carbox[1] and r.x_lane <= self.carbox[1] + self.carbox[3]
end

-- The rival's sideways move (race/rival.lua, RACEPRG 0x3996): in pixels at
-- its depth, kept off the road's edges, where its lean resets (0x39EC).
-- This port keeps its place as a fraction of the road, so on a curve it
-- follows the road rather than the screen.
function S:rival_side(r)
    local w = r.w_px or self.road_w or 0
    if r.pos >= 150 then r.u = 0.5 end                      -- 0x3A18
    local dx = rivalai.side_step(r, self.rules.tick, w, (r.u - 0.5) * w)
    if dx ~= 0 and w > 0 then
        local u = r.u + dx / w
        if u < RIVAL_U[1] or u > RIVAL_U[2] then r.lean = 0 end
        r.u = math.max(RIVAL_U[1], math.min(RIVAL_U[2], u))
    end
end

-- One game tick for the rival: movement, being left behind, and every 100
-- ticks (50 on HARDENED) an AI decision while you're moving (RACEPRG 0x3467).
function S:rival_tick()
    local s, r = self.rules, self.current
    if not r or r.out then return end
    if self.hold and not r.destroyed then r.speed = 0 end   -- HOLD's rivals are parked (0x4319)
    local prev = r.pos
    local res = rivalai.tick(r, s.speed)
    self:rival_side(r)
    self:collisions(r, prev)
    if not r.destroyed then
        if res == "lost" or (self.hold and r.pos <= rivalai.LOST_BEHIND) then
            self:destroy(r)                                 -- left behind (0x3E7F)
            return
        end
    end
    if self.hold or r.destroyed or s.speed <= 0 or s.tick < self.next_decision then return end
    self.next_decision = s.tick + rivalai.decision_interval(self.level)
    local fire = rivalai.decide(r, {
        player_speed = s.speed, spinning = s.slide ~= nil,
        lined_up = self:lined_up(r), x = r.x_lane, shot_free = s.rshot == nil,
    })
    if fire and rules.rival_fire(s) then
        s.rshot.from = r
        voc.play("LASER", 0.5)
    end
end

-- Where the shot is this frame: muzzle and centre bolt (nil before the
-- first draw has placed the car).
function S:shot_position()
    if not self.cary then return nil end
    local off, dx, dy = rules.shot_aim(self.view)
    local mx, my = self.carx + off, self.cartop + 6
    return mx, my, mx + dx, my + dy
end

-- One effects frame: the shot moves on and is tested against the rivals,
-- explosions advance (RACEPRG 0x2E14, 0x2F93, 0x4688).
function S:effects_frame()
    local s = self.rules
    -- Scraping a wall: smoke on that side, 1 armour a cycle (0x301B).
    if s.scraping and s.speed > 0 and not s.destroyed then
        self.smoke_frame = collide.smoke(s)
        if self.smoke_frame == collide.SMOKE_FIRST then voc.play("CRRR3") end
    else
        self.smoke_frame = nil
    end
    if s.shot then
        local result = rules.shot_step(s)
        if result == "kill" and s.shot_target then
            self:destroy(s.shot_target)
        end
        if result then s.shot_target = nil end
    end
    local sh = s.shot
    if sh and not sh.hit then
        local _, _, bx = self:shot_position()
        if bx then
            for _, r in ipairs(self.rivals) do
                local _, _, _, range = rules.shot_aim(self.view)
                if r.box and not r.destroyed and r.pos >= 0 and r.pos <= range
                   and bx >= r.box[1] and bx <= r.box[1] + r.box[3] then
                    s.shot_target = r
                    voc.play("CHOCTIR")
                    if rules.hit(s, r) then self:destroy(r) end
                    break
                end
            end
        end
    end
    -- The rival's shot at you.
    if s.rshot then
        local res = rules.rival_shot_step(s)
        if res == "hit" then voc.play("CHOC4")
        elseif res == "destroyed" then self:wreck() end
    end
    local from = s.rshot and s.rshot.from
    if from and not s.rshot.hit and not from.destroyed and self:lined_up(from)
       and from.pos <= -rules.RIVAL_HIT_BEHIND[1] and from.pos >= -rules.RIVAL_HIT_BEHIND[2] then
        rules.rival_hit(s)
        voc.play("CHOCTIR")
    end
    if self.wreck_frame then
        self.wreck_frame = self.wreck_frame + 1
        if self.wreck_frame >= rules.EXPLOSION_FRAMES then self:finish("wrecked") end
    end
    for _, r in ipairs(self.rivals) do
        if r.boom and not r.out then
            r.boom = r.boom + 1
            if r.box then s.points = s.points + rules.EXPLOSION_POINTS end
            if r.boom >= rules.EXPLOSION_FRAMES then
                r.out = true
                self:next_rival()
            end
        end
    end
end

-- Your armour ran out (RACEPRG 0x1940): EXPLOD2 and the explosion over your
-- car, which is gone after frame 15; then the race is lost ([0x68CB] = -2).
function S:wreck()
    if self.wreck_frame then return end
    self.wreck_frame = 0
    voc.play("EXPLOD2")
end

-- After a rival's explosion the next one comes in, 28 sections ahead at
-- your speed; when none are left the round is won (RACEPRG 0x3F89).
function S:next_rival()
    self.left = self.left - 1
    if self.left <= 0 then
        self.left = 0
        self:finish("won")
        return
    end
    self:spawn(rivalai.new(self.left, rivalai.NEXT_AHEAD, self.rules.speed))
    self.next_decision = self.rules.tick
end

-- outcome: "won", "laps", "wrecked" or "quit" (ESC). A rival already
-- destroyed but still exploding counts as gone (0x0A3B: its damage [0xD30]
-- at the car's limit [0x814]).
function S:finish(outcome)
    if self.finished then return end
    self.finished = true
    local s = self.rules
    if outcome ~= "won" then
        for _, r in ipairs(self.rivals) do
            if r.destroyed and not r.out then self.left = math.max(0, self.left - 1) end
        end
    end
    local qualified = rivalai.qualifies(outcome, self.left, s.points)
    self.result = { outcome = outcome, left = self.left, points = s.points, qualified = qualified }
    voc.play(qualified and "BONUS11" or "MALUS2")
end

-- A rival at full damage blows up and falls back (RACEPRG 0x3E98: EXPLOD2,
-- its speed set to yours - 20); after the explosion it is out of the race.
function S:destroy(r)
    if r.boom then return end
    r.destroyed, r.damage, r.boom = true, rules.RIVAL_HEALTH, 0
    r.speed = math.max(0, self.rules.speed - 20)
    self.radar_quiet_until = self.rules.tick + 50       -- [0x68EB] = 1 for 50 ticks
    voc.play("EXPLOD2")
end

-- Damage bar over a hit rival: (100 - damage) / 2 pixels, segmented every
-- 4th pixel, in dashboard colours (RACEPRG 0x0FC3..0x101E).
local function health_bar(cx, y, damage)
    local x, y = math.floor(cx) - 26, math.floor(y)
    gfx.fill(x, y, 52, 6, 74)
    local n = (rules.RIVAL_HEALTH - damage) // 2
    for i = 0, n - 1 do
        local c = (i % 4 == 3) and 74 or (i < 16 and 77 or 76)
        gfx.fill(x + 1 + i, y + 1, 1, 4, c)
    end
end

local function edges(T, cam, si)
    local sec = T.sections[si]
    if not sec then return nil end
    local lx, ly = T:project(cam, table.unpack(sec.road[2]))
    local rx, ry = T:project(cam, table.unpack(sec.road[3]))
    if not lx or not rx then return nil end
    return lx, ly, rx, ry
end

-- Draws sprite i of a set centred on x with its top at y (how the game
-- places every dashboard image: RACEPRG 0x7041 centres on x).
local function draw_top(set, i, x, y)
    local w = set:size(i)
    if w then set:draw(i, math.floor(x - w // 2), math.floor(y)) end
end

-- Tile scanner: the next three rows ahead, five lanes, ZONES_ icons on the
-- ZONES_ panel, at the car's place on its dashboard (RACEPRG 0x0EA2, 0x10A6).
function S:draw_scanner(base)
    if not self.zones then return end
    local at = hud.SCANNER_AT[self.car.code]
    if not at then return end
    local x0, y0 = at[1], at[2] + hud.DASH_Y
    local pw = self.zones:size(20)
    if pw then draw_top(self.zones, 20, x0 + pw // 2, y0) end
    for row = 0, 2 do
        local si = base + PLAYER_AHEAD + 1 + row * 2
        for lane = 0, 4 do
            local code = self.T:tile(si, lane)
            if code ~= 0 then
                local icon = (code >= 17) and (math.min(code, 25) - 7) or (code - 1)
                draw_top(self.zones, icon, x0 + 9 + lane * 10, y0 + 20 - row * 10)
            end
        end
    end
end

-- The gauges, lamps and radar (RACEPRG 0x115E..0x1243).
function S:draw_gauges()
    local set, code, s = self.car.gauges, self.car.code, self.rules
    if not set then return end
    local v = hud.values(s)
    for name, tbl in pairs(hud.GAUGES) do
        local g = tbl[code]
        if g then draw_top(set, hud.frame(g, v[name], hud.max_for(name, s)), g[1], g[2] + hud.DASH_Y) end
    end
    -- Radar: the screen, then the nearest rival's blip (or the jammed blink).
    local rd = hud.RADAR[code]
    if not rd then return end
    local y = rd[2] + hud.DASH_Y
    draw_top(set, rd[3] + 2, rd[1], y)
    if s.timers[6] then
        draw_top(set, rd[3] + (s.tick & 1), rd[1], y)
        return
    end
    if self.radar_quiet_until and s.tick < self.radar_quiet_until then return end
    local near = self.current
    if near and not near.out and not near.destroyed then
        local f = rd[3] + 3 + hud.blip(near.pos // 8)
        local bw = set:size(f) or 0
        draw_top(set, f, hud.blip_x(rd, near.u, bw), y)
    end
end

-- Status line and the opponents digit (RACEPRG 0x150A): LAPS: n at the top
-- left, the score in six digits at the top right.
-- In 16:9 they move out to the screen's edges (gfx.wing() each side).
function S:draw_status(lap)
    local wing = gfx.wing()
    font.draw(2 - wing, 6, "LAPS:", C_TEXT)
    font.draw(50 - wing, 6, tostring(math.max(0, self.laps - (lap - 1))), C_TEXT)
    font.draw(250 + wing, 6, string.format("%06d", math.min(999999, self.total + self.rules.points)), C_TEXT)
    local at = hud.OPPONENTS_AT[self.car.code]
    if at and at[1] ~= 0 and not self.last_round then
        font.draw_small(at[1], at[2] + hud.DASH_Y, tostring(math.min(9, self.left)), C_TEXT)
    end
end

-- The rival's shot from behind: its muzzle flash and bolt rise from the
-- bottom of the road at the rival's place; a hit plays the impact on you.
function S:draw_rival_shot()
    local sh = self.rules.rshot
    if not sh or not self.fx or not sh.from then return end
    if sh.hit then
        if self.carbox then
            local b = self.carbox
            self:draw_fx(self.fx, sh.frame, b[1] + b[3] / 2, b[2] + b[4] / 2)
        end
        return
    end
    local x = sh.from.x_lane
    if not x then return end
    local y = hud.DASH_Y - 8
    if sh.muzzle <= 13 then self:draw_fx(self.fx, sh.muzzle, x, y) end
    if sh.frame < rules.SHOT.bolt_end then self:draw_fx(self.fx, sh.frame, x, y - 17) end
end

-- Draws sprite i of a set centred on (x, y).
function S:draw_fx(set, i, x, y, scale)
    local w, h = set:size(i)
    if not w then return end
    scale = scale or 1
    set:draw(i, math.floor(x - w * scale / 2), math.floor(y - h * scale / 2), scale)
end

-- Muzzle flash at the car and the bolts ahead of it, one per gun.
function S:draw_shot()
    local sh = self.rules.shot
    if not sh or not self.fx then return end
    local mx, my, bx, by = self:shot_position()
    if not mx then return end
    if sh.muzzle <= 13 then self:draw_fx(self.fx, sh.muzzle, mx, my) end
    if not sh.hit and sh.frame < rules.SHOT.bolt_end then
        for _, d in ipairs(GUN_SPREAD[sh.guns] or GUN_SPREAD[1]) do
            self:draw_fx(self.fx, sh.frame, bx + d, by)
        end
    end
end

function S:draw()
    local T, s = self.T, self.rules
    local f = math.floor(self.pos)
    if f ~= self.frame then
        local info = T.video:decode(f)
        if self.frame < 0 or f == 0 or info.palette_changed then
            T.video:apply_palette()
            self.car:apply_palette()
            self.rival:apply_palette()
            if self.boom then self.boom:apply_palette() end
        end
        self.frame = f
    end
    gfx.clear(0)
    T.video:draw(0, track.VIDEO_Y)
    if not self.boom then
        gfx.setcolor(C_TEXT, 235, 235, 240)
        gfx.setcolor(C_WARN, 255, 80, 60)
    end
    gfx.setcolor(C_GOOD, 90, 255, 120)

    local cam = T:camera(f)
    local base = f + 1
    local plx, ply, prx, pry = edges(T, cam, base + PLAYER_AHEAD)
    local ref = plx and math.max(1, prx - plx) or 1

    -- Rivals, far to near, hidden behind scenery by the kl masks.
    table.sort(self.rivals, function(a, b) return a.ahead > b.ahead end)
    local sh = s.shot
    for _, r in ipairs(self.rivals) do
        r.x, r.box = nil, nil
        r.x_lane = plx and (plx + (prx - plx) * r.u) or nil    -- its place across the road, at your car
        local ahead = math.floor(r.ahead)
        if not r.out and ahead >= PLAYER_AHEAD and ahead < track.WINDOW - 2 then
            local lx, ly, rx, ry = edges(T, cam, base + ahead)
            if lx then
                local x, y = lx + (rx - lx) * r.u, ly + (ry - ly) * r.u
                r.w_px = rx - lx
                local scale = math.max(0.05, (rx - lx) / ref)
                local sec = T.sections[base + ahead]
                local dh = ((sec.heading - cam.heading + 1440) % 2880) - 1440
                local angle = (dh / 8) * 96 / 180 + (160 - x) / 8 * scale - r.lean   -- less its lean (0x4574)
                local L = T:mask(f, ahead)
                if L then gfx.clip(L.lines, track.VIDEO_Y) end
                local fx_scale = math.max(0.4, math.min(1, scale * 1.5))
                local bl, bt, bw, bh
                if not r.boom or r.boom < 4 then          -- the car vanishes into the blast
                    bl, bt, bw, bh = self.rival:draw(math.floor(angle), x, y, scale)
                end
                if bl then r.box = { bl, bt, bw, bh } else r.box = { x - 20 * scale, y - 30 * scale, 40 * scale, 30 * scale } end
                local cy = r.box[2] + r.box[4] / 2
                if sh and sh.hit and s.shot_target == r and self.fx then
                    self:draw_fx(self.fx, sh.frame, x, cy, fx_scale)
                end
                if r.boom and self.boom then
                    self:draw_fx(self.boom, math.min(r.boom, rules.EXPLOSION_FRAMES - 1), x, cy, fx_scale)
                end
                gfx.unclip()
                if r.damage > 0 and not r.destroyed then health_bar(x, r.box[2] - 8, r.damage) end
                r.x = x
            end
        end
    end

    if plx then
        self.road_w = prx - plx
        local y = ply + (pry - ply) * (self.carx - plx) / math.max(1, prx - plx)
        local left, top, w, h
        if not self.wreck_frame or self.wreck_frame < 15 then
            left, top, w, h = self.car:draw(math.floor(self.view - self.steering.lean - self.body_lean + (self.sway or 0)), self.carx, y, 1.0,
                                            (s.speed > 0 and self.spin > 0) and (self.spin - 1) or nil)
        end
        if left then self.carbox = { left, top, w, h } end
        self.cary, self.cartop = y, top or (y - 30)
        if self.smoke_frame and self.fx and s.scraping then           -- 20 px out on the wall's side
            self:draw_fx(self.fx, self.smoke_frame, self.carx + (s.scraping == "left" and -20 or 20), self.cartop)
        end
        self:draw_shot()
        self:draw_rival_shot()
        if self.wreck_frame and self.boom and self.carbox then
            local b = self.carbox
            self:draw_fx(self.boom, math.min(self.wreck_frame, rules.EXPLOSION_FRAMES - 1),
                         b[1] + b[3] / 2, b[2] + b[4] / 2)
        end
    end

    -- The cockpit, scanner and gauges: in 16:9 stretched across the whole
    -- screen (the engine's HUD layer); what's drawn after stays on top.
    gfx.hud_begin()
    self.car:draw_dashboard()
    self:draw_scanner(base)
    self:draw_gauges()
    gfx.hud_end()

    local lap = T.lap > 0 and math.min(self.laps, f // T.lap + 1) or 1
    self:draw_status(lap)
    if self.debug then
        local r = self.current
        gfx.fill(0, 20, 320, 8, 0)
        gfx.text(2, 21, string.format("OPP %d RIV %s %+d/8 V%d SPD %d EN %d AR %d PTS %d THR %d",
            self.left, r and r.state or "-", r and r.pos or 0, r and r.speed or 0,
            s.speed // 10, s.energy, s.armour, s.points, s.thrill), C_TEXT)
    end

    if s.message and s.tick < s.message_until then
        local w = #s.message * 4
        gfx.fill(160 - w // 2 - 3, 32, w + 6, 9, 0)
        gfx.text(160 - w // 2, 34, s.message, s.message:find("+", 1, true) and C_GOOD or C_TEXT)
    end

    if self.demo and (s.tick // 50) % 2 == 0 then               -- 0x14B8: blinks every 50 ticks
        font.draw(60, 32, "PRESS BUTTON TO START", C_TEXT)
    end
    if self.clock < 0 then
        local t = "GET READY"
        gfx.fill(100, 70, 120, 13 + (self.title and 10 or 0), 0)
        gfx.text(160 - #t * 2, 74, t, C_TEXT)
        if self.title then gfx.text(160 - #self.title * 2, 84, self.title, C_GOOD) end
    elseif self.clock < 1 then
        gfx.text(156, 74, "GO", C_GOOD)
    end
    if self.finished then
        local res = self.result
        local t1 = ({ won = "ALL OPPONENTS DESTROYED", laps = "END OF RACE", wrecked = "DESTROYED", quit = "GAVE UP" })[res.outcome]
        local t2 = res.qualified and "QUALIFIED" or
            (res.outcome == "laps" and string.format("%d OPPONENTS LEFT - NOT QUALIFIED", res.left) or "NOT QUALIFIED")
        local t3 = "PRESS ENTER"
        gfx.fill(60, 64, 200, 38, 0)
        gfx.text(160 - #t1 * 2, 68, t1, C_TEXT)
        gfx.text(160 - #t2 * 2, 80, t2, res.qualified and C_GOOD or C_WARN)
        gfx.text(160 - #t3 * 2, 92, t3, C_TEXT)
    end
end

return S
