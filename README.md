# Album 02

The second in the Album series of firmwares for the M-VAVE FM-1 ([Album 01](https://github.com/joshbartlettband-web/album-fm1)).
Five tracks of filter-house: drums, bass, chords, lead and a singing voice, each patch with its own moving
picture in the style of 1970s space-opera anime. Played from its own keys or from a sequencer over MIDI.

- Five tracks, three patches each: DRUMS (BOX, BOOM, CLUB), BASS (PLUCK, ACID, ROUND), CHORDS (PAD, STAB, WIND),
  LEAD (BRASS, GLASS, FUNK), VOX (CHOIR, ROBOT, MONK), a formant singer
- Five knobs on every patch (TONE, MOTION, PUMP, SWEEP, SPACE) and four effects under EDIT (PHASER, CHORUS, GRIT,
  ECHO); everything but the drums pumps on the kick
- A picture for each patch, drawn by code each frame in the manner of 1970s space-opera animation; every knob and
  effect moves it
- A sequencer after the KeyStep Pro's: 16 patterns a track, up to 64 steps of up to four notes with gate,
  velocity, shift and chance; step mode on the keys; live and step recording; scenes and a chain of them for a
  song; the arpeggiator and chord mode
- Four projects, saved safe from a power cut
- MIDI in over USB and TRS, out over USB: a channel a track, clock in and out, the KeyStep Pro's knobs and program
  changes. No Bluetooth.
- Two cores: the second renders the chords, echo and reverb; one core takes over if it misbehaves
- The manual: [album/docs/MANUAL.md](album/docs/MANUAL.md); the design: [docs/PLAN.md](docs/PLAN.md)

**Status:** everything above runs in the simulator and its tests, and builds for the FM-1. It has not yet been
played on an FM-1: the patches' balance and the second core want a real unit.

## Build

```sh
git clone --recursive https://github.com/joshbartlettband-web/album-02-fm1
pip install Pillow numpy
python3 base/tools/build.py album --host                      # the simulator
album/build/host/album02_host album/tests/basic.txt out        # a test: WAVs and screenshots in out/
python3 album/tools/demo.py && album/build/host/album02_host album/tests/demo.txt out   # every patch, and a groove
python3 album/tools/listen.py out                             # the WAVs, measured
album/build/host/album02_host album/tests/scenes.txt out && python3 album/tools/montage.py out scenes.png   # the pictures
python3 base/tools/build.py album                             # the firmware (JieLi's toolchain: base/tools/get_toolchain.sh)
```

## Licence

MIT (`LICENSE`). Built on fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita, Hügelton Instruments (both
MPL-2.0, in `base/`). M-VAVE and FM-1 are trademarks of their owners; Album is not affiliated with M-VAVE.
