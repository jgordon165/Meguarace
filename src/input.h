#pragma once
#include <SDL.h>
#include <array>
#include <string>

// Keyboard state with edge detection. Keys are named the SDL way
// ("Left", "Return", "Space", "A", ...); names are case-insensitive.
//
// Game controllers (the Wii U GamePad and Pro Controller, or any pad SDL
// knows on a PC) arrive as the keys that do the same job, as on the Xbox 360
// (README, "Controllers"): the game itself knows keys only.
class Input {
public:
    void beginFrame();              // call before polling events
    void handle(const SDL_Event& e);
    bool down(const std::string& key) const;
    bool pressed(const std::string& key) const;    // went down this frame
    bool released(const std::string& key) const;   // went up this frame
    bool anyPressed() const;                       // any key went down this frame
    std::string firstPressed() const;              // name of a key that went down this frame, or ""
    // Text entry (the options' HD FOLDER line): characters typed this frame,
    // as the keyboard layout gives them (UTF-8), while text input is on.
    void textStart() { SDL_StartTextInput(); }
    void textStop() { SDL_StopTextInput(); }
    const std::string& text() const { return text_; }

private:
    using Keys = std::array<bool, SDL_NUM_SCANCODES>;
    Keys kb_{}, pad_{};      // held on the keyboard, held on a controller
    Keys cur_{}, prev_{};    // either, now and last frame
    void readPads();
    std::string text_;
    static SDL_Scancode lookup(const std::string& key);
};
