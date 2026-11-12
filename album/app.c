/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alive-Stable-7254 */
/* Album 02: the controls, the screen and MIDI around the sound (dsp/engine.h) and the sequencer (dsp/seq.h). Laid
 * out after fm1-base's template (Charles Vestal, MPL-2.0). Part of the unity build (base/device/unity.c,
 * base/host/plat_host.c), after the platform: gfx.c (the canvas, the fonts) and announce.c are in scope.
 *
 * The controls (docs/MANUAL.md has them for the player):
 *   keys                 the chosen track; with SEQ lit, the white keys are the steps and four black keys the pages
 *   SELECT, PRESET       the track, its patch
 *   K1..K4, ALGORITHM    TONE, MOTION, PUMP, SWEEP, SPACE; EDIT held: PHASER, CHORUS, GRIT, ECHO
 *   PLAY, REC            the patterns start and stop; recording on and off
 *   SEQ                  step mode; held: length (ALGORITHM), direction (PRESET), swing (K1)
 *   HOME                 held: a white key picks the pattern (held a second: copies this one there); REC clears it;
 *                        tapped: every note let go
 *   ENV                  held: a white key recalls a scene (held a second: stores one)
 *   LFO                  held: a white key adds a scene to the chain, ALGORITHM its bars, OCT- takes one off;
 *                        tapped: the chain on and off
 *   FX                   held: the first five white keys mute the tracks; tapped: this track muted or not
 *   ARP                  the arpeggiator on and off; held: mode (PRESET), rate (ALGORITHM), octaves (K1), gate
 *                        (K2), latch (K3)
 *   SCL                  chord mode: tapped with keys held, they become the chord; tapped alone, on and off
 *   GLO                  tapped twice or more: the tempo; held: tempo (ALGORITHM), project (PRESET, SAVE saves
 *                        there), recording onto the steps or as played (K1), the track's MIDI channel (K2), one
 *                        core or two (K3), MIDI out (K4)
 *   SAVE                 the project, kept */
#include "fastmath.h"
#include "engine.h"
#include "seq.h"
#include "visual.c"

static const char *const TRACK_NAME[NTRACK] = {"DRUMS", "BASS", "CHORDS", "LEAD", "VOX"};
static const char *const KNOB_NAME[NKNOB] = {"TONE", "MOTION", "PUMP", "SWEEP", "SPACE", "PHASER", "CHORUS", "GRIT",
                                             "ECHO"};
static const char *const DIR_NAME[SEQ_NDIR] = {"FORWARD", "BACKWARD", "PING-PONG", "RANDOM"};
static const char *const ARP_NAME[ARP_NMODE] = {"UP", "DOWN", "UP-DOWN +", "UP-DOWN", "RANDOM", "AS PLAYED",
                                                "REPEAT"};
static const char *const RATE_NAME[ARP_NRATE] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const char *const OUT_NAME[3] = {"OFF", "NOTES", "NOTES + CLOCK"};
static const char *const DRUM_NAME[ENG_DRUM_PARTS] = {"KICK", "RIM", "SNARE", "CLAP", "SNARE 2", "LOW TOM", "HAT",
                                                      "MID TOM", "PEDAL HAT", "HIGH TOM", "OPEN HAT", "BELL"};
static const int8_t TRACK_BASE[NTRACK] = {36, 29, 53, 53, 53};   /* the lowest key's note at octave 0 */
static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
#define KNOB_STEPS 64

#include "project.c"

/* ------------------------------------------------------------- settings --- */
#define SET_MAGIC 0x41323032u            /* "A202": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    uint8_t track;
    uint8_t slot;                        /* the project open */
    uint8_t cores;                       /* 1 or 2, from the next start */
    uint8_t midi_out;                    /* 0 off, 1 notes, 2 notes and clock */
    uint8_t chan[NTRACK];                /* each track's MIDI channel, 0..15 */
    uint8_t bank_of[PROJ_SLOTS];         /* the bank each project is in, 0xFF none yet */
    uint8_t rsv[3];
} settings_t;
static settings_t set;
static uint8_t dirty;                    /* the project changed since it was saved: SAVE is lit */
static uint8_t cores_now = 1;            /* the cores rendering at this start */

static void settings_defaults(void)
{
    int t;
    memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    set.track = T_CHORDS;
    set.cores = 2;
    set.midi_out = 1;
    for (t = 0; t < NTRACK; t++)
        set.chan[t] = (uint8_t)t;        /* channels 1 to 5, and 10 for the drums besides */
    memset(set.bank_of, 0xFF, sizeof set.bank_of);
}
static int settings_ok(void)
{
    int t, i, j;
    if (set.magic != SET_MAGIC || set.track >= NTRACK || set.slot >= PROJ_SLOTS || set.cores < 1u ||
        set.cores > 2u || set.midi_out > 2u)
        return 0;
    for (t = 0; t < NTRACK; t++)
        if (set.chan[t] > 15u)
            return 0;
    for (i = 0; i < PROJ_SLOTS; i++) {
        if (set.bank_of[i] != 0xFFu && set.bank_of[i] >= PROJ_BANKS)
            return 0;
        for (j = 0; j < i; j++)
            if (set.bank_of[i] != 0xFFu && set.bank_of[i] == set.bank_of[j])
                return 0;
    }
    return 1;
}

/* the sound's settings into the engine (after a load) */
static void sound_apply(void)
{
    int t, k;
    for (t = 0; t < NTRACK; t++) {
        eng_patch(t, snd.patch[t]);
        eng_knob(t, K_TONE, (float)snd.tm[t][0] / KNOB_STEPS);
        eng_knob(t, K_MOTION, (float)snd.tm[t][1] / KNOB_STEPS);
    }
    for (k = K_PUMP; k < NKNOB; k++)
        eng_knob(0, k, (float)snd.knob[k] / KNOB_STEPS);
    seq_tempo((float)snd.bpm);
}

static void project_open(int slot)
{
    seq_play(0);
    eng_panic();
    if (set.bank_of[slot] == 0xFFu || proj_load_bank(set.bank_of[slot]) != 0) {
        seq_init();
        sound_defaults();
    }
    set.slot = (uint8_t)slot;
    sound_apply();
    dirty = 0;
}

/* the project into slot: a free bank, then the settings pointed at it. 0 = saved */
static int project_save(int slot)
{
    uint32_t b, i;
    int used;
    for (b = 0; b < PROJ_BANKS; b++) {   /* a bank no project is in */
        used = 0;
        for (i = 0; i < PROJ_SLOTS; i++)
            used |= set.bank_of[i] == b;
        if (!used)
            break;
    }
    if (b == PROJ_BANKS || proj_write_bank(b) != 0)
        return -1;
    set.bank_of[slot] = (uint8_t)b;
    set.slot = (uint8_t)slot;
    if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) != 0)
        return -1;
    dirty = 0;
    return 0;
}

/* ------------------------------------------------------------------ MIDI --- */
static volatile uint32_t midi_count;
static volatile int8_t midi_patch[NTRACK];   /* a program change from MIDI, for the main loop: -1 none */
static int chan_track(uint32_t ch)       /* the track that listens on a channel; channel 10 is always the drums */
{
    int t;
    for (t = 0; t < NTRACK; t++)
        if (set.chan[t] == ch)
            return t;
    return ch == 9u ? T_DRUMS : -1;
}
/* the KeyStep Pro's (or any controller's) knobs: on a track's channel, TONE and MOTION are that track's */
static int cc_knob(uint32_t cc)
{
    switch (cc) {
    case 74: return K_TONE;
    case 71: case 1: return K_MOTION;
    case 85: return K_PUMP;
    case 86: return K_SWEEP;
    case 91: return K_SPACE;
    case 94: return K_PHASER;
    case 93: return K_CHORUS;
    case 87: return K_GRIT;
    case 88: return K_ECHO;
    default: return -1;
    }
}
static volatile uint8_t midi_knob_seen;
static void midi_in(void)                /* the render: the packets that came in, played at once */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        int t = st < 0xF0u ? chan_track(st & 15u) : -1, k;
        if (st >= 0xF8u) {
            seq_midi(st, d1, d2);
            continue;
        }
        if (st != 0xFEu)
            midi_count++;
        if (t < 0) {                     /* not a track's channel: only the whole sound's knobs */
            if ((st & 0xF0u) == 0xB0u && (k = cc_knob(d1)) > K_MOTION) {
                eng_knob(0, k, (float)d2 * (1.0f / 127.0f));
                midi_knob_seen = 1;
            }
            continue;
        }
        switch (st & 0xF0u) {
        case 0x90u:
            seq_midi_note(t, (int)d1, (int)d2);
            break;
        case 0x80u:
            seq_midi_note(t, (int)d1, 0);
            break;
        case 0xD0u:
            eng_pressure(t, (float)d1 * (1.0f / 127.0f));
            break;
        case 0xC0u:
            if (d1 < ENG_PATCHES)
                midi_patch[t] = (int8_t)d1;
            break;
        case 0xB0u:
            if (d1 == 123u || d1 == 120u) {
                eng_panic();
            } else if ((k = cc_knob(d1)) >= 0) {
                eng_knob(t, k, (float)d2 * (1.0f / 127.0f));
                midi_knob_seen = 1;
            }
            break;
        default:
            break;
        }
    }
}

static void render_b(uint32_t arg)       /* the second core's job */
{
    (void)arg;
    eng_part_b();
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float gain_now;
    float gain = (float)master_q12 * (8300000.0f / 4096.0f);
    gain_now += (gain - gain_now) * 0.25f;   /* MASTER glides: no step at a block's edge */
    midi_in();
    seq_render(n);
    eng_begin(n);
    if (plat_cpu2_run(render_b, 0)) {    /* the chords and the echo and reverb on the second core, meanwhile the rest */
        eng_part_a();
        if (plat_cpu2_wait() != 0)       /* it was given up and the job dropped: this core does it */
            eng_part_b();
    } else {
        eng_part_a();
        eng_part_b();
    }
    eng_end(lr, n, gain_now);
}
void app_panic(void) { eng_panic(); }

void app_init(void)
{
    int t;
    eng_init();
    seq_init();
    for (t = 0; t < NTRACK; t++)
        midi_patch[t] = -1;
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || !settings_ok())
        settings_defaults();
    if (set.cores == 2u && plat_cpu2_set(1) == 0)
        cores_now = 2;
    seq_midi_out(set.midi_out);
    seq_track(set.track);
    project_open(set.slot);
}

/* --------------------------------------------------------------------- UI --- */
enum { HOLD_NONE, HOLD_PAT, HOLD_SCENE, HOLD_CHAIN, HOLD_MUTE, HOLD_ARP, HOLD_SEQ, HOLD_GLO, HOLD_EDIT };
static struct {
    char msg[2][24];                     /* a short message for a moment: a knob's value, a patch's name */
    uint32_t msg_until;
    uint32_t keys, btn;                  /* held at the last input */
    uint8_t key_track[NKEYS], key_note[NKEYS];   /* what each held key started, so it ends the same note */
    uint32_t key_down_at[NKEYS];         /* with a page held: when the key went down (a second's hold acts) */
    uint8_t key_long[NKEYS];             /* that key's hold has acted */
    uint32_t btn_at[NB];                 /* when each button went down */
    uint8_t btn_used[NB];                /* something was done while it was held: no tap when it comes up */
    uint8_t step_mode, page;
    int8_t step_held;                    /* step mode: the step held (its key), -1 none */
    uint8_t step_turned, step_was_empty;
    uint8_t lane[NTRACK];                /* drums in step mode: the part the steps place */
    step_t last[NTRACK];                 /* what an empty step gets: the last thing played or placed on the track */
    uint8_t target;                      /* GLO held: the project SAVE saves into, PRESET picks */
    uint8_t target_turned;               /* PRESET turned during this hold: GLO let go opens the target */
    int8_t confirm;                      /* the project asked for while this one had changes: asked again, opened */
    uint32_t taps[4];                    /* GLO tapped: the tempo from the gaps */
    uint8_t ntap;
    uint8_t redraw;
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void int_text(int32_t v, char *b)     /* signed, no padding */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void note_name(int note, char *b)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char t[8];
    b[0] = 0;
    text_cat(b, N[note % 12]);
    int_text(note / 12 - 1, t);
    text_cat(b, t);
}
void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {                          /* and to a screen reader (announce.c, fm1-reader) */
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}
static void say_num(const char *what, int32_t v, const char *unit)
{
    char t[24];
    int_text(v, t);
    if (unit)
        text_cat(t, unit);
    app_say(what, t);
}
static void speak(void)                      /* a screen reader that has just started listening hears the screen */
{
    char t[64];
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME " " APP_TAGLINE ". ");
        text_cat(t, TRACK_NAME[set.track]);
        text_cat(t, " ");
        text_cat(t, eng_patch_name(set.track, snd.patch[set.track]));
        text_cat(t, seq_playing() ? ". PLAYING" : ". STOPPED");
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}
static void say_track(void) { app_say(TRACK_NAME[set.track], eng_patch_name(set.track, snd.patch[set.track])); }
static void touch_project(void) { dirty = 1; }

static int hold_page(uint32_t btn)       /* the page a held button opens, for the keys, the encoders and the screen */
{
    if (btn >> B_HOME & 1u) return HOLD_PAT;
    if (btn >> B_ENV & 1u) return HOLD_SCENE;
    if (btn >> B_LFO & 1u) return HOLD_CHAIN;
    if (btn >> B_FX & 1u) return HOLD_MUTE;
    if (btn >> B_ARP & 1u) return HOLD_ARP;
    if (btn >> B_SEQ & 1u) return HOLD_SEQ;
    if (btn >> B_GLO & 1u) return HOLD_GLO;
    if (btn >> B_EDIT & 1u) return HOLD_EDIT;
    return HOLD_NONE;
}
static void mark_used(uint32_t btn)      /* the held buttons have been used: they do not act as taps */
{
    uint32_t k;
    for (k = 0; k < NB; k++)
        if (btn >> k & 1u)
            ui.btn_used[k] = 1;
}

static void keys_release(void)           /* the held keys' notes ended (the track or the octave is changing) */
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        if ((ui.keys >> k & 1u) && ui.key_note[k])
            seq_key(ui.key_track[k], ui.key_note[k], 0);
    memset(ui.key_note, 0, sizeof ui.key_note);
}

static int white_index(uint32_t k)       /* key k's place among the white keys, -1 a black key */
{
    int i;
    for (i = 0; i < 16; i++)
        if (WK[i] == k)
            return i;
    return -1;
}
static int black_index(uint32_t k)
{
    int i;
    for (i = 0; i < 11; i++)
        if (BK[i] == k)
            return i;
    return -1;
}

/* ------------------------------------------------------------- steps --- */
static pattern_t *pat_now(void) { return &seq_pat[set.track][seq_song.cur[set.track]]; }
static pat_hdr_t *hdr_now(void) { return &seq_song.hdr[set.track][seq_song.cur[set.track]]; }
static int step_has_lane(const step_t *s, int note)
{
    int k;
    for (k = 0; k < SEQ_NOTES; k++)
        if (s->n[k] == note)
            return 1;
    return 0;
}
static int lane_note(void) { return TRACK_BASE[T_DRUMS] + ui.lane[T_DRUMS]; }
static void step_say(const step_t *s, int idx)
{
    char a[16] = "STEP ", b[24], t[8];
    int_text(idx + 1, t);
    text_cat(a, t);
    if (!s->n[0]) {
        app_say(a, "EMPTY");
        return;
    }
    if (set.track == T_DRUMS)
        b[0] = 0, text_cat(b, DRUM_NAME[(s->n[0] - TRACK_BASE[T_DRUMS] + 120) % 12]);
    else
        note_name(s->n[0], b);
    text_cat(b, " V");
    int_text(s->vel, t);
    text_cat(b, t);
    app_say(a, b);
}
static void step_key(uint32_t k, int down) /* step mode: a white key down or up */
{
    int w = white_index(k), idx;
    step_t *s;
    if (w < 0)
        return;
    idx = ui.page * 16 + w;
    if (idx >= hdr_now()->len)
        return;
    s = &pat_now()->s[idx];
    if (down) {
        ui.step_held = (int8_t)idx;
        ui.step_turned = 0;
        ui.step_was_empty = set.track == T_DRUMS ? !step_has_lane(s, lane_note()) : !s->n[0];
        if (ui.step_was_empty) {         /* an empty step: filled at once, so it can be heard and turned */
            if (set.track == T_DRUMS) {
                int j;
                if (!s->n[0]) {
                    *s = seq_step_default(lane_note());
                } else {
                    for (j = 0; j < SEQ_NOTES && s->n[j]; j++) {
                    }
                    s->n[j < SEQ_NOTES ? j : SEQ_NOTES - 1] = (uint8_t)lane_note();
                }
            } else {
                *s = ui.last[set.track];
                if (!s->n[0])
                    *s = seq_step_default(TRACK_BASE[set.track] + 12 + 12 * snd.octave[set.track]);
            }
            touch_project();
        }
        step_say(s, idx);
    } else if (ui.step_held == idx) {
        if (!ui.step_was_empty && !ui.step_turned) {   /* a filled step tapped: emptied (drums: the part taken out) */
            if (set.track == T_DRUMS && s->n[1]) {
                int j, m = 0;
                uint8_t keep[SEQ_NOTES] = {0};
                for (j = 0; j < SEQ_NOTES; j++)
                    if (s->n[j] && s->n[j] != lane_note())
                        keep[m++] = s->n[j];
                memcpy(s->n, keep, sizeof keep);
            } else {
                memset(s, 0, sizeof *s);
            }
            touch_project();
            app_say("STEP", "CLEARED");
        } else if (set.track != T_DRUMS) {
            ui.last[set.track] = *s;
        }
        ui.step_held = -1;
    }
}
static void step_turn(int enc, int32_t e)  /* a step held: the five step knobs */
{
    step_t *s = &pat_now()->s[ui.step_held];
    char t[24];
    int k;
    ui.step_turned = 1;
    touch_project();
    switch (enc) {
    case EN_K1:                          /* pitch: the whole step, a semitone a detent */
        if (set.track == T_DRUMS) {
            ui.lane[T_DRUMS] = (uint8_t)(((ui.lane[T_DRUMS] + e) % 12 + 12) % 12);
            s->n[0] = (uint8_t)lane_note();
            app_say("PART", DRUM_NAME[ui.lane[T_DRUMS]]);
            return;
        }
        for (k = 0; k < SEQ_NOTES && s->n[k]; k++)
            s->n[k] = (uint8_t)clamp(s->n[k] + e, 1, 127);
        note_name(s->n[0], t);
        app_say("PITCH", t);
        break;
    case EN_K2:
        s->gate = (uint8_t)clamp(s->gate + e, 1, 255);
        say_num("GATE", s->gate * 100 / 8, "%");
        break;
    case EN_K3:
        s->vel = (uint8_t)clamp(s->vel + 4 * e, 1, 127);
        say_num("VELOCITY", s->vel, 0);
        break;
    case EN_K4:
        s->shift = (int8_t)clamp(s->shift + e, -11, 11);
        say_num("SHIFT", s->shift * 100 / 24, "%");
        break;
    case EN_ALGO:
        s->chance = (uint8_t)clamp(s->chance + 5 * e, 5, 100);
        say_num("CHANCE", s->chance, "%");
        break;
    default:
        break;
    }
}

/* ----------------------------------------------------------- the knobs --- */
static void knob_turn(int k, int32_t e)
{
    uint8_t *v = k <= K_MOTION ? &snd.tm[set.track][k] : &snd.knob[k];
    *v = (uint8_t)clamp((int32_t)*v + e, 0, KNOB_STEPS);
    eng_knob(set.track, k, (float)*v / KNOB_STEPS);
    touch_project();
    say_num(KNOB_NAME[k], (int32_t)*v * 100 / KNOB_STEPS, 0);
}

static void turn(int enc, int32_t e, int page)
{
    play_t *pl = &seq_song.play[set.track];
    pat_hdr_t *h = hdr_now();
    char t[24];
    if (ui.step_mode && ui.step_held >= 0 && enc != EN_SELECT && enc != EN_PRESET) {
        step_turn(enc, e);
        return;
    }
    switch (page) {
    case HOLD_SEQ:
        if (enc == EN_ALGO) {
            h->len = (uint8_t)clamp(h->len + e, 1, SEQ_STEPS);
            if (ui.page * 16 >= h->len)
                ui.page = (uint8_t)((h->len - 1) / 16);
            say_num("LENGTH", h->len, 0);
        } else if (enc == EN_PRESET) {
            h->dir = (uint8_t)(((h->dir + e) % SEQ_NDIR + SEQ_NDIR) % SEQ_NDIR);
            app_say("DIRECTION", DIR_NAME[h->dir]);
        } else if (enc == EN_K1) {
            h->swing = (uint8_t)clamp(h->swing + e, 0, 12);
            say_num("SWING", 50 + h->swing * 25 / 12, "%");
        } else {
            return;
        }
        touch_project();
        return;
    case HOLD_ARP:
        if (enc == EN_PRESET) {
            pl->mode = (uint8_t)(((pl->mode + e) % ARP_NMODE + ARP_NMODE) % ARP_NMODE);
            app_say("ARP", ARP_NAME[pl->mode]);
        } else if (enc == EN_ALGO) {
            pl->rate = (uint8_t)clamp(pl->rate + e, 0, ARP_NRATE - 1);
            app_say("RATE", RATE_NAME[pl->rate]);
        } else if (enc == EN_K1) {
            pl->octaves = (uint8_t)clamp(pl->octaves + e, 1, 4);
            say_num("OCTAVES", pl->octaves, 0);
        } else if (enc == EN_K2) {
            pl->gate = (uint8_t)clamp(pl->gate + 5 * e, 10, 100);
            say_num("ARP GATE", pl->gate, "%");
        } else if (enc == EN_K3) {
            pl->latch = e > 0;
            app_say("LATCH", pl->latch ? "ON" : "OFF");
        } else {
            return;
        }
        touch_project();
        return;
    case HOLD_GLO:
        if (enc == EN_ALGO) {
            snd.bpm = (uint8_t)clamp(snd.bpm + e, 40, 240);
            seq_tempo((float)snd.bpm);
            touch_project();
            say_num("TEMPO", snd.bpm, 0);
        } else if (enc == EN_PRESET) {
            ui.target = (uint8_t)clamp(ui.target + e, 0, PROJ_SLOTS - 1);
            ui.target_turned = 1;
            say_num("PROJECT", ui.target + 1, ui.target == set.slot ? " OPEN" :
                    set.bank_of[ui.target] == 0xFFu ? " EMPTY" : 0);
        } else if (enc == EN_K1) {
            seq_song.quant = e > 0;
            touch_project();
            app_say("RECORD", seq_song.quant ? "ON THE STEPS" : "AS PLAYED");
        } else if (enc == EN_K2) {
            set.chan[set.track] = (uint8_t)clamp(set.chan[set.track] + e, 0, 15);
            plat_store_save(OBJ_SETTINGS, &set, sizeof set);
            t[0] = 0;
            text_cat(t, TRACK_NAME[set.track]);
            text_cat(t, " CHANNEL");
            say_num(t, set.chan[set.track] + 1, 0);
        } else if (enc == EN_K3) {
            set.cores = e > 0 ? 2 : 1;
            plat_store_save(OBJ_SETTINGS, &set, sizeof set);
            app_say(set.cores == 2u ? "TWO CORES" : "ONE CORE", "FROM THE NEXT START");
        } else if (enc == EN_K4) {
            set.midi_out = (uint8_t)clamp(set.midi_out + e, 0, 2);
            seq_midi_out(set.midi_out);
            plat_store_save(OBJ_SETTINGS, &set, sizeof set);
            app_say("MIDI OUT", OUT_NAME[set.midi_out]);
        }
        return;
    case HOLD_CHAIN:
        if (enc == EN_ALGO && seq_song.chain_n) {
            seq_chain_bars((int)e);
            touch_project();
            say_num("BARS", seq_song.chain[seq_song.chain_n - 1u].bars, 0);
        }
        return;
    case HOLD_EDIT:
        if (enc >= EN_K1) {
            static const uint8_t FX[4] = {K_PHASER, K_CHORUS, K_GRIT, K_ECHO};
            knob_turn(FX[enc - EN_K1], e);
        }
        return;
    default:
        break;
    }
    switch (enc) {
    case EN_SELECT:
        keys_release();
        set.track = (uint8_t)clamp((int32_t)set.track + e, 0, NTRACK - 1);
        seq_track(set.track);
        ui.step_held = -1;
        if (ui.page * 16 >= hdr_now()->len)
            ui.page = 0;
        say_track();
        break;
    case EN_PRESET: {
        int n = eng_patch_count(set.track);
        snd.patch[set.track] = (uint8_t)((((int32_t)snd.patch[set.track] + e) % n + n) % n);
        eng_patch(set.track, snd.patch[set.track]);
        touch_project();
        say_track();
        break;
    }
    case EN_ALGO:
        if (ui.step_mode && set.track == T_DRUMS) {   /* step mode on the drums: the part the steps place */
            ui.lane[T_DRUMS] = (uint8_t)(((ui.lane[T_DRUMS] + e) % 12 + 12) % 12);
            app_say("PART", DRUM_NAME[ui.lane[T_DRUMS]]);
        } else if (seq_recording() && !seq_playing()) {   /* recording a step at a time: the step it writes */
            seq_cursor_move((int)e);
            say_num("STEP", seq_cursor() + 1, 0);
        } else {
            knob_turn(K_SPACE, e);
        }
        break;
    default: {
        static const uint8_t PLAIN[4] = {K_TONE, K_MOTION, K_PUMP, K_SWEEP};
        knob_turn(PLAIN[enc - EN_K1], e);
        break;
    }
    }
}

/* ------------------------------------------------------------- buttons --- */
static void button_down(uint32_t k, uint32_t btn)
{
    int page = hold_page(btn & ~(1u << k));
    switch (k) {
    case B_PLAY:
        seq_play(!seq_playing());
        app_say(seq_playing() ? "PLAY" : "STOP", "");
        break;
    case B_REC:
        if (page == HOLD_PAT) {          /* HOME held: this pattern emptied */
            seq_pat_clear(set.track, seq_song.cur[set.track]);
            touch_project();
            mark_used(btn);
            app_say("PATTERN", "CLEARED");
            break;
        }
        seq_rec(!seq_recording());
        app_say("RECORD", seq_recording() ? (seq_playing() ? "ON" : "A STEP AT A TIME") : "OFF");
        break;
    case B_SAVE: {
        int slot = page == HOLD_GLO ? ui.target : set.slot;
        app_say("SAVING", "");
        if (page == HOLD_GLO) {
            mark_used(btn);
            ui.target_turned = 0;        /* saved there: it is open now, nothing to open on letting go */
        }
        if (project_save(slot) == 0)
            say_num("SAVED", slot + 1, 0);
        else
            app_say("SAVE", "FAILED");
        break;
    }
    case B_OCTDN:
    case B_OCTUP: {
        int32_t d = k == B_OCTUP ? 1 : -1, o;
        char t[4];
        if (page == HOLD_CHAIN && k == B_OCTDN) {
            seq_chain_drop();
            touch_project();
            mark_used(btn);
            say_num("CHAIN", seq_song.chain_n, " SCENES");
            break;
        }
        if (ui.step_mode && ui.step_held >= 0 && set.track != T_DRUMS) {   /* a step held: an octave */
            step_t *s = &pat_now()->s[ui.step_held];
            int j;
            for (j = 0; j < SEQ_NOTES && s->n[j]; j++)
                s->n[j] = (uint8_t)clamp(s->n[j] + 12 * d, 1, 127);
            ui.step_turned = 1;
            touch_project();
            step_say(s, ui.step_held);
            break;
        }
        o = clamp(snd.octave[set.track] + d, -2, 2);
        if (o != snd.octave[set.track]) {
            keys_release();
            snd.octave[set.track] = (int8_t)o;
            touch_project();
        }
        t[0] = o < 0 ? '-' : '+';
        t[1] = (char)('0' + (o < 0 ? -o : o));
        t[2] = 0;
        app_say("OCTAVE", t);
        break;
    }
    default:
        break;
    }
}

static void tap_tempo(void)
{
    uint32_t now = plat_ms(), i, sum = 0;
    if (ui.ntap && now - ui.taps[ui.ntap - 1u] > 2000u)
        ui.ntap = 0;
    if (ui.ntap == 4u) {
        ui.taps[0] = ui.taps[1], ui.taps[1] = ui.taps[2], ui.taps[2] = ui.taps[3];
        ui.ntap = 3;
    }
    ui.taps[ui.ntap++] = now;
    if (ui.ntap < 2u) {
        app_say("TAP", "AGAIN FOR THE TEMPO");
        return;
    }
    for (i = 1; i < ui.ntap; i++)
        sum += ui.taps[i] - ui.taps[i - 1u];
    snd.bpm = (uint8_t)clamp((int32_t)(60000u * (ui.ntap - 1u) / sum), 40, 240);
    seq_tempo((float)snd.bpm);
    touch_project();
    say_num("TEMPO", snd.bpm, 0);
}

static void button_tap(uint32_t k)       /* a page button let go without having been used */
{
    play_t *pl = &seq_song.play[set.track];
    switch (k) {
    case B_HOME:
        eng_panic();
        seq_panic();
        keys_release();
        app_say("ALL NOTES", "OFF");
        break;
    case B_SEQ:
        ui.step_mode = !ui.step_mode;
        ui.step_held = -1;
        keys_release();
        app_say("STEPS", ui.step_mode ? "ON THE KEYS" : "OFF");
        break;
    case B_ARP:
        pl->on = !pl->on;
        keys_release();
        touch_project();
        app_say("ARP", pl->on ? ARP_NAME[pl->mode] : "OFF");
        break;
    case B_SEL: {                        /* chord mode: the keys held become the chord */
        uint32_t i, n = 0;
        int lo = 128;
        uint8_t ns[SEQ_NOTES];
        for (i = 0; i < NKEYS && n < SEQ_NOTES; i++)
            if ((ui.keys >> i & 1u) && ui.key_note[i]) {
                ns[n++] = ui.key_note[i];
                if (ui.key_note[i] < lo)
                    lo = ui.key_note[i];
            }
        if (n >= 2u) {
            uint32_t a, b;
            for (a = 1; a < n; a++)       /* lowest first */
                for (b = a; b > 0 && ns[b - 1] > ns[b]; b--) {
                    uint8_t x = ns[b];
                    ns[b] = ns[b - 1];
                    ns[b - 1] = x;
                }
            for (i = 0; i < n; i++)
                pl->chord[i] = (int8_t)(ns[i] - lo);
            pl->chord_n = (uint8_t)n;
            pl->chord_on = 1;
            keys_release();
            app_say("CHORD", "KEPT");
        } else if (pl->chord_n) {
            pl->chord_on = !pl->chord_on;
            app_say("CHORD", pl->chord_on ? "ON" : "OFF");
        } else {
            app_say("CHORD", "HOLD ITS KEYS FIRST");
        }
        touch_project();
        break;
    }
    case B_LFO:
        if (!seq_song.chain_n) {
            app_say("CHAIN", "HOLD LFO, THEN SCENES");
            break;
        }
        seq_song.chain_on = !seq_song.chain_on;
        touch_project();
        app_say("CHAIN", seq_song.chain_on ? "ON" : "OFF");
        break;
    case B_FX:
        seq_mute(set.track, !(seq_song.mute >> set.track & 1u));
        touch_project();
        app_say(TRACK_NAME[set.track], seq_song.mute >> set.track & 1u ? "MUTED" : "PLAYS");
        break;
    case B_GLO:
        tap_tempo();
        break;
    case B_ENV:
        app_say("SCENES", "HOLD ENV, THEN A KEY");
        break;
    case B_EDIT:
        app_say("EFFECTS", "HOLD EDIT, TURN K1-K4");
        break;
    default:
        break;
    }
}

/* a white key with a page held: down (and its second's hold), up */
static void page_key(int page, int w, int phase)   /* phase 0 down, 1 tapped (up soon), 2 held a second */
{
    switch (page) {
    case HOLD_PAT:
        if (phase == 1) {
            seq_select(set.track, w);
            say_num("PATTERN", w + 1, seq_playing() ? " AT THE BAR" : 0);
            touch_project();
        } else if (phase == 2) {
            seq_pat_copy(set.track, seq_song.cur[set.track], w);
            seq_select(set.track, w);
            touch_project();
            say_num("COPIED TO", w + 1, 0);
        }
        break;
    case HOLD_SCENE:
        if (phase == 1) {
            if (seq_song.scene_used >> w & 1u) {
                seq_scene_recall(w);
                say_num("SCENE", w + 1, seq_playing() ? " AT THE BAR" : 0);
            } else {
                say_num("SCENE", w + 1, " EMPTY: HOLD TO STORE");
            }
        } else if (phase == 2) {
            seq_scene_store(w);
            touch_project();
            say_num("SCENE", w + 1, " STORED");
        }
        break;
    case HOLD_CHAIN:
        if (phase == 0) {
            if (!(seq_song.scene_used >> w & 1u)) {
                say_num("SCENE", w + 1, " EMPTY");
            } else if (seq_song.chain_n >= SEQ_CHAIN) {
                app_say("CHAIN", "FULL");
            } else {
                char t[24] = "CHAIN ", n[8];
                seq_chain_add(w);
                touch_project();
                int_text(seq_song.chain_n, n);
                text_cat(t, n);
                say_num(t, w + 1, 0);
            }
        }
        break;
    case HOLD_MUTE:
        if (phase == 0 && w < NTRACK) {
            seq_mute(w, !(seq_song.mute >> w & 1u));
            touch_project();
            app_say(TRACK_NAME[w], seq_song.mute >> w & 1u ? "MUTED" : "PLAYS");
        }
        break;
    default:
        break;
    }
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, prs = 0, now = plat_ms();
    int32_t e;
    int page;
    touch_poll();                        /* the sensed keys' presses, velocities and pressure */
    keys = touch_keys();
    /* buttons down first: a page opens before the keys are read */
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if ((btn & bit) && !(ui.btn & bit)) {
            ui.btn_at[k] = now;
            ui.btn_used[k] = 0;
            button_down(k, btn);
            ui.redraw = 1;
        } else if (!(btn & bit) && (ui.btn & bit)) {
            if (k == B_GLO && ui.target_turned) {   /* a project chosen: opened (asked twice if this one changed) */
                ui.target_turned = 0;
                if (ui.target == set.slot) {
                    ui.confirm = -1;
                } else if (dirty && ui.confirm != (int8_t)ui.target) {
                    ui.confirm = (int8_t)ui.target;
                    app_say("NOT SAVED", "SAVE, OR CHOOSE IT AGAIN");
                } else {
                    keys_release();
                    project_open(ui.target);
                    plat_store_save(OBJ_SETTINGS, &set, sizeof set);
                    ui.confirm = -1;
                    say_num("PROJECT", set.slot + 1, set.bank_of[set.slot] == 0xFFu ? " NEW" : " OPEN");
                }
            }
            /* a tap is short: a longer press was a look at its page */
            if (!ui.btn_used[k] && now - ui.btn_at[k] < (k == B_GLO ? 250u : 350u))
                button_tap(k);
            ui.redraw = 1;
        }
    }
    page = hold_page(btn);
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        int w = white_index(k), b = black_index(k);
        int down = (keys & bit) && !(ui.keys & bit), up = !(keys & bit) && (ui.keys & bit);
        if (page != HOLD_NONE && page != HOLD_EDIT && page != HOLD_ARP && page != HOLD_GLO && page != HOLD_SEQ) {
            if (w >= 0) {                /* a page's keys */
                if (down) {
                    ui.key_down_at[k] = now;
                    ui.key_long[k] = 0;
                    mark_used(btn);
                    page_key(page, w, 0);
                } else if ((keys & bit) && !ui.key_long[k] && now - ui.key_down_at[k] >= 1000u) {
                    ui.key_long[k] = 1;
                    page_key(page, w, 2);
                } else if (up && !ui.key_long[k]) {
                    page_key(page, w, 1);
                }
            }
            continue;
        }
        if (ui.step_mode) {              /* the steps, and four pages */
            if (w >= 0 && (down || up))
                step_key(k, down);
            if (b >= 0 && b < 4 && down) {
                if (b * 16 < hdr_now()->len) {
                    ui.page = (uint8_t)b;
                    say_num("PAGE", b + 1, 0);
                } else {
                    app_say("PAGE", "PAST THE END");
                }
            }
            continue;
        }
        if (down) {
            int32_t note = clamp(TRACK_BASE[set.track] + (int32_t)k + 12 * snd.octave[set.track], 1, 127);
            uint32_t v = touch_vel(k);
            ui.key_track[k] = set.track;
            ui.key_note[k] = (uint8_t)note;
            seq_key(set.track, note, (int)(v ? v : 1u));
            if (set.track == T_DRUMS)
                ui.lane[T_DRUMS] = (uint8_t)((note - TRACK_BASE[T_DRUMS]) % 12);
            else
                ui.last[set.track] = seq_step_default(note);
            if (page != HOLD_NONE)
                mark_used(btn);
        } else if (up && ui.key_note[k]) {
            seq_key(ui.key_track[k], ui.key_note[k], 0);
            ui.key_note[k] = 0;
        }
        if ((keys & bit) && touch_pressure(k) > prs)
            prs = touch_pressure(k);
    }
    {   /* two or more keys down: the step an empty step gets is that chord */
        uint32_t i, n = 0;
        step_t c = seq_step_default(0);
        for (i = 0; i < NKEYS && n < SEQ_NOTES; i++)
            if ((keys >> i & 1u) && ui.key_note[i])
                c.n[n++] = ui.key_note[i];
        if (n >= 2u && set.track != T_DRUMS)
            ui.last[set.track] = c;
    }
    if (keys != ui.keys)
        ui.redraw = 1;
    eng_pressure(set.track, (float)prs * (1.0f / 127.0f));   /* the hardest-pressed key leans on the sound */
    ui.keys = keys;
    ui.btn = btn;
    for (k = 0; k < NE; k++)
        if ((e = plat_enc((int)k)) != 0) {
            if (page != HOLD_NONE)
                mark_used(btn);
            turn((int)k, e, page);
        }
}
void app_input_only(void) { input(); }

/* ---------------------------------------------------------------- lights --- */
static void lights(void)
{
    uint32_t btn = ui.btn, keys = 0, dim_keys = 0, dim_btn = 0, now = plat_ms(), i;
    int page = hold_page(ui.btn), blink = (now / 200u) & 1u;
    const play_t *pl = &seq_song.play[set.track];
    if (dirty)
        btn |= 1u << B_SAVE;
    if (seq_playing())
        btn |= LED_PLAY_GREEN;
    if (seq_recording())
        btn |= 1u << B_REC;
    if (ui.step_mode)
        btn |= 1u << B_SEQ;
    if (pl->on)
        btn |= 1u << B_ARP;
    if (pl->chord_on)
        btn |= 1u << B_SEL;
    if (seq_song.chain_on)
        btn |= 1u << B_LFO;
    if (seq_song.mute >> set.track & 1u)
        btn |= 1u << B_FX;
    if (page == HOLD_PAT) {
        for (i = 0; i < 16u; i++) {
            if (i == seq_song.cur[set.track] || (i == (uint32_t)seq_queued(set.track) && blink))
                keys |= 1u << WK[i];
            else if (!seq_pat_empty(set.track, (int)i))
                dim_keys |= 1u << WK[i];
        }
    } else if (page == HOLD_SCENE || page == HOLD_CHAIN) {
        int cur = page == HOLD_CHAIN && seq_chain_pos() >= 0 ? seq_song.chain[seq_chain_pos()].scene : seq_scene_now();
        for (i = 0; i < 16u; i++) {
            if ((int)i == cur)
                keys |= 1u << WK[i];
            else if (seq_song.scene_used >> i & 1u)
                dim_keys |= 1u << WK[i];
        }
    } else if (page == HOLD_MUTE) {
        for (i = 0; i < NTRACK; i++)
            if (!(seq_song.mute >> i & 1u))
                keys |= 1u << WK[i];
    } else if (ui.step_mode) {
        const pattern_t *p = pat_now();
        const pat_hdr_t *h = hdr_now();
        int at = seq_step_now(set.track);
        for (i = 0; i < 16u; i++) {
            uint32_t idx = ui.page * 16u + i;
            int on;
            if (idx >= h->len)
                continue;
            on = set.track == T_DRUMS ? step_has_lane(&p->s[idx], lane_note()) : p->s[idx].n[0] != 0;
            if ((int)idx == at)
                on = !on;
            if (on)
                keys |= 1u << WK[i];
            else
                dim_keys |= set.track == T_DRUMS && p->s[idx].n[0] ? 1u << WK[i] : 0u;
        }
        for (i = 0; i < 4u; i++)
            if (i * 16u < h->len) {
                if (i == ui.page)
                    keys |= 1u << BK[i];
                else
                    dim_keys |= 1u << BK[i];
            }
    } else {
        keys = ui.keys;
    }
    plat_lights(btn, keys, dim_btn, dim_keys, 1);
}

/* ---------------------------------------------------------------- screen --- */
/* the picture is the patch's visualiser (visual.c); over it, at the top, the track and the patch or a message, and
 * at the bottom what a held page shows, or the steps */
static void strip_begin(int32_t y0)
{
    cv_begin(240, VIS_STRIP, C_BLACK);
    cv_oy = -y0;
    cv_y0 = 0;
    cv_y1 = VIS_STRIP;
}
static void text_shadow(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(x + 1, y + 1, f, s, C_BLACK);
    cv_text(x, y, f, s, c);
}

static void draw_top(void)
{
    uint32_t now = plat_ms();
    uint16_t c = vis_track_rgb(set.track);
    char t[24];
    cv_rect(0, 0, 240, 2, c);
    if (now < ui.msg_until) {
        int32_t x = 0;
        text_shadow(8, 6, &FONT_B, ui.msg[0], C_WHITE);
        x = 8 + text_w(&FONT_B, ui.msg[0]) + 8;
        text_shadow(x, 6, &FONT_B, ui.msg[1], c);
    } else {
        int32_t x = 8 + text_w(&FONT_B, TRACK_NAME[set.track]) + 8;
        text_shadow(8, 6, &FONT_B, TRACK_NAME[set.track], c);
        text_shadow(x, 6, &FONT_B, eng_patch_name(set.track, snd.patch[set.track]), C_WHITE);
        if (seq_playing()) {
            int_text((int32_t)seq_bar() + 1, t);
            text_shadow(232 - text_w(&FONT_S, t), 6, &FONT_S, t, seq_recording() ? RGB(255, 60, 60) : C_WHITE);
        } else if (seq_recording()) {
            text_shadow(200, 6, &FONT_S, "REC", RGB(255, 60, 60));
        }
    }
}

static void cell(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, int filled)
{
    if (filled) {
        cv_rect(x, y, w, h, c);
    } else {
        cv_rect(x, y, w, 1, c);
        cv_rect(x, y + h - 1, w, 1, c);
        cv_rect(x, y, 1, h, c);
        cv_rect(x + w - 1, y, 1, h, c);
    }
}

static void draw_bottom(void)
{
    int page = hold_page(ui.btn), i;
    uint16_t c = vis_track_rgb(set.track), dim = RGB(70, 70, 90);
    int32_t y = 240 - 30;
    char t[32];
    const pat_hdr_t *h = hdr_now();
    cv_rect(0, y - 4, 240, 34, RGB(6, 8, 24));
    if (page == HOLD_PAT || page == HOLD_SCENE || page == HOLD_CHAIN || page == HOLD_MUTE) {
        static const char *const TITLE[] = {"", "PATTERNS", "SCENES", "CHAIN", "MUTE"};
        cv_text(6, y - 2, &FONT_XS, TITLE[page], C_WHITE);
        for (i = 0; i < 16; i++) {
            int32_t x = 6 + i * 14;
            int on = 0, cur = 0;
            if (page == HOLD_PAT) {
                on = !seq_pat_empty(set.track, i);
                cur = i == seq_song.cur[set.track];
            } else if (page == HOLD_SCENE) {
                on = seq_song.scene_used >> i & 1u;
                cur = i == seq_scene_now();
            } else if (page == HOLD_CHAIN) {
                on = i < seq_song.chain_n;
                cur = i == seq_chain_pos();
            } else {
                if (i >= NTRACK)
                    break;
                on = !(seq_song.mute >> i & 1u);
            }
            cell(x, y + 12, 12, 12, cur ? C_WHITE : page == HOLD_MUTE ? vis_track_rgb(i) : c, on || cur);
        }
        if (page == HOLD_CHAIN && seq_song.chain_n) {
            t[0] = 0;
            for (i = 0; i < seq_song.chain_n && i < 8; i++) {
                char n[8];
                int_text(seq_song.chain[i].scene + 1, n);
                text_cat(t, n);
                text_cat(t, i + 1 < seq_song.chain_n ? " " : "");
            }
            cv_text(60, y - 2, &FONT_XS, t, c);
        }
        return;
    }
    if (page == HOLD_GLO) {
        char a[48] = "";
        int_text((int32_t)(seq_tempo_get() + 0.5f), t);
        text_cat(a, t);
        text_cat(a, seq_clock_ext() ? " BPM MIDI  " : " BPM  ");
        int_text((int32_t)plat_cpu_pct(), t);
        text_cat(a, "CPU ");
        text_cat(a, t);
        text_cat(a, cores_now == 2u ? "% TWO CORES" : "% ONE CORE");
        cv_text(6, y - 2, &FONT_XS, a, C_WHITE);
        a[0] = 0;
        text_cat(a, "PROJECT ");
        int_text(set.slot + 1, t);
        text_cat(a, t);
        text_cat(a, "  CH ");
        int_text(set.chan[set.track] + 1, t);
        text_cat(a, t);
        text_cat(a, "  OUT ");
        text_cat(a, OUT_NAME[set.midi_out]);
        cv_text(6, y + 12, &FONT_XS, a, c);
        return;
    }
    if (page == HOLD_ARP) {
        const play_t *pl = &seq_song.play[set.track];
        char a[48] = "ARP ";
        text_cat(a, pl->on ? ARP_NAME[pl->mode] : "OFF");
        text_cat(a, "  ");
        text_cat(a, RATE_NAME[pl->rate]);
        text_cat(a, "  OCT ");
        int_text(pl->octaves, t);
        text_cat(a, t);
        text_cat(a, pl->latch ? "  LATCH" : "");
        cv_text(6, y + 4, &FONT_XS, a, C_WHITE);
        return;
    }
    {   /* the steps of this page; the step playing lit */
        const pattern_t *p = pat_now();
        int at = seq_step_now(set.track), base = ui.page * 16;
        char a[40] = "P";
        int_text(seq_song.cur[set.track] + 1, t);
        text_cat(a, t);
        if (page == HOLD_SEQ) {
            text_cat(a, "  LEN ");
            int_text(h->len, t);
            text_cat(a, t);
            text_cat(a, "  ");
            text_cat(a, DIR_NAME[h->dir]);
            text_cat(a, "  SWING ");
            int_text(50 + h->swing * 25 / 12, t);
            text_cat(a, t);
        } else if (ui.step_mode) {
            text_cat(a, "  PAGE ");
            int_text(ui.page + 1, t);
            text_cat(a, t);
            if (set.track == T_DRUMS) {
                text_cat(a, "  ");
                text_cat(a, DRUM_NAME[ui.lane[T_DRUMS]]);
            }
        }
        cv_text(6, y - 2, &FONT_XS, a, ui.step_mode ? C_WHITE : RGB(150, 150, 170));
        for (i = 0; i < 16; i++) {
            int idx = base + i, on;
            int32_t x = 6 + i * 14;
            if (idx >= h->len) {
                cv_rect(x + 5, y + 17, 2, 2, dim);
                continue;
            }
            on = p->s[idx].n[0] != 0;
            if (idx == at)
                cv_rect(x - 1, y + 11, 14, 14, C_WHITE);
            cell(x, y + 12, 12, 12, on ? c : dim, on);
            if (on && p->s[idx].chance < 100)
                cv_rect(x + 3, y + 15, 6, 6, RGB(6, 8, 24));
        }
    }
}

static void draw(void)
{
    vis_state_t vs;
    int32_t y;
    vis_gather(&vs, set.track, snd.patch[set.track]);
    vs.held = ui.keys != 0 && !ui.step_mode;
    for (y = 0; y < 240; y += VIS_STRIP) {
        strip_begin(y);
        vis_draw(&vs, y);
        if (y < 30)
            draw_top();
        if (y + VIS_STRIP > 200)
            draw_bottom();
        cv_oy = 0;
        cv_blit(0, (uint32_t)y);
    }
}

void app_ui_init(void)
{
    touch_on(1);                         /* velocity and pressure: the keys' readings on */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.step_held = -1;
    ui.target = set.slot;
    ui.confirm = -1;
    vis_init();
    ui.redraw = 1;
}
void app_frame(void)
{
    static uint32_t last_draw;
    uint32_t now = plat_ms();
    int t;
    input();
    for (t = 0; t < NTRACK; t++)         /* a program change from MIDI */
        if (midi_patch[t] >= 0) {
            snd.patch[t] = (uint8_t)midi_patch[t];
            eng_patch(t, snd.patch[t]);
            midi_patch[t] = -1;
            touch_project();
            if (t == set.track)
                say_track();
        }
    if (midi_knob_seen) {                /* a controller's knob: the project's copy follows the engine's */
        int k;
        midi_knob_seen = 0;
        for (t = 0; t < NTRACK; t++) {
            snd.tm[t][0] = (uint8_t)(eng_knob_get(t, K_TONE) * KNOB_STEPS + 0.5f);
            snd.tm[t][1] = (uint8_t)(eng_knob_get(t, K_MOTION) * KNOB_STEPS + 0.5f);
        }
        for (k = K_PUMP; k < NKNOB; k++)
            snd.knob[k] = (uint8_t)(eng_knob_get(0, k) * KNOB_STEPS + 0.5f);
        touch_project();
    }
    speak();
    lights();
    if (ui.msg_until && now >= ui.msg_until)
        ui.msg_until = 0;
    if (now - last_draw >= 33u) {        /* the pictures move: about thirty a second */
        ui.redraw = 0;
        last_draw = now;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "track"))
        *got = set.track;
    else if (!strcmp(what, "patch"))
        *got = snd.patch[set.track];
    else if (!strcmp(what, "octave"))
        *got = snd.octave[set.track];
    else if (!strcmp(what, "dirty"))
        *got = dirty;
    else if (!strcmp(what, "note"))
        *got = eng_note_now(set.track);
    else if (!strcmp(what, "voices"))
        *got = (int)eng_voices_on();
    else if (!strcmp(what, "playing"))
        *got = seq_playing();
    else if (!strcmp(what, "pattern"))
        *got = seq_song.cur[set.track];
    else if (!strcmp(what, "steps")) {   /* the steps of this pattern with notes */
        int i, n = 0;
        for (i = 0; i < SEQ_STEPS; i++)
            n += pat_now()->s[i].n[0] != 0;
        *got = n;
    } else if (!strncmp(what, "step", 4)) {   /* stepN: step N's first note, 0 none */
        *got = pat_now()->s[atoi(what + 4) % SEQ_STEPS].n[0];
    } else if (!strcmp(what, "len"))
        *got = hdr_now()->len;
    else if (!strcmp(what, "scene"))
        *got = seq_scene_now();
    else if (!strcmp(what, "chain"))
        *got = seq_song.chain_n;
    else if (!strcmp(what, "slot"))
        *got = set.slot;
    else if (!strcmp(what, "bpm"))
        *got = (int)(seq_tempo_get() + 0.5f);
    else if (!strcmp(what, "arp"))
        *got = seq_song.play[set.track].on;
    else if (!strcmp(what, "chord"))
        *got = seq_song.play[set.track].chord_on ? seq_song.play[set.track].chord_n : 0;
    else if (!strcmp(what, "mute"))
        *got = seq_song.mute;
    else if (!strncmp(what, "knob", 4)) {    /* knobN: knob N of this track, 0..64 */
        int k = atoi(what + 4) % NKNOB;
        *got = k <= K_MOTION ? snd.tm[set.track][k] : snd.knob[k];
    } else
        return 0;
    return 1;
}
#endif
