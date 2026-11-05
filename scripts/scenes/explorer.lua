-- Asset explorer: browses the files inside MEGARACE.DAT (or the plain game
-- folder if the archive isn't found). The workbench for mapping formats.
--   ENTER  play an .HNM video / hex-view anything else
--   V      view as raw 8-bit pixels (HSQ files are shown decompressed)

local scenes = require("core.scenes")
local ui     = require("core.ui")
local S = {}

local ROWS = 22

local KIND = {
    HNM = "video", HSQ = "packed", VOC = "sound", ZNN = "track data",
    SUB = "subtitles", RAW = "font", BIN = "data",
}

-- A source abstracts "read bytes of file X" over the archive or the folder.
local function datSource()
    local files = {}
    for _, e in ipairs(dat.list()) do
        local ext = e.name:match("%.(%w+)$") or ""
        files[#files + 1] = { path = e.name, size = e.size, kind = KIND[ext:upper()] or ext }
    end
    return {
        title = "MEGARACE.DAT",
        files = files,
        read  = function(p) return dat.read(p) end,           -- unpacked if HSQ
        range = function(p, off, n) return dat.read_range(p, off, n) end,
    }
end

local function folderSource(report)
    return {
        title = "GAME FOLDER",
        files = report.files,
        read  = function(p) return fs.read(p) end,
        range = function(p, off, n) return fs.read_range(p, off, n) end,
    }
end

function S:enter(report)
    ui.install()
    self.src = report.dat and datSource() or folderSource(report)
    self.files = self.src.files
    self.total = 0
    for _, f in ipairs(self.files) do self.total = self.total + f.size end
    self.sel, self.top = 1, 1
end

function S:update(dt)
    local n = #self.files
    if n == 0 then return end
    if ui.repeating("Down", dt)     then self.sel = math.min(n, self.sel + 1) end
    if ui.repeating("Up", dt)       then self.sel = math.max(1, self.sel - 1) end
    if ui.repeating("PageDown", dt) then self.sel = math.min(n, self.sel + ROWS) end
    if ui.repeating("PageUp", dt)   then self.sel = math.max(1, self.sel - ROWS) end
    if self.sel < self.top then self.top = self.sel end
    if self.sel >= self.top + ROWS then self.top = self.sel - ROWS + 1 end

    local f = self.files[self.sel]
    if input.pressed("Return") then
        local base = f.path:upper():match("^(.+)%.HNM$")
        if base and dat.exists(base .. ".ZNN") then
            scenes.push(require("scenes.trackview"), base)     -- race track: drive it
        elseif base then
            scenes.push(require("scenes.player"), f.path, { hud = true })
        else
            scenes.push(require("scenes.hexview"), f, self.src)
        end
    end
    if input.pressed("V") then scenes.push(require("scenes.rawview"), f, self.src) end
end

function S:draw()
    gfx.clear(ui.BG)
    ui.header(self.src.title, string.format("%d FILES %s", #self.files, ui.human(self.total)))
    for i = 0, ROWS - 1 do
        local idx = self.top + i
        local f = self.files[idx]
        if not f then break end
        local y = 12 + i * ui.LINE
        local c = ui.TEXT
        if idx == self.sel then
            gfx.fill(0, y - 1, gfx.width(), ui.LINE, ui.PANEL)
            c = ui.HILITE
        end
        local name = f.path
        if #name > 46 then name = ".." .. name:sub(-44) end
        gfx.text(4, y, name, c)
        gfx.text(196, y, ui.human(f.size), ui.DIM)
        gfx.text(236, y, f.kind, ui.DIM)
    end
    ui.footer("ENTER PLAY/DRIVE/HEX   V RAW IMAGE   ESC QUIT")
end

return S
