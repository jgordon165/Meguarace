#include "formats/herad.h"

#include <algorithm>

namespace herad {

namespace {

constexpr size_t HEADER = 52;
constexpr size_t INST_SIZE = 40;
constexpr int MAX_TRACKS = 21;
constexpr int VOICES = 9;               // per OPL chip

// Operator offset of each voice's modulator (the carrier is +3).
constexpr uint8_t SLOT[VOICES] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
// F-numbers of C..B for the driver's octave layout.
constexpr uint16_t FNUM[12] = {343, 364, 385, 408, 433, 459, 486, 515, 546, 579, 614, 650};
// Pitch-bend steps: fine (per semitone, up to the next note) and coarse.
constexpr uint8_t FINE_BEND[13] = {19, 21, 21, 23, 25, 26, 27, 29, 31, 33, 35, 36, 37};
constexpr uint8_t COARSE_BEND[10] = {0, 5, 10, 15, 20, 0, 6, 12, 18, 24};

uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

Instrument decode(const uint8_t* p) {
    Instrument i;
    auto s = [&](int k) { return int8_t(p[k]); };
    i.mode = s(0);
    i.mod_ksl = p[2];  i.mod_mul = p[3];  i.feedback = p[4];  i.mod_a = p[5];
    i.mod_s = p[6];    i.mod_eg = p[7];   i.mod_d = p[8];     i.mod_r = p[9];
    i.mod_out = p[10]; i.mod_am = p[11];  i.mod_vib = p[12];  i.mod_ksr = p[13];
    i.con = p[14];     i.car_ksl = p[15]; i.car_mul = p[16];  i.pan = p[17];
    i.car_a = p[18];   i.car_s = p[19];   i.car_eg = p[20];   i.car_d = p[21];
    i.car_r = p[22];   i.car_out = p[23]; i.car_am = p[24];   i.car_vib = p[25];
    i.car_ksr = p[26]; i.fb_at = s(27);   i.mod_wave = p[28]; i.car_wave = p[29];
    i.mod_out_vel = s(30); i.car_out_vel = s(31); i.fb_vel = s(32);
    i.slide_coarse = p[33]; i.transpose = p[34]; i.slide_dur = p[35];
    i.slide_range = s(36);  i.mod_out_at = s(38); i.car_out_at = s(39);
    i.keymap_offset = p[2];
    std::copy(p + 4, p + 40, i.keymap);
    return i;
}

// Walks one track as version-1 events; false at the first malformed one.
bool valid_v1(const std::vector<uint8_t>& d) {
    size_t pos = 0;
    auto param = [&]() { return pos < d.size() && d[pos++] <= 0x7F; };
    while (pos < d.size()) {
        while (pos < d.size() && (d[pos++] & 0x80)) {}       // delta time
        if (pos >= d.size()) return false;
        uint8_t status = d[pos++];
        if (status < 0x80) return false;
        if (status < 0xC0)      { if (!param() || !param()) return false; }
        else if (status < 0xF0) { if (!param()) return false; }
        else if (status == 0xFF) break;
    }
    return true;
}

}  // namespace

bool Song::parse(const uint8_t* data, size_t size) {
    *this = Song{};
    if (!data || size < HEADER) return false;
    const size_t inst_off = u16(data);
    if (inst_off > size || inst_off < HEADER) return false;
    const size_t n_inst = (size - inst_off) / INST_SIZE;
    if (n_inst == 0) return false;
    const uint16_t first = u16(data + 2);
    if (first != 0x32 && first != 0x52) return false;
    agd = first == 0x52;
    loop_start = u16(data + 0x2C);
    loop_end   = u16(data + 0x2E);
    loop_count = u16(data + 0x30);
    speed      = u16(data + 0x32);
    if (speed == 0) return false;

    for (int i = 0; i < MAX_TRACKS; ++i) {
        if (u16(data + 2 + i * 2) == 0) break;
        size_t start = size_t(u16(data + 2 + i * 2)) + 2;
        size_t next = (i < MAX_TRACKS - 1) ? size_t(u16(data + 4 + i * 2)) + 2 : inst_off;
        if (next <= 2) next = inst_off;
        if (start > next || next > size) return false;
        tracks.emplace_back(data + start, data + next);
    }
    for (size_t i = 0; i < n_inst; ++i) {
        instruments.push_back(decode(data + inst_off + i * INST_SIZE));
        if (instruments.back().mode == -1) v2 = true;
    }
    // Version 2 without keymaps: its 1-byte note-offs don't parse as version 1.
    if (!v2) for (auto& t : tracks) if (!valid_v1(t)) { v2 = true; break; }
    return true;
}

uint32_t Song::length_ticks() const {
    uint32_t longest = 0;
    for (auto& d : tracks) {
        uint32_t sum = 0;
        size_t pos = 0;
        while (pos < d.size()) {
            uint32_t v = 0;
            do { v = (v << 7) | (d[pos] & 0x7F); } while ((d[pos++] & 0x80) && pos < d.size());
            sum += v;
            if (pos >= d.size()) break;
            switch (d[pos++] & 0xF0) {
            case 0x80: pos += v2 ? 1 : 2; break;
            case 0x90: case 0xA0: case 0xB0: pos += 2; break;
            case 0xC0: case 0xD0: case 0xE0: pos += 1; break;
            default: pos = d.size(); break;
            }
        }
        longest = std::max(longest, sum);
    }
    return longest;
}

// ---------------------------------------------------------------- Player ----

Player::Player(const Song& song, bool loop) : s_(song), loop_(loop) {}

void Player::rewind(std::vector<Write>& out) {
    out_ = &out;
    time_ = 0;
    ticks_ = 0xFFFFFFFFu;          // the driver always plays one spare tick first
    ended_ = false;
    loops_ = 0;
    have_saved_ = false;
    total_ = s_.length_ticks();
    trk_.assign(s_.tracks.size(), Track{});
    ch_.assign(s_.tracks.size(), Channel{});
    loop_start_ = s_.loop_start;
    loop_end_ = s_.loop_end;
    loop_count_ = s_.loop_count;
    if (s_.v2) {
        // Version 2 always repeats: a song with a repeat count loops whole,
        // one without loops its marked measures (intro, then the loop).
        const uint32_t measures = (total_ + Song::MEASURE_TICKS - 1) / Song::MEASURE_TICKS;
        if (!loop_start_ || loop_count_) loop_start_ = 1;
        if (!loop_end_ || loop_count_) loop_end_ = uint16_t(measures + 1);
        loop_count_ = 0;
    }
    w(0, 0x01, 0x20);              // waveform select on
    w(0, 0xBD, 0x00);              // melodic mode
    w(0, 0x08, 0x40);              // note select
    if (s_.agd) {
        w(VOICES, 0x05, 0x01);     // OPL3 mode
        w(VOICES, 0x04, 0x00);     // no 4-operator voices
    }
}

bool Player::timer(std::vector<Write>& out) {
    out_ = &out;
    time_ -= 256;
    if (time_ < 0) {
        time_ += s_.speed;
        tick();
    }
    return !ended_;
}

uint32_t Player::delta(int t) {
    const auto& d = s_.tracks[size_t(t)];
    auto& tr = trk_[size_t(t)];
    uint32_t v = 0;
    do { v = (v << 7) | (d[tr.pos] & 0x7F); } while ((d[tr.pos++] & 0x80) && tr.pos < d.size());
    return v;
}

void Player::tick() {
    const uint32_t M = Song::MEASURE_TICKS;
    const bool looping = loop_ && loop_start_ && loop_end_;
    if (looping && (ticks_ + 1) % M == 0 && (ticks_ + 1) / M + 1 == loop_start_) {
        saved_ = trk_;
        saved_ticks_ = ticks_;
        have_saved_ = true;
    }
    bool all_done = true;
    for (size_t t = 0; t < s_.tracks.size(); ++t) {
        const auto& d = s_.tracks[t];
        auto& tr = trk_[t];
        auto& c = ch_[t];
        if (c.slide > 0 && c.keyon) {               // pitch-slide macro
            --c.slide;
            c.bend = uint8_t(c.bend + s_.instruments[c.playprog].slide_range);
            if (c.note & 0x7F) play(int(t), c.note, UPDATE);
        }
        if (tr.pos >= d.size()) continue;
        all_done = false;
        if (tr.counter == 0) {
            const bool first = tr.pos == 0;
            tr.wait = uint16_t(delta(int(t)));      // the driver keeps 16 bits
            if (first && tr.wait) ++tr.wait;        // matches the spare first tick
        }
        if (++tr.counter >= tr.wait) {
            tr.counter = 0;
            while (tr.pos < d.size()) {
                command(int(t));
                if (tr.pos >= d.size()) break;
                if (d[tr.pos] == 0) ++tr.pos;       // next event at the same time
                else break;
            }
        } else if (tr.wait >= 0x8000) {             // runaway delay: end the track
            tr.pos = d.size();
            tr.counter = tr.wait;
        }
    }
    ended_ = all_done;
    if (!all_done) ++ticks_;
    if (looping && have_saved_ &&
        (ticks_ == total_ || (ticks_ % M == 0 && ticks_ / M + 1 == loop_end_))) {
        if (!loop_count_ || loops_ + 1 < loop_count_) {
            trk_ = saved_;
            ticks_ = saved_ticks_;
            ended_ = false;
            ++loops_;
        }
    }
}

void Player::command(int t) {
    const auto& d = s_.tracks[size_t(t)];
    auto& tr = trk_[size_t(t)];
    if (t >= (s_.agd ? 2 * VOICES : VOICES)) { tr.pos = d.size(); return; }
    auto get = [&]() -> uint8_t { return tr.pos < d.size() ? d[tr.pos++] : 0; };
    const uint8_t status = get();
    if (status == 0xFF) { tr.pos = d.size(); return; }
    switch (status & 0xF0) {
    case 0x80: { uint8_t n = get(); if (!s_.v2) get(); note_off(t, n); break; }
    case 0x90: { uint8_t n = get(); uint8_t v = get(); note_on(t, n, v); break; }
    case 0xA0: case 0xB0: get(); get(); break;
    case 0xC0: program(t, get()); break;
    case 0xD0: aftertouch(t, get()); break;
    case 0xE0: bend(t, get()); break;
    default: tr.pos = d.size(); break;
    }
}

void Player::note_on(int c, uint8_t note, uint8_t vel) {
    auto& ch = ch_[size_t(c)];
    const auto& I = s_.instruments;
    if (ch.keyon) { ch.keyon = false; play(c, ch.note, OFF); }
    if (s_.v2 && I[ch.program].mode == -1) {        // keymap: instrument per note
        const int8_t k = int8_t(note - (I[ch.program].keymap_offset + 24));
        if (k < 0 || k >= 36) return;
        ch.playprog = I[ch.program].keymap[k];
        if (ch.playprog >= I.size()) return;
        load(c, ch.playprog);
    }
    ch.note = note;
    ch.keyon = true;
    ch.bend = 0x40;
    const Instrument& in = I[ch.playprog];
    if (s_.v2 && in.mode == -1) return;
    play(c, note, ON);
    if (in.mod_out_vel) level(c, ch.playprog, in.mod_out_vel, vel, false);
    if (in.car_out_vel) level(c, ch.playprog, in.car_out_vel, vel, true);
    if (in.fb_vel) feedback(c, ch.playprog, in.fb_vel, vel);
}

void Player::note_off(int c, uint8_t note) {
    auto& ch = ch_[size_t(c)];
    if (note != ch.note || !ch.keyon) return;
    ch.keyon = false;
    play(c, note, OFF);
}

void Player::program(int c, uint8_t prog) {
    if (prog >= s_.instruments.size()) return;
    ch_[size_t(c)].program = ch_[size_t(c)].playprog = prog;
    load(c, prog);
}

void Player::aftertouch(int c, uint8_t vel) {
    if (s_.v2) return;                              // version 2 ignores it
    const uint8_t p = ch_[size_t(c)].playprog;
    const Instrument& in = s_.instruments[p];
    if (in.mod_out_at) level(c, p, in.mod_out_at, vel, false);
    if (in.car_out_at && in.car_out_vel) level(c, p, in.car_out_at, vel, true);
    if (in.fb_at) feedback(c, p, in.fb_at, vel);
}

void Player::bend(int c, uint8_t b) {
    auto& ch = ch_[size_t(c)];
    ch.bend = b;
    if (ch.keyon) play(c, ch.note, UPDATE);
}

void Player::play(int c, uint8_t note, State st) {
    auto& ch = ch_[size_t(c)];
    const Instrument& in = s_.instruments[ch.playprog];
    if (in.transpose) {
        const uint8_t diff = uint8_t(in.transpose - 0x31);
        note = (s_.v2 && diff < 0x60) ? uint8_t(diff + 0x18) : uint8_t(note + in.transpose);
    }
    note = uint8_t(note - 24);
    if (st != UPDATE && note >= 0x60) note = 0;     // out of range
    int oct = note / 12, key = note % 12;
    if (st != UPDATE && in.slide_dur) ch.slide = (st == ON) ? in.slide_dur : 0;

    const int b = ch.bend;
    int detune = 0;
    if (!(in.slide_coarse & 1)) {                   // fine bend: 1/32 steps
        const int amount = b < 0x40 ? 0x40 - b : b - 0x40;
        const int semis = amount >> 5, frac = (amount << 3) & 0xFF;
        if (b < 0x40) {
            key -= semis;
            if (key < 0) { key += 12; --oct; }
            if (oct < 0) { key = 0; oct = 0; }
            detune = -((FINE_BEND[key] * frac) >> 8);
        } else {
            key += semis;
            if (key >= 12) { key -= 12; ++oct; }
            detune = (FINE_BEND[key + 1] * frac) >> 8;
        }
    } else {                                        // coarse bend: 1/5 semitones
        if (b < 0x40) {
            const int amount = 0x40 - b;
            key -= amount / 5;
            while (key < 0) { key += 12; --oct; }
            if (oct < 0) { key = 0; oct = 0; }
            detune = -COARSE_BEND[(amount % 5) + (key >= 6 ? 5 : 0)];
        } else {
            const int amount = b - 0x40;
            key += amount / 5;
            while (key >= 12) { key -= 12; ++oct; }
            detune = COARSE_BEND[(amount % 5) + (key >= 6 ? 5 : 0)];
        }
    }
    freq(c, oct, uint16_t(FNUM[key] + detune), st != OFF);
}

void Player::freq(int c, int oct, uint16_t f, bool on) {
    const uint8_t v = uint8_t(c % VOICES);
    w(c, uint8_t(0xA0 + v), uint8_t(f & 0xFF));
    w(c, uint8_t(0xB0 + v), uint8_t(((f >> 8) & 3) | ((oct & 7) << 2) | (on ? 0x20 : 0)));
}

void Player::load(int c, uint8_t i) {
    const Instrument& in = s_.instruments[i];
    if (s_.v2 && in.mode == -1) return;
    const uint8_t slot = SLOT[c % VOICES];
    auto ctl = [](uint8_t mul, uint8_t ksr, uint8_t eg, uint8_t vib, uint8_t am) {
        return uint8_t((mul & 15) | ((ksr & 1) << 4) | ((eg ? 1 : 0) << 5) | ((vib & 1) << 6) | ((am & 1) << 7));
    };
    w(c, uint8_t(0x20 + slot), ctl(in.mod_mul, in.mod_ksr, in.mod_eg, in.mod_vib, in.mod_am));
    w(c, uint8_t(0x23 + slot), ctl(in.car_mul, in.car_ksr, in.car_eg, in.car_vib, in.car_am));
    w(c, uint8_t(0x40 + slot), uint8_t((in.mod_out & 63) | ((in.mod_ksl & 3) << 6)));
    w(c, uint8_t(0x43 + slot), uint8_t((in.car_out & 63) | ((in.car_ksl & 3) << 6)));
    w(c, uint8_t(0x60 + slot), uint8_t((in.mod_d & 15) | ((in.mod_a & 15) << 4)));
    w(c, uint8_t(0x63 + slot), uint8_t((in.car_d & 15) | ((in.car_a & 15) << 4)));
    w(c, uint8_t(0x80 + slot), uint8_t((in.mod_r & 15) | ((in.mod_s & 15) << 4)));
    w(c, uint8_t(0x83 + slot), uint8_t((in.car_r & 15) | ((in.car_s & 15) << 4)));
    feedback(c, i, 0, 0);
    w(c, uint8_t(0xE0 + slot), uint8_t(in.mod_wave & (s_.agd ? 7 : 3)));
    w(c, uint8_t(0xE3 + slot), uint8_t(in.car_wave & (s_.agd ? 7 : 3)));
}

// Velocity/aftertouch macro on an operator's output level. The driver's
// arithmetic is 16-bit unsigned; kept so out-of-range input behaves the same.
void Player::level(int c, uint8_t i, int sens, uint8_t v, bool carrier) {
    if (sens < -4 || sens > 4) return;
    const Instrument& in = s_.instruments[i];
    int x = sens < 0 ? (v >> (sens + 4)) : ((0x80 - v) >> (4 - sens));
    if (x > 63) x = 63;
    uint16_t out = uint16_t(uint16_t(x) + (carrier ? in.car_out : in.mod_out));
    if (out > 63) out = 63;
    const uint8_t ksl = carrier ? in.car_ksl : in.mod_ksl;
    w(c, uint8_t((carrier ? 0x43 : 0x40) + SLOT[c % VOICES]), uint8_t((out & 63) | ((ksl & 3) << 6)));
}

// Feedback register (also the connection and, for OPL3, the panning).
// sens 0 writes the instrument's own feedback.
void Player::feedback(int c, uint8_t i, int sens, uint8_t v) {
    const Instrument& in = s_.instruments[i];
    uint8_t fb = in.feedback;
    if (sens != 0) {
        if (sens < -6 || sens > 6) return;
        int x = sens < 0 ? (v >> (sens + 7)) : ((0x80 - v) >> (7 - sens));
        if (x > 7) x = 7;
        fb = uint8_t(uint8_t(x) + in.feedback);
        if (fb > 7) fb = 7;
    }
    const uint8_t pan = s_.agd ? uint8_t((in.pan == 0 || in.pan > 3) ? 3 : in.pan) : 0;
    w(c, uint8_t(0xC0 + c % VOICES), uint8_t((in.con ? 0 : 1) | ((fb & 7) << 1) | (pan << 4)));
}

void Player::w(int c, uint8_t reg, uint8_t val) {
    if (out_) out_->push_back(Write{uint8_t(c >= VOICES ? 1 : 0), reg, val});
}

}  // namespace herad
