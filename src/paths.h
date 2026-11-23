#pragma once
// Where the game's own files and the player's are, per platform.
//
//   base()  the folder holding scripts/ and soundfont/ (the program's)
//   pref()  the player's folder: settings, scores, saves (ends in a slash)
//   root()  consoles: the port's folder on the card or stick, holding
//           data/ (the CD's files) and hd/; empty elsewhere
//
// Most platforms ask SDL (SDL_GetBasePath, SDL_GetPrefPath). The Wii U
// (Aroma) has neither in SDL: the .wuhb's own content is /vol/content
// (scripts, the SoundFont) and the player's folder is on the SD card,
// sd:/wiiu/apps/meguarace/ (README-wiiu.txt).
#include <SDL.h>
#include <string>
#ifdef MEGARACE_WIIU_LAYOUT
#include <sys/stat.h>
#endif

namespace paths {

#ifdef MEGARACE_WIIU_LAYOUT
inline std::string root() { return "fs:/vol/external01/wiiu/apps/meguarace/"; }
inline std::string base() {
    // scripts/ on the card wins (to try script changes without a new .wuhb).
    struct stat st;
    if (::stat((root() + "scripts").c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return root();
    return "fs:/vol/content/";
}
inline std::string pref() {
    const std::string p = root() + "save/";
    ::mkdir("fs:/vol/external01/wiiu", 0777);
    ::mkdir("fs:/vol/external01/wiiu/apps", 0777);
    ::mkdir(root().c_str(), 0777);
    ::mkdir(p.c_str(), 0777);
    return p;
}
#else
inline std::string root() { return {}; }
inline std::string base() {
    char* b = SDL_GetBasePath();
    std::string r = b ? b : "./";
    SDL_free(b);
    return r;
}
inline std::string pref() {
    char* p = SDL_GetPrefPath("Meguarace", "Meguarace");
    std::string r = p ? p : "";
    SDL_free(p);
    return r;
}
#endif

}  // namespace paths
