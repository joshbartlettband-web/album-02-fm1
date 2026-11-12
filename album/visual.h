/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alive-Stable-7254 */
/* Album 02's pictures: a scene for each patch, drawn by code alone each frame in the manner of 1970s space-opera
 * animation (flat colours, deep navy, magenta, cyan, gold), and moved by the sound. Part of app.c (the unity build).
 *
 * What every scene shares, so each effect looks like itself on all fifteen:
 *   PUMP     the whole picture's light dips at each kick
 *   SWEEP    an iris: down from the middle it closes from the edges, up from it a hole opens in the centre
 *   PHASER   bands of colour slide across
 *   CHORUS   the outlines doubled, apart in two colours
 *   GRIT     the pixels coarsen
 *   ECHO     the moving parts trail copies of themselves, fading
 *   SPACE    the stars' trails lengthen and glow
 * TONE and MOTION are each scene's own: what a patch's brightness and movement look like there. */
#pragma once
#include <stdint.h>

#define VIS_STRIP 48                     /* rows drawn at a time: the screen in five strips */

typedef struct {
    int track, patch;
    float level[5], pulse, duck, phaser, chorus, vowel;
    float tone, motion, pump, sweep, space, ph_amt, ch_amt, grit, echo;
    int note;                            /* the newest note on the track, -1 none */
    float pitch;                         /* that note, 0 (low) .. 1 (high), held after it ends */
    float beat;                          /* 0..1 through the beat */
    float t;                             /* seconds */
    float run[4];                        /* the scene's own clocks: each advances at a speed the sound sets */
    int held, playing, step;
} vis_state_t;

void vis_init(void);
void vis_gather(vis_state_t *v, int track, int patch);
void vis_draw(const vis_state_t *v, int32_t y0);   /* rows y0 .. y0 + VIS_STRIP into the canvas */
uint16_t vis_track_rgb(int track);
