/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alive-Stable-7254 */
/* Album 02's patches: three a track, each to be told from the others in one bar. Times in seconds, cutoffs in Hz,
 * levels 0..1. */
#pragma once

#define NPATCH 3

/* ----------------------------------------------------------------- drums --- */
typedef struct {
    const char *name;
    float kick_f, kick_sweep, kick_pt, kick_dec, kick_drive, kick_click;   /* its resting pitch, how far above it starts
                                                                             * (times), the fall's time, the decay */
    float snare_f, snare_dec, snare_noise, snare_hp;   /* the body's pitch, the decay, noise against body, the noise's high-pass */
    float hat_tune, hat_dec, open_dec, hat_hp;
    float clap_dec, tom_f, tom_dec;
    float level;
} kit_t;

static const kit_t KITS[NPATCH] = {
    /* a dry early-80s box: short, woody, the snare mostly noise */
    {"BOX", 62.0f, 2.2f, 0.022f, 0.16f, 0.25f, 0.5f, 190.0f, 0.13f, 0.75f, 900.0f, 1.0f, 0.035f, 0.22f, 6500.0f,
     0.16f, 95.0f, 0.2f, 1.0f},
    /* a long low kick that hums a note, thin ticking hats, a small snare */
    {"BOOM", 47.0f, 1.6f, 0.05f, 0.62f, 0.1f, 0.15f, 172.0f, 0.2f, 0.45f, 1500.0f, 1.12f, 0.025f, 0.4f, 8000.0f,
     0.26f, 80.0f, 0.36f, 1.0f},
    /* a hard club kit: a punched, driven kick, a bright snare, long open hats */
    {"CLUB", 54.0f, 4.5f, 0.03f, 0.3f, 0.8f, 0.7f, 210.0f, 0.19f, 0.62f, 1900.0f, 0.94f, 0.05f, 0.55f, 5500.0f,
     0.21f, 110.0f, 0.28f, 0.95f},
};

/* ------------------------------------------------------------------ bass --- */
typedef struct {
    const char *name;
    float saw, pulse, sub;
    float cut_lo, cut_hi, res;           /* the low-pass at Tone 0 and Tone 1 (no key tracking) */
    float fenv, fdec;                    /* the filter's envelope: octaves above the cutoff at a note's start, its fall */
    float dec, sus, rel;                 /* the level's envelope (the attack is 2 ms) */
    float glide, drive;
} bass_t;

static const bass_t BASSES[NPATCH] = {
    {"PLUCK", 1.0f, 0.0f, 0.55f, 70.0f, 2600.0f, 0.58f, 3.2f, 0.11f, 0.22f, 0.25f, 0.05f, 0.0f, 0.2f},
    {"ACID", 1.0f, 0.0f, 0.0f, 110.0f, 3400.0f, 0.9f, 3.6f, 0.19f, 0.5f, 0.6f, 0.04f, 0.07f, 0.75f},
    {"ROUND", 0.15f, 0.5f, 1.0f, 55.0f, 800.0f, 0.08f, 0.9f, 0.3f, 0.6f, 0.85f, 0.09f, 0.02f, 0.05f},
};

/* ---------------------------------------------------------------- chords --- */
typedef struct {
    const char *name;
    float saw, pulse, sub, noise;
    float detune;                        /* cents between the saw and the pulse */
    float pwm;                           /* how far the pulse's width sways */
    float cut_lo, cut_hi, res;
    float keytrack;                      /* 1: the cutoff follows the key (cut_lo and cut_hi then multiply its pitch) */
    float fenv, fdec, hp;
    float att, dec, sus, rel;
    float chorus;                        /* how much of the chorus this patch has before the CHORUS knob adds more */
    float level;
} chord_t;

static const chord_t CHORDS[NPATCH] = {
    {"PAD", 0.8f, 0.25f, 0.3f, 0.0f, 9.0f, 0.3f, 260.0f, 6500.0f, 0.18f, 0.0f, 1.3f, 0.9f, 90.0f,
     0.03f, 0.9f, 0.7f, 0.55f, 0.8f, 1.0f},
    {"STAB", 0.75f, 0.6f, 0.0f, 0.0f, 14.0f, 0.0f, 180.0f, 5200.0f, 0.38f, 0.0f, 3.6f, 0.16f, 160.0f,
     0.002f, 0.22f, 0.0f, 0.1f, 0.35f, 1.15f},
    /* the filter rings on noise and whistles the note */
    {"WIND", 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 4.0f, 0.985f, 1.0f, 0.25f, 0.3f, 200.0f,
     0.25f, 1.0f, 0.85f, 0.9f, 1.0f, 0.9f},
};

/* ------------------------------------------------------------------ lead --- */
typedef struct {
    const char *name;
    float fm;                            /* 0: two detuned saws; 1: two-operator FM */
    float detune;                        /* cents between the saws */
    float cut_lo, cut_hi, res;
    float fenv, fatt, fdec;              /* the filter's envelope, with an attack of its own (brass) */
    float att, dec, sus, rel;
    float glide;
    float vib_rate, vib_cents, vib_delay;
    float fm_ratio, fm_index, fm_dec;
} lead_t;

static const lead_t LEADS[NPATCH] = {
    {"BRASS", 0.0f, 14.0f, 380.0f, 5200.0f, 0.14f, 2.2f, 0.06f, 0.35f, 0.012f, 0.3f, 0.8f, 0.12f, 0.03f,
     5.2f, 12.0f, 0.35f, 0.0f, 0.0f, 0.0f},
    {"GLASS", 1.0f, 0.0f, 3000.0f, 14000.0f, 0.0f, 0.0f, 0.001f, 0.1f, 0.001f, 1.3f, 0.0f, 0.9f, 0.0f,
     5.0f, 4.0f, 0.6f, 3.0f, 3.2f, 0.28f},
    {"FUNK", 0.0f, 6.0f, 500.0f, 6000.0f, 0.42f, 1.6f, 0.002f, 0.12f, 0.002f, 0.15f, 0.55f, 0.06f, 0.0f,
     7.2f, 55.0f, 0.07f, 0.0f, 0.0f, 0.0f},
};

/* ----------------------------------------------------------------- voice --- */
typedef struct {
    const char *name;
    float size;                          /* the formants' scale: above 1 a smaller head (a child), below a larger one */
    float octave;                        /* semitones added to every note */
    float narrow;                        /* 0: a soft glottal buzz; 1: a narrow pulse (a machine) */
    float bright;                        /* the buzz's low-pass (Hz) */
    float q;                             /* how sharp the formants ring */
    float vib_rate, vib_cents, vib_delay;
    float glide;
    float breath;
    float att, rel;
    float stepped;                       /* 1: no slide, no vibrato, the vowel in five steps */
} vox_t;

static const vox_t VOXES[NPATCH] = {
    {"CHOIR", 1.2f, 12.0f, 0.0f, 2600.0f, 9.0f, 5.6f, 16.0f, 0.3f, 0.06f, 0.16f, 0.05f, 0.18f, 0.0f},
    {"ROBOT", 1.0f, 0.0f, 1.0f, 6000.0f, 16.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.004f, 0.03f, 1.0f},
    {"MONK", 0.82f, -12.0f, 0.0f, 1700.0f, 11.0f, 4.4f, 30.0f, 0.5f, 0.16f, 0.05f, 0.09f, 0.3f, 0.0f},
};
