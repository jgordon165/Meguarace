#include "fm.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)              // third-party header, not built with our /W4
#endif
#include "ymfm_opl.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace {
constexpr uint32_t OPL2_CLOCK = 3579545;   // Sound Blaster / AdLib crystal
}

struct FmMusic::Chip : ymfm::ymfm_interface {
    ymfm::ym3812 opl{*this};
};

FmMusic::FmMusic() : chip_(std::make_unique<Chip>()) {}
FmMusic::~FmMusic() = default;

double FmMusic::chip_rate() { return double(OPL2_CLOCK) / 72.0; }

bool FmMusic::load(const uint8_t* data, size_t size, bool loop) {
    playing_ = false;
    player_.reset();
    if (!song_.parse(data, size) || song_.agd) return false;
    chip_->opl.reset();
    player_ = std::make_unique<herad::Player>(song_, loop);
    writes_.clear();
    player_->rewind(writes_);
    for (auto& w : writes_) chip_->opl.write(0, w.reg), chip_->opl.write(1, w.val);
    writes_.clear();
    timer_acc_ = 0;
    pos_ = 0;
    prev_ = cur_ = 0;
    playing_ = true;
    return true;
}

int16_t FmMusic::next_sample() {
    // Timer interrupts land between chip samples at their exact rate.
    timer_acc_ -= 1.0;
    while (timer_acc_ < 0) {
        timer_acc_ += chip_rate() / herad::Song::TIMER_HZ;
        if (playing_) {
            if (!player_->timer(writes_)) playing_ = false;
            for (auto& w : writes_) chip_->opl.write(0, w.reg), chip_->opl.write(1, w.val);
            writes_.clear();
        }
    }
    ymfm::ym3812::output_data o;
    chip_->opl.generate(&o, 1);
    int32_t v = o.data[0];
    return int16_t(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

void FmMusic::render(int32_t* out, int frames, int rate, float gain) {
    if (!player_ || rate <= 0) return;
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
