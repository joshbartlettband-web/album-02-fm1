#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Alive-Stable-7254
"""The simulator's WAVs, measured: peak, loudness, brightness, where the energy sits, and how often the limiter
is leaned on. A patch that is silent, clipped or the same as its neighbour shows here before it is heard.

    python3 album/tools/listen.py out
"""
import sys
import wave
from pathlib import Path
import numpy as np


def load(p):
    with wave.open(str(p)) as w:
        n, ch, sw, fs = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if sw == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        a = (b[:, 0] | b[:, 1] << 8 | b[:, 2] << 16)
        a = np.where(a & 0x800000, a - 0x1000000, a) / 8388608.0
    else:
        a = np.frombuffer(raw, dtype={2: np.int16, 4: np.int32}[sw]) / float(1 << (8 * sw - 1))
    return a.reshape(-1, ch), fs


def main():
    d = Path(sys.argv[1])
    print(f"{'file':16} {'peak dB':>8} {'rms dB':>7} {'centroid':>9} {'<150':>5} {'150-2k':>6} {'>2k':>5} {'hot%':>5} {'L/R':>5}")
    for p in sorted(d.glob("*.wav")):
        a, fs = load(p)
        m = a.mean(axis=1)
        if not np.isfinite(a).all() or not len(m):
            print(f"{p.name:16} NOT NUMBERS")
            continue
        peak = np.abs(a).max()
        rms = np.sqrt((m ** 2).mean())
        sp = np.abs(np.fft.rfft(m * np.hanning(len(m)))) ** 2
        f = np.fft.rfftfreq(len(m), 1 / fs)
        tot = sp.sum() + 1e-30
        cen = (sp * f).sum() / tot
        lo, mid, hi = sp[f < 150].sum() / tot, sp[(f >= 150) & (f < 2000)].sum() / tot, sp[f >= 2000].sum() / tot
        hot = (np.abs(a) > 0.9 * peak).mean() * 100 if peak > 0.5 else 0.0
        lr = np.sqrt((a[:, 0] ** 2).mean() + 1e-30) / np.sqrt((a[:, -1] ** 2).mean() + 1e-30)
        db = lambda x: 20 * np.log10(x + 1e-12)
        print(f"{p.name:16} {db(peak):8.1f} {db(rms):7.1f} {cen:9.0f} {lo:5.2f} {mid:6.2f} {hi:5.2f} {hot:5.1f} {lr:5.2f}")


if __name__ == "__main__":
    main()
