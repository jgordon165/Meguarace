#pragma once
// FM music: a HERAD song (formats/herad.h) played on an emulated OPL2 chip
// (ymfm's YM3812, BSD-3 licensed) and resampled to the output rate. No SDL,
// so the tests render it directly; the mixer (audio.cpp) calls render() from
// the audio thread.

#include "formats/herad.h"

#include <cstdint>
#include <memory>
#include <vector>

class FmMusic {
public:
    FmMusic();
    ~FmMusic();

    // `song` is an unpacked HERAD file. Returns false if it doesn't parse or
    // needs an OPL3 (AGD songs; MegaRace has none).
    bool load(const uint8_t* song, size_t size, bool loop = true);
    bool playing() const { return playing_; }

    // Adds `frames` stereo frames at `rate` Hz to `out`, scaled by `gain`.
    void render(int32_t* out, int frames, int rate, float gain);

    static double chip_rate();     // the OPL2's own sample rate, 49716 Hz

private:
    struct Chip;
    int16_t next_sample();

    std::unique_ptr<Chip> chip_;
    herad::Song song_;
    std::unique_ptr<herad::Player> player_;
    std::vector<herad::Write> writes_;
    bool playing_ = false;
    double timer_acc_ = 0;         // native samples until the next 200.299 Hz tick
    double pos_ = 0;               // resampler read position between prev_ and cur_
    int16_t prev_ = 0, cur_ = 0;
};
