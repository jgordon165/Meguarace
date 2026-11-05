-- Identifies files by magic bytes, so the explorer can tell executables,
-- sound and video apart before each format has a real decoder.
-- Signatures here are generic DOS-era formats, not MegaRace-specific
-- guesses; Cryo's own formats get entries once we've confirmed them on
-- your files.

local M = {}

local function u16(s, i) return s:byte(i) + s:byte(i + 1) * 256 end
local function u32(s, i) return u16(s, i) + u16(s, i + 2) * 65536 end

function M.bytes(head, size)
    if #head < 4 then return "tiny" end
    local m2 = head:sub(1, 2)
    if m2 == "MZ" or m2 == "ZM" then
        -- Look for a 32-bit LE/LX/PE image behind the DOS stub.
        if #head >= 0x40 then
            local off = u32(head, 0x3D)
            if off > 0 and off + 2 <= #head then
                local sig = head:sub(off + 1, off + 2)
                if sig == "LE" or sig == "LX" then return "exe-le32" end
                if sig == "PE" then return "exe-pe" end
            end
        end
        return "exe-mz"
    end
    if head:sub(1, 4) == "RIFF" then return "riff-" .. head:sub(9, 12):lower():gsub("%s", "") end
    if head:sub(1, 19) == "Creative Voice File" then return "voc" end
    if head:sub(1, 4) == "FORM" then return "iff" end
    if head:sub(1, 3) == "GIF" then return "gif" end
    if head:byte(1) == 0x0A and head:byte(2) <= 5 and head:byte(3) == 1 then return "pcx" end
    if head:sub(1, 3) == "HNM" or head:sub(1, 4) == "UBB2" then return "cryo-video?" end
    if size == 768 then return "palette?" end
    if size == 64000 or size == 64768 then return "screen320x200?" end
    local txt = 0
    for i = 1, math.min(#head, 128) do
        local b = head:byte(i)
        if b == 9 or b == 10 or b == 13 or (b >= 32 and b < 127) then txt = txt + 1 end
    end
    if txt >= math.min(#head, 128) * 0.95 then return "text" end
    return "data"
end

function M.file(path, size)
    local head = fs.read_range(path, 0, 512) or ""
    return M.bytes(head, size)
end

return M
