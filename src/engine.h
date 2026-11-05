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
    std::string dataArg;
    std::string musicArg;
};
