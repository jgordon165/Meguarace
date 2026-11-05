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

    -- Main menu (F12 there opens the asset explorer). The boot videos play
    -- on top of it; any key skips one. MEGARACE_SKIP_INTRO=1 skips them all.
    local menu = require("scenes.menu")
    menu.report = report
    scenes.switch(menu)
    if not os.getenv("MEGARACE_SKIP_INTRO") then
        playSequence(manifest.archive.intro)
    end
end

function game.update(dt)
    scenes.update(dt)
end

function game.draw()
    scenes.draw()
end

function game.shutdown()
    audio.stop_all()
end
