#include "hd/hdconfig.h"

#include <cctype>
#include <fstream>

#include "fsx.h"

namespace hd {

static std::string configFile(const std::string& userDir) { return fsx::join(userDir, "hd.cfg"); }

std::string defaultFolder(const std::string& userDir) { return fsx::join(userDir, "hd"); }

std::string configuredFolder(const std::string& userDir) {
    std::ifstream in(configFile(userDir));
    std::string line;
    if (!in || !std::getline(in, line)) return "";
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
    while (!line.empty() && line.front() == ' ') line.erase(line.begin());
    return line;
}

std::string packFolder(const std::string& userDir) {
    std::string c = configuredFolder(userDir);
    return c.empty() ? defaultFolder(userDir) : c;
}

bool setConfiguredFolder(const std::string& userDir, const std::string& folder) {
    if (folder.empty()) return fsx::remove(configFile(userDir));
    std::ofstream out(configFile(userDir), std::ios::trunc);
    out << folder << "\n";
    return bool(out);
}

// 1..8 letters, digits or underscores, upper-cased; else empty.
static std::string plainStem(std::string stem) {
    if (stem.empty() || stem.size() > 8) return "";
    for (auto& c : stem) {
        unsigned char u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && u != '_') return "";
        c = char(std::toupper(u));
    }
    return stem;
}

static std::string withExtension(const std::string& name, const char* ext, bool optional) {
    if (name.size() >= 4) {
        std::string e = name.substr(name.size() - 4);
        for (auto& c : e) c = char(std::toupper(static_cast<unsigned char>(c)));
        if (e == ext) return plainStem(name.substr(0, name.size() - 4));
    }
    return optional ? plainStem(name) : "";
}

std::string safeVideoStem(const std::string& name) { return withExtension(name, ".HNM", false); }
std::string safeSpriteStem(const std::string& name) { return withExtension(name, ".HSQ", true); }

}  // namespace hd
