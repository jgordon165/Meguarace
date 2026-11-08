// Format decoder tests. Uses hand-built inputs only: no MegaRace data is
// needed (or allowed) in the repository, so these run anywhere, including CI.
//
// Tests that need the real game run separately when MEGARACE_DATA points at
// a MegaRace CD (see tests/lua/main.lua).

#include "fm.h"
#include "formats/cryo.h"
#include "formats/herad.h"
#include "formats/gm_map.h"
#include "sides.h"
#include "resampler.h"
#include "voice_filter.h"

#include <cmath>
#include <complex>
#include <random>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_checks;                                                                 \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                           \
    } while (0)

using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------------------
// Minimal HSQ encoder, the exact mirror of unHsq's reader: bits go into
// 16-bit little-endian words, LSB first, and each word's slot is reserved in
// the output at the moment the decoder will fetch it.
class HsqWriter {
public:
    void literal(uint8_t b) { bit(1); out_.push_back(b); }
    void shortMatch(int count, int offset) {          // count 2..5, offset -1..-256
        bit(0); bit(0);
        bit((count - 2) >> 1); bit((count - 2) & 1);
        out_.push_back(uint8_t(offset + 256));
    }
    void longMatch(int count, int offset) {           // count 3..9 here, offset -1..-8192
        bit(0); bit(1);
        unsigned w = unsigned(offset + 8192) << 3 | unsigned(count - 2);
        out_.push_back(uint8_t(w)); out_.push_back(uint8_t(w >> 8));
    }
    Bytes finish(uint16_t unpacked) {
        bit(0); bit(1);
        out_.push_back(0); out_.push_back(0); out_.push_back(0);   // count 0, then 0 = end
        Bytes r = {uint8_t(unpacked), uint8_t(unpacked >> 8), 0, 0, 0, 0};
        size_t packed = out_.size() + 6;
        r[3] = uint8_t(packed); r[4] = uint8_t(packed >> 8);
        unsigned s = 0;
        for (int i = 0; i < 5; ++i) s += r[size_t(i)];
        r[5] = uint8_t(0xAB - s);
        r.insert(r.end(), out_.begin(), out_.end());
        return r;
    }

private:
    void bit(int b) {
        if (used_ == 16) {
            slot_ = out_.size();
            out_.push_back(0); out_.push_back(0);
            used_ = 0;
        }
        if (b) {
            uint16_t w = uint16_t(out_[slot_] | out_[slot_ + 1] << 8);
            w = uint16_t(w | 1u << used_);
            out_[slot_] = uint8_t(w); out_[slot_ + 1] = uint8_t(w >> 8);
        }
        ++used_;
    }
    Bytes out_;
    size_t slot_ = 0;
    int used_ = 16;
};

static void testHsq() {
    // "ABCABCABCX" + a long back-reference.
    HsqWriter w;
    w.literal('A'); w.literal('B'); w.literal('C');
    w.shortMatch(5, -3);          // ABCAB
    w.literal('X');
    w.longMatch(9, -9);           // BCABCABCX
    Bytes packed = w.finish(18);
    CHECK(isHsq(packed.data(), packed.size()));
    std::vector<uint8_t> out;
    try { out = unHsq(packed.data(), packed.size()); }
    catch (const std::exception& e) { std::fprintf(stderr, "unHsq threw: %s\n", e.what()); }
    std::string s(out.begin(), out.end());
    // Exact expected output: literals ABC, copy 5 from -3 => ABCAB, X, copy 9 from -9.
    std::string expect = "ABCABCAB";
    expect += 'X';
    std::string tail = expect.substr(expect.size() - 9, 9);   // overlapping copy of the last 9
    expect += tail;
    CHECK(s == expect);

    // Not HSQ: wrong checksum.
    Bytes bad = packed;
    bad[5] ^= 1;
    CHECK(!isHsq(bad.data(), bad.size()));
    bool threw = false;
    try { unHsq(bad.data(), bad.size()); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    // Truncated stream must throw, not read out of bounds.
    Bytes cut(packed.begin(), packed.begin() + 8);
    threw = false;
    try { unHsq(cut.data(), cut.size()); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

// ---------------------------------------------------------------------------
// HNM container built in memory.

static void put16(Bytes& b, unsigned v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }

static Bytes chunk(const char* tag, const Bytes& body) {
    Bytes c = {uint8_t(tag[0]), uint8_t(tag[1])};
    put16(c, unsigned(body.size() + 4));
    c.insert(c.end(), body.begin(), body.end());
    return c;
}

static Bytes videoChunk(int w, int h, uint8_t mode, const Bytes& data) {
    uint32_t hdr = uint32_t(w & 0x1FF) | uint32_t(h) << 16 | uint32_t(mode) << 24;
    Bytes v(4 + data.size());
    for (size_t i = 0; i < 4; ++i) v[i] = uint8_t(hdr >> (8 * i));
    std::copy(data.begin(), data.end(), v.begin() + 4);
    return v;
}

static Bytes hnmFile(const std::vector<Bytes>& frames) {
    Bytes pal = {1, 2, 63, 0, 0, 0, 63, 0, 0xFF, 0xFF};   // index 1 red, 2 green (6-bit)
    Bytes f;
    put16(f, unsigned(2 + pal.size()));
    f.insert(f.end(), pal.begin(), pal.end());
    for (const auto& fr : frames) {
        put16(f, unsigned(fr.size() + 2));
        f.insert(f.end(), fr.begin(), fr.end());
    }
    return f;
}

static void testHnm173() {
    // 0xAD frame, long runs, no x/y: codebook [5,7,9]. Raster bits (MSB first
    // from a little-endian word): 0 literal 5 | 1 end | 10 run of 2 (7) |
    // 1 end | 110 run of 3 (9)  =>  01101110 = 0x6E00.
    Bytes a = {6, 0, 3, 0, 0x84, 0x20, 5, 7, 9, 0x00, 0x6E};
    // 0xAD frame with a codebook back-reference: 1 2 [copy 2 from 2 back] 3.
    Bytes b = {5, 0, 5, 0, 0x84, 0x1F, 1, 2, 0x80, 0x10, 3, 0x00, 0x00};
    // Overlay frame placed at x=3, y=4 (flags bit 2 clear), codebook [0, 8].
    Bytes c = {2, 0, 2, 0, 0x80, 0x00, 3, 0, 4, 0, 0, 8, 0x00, 0x00};
    unsigned s = 0;
    for (int i = 0; i < 5; ++i) s += c[size_t(i)];
    c[5] = uint8_t(0xAD - s);

    Bytes pt(58, 0x11);
    Bytes frame0 = chunk("pt", pt);
    Bytes v0 = videoChunk(3, 2, 0xFE, a);
    frame0.insert(frame0.end(), v0.begin(), v0.end());
    Bytes frame1 = videoChunk(5, 1, 0xFE, b);
    Bytes frame2 = videoChunk(2, 1, 0xFF, c);
    // A damaged-tag kl chunk (as on the real disc, NGL1 frame 2185), then video.
    Bytes kl = {0xF8, 0x4B};
    put16(kl, 8);
    kl.insert(kl.end(), {1, 2, 3, 4});
    Bytes frame3 = kl;
    Bytes v3 = videoChunk(5, 1, 0xFE, b);
    frame3.insert(frame3.end(), v3.begin(), v3.end());

    HnmVideo v;
    CHECK(v.load(hnmFile({frame0, frame1, frame2, frame3})));
    CHECK(v.frameCount() == 4);
    CHECK(v.palette()[3] == 255);   // index 1 red: 6-bit 63 expands to 255
    CHECK(v.palette()[3 + 1] == 0);

    HnmVideo::Frame f;
    CHECK(v.decode(0, f));
    CHECK(f.hasVideo && f.w == 3 && f.h == 2 && !f.overlay);
    CHECK((f.pixels == Bytes{5, 7, 7, 9, 9, 9}));
    CHECK(f.chunks.count("pt") == 1 && f.chunks["pt"].size() == 58);

    CHECK(v.decode(1, f));
    CHECK((f.pixels == Bytes{1, 2, 1, 2, 3}));

    CHECK(v.decode(2, f));
    CHECK(f.overlay && f.x == 3 && f.y == 4);
    CHECK((f.pixels == Bytes{0, 8}));

    CHECK(v.decode(3, f));
    CHECK((f.pixels == Bytes{1, 2, 1, 2, 3}));
    CHECK(f.chunks.count("kl") == 1 && (f.chunks["kl"] == Bytes{1, 2, 3, 4}));

    std::map<std::string, std::vector<uint8_t>> only;
    CHECK(v.dataChunks(0, only) && only.count("pt") == 1);
    CHECK(v.dataChunks(3, only) && only.count("kl") == 1);

    CHECK(!v.decode(4, f));      // out of range
}

static void testHnmCorrupt() {
    // Truncated frame data must fail cleanly (exception or false), never crash.
    Bytes a = {6, 0, 3, 0, 0x84, 0x20, 5, 7};
    HnmVideo v;
    CHECK(v.load(hnmFile({videoChunk(3, 2, 0xFE, a)})));
    HnmVideo::Frame f;
    bool ok = true;
    try { ok = v.decode(0, f); } catch (const std::exception&) { ok = false; }
    CHECK(!ok);
}

// Sprite set: palette block, a raw image, a PackBits image and a data entry.
static void testSpriteSet() {
    Bytes pal = {128, 1, 63, 0, 0, 0xFF, 0xFF};              // colour 128 = red
    Bytes raw = {2, 0, 2, 0xFF, 1, 2, 3, 4};                  // 2x2 raw
    Bytes packed = {3, 0x80, 2, 0xFF, 0xFE, 7, 0x02, 8, 9, 10}; // 3x2: 7,7,7 + 8,9,10
    Bytes data = {0, 0, 0xBF, 0, 1, 2, 3};                    // width 0: not an image
    Bytes f;
    size_t tbl = 2 + pal.size();
    put16(f, unsigned(tbl));
    f.insert(f.end(), pal.begin(), pal.end());
    size_t first = 6;                                          // 3 entries x 2 bytes
    put16(f, unsigned(first));
    put16(f, unsigned(first + raw.size()));
    put16(f, unsigned(first + raw.size() + packed.size()));
    f.insert(f.end(), raw.begin(), raw.end());
    f.insert(f.end(), packed.begin(), packed.end());
    f.insert(f.end(), data.begin(), data.end());

    SpriteSet s;
    CHECK(s.load(f));
    CHECK(s.count() == 3);
    const auto* a = s.image(0);
    CHECK(a && a->w == 2 && a->h == 2 && (a->pixels == Bytes{1, 2, 3, 4}));
    const auto* b = s.image(1);
    CHECK(b && b->w == 3 && b->h == 2 && (b->pixels == Bytes{7, 7, 7, 8, 9, 10}));
    CHECK(s.image(2) == nullptr);
    CHECK(s.image(9) == nullptr);
    CHECK(s.paletteBlock().size() == pal.size() && s.paletteBlock()[0] == 128);

    // Truncated PackBits data is padded, never read past the end.
    Bytes cut = f;
    cut.resize(cut.size() - data.size() - 3);
    SpriteSet t;
    bool ok = true;
    try { ok = t.load(cut); } catch (const std::exception&) { ok = false; }
    CHECK(!ok || (t.image(1) == nullptr || t.image(1)->pixels.size() == 6));
}

// Video soundtracks arrive one small chunk per frame. Resampling them chunk by
// chunk must give the same output as one big conversion, with the expected
// length; otherwise speech drifts away from the picture.
static void testResamplerChunks() {
    const int N = 886, CHUNKS = 500;
    std::vector<uint8_t> all;
    for (int i = 0; i < N * CHUNKS; ++i) all.push_back(uint8_t(128 + 100 * ((i / 7) % 2 ? 1 : -1)));

    Resampler whole;
    std::vector<int16_t> a;
    whole.push(all.data(), all.size(), 8, 1, 11111, 44100, a);

    Resampler chunked;
    std::vector<int16_t> b;
    for (int c = 0; c < CHUNKS; ++c) chunked.push(all.data() + size_t(c) * N, N, 8, 1, 11111, 44100, b);

    const double expect = double(N) * CHUNKS * 44100.0 / 11111.0;
    CHECK(a.size() == b.size());
    bool same = a.size() == b.size();
    for (size_t i = 0; same && i < a.size(); ++i) same = std::abs(a[i] - b[i]) <= 1;   // rounding only
    CHECK(same);
    // The tail can't interpolate past the last input sample: at most
    // dst/src (~4) frames short over the whole stream.
    CHECK(std::abs(double(b.size() / 2) - expect) <= 5.0);

    // 16-bit stereo, downsampling, odd chunk sizes.
    std::vector<uint8_t> st;
    for (int i = 0; i < 9001; ++i) { int16_t v = int16_t(i * 3); st.push_back(uint8_t(v)); st.push_back(uint8_t(v >> 8)); st.push_back(uint8_t(-v)); st.push_back(uint8_t((-v) >> 8)); }
    Resampler r2;
    std::vector<int16_t> c2;
    for (size_t off = 0; off < st.size(); off += 4 * 37) r2.push(st.data() + off, std::min<size_t>(4 * 37, st.size() - off), 16, 2, 48000, 44100, c2);
    CHECK(std::abs(double(c2.size() / 2) - 9001.0 * 44100 / 48000) <= 5.0);
    CHECK(c2[0] == 0 && c2[2] > 0 && c2[3] < 0);   // left rises, right falls

    // 8-bit unsigned centre (128) is silence.
    uint8_t mid[4] = {128, 128, 128, 128};
    std::vector<int16_t> sil;
    Resampler r3;
    r3.push(mid, 4, 8, 1, 11025, 44100, sil);
    bool silent = true;
    for (auto v : sil) silent = silent && v == 0;
    CHECK(silent && !sil.empty());
}

// ---------------------------------------------------------------------------
// HERAD: a hand-built version-1 song with one track and one instrument.
//   program 0, note on C4 (60) at velocity 64, 10 ticks later note off.
static Bytes tinySong(uint16_t speed) {
    Bytes track = {0x00, 0xC0, 0x00,  0x00, 0x90, 60, 0x40,  0x00, 0xE0, 0x60,
                   0x0A, 0x80, 60, 0x00,  0x00, 0xFF};
    Bytes d(0x34, 0);
    auto put16 = [&](size_t at, uint16_t v) { d[at] = uint8_t(v); d[at + 1] = uint8_t(v >> 8); };
    put16(2, 0x32);                                   // track 0 at 0x32 + 2 = 0x34
    put16(0x32, speed);
    d.insert(d.end(), track.begin(), track.end());
    put16(0, uint16_t(d.size()));                     // instruments follow the track
    Bytes inst(40, 0);
    inst[3] = 1; inst[16] = 1;                        // multipliers
    inst[5] = 15; inst[18] = 15;                      // attack
    inst[10] = 10; inst[23] = 0;                      // output levels
    inst[4] = 3;                                      // feedback
    d.insert(d.end(), inst.begin(), inst.end());
    return d;
}

static bool hasWrite(const std::vector<herad::Write>& w, uint8_t reg, uint8_t val) {
    for (auto& x : w) if (x.reg == reg && x.val == val) return true;
    return false;
}

// ---- hiss filter (src/voice_filter.h) -------------------------------------
static std::vector<float> runFilter(VoiceFilter& f, const std::vector<float>& x, size_t chunk) {
    std::vector<float> all, part;
    for (size_t i = 0; i < x.size(); i += chunk) {
        f.process(x.data() + i, std::min(chunk, x.size() - i), part);
        all.insert(all.end(), part.begin(), part.end());
    }
    f.flush(part);
    all.insert(all.end(), part.begin(), part.end());
    return all;
}

static double rmsDb(const std::vector<float>& x, size_t a, size_t b) {
    double e = 0;
    for (size_t i = a; i < b; ++i) e += double(x[i]) * x[i];
    return 10 * std::log10(e / double(b - a) + 1e-20);
}

static void testVoiceFilter() {
    // FFT there and back.
    std::vector<std::complex<float>> a(16), b;
    for (int i = 0; i < 16; ++i) a[size_t(i)] = {float(i % 5) - 2.0f, float(i % 3)};
    b = a;
    VoiceFilter::fft(b.data(), 16, false);
    VoiceFilter::fft(b.data(), 16, true);
    float err = 0;
    for (int i = 0; i < 16; ++i) err = std::max(err, std::abs(a[size_t(i)] - b[size_t(i)]));
    CHECK(err < 1e-5f);

    // The high-pass takes the offset away.
    VoiceFilter::HighPass hp;
    hp.design(80, 11111);
    float y = 0;
    for (int i = 0; i < 11111; ++i) y = hp.run(0.25f);
    CHECK(std::fabs(y) < 1e-3f);

    // A "voice" (a 440 Hz tone in bursts) in white hiss at -35 dB.
    const int rate = 11111;
    std::vector<float> x(size_t(rate) * 6);
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 0.0178f);
    for (size_t i = 0; i < x.size(); ++i) {
        bool voice = (i / size_t(rate)) % 2 == 0;   // on, off, on, ...
        x[i] = noise(rng) + (voice ? 0.3f * std::sin(2 * 3.14159265f * 440 * float(i) / rate) : 0.0f);
    }
    std::vector<uint8_t> pcm(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        pcm[i] = uint8_t(std::clamp(std::lround(x[i] * 128 + 128), 0L, 255L));
    auto prof = VoiceFilter::profile(pcm.data(), pcm.size(), rate);
    CHECK(prof.size() == size_t(VoiceFilter::kBins));

    VoiceFilter f;
    f.start(prof, rate);
    auto out = runFilter(f, x, 889);   // a video frame's worth at a time
    CHECK(out.size() == x.size());     // as long as the input, after flush
    // Between bursts (second 1..2): hiss well down. In a burst (second 2..3): the voice kept.
    double hissIn = rmsDb(x, size_t(rate) * 1 + 2000, size_t(rate) * 2 - 2000);
    double hissOut = rmsDb(out, size_t(rate) * 1 + 2000, size_t(rate) * 2 - 2000);
    double voiceIn = rmsDb(x, size_t(rate) * 2 + 2000, size_t(rate) * 3 - 2000);
    double voiceOut = rmsDb(out, size_t(rate) * 2 + 2000, size_t(rate) * 3 - 2000);
    CHECK(hissIn - hissOut > 8.0);
    CHECK(std::fabs(voiceIn - voiceOut) < 1.0);

    // Chunking doesn't change the result.
    VoiceFilter g;
    g.start(prof, rate);
    auto whole = runFilter(g, x, x.size());
    float diff = 0;
    for (size_t i = 0; i < whole.size(); ++i) diff = std::max(diff, std::fabs(whole[i] - out[i]));
    CHECK(whole.size() == out.size() && diff < 1e-4f);

    // Lined up with the input: the burst starts at sample 2*rate, not later.
    CHECK(rmsDb(out, size_t(rate) * 2 - 300, size_t(rate) * 2 - 100) < voiceOut - 15);
    CHECK(rmsDb(out, size_t(rate) * 2 + 100, size_t(rate) * 2 + 300) > voiceOut - 3);

    // Bypassed: exactly the input, same length and timing.
    VoiceFilter h;
    h.start(prof, rate);
    h.setBypass(true);
    auto dry = runFilter(h, x, 500);
    CHECK(dry == x);

    // Off (no profile): passes through.
    VoiceFilter o;
    o.start({}, rate);
    CHECK(!o.active());
    CHECK(VoiceFilter::profile(pcm.data(), 100, rate).empty());   // too short
}

static void testHerad() {
    Bytes d = tinySong(256);
    herad::Song s;
    CHECK(s.parse(d));
    CHECK(!s.v2 && !s.agd);
    CHECK(s.tracks.size() == 1 && s.instruments.size() == 1);
    CHECK(s.length_ticks() == 10);

    herad::Player p(s, false);
    std::vector<herad::Write> w;
    p.rewind(w);
    CHECK(hasWrite(w, 0x01, 0x20) && hasWrite(w, 0xBD, 0x00) && hasWrite(w, 0x08, 0x40));

    // First tick: program change, note on (C4 -> octave 3, F-number 343),
    // then a bend of +1 semitone (0x60) -> F-number 364, still keyed on.
    w.clear();
    CHECK(p.timer(w));
    CHECK(hasWrite(w, 0x20, 0x01) && hasWrite(w, 0x60, 0xF0) && hasWrite(w, 0x40, 0x0A));
    CHECK(hasWrite(w, 0xC0, 0x07));                   // feedback 3; connection byte 0 = additive
    CHECK(hasWrite(w, 0xA0, 343 & 0xFF) && hasWrite(w, 0xB0, 0x20 | (3 << 2) | (343 >> 8)));
    CHECK(hasWrite(w, 0xA0, 364 & 0xFF));
    // Note off exactly 10 ticks later (speed 256: one tick per timer call).
    int off_at = -1;
    for (int i = 1; i <= 20 && off_at < 0; ++i) {
        w.clear();
        p.timer(w);
        if (hasWrite(w, 0xB0, (3 << 2) | (364 >> 8))) off_at = i;
    }
    CHECK(off_at == 10);
    w.clear();
    for (int i = 0; i < 5; ++i) p.timer(w);
    CHECK(!p.timer(w));                               // ended (no loop)

    // Speed 512: one tick every second timer call.
    herad::Song s2;
    CHECK(s2.parse(tinySong(512)));
    herad::Player p2(s2, false);
    p2.rewind(w);
    int calls = 0;
    while (p2.timer(w) && calls < 1000) ++calls;
    CHECK(calls >= 20 && calls <= 26);

    // Looping (header: from measure 1 to 2, forever): never ends, counts passes.
    Bytes ld = tinySong(256);
    ld[0x2C] = 1; ld[0x2E] = 2;
    herad::Song ls;
    CHECK(ls.parse(ld));
    herad::Player p3(ls, true);
    p3.rewind(w);
    for (int i = 0; i < 100; ++i) CHECK(p3.timer(w));
    CHECK(p3.loops() >= 1);

    // Rejects junk.
    herad::Song bad;
    CHECK(!bad.parse(Bytes(10, 0)));
    Bytes wrong = tinySong(256); wrong[2] = 0x40;    // first track must be at 0x32/0x52
    CHECK(!bad.parse(wrong));

    // On the emulated chip: silence before the note, sound once it plays.
    FmMusic m;
    CHECK(m.load(d.data(), d.size(), false));
    std::vector<int32_t> out(2 * 2205, 0);
    m.render(out.data(), 2205, 44100, 1.0f);          // 50 ms
    int32_t peak = 0;
    for (auto v : out) peak = std::max(peak, std::abs(v));
    CHECK(peak > 100);
    CHECK(std::abs(FmMusic::chip_rate() - 49715.9) < 1.0);
}

// A version-2 song: track 0 plays a drum through a keymap, track 1 a bass note.
static Bytes keymapSong() {
    Bytes t0 = {0x00, 0xC0, 0x00,  0x00, 0x90, 40, 0x7F,  0x05, 0x80, 40,  0x00, 0xFF};
    Bytes t1 = {0x00, 0xC0, 0x02,  0x00, 0x90, 36, 0x40,  0x08, 0x80, 36,  0x00, 0xFF};
    Bytes d(0x34, 0);
    auto put16 = [&](size_t at, uint16_t v) { d[at] = uint8_t(v); d[at + 1] = uint8_t(v >> 8); };
    put16(0x32, 256);
    put16(2, uint16_t(d.size() - 2));
    d.insert(d.end(), t0.begin(), t0.end());
    put16(4, uint16_t(d.size() - 2));
    d.insert(d.end(), t1.begin(), t1.end());
    put16(0, uint16_t(d.size()));
    Bytes keymap(40, 0);
    keymap[0] = 0xFF;                                 // mode -1: keymap
    for (int k = 4; k < 40; ++k) keymap[size_t(k)] = 1;   // every note -> instrument 1
    Bytes hat(40, 0);                                 // metallic noise, short
    hat[3] = 14; hat[16] = 14; hat[4] = 7; hat[5] = 15; hat[18] = 15; hat[21] = 8; hat[22] = 8;
    Bytes bass(40, 0);                                // sustaining
    bass[3] = 1; bass[16] = 1; bass[5] = 15; bass[18] = 15; bass[20] = 1; bass[19] = 2; bass[14] = 1;
    for (auto* i : {&keymap, &hat, &bass}) d.insert(d.end(), i->begin(), i->end());
    return d;
}

static void testMusicStyles() {
    herad::Song s;
    CHECK(s.parse(keymapSong()));
    CHECK(s.v2 && s.tracks.size() == 2 && s.instruments.size() == 3);

    // Note events, with the keymap resolved to the instrument that sounds.
    herad::Player p(s, false);
    std::vector<herad::Write> w;
    std::vector<herad::Event> ev;
    p.set_events(&ev);
    p.rewind(w);
    p.timer(w);
    int ons = 0;
    for (auto& e : ev) {
        if (e.type != herad::Event::NoteOn) continue;
        ++ons;
        if (e.track == 0) CHECK(e.inst == 1 && e.note == 40 && e.vel == 0x7F);
        if (e.track == 1) CHECK(e.inst == 2 && e.note == 36);
    }
    CHECK(ons == 2);
    ev.clear();
    bool off = false;
    for (int i = 0; i < 10; ++i) { p.timer(w); for (auto& e : ev) off |= e.type == herad::Event::NoteOff; }
    CHECK(off);

    // GM: the keymap's instrument is a drum (the busiest cymbal: closed hat),
    // the sustaining low part a synth bass, lifted into range.
    auto gm = herad::gm_map(s);
    CHECK(gm.size() == 3);
    CHECK(gm[0].notes == 0 && gm[0].program == -1);   // the keymap itself plays nothing
    CHECK(gm[1].drum == 42);
    CHECK(gm[2].drum == -1 && gm[2].program >= 32 && gm[2].program <= 39);
    CHECK(gm[2].key + gm[2].shift >= 28);
    // C0 at multiplier 1 is MIDI 12; carrier multiplier 2 adds an octave.
    herad::Instrument in;
    in.car_mul = 1;
    CHECK(herad::midi_key(s, in, 24 + 48) == 60);
    in.car_mul = 2;
    CHECK(herad::midi_key(s, in, 24 + 48) == 72);
    CHECK(herad::track_pans(s).size() == 2);

    // Enhanced: OPL3, stereo out, sound once the notes play.
    Bytes d = keymapSong();
    FmMusic e(MusicStyle::Stereo);
    CHECK(e.load(d.data(), d.size(), false));
    CHECK(e.style() == MusicStyle::Stereo);
    std::vector<int32_t> out(2 * 4410, 0);
    e.render(out.data(), 4410, 44100, 1.0f);
    int32_t peak = 0;
    for (auto v : out) peak = std::max(peak, std::abs(v));
    CHECK(peak > 50);
    // SoundFont without a bank plays Enhanced.
    FmMusic sf(MusicStyle::SoundFont, nullptr);
    CHECK(sf.style() == MusicStyle::Stereo && sf.load(d.data(), d.size(), false));
    FmMusic edm(MusicStyle::Enhanced, nullptr);
    CHECK(edm.style() == MusicStyle::Stereo);
    // The soft limiter: unity below the knee, never past the ceiling.
    CHECK(musicfx::soft_clip(0.5f, 0.7f, 1.0f) == 0.5f);
    CHECK(musicfx::soft_clip(10.0f, 0.7f, 1.0f) <= 1.0f && musicfx::soft_clip(-10.0f, 0.7f, 1.0f) >= -1.0f);
    CHECK(!SoundFontBank::load("no-such-file.sf2"));
    // Starting part-way: the sequencer is already there.
    FmMusic later(MusicStyle::Original);
    CHECK(later.load(d.data(), d.size(), false, 5));
    CHECK(later.position() >= 5);

    // The reverb has a tail; the EQ passes DC through a 0 dB shelf.
    musicfx::Chain fx;
    fx.init(44100, 0.0f, 0.0f, 0.6f, 0.5f, 0.3f);
    std::vector<float> lr(2 * 8000, 0.0f);
    lr[0] = lr[1] = 1.0f;
    fx.run(lr.data(), 8000);
    float tail = 0;
    for (int i = 4000; i < 8000; ++i) tail = std::max(tail, std::fabs(lr[size_t(2 * i)]));
    CHECK(tail > 1e-5f);
}

static void testSides() {
    for (auto st : {sides::Carbon, sides::Space, sides::Cyberpunk}) {
        const auto p = sides::make(st, 60, 270);
        CHECK(p.w == 60 && p.h == 270 && p.base.size() == size_t(60 * 270));
        bool opaque = true, varied = false;
        for (auto c : p.base) { opaque &= (c >> 24) == 0xFF; varied |= c != p.base[0]; }
        CHECK(opaque && varied);
        CHECK(p.overlay.empty() || p.overlay.size() == p.base.size());
        CHECK(sides::make(st, 60, 270).base == p.base);    // the same picture every time
    }
    CHECK(!sides::make(sides::Space, 60, 270).right.empty());
    CHECK(sides::make(sides::Carbon, 0, 10).base.empty());
}

int main() {
    for (auto* t : {testHsq, testHnm173, testHnmCorrupt, testSpriteSet, testResamplerChunks, testVoiceFilter, testHerad, testMusicStyles, testSides}) {
        try { t(); }
        catch (const std::exception& e) { ++g_failures; std::fprintf(stderr, "exception: %s\n", e.what()); }
    }
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
