-- UI colours live at the top of the palette (250..255) so they don't clash
-- with game palettes loaded into the low indices while exploring.

local M = {
    BG = 250, PANEL = 251, DIM = 252, TEXT = 253, HILITE = 254, WARN = 255,
    LINE = 7, CHAR = 4,
}

function M.install()
    gfx.setcolor(M.BG,     12,  12,  20)
    gfx.setcolor(M.PANEL,  28,  30,  48)
    gfx.setcolor(M.DIM,   110, 116, 140)
    gfx.setcolor(M.TEXT,  225, 228, 240)
    gfx.setcolor(M.HILITE, 255, 170,  40)
    gfx.setcolor(M.WARN,  255,  80,  80)
end

-- Restores a plain grey ramp in 0..249 (default view for unknown images).
function M.grey()
    for i = 0, 249 do gfx.setcolor(i, i, i, i) end
end

function M.header(title, hint)
    gfx.fill(0, 0, gfx.width(), 9, M.PANEL)
    gfx.text(2, 2, title, M.HILITE)
    if hint then gfx.text(gfx.width() - #hint * M.CHAR - 2, 2, hint, M.DIM) end
end

function M.footer(text)
    gfx.fill(0, gfx.height() - 9, gfx.width(), 9, M.PANEL)
    gfx.text(2, gfx.height() - 7, text, M.DIM)
end

function M.human(n)
    if n >= 1048576 then return string.format("%.1fM", n / 1048576) end
    if n >= 1024 then return string.format("%.1fK", n / 1024) end
    return tostring(n)
end

-- Key repeat for held navigation keys.
local held = {}
function M.repeating(key, dt)
    if input.pressed(key) then held[key] = -0.35; return true end
    if input.down(key) then
        held[key] = (held[key] or 0) + dt
        if held[key] >= 0.04 then held[key] = 0; return true end
    else
        held[key] = nil
    end
    return false
end

return M
