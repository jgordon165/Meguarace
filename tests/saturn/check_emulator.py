#!/usr/bin/env python3
"""Checks a Saturn emulator run of the test disc (make_test_dat.py).

Takes the screenshots (PPM, <prefix>_<frame>.ppm, one every 10 emulator
frames) and the audio (<prefix>.wav) of a run with no buttons pressed, and
checks what make_test_dat.py's videos must look and sound like:

  * MINDSCAP.HNM: the bars and square of video frame k, shown when its
    sound is due (picture within a frame of the 440 Hz tone's clock);
    palette index 0 in its own colour (dark blue) on the picture's bottom
    rows; black borders above and below the 200 picture lines
  * CRYO.HNM: silent; the grey ramp with overlay squares built on it, their
    transparent band showing the ramp
  * INTRO.HNM: 660 Hz; the bars turn grey at its palette change
  * then the video explorer (text on black)

    python tests/saturn/check_emulator.py <prefix>
Exit status 0 if every check passes.
"""
import glob
import re
import sys
import wave

import numpy as np
from PIL import Image

failures = []


def check(ok, what):
    print(("ok    " if ok else "FAIL  ") + what)
    if not ok:
        failures.append(what)


def tone_segments(path):
    w = wave.open(path)
    rate = w.getframerate()
    d = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2)[:, 0].astype(float)
    win = rate // 10
    segs = []
    for i in range(0, len(d) - win, win):
        seg = d[i:i + win]
        if np.sqrt(np.mean(seg ** 2)) < 200:
            f = 0
        else:
            sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
            f = int(round(np.fft.rfftfreq(len(seg), 1 / rate)[np.argmax(sp)] / 10) * 10)
        if segs and segs[-1][0] == f:
            segs[-1][2] = (i + win) / rate
        else:
            segs.append([f, i / rate, (i + win) / rate])
    return segs


def classify(im):
    px = im.load()
    if px[160, 100] == (0, 0, 0) and px[300, 200] == (0, 0, 0):
        return "text"
    greys = all(px[x, 100][0] == px[x, 100][1] == px[x, 100][2] for x in range(8, 320, 16))
    if greys and px[8, 100][0] < px[315, 100][0]:
        return "ramp"
    if greys:
        return "grey"
    return "bars"


def main():
    prefix = sys.argv[1]
    shots = {}
    for path in glob.glob(prefix + "_*.ppm"):
        shots[int(re.search(r"_(\d+)\.ppm$", path).group(1))] = Image.open(path).convert("RGB")
    frames = sorted(shots)
    kinds = {f: classify(shots[f]) for f in frames}

    segs = [s for s in tone_segments(prefix + ".wav") if s[2] - s[1] >= 0.15 or s[0] == 0]
    tones = [s for s in segs if s[0]]
    print("sound:", ", ".join("%d Hz %.1f-%.1fs" % (f, a, b) for f, a, b in tones))
    check(len(tones) >= 2 and tones[0][0] == 440 and tones[1][0] == 660, "440 Hz then 660 Hz")
    if len(tones) >= 2:
        check(abs((tones[0][2] - tones[0][1]) - 4.0) < 0.25, "MINDSCAP's tone lasts its 50 frames (4.0 s)")
        gap = tones[1][1] - tones[0][2]
        check(1.5 < gap < 3.5, "silence for CRYO between them (%.1f s)" % gap)
        check(abs((tones[1][2] - tones[1][1]) - 2.0) < 0.25, "INTRO's tone lasts 2.0 s")

    # MINDSCAP: square position gives the video frame; compare with the tone's clock.
    start = tones[0][1] if tones else 0
    errs = []
    for f in frames:
        if kinds[f] != "bars" or not tones or f / 60 > tones[0][2]:
            continue
        px = shots[f].load()
        ys = [y for y in range(12, 212) for x in range(0, 320, 4) if px[x, y] == (248, 248, 248)]
        if not ys:
            continue
        k = (min(ys) - 12) // 3
        errs.append(k - (f / 60 - start) * 12.5)
    if errs:
        print("picture vs sound: %+.2f .. %+.2f video frames" % (min(errs), max(errs)))
        check(min(errs) > -1.8 and max(errs) < 0.8, "MINDSCAP's picture follows its sound (within a frame)")
    else:
        check(False, "MINDSCAP frames seen")

    bars = [f for f in frames if kinds[f] == "bars"]
    if bars:
        px = shots[bars[len(bars) // 2]].load()
        check(px[100, 5] == (0, 0, 0) and px[100, 218] == (0, 0, 0), "black borders above and below")
        check(px[100, 208] == (0, 0, 80), "palette index 0 in its own colour (%s)" % (px[100, 208],))
        check(px[100, 211] == (0, 0, 80) and px[100, 212] == (0, 0, 0), "picture is lines 12-211")

    ramps = [f for f in frames if kinds[f] == "ramp"]
    check(bool(ramps), "CRYO's ramp shown")
    if ramps:
        px = shots[ramps[-1]].load()
        check(px[50, 14] == (0, 0, 160), "CRYO's index 0 (blue) shown on its top rows")
        band = px[100, 12 + 60 + 20]          # the transparent band of the squares
        top = px[100, 12 + 60 + 5]
        check(top == (248, 248, 248) and band != top, "overlay squares drawn, transparent band shows the ramp")

    order = [kinds[f] for f in frames]
    seq = [k for i, k in enumerate(order) if i == 0 or order[i - 1] != k]
    print("screens:", " ".join(seq))
    try:
        i = seq.index("bars"); j = seq.index("ramp", i); g = seq.index("grey", j); t = seq.index("text", g)
        check(True, "bars, ramp, grey (INTRO's palette change), then the explorer")
    except ValueError:
        check(False, "bars, ramp, grey (INTRO's palette change), then the explorer")

    print("%d failures" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
