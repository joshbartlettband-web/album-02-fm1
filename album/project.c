/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alive-Stable-7254 */
/* Album 02's projects: four, each the sound (patches, knobs, octaves, tempo) and the sequencer (patterns, scenes,
 * the chain, the arpeggiator and chord mode). Included by app.c (the unity build).
 *
 * They live in five banks of the flash's data area, eleven sectors each; the settings object (the store's, torn-
 * write safe) says which bank holds which project. A save writes the project into a bank that holds none, checks
 * it back, and only then points the settings at it: power lost halfway leaves the last save as it was.
 *
 * A bank: the head (written last), the sound, the sequencer's song, then each pattern that is not empty, its track
 * and number first. */
#include "seq.h"

#define PROJ_SLOTS 4
#define PROJ_BANKS 5
#define PROJ_BANK_BYTES (11u * 4096u)
#define PROJ_BASE PLAT_DATA_LO
#define PROJ_MAGIC 0x4A503241u           /* "A2PJ" */
#define PROJ_VERSION 1u
_Static_assert(PROJ_BASE + PROJ_BANKS * PROJ_BANK_BYTES <= 0xDC000u, "the banks stop below the panel's sectors");

typedef struct {
    uint8_t patch[NTRACK];
    int8_t octave[NTRACK];
    uint8_t tm[NTRACK][2];               /* TONE and MOTION, a track each, 0..64 */
    uint8_t knob[NKNOB];                 /* the rest (K_PUMP ..), 0..64 */
    uint8_t bpm;
    uint8_t rsv[3];
} sound_t;

typedef struct {
    uint32_t magic, version, bytes, crc;  /* crc: of everything after the head, bytes long */
    uint32_t npat;
    uint32_t rsv[3];
} proj_head_t;

static sound_t snd;

static uint32_t crc32_add(uint32_t c, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    uint32_t i, k;
    c = ~c;
    for (i = 0; i < n; i++) {
        c ^= b[i];
        for (k = 0; k < 8u; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint32_t bank_off(uint32_t b) { return PROJ_BASE + b * PROJ_BANK_BYTES; }

/* the bank's contents, checked: 1 = good */
static int bank_ok(uint32_t b, const proj_head_t **head)
{
    const proj_head_t *h;
    if (b >= PROJ_BANKS || !(h = (const proj_head_t *)plat_data(bank_off(b))))
        return 0;
    if (h->magic != PROJ_MAGIC || h->version != PROJ_VERSION || h->bytes > PROJ_BANK_BYTES - sizeof *h)
        return 0;
    if (crc32_add(0, (const uint8_t *)h + sizeof *h, h->bytes) != h->crc)
        return 0;
    *head = h;
    return 1;
}

static void sound_defaults(void)
{
    int t, k;
    memset(&snd, 0, sizeof snd);
    snd.bpm = 123;
    for (t = 0; t < NTRACK; t++) {
        snd.tm[t][0] = (uint8_t)(eng_knob_get(t, K_TONE) * 64.0f + 0.5f);
        snd.tm[t][1] = (uint8_t)(eng_knob_get(t, K_MOTION) * 64.0f + 0.5f);
    }
    for (k = K_PUMP; k < NKNOB; k++)
        snd.knob[k] = (uint8_t)(eng_knob_get(0, k) * 64.0f + 0.5f);
}

static int sound_ok(const sound_t *s)
{
    int t, k;
    if (s->bpm < 40u || s->bpm > 240u)
        return 0;
    for (t = 0; t < NTRACK; t++)
        if (s->octave[t] < -2 || s->octave[t] > 2 || s->patch[t] >= ENG_PATCHES || s->tm[t][0] > 64u ||
            s->tm[t][1] > 64u)
            return 0;
    for (k = 0; k < NKNOB; k++)
        if (s->knob[k] > 64u)
            return 0;
    return 1;
}

/* a project into RAM: 0 = loaded, -1 = none there (the defaults: an empty project) */
static int proj_load_bank(uint32_t b)
{
    const proj_head_t *h;
    const uint8_t *p;
    uint32_t i;
    seq_init();
    sound_defaults();
    if (!bank_ok(b, &h))
        return -1;
    p = (const uint8_t *)h + sizeof *h;
    if (h->bytes < sizeof snd + sizeof seq_song + h->npat * (2u + sizeof(pattern_t)))
        return -1;
    memcpy(&snd, p, sizeof snd);
    p += sizeof snd;
    if (!sound_ok(&snd))
        sound_defaults();
    memcpy(&seq_song, p, sizeof seq_song);
    p += sizeof seq_song;
    for (i = 0; i < h->npat; i++) {
        uint32_t t = p[0], n = p[1];
        if (t < SEQ_TRACKS && n < SEQ_PATTERNS)
            memcpy(&seq_pat[t][n], p + 2, sizeof(pattern_t));
        p += 2u + sizeof(pattern_t);
    }
    for (i = 0; i < SEQ_TRACKS; i++) {   /* anything out of range, made safe */
        uint32_t n;
        if (seq_song.cur[i] >= SEQ_PATTERNS)
            seq_song.cur[i] = 0;
        for (n = 0; n < SEQ_PATTERNS; n++)
            if (!seq_song.hdr[i][n].len || seq_song.hdr[i][n].len > SEQ_STEPS)
                seq_song.hdr[i][n].len = 16;
        seq_song.play[i].mode %= ARP_NMODE;
        seq_song.play[i].rate %= ARP_NRATE;
        if (seq_song.play[i].chord_n > SEQ_NOTES)
            seq_song.play[i].chord_n = 0;
    }
    if (seq_song.chain_n > SEQ_CHAIN)
        seq_song.chain_n = 0;
    return 0;
}

/* RAM into bank b: 0 = written and read back whole */
static int proj_write_bank(uint32_t b)
{
    proj_head_t h;
    uint32_t off = bank_off(b), w = sizeof h, i, t, n, crc = 0, npat = 0;
    const proj_head_t *back;
    for (i = 0; i < PROJ_BANK_BYTES / 4096u; i++) {   /* only the sectors it will use are erased */
        if (i * 4096u > sizeof h + sizeof snd + sizeof seq_song + 80u * (2u + sizeof(pattern_t)))
            break;
        if (plat_data_erase(off + i * 4096u))
            return -1;
    }
#define PUT(src, len)                                                       \
    do {                                                                    \
        if (plat_data_write(off + w, (src), (len)))                         \
            return -1;                                                      \
        crc = crc32_add(crc, (src), (len));                                 \
        w += (len);                                                         \
    } while (0)
    PUT(&snd, sizeof snd);
    PUT(&seq_song, sizeof seq_song);
    for (t = 0; t < SEQ_TRACKS; t++)
        for (n = 0; n < SEQ_PATTERNS; n++) {
            uint8_t tag[2];
            if (seq_pat_empty((int)t, (int)n))
                continue;
            tag[0] = (uint8_t)t;
            tag[1] = (uint8_t)n;
            PUT(tag, 2u);
            PUT(&seq_pat[t][n], sizeof(pattern_t));
            npat++;
        }
#undef PUT
    memset(&h, 0, sizeof h);
    h.magic = PROJ_MAGIC;
    h.version = PROJ_VERSION;
    h.bytes = w - sizeof h;
    h.crc = crc;
    h.npat = npat;
    if (plat_data_write(off, &h, sizeof h))
        return -1;
    return bank_ok(b, &back) ? 0 : -1;
}
