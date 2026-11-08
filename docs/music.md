# Music styles

MegaRace's music is six FM songs in `MEGARACE.DAT` (Stéphane Picq's HERAD
format, written for the AdLib / Sound Blaster OPL2 chip). The port's
sequencer (`src/formats/herad.cpp`) plays them exactly as the original
driver does. **Options > ADVANCED > MUSIC** picks what plays the notes. It's
saved as it changes, and the song that's playing carries on in the new style
from where it was.

| Style | What you hear |
|---|---|
| ORIGINAL | The emulated OPL2 ([ymfm](https://github.com/aaronsgiles/ymfm)), mono, as on a Sound Blaster in 1994. |
| STEREO FM | The same FM, register for register, on an emulated OPL3. The OPL3 can send each voice to its own output, so the tracks spread across the stereo field: the bass and the drums in the middle, the others alternating left and right. Then a warmer EQ (+3 dB at 120 Hz, −2.5 dB above 9 kHz) and a small room reverb. |
| SOUNDFONT | The songs' notes played by a General MIDI SoundFont ([TinySoundFont](https://github.com/schellingb/TinySoundFont)) instead of the chip, with the same reverb. Pitch bends, slides and each instrument's level and velocity response come from the song. |
| ENHANCED (the default) | A dance (EDM) remix on the same SoundFont. The songs already run at 106–148 BPM. See below. |

All four are about equally loud (ENHANCED a little louder), and they're cheap: the SoundFont style
takes about 2% of one CPU core.

## The SoundFont

The game comes with [GeneralUser GS](https://www.schristiancollins.com/generaluser.php)
by S. Christian Collins in `soundfont/` next to the game (free to use and
redistribute; its `LICENSE.txt` is beside it). To use another bank, put a
`.sf2` in one of these folders. The first one found is used, and the
folders are searched in this order:

1. `soundfont/` next to the game
2. `soundfont/` in your Meguarace user folder (where the save file is)
3. the `--music` folder

To use your own bank instead of the bundled one, replace or remove the
`.sf2` in the first folder. Building from source downloads GeneralUser GS
into the build folder (`MEGARACE_FETCH_SOUNDFONT`, on by default; the
download is pinned and checked by hash). Without a SoundFont, SOUNDFONT
and ENHANCED play STEREO FM, and ADVANCED tells you why.

## ENHANCED: the dance remix

The same notes and the same instrument choices as SOUNDFONT, played as a
dance mix (`src/fm.cpp`, `edm_layers`):

| Part | Becomes |
|---|---|
| Bass | Acid Bass over a sine sub an octave down (GeneralUser GS's variation bank) |
| Leads (square, saw, brass) | A supersaw: two saw leads detuned ±14 cents and spread left and right, plus one an octave down |
| Pads, strings | Sweep Pad and Polysynth, detuned and spread wide |
| Electric pianos, plucks | Polysynth and saw stabs |
| Bells, vibraphone | Echo Drops |
| Drums | The Dance kit, with a clap on every snare, and a four-on-the-floor kick on every beat while the song's drums are playing |

The synths pump to the kick (sidechain: down 70%, back over 60% of a beat),
and go through a stereo ping-pong delay on the dotted eighth of the song's
own tempo and a bigger room. The drums have their own bus: more low end
and only a touch of room. A soft limiter over the mix catches the peaks.
The kicks follow the song's measures (96 ticks, a beat every 24), so they
land on the beat. Drum-free intros and breaks stay without them.

## Which GM instrument plays which FM instrument

An FM song doesn't say "piano" or "snare". The engine guesses each
instrument from its sound (`src/formats/gm_map.cpp`):

- **Drums.** The songs play their kits through keymap instruments (one FM
  patch per note). Every patch a keymap uses is a drum, and so is any
  metallic-noise patch (hi-hats played on an ordinary track). A patch whose
  pitch drops is a kick or a tom: the most used one in the song is the kick.
  A noisy modulator on a body is a snare: the most used one is the snare,
  the rest are claps. A metallic carrier is a hi-hat or cymbal: the busiest
  is the closed hi-hat, the next the open one, and a long ring that's played
  sparingly is a crash.
- **Melodic parts.** Below about A2 they're bass (synth bass, or finger bass
  for soft decaying patches). Sustaining patches with a slow attack are pads;
  with a fast attack and long notes, brass or strings, otherwise square or
  saw leads. Decaying patches are electric pianos, or vibraphone and celesta
  when their harmonics are metallic. Parts the FM chip played below hearing
  (sounding only through their harmonics) move up an octave or two.

To change a choice, list it in `scripts/game/music.lua` under `M.GM`, by
song and the song's instrument number (from 0). Use a GM program 0..127,
or `M.DRUM` plus a GM drum key:

```lua
M.GM = {
    NEWSAN = { [4] = 81, [13] = M.DRUM + 36 },   -- instrument 4: saw lead; 13: kick
}
```

The songs' instrument numbers and what each became are easiest to see from
a quick test program calling `herad::gm_map` (`src/formats/gm_map.h`).
