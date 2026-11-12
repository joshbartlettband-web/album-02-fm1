#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Alive-Stable-7254
"""The scenes' screenshots (tests/scenes.txt) in one picture: a row a track.
    python3 album/tools/montage.py OUTDIR montage.png"""
import sys
from pathlib import Path
from PIL import Image

NAMES = [["box", "boom", "club"], ["pluck", "acid", "round"], ["pad", "stab", "wind"], ["brass", "glass", "funk"],
         ["choir", "robot", "monk"]]
d = Path(sys.argv[1])
im = Image.new("RGB", (3 * 244 + 4, 5 * 244 + 4), (40, 40, 40))
for r, row in enumerate(NAMES):
    for c, n in enumerate(row):
        p = d / f"scene-{n}.png"
        if p.exists():
            im.paste(Image.open(p).convert("RGB").resize((240, 240)), (4 + c * 244, 4 + r * 244))
im.save(sys.argv[2])
