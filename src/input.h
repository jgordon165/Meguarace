#pragma once
#include <SDL.h>
#include <array>
#include <string>

// Keyboard state with edge detection. Keys are named the SDL way
// ("Left", "Return", "Space", "A", ...); names are case-insensitive.
class Input {
public:
    void beginFrame();              // call before polling events
    void handle(const SDL_Event& e);
    bool down(const std::string& key) const;
    bool pressed(const std::string& key) const;    // went down this frame
    bool released(const std::string& key) const;   // went up this frame

private:
    std::array<bool, SDL_NUM_SCANCODES> cur_{}, prev_{};
    static SDL_Scancode lookup(const std::string& key);
};
