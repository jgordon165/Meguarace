-- Little-endian binary reader over a Lua string, built on string.unpack.
-- For lightweight formats (headers, tables, palettes, track data). Per-frame
-- video decoding should move to C++ (src/formats/) for speed.

local M = {}
M.__index = M

function M.new(data, pos)
    return setmetatable({ data = data, pos = pos or 1 }, M)
end

function M:left() return #self.data - self.pos + 1 end
function M:seek(offset0) self.pos = offset0 + 1 end   -- 0-based like a hex editor
function M:tell() return self.pos - 1 end
function M:skip(n) self.pos = self.pos + n end

local function rd(self, fmt)
    local v, nxt = string.unpack(fmt, self.data, self.pos)
    self.pos = nxt
    return v
end

function M:u8()  return rd(self, "<I1") end
function M:s8()  return rd(self, "<i1") end
function M:u16() return rd(self, "<I2") end
function M:s16() return rd(self, "<i2") end
function M:u32() return rd(self, "<I4") end
function M:s32() return rd(self, "<i4") end
function M:bytes(n)
    local s = self.data:sub(self.pos, self.pos + n - 1)
    self.pos = self.pos + n
    return s
end
function M:cstr(n)   -- fixed-size, NUL-padded string (DOS file names etc.)
    return (self:bytes(n):gsub("%z.*", ""))
end

-- Hex dump lines for the explorer.
function M.hexlines(data, offset0, rows, width)
    width = width or 16
    local out = {}
    for r = 0, rows - 1 do
        local base = offset0 + r * width
        if base >= #data then break end
        local hex, asc = {}, {}
        for i = 0, width - 1 do
            local b = data:byte(base + i + 1)
            if b then
                hex[#hex + 1] = string.format("%02X", b)
                asc[#asc + 1] = (b >= 32 and b < 127) and string.char(b) or "."
            else
                hex[#hex + 1] = "  "
            end
        end
        out[#out + 1] = string.format("%06X %s %s", base, table.concat(hex, " "), table.concat(asc))
    end
    return out
end

return M
