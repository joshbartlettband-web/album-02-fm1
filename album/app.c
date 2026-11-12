/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alive-Stable-7254 */
/* Album 02: the screen, the controls and MIDI around the sound (dsp/engine.h). Laid out after fm1-base's template
 * (Charles Vestal, MPL-2.0). Part of the unity build (base/device/unity.c, base/host/plat_host.c), after the
 * platform: gfx.c (the canvas, the fonts) and announce.c are in scope.
 *
 * So far: the five tracks played from the keys and over MIDI, a patch each, the five knobs and the four effects.
 * The pictures, the patterns and the second core come next (docs/PLAN.md). */
#include "fastmath.h"
#include "engine.h"

#define SET_MAGIC 0x41323031u            /* "A201": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    uint8_t track;
    int8_t octave[NTRACK];
    uint8_t patch[NTRACK];
    uint8_t chan[NTRACK];                /* each track's MIDI channel, 0..15 */
    uint8_t tm[NTRACK][2];               /* TONE and MOTION, a track each, 0..64 */
    uint8_t knob[NKNOB];                 /* the rest (K_PUMP ..), 0..64 */
    uint8_t bpm;
    uint8_t rsv[2];
} settings_t;
static settings_t set;
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit */

static const char *const TRACK_NAME[NTRACK] = {"DRUMS", "BASS", "CHORDS", "LEAD", "VOX"};
static const char *const KNOB_NAME[NKNOB] = {"TONE", "MOTION", "PUMP", "SWEEP", "SPACE", "PHASER", "CHORUS", "GRIT",
                                             "ECHO"};
static const uint16_t TRACK_RGB[NTRACK] = {RGB(255, 196, 40), RGB(255, 60, 170), RGB(40, 210, 255), RGB(255, 120, 40),
                                           RGB(170, 120, 255)};
static const int8_t TRACK_BASE[NTRACK] = {36, 29, 53, 53, 53};   /* the lowest key's note at octave 0 */
#define KNOB_STEPS 64

/* ------------------------------------------------------------------ MIDI --- */
static volatile uint32_t midi_count;
static int chan_track(uint32_t ch)       /* the track that listens on a channel; channel 10 is always the drums */
{
    int t;
    for (t = 0; t < NTRACK; t++)
        if (set.chan[t] == ch)
            return t;
    return ch == 9u ? T_DRUMS : -1;
}
static void midi_in(void)                /* the render: the packets that came in, played at once */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        int t = st < 0xF0u ? chan_track(st & 15u) : -1;
        if (st != 0xF8u && st != 0xFEu)
            midi_count++;
        if (t < 0)
            continue;
        if ((st & 0xF0u) == 0x90u && d2)
            eng_note_on_now(t, (int)d1, (int)d2);
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            eng_note_off_now(t, (int)d1);
        else if ((st & 0xF0u) == 0xD0u)
            eng_pressure(t, (float)d1 * (1.0f / 127.0f));
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float gain_now;
    float gain = (float)master_q12 * (8300000.0f / 4096.0f);
    gain_now += (gain - gain_now) * 0.25f;   /* MASTER glides: no step at a block's edge */
    midi_in();
    eng_begin(n);
    eng_part_a();
    eng_part_b();
    eng_end(lr, n, gain_now);
}
void app_panic(void) { eng_panic(); }

/* --------------------------------------------------------------- settings --- */
static void settings_apply(void)
{
    int t, k;
    for (t = 0; t < NTRACK; t++) {
        eng_patch(t, set.patch[t]);
        eng_knob(t, K_TONE, (float)set.tm[t][0] / KNOB_STEPS);
        eng_knob(t, K_MOTION, (float)set.tm[t][1] / KNOB_STEPS);
    }
    for (k = K_PUMP; k < NKNOB; k++)
        eng_knob(0, k, (float)set.knob[k] / KNOB_STEPS);
    eng_tempo((float)set.bpm);
}
static void settings_defaults(void)
{
    int t, k;
    memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    set.track = T_CHORDS;
    set.bpm = 123;
    for (t = 0; t < NTRACK; t++) {
        set.chan[t] = (uint8_t)t;        /* channels 1 to 5, and 10 for the drums besides */
        set.tm[t][0] = (uint8_t)(eng_knob_get(t, K_TONE) * KNOB_STEPS + 0.5f);
        set.tm[t][1] = (uint8_t)(eng_knob_get(t, K_MOTION) * KNOB_STEPS + 0.5f);
    }
    for (k = K_PUMP; k < NKNOB; k++)
        set.knob[k] = (uint8_t)(eng_knob_get(0, k) * KNOB_STEPS + 0.5f);
}
static int settings_ok(void)
{
    int t, k;
    if (set.magic != SET_MAGIC || set.track >= NTRACK || set.bpm < 40u || set.bpm > 240u)
        return 0;
    for (t = 0; t < NTRACK; t++)
        if (set.octave[t] < -2 || set.octave[t] > 2 || set.patch[t] >= eng_patch_count(t) || set.chan[t] > 15u ||
            set.tm[t][0] > KNOB_STEPS || set.tm[t][1] > KNOB_STEPS)
            return 0;
    for (k = 0; k < NKNOB; k++)
        if (set.knob[k] > KNOB_STEPS)
            return 0;
    return 1;
}
void app_init(void)
{
    eng_init();
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || !settings_ok())
        settings_defaults();
    settings_apply();
}

/* --------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];                     /* a short message for a moment: a knob's value, a patch's name */
    uint32_t msg_until;
    uint32_t keys, btn;                  /* held at the last input */
    uint8_t key_track[NKEYS], key_note[NKEYS];   /* what each held key started, so it ends the same note */
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
static void speak(void)                      /* a screen reader that has just started listening hears the screen */
{
    char t[64];
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME " " APP_TAGLINE ". ");
        text_cat(t, TRACK_NAME[set.track]);
        text_cat(t, " ");
        text_cat(t, eng_patch_name(set.track, set.patch[set.track]));
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

static void say_track(void) { app_say(TRACK_NAME[set.track], eng_patch_name(set.track, set.patch[set.track])); }

static void keys_release(void)               /* the held keys' notes ended (the track or the octave is changing) */
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        if (ui.keys >> k & 1u)
            eng_note_off(ui.key_track[k], ui.key_note[k]);
    ui.keys = 0;
}

static void knob_turn(int k, int32_t e)      /* one of the nine knobs, e detents */
{
    char t[12];
    uint8_t *v = k <= K_MOTION ? &set.tm[set.track][k] : &set.knob[k];
    *v = (uint8_t)clamp((int32_t)*v + e, 0, KNOB_STEPS);
    eng_knob(set.track, k, (float)*v / KNOB_STEPS);
    set_dirty = 1;
    int_text((int32_t)*v * 100 / KNOB_STEPS, t);
    app_say(KNOB_NAME[k], t);
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, prs = 0;
    int32_t e;
    int edit = (btn >> B_EDIT) & 1u;
    touch_poll();                            /* the sensed keys' presses, velocities and pressure */
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            int32_t note = clamp(TRACK_BASE[set.track] + (int32_t)k + 12 * set.octave[set.track], 0, 127);
            uint32_t v = touch_vel(k);
            ui.key_track[k] = set.track;
            ui.key_note[k] = (uint8_t)note;
            eng_note_on(set.track, note, (int)(v ? v : 1u));
            plat_midi_send(0x09u | (0x90u | set.chan[set.track]) << 8 | (uint32_t)note << 16 | v << 24, TO_USB);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            eng_note_off(ui.key_track[k], ui.key_note[k]);
            plat_midi_send(0x08u | (0x80u | set.chan[ui.key_track[k]]) << 8 | (uint32_t)ui.key_note[k] << 16, TO_USB);
        }
        if ((keys & bit) && touch_pressure(k) > prs)
            prs = touch_pressure(k);
    }
    eng_pressure(set.track, (float)prs * (1.0f / 127.0f));   /* the hardest-pressed key leans on the sound */
    ui.keys = keys;
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (k == B_OCTDN || k == B_OCTUP) {
            int32_t o = clamp(set.octave[set.track] + (k == B_OCTUP ? 1 : -1), -2, 2);
            char t[4] = {o < 0 ? '-' : '+', (char)('0' + (o < 0 ? -o : o)), 0, 0};
            if (o != set.octave[set.track]) {
                keys_release();
                set.octave[set.track] = (int8_t)o;
                set_dirty = 1;
            }
            app_say("OCTAVE", t);
        } else if (k == B_SAVE) {
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SAVED", "");
            } else {
                app_say("SAVE", "FAILED");
            }
        } else if (k == B_HOME) {            /* everything let go: a stuck note's way out */
            eng_panic();
            app_say("ALL NOTES", "OFF");
        }
    }
    ui.btn = btn;
    for (k = 0; k < NE; k++) {
        if ((e = plat_enc((int)k)) == 0)
            continue;
        if (k == EN_SELECT) {                /* the track */
            keys_release();
            set.track = (uint8_t)clamp((int32_t)set.track + e, 0, NTRACK - 1);
            set_dirty = 1;
            say_track();
        } else if (k == EN_PRESET) {         /* this track's patch */
            int n = eng_patch_count(set.track);
            set.patch[set.track] = (uint8_t)((((int32_t)set.patch[set.track] + e) % n + n) % n);
            eng_patch(set.track, set.patch[set.track]);
            set_dirty = 1;
            say_track();
        } else if (k == EN_ALGO) {
            knob_turn(K_SPACE, e);
        } else {                             /* K1 to K4: the knobs, or with EDIT held the effects */
            static const uint8_t PLAIN[4] = {K_TONE, K_MOTION, K_PUMP, K_SWEEP};
            static const uint8_t FX[4] = {K_PHASER, K_CHORUS, K_GRIT, K_ECHO};
            knob_turn(edit ? FX[k - EN_K1] : PLAIN[k - EN_K1], e);
        }
    }
    plat_glow(1);
    plat_leds(btn | (set_dirty ? 1u << B_SAVE : 0u), keys);
}
void app_input_only(void) { input(); }

/* the screen for now: the track and its patch, a bar a track that follows its level, the pump's dip. The pictures
 * replace it */
static void draw(void)
{
    uint32_t now = plat_ms();
    int t;
    cv_begin(240, 48, C_BLACK);
    cv_rect(0, 0, 240, 3, TRACK_RGB[set.track]);
    if (now < ui.msg_until) {
        int32_t x = cv_text(10, 12, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 12, &FONT_B, ui.msg[1], TRACK_RGB[set.track]);
    } else {
        int32_t x = cv_text(10, 12, &FONT_B, TRACK_NAME[set.track], TRACK_RGB[set.track]);
        cv_text(x + 8, 12, &FONT_B, eng_patch_name(set.track, set.patch[set.track]), C_WHITE);
    }
    cv_blit(0, 0);
    cv_begin(240, 144, C_BLACK);
    for (t = 0; t < NTRACK; t++) {
        int32_t h = clamp((int32_t)(eng_level(t) * 130.0f), 2, 130);
        cv_rect(14 + t * 44, 136 - h, 36, h, TRACK_RGB[t]);
        if (t == set.track)
            cv_rect(14 + t * 44, 140, 36, 3, C_WHITE);
    }
    cv_blit(0, 48);
    cv_begin(240, 48, C_BLACK);
    {
        int32_t w = (int32_t)((1.0f - eng_duck()) * 220.0f);
        cv_rect(10, 8, w, 6, C_WHITE);
        for (t = 0; t < NTRACK; t++) {
            char n[8];
            int_text(set.chan[t] + 1, n);
            cv_text(14 + t * 44, 24, &FONT_S, TRACK_NAME[t], t == set.track ? C_WHITE : C_GRAY);
        }
    }
    cv_blit(0, 192);
}

void app_ui_init(void)
{
    touch_on(1);                             /* velocity and pressure: the keys' readings on */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}
void app_frame(void)
{
    static uint32_t last_draw;
    uint32_t now = plat_ms();
    input();
    speak();
    if (ui.msg_until && now >= ui.msg_until)
        ui.msg_until = 0;
    if (ui.redraw || now - last_draw >= 33u) {   /* the levels move: about thirty pictures a second */
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
        *got = set.patch[set.track];
    else if (!strcmp(what, "octave"))
        *got = set.octave[set.track];
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "note"))
        *got = eng_note_now(set.track);
    else if (!strcmp(what, "voices"))
        *got = (int)eng_voices_on();
    else if (!strncmp(what, "knob", 4))      /* knobN: knob N of this track, 0..64 */
        *got = atoi(what + 4) <= K_MOTION ? set.tm[set.track][atoi(what + 4) % 2] : set.knob[atoi(what + 4) % NKNOB];
    else
        return 0;
    return 1;
}
#endif
