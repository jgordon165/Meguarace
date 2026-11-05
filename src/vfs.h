#pragma once
// Read-only virtual file system over the user's own MegaRace CD/install folder.
// DOS file names are 8.3 and usually upper case, so lookups are
// case-insensitive on every path component regardless of host OS.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class Vfs {
public:
    struct Entry {
        std::string name;   // name as it exists on disk
        bool        isDir;
        uint64_t    size;
    };

    // Returns false if the folder doesn't exist; the engine still runs so the
    // Lua side can tell the player what to do.
    bool mount(const std::filesystem::path& root);
    bool mounted() const { return mounted_; }
    const std::filesystem::path& root() const { return root_; }

    // `rel` uses '/' or '\\' separators and any letter case.
    std::optional<std::filesystem::path> resolve(const std::string& rel) const;
    bool exists(const std::string& rel) const { return resolve(rel).has_value(); }
    std::optional<uint64_t> size(const std::string& rel) const;
    std::optional<std::vector<uint8_t>> read(const std::string& rel) const;
    // Reads `count` bytes at `offset` (for large video files).
    std::optional<std::vector<uint8_t>> readRange(const std::string& rel,
                                                  uint64_t offset, uint64_t count) const;
    std::vector<Entry> list(const std::string& rel) const;

private:
    std::filesystem::path root_;
    bool mounted_ = false;
};
