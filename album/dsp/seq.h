/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alive-Stable-7254 */
/* Album 02's sequencer, after the KeyStep Pro's: five tracks of 16 patterns, each up to 64 steps of up to four
 * notes, with a gate, a velocity, a shift in time and a chance a step; scenes (a pattern on each track) and a chain
 * of scenes for a song; the arpeggiator and chord mode on the FM-1's own keys; recording, live or a step at a time.
 *
 * It runs in the render, before the engine (seq_render, then eng_begin): everything that sounds passes through it,
 * so the keys, MIDI in and the patterns meet in one place and in one order. The main loop hands it the keys
 * through a queue and edits the patterns in place (a byte at a time: the render may read a step half edited, and
 * plays it as it then stands).
 *
 * Time is in ticks, 384 a beat (96 a sixteenth step); the clock is the FM-1's own, or MIDI clock while it comes
 * (24 a beat, each one 16 ticks, the time between filled in). The clock runs whether the patterns play or not, so
 * the arpeggiator keeps time when they are stopped. */
#pragma once
#include <stdint.h>

#define SEQ_TRACKS 5
#define SEQ_PATTERNS 16
#define SEQ_STEPS 64
#define SEQ_NOTES 4
#define SEQ_SCENES 16
#define SEQ_CHAIN 16
#define SEQ_PPQ 384
#define SEQ_STEP_TICKS (SEQ_PPQ / 4)
#define SEQ_BAR_TICKS (SEQ_PPQ * 4)

typedef struct {
    uint8_t n[SEQ_NOTES];                /* notes, 0 = none (n[0] 0: the step is empty) */
    uint8_t vel;                         /* 1..127 */
    uint8_t gate;                        /* eighths of a step, 1..255 (8: the whole step; above it ties over) */
    int8_t shift;                        /* 24ths of a step, -11..11 (early .. late) */
    uint8_t chance;                      /* percent, 1..100 */
} step_t;
typedef struct { step_t s[SEQ_STEPS]; } pattern_t;
typedef struct {
    uint8_t len;                         /* steps, 1..64 */
    uint8_t dir;                         /* SEQ_DIR_* */
    uint8_t swing;                       /* the off sixteenths late by this many 24ths of a step, 0..12 */
    uint8_t rsv;
} pat_hdr_t;
enum { SEQ_FWD, SEQ_BACK, SEQ_PINGPONG, SEQ_RANDOM, SEQ_NDIR };

enum { ARP_UP, ARP_DOWN, ARP_INCL, ARP_EXCL, ARP_RANDOM, ARP_ORDER, ARP_REPEAT, ARP_NMODE };
enum { ARP_R4, ARP_R8, ARP_R8T, ARP_R16, ARP_R16T, ARP_R32, ARP_NRATE };
typedef struct {
    uint8_t on, mode, rate, octaves;     /* octaves 1..4 */
    uint8_t gate;                        /* percent of the rate, 10..100 */
    uint8_t latch;
    uint8_t chord_on, chord_n;           /* chord mode: the shape's notes (chord_n of them), above the key */
    int8_t chord[SEQ_NOTES];             /* semitones above the lowest, chord[0] = 0 */
} play_t;

typedef struct { uint8_t scene, bars; } chain_t;

/* what a project holds of the sequencer (saved whole with it) */
typedef struct {
    pat_hdr_t hdr[SEQ_TRACKS][SEQ_PATTERNS];
    uint8_t cur[SEQ_TRACKS];             /* each track's pattern */
    uint8_t mute;                        /* bit t: track t's pattern is silent */
    uint8_t scene[SEQ_SCENES][SEQ_TRACKS];
    uint16_t scene_used;
    chain_t chain[SEQ_CHAIN];
    uint8_t chain_n, chain_on;
    play_t play[SEQ_TRACKS];
    uint8_t quant;                       /* recording: 1 onto the steps, 0 as played (the shift kept) */
    uint8_t rsv[3];
} seq_song_t;

extern pattern_t seq_pat[SEQ_TRACKS][SEQ_PATTERNS];
extern seq_song_t seq_song;

void seq_init(void);                     /* empty patterns, the defaults */
int seq_pat_empty(int track, int p);
void seq_pat_clear(int track, int p);
void seq_pat_copy(int track, int from, int to);
step_t seq_step_default(int note);

/* the render: the clock, the queued keys, the patterns, the arpeggiator; n frames */
void seq_render(uint32_t n);
void seq_midi(uint32_t status, uint32_t d1, uint32_t d2);   /* the render: MIDI in (notes go to track t's engine) */
void seq_midi_note(int track, int note, int vel);           /* the render: a note from MIDI in (vel 0: off) */

/* the main loop */
void seq_key(int track, int note, int vel);   /* a key of the FM-1's own (vel 0: let go): chord mode, arp, recording */
void seq_track(int track);               /* the track the keys and the recording are on */
void seq_play(int on);
int seq_playing(void);
void seq_rec(int on);
int seq_recording(void);
void seq_select(int track, int p);       /* a track's pattern: now when stopped, else at the next bar */
int seq_queued(int track);               /* the pattern it changes to at the bar, -1 none */
void seq_scene_store(int s);
void seq_scene_recall(int s);
int seq_scene_now(void);                 /* the last scene recalled, -1 none */
void seq_chain_add(int s);
void seq_chain_drop(void);
void seq_chain_bars(int d);
int seq_chain_pos(void);                 /* the chain's entry playing, -1 off */
void seq_mute(int track, int on);
void seq_panic(void);                    /* every note the sequencer and the arp hold, let go (play goes on) */
void seq_tempo(float bpm);
float seq_tempo_get(void);               /* the tempo heard: the FM-1's own, or MIDI clock's */
int seq_clock_ext(void);                 /* 1 while MIDI clock comes in */
int seq_step_now(int track);             /* the step of its pattern playing (or the step recording's cursor), -1 none */
int seq_cursor(void);                    /* recording a step at a time: the step it writes next */
void seq_cursor_move(int d);
uint32_t seq_beat_phase(void);           /* ticks into the beat, 0..383 */
uint32_t seq_bar(void);                  /* bars since play started */
void seq_midi_out(int mode);             /* 0 nothing, 1 notes, 2 notes and clock: out over USB */
