MEGUARACE FOR THE XBOX 360
==========================

This is the Meguarace port of MegaRace built for the Xbox 360 with LibXenon.
It runs on a console that can start XeLL (XeLL Reloaded): a BadUpdate
softmod with FreeMyXe, an RGH or a JTAG. It doesn't run from the normal
dashboard.

No game data is included. You need your own copy of MegaRace (the DOS CD).

1. Format a USB stick as FAT32.
2. Copy this whole "meguarace" folder to the root of the stick, so it is
   at  meguarace/meguarace.elf32,  meguarace/scripts/ ...
3. Copy every file from your MegaRace CD (MEGARACE.DAT, RACEPRG.EXE and the
   rest) into  meguarace/data/
   Optional: ripped CD music tracks (track02.wav ...) in meguarace/data/music/
   and HD packs made with hd-conversion-utility in meguarace/hd/
4. Plug the stick into the 360, start XeLL, and have it load
   meguarace/meguarace.elf32 from the stick (in XeLL Reloaded, the file
   browser or its "launch ELF" option, depending on your version).

Controls
--------
  Left stick / D-pad     steer, move in menus
  Right trigger          accelerate          Left trigger    brake
  X or RB                fire
  A or START             choose / continue   B or BACK       back / quit
  Y                      HD pictures on/off (with HD packs)
  LB                     music on/off (menu)

Saves go in meguarace/save/ on the stick.
