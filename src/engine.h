#pragma once
#include "audio.h"
#include "formats/cryo.h"
#include "input.h"
#include "video.h"
#include "vfs.h"

#include <string>

// Everything the Lua side can reach. No original game code or data is
// compiled in; assets are read at runtime from the folder the player
// points at with --data.
struct Engine {
    Vfs   data;        // the player's own CD / install folder
    Vfs   music;       // optional ripped CD audio (trackNN.wav); MegaRace v1.0.1 has none
    CryoDat dat;       // MEGARACE.DAT inside `data`
    Video video;
    Audio audio;
    Input input;
    bool  quit = false;
    int   exitCode = 0;
    std::string dataArg;
    std::string musicArg;
    // HD videos: folder of .hdv packs made by hd-conversion-utility
    // from the player's own videos (the Options > HD FOLDER setting, hd.cfg);
    // empty when HD is off or there are none.
    std::string hdDir;
    bool hdOff = false;       // --no-hd
    std::string hdUpscaler;   // what made the packs: "REAL-ESRGAN", "SHARP", "SMOOTH" or ""
    std::string displayArg;   // --display: overrides the saved display mode
    bool classic = false;     // --classic: play as the original this session (nothing saved)
};
