#include "hd/hdconfig.h"

#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace hd {

static fs::path configFile(const std::string& userDir) { return fs::path(userDir) / "hd.cfg"; }

std::string defaultFolder(const std::string& userDir) { return (fs::path(userDir) / "hd").string(); }

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
    std::error_code ec;
    if (folder.empty()) {
        fs::remove(configFile(userDir), ec);
        return !fs::exists(configFile(userDir), ec);
    }
    std::ofstream out(configFile(userDir), std::ios::trunc);
    out << folder << "\n";
    return bool(out);
}

std::string safeVideoStem(const std::string& name) {
    if (name.size() < 5 || name.size() > 12) return "";
    std::string ext = name.substr(name.size() - 4);
    for (auto& c : ext) c = char(std::toupper(static_cast<unsigned char>(c)));
    if (ext != ".HNM") return "";
    std::string stem = name.substr(0, name.size() - 4);
    for (auto& c : stem) {
        unsigned char u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && u != '_') return "";
        c = char(std::toupper(u));
    }
    return stem;
}

}  // namespace hd
