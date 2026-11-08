-- MegaRace port: Lua entry point.
--
-- The C++ host calls game.init(), then game.update(dt) and game.draw() every
-- frame. All game data comes from the player's own MegaRace CD (--data),
-- read straight out of MEGARACE.DAT at runtime.

local scenes   = require("core.scenes")
local manifest = require("core.manifest")

game = {}


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
    -- on top of it, as in RACEPRG.EXE (0x4DFB..0x4E85): the publisher and
    -- developer logos, the show's intro, the channel ident and Lance on the
    -- round ahead. ENTER skips one, ESC skips the rest.
    -- MEGARACE_SKIP_INTRO=1 skips them all.
    -- Display mode saved in the options (the engine has already picked the
    -- largest that fits, or the one given with --display).
    require("game.display").apply_saved(require("game.save").load())

    local menu = require("scenes.menu")
    menu.report = report
    scenes.switch(menu)
    if not os.getenv("MEGARACE_SKIP_INTRO") then
        -- The port's own title screen first (scenes/title.lua), then the boot
        -- videos; ESC on the title skips those too.
        scenes.push(require("scenes.title"), { on_done = function(skip_all)
            if not skip_all then menu:pre_round("boot", { "MINDSCAP.HNM", "CRYO.HNM" }) end
        end })
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
