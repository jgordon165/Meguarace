-- Shown when --data doesn't point at a folder. The port ships no game data.

local ui = require("core.ui")
local S = {}

function S:enter(report) self.report = report; ui.install() end

function S:update()
    if input.pressed("Escape") then sys.quit() end
end

function S:draw()
    gfx.clear(ui.BG)
    ui.header("MEGARACE PORT")
    local y = 30
    local function line(t, c) gfx.text(16, y, t, c or ui.TEXT); y = y + 9 end
    line(self.report and "MEGARACE.DAT NOT FOUND IN THAT FOLDER" or "GAME DATA NOT FOUND", ui.WARN)
    y = y + 4
    line("THIS PORT USES THE FILES FROM YOUR OWN MEGARACE CD.")
    line("POINT IT AT THE CD (OR A COPY OF IT):")
    y = y + 4
    line("  MEGARACE --DATA D:\\", ui.HILITE)
    y = y + 4
    line("LOOKED IN: " .. sys.data_path, ui.DIM)
    y = y + 8
    ui.footer("ESC QUIT")
end

return S
