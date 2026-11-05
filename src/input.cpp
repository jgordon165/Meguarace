#include "input.h"

void Input::beginFrame() { prev_ = cur_; }

void Input::handle(const SDL_Event& e) {
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

bool Input::released(const std::string& key) const {
    auto sc = lookup(key);
    return sc != SDL_SCANCODE_UNKNOWN && !cur_[sc] && prev_[sc];
}
