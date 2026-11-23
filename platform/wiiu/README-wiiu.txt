MEGUARACE FOR THE WII U
=======================

This is the Meguarace port of MegaRace built for the Wii U as a homebrew
bundle (meguarace.wuhb) for the Aroma environment.

No game data is included. You need your own copy of MegaRace (the DOS CD).

1. Copy the "wiiu" folder from this zip to the root of the SD card Aroma
   uses (it merges with the one there), so you have
   sd:/wiiu/apps/meguarace.wuhb  and  sd:/wiiu/apps/meguarace/data/
2. Copy every file from your MegaRace CD (MEGARACE.DAT, RACEPRG.EXE and the
   rest) into  sd:/wiiu/apps/meguarace/data/
   Optional: ripped CD music tracks (track02.wav ...) in .../data/music/
   and HD packs made with hd-conversion-utility in  sd:/wiiu/apps/meguarace/hd/
3. Put the card back, boot into Aroma, and start Meguarace from the Wii U
   Menu.

The picture is on the TV and the GamePad at once.

Controls (GamePad or Pro Controller)
------------------------------------
  Left stick / D-pad     steer, move in menus
  ZR                     accelerate          ZL              brake
  X or R                 fire
  A or +                 choose / continue   B or -          back / quit
  + (in a race)          pause / go on
  Y                      HD pictures on/off (with HD packs)
  L                      music on/off (menu)
  HOME                   the HOME Menu (close the game from there)

Names (Hall of Fame, saves) are typed on an on-screen keyboard: the D-pad
moves between the keys, A types one (or SPACE, DEL, DONE), B rubs out a
letter (with nothing typed, it leaves), X types a space, + is done.

Saves go in  sd:/wiiu/apps/meguarace/save/
A scripts/ folder in  sd:/wiiu/apps/meguarace/  is used instead of the one
inside the .wuhb (for trying script changes).
