-- Creative Voice (.VOC) sound effects from MEGARACE.DAT.
-- The game's effects are a single type-1 block: 8-bit unsigned mono PCM
-- with a time-constant byte (rate = 1,000,000 / (256 - tc)). Other block
-- types are skipped.

local M = {}

-- Three effects on the disc (MALUS2, MALUS7, BONUS15) declare a time
-- constant of 6, i.e. 4 kHz, far below every other effect (7.5-9.6 kHz).
-- Treat implausibly low rates as this typical rate instead.
M.MIN_RATE = 5000
M.FALLBACK_RATE = 8000

-- Returns pcm (string), rate; or nil, error.
function M.decode(d)
    if not d or #d < 26 or d:sub(1, 19) ~= "Creative Voice File" then
        return nil, "not a VOC file"
    end
    local p = (d:byte(21) | d:byte(22) << 8) + 1
    local chunks, rate = {}, nil
    while p <= #d do
        local t = d:byte(p)
        if t == 0 then break end
        if p + 3 > #d then break end
        local n = d:byte(p + 1) | d:byte(p + 2) << 8 | d:byte(p + 3) << 16
        local body = p + 4
        if t == 1 and n >= 2 then
            rate = rate or math.floor(1000000 / (256 - d:byte(body)))
            if d:byte(body + 1) == 0 then                 -- 0 = 8-bit PCM
                chunks[#chunks + 1] = d:sub(body + 2, body + n - 1)
            end
        elseif t == 2 then                                -- continuation
            chunks[#chunks + 1] = d:sub(body, body + n - 1)
        end
        p = body + n
    end
    if not rate then return nil, "no sound data" end
    if rate < M.MIN_RATE then rate = M.FALLBACK_RATE end
    return table.concat(chunks), rate
end

-- Cache of decoded effects by name ("BONUS1"), loaded from the archive.
local cache = {}
function M.play(name, volume)
    local e = cache[name]
    if e == nil then
        local d = dat.read(name .. ".VOC", false)
        local pcm, rate = M.decode(d)
        e = pcm and { pcm = pcm, rate = rate } or false
        cache[name] = e
    end
    if e then return audio.pcm(e.pcm, e.rate, 8, 1, volume or 1.0) end
end

return M
