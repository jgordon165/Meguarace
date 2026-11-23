#include "input.h"

void Input::beginFrame() {
    prev_ = cur_;
    text_.clear();
}

void Input::handle(const SDL_Event& e) {
    if (e.type == SDL_TEXTINPUT) text_ += e.text.text;
    if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && !e.key.repeat) {
        auto sc = e.key.keysym.scancode;
        if (sc >= 0 && sc < SDL_NUM_SCANCODES) kb_[sc] = (e.type == SDL_KEYDOWN);
    }
    switch (e.type) {
    case SDL_CONTROLLERDEVICEADDED:
        SDL_GameControllerOpen(e.cdevice.which);   // stays open; SDL closes it at exit
        readPads();
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (SDL_GameController* c = SDL_GameControllerFromInstanceID(e.cdevice.which))
            SDL_GameControllerClose(c);
        readPads();
        break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
    case SDL_CONTROLLERAXISMOTION:
        readPads();
        break;
    default:
        break;
    }
    for (size_t i = 0; i < cur_.size(); ++i) cur_[i] = kb_[i] || pad_[i];
}

// Controllers -> keys, the Xbox 360's table (platform/xenon/sdl): the stick
// or the pad steers, the right trigger (ZR) accelerates and the left (ZL)
// brakes. SDL names the buttons by their labels, so on Nintendo's pads A
// (the right face button) confirms and B goes back.
void Input::readPads() {
    pad_.fill(false);
    const int stick = 16000, trigger = 8000;
    auto hold = [&](SDL_Scancode sc, bool on) { if (on) pad_[sc] = true; };
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) continue;
        SDL_GameController* c = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if (!c) continue;
        auto b = [&](SDL_GameControllerButton x) { return SDL_GameControllerGetButton(c, x) != 0; };
        auto a = [&](SDL_GameControllerAxis x) { return int(SDL_GameControllerGetAxis(c, x)); };
        hold(SDL_SCANCODE_UP, b(SDL_CONTROLLER_BUTTON_DPAD_UP) || a(SDL_CONTROLLER_AXIS_LEFTY) < -stick ||
                                  a(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > trigger);
        hold(SDL_SCANCODE_DOWN, b(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || a(SDL_CONTROLLER_AXIS_LEFTY) > stick ||
                                    a(SDL_CONTROLLER_AXIS_TRIGGERLEFT) > trigger);
        hold(SDL_SCANCODE_LEFT, b(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || a(SDL_CONTROLLER_AXIS_LEFTX) < -stick);
        hold(SDL_SCANCODE_RIGHT, b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || a(SDL_CONTROLLER_AXIS_LEFTX) > stick);
        hold(SDL_SCANCODE_RETURN, b(SDL_CONTROLLER_BUTTON_A) || b(SDL_CONTROLLER_BUTTON_START));
        hold(SDL_SCANCODE_ESCAPE, b(SDL_CONTROLLER_BUTTON_B) || b(SDL_CONTROLLER_BUTTON_BACK));
        hold(SDL_SCANCODE_SPACE, b(SDL_CONTROLLER_BUTTON_X) || b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));  // fire
        hold(SDL_SCANCODE_F10, b(SDL_CONTROLLER_BUTTON_Y));                                    // HD on/off
        hold(SDL_SCANCODE_M, b(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));                           // music
        hold(SDL_SCANCODE_BACKSPACE, b(SDL_CONTROLLER_BUTTON_RIGHTSTICK));
        hold(SDL_SCANCODE_P, b(SDL_CONTROLLER_BUTTON_START));                                  // pause in races
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
