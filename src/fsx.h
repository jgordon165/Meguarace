#pragma once
// The few file-system calls the engine needs, on std::filesystem where the
// platform has it and on plain stat/opendir where it doesn't (the Xbox 360's
// LibXenon C++ library has no directory support in std::filesystem).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fsx {

struct DirEntry {
    std::string name;   // as on disk
    std::string path;   // the folder and the name, as the listing gives it
    bool dir = false;
    uint64_t size = 0;
};

bool isDir(const std::string& p);
bool exists(const std::string& p);
std::optional<uint64_t> fileSize(const std::string& p);
bool remove(const std::string& p);
// Entries of a folder (without "." and ".."), unsorted; empty if it can't be read.
std::vector<DirEntry> list(const std::string& dir);
// A full, normalised path where the platform can make one; else `p`.
std::string canonical(const std::string& p);

std::string join(const std::string& a, const std::string& b);
std::string filename(const std::string& p);
std::string extension(const std::string& p);   // with the dot, as is ("" if none)

}  // namespace fsx
