MEGUARACE FOR THE XBOX 360 (.XEX)
=================================

This is the Meguarace port of MegaRace built as an Xbox 360 title
(meguarace.xex) with xsdk (openpak/x360-sdk). It runs on a console that
runs unsigned code: BadUpdate with FreeMyXe (or XeUnshackle), an RGH or a
JTAG. Launch it from the dashboard, Aurora or a file manager.

No game data is included. You need your own copy of MegaRace (the DOS CD).

1. Copy this whole "meguarace" folder to a USB stick (FAT32) or the hard
   drive, so it holds  meguarace.xex  and  scripts/ ...
2. Copy every file from your MegaRace CD (MEGARACE.DAT, RACEPRG.EXE and the
   rest) into  meguarace/data/
   Optional: ripped CD music tracks (track02.wav ...) in data/music/ and HD
   packs made with hd-conversion-utility in hd/
3. Launch meguarace.xex.

The game draws on the 360's GPU (Xenos), so it starts in its HD mode,
scaled to the TV, with everything as on the PC (smoothing, sharpen, the 16:9
sides, the ENHANCED music). If the GPU won't start, it draws on the CPU
instead and starts at 320x200 (the HD modes are too slow that way); the log
says which. Frames flip at the TV's vblank (60 Hz, three buffers); the log has the
frame times every 10 s. Options > ADVANCED > ANTI-ALIAS (on by default)
smooths the track video's diagonal edges on the GPU.
To leave: Options > QUIT. After Lance's farewell and the credits (B or
BACK skips them) the title stops its sound, shows a black frame and ends.
Where it goes is up to exit.txt beside meguarace.xex (copy
exit.txt.example and edit it):
  default                       the console's default dashboard (also with
                                no exit.txt; Aurora or FreeStyle if
                                DashLaunch makes it the default)
  Usb:\Aurora\default.xex       a .xex on the first USB drive (Usb1:,
                                Usb2: the others)
  Hdd:\Aurora\default.xex       on the hard drive
  \Device\Mass0\...\x.xex       a kernel path as it is
If that .xex isn't there, the log says so and the default dashboard is used.

Dashboard artwork: aurora/ has the port's own (an original design, no game
art) in the Xbox 360 dashboards' sizes: icon.png 64x64, banner.png 420x96,
boxart.png 900x600 (back, spine, front) and background.png 1280x720. The
.xex's Title ID is 4D475241, so Aurora keeps them for this title only. To
use them, add them with Aurora Asset Editor on a PC (it saves to Aurora
over FTP), or pick them in Aurora's own artwork options.

The sound is mixed on a hardware thread of its own.

Controls
--------
  Left stick / D-pad     steer, move in menus
  Right trigger          accelerate          Left trigger    brake
  X or RB                fire
  A or START             choose / continue   B or BACK       back / quit
  START (in a race)      pause / go on
  Y                      HD pictures on/off (with HD packs)
  LB                     music on/off (menu)
  Names: D-pad Up/Down change the letter, Right adds one, Left takes one off.

Saves go in save/ next to meguarace.xex. A log is written
there too: meguarace.log (send it along if something goes wrong).
