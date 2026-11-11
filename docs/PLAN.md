# Album 02: the plan

Written 2026-11-11. The sound follows a 2001 French house record; the pictures follow its animated film; the
hardware manners follow Arturia's KeyStep Pro, which will also be plugged into it. Nothing of the record or the
film ships in the firmware: no samples, riffs, names or characters. The inspiration is in the synthesis and the
palette.

## What it is

Five tracks that play at once, each with a few patches that differ sharply from one another. The FM-1's keys play
the chosen track; a sequencer over MIDI plays any of them. Everything but the drums passes through a bus that
ducks on the kick, so a two-chord loop pumps like a record from the first bar.

| Track | Engine | Patches (working names) |
| --- | --- | --- |
| 1 Drums | Synthesised kick, snare, clap, hats, toms; 12 parts on the keys | a dry 1980s box kit; a boomy long-kick kit; a hard club kit |
| 2 Bass | Mono saw and sub, resonant 24 dB low-pass, no key tracking | short plucked filter bass; squelching acid line; round sub |
| 3 Chords | Six-voice poly: saw, pulse, sub, noise, one envelope, chorus | chorused pad; filtered stab; self-resonating noise sweep |
| 4 Lead | Mono: detuned saws, or two-operator FM | soft-attack brass; glassy harp and bell; fast-vibrato funk stab |
| 5 Vox | A singing voice: a glottal pulse through five vowel formants, with vibrato and slides | choirboy; robot (hard-stepped pitch, phased); low monk |

Fifteen patches. Each must be recognisable in one bar with the screen covered.

The voice is a formant singer after the plug-in Delay Lama: one knob walks through the vowels, key pressure opens
the mouth, and notes slide into each other.

## The pictures

Each patch has its own visualiser: a scene in the manner of 1970s space-opera animation (flat cel colours, deep
navy, magenta, cyan, gold) built from two layers.

- A painted backdrop, kept in flash as a 240 x 240 palettised picture, as Album 01 keeps its photos
  (`tools/photos.py` carries over).
- Moving parts drawn by code over it: stars, light trails, rings, beams, an equaliser skyline. These are what the
  sound moves.

Every knob and effect moves something the eye can name. Examples: Sweep closes like an iris; Pump dips the whole
scene's light on each kick; the chorus doubles the outlines; the phaser slides colour bands across; the grit effect
coarsens the pixels; the voice's vowel changes the shape of a waveform mouth. No dials, no numbers except a knob's
value for a moment while it turns, as in Album 01.

The screen shows the chosen track's patch. The other tracks still reach it: the kick pulses every scene.

Backdrops can be drawn with the Gemini image API, then cropped and palettised by the photo tool. On 2026-11-11 the
API refused with "prepayment credits are depleted", so no test picture has been made.

## Controls

Album 01's manners are kept wherever they fit.

| Control | What it does |
| --- | --- |
| Keys | Play the chosen track: notes, drum parts, or chords in chord mode. Velocity and pressure count. |
| PRESET | The next or previous patch on this track (and its picture) |
| SELECT | The track, 1 to 5 |
| K1 to K4, ALGORITHM | The five knobs, the same on every patch: Tone, Motion, Pump, Sweep, Space |
| EDIT held, K1 to K4 | The four effects: Phaser, Chorus, Grit, Echo |
| ARP | Chord mode and the arpeggiator |
| SEQ, PLAY, REC | The track's pattern: on or off, start and stop, record |
| HOME | This patch back where it starts |
| SAVE | Keep everything |
| OCT- / OCT+ | The octave |

The other buttons (FX, SCL, ENV, LFO, GLO) are free for mute, fill and scale; to be settled when the engine plays.

## MIDI and the KeyStep Pro

MIDI is required; Bluetooth is left out entirely (`ble = false`), which also frees memory and removes the radio's
start-up from the second core's troubles.

- The KeyStep Pro and the FM-1 are both USB devices, so they do not connect by USB without a host between them.
  The direct cable is KeyStep Pro MIDI OUT (5-pin) to the FM-1's TRS MIDI in (Type A).
- Each track listens on its own channel, set to match the KeyStep Pro's four tracks, with the drums also on 10.
  The voice takes a fifth channel and the FM-1's own keys, so it can be sung over four sequenced tracks.
- The FM-1 follows MIDI clock, start and stop: Pump, Echo and the patterns keep the KeyStep Pro's time.
- The KeyStep Pro's five knobs in control mode map to the five knobs here by controller number.
- USB MIDI in and out as in Album 01.

Because the KeyStep Pro does the sequencing, the FM-1's own sequencer stays small: one pattern a track, recorded
by playing, for use without it. No scenes, chains or step editing in the first version.

## Two cores

The engine is cut in two from the first line, as Album 01's is (`eng_begin`, `eng_voices_a`, `eng_voices_b`,
`eng_end`), so both cores always have work.

| | Core 0 | Core 1 |
| --- | --- | --- |
| Renders | Drums, bass, lead, voice; the pump; the final mix | The six chord voices; echo and reverb of the last block's mix |
| With one core | everything, with four chord voices and a shorter reverb | idle |

Album 01's second core starts on Josh's FM-1, then crashes within minutes on a single flipped bit read from RAM
(`album-fm1/album/docs/SECOND-CORE.md`). That is a fault of some units at 360 MHz, not of the code, and other
firmware authors report the same. Album 02 carries over `cpu1/` and the stock supply setup and takes that note's
next steps in order: test the supply build with no radio; keep core 1 idle while the screen's transfer runs; try
320 MHz. Two cores stay opt-in at boot until a 30-minute soak on the real unit shows no misreads, then become the
default with one core as the fallback.

## Effects

- On the music bus (everything but drums): Pump, Sweep (one knob, low-pass one way and high-pass the other),
  Phaser.
- On the whole mix: Chorus, Grit (12-bit and sample-rate reduction), Echo in time, a plate reverb (Space), a limiter.

## Order of work

1. Engine in the simulator: the five tracks, one patch each, the pump. Listen to rendered WAVs.
2. MIDI: channels, clock, the KeyStep Pro's controllers.
3. The other ten patches, each checked against the rule above.
4. The visualisers: the moving parts first, then backdrops once the image API has credit.
5. The second core on hardware.
6. The manual, the site, the Firmware Hub card.

Every push builds the firmware and the browser emulator on GitHub, so each step can be heard and installed from
a phone.
