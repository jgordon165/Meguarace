-- MegaRace port: Lua entry point.
--
-- The C++ host calls game.init(), then game.update(dt) and game.draw() every
-- frame. All game data comes from the player's own MegaRace CD (--data),
-- read straight out of MEGARACE.DAT at runtime.

local scenes   = require("core.scenes")
local manifest = require("core.manifest")

game = {}

-- Original boot order: publisher logo, developer logo, then the intro.
local function playSequence(list, done)
    local i = 0
    local function step()
        i = i + 1
        if list[i] and dat.exists(list[i]) then
            scenes.push(require("scenes.player"), list[i], { on_done = step })
        elseif done then
            done()
        end
    end
    step()
end

function game.init()
    sys.log("data folder: " .. sys.data_path .. (fs.mounted() and "" or " (NOT FOUND)"))

    if not fs.mounted() then
        scenes.switch(require("scenes.nodata"))
        return
    end

    local report = manifest.check()
    for _, line in ipairs(report.log) do sys.log(line) end
    if not report.ok then
        scenes.switch(require("scenes.nodata"), report)
        return
    end

    -- Until menus and racing are reimplemented, the explorer is the hub.
    -- Skip the intro with --scripts tweaks or by pressing a key.
    scenes.switch(require("scenes.explorer"), report)
    if not os.getenv("MEGARACE_SKIP_INTRO") then
        playSequence(manifest.archive.intro)
    end
end

function game.update(dt)
    if input.pressed("Escape") and scenes.depth() <= 1 then sys.quit() end
    scenes.update(dt)
end

function game.draw()
    scenes.draw()
end

function game.shutdown()
    audio.stop_all()
end
