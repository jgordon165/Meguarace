#include "fsx.h"

#ifndef XENON
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fsx {

#ifndef XENON
bool isDir(const std::string& p) { std::error_code ec; return fs::is_directory(p, ec); }
bool exists(const std::string& p) { std::error_code ec; return fs::exists(p, ec); }
std::optional<uint64_t> fileSize(const std::string& p) {
    std::error_code ec;
    auto s = fs::file_size(p, ec);
    if (ec) return std::nullopt;
    return uint64_t(s);
}
bool remove(const std::string& p) { std::error_code ec; fs::remove(p, ec); return !fs::exists(p, ec); }
std::vector<DirEntry> list(const std::string& dir) {
    std::vector<DirEntry> out;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        DirEntry e;
        e.name = de.path().filename().string();
        e.dir = de.is_directory(ec);
        e.size = e.dir ? 0 : uint64_t(de.file_size(ec));
        out.push_back(std::move(e));
    }
    return out;
}
std::string canonical(const std::string& p) {
    std::error_code ec;
    auto c = fs::canonical(p, ec);
    return ec ? p : c.string();
}
#else
bool isDir(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool exists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0; }
std::optional<uint64_t> fileSize(const std::string& p) {
    struct stat st;
    if (::stat(p.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return std::nullopt;
    return uint64_t(st.st_size);
}
bool remove(const std::string& p) { ::unlink(p.c_str()); return !exists(p); }
std::vector<DirEntry> list(const std::string& dir) {
    std::vector<DirEntry> out;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent* de = ::readdir(d)) {
        std::string n = de->d_name;
        if (n == "." || n == "..") continue;
        DirEntry e;
        e.name = n;
        std::string full = join(dir, n);
        struct stat st;
        if (::stat(full.c_str(), &st) == 0) {
            e.dir = S_ISDIR(st.st_mode);
            e.size = e.dir ? 0 : uint64_t(st.st_size);
        }
        out.push_back(std::move(e));
    }
    ::closedir(d);
    return out;
}
std::string canonical(const std::string& p) { return p; }
#endif

static bool isSep(char c) { return c == '/' || c == '\\'; }

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (isSep(a.back())) return a + b;
    return a + "/" + b;
}

std::string filename(const std::string& p) {
    size_t i = p.size();
    while (i > 0 && !isSep(p[i - 1]) && p[i - 1] != ':') --i;
    return p.substr(i);
}

std::string extension(const std::string& p) {
    std::string f = filename(p);
    size_t dot = f.rfind('.');
    if (dot == std::string::npos || dot == 0) return "";
    return f.substr(dot);
}

}  // namespace fsx
