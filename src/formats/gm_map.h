#pragma once
// From a HERAD song's FM instruments to General MIDI, for the SoundFont
// music mode (src/fm.h), and a stereo place for each track, for the
// enhanced FM mode. No audio code: the tests check it directly.
//
// Drums: MegaRace's songs play their drums through keymap instruments (one
// instrument per note); every instrument a keymap points at is a drum, and
// its envelope, noise (high operator multipliers, feedback) and pitch slide
// pick the GM drum: kick, snare, toms, hi-hats, cymbals.
// The rest are melodic: sustaining or decaying, attack, brightness and the
// range the song plays them in pick a GM program (bass, electric piano,
// leads, pads, bells...). It's a guess from the patch, so music.lua can
// override any instrument per song.

#include "formats/herad.h"

#include <vector>

namespace herad {

struct GmVoice {
    int program = -1;     // GM program 0..127 (melodic)
    int drum = -1;        // GM drum key 35..81 (channel 10); -1 = melodic
    int key = 60;         // median sounding key (MIDI) the song plays it at
    int shift = 0;        // octaves (in semitones) to bring a melodic part into range
    int notes = 0;        // how many notes the song plays on it
};

// The MIDI key a note sounds at on an instrument: its transpose (or fixed
// note), the driver's octave layout and the carrier's frequency multiplier.
int midi_key(const Song& s, const Instrument& in, uint8_t note);

// GM program or drum for every instrument (unused ones: program -1).
std::vector<GmVoice> gm_map(const Song& s);

// Stereo place per track for the enhanced mode: -1 left, 0 centre, 1 right.
// Bass and drum tracks stay in the centre; the others alternate by range.
std::vector<int> track_pans(const Song& s);

}  // namespace herad
