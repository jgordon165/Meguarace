#include "fm.h"

#include "formats/gm_map.h"

#include <algorithm>
#include <cmath>

#if defined(_MSC_VER)
#pragma warning(push, 0)              // third-party headers, not built with our /W4
#endif
#include "ymfm_opl.h"
#include "tsf.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace {
constexpr uint32_t OPL2_CLOCK = 3579545;    // Sound Blaster / AdLib crystal
// The OPL3 runs at 4x the clock (14.318 MHz) for the same 49716 Hz samples.
constexpr int VOICES = 9;
constexpr int DRUMS = 9;                    // GM channel 10

// Enhanced: where each OPL3 output goes. A (bit 4) the centre, C (bit 6)
// mostly left, D (bit 7) mostly right.
constexpr uint8_t BUS_CENTRE = 0x10, BUS_LEFT = 0x40, BUS_RIGHT = 0x80;
constexpr float CENTRE = 0.74f, NEAR = 0.92f, FAR = 0.38f;
// Levels: Enhanced and SoundFont about as loud as Original.
constexpr float ENHANCED_GAIN = 0.39f;
constexpr float SF_GAIN = 0.20f * 32767.0f;
constexpr float SYNTH_BUS = 0.8f, DRUM_BUS = 2.0f;   // the kick on top, as in a dance mix
constexpr float EDM_DRIVE = 0.22f;             // into the limiter: it catches only the peaks
constexpr float EDM_GAIN = 0.70f * 32767.0f;
constexpr int DANCE_KIT = 26;               // GM drum kit numbers as GS banks have them
constexpr int LAYER_CH = 16;                // Edm: channels 16.. for the synth layers (3 per track)
}

struct FmMusic::Chip2 : ymfm::ymfm_interface {
    ymfm::ym3812 opl{*this};
};
struct FmMusic::Chip3 : ymfm::ymfm_interface {
    ymfm::ymf262 opl{*this};
};

// ------------------------------------------------------------ SoundFontBank --

std::shared_ptr<SoundFontBank> SoundFontBank::load(const std::string& path) {
    tsf* f = tsf_load_filename(path.c_str());
    if (!f) return nullptr;
    auto b = std::shared_ptr<SoundFontBank>(new SoundFontBank());
    b->tsf_ = f;
    b->path_ = path;
    return b;
}

SoundFontBank::~SoundFontBank() {
    if (tsf_) tsf_close(static_cast<tsf*>(tsf_));
}

// ------------------------------------------------------------------ FmMusic --

FmMusic::FmMusic(MusicStyle style, std::shared_ptr<SoundFontBank> bank) : style_(style), bank_(std::move(bank)) {
    if (style_ == MusicStyle::SoundFont || style_ == MusicStyle::Edm) {
        if (bank_ && bank_->synth()) {
            sf_ = tsf_copy(static_cast<tsf*>(bank_->synth()));
            if (style_ == MusicStyle::Edm) drums_ = tsf_copy(static_cast<tsf*>(bank_->synth()));
        }
        if (!sf_ || (style_ == MusicStyle::Edm && !drums_)) {
            if (sf_) tsf_close(static_cast<tsf*>(sf_));
            sf_ = nullptr;
            style_ = MusicStyle::Enhanced;
        }
        edm_ = style_ == MusicStyle::Edm;
    }
    if (style_ == MusicStyle::Original) chip2_ = std::make_unique<Chip2>();
    if (style_ == MusicStyle::Enhanced) chip3_ = std::make_unique<Chip3>();
    if (sf_) tsf_set_max_voices(static_cast<tsf*>(sf_), edm_ ? 160 : 96);   // no allocations on the audio thread
    if (drums_) tsf_set_max_voices(static_cast<tsf*>(drums_), 48);
}

FmMusic::~FmMusic() {
    if (sf_) tsf_close(static_cast<tsf*>(sf_));
    if (drums_) tsf_close(static_cast<tsf*>(drums_));
}

double FmMusic::chip_rate() { return double(OPL2_CLOCK) / 72.0; }

void FmMusic::apply(std::vector<herad::Write>& w) {
    if (chip2_) {
        for (auto& x : w) chip2_->opl.write(0, x.reg), chip2_->opl.write(1, x.val);
    } else if (chip3_) {
        for (auto& x : w) {
            uint8_t v = x.val;
            if (x.reg >= 0xC0 && x.reg < 0xC0 + VOICES) v = uint8_t((v & 0x0F) | bus_[x.reg - 0xC0]);
            chip3_->opl.write(0, x.reg);
            chip3_->opl.write(1, v);
        }
    }
    w.clear();
}

bool FmMusic::load(const uint8_t* data, size_t size, bool loop, uint32_t start, const std::vector<int>* gm) {
    playing_ = false;
    player_.reset();
    if (!song_.parse(data, size) || song_.agd) return false;
    player_ = std::make_unique<herad::Player>(song_, loop);
    writes_.clear();
    events_.clear();
    if (chip2_) chip2_->opl.reset();
    if (chip3_) {
        chip3_->opl.reset();
        chip3_->opl.write(2, 0x05);                  // OPL3 mode, for the output bits
        chip3_->opl.write(3, 0x01);
        const auto pans = herad::track_pans(song_);
        bus_.assign(VOICES, BUS_CENTRE);
        for (size_t t = 0; t < pans.size() && t < size_t(VOICES); ++t)
            bus_[t] = pans[t] < 0 ? BUS_LEFT : pans[t] > 0 ? BUS_RIGHT : BUS_CENTRE;
    }
    if (sf_) {
        const auto map = herad::gm_map(song_);
        gm_.assign(map.size(), Voice{});
        for (size_t i = 0; i < map.size(); ++i) {
            gm_[i] = Voice{map[i].program, map[i].drum, map[i].shift};
            const int o = gm && i < gm->size() ? (*gm)[i] : -1;
            if (o >= 128) gm_[i] = Voice{0, std::clamp(o - 128, 27, 87), 0};
            else if (o >= 0) gm_[i] = Voice{o, -1, map[i].drum >= 0 ? 0 : map[i].shift};
        }
        tsf_reset(static_cast<tsf*>(sf_));
        if (drums_) tsf_reset(static_cast<tsf*>(drums_));
        for (auto& h : held_) h = Held{};
        for (auto& p : chProgram_) p = -1;
        chPreset_.assign(LAYER_CH + 3 * VOICES, -1);
        kicks_.reserve(256);
        lastPos_ = lastDrumPos_ = 0xFFFFFFFFu;
        kickNow_ = false;
        duck_ = 1e9;
        beat_ = 24.0 * song_.speed / (herad::Song::TIMER_HZ * 256.0);
        sfRate_ = 0;
        player_->set_events(&events_);
    }
    player_->rewind(writes_);
    apply(writes_);
    // Skip ahead without sound: registers and controllers keep up, notes don't.
    // (position() starts at -1: the driver's spare first tick.)
    for (long guard = 0; start && uint32_t(player_->position() + 1u) < start + 1u && guard < 4000000; ++guard) {
        if (!player_->timer(writes_)) break;
        apply(writes_);
        if (sf_) sf_events(false);
    }
    timer_acc_ = 0;
    pos_ = 0;
    prev_ = cur_ = 0;
    prevL_ = prevR_ = curL_ = curR_ = 0;
    playing_ = true;
    return true;
}

bool FmMusic::tick_timer() {
    if (!playing_) return false;
    if (!player_->timer(writes_)) playing_ = false;
    apply(writes_);
    if (sf_) sf_events(true);
    return playing_;
}

int16_t FmMusic::next_sample() {
    // Timer interrupts land between chip samples at their exact rate.
    timer_acc_ -= 1.0;
    while (timer_acc_ < 0) {
        timer_acc_ += chip_rate() / herad::Song::TIMER_HZ;
        tick_timer();
    }
    ymfm::ym3812::output_data o;
    chip2_->opl.generate(&o, 1);
    int32_t v = o.data[0];
    return int16_t(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

void FmMusic::next_frame3(float& l, float& r) {
    timer_acc_ -= 1.0;
    while (timer_acc_ < 0) {
        timer_acc_ += chip_rate() / herad::Song::TIMER_HZ;
        tick_timer();
    }
    ymfm::ymf262::output_data o;
    chip3_->opl.generate(&o, 1);
    const float a = float(o.data[0]), c = float(o.data[2]), d = float(o.data[3]);
    l = a * CENTRE + c * NEAR + d * FAR;
    r = a * CENTRE + c * FAR + d * NEAR;
}

void FmMusic::render(int32_t* out, int frames, int rate, float gain) {
    if (!player_ || rate <= 0) return;
    if (sf_) return render_sf(out, frames, rate, gain);
    if (chip3_) return render_chip3(out, frames, rate, gain);
    const double step = chip_rate() / double(rate);
    for (int i = 0; i < frames; ++i) {
        while (pos_ >= 1.0) { prev_ = cur_; cur_ = next_sample(); pos_ -= 1.0; }
        const double s = (prev_ + (cur_ - prev_) * pos_) * gain;
        const int32_t v = int32_t(s);
        out[2 * i] += v;
        out[2 * i + 1] += v;
        pos_ += step;
    }
}

static inline int32_t clamp_out(float v) {
    return int32_t(std::clamp(v, -2.0e9f, 2.0e9f));
}

void FmMusic::render_chip3(int32_t* out, int frames, int rate, float gain) {
    if (fx_.rate() != rate) fx_.init(rate, 3.0f, -2.5f, 0.55f, 0.45f, 0.16f);
    buf_.resize(size_t(frames) * 2);
    const double step = chip_rate() / double(rate);
    for (int i = 0; i < frames; ++i) {
        while (pos_ >= 1.0) {
            prevL_ = curL_; prevR_ = curR_;
            next_frame3(curL_, curR_);
            pos_ -= 1.0;
        }
        const float t = float(pos_);
        buf_[2 * i] = prevL_ + (curL_ - prevL_) * t;
        buf_[2 * i + 1] = prevR_ + (curR_ - prevR_) * t;
        pos_ += step;
    }
    fx_.run(buf_.data(), frames);
    const float g = gain * ENHANCED_GAIN;
    for (int i = 0; i < 2 * frames; ++i) out[i] += clamp_out(buf_[size_t(i)] * g);
}

// ---------------------------------------------------------------- SoundFont --

void FmMusic::sf_setup(int rate) {
    tsf* f = static_cast<tsf*>(sf_);
    tsf_set_output(f, TSF_STEREO_INTERLEAVED, rate, 0.0f);
    if (!edm_) {
        tsf_channel_set_presetnumber(f, DRUMS, 0, 1);
        for (int c = 0; c < VOICES; ++c) tsf_channel_set_pitchrange(f, c, 2.0f);
        for (int c = 0; c < VOICES; ++c) if (chProgram_[c] >= 0) tsf_channel_set_presetnumber(f, c, chProgram_[c], 0);
        fx_.init(rate, 0.0f, 0.0f, 0.6f, 0.5f, 0.14f);
    } else {
        tsf* d = static_cast<tsf*>(drums_);
        tsf_set_output(d, TSF_STEREO_INTERLEAVED, rate, 0.0f);
        if (!tsf_channel_set_bank_preset(d, DRUMS, 128, DANCE_KIT)) tsf_channel_set_presetnumber(d, DRUMS, 0, 1);
        chPreset_.assign(chPreset_.size(), -1);          // set again at the new rate
        // Synths: a bigger, brighter room; drums: punchy, nearly dry.
        fx_.init(rate, 0.0f, 2.5f, 0.85f, 0.4f, 0.20f);
        drumFx_.init(rate, 4.0f, 2.0f, 0.4f, 0.5f, 0.05f);
        delay_.init(int(0.75 * beat_ * rate), 0.38f, 0.24f);   // dotted eighth
    }
    sfRate_ = rate;
}

// The carrier's level for a note: its output level plus the velocity
// macro, as the driver sets it (Player::level), in 0.75 dB steps.
static float note_gain(const herad::Instrument& in, uint8_t vel) {
    int att = in.car_out & 63;
    const int sens = in.car_out_vel;
    if (sens && sens >= -4 && sens <= 4) {
        int x = sens < 0 ? (vel >> (sens + 4)) : ((0x80 - vel) >> (4 - sens));
        att = std::min(63, std::min(x, 63) + att);
    }
    if (!in.con) att = std::min(att, int(in.mod_out & 63));   // additive: both operators sound
    return std::clamp(float(std::pow(10.0, -0.75 * att / 20.0)), 0.03f, 1.0f);
}

namespace {
// Edm: what plays a GM program. Up to three layers, each its own channel.
struct Layer { int bank, program, shift; float tune, pan, gain; };

int edm_layers(int program, Layer* out) {
    if (program >= 32 && program <= 39) {                   // bass: acid over a sine sub
        out[0] = {8, 38, 0, 0.0f, 0.5f, 0.6f};
        out[1] = {8, 80, -12, 0.0f, 0.5f, 0.45f};
        return 2;
    }
    if ((program >= 80 && program <= 87) || program == 62 || program == 63) {   // leads: supersaw
        out[0] = {0, 81, 0, -0.14f, 0.15f, 0.55f};
        out[1] = {0, 81, 0, 0.14f, 0.85f, 0.55f};
        out[2] = {0, 81, -12, 0.0f, 0.5f, 0.3f};
        return 3;
    }
    if ((program >= 88 && program <= 95) || (program >= 48 && program <= 51)) {   // pads, wide
        out[0] = {0, 95, 0, -0.08f, 0.2f, 0.6f};
        out[1] = {0, 90, 0, 0.08f, 0.8f, 0.5f};
        return 2;
    }
    if (program >= 8 && program <= 15) {                    // bells: echo drops
        out[0] = {0, 102, 0, 0.0f, 0.5f, 0.8f};
        return 1;
    }
    if (program <= 7 || (program >= 24 && program <= 31) || program == 84) {   // keys, plucks: polysynth stabs
        out[0] = {0, 90, 0, -0.06f, 0.35f, 0.6f};
        out[1] = {0, 81, 0, 0.06f, 0.65f, 0.4f};
        return 2;
    }
    out[0] = {0, program, 0, 0.0f, 0.5f, 0.8f};
    return 1;
}
}  // namespace

void FmMusic::edm_note_on(int t, int program, int key, float gain) {
    tsf* f = static_cast<tsf*>(sf_);
    Layer L[3];
    const int n = edm_layers(program, L);
    Held& h = held_[t];
    h = Held{};
    for (int i = 0; i < n; ++i) {
        const int ch = LAYER_CH + 3 * t + i;
        const int preset = L[i].bank * 128 + L[i].program;
        if (chPreset_[size_t(ch)] != preset) {
            chPreset_[size_t(ch)] = preset;
            if (!tsf_channel_set_bank_preset(f, ch, L[i].bank, L[i].program))
                tsf_channel_set_presetnumber(f, ch, L[i].program, 0);   // no variation bank: the GM one
            tsf_channel_set_tuning(f, ch, L[i].tune);
            tsf_channel_set_pan(f, ch, L[i].pan);
        }
        tsf_channel_set_pitchwheel(f, ch, 8192);
        const int k = std::clamp(key + L[i].shift, 0, 127);
        tsf_channel_note_on(f, ch, k, gain * L[i].gain);
        h.ch[i] = ch;
        h.key[i] = k;
    }
    h.n = n;
}

void FmMusic::sf_events(bool notes) {
    tsf* f = static_cast<tsf*>(sf_);
    tsf* drum = edm_ ? static_cast<tsf*>(drums_) : f;
    auto release = [&](Held& h) {
        if (notes && !h.drum)                                  // drums ring out
            for (int i = 0; i < h.n; ++i) tsf_channel_note_off(f, h.ch[i], h.key[i]);
        h = Held{};
    };
    for (const auto& e : events_) {
        if (e.track >= VOICES || e.inst >= gm_.size()) continue;
        const int t = e.track;
        Held& h = held_[t];
        const Voice& v = gm_[e.inst];
        const herad::Instrument& in = song_.instruments[e.inst];
        switch (e.type) {
        case herad::Event::NoteOn: {
            release(h);
            if (v.program < 0) break;
            if (!edm_ && v.drum < 0 && chProgram_[t] != v.program) {
                chProgram_[t] = v.program;
                if (sfRate_) tsf_channel_set_presetnumber(f, t, v.program, 0);
            }
            if (!notes || !sfRate_) break;
            const float g = note_gain(in, e.vel);
            if (v.drum >= 0) {
                tsf_channel_note_on(drum, DRUMS, v.drum, g);
                if (edm_) {
                    lastDrumPos_ = player_->position();
                    if (v.drum == 38 || v.drum == 40) tsf_channel_note_on(drum, DRUMS, 39, g * 0.7f);   // + clap
                    if (v.drum == 35 || v.drum == 36) kickNow_ = true;
                }
                h.drum = true;
                h.n = 1;
                h.ch[0] = DRUMS;
                h.key[0] = v.drum;
                break;
            }
            const int key = std::clamp(herad::midi_key(song_, in, e.note) + v.shift, 0, 127);
            const float range = (in.slide_coarse & 1) ? 12.8f : 2.0f;
            if (edm_) {
                edm_note_on(t, v.program, key, g);
                for (int i = 0; i < held_[t].n; ++i) tsf_channel_set_pitchrange(f, held_[t].ch[i], range);
            } else {
                tsf_channel_set_pitchrange(f, t, range);
                tsf_channel_set_pitchwheel(f, t, 8192);
                tsf_channel_note_on(f, t, key, g);
                h.n = 1;
                h.ch[0] = t;
                h.key[0] = key;
            }
            break;
        }
        case herad::Event::NoteOff:
            release(h);
            break;
        case herad::Event::Bend:
            if (notes && !h.drum)
                for (int i = 0; i < h.n; ++i)
                    tsf_channel_set_pitchwheel(f, h.ch[i], std::clamp(8192 + (int(e.bend) - 0x40) * 128, 0, 16383));
            break;
        }
    }
    events_.clear();
    // Edm: four on the floor. A kick on every beat while the song's drums are
    // playing (within the last two measures), unless the song kicked there.
    if (edm_ && notes && sfRate_) {
        const uint32_t pos = player_->position();
        if (pos != lastPos_) {
            lastPos_ = pos;
            const bool drumming = lastDrumPos_ != 0xFFFFFFFFu && pos >= lastDrumPos_ && pos - lastDrumPos_ < 192;
            if (drumming && pos % 24 == 0 && !kickNow_) {
                tsf_channel_note_on(drum, DRUMS, 36, 1.0f);
                kickNow_ = true;
            }
        }
    }
}

// Edm: synths (in buf_) pump to the kick (kicks: the sample each lands on),
// then delay and room; drums (in buf2_) their own EQ and room; then the two
// together through a soft limiter.
void FmMusic::edm_mix(int frames, const std::vector<int>& kicks) {
    const double release = 0.6 * beat_ * sfRate_;
    const float depth = 0.7f;
    size_t k = 0;
    for (int i = 0; i < frames; ++i) {
        while (k < kicks.size() && kicks[k] <= i) { duck_ = 0; ++k; }
        const double x = duck_ / release;
        const float g = x < 1.0 ? 1.0f - depth * float((1.0 - x) * (1.0 - x)) : 1.0f;
        duck_ += 1.0;
        float& l = buf_[size_t(2 * i)];
        float& r = buf_[size_t(2 * i + 1)];
        l *= g;
        r *= g;
        delay_.run(l, r);
    }
    fx_.run(buf_.data(), frames);
    drumFx_.run(buf2_.data(), frames);
    for (size_t i = 0; i < size_t(2 * frames); ++i)
        buf_[i] = musicfx::soft_clip((buf_[i] * SYNTH_BUS + buf2_[i] * DRUM_BUS) * EDM_DRIVE, 0.7f, 1.0f);
}

void FmMusic::render_sf(int32_t* out, int frames, int rate, float gain) {
    if (sfRate_ != rate) sf_setup(rate);
    tsf* f = static_cast<tsf*>(sf_);
    buf_.resize(size_t(frames) * 2);
    if (edm_) buf2_.resize(size_t(frames) * 2);
    kicks_.clear();
    const double per_tick = double(rate) / herad::Song::TIMER_HZ;
    int done = 0;
    while (done < frames) {
        if (timer_acc_ <= 0) {
            timer_acc_ += per_tick;
            tick_timer();
            if (kickNow_) { kicks_.push_back(done); kickNow_ = false; }
            continue;
        }
        const int n = std::min(frames - done, std::max(1, int(std::ceil(timer_acc_))));
        tsf_render_float(f, buf_.data() + 2 * done, n, 0);
        if (edm_) tsf_render_float(static_cast<tsf*>(drums_), buf2_.data() + 2 * done, n, 0);
        timer_acc_ -= n;
        done += n;
    }
    float g = gain;
    if (edm_) {
        edm_mix(frames, kicks_);
        g *= EDM_GAIN;
    } else {
        fx_.run(buf_.data(), frames);
        g *= SF_GAIN;
    }
    for (int i = 0; i < 2 * frames; ++i) out[i] += clamp_out(buf_[size_t(i)] * g);
}
