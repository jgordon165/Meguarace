-- Plays a run of videos one after another (files from MEGARACE.DAT), then
-- calls done(). ESC in any of them skips the rest of the run; missing files
-- are passed over.

local scenes = require("core.scenes")
local M = {}

function M.play(files, done, opts)
    local i = 0
    local function step(how)
        if how == "all" then i = #files end
        i = i + 1
        while files[i] and not dat.exists(files[i]) do i = i + 1 end
        if files[i] then
            local o = { on_done = step }
            if opts then for k, v in pairs(opts) do o[k] = v end end
            scenes.push(require("scenes.player"), files[i], o)
        elseif done then
            done(how == "all")
        end
    end
    step()
end

return M
