/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alive-Stable-7254 */
/* Album 02's sound engine: five tracks at once (drums, bass, chords, lead, a singing voice). Everything but the
 * drums passes through a bus that ducks on the kick (the pump), a one-knob filter (the sweep) and a phaser; then
 * grit, an echo in time, a reverb and a limiter on the whole mix.
 *
 * The render is four steps so that the FM-1's two cores can share it (as Album 01's):
 *   eng_begin      core 0: the queued notes and patch changes, the knobs, this block's filter settings
 *   eng_part_a     core 0: drums, bass, lead, voice
 *   eng_part_b     core 1 (or core 0 alone): the chord voices and their chorus; the echo and the reverb of the last
 *                  block's mix (a block late, 5.8 ms, less than either's first reflection)
 *   eng_end        core 0: the two halves summed, sweep, phaser, pump, grit, the wet added, the limiter, out
 * Between eng_begin and eng_end each core touches only its own voices and buffers; everything shared is written in
 * eng_begin or eng_end and read by both. */
#pragma once
#include <stdint.h>

#define ENG_FS 44100.0f
#define ENG_BLOCK_MAX 256u
#define ENG_SUB 16u                      /* samples between pitch and filter updates */
#define ENG_CHORD_VOICES 6u
#define ENG_PATCHES 3                    /* patches a track */
#define ENG_DRUM_PARTS 12u               /* a part a semitone: C kick, C# rim, D snare, D# clap, E snare 2, F low tom,
                                          * F# closed hat, G mid tom, G# pedal hat, A high tom, A# open hat, B cowbell */

enum { T_DRUMS, T_BASS, T_CHORDS, T_LEAD, T_VOX, NTRACK };
/* the five knobs (K1 to K4 and ALGORITHM): TONE and MOTION are each track's own, the rest the whole sound's; then the
 * four effects (EDIT held and K1 to K4) */
enum { K_TONE, K_MOTION, K_PUMP, K_SWEEP, K_SPACE, K_PHASER, K_CHORUS, K_GRIT, K_ECHO, NKNOB };

void eng_init(void);

/* from the main loop: queued, played at the next block's start */
void eng_note_on(int track, int note, int vel);
void eng_note_off(int track, int note);
void eng_patch(int track, int p);
void eng_panic(void);
/* from the render itself, before eng_begin (MIDI in, the sequencer): at once */
void eng_note_on_now(int track, int note, int vel);
void eng_note_off_now(int track, int note);

void eng_knob(int track, int k, float v);   /* 0..1, any context; track counts for K_TONE and K_MOTION only */
float eng_knob_get(int track, int k);
void eng_pressure(int track, float v);   /* the held key's pressure, 0..1 (the voice opens its mouth, the lead leans) */
void eng_tempo(float bpm);
float eng_tempo_get(void);

int eng_patch_count(int track);
int eng_patch_now(int track);
const char *eng_patch_name(int track, int p);

/* what the pictures follow */
float eng_level(int track);              /* each track's level, 0..~1, smoothed */
float eng_pulse(void);                   /* the kick, 1 at its hit, decaying */
float eng_duck(void);                    /* how far the pump holds the bus down now, 0..1 */
float eng_phase(int which);              /* 0 the phaser's sweep, 1 the chorus's: -1..1 */
float eng_vowel(void);                   /* the voice's vowel now, 0 (U) .. 1 (I) */
int eng_note_now(int track);             /* the newest note sounding on a track, -1 none */
uint32_t eng_voices_on(void);

void eng_begin(uint32_t n);
void eng_part_a(void);
void eng_part_b(void);
void eng_end(int32_t *lr, uint32_t n, float gain);
void eng_drop_b(void);                   /* core 1's half is silence this block (it was given up mid-job) */
