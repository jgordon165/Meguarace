#include "input.h"

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
