#pragma once
// Bounds-checked little-endian reader for writing C++ decoders of the
// original file formats (video, sprites, track data) once they're identified.
// Lightweight parsing can stay in Lua with string.unpack; heavy decoders
// (per-frame video) belong here for speed.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

class BinReader {
public:
    BinReader(const uint8_t* data, size_t size) : p_(data), n_(size) {}
    explicit BinReader(const std::vector<uint8_t>& v) : p_(v.data()), n_(v.size()) {}

    size_t pos()  const { return pos_; }
    size_t size() const { return n_; }
    size_t left() const { return n_ - pos_; }
    bool   eof()  const { return pos_ >= n_; }

    void seek(size_t p) { need(p, 0); pos_ = p; }
    void skip(size_t k) { need(pos_, k); pos_ += k; }

    uint8_t  u8()  { need(pos_, 1); return p_[pos_++]; }
    int8_t   s8()  { return static_cast<int8_t>(u8()); }
    uint16_t u16() { need(pos_, 2); uint16_t v = uint16_t(p_[pos_] | p_[pos_ + 1] << 8); pos_ += 2; return v; }
    int16_t  s16() { return static_cast<int16_t>(u16()); }
    uint32_t u32() { uint32_t lo = u16(); return lo | uint32_t(u16()) << 16; }
    int32_t  s32() { return static_cast<int32_t>(u32()); }

    const uint8_t* bytes(size_t k) { need(pos_, k); const uint8_t* r = p_ + pos_; pos_ += k; return r; }
    std::string str(size_t k) { const uint8_t* b = bytes(k); return std::string(b, b + k); }

private:
    void need(size_t at, size_t k) const {
        if (at > n_ || k > n_ - at) throw std::out_of_range("BinReader: read past end");
    }
    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
};
