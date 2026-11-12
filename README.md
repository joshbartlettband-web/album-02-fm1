# Album 02

The second in the Album series of firmwares for the M-VAVE FM-1 ([Album 01](https://github.com/joshbartlettband-web/album-fm1)).
Five tracks of filter-house: drums, bass, chords, lead and a singing voice, each patch with its own moving
picture in the style of 1970s space-opera anime. Played from its own keys or from a sequencer over MIDI.

**Status: the sound plays.** Five tracks with three patches each, from the keys (SELECT picks the track, PRESET
the patch) and over MIDI (channels 1 to 5, and 10 for the drums). K1 to K4 and ALGORITHM are Tone, Motion, Pump,
Sweep and Space; with EDIT held, K1 to K4 are Phaser, Chorus, Grit and Echo. The screen is a placeholder: the
pictures, the sequencer and the second core are still to come. The design is in [docs/PLAN.md](docs/PLAN.md).

## Build

```sh
git clone --recursive https://github.com/joshbartlettband-web/album-02-fm1
pip install Pillow numpy
python3 base/tools/build.py album --host                      # the simulator
album/build/host/album02_host album/tests/basic.txt out        # a test: WAVs and screenshots in out/
python3 album/tools/demo.py && album/build/host/album02_host album/tests/demo.txt out   # every patch, and a groove
python3 album/tools/listen.py out                             # the WAVs, measured
python3 base/tools/build.py album                             # the firmware (JieLi's toolchain: base/tools/get_toolchain.sh)
```

## Licence

MIT (`LICENSE`). Built on fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita, Hügelton Instruments (both
MPL-2.0, in `base/`). M-VAVE and FM-1 are trademarks of their owners; Album is not affiliated with M-VAVE.
