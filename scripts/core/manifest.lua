-- The files the port needs from the player's MegaRace CD, and a check that
-- they're present. Sizes are from the US v1.0.1 CD; another release only
-- produces a warning.

local sniff = require("formats.sniff")

local M = {}

-- On the CD root.
M.required = {
    { path = "MEGARACE.DAT", what = "game archive (videos, graphics, sound)", size = 515357744 },
}
M.optional = {
    { path = "RACEPRG.EXE", what = "original executable (reverse-engineering reference only)", size = 89672 },
}

-- Inside MEGARACE.DAT. Names come from the game's own file table.
M.archive = {
    intro   = { "MINDSCAP.HNM", "CRYO.HNM", "INTRO.HNM" },
    credits = { "CREDIT.HNM" },
    ending  = { "FIN.HNM" },
    -- Track videos come in pairs (first/second half) with matching .ZNN
    -- track data, e.g. NS11/NS12 = "NEW SAN" track 1.
    tracks  = { "NS1", "MV1", "FC1", "NGL", "SUB", "NS2", "MV2", "PAR", "FC2",
                "JNK", "MV3", "FC3", "NS3", "LST" },
    enemies = { "JOSE", "HOOPER", "LUIS", "OUZBEL", "MARIA", "PALOMA", "OMEGA", "RAMON" },
    subtitles = { en = "MEGAENGL.SUB", us = "MEGAAMER.SUB", fr = "MEGAFREN.SUB",
                  de = "MEGAGERM.SUB", it = "MEGAITAL.SUB", es = "MEGASPAN.SUB" },
}

function M.scan(dir, out)
    out = out or {}
    for _, e in ipairs(fs.list(dir or "")) do
        local rel = dir and dir ~= "" and (dir .. "/" .. e.name) or e.name
        if e.dir then
            -- Skip the unrelated EA demo that shares the CD.
            if rel:upper() ~= "EACDEMO" then M.scan(rel, out) end
        else
            out[#out + 1] = { path = rel, size = e.size }
        end
    end
    return out
end

function M.check()
    local report = { files = M.scan(), missing = {}, mismatched = {}, log = {}, total = 0 }
    for _, f in ipairs(report.files) do
        report.total = report.total + f.size
        f.kind = sniff.file(f.path, f.size)
    end
    for _, want in ipairs(M.required) do
        local sz = fs.size(want.path)
        if not sz then
            report.missing[#report.missing + 1] = want
        elseif want.size and sz ~= want.size then
            report.mismatched[#report.mismatched + 1] = want
        end
    end
    local log = report.log
    log[#log + 1] = string.format("%d files on disc, %.1f MB", #report.files, report.total / 1048576)
    for _, m in ipairs(report.missing) do log[#log + 1] = "MISSING " .. m.path .. " (" .. m.what .. ")" end
    for _, m in ipairs(report.mismatched) do
        log[#log + 1] = "NOTE: " .. m.path .. " differs from the US v1.0.1 CD; other releases are untested"
    end

    if fs.exists("MEGARACE.DAT") and dat.open("MEGARACE.DAT") then
        report.dat = true
        local n = #dat.list()
        log[#log + 1] = string.format("MEGARACE.DAT: %d files", n)
        for _, name in ipairs(M.archive.intro) do
            if not dat.exists(name) then log[#log + 1] = "archive is missing " .. name end
        end
    end
    report.ok = #report.missing == 0 and report.dat == true
    return report
end

return M
