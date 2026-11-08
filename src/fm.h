#pragma once
// The game's music: a HERAD song (formats/herad.h) in one of three styles.
//   Original   the emulated OPL2 (ymfm's YM3812, BSD-3), mono, as the game
//              sounded on a Sound Blaster.
//   Enhanced   the same FM notes on an emulated OPL3 (YMF262), which can
//              send each voice to its own output: the tracks spread across
//              the stereo field (bass and drums in the middle), then a
//              warmer EQ and a small room reverb (music_fx.h).
//   SoundFont  the song's notes played by a General MIDI SoundFont
//              (TinySoundFont, MIT) instead of the chip: each FM instrument
//              mapped to a GM program or drum (formats/gm_map.h), with the
//              same reverb.
// No SDL, so the tests render it directly; the mixer (audio.cpp) calls
// render() from the audio thread.

#include "formats/herad.h"
#include "music_fx.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class MusicStyle { Original = 0, Enhanced = 1, SoundFont = 2 };

// A SoundFont (.sf2), loaded once and shared: each song plays on a cheap
// copy that shares the samples.
class SoundFontBank {
public:
    static std::shared_ptr<SoundFontBank> load(const std::string& path);
    ~SoundFontBank();
    void* synth() const { return tsf_; }       // tsf*
    const std::string& path() const { return path_; }

private:
    void* tsf_ = nullptr;
    std::string path_;
};

class FmMusic {
public:
    // SoundFont without a bank plays Enhanced (style() says which).
    explicit FmMusic(MusicStyle style = MusicStyle::Original, std::shared_ptr<SoundFontBank> bank = nullptr);
    ~FmMusic();

    // `song` is an unpacked HERAD file. Returns false if it doesn't parse or
    // needs an OPL3 (AGD songs; MegaRace has none). `start` skips ahead to
    // that sequencer position (switching styles mid-song). `gm` overrides the
    // SoundFont instrument per song instrument: a GM program 0..127, 128 + a
    // GM drum key, or -1 for the automatic choice.
    bool load(const uint8_t* song, size_t size, bool loop = true, uint32_t start = 0,
              const std::vector<int>* gm = nullptr);
    bool playing() const { return playing_; }
    MusicStyle style() const { return style_; }
    uint32_t position() const { return player_ ? player_->position() : 0; }

    // Adds `frames` stereo frames at `rate` Hz to `out`, scaled by `gain`.
    void render(int32_t* out, int frames, int rate, float gain);

    static double chip_rate();     // the OPL2's own sample rate, 49716 Hz

private:
    struct Chip2;
    struct Chip3;
    void apply(std::vector<herad::Write>& w);
    bool tick_timer();                       // one 200.299 Hz interrupt
    int16_t next_sample();                   // Original: one chip sample
    void next_frame3(float& l, float& r);    // Enhanced: one chip frame
    void render_chip3(int32_t* out, int frames, int rate, float gain);
    void render_sf(int32_t* out, int frames, int rate, float gain);
    void sf_setup(int rate);
    void sf_events(bool notes);

    MusicStyle style_;
    std::shared_ptr<SoundFontBank> bank_;
    void* sf_ = nullptr;                     // tsf* (this song's copy)
    std::unique_ptr<Chip2> chip2_;
    std::unique_ptr<Chip3> chip3_;
    herad::Song song_;
    std::unique_ptr<herad::Player> player_;
    std::vector<herad::Write> writes_;
    std::vector<herad::Event> events_;
    bool playing_ = false;
    double timer_acc_ = 0;         // samples until the next 200.299 Hz tick
    double pos_ = 0;               // resampler read position between prev_ and cur_
    int16_t prev_ = 0, cur_ = 0;
    float prevL_ = 0, prevR_ = 0, curL_ = 0, curR_ = 0;
    std::vector<uint8_t> bus_;     // Enhanced: OPL3 output bits per voice
    // SoundFont
    struct Voice { int program = -1, drum = -1, shift = 0; };
    std::vector<Voice> gm_;        // per song instrument
    struct Held { int ch = -1, key = -1; };
    Held held_[9];
    int chProgram_[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
    int sfRate_ = 0;
    musicfx::Chain fx_;
    std::vector<float> buf_;
};
