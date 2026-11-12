#include "vfs.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "fsx.h"

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

bool Vfs::mount(const std::string& root) {
    mounted_ = fsx::isDir(root);
    root_ = mounted_ ? fsx::canonical(root) : root;
    return mounted_;
}

std::optional<std::string> Vfs::resolve(const std::string& rel) const {
    if (!mounted_) return std::nullopt;
    std::string cur = root_;
    for (const auto& part : splitPath(rel)) {
        if (part == "." ) continue;
        if (part == "..") return std::nullopt;   // stay inside the game folder
        const std::string want = lower(part);
        bool found = false;
        for (const auto& de : fsx::list(cur)) {
            if (lower(de.name) == want) {
                cur = de.path;
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
    return fsx::fileSize(*p);
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
    for (const auto& de : fsx::list(*p)) out.push_back(Entry{de.name, de.dir, de.size});
    std::sort(out.begin(), out.end(),
              [](const Entry& a, const Entry& b) { return lower(a.name) < lower(b.name); });
    return out;
}
