#include "formats/gm_map.h"

#include <algorithm>
#include <cmath>

namespace herad {

namespace {

// OPL frequency multipliers (register value -> factor).
constexpr double MULT[16] = {0.5, 1, 2, 3, 4, 5, 6, 7, 8, 8, 10, 10, 12, 12, 15, 15};

struct Note { int track; uint8_t inst; int key; uint32_t len; bool drum; };

// Every note of the song: which instrument sounds it, at what key, for how
// long (ticks). Walks the tracks like the player (keymaps resolved).
std::vector<Note> notes_of(const Song& s) {
    std::vector<Note> out;
    const auto& I = s.instruments;
    for (size_t t = 0; t < s.tracks.size() && t < 9; ++t) {
        const auto& d = s.tracks[t];
        size_t pos = 0;
        uint32_t time = 0;
        uint8_t prog = 0;
        long open = -1;              // index in out of the sounding note
        uint32_t opened = 0;
        auto close = [&](uint32_t at) {
            if (open >= 0) out[size_t(open)].len = at - opened;
            open = -1;
        };
        while (pos < d.size()) {
            uint32_t v = 0;
            do { v = (v << 7) | (d[pos] & 0x7F); } while ((d[pos++] & 0x80) && pos < d.size());
            time += v;
            if (pos >= d.size()) break;
            const uint8_t st = d[pos++];
            auto get = [&]() -> uint8_t { return pos < d.size() ? d[pos++] : 0; };
            switch (st & 0xF0) {
            case 0x80: get(); if (!s.v2) get(); close(time); break;
            case 0x90: {
                const uint8_t n = get(); get();
                close(time);
                if (prog >= I.size()) break;
                uint8_t inst = prog;
                bool drum = false;
                if (s.v2 && I[prog].mode == -1) {
                    const int k = int(n) - (I[prog].keymap_offset + 24);
                    if (k < 0 || k >= 36 || I[prog].keymap[k] >= I.size()) break;
                    inst = I[prog].keymap[k];
                    drum = true;
                }
                out.push_back(Note{int(t), inst, midi_key(s, I[inst], n), 0, drum});
                open = long(out.size() - 1);
                opened = time;
                break;
            }
            case 0xA0: case 0xB0: get(); get(); break;
            case 0xC0: prog = get(); break;
            case 0xD0: case 0xE0: get(); break;
            default: pos = d.size(); break;
            }
            if (st == 0xFF) break;
        }
        close(time);
    }
    return out;
}

template <class T> T median(std::vector<T> v) {
    if (v.empty()) return T{};
    std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
    return v[v.size() / 2];
}

// Hi-hats and cymbals: a metallic carrier with a noisy modulator. Some songs
// play them on ordinary tracks, not through a keymap.
bool metallic_noise(const Instrument& in) {
    return in.car_mul >= 10 && (in.mod_mul >= 10 || in.feedback >= 5);
}

enum class Kind { Cymbal, Snare, Drop, Tonal };

Kind drum_kind(const Instrument& in) {
    if (in.car_mul >= 10) return Kind::Cymbal;
    if (in.mod_mul >= 10 && in.feedback >= 5) return Kind::Snare;
    if ((in.slide_coarse & 1) && in.slide_range < 0 && in.slide_dur > 0) return Kind::Drop;
    return Kind::Tonal;
}

int cymbal_for(const Instrument& in) {
    if (in.car_d <= 3 || (in.car_eg && in.car_r <= 4)) return 49;       // crash: long ring
    if (in.car_d >= 6 && in.car_r >= 6) return 42;                       // closed hi-hat
    return 46;                                                           // open hi-hat
}

int tom_for(int key) { return key < 56 ? 45 : key < 66 ? 48 : 50; }

// GM program for a melodic instrument heard around `key`, notes `len` long.
int program_for(const Instrument& in, int key, uint32_t len) {
    const bool sustains = in.car_eg != 0 && in.car_s <= 4;
    const bool slow = in.car_a <= 7;
    const bool bright = in.feedback >= 5 || in.mod_wave != 0;
    const bool metallic = in.car_mul >= 4 && in.car_mul != 8;
    if (key < 45) {                                                 // the bass line
        if (sustains) return bright ? 38 : 39;                      // synth bass 1 / 2
        return bright ? 38 : 33;                                    // synth / finger bass
    }
    if (sustains) {
        if (slow) return bright ? 90 : 89;                          // polysynth / warm pad
        if (len >= 24) return bright ? 62 : 48;                     // synth brass / strings
        return in.feedback >= 6 ? 81 : 80;                          // saw / square lead
    }
    if (metallic) return key >= 84 ? 8 : 11;                        // celesta / vibraphone
    if (in.car_d >= 6) return bright ? 84 : 4;                      // charang / e-piano 1
    return 5;                                                       // e-piano 2
}

}  // namespace

int midi_key(const Song& s, const Instrument& in, uint8_t note) {
    if (in.transpose) {
        const uint8_t diff = uint8_t(in.transpose - 0x31);
        note = (s.v2 && diff < 0x60) ? uint8_t(diff + 0x18) : uint8_t(note + in.transpose);
    }
    int idx = int(uint8_t(note - 24));
    if (idx >= 0x60) idx = 0;
    // Index 0 is C0 (16.35 Hz: F-number 343, block 0) at multiplier 1.
    const int shift = int(std::lround(12.0 * std::log2(MULT[in.car_mul & 15])));
    return std::clamp(12 + idx + shift, 0, 127);
}

std::vector<GmVoice> gm_map(const Song& s) {
    std::vector<GmVoice> v(s.instruments.size());
    std::vector<bool> drum(v.size(), false);
    const auto all = notes_of(s);
    for (size_t i = 0; i < v.size(); ++i) {
        std::vector<int> keys;
        std::vector<uint32_t> lens;
        for (const auto& n : all)
            if (n.inst == i) { keys.push_back(n.key); lens.push_back(n.len); if (n.drum) drum[i] = true; }
        if (keys.empty()) continue;
        auto& g = v[i];
        g.notes = int(keys.size());
        g.key = median(keys);
        const Instrument& in = s.instruments[i];
        if (!drum[i] && metallic_noise(in)) drum[i] = true;
        if (drum[i]) continue;
        g.program = program_for(in, g.key, median(lens));
        // Into a range the SoundFont plays well (FM low notes can sit below
        // hearing and still sound through their harmonics).
        const int lo = g.program >= 32 && g.program <= 39 ? 28 : 40;
        while (g.key + g.shift < lo) g.shift += 12;
        while (g.key + g.shift > 96) g.shift -= 12;
    }
    // Drums. Of the dropping ones the most used is the kick and the rest are
    // toms; of the snares the most used is the snare, the rest claps.
    long kick = -1, snare = -1, hat = -1, hat2 = -1;
    for (size_t i = 0; i < v.size(); ++i) {
        if (!drum[i] || !v[i].notes) continue;
        const Kind k = drum_kind(s.instruments[i]);
        if (k == Kind::Cymbal) {
            if (hat < 0 || v[i].notes > v[size_t(hat)].notes) { hat2 = hat; hat = long(i); }
            else if (hat2 < 0 || v[i].notes > v[size_t(hat2)].notes) hat2 = long(i);
        }
        if (k == Kind::Drop && (kick < 0 || v[i].notes > v[size_t(kick)].notes)) kick = long(i);
        if (k == Kind::Snare && (snare < 0 || v[i].notes > v[size_t(snare)].notes)) snare = long(i);
    }
    for (size_t i = 0; i < v.size(); ++i) {
        if (!drum[i] || !v[i].notes) continue;
        auto& g = v[i];
        const Instrument& in = s.instruments[i];
        g.program = 0;
        switch (drum_kind(in)) {
        // The busiest cymbal keeps time: the closed hi-hat; the next, the open
        // one. The rest by their ring (a crash only if it's played sparingly).
        case Kind::Cymbal:
            g.drum = long(i) == hat ? 42 : long(i) == hat2 ? 46 : cymbal_for(in);
            if (g.drum == 49 && g.notes > 64) g.drum = 46;
            break;
        case Kind::Snare:  g.drum = long(i) == snare ? 38 : 39; break;
        case Kind::Drop:   g.drum = long(i) == kick || g.key < 40 ? 36 : tom_for(g.key); break;
        case Kind::Tonal:  g.drum = g.key < 40 ? 36 : tom_for(g.key); break;
        }
    }
    return v;
}

std::vector<int> track_pans(const Song& s) {
    const size_t n = std::min<size_t>(s.tracks.size(), 9);
    std::vector<int> pan(n, 0);
    const auto all = notes_of(s);
    struct T { size_t track; int key; bool centre; };
    std::vector<T> order;
    for (size_t t = 0; t < n; ++t) {
        std::vector<int> keys;
        int drums = 0;
        for (const auto& x : all)
            if (size_t(x.track) == t) { keys.push_back(x.key); drums += x.drum; }
        if (keys.empty()) continue;
        const int k = median(keys);
        order.push_back(T{t, k, drums * 2 > int(keys.size()) || k < 45});
    }
    std::sort(order.begin(), order.end(), [](const T& a, const T& b) { return a.key < b.key; });
    int side = -1;
    for (const auto& t : order) {
        if (t.centre) continue;
        pan[t.track] = side;
        side = -side;
    }
    return pan;
}

}  // namespace herad
