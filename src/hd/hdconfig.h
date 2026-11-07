#pragma once
// Where HD packs live: a setting of the game (Options > HD FOLDER), kept in
// hd.cfg in the Meguarace user folder so both the game and hd-conversion-utility read
// the same place. hd.cfg holds one line, the folder; no file or an empty
// line means the default, <user folder>/hd.
//
// Neither program takes the folder on its command line, and pack file names
// are only ever the archive's own video names, checked by safeVideoStem().

#include <string>

namespace hd {

// userDir: the Meguarace user folder (SDL_GetPrefPath("Meguarace", "Meguarace")).
std::string defaultFolder(const std::string& userDir);
std::string configuredFolder(const std::string& userDir);   // "" = default
std::string packFolder(const std::string& userDir);         // configured, else default
// Saves the setting ("" = back to the default). False if hd.cfg can't be written.
bool setConfiguredFolder(const std::string& userDir, const std::string& folder);

// "B01.HNM" -> "B01": the stem of a video name as the archive has them
// (1..8 letters, digits or underscores, then .HNM, any case), upper-cased.
// Empty for anything else, so a name can never point outside the folder.
std::string safeVideoStem(const std::string& name);

}  // namespace hd
