#include "vfs.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace fs = std::filesystem;

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::vector<std::string> splitPath(const std::string& rel) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : rel) {
        if (c == '/' || c == '\\') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

bool Vfs::mount(const fs::path& root) {
    std::error_code ec;
    mounted_ = fs::is_directory(root, ec);
    root_ = mounted_ ? fs::canonical(root, ec) : root;
    return mounted_;
}

std::optional<fs::path> Vfs::resolve(const std::string& rel) const {
    if (!mounted_) return std::nullopt;
    fs::path cur = root_;
    for (const auto& part : splitPath(rel)) {
        if (part == "." ) continue;
        if (part == "..") return std::nullopt;   // stay inside the game folder
        const std::string want = lower(part);
        std::error_code ec;
        bool found = false;
        for (const auto& de : fs::directory_iterator(cur, ec)) {
            if (lower(de.path().filename().string()) == want) {
                cur = de.path();
                found = true;
                break;
            }
        }
        if (!found) return std::nullopt;
    }
    return cur;
}

std::optional<uint64_t> Vfs::size(const std::string& rel) const {
    auto p = resolve(rel);
    if (!p) return std::nullopt;
    std::error_code ec;
    auto s = fs::file_size(*p, ec);
    if (ec) return std::nullopt;
    return s;
}

std::optional<std::vector<uint8_t>> Vfs::read(const std::string& rel) const {
    auto s = size(rel);
    if (!s) return std::nullopt;
    return readRange(rel, 0, *s);
}

std::optional<std::vector<uint8_t>> Vfs::readRange(const std::string& rel,
                                                   uint64_t offset, uint64_t count) const {
    auto p = resolve(rel);
    if (!p) return std::nullopt;
    std::ifstream f(*p, std::ios::binary);
    if (!f) return std::nullopt;
    f.seekg(static_cast<std::streamoff>(offset));
    std::vector<uint8_t> buf(count);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(count));
    buf.resize(static_cast<size_t>(f.gcount()));
    return buf;
}

std::vector<Vfs::Entry> Vfs::list(const std::string& rel) const {
    std::vector<Entry> out;
    auto p = resolve(rel);
    if (!p) return out;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(*p, ec)) {
        Entry e;
        e.name  = de.path().filename().string();
        e.isDir = de.is_directory(ec);
        e.size  = e.isDir ? 0 : de.file_size(ec);
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(),
              [](const Entry& a, const Entry& b) { return lower(a.name) < lower(b.name); });
    return out;
}
