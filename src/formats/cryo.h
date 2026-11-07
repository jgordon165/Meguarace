#pragma once
// Cryo Interactive formats used by MegaRace, decoded at runtime from the
// player's own MEGARACE.DAT. Format notes: docs/formats.md.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

class Vfs;

// ---- MEGARACE.DAT archive --------------------------------------------------
class CryoDat {
public:
    struct Entry { std::string name; uint32_t size = 0, offset = 0; uint8_t flag = 0; };

    bool open(const Vfs& vfs, const std::string& datPath);
    bool isOpen() const { return vfs_ != nullptr; }
    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(const std::string& name) const;          // case-insensitive
    std::optional<std::vector<uint8_t>> read(const std::string& name) const;
    std::optional<std::vector<uint8_t>> readRange(const Entry& e, uint32_t off, uint32_t len) const;

private:
    const Vfs* vfs_ = nullptr;
    std::string path_;
    std::vector<Entry> entries_;
    std::map<std::string, size_t> index_;
};

// ---- HSQ (LZ77, checksum 0xAB) ---------------------------------------------
bool isHsq(const uint8_t* d, size_t n);
// Throws std::runtime_error on malformed input.
std::vector<uint8_t> unHsq(const uint8_t* d, size_t n);

// ---- Sprite sets (most graphics .HSQ files, after unpacking) ---------------
//   u16 table_offset
//   palette ranges (as in HNM) from byte 2 up to table_offset
//   u16 entry offsets, relative to table_offset; count = first offset / 2
//   entry: u16 width (bit 15 = PackBits-compressed), u8 height, u8 (0xFF), pixels
// Index 0 is transparent. Entries with width 0 are data tables, not images.
class SpriteSet {
public:
    struct Image { int w = 0, h = 0; std::vector<uint8_t> pixels; };

    bool load(const std::vector<uint8_t>& unpacked);   // throws on malformed data
    size_t count() const { return images_.size(); }
    const Image* image(size_t i) const {                // nullptr for data entries
        return i < images_.size() && images_[i].w > 0 ? &images_[i] : nullptr;
    }
    const std::vector<uint8_t>& paletteBlock() const { return palette_; }   // raw 6-bit ranges

private:
    std::vector<Image> images_;
    std::vector<uint8_t> palette_;
};

// ---- HNM (1) video ---------------------------------------------------------
class HnmVideo {
public:
    struct Frame {
        int w = 0, h = 0, x = 0, y = 0;
        bool overlay = false;               // mode 0xFF: index 0 is transparent
        bool hasVideo = false;
        bool paletteChanged = false;
        // Subtitle cue in the video header's high bits (RACEPRG 0xD24F):
        // 0x40 show the next line, 0x20 clear the line, 0 nothing.
        int subtitleCue = 0;
        std::vector<uint8_t> pixels;        // w*h indices
        std::vector<uint8_t> sound;         // 8-bit unsigned PCM for this frame
        std::map<std::string, std::vector<uint8_t>> chunks;   // "pt", "kl", ...
    };

    bool load(std::vector<uint8_t> file);  // whole .HNM in memory
    int  frameCount() const { return static_cast<int>(offsets_.size()); }
    int  soundRate() const { return soundRate_; }
    const std::array<uint8_t, 768>& palette() const { return pal_; }   // 8-bit RGB
    // Decodes frame i (sequential access is cheapest; frames are independent
    // except overlays, which the caller composites onto its screen).
    bool decode(int i, Frame& out);
    // Reads only the game-data chunks ("pt", "kl") of frame i, without
    // decoding the picture or sound. Cheap; used to index whole tracks.
    bool dataChunks(int i, std::map<std::string, std::vector<uint8_t>>& out) const;
    // The whole soundtrack (8-bit unsigned PCM, as decode() gives it frame
    // by frame), without decoding any picture, and its rate (0 if none). Cheap.
    std::vector<uint8_t> soundTrack(int* rate = nullptr) const;

private:
    std::vector<uint8_t> d_;
    std::vector<uint32_t> offsets_;         // absolute file offsets
    std::array<uint8_t, 768> pal_{};
    int soundRate_ = 0;
    bool vocHeaderSeen_ = false;
    size_t parsePalette(const uint8_t* p, size_t n);
    void addSound(const uint8_t* p, size_t n, std::vector<uint8_t>& out);
    static void appendVoc(const uint8_t* p, size_t n, bool& headerSeen, int* rate,
                          std::vector<uint8_t>& out);
};
