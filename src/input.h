#pragma once
#include <SDL.h>
#include <array>
#include <string>
#include <vector>

// Keyboard state with edge detection. Keys are named the SDL way
// ("Left", "Return", "Space", "A", ...); names are case-insensitive.
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

    // The Xbox 360's controller buttons by name (Options > CONTROLLER,
    // scripts/game/pad.lua): "A", "B", "X", "Y", "LB", "RB", "LT", "RT",
    // "LS", "RS", "START", "BACK", and "UP", "DOWN", "LEFT", "RIGHT" (the
    // D-pad or the left stick). Elsewhere there are none: all false.
    void endEvents();                               // call after polling events
    bool padDown(const std::string& button) const;
    bool padPressed(const std::string& button) const;
    std::string padFirstPressed() const;            // a button that went down this frame, or ""
    // Buttons in the list stop being keys too (the race's own), until the
    // next call; an empty list gives them all back.
    void padTake(const std::vector<std::string>& buttons);

private:
    unsigned padCur_ = 0, padPrev_ = 0;
    static unsigned padBit(const std::string& button);
    std::array<bool, SDL_NUM_SCANCODES> cur_{}, prev_{};
    std::string text_;
    static SDL_Scancode lookup(const std::string& key);
};
