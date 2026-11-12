/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alive-Stable-7254 */
/* Album 02's sequencer (seq.h has the plan). */
#include <stddef.h>
#ifdef PLAT_HOST
#include <string.h>
#else
void *memset(void *d, int c, unsigned n);   /* the device's own (felucca-bsp chip/libc.c) */
void *memcpy(void *d, const void *s, unsigned n);
#endif
#include "seq.h"
#include "engine.h"
#include "fastmath.h"
#include "plat.h"

pattern_t seq_pat[SEQ_TRACKS][SEQ_PATTERNS] PLAT_POOL;
seq_song_t seq_song;

/* ------------------------------------------------------------- the clock --- */
static int32_t tk;                       /* ticks: the clock now */
static float tk_fr;                      /* and the part of a tick */
static volatile float bpm = 123.0f;
static volatile uint8_t playing, recording, want_play, want_stop;
static int32_t play_at;                  /* the tick play started at: bars count from it */
static uint32_t ext_ticks;               /* MIDI clock: ticks counted since its start, and the samples between them */
static uint32_t ext_since, ext_period = 0;
static uint8_t ext_on;                   /* MIDI clock is coming */
static uint32_t smp;                     /* samples since the start: when each tick came */
static uint32_t tick_at[24];             /* the last ticks' times, for the tempo: their average gap */
static uint8_t tick_n, tick_w;
static float ext_bpm;
static uint8_t midi_out_mode = 1;
static uint32_t clk_out_acc;             /* clock out: ticks since the last one sent */
static uint32_t rng = 0x1234567u;

static inline uint32_t rnd(void)
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rng = x;
}
static inline int32_t fdiv(int32_t a, int32_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }   /* floor */
static inline uint32_t hash(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9E3779B1u ^ b * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    return h ^ (h >> 12);
}

/* ------------------------------------------------------ what is sounding --- */
/* every note the sequencer started (patterns, arpeggio), each with the tick it ends at */
#define HELD_N 48u
static struct { uint8_t on, track, note, rsv; int32_t off; } held[HELD_N];
static uint8_t cur_track = 2;

static void out_note(int track, int note, int vel)   /* to the engine, and out over USB */
{
    if (vel > 0)
        eng_note_on_now(track, note, vel);
    else
        eng_note_off_now(track, note);
    if (midi_out_mode) {
        uint32_t ch = (uint32_t)track, st = vel > 0 ? 0x90u : 0x80u;
        plat_midi_send((st >> 4) | (st | ch) << 8 | (uint32_t)note << 16 | (uint32_t)(vel > 0 ? vel : 0) << 24, TO_USB);
    }
}
static void held_add(int track, int note, int vel, int32_t off)
{
    uint32_t i, slot = HELD_N;
    for (i = 0; i < HELD_N; i++) {
        if (held[i].on && held[i].track == track && held[i].note == note) {   /* the same note again: it restarts */
            out_note(track, note, 0);
            slot = i;
            break;
        }
        if (!held[i].on && slot == HELD_N)
            slot = i;
    }
    if (slot == HELD_N) {                /* full: the one ending soonest goes now */
        int32_t soon = 0x7FFFFFFF;
        for (i = 0; i < HELD_N; i++)
            if (held[i].off < soon) {
                soon = held[i].off;
                slot = i;
            }
        out_note(held[slot].track, held[slot].note, 0);
    }
    held[slot].on = 1;
    held[slot].track = (uint8_t)track;
    held[slot].note = (uint8_t)note;
    held[slot].off = off;
    out_note(track, note, vel);
}
static void held_due(int32_t now)
{
    uint32_t i;
    for (i = 0; i < HELD_N; i++)
        if (held[i].on && held[i].off <= now) {
            held[i].on = 0;
            out_note(held[i].track, held[i].note, 0);
        }
}
static void held_all_off(int only_track)  /* -1: every track */
{
    uint32_t i;
    for (i = 0; i < HELD_N; i++)
        if (held[i].on && (only_track < 0 || held[i].track == only_track)) {
            held[i].on = 0;
            out_note(held[i].track, held[i].note, 0);
        }
}

/* ------------------------------------------------------------ patterns --- */
static int32_t origin[SEQ_TRACKS];       /* the step a track's pattern counts from */
static int8_t queued[SEQ_TRACKS];        /* the pattern it changes to at the next bar, -1 none */
static int8_t scene_now = -1, scene_q = -1;
static int8_t chain_pos = -1;
static uint8_t chain_left;
static int32_t last_bar;
static int16_t step_now[SEQ_TRACKS];

step_t seq_step_default(int note)
{
    step_t s;
    memset(&s, 0, sizeof s);
    s.n[0] = (uint8_t)note;
    s.vel = 100;
    s.gate = 6;
    s.chance = 100;
    return s;
}
int seq_pat_empty(int track, int p)
{
    uint32_t i;
    for (i = 0; i < SEQ_STEPS; i++)
        if (seq_pat[track][p].s[i].n[0])
            return 0;
    return 1;
}
void seq_pat_clear(int track, int p)
{
    memset(&seq_pat[track][p], 0, sizeof seq_pat[track][p]);
    seq_song.hdr[track][p].len = 16;
    seq_song.hdr[track][p].dir = SEQ_FWD;
    seq_song.hdr[track][p].swing = 0;
}
void seq_pat_copy(int track, int from, int to)
{
    if (from == to)
        return;
    seq_pat[track][to] = seq_pat[track][from];
    seq_song.hdr[track][to] = seq_song.hdr[track][from];
}

static int step_of(int track, int32_t g)  /* the step of a track's pattern that global step g plays, -1 none */
{
    const pat_hdr_t *h = &seq_song.hdr[track][seq_song.cur[track]];
    int32_t r = g - origin[track], L = h->len ? h->len : 16, m;
    if (r < 0)
        return -1;
    switch (h->dir) {
    case SEQ_BACK:
        return L - 1 - (int)(r % L);
    case SEQ_PINGPONG:
        if (L < 2)
            return 0;
        m = r % (2 * L - 2);
        return m < L ? (int)m : (int)(2 * L - 2 - m);
    case SEQ_RANDOM:
        return (int)(hash((uint32_t)r, (uint32_t)track * 31u + seq_song.cur[track]) % (uint32_t)L);
    default:
        return (int)(r % L);
    }
}
static int32_t fire_time(int track, int32_t g, const step_t *s)
{
    const pat_hdr_t *h = &seq_song.hdr[track][seq_song.cur[track]];
    return g * SEQ_STEP_TICKS + s->shift * 4 + ((g & 1) ? h->swing * 4 : 0);
}

/* live recording: a note played on the step it will next come round on must not play again as it passes now */
static struct { int8_t track; int16_t idx; int32_t g; } skip[8];
static uint8_t skip_w;

static void patterns_play(int32_t from, int32_t to)   /* every step that falls in [from, to) */
{
    int t;
    for (t = 0; t < SEQ_TRACKS; t++) {
        int32_t g, g0 = fdiv(from, SEQ_STEP_TICKS) - 1, g1 = fdiv(to - 1, SEQ_STEP_TICKS) + 1;
        pattern_t *p = &seq_pat[t][seq_song.cur[t]];
        int idx_now = step_of(t, fdiv(to - 1, SEQ_STEP_TICKS));
        step_now[t] = (int16_t)idx_now;
        if (seq_song.mute >> t & 1u)
            continue;
        for (g = g0; g <= g1; g++) {
            int idx = step_of(t, g), k;
            const step_t *s;
            int32_t at;
            uint32_t j;
            if (idx < 0)
                continue;
            s = &p->s[idx];
            if (!s->n[0])
                continue;
            at = fire_time(t, g, s);
            if (at < from || at >= to)
                continue;
            for (j = 0; j < 8u; j++)
                if (skip[j].track == t && skip[j].idx == idx && skip[j].g == g)
                    break;
            if (j < 8u)
                continue;
            if (s->chance < 100 && (int)(rnd() % 100u) >= s->chance)
                continue;
            for (k = 0; k < SEQ_NOTES && s->n[k]; k++)
                held_add(t, s->n[k], s->vel, at + (int32_t)(s->gate ? s->gate : 1) * (SEQ_STEP_TICKS / 8));
        }
    }
}

static void scene_apply(int s, int now)
{
    int t;
    if (s < 0 || s >= SEQ_SCENES || !(seq_song.scene_used >> s & 1u))
        return;
    for (t = 0; t < SEQ_TRACKS; t++) {
        if (now) {
            if (seq_song.cur[t] != seq_song.scene[s][t])
                held_all_off(t);
            seq_song.cur[t] = seq_song.scene[s][t];
            queued[t] = -1;
        } else {
            queued[t] = (int8_t)seq_song.scene[s][t];
        }
    }
    scene_now = (int8_t)s;
}

static void bar_line(int32_t bar_tick)   /* a bar starts: the chain moves on, the queued patterns start */
{
    int t;
    int32_t g = bar_tick / SEQ_STEP_TICKS;
    if (seq_song.chain_on && seq_song.chain_n) {
        if (chain_left > 1u) {
            chain_left--;
        } else {
            chain_pos = (int8_t)((chain_pos + 1) % seq_song.chain_n);
            chain_left = seq_song.chain[chain_pos].bars ? seq_song.chain[chain_pos].bars : 1;
            scene_apply(seq_song.chain[chain_pos].scene, 0);
        }
    }
    if (scene_q >= 0) {
        scene_apply(scene_q, 0);
        scene_q = -1;
    }
    for (t = 0; t < SEQ_TRACKS; t++)
        if (queued[t] >= 0) {
            if (seq_song.cur[t] != queued[t])
                held_all_off(t);
            seq_song.cur[t] = (uint8_t)queued[t];
            queued[t] = -1;
            origin[t] = g;
        }
}

/* ------------------------------------------------- the keys: chords, arp --- */
#define KQ 64u
static struct { uint8_t track, note, vel, rsv; } kq[KQ];
static volatile uint32_t kq_head, kq_tail;

typedef struct {
    uint8_t n;                           /* notes held for the arpeggio, in the order played */
    uint8_t note[16];
    uint8_t vel;
    uint8_t keys;                        /* keys still down (latch: a new press after all were up starts afresh) */
    int16_t pos;                         /* where the arpeggio is in its run */
    int16_t rsv;                         /* up and down: the place in the cycle */
    int8_t dirn;
    int32_t next;                        /* the tick its next note is due */
} arp_t;
static arp_t arp[SEQ_TRACKS];
static uint8_t chord_of[SEQ_TRACKS][128][SEQ_NOTES] PLAT_POOL;   /* what each key started, to let the same go */
static const uint16_t ARP_TICKS[ARP_NRATE] = {384, 192, 128, 96, 64, 48};

/* recording */
static struct { uint8_t on, track, note; int16_t idx; int32_t at; } rec_held[16];
static int16_t cursor;                   /* recording a step at a time: the step it writes */
static uint8_t cursor_keys;              /* keys down since the step was started */
static uint8_t cursor_wrote;

static void rec_on(int track, int note, int vel)
{
    pattern_t *p;
    pat_hdr_t *h;
    step_t *s;
    int idx, k;
    int32_t g, sh;
    uint32_t i;
    if (!recording || track != cur_track || track < 0 || track >= SEQ_TRACKS)
        return;
    p = &seq_pat[track][seq_song.cur[track]];
    h = &seq_song.hdr[track][seq_song.cur[track]];
    if (!h->len)
        h->len = 16;
    if (playing) {
        g = fdiv(tk + SEQ_STEP_TICKS / 2, SEQ_STEP_TICKS);
        sh = seq_song.quant ? 0 : (tk - g * SEQ_STEP_TICKS - ((g & 1) ? h->swing * 4 : 0)) / 4;
        idx = (int)((g - origin[track]) % h->len);
        if (idx < 0)
            idx += h->len;
        if (g * SEQ_STEP_TICKS + sh * 4 > tk) {   /* the step is still to come: it is not to play this time round */
            skip[skip_w].track = (int8_t)track;
            skip[skip_w].idx = (int16_t)idx;
            skip[skip_w].g = g;
            skip_w = (uint8_t)((skip_w + 1u) & 7u);
        }
    } else {
        idx = cursor % h->len;
        sh = 0;
        if (!cursor_keys && !cursor_wrote) /* a new step: what it held before goes */
            memset(&p->s[idx], 0, sizeof p->s[idx]);
        cursor_keys++;
        cursor_wrote = 1;
    }
    s = &p->s[idx];
    if (!s->n[0]) {
        *s = seq_step_default(note);
        s->vel = (uint8_t)vel;
        s->shift = (int8_t)fm_clampf((float)sh, -11.0f, 11.0f);
    } else {
        for (k = 0; k < SEQ_NOTES && s->n[k] && s->n[k] != note; k++) {
        }
        if (k == SEQ_NOTES)
            k = SEQ_NOTES - 1;
        s->n[k] = (uint8_t)note;
        if (vel > s->vel)
            s->vel = (uint8_t)vel;
    }
    for (i = 0; i < 16u; i++)
        if (!rec_held[i].on) {
            rec_held[i].on = 1;
            rec_held[i].track = (uint8_t)track;
            rec_held[i].note = (uint8_t)note;
            rec_held[i].idx = (int16_t)idx;
            rec_held[i].at = tk;
            break;
        }
}
static void rec_off(int track, int note)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (rec_held[i].on && rec_held[i].track == track && rec_held[i].note == note) {
            rec_held[i].on = 0;
            if (playing) {
                step_t *s = &seq_pat[track][seq_song.cur[track]].s[rec_held[i].idx];
                int32_t len = (tk - rec_held[i].at) / (SEQ_STEP_TICKS / 8);
                s->gate = (uint8_t)(len < 1 ? 1 : len > 255 ? 255 : len);
            }
        }
    if (!playing && recording && track == cur_track && cursor_keys) {
        if (--cursor_keys == 0) {        /* every key up: the next step */
            const pat_hdr_t *h = &seq_song.hdr[track][seq_song.cur[track]];
            cursor = (int16_t)((cursor + 1) % (h->len ? h->len : 16));
            cursor_wrote = 0;
        }
    }
}

static void live(int track, int note, int vel)   /* a note played live (chord mode and the arp already applied) */
{
    out_note(track, note, vel);
    if (vel > 0)
        rec_on(track, note, vel);
    else
        rec_off(track, note);
}

static void arp_note(arp_t *a, int track, int note, int on)
{
    uint32_t i, j;
    play_t *pl = &seq_song.play[track];
    if (on) {
        if (pl->latch && a->keys == 0) {  /* latched: a press after every key was up starts a new set */
            held_all_off(track);
            a->n = 0;
        }
        a->keys++;
        for (i = 0; i < a->n; i++)
            if (a->note[i] == note)
                return;
        if (a->n < 16u)
            a->note[a->n++] = (uint8_t)note;
        if (a->n == 1u) {                /* the first note: on the next beat part, from the start */
            int32_t r = ARP_TICKS[pl->rate % ARP_NRATE];
            a->next = fdiv(tk, r) * r + r;
            if (tk - fdiv(tk, r) * r < r / 4)   /* played just after a line: play now */
                a->next = tk;
            a->pos = -1;
            a->rsv = -1;
            a->dirn = 1;
        }
    } else {
        if (a->keys)
            a->keys--;
        if (pl->latch)
            return;
        for (i = j = 0; i < a->n; i++)
            if (a->note[i] != note)
                a->note[j++] = a->note[i];
        a->n = (uint8_t)j;
    }
}

static void arp_run(int track, int32_t from, int32_t to)
{
    arp_t *a = &arp[track];
    play_t *pl = &seq_song.play[track];
    int32_t r = ARP_TICKS[pl->rate % ARP_NRATE];
    (void)from;
    if (!pl->on || !a->n)
        return;
    while (a->next < to) {
        uint8_t sorted[16], run[64];
        uint32_t i, j, n = a->n, oct = pl->octaves ? pl->octaves : 1, len = 0;
        int32_t at = a->next, gate = r * (pl->gate ? pl->gate : 50) / 100;
        a->next += r;
        if (gate < 8)
            gate = 8;
        if (pl->mode == ARP_REPEAT) {    /* the whole chord again, each time */
            for (i = 0; i < n; i++)
                held_add(track, a->note[i], a->vel ? a->vel : 100, at + gate);
            continue;
        }
        for (i = 0; i < n; i++)
            sorted[i] = a->note[i];
        if (pl->mode != ARP_ORDER)       /* lowest first */
            for (i = 1; i < n; i++)
                for (j = i; j > 0 && sorted[j - 1] > sorted[j]; j--) {
                    uint8_t x = sorted[j];
                    sorted[j] = sorted[j - 1];
                    sorted[j - 1] = x;
                }
        for (j = 0; j < oct; j++)
            for (i = 0; i < n && len < 64u; i++)
                run[len++] = (uint8_t)clamp(sorted[i] + 12 * (int32_t)j, 0, 127);
        {
            int32_t p = a->pos, L = (int32_t)len;
            switch (pl->mode) {
            case ARP_DOWN:
                p = p < 0 ? L - 1 : (p - 1 + L) % L;
                break;
            case ARP_INCL:               /* up and down, the ends played twice: 0 1 2 2 1 0 */
            case ARP_EXCL: {             /* up and down, the ends once: 0 1 2 1 */
                int32_t c = pl->mode == ARP_INCL ? 2 * L : (L > 1 ? 2 * L - 2 : 1), q;
                a->rsv = (int16_t)((a->rsv + 1) % c);   /* the place in the up-and-down cycle */
                q = a->rsv;
                if (pl->mode == ARP_INCL)
                    p = q < L ? q : 2 * L - 1 - q;
                else
                    p = q < L ? q : 2 * L - 2 - q;
                break;
            }
            case ARP_RANDOM:
                p = (int32_t)(rnd() % (uint32_t)L);
                break;
            default:                     /* up, as played */
                p = (p + 1) % L;
                break;
            }
            a->pos = (int16_t)p;
            held_add(track, run[p], a->vel ? a->vel : 100, at + gate);
            rec_on(track, run[p], a->vel ? a->vel : 100);
            rec_off(track, run[p]);
        }
    }
}

static void key_in(int track, int note, int vel)
{
    play_t *pl = &seq_song.play[track];
    uint8_t *ex = chord_of[track][note & 127];
    int k, n;
    if (vel > 0) {                       /* the notes this key plays: one, or its chord */
        n = pl->chord_on && pl->chord_n ? pl->chord_n : 1;
        for (k = 0; k < SEQ_NOTES; k++)
            ex[k] = k < n ? (uint8_t)clamp(note + (k ? pl->chord[k] : 0), 1, 127) : 0;
    }
    for (k = 0; k < SEQ_NOTES && ex[k]; k++) {
        if (pl->on) {
            arp[track].vel = vel > 0 ? (uint8_t)vel : arp[track].vel;
            arp_note(&arp[track], track, ex[k], vel > 0);
        } else {
            live(track, ex[k], vel);
        }
    }
    if (vel <= 0)
        memset(ex, 0, SEQ_NOTES);
}

/* --------------------------------------------------------------- render --- */
void seq_render(uint32_t n)
{
    int32_t from = tk, to, t;
    float adv;
    while (kq_tail != kq_head) {         /* the keys first */
        uint32_t i = kq_tail % KQ;
        key_in(kq[i].track, kq[i].note, kq[i].vel);
        kq_tail++;
    }
    if (want_stop) {
        want_stop = 0;
        playing = 0;
        held_all_off(-1);
        if (midi_out_mode == 2u)
            plat_midi_send(0x0Fu | 0xFCu << 8, TO_USB);
    }
    if (want_play) {                     /* from the start of a bar, now */
        want_play = 0;
        tk = from = 0;
        tk_fr = 0.0f;
        play_at = 0;
        last_bar = -1;
        for (t = 0; t < SEQ_TRACKS; t++) {
            origin[t] = 0;
            if (queued[t] >= 0) {
                seq_song.cur[t] = (uint8_t)queued[t];
                queued[t] = -1;
            }
        }
        chain_pos = -1;
        chain_left = 0;
        for (t = 0; t < 8; t++)
            skip[t].track = -1;
        playing = 1;
        if (midi_out_mode == 2u)
            plat_midi_send(0x0Fu | 0xFAu << 8, TO_USB);
        clk_out_acc = 0;
    }
    /* the clock: its own, or MIDI clock's ticks with the time between filled in at its rate */
    smp += n;
    if (ext_on) {
        uint32_t limit = ext_ticks * 16u + 15u;
        ext_since += n;
        if (ext_since > (uint32_t)(ENG_FS * 0.5f)) {   /* no clock for half a second: it has gone */
            ext_on = 0;
        } else {
            float per = ext_period ? (float)ext_period / 16.0f : 1e9f;
            float want = (float)(ext_ticks * 16u) + fm_minf((float)ext_since / per, 15.99f);
            float now = (float)tk + tk_fr;
            if (want > now) {
                tk = (int32_t)want;
                tk_fr = want - (float)tk;
            }
            if ((uint32_t)tk > limit && playing)
                tk = (int32_t)limit;
        }
    }
    if (!ext_on) {
        adv = bpm * (float)SEQ_PPQ / 60.0f * (float)n / ENG_FS + tk_fr;
        tk += (int32_t)adv;
        tk_fr = adv - (float)(int32_t)adv;
        if (playing && midi_out_mode == 2u) {   /* clock out: 24 a beat */
            clk_out_acc += (uint32_t)(tk - from);
            while (clk_out_acc >= 16u) {
                clk_out_acc -= 16u;
                plat_midi_send(0x0Fu | 0xF8u << 8, TO_USB);
            }
        }
    }
    to = tk;
    if (playing) {
        int32_t bar = fdiv(to - 1 - play_at, SEQ_BAR_TICKS);
        if (bar > last_bar) {            /* a bar line in this block: the changes it brings, before its steps */
            if (last_bar < 0 && seq_song.chain_on && seq_song.chain_n) {
                chain_pos = 0;
                chain_left = seq_song.chain[0].bars ? seq_song.chain[0].bars : 1;
                scene_apply(seq_song.chain[0].scene, 1);
            } else if (last_bar >= 0) {
                bar_line(play_at + bar * SEQ_BAR_TICKS);
            }
            last_bar = bar;
        }
        patterns_play(from, to);
    } else {
        for (t = 0; t < SEQ_TRACKS; t++)
            step_now[t] = -1;
    }
    for (t = 0; t < SEQ_TRACKS; t++)
        arp_run(t, from, to);
    held_due(to);
    eng_tempo(seq_tempo_get());
}

void seq_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    (void)d1;
    (void)d2;
    switch (status) {
    case 0xF8u:
        if (!ext_on) {                   /* the first tick: MIDI clock takes over, in step with it */
            ext_on = 1;
            ext_ticks = (uint32_t)(tk / 16);
            ext_period = 0;
            tick_n = 0;
        } else {
            ext_ticks++;
        }
        /* the tempo: the average gap over the last 24 ticks (each known to a block: averaged, exact enough) */
        if (tick_n) {
            uint32_t first = tick_at[(tick_w + 24u - tick_n) % 24u];
            ext_period = (smp - first) / tick_n;
            if (ext_period)
                ext_bpm = ENG_FS * 60.0f / ((float)ext_period * 24.0f);
        }
        tick_at[tick_w] = smp;
        tick_w = (uint8_t)((tick_w + 1u) % 24u);
        if (tick_n < 23u)
            tick_n++;
        ext_since = 0;
        break;
    case 0xFAu:                          /* start */
        ext_ticks = 0;
        ext_since = 0;
        want_play = 1;
        if (ext_on)
            tk = 0;
        break;
    case 0xFBu:                          /* continue */
        playing = 1;
        break;
    case 0xFCu:
        want_stop = 1;
        break;
    default:
        break;
    }
}
void seq_midi_note(int track, int note, int vel)
{
    if (track < 0 || track >= SEQ_TRACKS || note < 0 || note > 127)
        return;
    if (vel > 0) {
        eng_note_on_now(track, note, vel);
        rec_on(track, note, vel);
    } else {
        eng_note_off_now(track, note);
        rec_off(track, note);
    }
}

/* ------------------------------------------------------------ main loop --- */
void seq_key(int track, int note, int vel)
{
    if (kq_head - kq_tail >= KQ || track < 0 || track >= SEQ_TRACKS)
        return;
    kq[kq_head % KQ].track = (uint8_t)track;
    kq[kq_head % KQ].note = (uint8_t)note;
    kq[kq_head % KQ].vel = (uint8_t)(vel < 0 ? 0 : vel > 127 ? 127 : vel);
    kq_head++;
}
void seq_track(int track)
{
    if (track >= 0 && track < SEQ_TRACKS)
        cur_track = (uint8_t)track;
}
void seq_play(int on)
{
    if (on)
        want_play = 1;
    else
        want_stop = 1;
}
int seq_playing(void) { return playing || want_play; }
void seq_rec(int on)
{
    recording = (uint8_t)(on != 0);
    cursor_keys = 0;
    cursor_wrote = 0;
}
int seq_recording(void) { return recording; }
void seq_select(int track, int p)
{
    if (track < 0 || track >= SEQ_TRACKS || p < 0 || p >= SEQ_PATTERNS)
        return;
    if (playing)
        queued[track] = (int8_t)p;
    else {
        seq_song.cur[track] = (uint8_t)p;
        queued[track] = -1;
    }
    cursor = 0;
}
int seq_queued(int track) { return track >= 0 && track < SEQ_TRACKS ? queued[track] : -1; }
void seq_scene_store(int s)
{
    int t;
    if (s < 0 || s >= SEQ_SCENES)
        return;
    for (t = 0; t < SEQ_TRACKS; t++)
        seq_song.scene[s][t] = queued[t] >= 0 ? (uint8_t)queued[t] : seq_song.cur[t];
    seq_song.scene_used |= (uint16_t)(1u << s);
    scene_now = (int8_t)s;
}
void seq_scene_recall(int s)
{
    if (s < 0 || s >= SEQ_SCENES || !(seq_song.scene_used >> s & 1u))
        return;
    if (playing)
        scene_q = (int8_t)s;
    else
        scene_apply(s, 1);
    scene_now = (int8_t)s;
}
int seq_scene_now(void) { return scene_now; }
void seq_chain_add(int s)
{
    if (s < 0 || s >= SEQ_SCENES || seq_song.chain_n >= SEQ_CHAIN)
        return;
    seq_song.chain[seq_song.chain_n].scene = (uint8_t)s;
    seq_song.chain[seq_song.chain_n].bars = 1;
    seq_song.chain_n++;
}
void seq_chain_drop(void)
{
    if (seq_song.chain_n)
        seq_song.chain_n--;
    if (!seq_song.chain_n)
        seq_song.chain_on = 0;
}
void seq_chain_bars(int d)
{
    chain_t *c;
    if (!seq_song.chain_n)
        return;
    c = &seq_song.chain[seq_song.chain_n - 1u];
    c->bars = (uint8_t)clamp((int32_t)c->bars + d, 1, 64);
}
int seq_chain_pos(void) { return playing && seq_song.chain_on ? chain_pos : -1; }
void seq_mute(int track, int on)
{
    if (track < 0 || track >= SEQ_TRACKS)
        return;
    if (on)
        seq_song.mute |= (uint8_t)(1u << track);
    else
        seq_song.mute &= (uint8_t)~(1u << track);
}
void seq_panic(void)
{
    want_stop = playing;
    memset(arp, 0, sizeof arp);
}
void seq_tempo(float b) { bpm = fm_clampf(b, 40.0f, 240.0f); }
float seq_tempo_get(void) { return ext_on && ext_bpm > 20.0f ? ext_bpm : bpm; }
int seq_clock_ext(void) { return ext_on; }
int seq_step_now(int track)
{
    if (track < 0 || track >= SEQ_TRACKS)
        return -1;
    if (!playing && recording && track == cur_track)
        return cursor;
    return playing ? step_now[track] : -1;
}
int seq_cursor(void) { return cursor; }
void seq_cursor_move(int d)
{
    const pat_hdr_t *h = &seq_song.hdr[cur_track][seq_song.cur[cur_track]];
    int32_t L = h->len ? h->len : 16;
    cursor = (int16_t)(((cursor + d) % L + L) % L);
    cursor_keys = 0;
    cursor_wrote = 0;
}
uint32_t seq_beat_phase(void) { return (uint32_t)(tk % SEQ_PPQ); }
uint32_t seq_bar(void) { return playing ? (uint32_t)fdiv(tk - play_at, SEQ_BAR_TICKS) : 0u; }
void seq_midi_out(int mode) { midi_out_mode = (uint8_t)(mode < 0 ? 0 : mode > 2 ? 2 : mode); }

void seq_init(void)
{
    int t, p;
    memset(&seq_song, 0, sizeof seq_song);
    for (t = 0; t < SEQ_TRACKS; t++) {
        for (p = 0; p < SEQ_PATTERNS; p++)
            seq_pat_clear(t, p);
        queued[t] = -1;
        step_now[t] = -1;
        seq_song.play[t].mode = ARP_UP;
        seq_song.play[t].rate = ARP_R16;
        seq_song.play[t].octaves = 1;
        seq_song.play[t].gate = 50;
    }
    seq_song.quant = 1;
    for (t = 0; t < 8; t++)
        skip[t].track = -1;
}
