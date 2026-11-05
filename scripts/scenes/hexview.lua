-- Hex dump of one file. HSQ files from the archive are shown decompressed.

local scenes = require("core.scenes")
local ui     = require("core.ui")
local bytes  = require("core.bytes")
local S = {}

local ROWS, WIDTH = 25, 16
local PAGE = ROWS * WIDTH

function S:enter(file, src)
    self.file, self.off = file, 0
    self.data = src.read(file.path) or ""
end

function S:update(dt)
    local max = math.max(0, #self.data - PAGE)
    if ui.repeating("Down", dt)     then self.off = self.off + WIDTH end
    if ui.repeating("Up", dt)       then self.off = self.off - WIDTH end
    if ui.repeating("PageDown", dt) then self.off = self.off + PAGE end
    if ui.repeating("PageUp", dt)   then self.off = self.off - PAGE end
    if input.pressed("Home") then self.off = 0 end
    if input.pressed("End")  then self.off = max end
    self.off = math.max(0, math.min(max + WIDTH - 1, self.off))
    self.off = self.off - self.off % WIDTH
    if input.pressed("Escape") or input.pressed("Backspace") then scenes.pop() end
end

function S:draw()
    gfx.clear(ui.BG)
    ui.header(self.file.path, self.file.kind)
    for i, l in ipairs(bytes.hexlines(self.data, self.off, ROWS, WIDTH)) do
        gfx.text(2, 4 + i * ui.LINE, l, ui.TEXT)
    end
    ui.footer(string.format("OFFSET %X / %X   UP DOWN PGUP PGDN HOME END   ESC BACK",
                            self.off, #self.data))
end

return S
