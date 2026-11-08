#pragma once
// Backgrounds for the 16:9 wings (Options > ADVANCED > SIDES), drawn by code
// (no artwork files): CARBON (woven carbon fibre with a sheen), SPACE (a
// nebula with stars) and CYBERPUNK (a neon city over a perspective grid).
// Each is made for the left wing at its size on screen; the right wing is
// the same picture mirrored. SPACE and CYBERPUNK also have an overlay that
// scrolls down while the game runs (near stars, rain). No SDL: the tests
// call it directly.

#include <cstdint>
#include <vector>

namespace sides {

enum Style { Black = 0, Ambient = 1, Carbon = 2, Space = 3, Cyberpunk = 4 };

struct Picture {
    int w = 0, h = 0;
    std::vector<uint32_t> base;      // 0xAARRGGBB, opaque
    std::vector<uint32_t> overlay;   // same size, transparent where empty; empty: none
    // The right wing's own picture (drawn mirrored, so it too has the
    // picture edge on the right); empty: the left one mirrored.
    std::vector<uint32_t> right, rightOverlay;
};

// The left wing's picture, w x h pixels (the wing's size on screen; the
// picture edge is on the right).
Picture make(Style style, int w, int h);

}  // namespace sides
