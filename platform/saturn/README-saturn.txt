MEGUARACE FOR THE SEGA SATURN
=============================

This is the Meguarace port of MegaRace built for the Sega Saturn with
libyaul. No game data is included: you need your own copy of MegaRace
(the DOS CD), and you make the Saturn disc from it on your computer.

This build is the first milestone (see docs/saturn.md in the repository):
it plays the original boot sequence (Mindscape, Cryo, the intro with Lance
Boyle) with its sound, then opens a video explorer in which every video
on the disc can be played. The game itself (menus, races) comes next.

Making the disc
---------------
You need Python 3.8 or newer (python.org). From this folder:

  python saturn_disc.py --data D:\ --build . --out megarace

where D:\ is your MegaRace CD (or a folder holding a copy of it). This
writes megarace.iso and megarace.cue (about 500 MB).

Playing it
----------
  Emulator          open megarace.cue (Mednafen, Ymir, Kronos, SSF...)
  Optical drive     copy megarace.iso and megarace.cue to the SD card
  emulator          (Satiator, Fenrir, Rhea/Phoebe, MODE)
  Burned CD-R       burn megarace.cue with any burning program, for a Saturn
                    that boots CD-Rs (a modchip, or Pseudo Saturn Kai on an
                    Action Replay cartridge)

A RAM cartridge (1 or 4 MB, such as the Action Replay 4M) isn't needed
yet. The explorer's top line shows whether one was found; the game will
use it for more room when it arrives.

Controls
--------
  Videos      A, C or START skips a video, B skips the rest of a sequence
  Explorer    UP/DOWN choose, LEFT/RIGHT page, A plays

After a video the explorer shows what played: frames, sound rate, frames
that failed to decode (there should be none) and how often the picture
had to wait for the CD drive. If you see a wrong picture, a stutter or
any of those numbers, please report it with the video's name.
