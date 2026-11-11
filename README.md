# Album 02

The second in the Album series of firmwares for the M-VAVE FM-1 ([Album 01](https://github.com/joshbartlettband-web/album-fm1)).
Five tracks of filter-house: drums, bass, chords, lead and a singing voice, each patch with its own moving
picture in the style of 1970s space-opera anime. Played from its own keys or from a sequencer over MIDI.

**Status: scaffold.** The repository builds fm1-base's template under Album 02's name, without Bluetooth. The
design is in [docs/PLAN.md](docs/PLAN.md).

## Build

```sh
git clone --recursive https://github.com/joshbartlettband-web/album-02-fm1
pip install Pillow numpy
python3 base/tools/build.py album --host                      # the simulator
album/build/host/album02_host album/tests/basic.txt out        # a test: WAVs and screenshots in out/
python3 base/tools/build.py album                             # the firmware (JieLi's toolchain: base/tools/get_toolchain.sh)
```

## Licence

MIT (`LICENSE`); `album/app.c` and `album/app.h` are still fm1-base's template (MPL-2.0) until Album 02's own
replace them. Built on fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita, Hügelton Instruments (both
MPL-2.0, in `base/`). M-VAVE and FM-1 are trademarks of their owners; Album is not affiliated with M-VAVE.
