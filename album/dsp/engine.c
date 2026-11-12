/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alive-Stable-7254 */
/* Album 02's sound engine (engine.h has the plan). Single precision throughout, no libm: fm1-base's fastmath.h, so
 * the host simulator computes the same samples as the FM-1. */
#include "engine.h"
#include "fastmath.h"
#include "plat.h"
#include "patches.h"

/* ------------------------------------------------------------------ tools --- */
#define SINE_N 1024u
static float sine_tab[SINE_N + 1u] PLAT_POOL;

static inline float sine01(float ph)     /* sin(2 pi ph), any ph >= 0 */
{
    float x = ph * (float)SINE_N;
    int32_t i = (int32_t)x;
    float f = x - (float)i;
    i &= (int32_t)(SINE_N - 1u);
    return sine_tab[i] + (sine_tab[i + 1] - sine_tab[i]) * f;
}
static inline uint32_t rnd(uint32_t *s)  /* xorshift32 */
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
static inline float noise(uint32_t *s) { return (float)(int32_t)rnd(s) * (1.0f / 2147483648.0f); }   /* -1..1 */

static inline float blep(float t, float dt)   /* polyBLEP: the step's correction near a wrap */
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline float osc_saw(float *ph, float dt)
{
    float p = *ph + dt, s;
    if (p >= 1.0f)
        p -= 1.0f;
    *ph = p;
    s = 2.0f * p - 1.0f;
    return s - blep(p, dt);
}
static inline float osc_pulse(float *ph, float dt, float w)
{
    float p = *ph + dt, s, q;
    if (p >= 1.0f)
        p -= 1.0f;
    *ph = p;
    s = p < w ? 1.0f : -1.0f;
    s += blep(p, dt);
    q = p - w;
    if (q < 0.0f)
        q += 1.0f;
    return s - blep(q, dt);
}
static inline float soft(float x)        /* a soft clip: +-1.5 in, +-1 out */
{
    x = fm_clampf(x, -1.5f, 1.5f);
    return x - x * x * x * (4.0f / 27.0f);
}
static inline float tcoef(float t)       /* the per-sample factor of a decay with time constant t (s) */
{
    return t <= 0.0f ? 0.0f : fm_expf(-1.0f / (t * ENG_FS));
}
static inline float note_hz(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)); }
static inline float lp_g(float hz)       /* a one-pole low-pass's coefficient */
{
    return 1.0f - fm_expf(-6.2831853f * fm_clampf(hz, 10.0f, 15000.0f) / ENG_FS);
}

/* a four-pole low-pass that rings (the ladder): g from lp_g, k 0..~4.2 */
typedef struct { float y[4]; } lad_t;
static inline float ladder(lad_t *f, float x, float g, float k)
{
    float in = soft(x - k * f->y[3]);
    f->y[0] += g * (in - f->y[0]);
    f->y[1] += g * (f->y[0] - f->y[1]);
    f->y[2] += g * (f->y[1] - f->y[2]);
    f->y[3] += g * (f->y[2] - f->y[3]);
    return f->y[3];
}

/* a mono track's held notes: the newest sounds, and the one before it comes back when it is let go */
typedef struct { uint8_t n, note[8], vel[8]; } stack_t;
static void stack_del(stack_t *s, int note)
{
    uint32_t i, j = 0;
    for (i = 0; i < s->n; i++)
        if (s->note[i] != note) {
            s->note[j] = s->note[i];
            s->vel[j++] = s->vel[i];
        }
    s->n = (uint8_t)j;
}
static void stack_add(stack_t *s, int note, int vel)
{
    uint32_t i;
    stack_del(s, note);
    if (s->n == 8u) {
        for (i = 1; i < 8u; i++) {
            s->note[i - 1u] = s->note[i];
            s->vel[i - 1u] = s->vel[i];
        }
        s->n = 7;
    }
    s->note[s->n] = (uint8_t)note;
    s->vel[s->n++] = (uint8_t)vel;
}

/* ------------------------------------------------------------------ state --- */
typedef struct { uint8_t type, track, a, b; } ev_t;
enum { EV_ON, EV_OFF, EV_PATCH, EV_PANIC };
#define EVQ 64u
static ev_t evq[EVQ];
static volatile uint32_t ev_head, ev_tail;   /* the main loop writes head, the render tail */

static volatile float knob_tm[NTRACK][2];    /* TONE and MOTION, a track each */
static volatile float knob_g[NKNOB];         /* the rest (K_PUMP ..) */
static volatile float pressure[NTRACK];
static volatile float tempo_bpm = 120.0f;
static uint8_t patch_now[NTRACK];
static volatile float meter[NTRACK], v_pulse, v_duck, v_phaser, v_chorus, v_vowel;
static uint32_t blk_n;
static uint32_t seed_a = 0x2545F491u, seed_b = 0x9E3779B9u;   /* each core its own noise */

/* the block's buffers: part A's, part B's, and the send that eng_end leaves for part B's next block */
static float a_l[ENG_BLOCK_MAX] PLAT_POOL, a_r[ENG_BLOCK_MAX] PLAT_POOL;     /* bass, lead, voice */
static float d_l[ENG_BLOCK_MAX] PLAT_POOL, d_r[ENG_BLOCK_MAX] PLAT_POOL;     /* drums */
static float c_l[ENG_BLOCK_MAX] PLAT_POOL, c_r[ENG_BLOCK_MAX] PLAT_POOL;     /* chords */
static float w_l[ENG_BLOCK_MAX] PLAT_POOL, w_r[ENG_BLOCK_MAX] PLAT_POOL;     /* echo and reverb */
static float s_l[ENG_BLOCK_MAX] PLAT_POOL, s_r[ENG_BLOCK_MAX] PLAT_POOL;     /* the send */
static float mono[ENG_BLOCK_MAX] PLAT_POOL;                                  /* part A's scratch */
static float cmono[ENG_BLOCK_MAX] PLAT_POOL;                                 /* part B's */

/* ------------------------------------------------------------------ drums --- */
enum { D_KICK, D_RIM, D_SNARE, D_CLAP, D_SNARE2, D_TOM_L, D_HAT, D_TOM_M, D_HAT_P, D_TOM_H, D_HAT_O, D_BELL };
typedef struct {
    uint8_t on;
    uint32_t t;
    float vel, amp, ca, pe, cp, ph, ph2, f, a2, c2, lp, lp2;
} drum_t;
static drum_t drum[ENG_DRUM_PARTS];
static float hat_ph[6], drum_lp_l, drum_lp_r;
static uint8_t kick_hit;                 /* a kick started in this block: the pump */
static const float HAT_F[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
static const float DRUM_PAN[ENG_DRUM_PARTS] = {0.5f, 0.38f, 0.5f, 0.52f, 0.5f, 0.3f, 0.62f, 0.5f, 0.62f, 0.7f, 0.62f, 0.34f};

static void drum_hit(int part, int vel)
{
    const kit_t *k = &KITS[patch_now[T_DRUMS]];
    float len = 0.6f + 0.8f * knob_tm[T_DRUMS][1];   /* MOTION: every decay shorter or longer */
    drum_t *d;
    if (part == D_HAT_P || part == D_HAT_O) {         /* the three hats are one voice: each cuts the others off */
        d = &drum[D_HAT];
        d->ca = tcoef((part == D_HAT_O ? k->open_dec : k->hat_dec * 0.6f) * len);
    } else {
        d = &drum[part];
    }
    d->on = 1;
    d->t = 0;
    d->vel = (float)vel * (1.0f / 127.0f);
    d->vel = 0.35f + 0.65f * d->vel * d->vel;
    d->amp = 1.0f;
    d->pe = 1.0f;
    switch (part) {
    case D_KICK:
        d->f = k->kick_f;
        d->ca = tcoef(k->kick_dec * len);
        d->cp = tcoef(k->kick_pt);
        d->ph = 0.0f;
        kick_hit = 1;
        break;
    case D_TOM_L: case D_TOM_M: case D_TOM_H:
        d->f = k->tom_f * (part == D_TOM_L ? 1.0f : part == D_TOM_M ? 1.5f : 2.1f);
        d->ca = tcoef(k->tom_dec * len);
        d->cp = tcoef(0.04f);
        d->ph = 0.0f;
        break;
    case D_SNARE: case D_SNARE2:
        d->f = k->snare_f * (part == D_SNARE2 ? 1.25f : 1.0f);
        d->ca = tcoef(k->snare_dec * len * (part == D_SNARE2 ? 0.55f : 1.0f));
        d->a2 = 1.0f;
        d->c2 = tcoef(k->snare_dec * 0.45f);
        d->ph = d->ph2 = 0.0f;
        break;
    case D_CLAP:
        d->ca = tcoef(0.008f);
        break;
    case D_HAT:
        d->ca = tcoef(k->hat_dec * len);
        break;
    case D_RIM:
        d->ca = tcoef(0.012f);
        d->ph = d->ph2 = 0.0f;
        break;
    case D_BELL:
        d->ca = tcoef(0.09f * len);
        d->a2 = 1.0f;
        d->c2 = tcoef(0.012f);
        break;
    default:
        break;
    }
}

static void drums_render(uint32_t n)
{
    const kit_t *k = &KITS[patch_now[T_DRUMS]];
    uint32_t p, i;
    float tone = knob_tm[T_DRUMS][0];
    float g_tone = lp_g(500.0f * fm_exp2f(tone * 5.0f));   /* TONE: the whole kit's low-pass, 500 Hz .. 16 kHz */
    float peak = 0.0f;
    for (i = 0; i < n; i++)
        d_l[i] = d_r[i] = 0.0f;
    for (p = 0; p < ENG_DRUM_PARTS; p++) {
        drum_t *d = &drum[p];
        float lv, pl, pr;
        if (!d->on)
            continue;
        lv = d->vel * k->level;
        pr = DRUM_PAN[p];
        pl = 1.0f - pr;
        for (i = 0; i < n; i++) {
            float s = 0.0f;
            switch (p) {
            case D_KICK: {
                float f = d->f * (1.0f + k->kick_sweep * d->pe);
                d->ph += f * (1.0f / ENG_FS);
                s = sine01(d->ph);
                if (d->t < 90u)              /* the click: 2 ms of noise */
                    s += k->kick_click * noise(&seed_a) * (1.0f - (float)d->t * (1.0f / 90.0f));
                s = soft(s * (1.0f + 2.5f * k->kick_drive)) * d->amp * 1.15f;
                d->pe *= d->cp;
                break;
            }
            case D_TOM_L: case D_TOM_M: case D_TOM_H:
                d->ph += d->f * (1.0f + 0.6f * d->pe) * (1.0f / ENG_FS);
                s = sine01(d->ph) * d->amp * 0.8f;
                d->pe *= d->cp;
                break;
            case D_SNARE: case D_SNARE2: {
                float nz = noise(&seed_a), body;
                d->ph += d->f * (1.0f / ENG_FS);
                d->ph2 += d->f * 1.78f * (1.0f / ENG_FS);
                body = (sine01(d->ph) + 0.5f * sine01(d->ph2)) * d->a2;
                d->lp += d->pe * (nz - d->lp);               /* pe: the high-pass's coefficient */
                s = ((1.0f - k->snare_noise) * body + k->snare_noise * 2.2f * (nz - d->lp) * d->amp) * 0.9f;
                d->a2 *= d->c2;
                break;
            }
            case D_CLAP: {
                float nz = noise(&seed_a);
                d->lp += 0.12f * (nz - d->lp);               /* about 900 Hz .. */
                d->lp2 += 0.45f * ((nz - d->lp) - d->lp2);   /* .. to 4 kHz: a band of noise */
                s = d->lp2 * d->amp * 1.6f;
                if (d->t == 440u || d->t == 880u)            /* three quick bursts 10 ms apart, */
                    d->amp = 1.0f;
                else if (d->t == 1320u) {                    /* then the tail */
                    d->amp = 1.0f;
                    d->ca = tcoef(k->clap_dec * 0.35f);
                }
                break;
            }
            case D_HAT: {
                uint32_t j;
                float m = 0.0f;
                for (j = 0; j < 6u; j++) {                   /* six squares that share no harmonic */
                    hat_ph[j] += HAT_F[j] * k->hat_tune * (1.0f / ENG_FS);
                    if (hat_ph[j] >= 1.0f)
                        hat_ph[j] -= 1.0f;
                    m += hat_ph[j] < 0.5f ? 1.0f : -1.0f;
                }
                m = m * (1.0f / 6.0f) + 0.3f * noise(&seed_a);
                d->lp += d->pe * (m - d->lp);                /* pe: the high-pass's coefficient */
                m -= d->lp;
                d->lp2 += d->pe * (m - d->lp2);
                s = (m - d->lp2) * d->amp * 3.2f;
                break;
            }
            case D_RIM:
                d->ph += 1750.0f / ENG_FS;
                d->ph2 += 480.0f / ENG_FS;
                s = (sine01(d->ph) + 0.6f * sine01(d->ph2)) * d->amp * 0.9f;
                break;
            case D_BELL: {
                float m;
                d->ph += 540.0f / ENG_FS;
                d->ph2 += 800.0f / ENG_FS;
                if (d->ph >= 1.0f)
                    d->ph -= 1.0f;
                if (d->ph2 >= 1.0f)
                    d->ph2 -= 1.0f;
                m = (d->ph < 0.5f ? 0.5f : -0.5f) + (d->ph2 < 0.5f ? 0.5f : -0.5f);
                d->lp += 0.2f * (m - d->lp);
                s = d->lp * (0.6f * d->amp + 0.4f * d->a2);
                d->a2 *= d->c2;
                break;
            }
            default:
                break;
            }
            s *= lv;
            d_l[i] += s * pl * 2.0f;
            d_r[i] += s * pr * 2.0f;
            d->amp *= d->ca;
            d->t++;
        }
        if (d->amp < 0.0004f && !(p == D_CLAP && d->t < 1400u))
            d->on = 0;
    }
    for (i = 0; i < n; i++) {
        float a;
        drum_lp_l += g_tone * (d_l[i] - drum_lp_l);
        drum_lp_r += g_tone * (d_r[i] - drum_lp_r);
        d_l[i] = drum_lp_l;
        d_r[i] = drum_lp_r;
        a = fm_fabsf(d_l[i]);
        if (a > peak)
            peak = a;
    }
    meter[T_DRUMS] += 0.3f * (peak - meter[T_DRUMS]);
}

/* the snare's and the hat's high-pass coefficients ride in the voice's pe (nothing else there uses it) */
static void drum_fix(int part)
{
    const kit_t *k = &KITS[patch_now[T_DRUMS]];
    if (part == D_SNARE || part == D_SNARE2)
        drum[part].pe = lp_g(k->snare_hp);
    else if (part == D_HAT || part == D_HAT_P || part == D_HAT_O)
        drum[D_HAT].pe = lp_g(k->hat_hp);
}

/* --------------------------------------------------------- a mono voice --- */
typedef struct {
    stack_t held;
    uint8_t gate;
    float pitch, target;                 /* semitones (MIDI), the pitch sliding to its target */
    float vel, env, fenv, fatk;
    float ph[3], vib_ph, age;            /* age: seconds since the note started (the vibrato waits) */
    float fm_i;
    lad_t lad;
    float st[8];                         /* a track's own filter states */
} mv_t;
static mv_t bass, lead, vox;

static void mono_on(mv_t *v, int note, int vel, int slide)
{
    int was = v->held.n > 0;
    stack_add(&v->held, note, vel);
    v->target = (float)note;
    v->vel = (float)vel * (1.0f / 127.0f);
    if (!was || !slide) {                /* a new note, not played over the last: it starts again */
        if (!slide || v->env < 0.001f)
            v->pitch = v->target;
        v->fenv = 1.0f;
        v->fatk = 0.0f;
        v->age = 0.0f;
        v->fm_i = 1.0f;
    }
    v->gate = 1;
}
static void mono_off(mv_t *v, int note)
{
    stack_del(&v->held, note);
    if (v->held.n) {
        v->target = (float)v->held.note[v->held.n - 1u];
    } else {
        v->gate = 0;
    }
}

static void bass_render(uint32_t n)
{
    const bass_t *p = &BASSES[patch_now[T_BASS]];
    mv_t *v = &bass;
    float tone = knob_tm[T_BASS][0], mot = knob_tm[T_BASS][1];
    float cut = p->cut_lo * fm_powf(p->cut_hi / p->cut_lo, tone);
    float k = p->res * 4.1f, peak = 0.0f;
    float c_fd = tcoef(p->fdec), c_dec = tcoef(p->dec), c_rel = tcoef(p->rel), c_gl = tcoef(p->glide * 0.4f);
    float drive = 1.0f + 3.0f * p->drive;
    uint32_t i = 0;
    if (!v->gate && v->env < 0.0002f) {
        for (i = 0; i < n; i++)
            mono[i] = 0.0f;
        meter[T_BASS] *= 0.7f;
        return;
    }
    while (i < n) {
        uint32_t e = i + ENG_SUB < n ? i + ENG_SUB : n;
        float dt, g;
        v->pitch = v->target + (v->pitch - v->target) * fm_powf(c_gl, (float)ENG_SUB);
        dt = note_hz(v->pitch) * (1.0f / ENG_FS);
        g = lp_g(cut * fm_exp2f(p->fenv * 2.0f * mot * v->fenv * (0.5f + 0.5f * v->vel)));
        for (; i < e; i++) {
            float s = p->saw * osc_saw(&v->ph[0], dt);
            if (p->pulse > 0.0f)
                s += p->pulse * osc_pulse(&v->ph[1], dt, 0.5f);
            if (p->sub > 0.0f)
                s += p->sub * osc_pulse(&v->ph[2], dt * 0.5f, 0.5f);
            s = ladder(&v->lad, s * 0.6f * drive, g, k) * (1.0f + 0.35f * k);
            if (v->gate) {
                if (v->env < 0.999f && v->fatk < 1.0f) {   /* 2 ms up, then down to the sustain */
                    v->env += 0.0113f;
                    if (v->env >= 1.0f) {
                        v->env = 1.0f;
                        v->fatk = 1.0f;
                    }
                } else {
                    v->env = p->sus + (v->env - p->sus) * c_dec;
                }
            } else {
                v->env *= c_rel;
                v->fatk = 0.0f;
            }
            v->fenv *= c_fd;
            s = soft(s) * v->env * 0.9f;
            mono[i] = s;
            if (s > peak)
                peak = s;
        }
    }
    meter[T_BASS] += 0.3f * (peak - meter[T_BASS]);
}

static void lead_render(uint32_t n)
{
    const lead_t *p = &LEADS[patch_now[T_LEAD]];
    mv_t *v = &lead;
    float tone = knob_tm[T_LEAD][0], mot = knob_tm[T_LEAD][1], prs = pressure[T_LEAD];
    float cut = p->cut_lo * fm_powf(p->cut_hi / p->cut_lo, fm_clampf(tone + 0.25f * prs, 0.0f, 1.0f));
    float k = p->res * 4.1f, peak = 0.0f;
    float c_fd = tcoef(p->fdec), c_dec = tcoef(p->dec), c_rel = tcoef(p->rel), c_gl = tcoef(p->glide * 0.4f);
    float c_fm = tcoef(p->fm_dec);
    float att = p->att > 0.0005f ? 1.0f / (p->att * ENG_FS) : 1.0f;
    float fatt = p->fatt > 0.0005f ? 1.0f / (p->fatt * ENG_FS) : 1.0f;
    float det = fm_exp2f(p->detune * (0.5f / 1200.0f));
    uint32_t i = 0;
    if (!v->gate && v->env < 0.0002f) {
        meter[T_LEAD] *= 0.7f;
        return;
    }
    while (i < n) {
        uint32_t e = i + ENG_SUB < n ? i + ENG_SUB : n;
        float dt, g, vib = 0.0f, f_env;
        v->pitch = v->target + (v->pitch - v->target) * fm_powf(c_gl, (float)ENG_SUB);
        v->age += (float)ENG_SUB / ENG_FS;
        v->vib_ph += p->vib_rate * (float)ENG_SUB / ENG_FS;
        if (v->vib_ph >= 1.0f)
            v->vib_ph -= 1.0f;
        if (v->age > p->vib_delay)       /* the vibrato comes in after a moment, over a quarter second */
            vib = fm_minf((v->age - p->vib_delay) * 4.0f, 1.0f) * sine01(v->vib_ph) * p->vib_cents * 2.0f * mot;
        dt = note_hz(v->pitch + vib * 0.01f) * (1.0f / ENG_FS);
        f_env = v->fatk * v->fenv;
        g = lp_g(cut * fm_exp2f(p->fenv * f_env));
        for (; i < e; i++) {
            float s;
            if (p->fm > 0.5f) {              /* two operators: a modulator whose index falls away */
                float m;
                v->ph[1] += dt * p->fm_ratio;
                if (v->ph[1] >= 1.0f)
                    v->ph[1] -= 1.0f;
                m = sine01(v->ph[1]) * p->fm_index * (0.15f + 0.85f * v->fm_i) * (0.4f + 0.6f * tone) * (1.0f / 6.2831853f);
                v->ph[0] += dt;
                if (v->ph[0] >= 1.0f)
                    v->ph[0] -= 1.0f;
                s = sine01(v->ph[0] + m + 4.0f) * 0.45f;
                v->fm_i *= c_fm;
            } else {
                s = 0.5f * (osc_saw(&v->ph[0], dt * det) + osc_saw(&v->ph[1], dt / det));
                s = ladder(&v->lad, s * 0.7f, g, k) * (1.0f + 0.3f * k);
            }
            if (v->gate) {
                if (v->st[0] < 1.0f) {       /* st[0]: the attack's ramp */
                    v->st[0] += att;
                    v->env += att;
                    if (v->env > 1.0f)
                        v->env = 1.0f;
                } else {
                    v->env = p->sus + (v->env - p->sus) * c_dec;
                }
                if (v->fatk < 1.0f)
                    v->fatk = fm_minf(v->fatk + fatt, 1.0f);
                else
                    v->fenv *= c_fd;
            } else {
                v->env *= c_rel;
                v->st[0] = 0.0f;
            }
            s *= v->env * (0.5f + 0.5f * v->vel) * 1.4f;
            mono[i] += s;
            if (s > peak)
                peak = s;
        }
    }
    meter[T_LEAD] += 0.3f * (peak - meter[T_LEAD]);
}

/* the voice: a buzz at the note through three formants. The vowels run dark to bright, U O A E I */
static const float VOW_F[5][3] = {{300.0f, 870.0f, 2240.0f}, {570.0f, 840.0f, 2410.0f}, {730.0f, 1090.0f, 2440.0f},
                                  {530.0f, 1840.0f, 2480.0f}, {270.0f, 2290.0f, 3010.0f}};
static const float VOW_A[5][3] = {{1.0f, 0.25f, 0.06f}, {1.0f, 0.4f, 0.08f}, {1.0f, 0.5f, 0.15f},
                                  {1.0f, 0.32f, 0.2f}, {1.0f, 0.22f, 0.26f}};
static void vox_render(uint32_t n)
{
    const vox_t *p = &VOXES[patch_now[T_VOX]];
    mv_t *v = &vox;
    float tone = knob_tm[T_VOX][0], mot = knob_tm[T_VOX][1], prs = pressure[T_VOX];
    float c_gl = tcoef(p->glide * 0.4f), c_rel = tcoef(p->rel), peak = 0.0f;
    float att = p->att > 0.0005f ? 1.0f / (p->att * ENG_FS) : 1.0f;
    float g_b = lp_g(p->bright), q = 1.0f / p->q;
    float vw = fm_clampf(tone + 0.3f * prs, 0.0f, 1.0f) * 4.0f, fr;
    float ff[3], fa[3];
    int32_t vi, j;
    uint32_t i = 0;
    if (p->stepped > 0.5f)
        vw = (float)(int32_t)(vw + 0.5f);
    v_vowel = vw * 0.25f;
    if (!v->gate && v->env < 0.0002f) {
        meter[T_VOX] *= 0.7f;
        return;
    }
    vi = (int32_t)vw;
    if (vi > 3)
        vi = 3;
    fr = vw - (float)vi;
    for (j = 0; j < 3; j++) {
        float hz = (VOW_F[vi][j] + (VOW_F[vi + 1][j] - VOW_F[vi][j]) * fr) * p->size;
        ff[j] = 2.0f * fm_sin_pi(3.1415927f * fm_minf(hz, 5000.0f) / ENG_FS);
        fa[j] = VOW_A[vi][j] + (VOW_A[vi + 1][j] - VOW_A[vi][j]) * fr;
    }
    while (i < n) {
        uint32_t e = i + ENG_SUB < n ? i + ENG_SUB : n;
        float dt, vib = 0.0f;
        if (p->stepped > 0.5f)
            v->pitch = v->target;
        else
            v->pitch = v->target + (v->pitch - v->target) * fm_powf(c_gl, (float)ENG_SUB);
        v->age += (float)ENG_SUB / ENG_FS;
        v->vib_ph += p->vib_rate * (float)ENG_SUB / ENG_FS;
        if (v->vib_ph >= 1.0f)
            v->vib_ph -= 1.0f;
        if (v->age > p->vib_delay)
            vib = fm_minf((v->age - p->vib_delay) * 3.0f, 1.0f) * sine01(v->vib_ph) * p->vib_cents * 2.0f * mot;
        dt = note_hz(v->pitch + p->octave + vib * 0.01f) * (1.0f / ENG_FS);
        for (; i < e; i++) {
            float s, o = 0.0f;
            if (p->narrow > 0.5f)
                s = osc_pulse(&v->ph[0], dt, 0.12f) + 0.76f;
            else
                s = osc_saw(&v->ph[0], dt);
            v->st[6] += g_b * (s - v->st[6]);            /* the buzz, rounded off */
            s = v->st[6] + p->breath * noise(&seed_a);
            for (j = 0; j < 3; j++) {                    /* each formant: a band that rings */
                float *z = &v->st[j * 2];
                float hi;
                z[0] += ff[j] * z[1];
                hi = s - z[0] - q * z[1];
                z[1] += ff[j] * hi;
                o += z[1] * fa[j];
            }
            if (v->gate)
                v->env = fm_minf(v->env + att, 1.0f);
            else
                v->env *= c_rel;
            o = soft(o * q * 2.6f) * v->env * (0.6f + 0.4f * v->vel) * 0.8f;
            mono[i] += o;
            if (o > peak)
                peak = o;
        }
    }
    meter[T_VOX] += 0.3f * (peak - meter[T_VOX]);
}

/* ----------------------------------------------------------------- chords --- */
typedef struct {
    uint8_t stage;                       /* 0 off, 1 attack, 2 held, 3 let go */
    uint8_t note;
    uint32_t age;
    float vel, env, fenv, ph[3], hp;
    lad_t lad;
} cv_t;
static cv_t chord[ENG_CHORD_VOICES];
static uint32_t chord_age;
static float pwm_ph;
#define CHO_N 2048u
static float cho_buf[CHO_N] PLAT_POOL;
static uint32_t cho_w;
static float cho_ph;

static void chord_on(int note, int vel)
{
    uint32_t i, best = 0, score = 0;
    for (i = 0; i < ENG_CHORD_VOICES; i++)       /* the same note again takes its own voice */
        if (chord[i].stage && chord[i].note == note) {
            best = i;
            goto take;
        }
    for (i = 0; i < ENG_CHORD_VOICES; i++) {     /* else a free voice, else the oldest let go, else the oldest */
        cv_t *c = &chord[i];
        uint32_t s = !c->stage ? 0xFFFFFFFFu : (c->stage == 3 ? 0x80000000u : 0u) + (chord_age - c->age);
        if (s >= score) {
            score = s;
            best = i;
        }
    }
take:
    chord[best].stage = 1;
    chord[best].note = (uint8_t)note;
    chord[best].age = ++chord_age;
    chord[best].vel = (float)vel * (1.0f / 127.0f);
    chord[best].fenv = 1.0f;
}
static void chord_off(int note)
{
    uint32_t i;
    for (i = 0; i < ENG_CHORD_VOICES; i++)
        if (chord[i].stage && chord[i].stage != 3 && chord[i].note == note)
            chord[i].stage = 3;
}

static void chords_render(uint32_t n)
{
    const chord_t *p = &CHORDS[patch_now[T_CHORDS]];
    float tone = knob_tm[T_CHORDS][0], mot = knob_tm[T_CHORDS][1];
    float span = fm_powf(p->cut_hi / p->cut_lo, tone), k = p->res * 4.1f, peak = 0.0f;
    float c_fd = tcoef(p->fdec), c_dec = tcoef(p->dec), c_rel = tcoef(p->rel), g_hp = lp_g(p->hp);
    float att = p->att > 0.0005f ? 1.0f / (p->att * ENG_FS) : 1.0f;
    float det = fm_exp2f(p->detune * (0.5f / 1200.0f)), width, amt, depth;
    uint32_t i, vn;
    for (i = 0; i < n; i++)
        cmono[i] = 0.0f;
    pwm_ph += 0.31f * (float)n / ENG_FS;
    if (pwm_ph >= 1.0f)
        pwm_ph -= 1.0f;
    width = 0.5f + 0.4f * p->pwm * mot * 2.0f * sine01(pwm_ph);
    width = fm_clampf(width, 0.08f, 0.92f);
    for (vn = 0; vn < ENG_CHORD_VOICES; vn++) {
        cv_t *c = &chord[vn];
        float hz, dt;
        if (!c->stage)
            continue;
        hz = note_hz((float)c->note);
        dt = hz * (1.0f / ENG_FS);
        i = 0;
        while (i < n) {
            uint32_t e = i + ENG_SUB < n ? i + ENG_SUB : n;
            /* a patch that follows the key whistles the note: TONE picks its octave, below, at, above */
            float base = p->keytrack > 0.5f ? hz * (tone < 0.34f ? 0.5f : tone < 0.67f ? 1.0f : 2.0f) : p->cut_lo * span;
            float g = lp_g(base * fm_exp2f(p->fenv * 2.0f * mot * c->fenv));
            for (; i < e; i++) {
                float s = 0.0f;
                if (p->saw > 0.0f)
                    s += p->saw * osc_saw(&c->ph[0], dt * det);
                if (p->pulse > 0.0f)
                    s += p->pulse * osc_pulse(&c->ph[1], dt / det, width);
                if (p->sub > 0.0f)
                    s += p->sub * osc_pulse(&c->ph[2], dt * 0.5f, 0.5f);
                if (p->noise > 0.0f)
                    s += p->noise * noise(&seed_b);
                s = ladder(&c->lad, s * 0.45f, g, k) * (1.0f + 0.3f * k);
                c->hp += g_hp * (s - c->hp);
                s -= c->hp;
                if (c->stage == 1) {
                    c->env += att;
                    if (c->env >= 1.0f) {
                        c->env = 1.0f;
                        c->stage = 2;
                    }
                } else if (c->stage == 2) {
                    c->env = p->sus + (c->env - p->sus) * c_dec;
                } else {
                    c->env *= c_rel;
                }
                c->fenv *= c_fd;
                cmono[i] += s * c->env * (0.55f + 0.45f * c->vel);
            }
        }
        if (c->stage == 3 && c->env < 0.0003f) {
            c->stage = 0;
            c->env = 0.0f;
        }
    }
    /* the chorus: one short delay read at two places that sway against each other, one a side */
    amt = fm_clampf(p->chorus + knob_g[K_CHORUS], 0.0f, 1.0f);
    depth = 70.0f * amt;
    for (i = 0; i < n; i++) {
        float x = soft(cmono[i] * 0.7f * p->level), lfo, rd, a, b, fr;
        uint32_t ri;
        cho_buf[cho_w] = x;
        cho_ph += (0.45f + 0.5f * knob_g[K_CHORUS]) / ENG_FS;
        if (cho_ph >= 1.0f)
            cho_ph -= 1.0f;
        lfo = 4.0f * fm_fabsf(cho_ph - 0.5f) - 1.0f;         /* a triangle, as the old ones' */
        rd = (float)cho_w + (float)CHO_N - 160.0f - depth * lfo;
        ri = (uint32_t)rd;
        fr = rd - (float)ri;
        a = cho_buf[ri & (CHO_N - 1u)] + (cho_buf[(ri + 1u) & (CHO_N - 1u)] - cho_buf[ri & (CHO_N - 1u)]) * fr;
        rd = (float)cho_w + (float)CHO_N - 160.0f + depth * lfo;
        ri = (uint32_t)rd;
        fr = rd - (float)ri;
        b = cho_buf[ri & (CHO_N - 1u)] + (cho_buf[(ri + 1u) & (CHO_N - 1u)] - cho_buf[ri & (CHO_N - 1u)]) * fr;
        cho_w = (cho_w + 1u) & (CHO_N - 1u);
        c_l[i] = x + amt * (a - 0.3f * x);
        c_r[i] = x + amt * (b - 0.3f * x);
        if (x > peak)
            peak = x;
    }
    v_chorus = (4.0f * fm_fabsf(cho_ph - 0.5f) - 1.0f) * amt;
    meter[T_CHORDS] += 0.3f * (peak - meter[T_CHORDS]);
}

/* ---------------------------------------------------- echo and reverb --- */
#define ECHO_N 40000u
static int16_t echo_buf[ECHO_N] PLAT_POOL;
static uint32_t echo_w;
static float echo_lp;
static const uint16_t RV_LEN[4] = {1423, 1867, 2269, 2707};
static const uint16_t AP_LEN[3] = {113, 337, 571};
static float rv_buf[4][2708] PLAT_POOL, ap_buf[3][572] PLAT_POOL;
static uint32_t rv_i[4], ap_i[3];
static float rv_lp[4];

static void space_render(uint32_t n)
{
    float space = knob_g[K_SPACE], echo = knob_g[K_ECHO];
    float fb = 0.72f + 0.25f * space, wet = space * space * 0.55f;
    float beat = 60.0f * ENG_FS / tempo_bpm, t = 0.75f * beat;
    uint32_t i, j, et;
    while (2.0f * t > (float)(ECHO_N - 2u))      /* a dotted eighth, or the next shorter that fits */
        t *= 0.6666667f;
    et = (uint32_t)t;
    for (i = 0; i < n; i++) {
        float inl = s_l[i], inr = s_r[i], x = 0.5f * (inl + inr), el, er, d[4], sum;
        /* the echo: left a beat-part late, right two, round again darker */
        el = (float)echo_buf[(echo_w + ECHO_N - et) % ECHO_N] * (1.0f / 20000.0f);
        er = (float)echo_buf[(echo_w + ECHO_N - 2u * et) % ECHO_N] * (1.0f / 20000.0f);
        echo_lp += 0.35f * (er - echo_lp);
        echo_buf[echo_w] = (int16_t)(fm_clampf(x * echo + echo_lp * (0.35f + 0.3f * echo), -1.6f, 1.6f) * 20000.0f);
        echo_w = echo_w + 1u == ECHO_N ? 0u : echo_w + 1u;
        /* the reverb: three all-passes smear it, four delays feed each other */
        x += 0.5f * (el + er) * echo;
        for (j = 0; j < 3u; j++) {
            float b = ap_buf[j][ap_i[j]], y = b - 0.6f * x;
            ap_buf[j][ap_i[j]] = fm_flush(x + 0.6f * y);
            ap_i[j] = ap_i[j] + 1u == AP_LEN[j] ? 0u : ap_i[j] + 1u;
            x = y;
        }
        sum = 0.0f;
        for (j = 0; j < 4u; j++) {
            d[j] = rv_buf[j][rv_i[j]];
            rv_lp[j] += 0.45f * (d[j] - rv_lp[j]);
            d[j] = rv_lp[j];
            sum += d[j];
        }
        sum *= 0.5f;
        for (j = 0; j < 4u; j++) {
            rv_buf[j][rv_i[j]] = fm_flush(x + (d[j] - sum) * fb);
            rv_i[j] = rv_i[j] + 1u == RV_LEN[j] ? 0u : rv_i[j] + 1u;
        }
        w_l[i] = el * echo * 0.7f + (d[0] - d[2]) * wet;
        w_r[i] = er * echo * 0.7f + (d[1] - d[3]) * wet;
    }
}

/* ---------------------------------------------------------------- the bus --- */
static struct {
    float ic[4];                         /* the sweep's states, left and right */
    float g, k, a1, a2, a3;
    uint8_t mode;                        /* 0 off, 1 low-pass, 2 high-pass */
    float ph_z[2][4], ph_fb[2], ph_lfo;
    float pump, duck, c_pump;
    float hold_l, hold_r;
    uint32_t hold_n;
} bus;

static void events(void)
{
    while (ev_tail != ev_head) {
        ev_t e = evq[ev_tail % EVQ];
        ev_tail++;
        if (e.type == EV_ON)
            eng_note_on_now(e.track, e.a, e.b);
        else if (e.type == EV_OFF)
            eng_note_off_now(e.track, e.a);
        else if (e.type == EV_PATCH && e.track < NTRACK && e.a < NPATCH)
            patch_now[e.track] = e.a;
        else if (e.type == EV_PANIC) {
            uint32_t i;
            bass.held.n = lead.held.n = vox.held.n = 0;
            bass.gate = lead.gate = vox.gate = 0;
            for (i = 0; i < ENG_CHORD_VOICES; i++)
                if (chord[i].stage)
                    chord[i].stage = 3;
        }
    }
}

void eng_begin(uint32_t n)
{
    float s = knob_g[K_SWEEP], t, hz;
    blk_n = n > ENG_BLOCK_MAX ? ENG_BLOCK_MAX : n;
    events();
    /* the sweep: the knob's middle is out; down from there a low-pass closes, up a high-pass opens */
    if (s < 0.46f) {
        t = (0.46f - s) * (1.0f / 0.46f);
        hz = 16000.0f * fm_exp2f(-7.0f * t);
        bus.mode = 1;
    } else if (s > 0.54f) {
        t = (s - 0.54f) * (1.0f / 0.46f);
        hz = 30.0f * fm_exp2f(7.5f * t);
        bus.mode = 2;
    } else {
        t = 0.0f;
        hz = 1000.0f;
        bus.mode = 0;
    }
    bus.g = fm_tanf(3.1415927f * hz / ENG_FS);
    bus.k = 1.0f / (0.8f + 1.4f * t);
    bus.a1 = 1.0f / (1.0f + bus.g * (bus.g + bus.k));
    bus.a2 = bus.g * bus.a1;
    bus.a3 = bus.g * bus.a2;
    bus.c_pump = tcoef(0.34f * 60.0f / tempo_bpm);   /* the pump lets go over about a third of a beat */
    if (kick_hit) {
        kick_hit = 0;
        bus.pump = 1.0f;
        v_pulse = 1.0f;
    }
}

void eng_part_a(void)
{
    uint32_t n = blk_n, i;
    drums_render(n);
    bass_render(n);
    for (i = 0; i < n; i++) {
        a_l[i] = a_r[i] = mono[i] * 0.85f;
        mono[i] = 0.0f;
    }
    lead_render(n);
    for (i = 0; i < n; i++) {
        a_l[i] += mono[i] * 0.62f;
        a_r[i] += mono[i] * 0.84f;
        mono[i] = 0.0f;
    }
    vox_render(n);
    for (i = 0; i < n; i++) {
        a_l[i] += mono[i] * 0.84f;
        a_r[i] += mono[i] * 0.66f;
    }
}

void eng_part_b(void)
{
    chords_render(blk_n);
    space_render(blk_n);
}

void eng_drop_b(void)
{
    uint32_t i;
    for (i = 0; i < blk_n; i++)
        c_l[i] = c_r[i] = w_l[i] = w_r[i] = 0.0f;
}

void eng_end(int32_t *lr, uint32_t n, float gain)
{
    float depth = knob_g[K_PUMP] * 0.92f, ph_amt = knob_g[K_PHASER], grit = knob_g[K_GRIT];
    float space = knob_g[K_SPACE], ph_a[2] = {0.0f, 0.0f};
    uint32_t i, c, hold = 1u + (uint32_t)(grit * grit * 11.0f);
    float steps = fm_exp2f(15.0f - 9.0f * grit), send = 0.25f + 0.75f * space;
    if (n > blk_n)
        n = blk_n;
    if (ph_amt > 0.001f) {               /* the phaser: its four all-passes swept once a block, the sides a quarter apart */
        bus.ph_lfo += (0.12f + 0.5f * ph_amt) * (float)n / ENG_FS;
        if (bus.ph_lfo >= 1.0f)
            bus.ph_lfo -= 1.0f;
        for (c = 0; c < 2u; c++) {
            float w = 3.1415927f * 420.0f * fm_exp2f(1.4f * (1.0f + sine01(bus.ph_lfo + 0.25f * (float)c))) / ENG_FS;
            ph_a[c] = (1.0f - w) / (1.0f + w);
        }
        v_phaser = sine01(bus.ph_lfo) * ph_amt;
    } else {
        v_phaser = 0.0f;
    }
    for (i = 0; i < n; i++) {
        float m[2], o[2];
        m[0] = a_l[i] + c_l[i];
        m[1] = a_r[i] + c_r[i];
        for (c = 0; c < 2u; c++) {
            float x = m[c];
            if (bus.mode) {
                float *ic = &bus.ic[c * 2u];
                float v3 = x - ic[1], v1 = bus.a1 * ic[0] + bus.a2 * v3, v2 = ic[1] + bus.a2 * ic[0] + bus.a3 * v3;
                ic[0] = fm_flush(2.0f * v1 - ic[0]);
                ic[1] = fm_flush(2.0f * v2 - ic[1]);
                x = bus.mode == 1 ? v2 : x - bus.k * v1 - v2;
            }
            if (ph_amt > 0.001f) {
                float y = x + bus.ph_fb[c] * 0.55f, a = ph_a[c];
                uint32_t j;
                for (j = 0; j < 4u; j++) {
                    float z = bus.ph_z[c][j], out = z - a * y;
                    bus.ph_z[c][j] = fm_flush(y + a * out);
                    y = out;
                }
                bus.ph_fb[c] = y;
                x += ph_amt * 0.8f * y;
            }
            m[c] = x;
        }
        /* the pump: the bus held down at each kick and let back up */
        bus.pump *= bus.c_pump;
        bus.duck += 0.02f * (depth * bus.pump - bus.duck);
        m[0] *= 1.0f - bus.duck;
        m[1] *= 1.0f - bus.duck;
        s_l[i] = (m[0] + 0.3f * d_l[i]) * send;      /* what the echo and the reverb hear, next block */
        s_r[i] = (m[1] + 0.3f * d_r[i]) * send;
        o[0] = m[0] + d_l[i] + w_l[i];
        o[1] = m[1] + d_r[i] + w_r[i];
        if (grit > 0.001f) {                         /* grit: fewer samples and fewer steps, as an old sampler's */
            if (bus.hold_n == 0u) {
                bus.hold_l = (float)(int32_t)(o[0] * steps) / steps;
                bus.hold_r = (float)(int32_t)(o[1] * steps) / steps;
            }
            bus.hold_n = bus.hold_n + 1u >= hold ? 0u : bus.hold_n + 1u;
            o[0] = bus.hold_l;
            o[1] = bus.hold_r;
        }
        lr[2u * i] = (int32_t)(soft(o[0] * 0.8f) * gain);
        lr[2u * i + 1u] = (int32_t)(soft(o[1] * 0.8f) * gain);
    }
    v_duck = bus.duck;
    v_pulse *= 0.86f;
}

/* -------------------------------------------------------------- the notes --- */
void eng_note_on_now(int track, int note, int vel)
{
    if (note < 0 || note > 127 || vel <= 0)
        return;
    switch (track) {
    case T_DRUMS:
        drum_hit(note % 12, vel);
        drum_fix(note % 12);
        break;
    case T_BASS:
        mono_on(&bass, note, vel, BASSES[patch_now[T_BASS]].glide > 0.001f);
        break;
    case T_CHORDS:
        chord_on(note, vel);
        break;
    case T_LEAD:
        mono_on(&lead, note, vel, LEADS[patch_now[T_LEAD]].glide > 0.001f);
        break;
    case T_VOX:
        mono_on(&vox, note, vel, 1);
        break;
    default:
        break;
    }
}
void eng_note_off_now(int track, int note)
{
    if (track == T_BASS)
        mono_off(&bass, note);
    else if (track == T_CHORDS)
        chord_off(note);
    else if (track == T_LEAD)
        mono_off(&lead, note);
    else if (track == T_VOX)
        mono_off(&vox, note);
}
static void ev_push(int type, int track, int a, int b)
{
    if (ev_head - ev_tail >= EVQ)
        return;
    evq[ev_head % EVQ] = (ev_t){(uint8_t)type, (uint8_t)track, (uint8_t)a, (uint8_t)b};
    ev_head++;
}
void eng_note_on(int track, int note, int vel) { ev_push(EV_ON, track, note, vel); }
void eng_note_off(int track, int note) { ev_push(EV_OFF, track, note, 0); }
void eng_patch(int track, int p) { ev_push(EV_PATCH, track, p, 0); }
void eng_panic(void) { ev_push(EV_PANIC, 0, 0, 0); }

void eng_knob(int track, int k, float v)
{
    v = fm_clampf(v, 0.0f, 1.0f);
    if (k == K_TONE || k == K_MOTION) {
        if (track >= 0 && track < NTRACK)
            knob_tm[track][k] = v;
    } else if (k < NKNOB) {
        knob_g[k] = v;
    }
}
float eng_knob_get(int track, int k)
{
    if (k == K_TONE || k == K_MOTION)
        return track >= 0 && track < NTRACK ? knob_tm[track][k] : 0.0f;
    return k < NKNOB ? knob_g[k] : 0.0f;
}
void eng_pressure(int track, float v)
{
    if (track >= 0 && track < NTRACK)
        pressure[track] = fm_clampf(v, 0.0f, 1.0f);
}
void eng_tempo(float bpm) { tempo_bpm = fm_clampf(bpm, 40.0f, 240.0f); }
float eng_tempo_get(void) { return tempo_bpm; }
int eng_patch_count(int track) { (void)track; return NPATCH; }
int eng_patch_now(int track) { return track >= 0 && track < NTRACK ? patch_now[track] : 0; }
const char *eng_patch_name(int track, int p)
{
    if (p < 0 || p >= NPATCH)
        return "";
    switch (track) {
    case T_DRUMS: return KITS[p].name;
    case T_BASS: return BASSES[p].name;
    case T_CHORDS: return CHORDS[p].name;
    case T_LEAD: return LEADS[p].name;
    case T_VOX: return VOXES[p].name;
    default: return "";
    }
}
float eng_level(int track) { return track >= 0 && track < NTRACK ? meter[track] : 0.0f; }
float eng_pulse(void) { return v_pulse; }
float eng_duck(void) { return v_duck; }
float eng_phase(int which) { return which ? v_chorus : v_phaser; }
float eng_vowel(void) { return v_vowel; }
int eng_note_now(int track)
{
    uint32_t i, best = 0, age = 0;
    switch (track) {
    case T_BASS: return bass.held.n ? bass.held.note[bass.held.n - 1u] : -1;
    case T_LEAD: return lead.held.n ? lead.held.note[lead.held.n - 1u] : -1;
    case T_VOX: return vox.held.n ? vox.held.note[vox.held.n - 1u] : -1;
    case T_CHORDS:
        for (i = 0; i < ENG_CHORD_VOICES; i++)
            if ((chord[i].stage == 1 || chord[i].stage == 2) && chord[i].age >= age) {
                age = chord[i].age;
                best = i + 1u;
            }
        return best ? chord[best - 1u].note : -1;
    default: return -1;
    }
}
uint32_t eng_voices_on(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < ENG_CHORD_VOICES; i++)
        n += chord[i].stage != 0;
    for (i = 0; i < ENG_DRUM_PARTS; i++)
        n += drum[i].on;
    return n + (bass.env > 0.0002f) + (lead.env > 0.0002f) + (vox.env > 0.0002f);
}

void eng_init(void)
{
    uint32_t i;
    for (i = 0; i <= SINE_N; i++)
        sine_tab[i] = fm_sinf((float)i * (6.2831853f / (float)SINE_N));
    for (i = 0; i < NTRACK; i++) {
        knob_tm[i][0] = 0.6f;
        knob_tm[i][1] = 0.5f;
    }
    knob_tm[T_DRUMS][0] = 0.9f;
    knob_tm[T_VOX][0] = 0.5f;
    knob_tm[T_CHORDS][0] = 0.7f;
    knob_g[K_PUMP] = 0.6f;
    knob_g[K_SWEEP] = 0.5f;
    knob_g[K_SPACE] = 0.3f;
}
