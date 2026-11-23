#include "input.h"

#include <cctype>

#ifdef MEGARACE_XENON_LAYOUT
extern "C" unsigned XENON_PadButtons(void);       // platform/xenon/sdl/SDL_xenon_video.c
extern "C" void XENON_SetPadKeyMask(unsigned mask);
#endif

namespace {
// In the bits' order (platform/xenon/xenon_platform.h, XP_BTN_).
const char* const kPadNames[] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT",
                                 "LS", "RS", "START", "BACK", "UP", "DOWN", "LEFT", "RIGHT"};
const int kPadCount = int(sizeof kPadNames / sizeof *kPadNames);
}  // namespace

unsigned Input::padBit(const std::string& button) {
    std::string b;
    for (char c : button) b += char(std::toupper(static_cast<unsigned char>(c)));
    for (int i = 0; i < kPadCount; ++i)
        if (b == kPadNames[i]) return 1u << i;
    return 0;
}

void Input::endEvents() {
    padPrev_ = padCur_;
#ifdef MEGARACE_XENON_LAYOUT
    padCur_ = XENON_PadButtons();
#endif
}

bool Input::padDown(const std::string& button) const {
    const unsigned b = padBit(button);
    return b && (padCur_ & b);
}

bool Input::padPressed(const std::string& button) const {
    const unsigned b = padBit(button);
    return b && (padCur_ & b) && !(padPrev_ & b);
}

std::string Input::padFirstPressed() const {
    const unsigned went = padCur_ & ~padPrev_;
    for (int i = 0; i < kPadCount; ++i)
        if (went & (1u << i)) return kPadNames[i];
    return {};
}

void Input::padTake(const std::vector<std::string>& buttons) {
    unsigned m = 0;
    for (const auto& b : buttons) m |= padBit(b);
#ifdef MEGARACE_XENON_LAYOUT
    XENON_SetPadKeyMask(m);
#else
    (void)m;
#endif
}

void Input::beginFrame() {
    prev_ = cur_;
    text_.clear();
}

void Input::handle(const SDL_Event& e) {
    if (e.type == SDL_TEXTINPUT) text_ += e.text.text;
    if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && !e.key.repeat) {
        auto sc = e.key.keysym.scancode;
        if (sc >= 0 && sc < SDL_NUM_SCANCODES) cur_[sc] = (e.type == SDL_KEYDOWN);
    }
}

SDL_Scancode Input::lookup(const std::string& key) {
    return SDL_GetScancodeFromName(key.c_str());
}

bool Input::down(const std::string& key) const {
    auto sc = lookup(key);
    return sc != SDL_SCANCODE_UNKNOWN && cur_[sc];
}

bool Input::pressed(const std::string& key) const {
    auto sc = lookup(key);
    return sc != SDL_SCANCODE_UNKNOWN && cur_[sc] && !prev_[sc];
}

bool Input::anyPressed() const {
    for (size_t i = 0; i < cur_.size(); ++i)
        if (cur_[i] && !prev_[i]) return true;
    return false;
}

std::string Input::firstPressed() const {
    for (size_t i = 0; i < cur_.size(); ++i)
        if (cur_[i] && !prev_[i]) {
            const char* n = SDL_GetScancodeName(static_cast<SDL_Scancode>(i));
            if (n && *n) return n;
        }
    return "";
}

bool Input::released(const std::string& key) const {
    auto sc = lookup(key);
    return sc != SDL_SCANCODE_UNKNOWN && !cur_[sc] && prev_[sc];
}
