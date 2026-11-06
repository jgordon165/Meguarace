#pragma once
// HERAD: Cryo's FM music format (Stéphane Picq's "Herbulot AdLib" system,
// also used by Dune). MegaRace's six songs are HERAD "SDB" files for one OPL2
// chip, HSQ-packed in MEGARACE.DAT: NEWSAN, MAEVA, DETRITUS, LENNY, ALARME
// and PAGA. Format notes: docs/formats.md, "Music".
//
// Song parses an unpacked file. Player is the sequencer: driven by a
// 200.299 Hz timer, it turns the song's MIDI-like tracks into OPL register
// writes. It has no audio code, so the tests check its writes directly;
// the engine feeds them to an emulated chip (src/fm.h).

#include <cstdint>
#include <vector>

namespace herad {

struct Write {
    uint8_t chip;   // 0 = first OPL, 1 = second (OPL3 "AGD" songs only)
    uint8_t reg;
    uint8_t val;
};

// One 40-byte instrument record. Signed fields are macro sensitivities.
struct Instrument {
    int8_t  mode = 0;          // 0/1 = instrument, 4 = AGD, -1 = keymap (v2)
    uint8_t mod_ksl = 0, mod_mul = 0, feedback = 0, mod_a = 0, mod_s = 0,
            mod_eg = 0, mod_d = 0, mod_r = 0, mod_out = 0, mod_am = 0,
            mod_vib = 0, mod_ksr = 0, con = 0, car_ksl = 0, car_mul = 0,
            pan = 0, car_a = 0, car_s = 0, car_eg = 0, car_d = 0, car_r = 0,
            car_out = 0, car_am = 0, car_vib = 0, car_ksr = 0;
    int8_t  fb_at = 0;         // feedback by aftertouch
    uint8_t mod_wave = 0, car_wave = 0;
    int8_t  mod_out_vel = 0, car_out_vel = 0, fb_vel = 0;   // by velocity
    uint8_t slide_coarse = 0, transpose = 0, slide_dur = 0;
    int8_t  slide_range = 0;
    int8_t  mod_out_at = 0, car_out_at = 0;                  // by aftertouch
    // Keymap (mode -1): instrument per note, from C2 + keymap_offset.
    uint8_t keymap_offset = 0;
    uint8_t keymap[36] = {};
};

struct Song {
    bool agd = false;          // OPL3 version (first track at 0x52)
    bool v2 = false;           // version 2: keymaps, 1-byte note-off
    uint16_t loop_start = 0, loop_end = 0, loop_count = 0;   // in measures
    uint16_t speed = 0;        // timer divider, 0x0100..0x8100
    std::vector<std::vector<uint8_t>> tracks;
    std::vector<Instrument> instruments;

    bool parse(const uint8_t* data, size_t size);
    bool parse(const std::vector<uint8_t>& d) { return parse(d.data(), d.size()); }
    uint32_t length_ticks() const;    // longest track, in sequencer ticks

    static constexpr double TIMER_HZ = 200.299;   // driver timer rate
    static constexpr int MEASURE_TICKS = 96;
};

class Player {
public:
    explicit Player(const Song& song, bool loop = true);

    // Back to the start; emits the chip set-up writes.
    void rewind(std::vector<Write>& out);
    // One 200.299 Hz timer interrupt. Returns false once the song has
    // ended (never, while looping).
    bool timer(std::vector<Write>& out);

    uint32_t position() const { return ticks_; }   // sequencer ticks played
    int loops() const { return loops_; }

private:
    struct Track { size_t pos = 0; uint32_t counter = 0; uint32_t wait = 0; };
    struct Channel {
        uint8_t program = 0, playprog = 0, note = 0, bend = 0x40, slide = 0;
        bool keyon = false;
    };

    void tick();
    uint32_t delta(int t);
    void command(int t);
    void note_on(int c, uint8_t note, uint8_t vel);
    void note_off(int c, uint8_t note);
    void program(int c, uint8_t prog);
    void aftertouch(int c, uint8_t vel);
    void bend(int c, uint8_t b);
    enum State { OFF, ON, UPDATE };
    void play(int c, uint8_t note, State st);
    void freq(int c, int oct, uint16_t fnum, bool on);
    void load(int c, uint8_t inst);
    void level(int c, uint8_t inst, int sens, uint8_t v, bool carrier);
    void feedback(int c, uint8_t inst, int sens, uint8_t v);
    void w(int c, uint8_t reg, uint8_t val);

    const Song& s_;
    bool loop_;
    std::vector<Write>* out_ = nullptr;
    int32_t time_ = 0;                // timer divider accumulator (16-bit in the driver)
    uint32_t ticks_ = 0;              // ticks played; starts at -1 (one spare tick)
    uint32_t total_ = 0;
    uint16_t loop_start_ = 0, loop_end_ = 0, loop_count_ = 0;
    bool ended_ = false;
    int loops_ = 0;
    std::vector<Track> trk_, saved_;
    uint32_t saved_ticks_ = 0;
    bool have_saved_ = false;
    std::vector<Channel> ch_;
};

}  // namespace herad
