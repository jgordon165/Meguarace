-- Shows any file as raw 8-bit indexed pixels. Uncompressed backgrounds and
-- sprite sheets pop out visually once the width is right. Palettes: grey
-- ramp by default, or press P with the offset on a 768-byte VGA palette to
-- load it (6-bit values, as the VGA DAC uses).

local scenes = require("core.scenes")
local ui     = require("core.ui")
local S = {}

function S:enter(file, src)
    self.file, self.off, self.w = file, 0, 320
    -- Whole file in memory (decompressed if HSQ); game files are small
    -- apart from videos, which have their own player.
    self.data = src and src.read(file.path) or fs.read(file.path) or ""
    self.file = { path = file.path, size = #self.data, kind = file.kind }
    ui.grey()
end

local function slice(self, off, n) return self.data:sub(off + 1, off + n) end

function S:leave() ui.install() end

function S:update(dt)
    local shift = input.down("Left Shift") or input.down("Right Shift")
    local step = shift and 1 or 16
    if ui.repeating("Right", dt) then self.w = math.min(640, self.w + (shift and 1 or 8)) end
    if ui.repeating("Left", dt)  then self.w = math.max(8, self.w - (shift and 1 or 8)) end
    if ui.repeating("Down", dt)  then self.off = self.off + (shift and 1 or self.w * step) end
    if ui.repeating("Up", dt)    then self.off = math.max(0, self.off - (shift and 1 or self.w * step)) end
    if ui.repeating("PageDown", dt) then self.off = self.off + self.w * 180 end
    if ui.repeating("PageUp", dt)   then self.off = math.max(0, self.off - self.w * 180) end
    if input.pressed("Home") then self.off = 0 end
    if input.pressed("P") then
        local pal = slice(self, self.off, 768)
        if pal and #pal == 768 then gfx.palette(pal:sub(1, 750), 0, true) end
    end
    if input.pressed("G") then ui.grey() end
    if input.pressed("S") then
        local name = string.format("shot_%s_%X.bmp", self.file.path:gsub("[/\\]", "_"), self.off)
        sys.log((gfx.screenshot(name) and "saved " or "failed ") .. name)
    end
    self.off = math.min(self.off, math.max(0, self.file.size - 1))
    if input.pressed("Escape") or input.pressed("Backspace") then scenes.pop() end
end

function S:draw()
    gfx.clear(0)
    local h = gfx.height() - 18
    local w = math.min(self.w, gfx.width())
    local data = slice(self, self.off, self.w * h)
    -- Rows wider than the screen are cropped; narrower ones are packed.
    for row = 0, h - 1 do
        local s = data:sub(row * self.w + 1, row * self.w + w)
        if #s == 0 then break end
        gfx.blit(0, 9 + row, #s, 1, s)
    end
    ui.header(self.file.path, string.format("W %d  OFF %X", self.w, self.off))
    ui.footer("ARROWS W/OFF  SHIFT FINE  P PAL AT OFF  G GREY  S SAVE  ESC")
end

return S
