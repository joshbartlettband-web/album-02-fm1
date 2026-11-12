/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alive-Stable-7254 */
/* Album 02's pictures (visual.h has the plan). Included by app.c vx_after the platform (gfx.c's canvas: cv_px, its
 * pixels byte-swapped for the panel) and the engine. Every scene is a function of the sound's numbers and its own
 * clocks, so each strip of the screen can be drawn alone: the same picture comes out whichever rows are asked. */
#include "visual.h"

static int32_t vx_sy;                       /* the strip's first screen vx_row */
static float vx_bright = 1.0f;              /* the pump's dip, on every colour drawn */

/* ---------------------------------------------------------------- colour --- */
typedef struct { uint8_t r, g, b; } vx_col_t;
static const vx_col_t vx_NAVY = {6, 8, 34}, vx_SPACE = {2, 2, 14}, vx_MAGENTA = {255, 50, 170}, vx_CYAN = {50, 220, 255},
                   vx_GOLD = {255, 196, 60}, vx_ORANGE = {255, 120, 40}, vx_VIOLET = {150, 100, 255}, vx_WHITE = {255, 255, 245},
                   vx_RED = {240, 40, 60}, vx_TEAL = {20, 160, 150}, vx_PINK = {255, 150, 200}, vx_DEEP = {30, 10, 70};

static inline vx_col_t vx_mix(vx_col_t a, vx_col_t b, float t)
{
    vx_col_t c;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    c.r = (uint8_t)(a.r + (b.r - a.r) * t);
    c.g = (uint8_t)(a.g + (b.g - a.g) * t);
    c.b = (uint8_t)(a.b + (b.b - a.b) * t);
    return c;
}
static inline vx_col_t vx_scale(vx_col_t a, float k)
{
    vx_col_t c;
    k = k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
    c.r = (uint8_t)(a.r * k);
    c.g = (uint8_t)(a.g * k);
    c.b = (uint8_t)(a.b * k);
    return c;
}
static inline uint16_t vx_pack(vx_col_t c)     /* to the panel's order (swapped), the pump's dip applied */
{
    uint32_t r = (uint32_t)(c.r * vx_bright), g = (uint32_t)(c.g * vx_bright), b = (uint32_t)(c.b * vx_bright);
    uint32_t v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    return (uint16_t)(((v >> 8) & 0xFFu) | ((v & 0xFFu) << 8));
}
static inline vx_col_t vx_unpack(uint16_t s)
{
    uint32_t v = ((s >> 8) & 0xFFu) | ((s & 0xFFu) << 8);
    vx_col_t c = {(uint8_t)((v >> 11) << 3), (uint8_t)(((v >> 5) & 63u) << 2), (uint8_t)((v & 31u) << 3)};
    return c;
}
static vx_col_t vx_hue(float h)                /* a vx_bright colour round the wheel, 0..1 */
{
    static const vx_col_t W[6] = {{255, 50, 170}, {255, 120, 40}, {255, 210, 60}, {50, 230, 140}, {50, 220, 255},
                               {150, 100, 255}};
    float x = (h - fm_floorf(h)) * 6.0f;
    int i = (int)x;
    return vx_mix(W[i % 6], W[(i + 1) % 6], x - (float)i);
}

/* -------------------------------------------------------------- drawing --- */
static inline uint16_t *vx_row(int32_t y) { return cv_px + (uint32_t)(y - vx_sy) * 240u; }
static inline int vx_in_strip(int32_t y) { return y >= vx_sy && y < vx_sy + VIS_STRIP; }

static void vx_hspan(int32_t x0, int32_t x1, int32_t y, vx_col_t c)   /* x0 .. x1 inclusive */
{
    uint16_t *p, v;
    if (!vx_in_strip(y))
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 > 239)
        x1 = 239;
    if (x0 > x1)
        return;
    p = vx_row(y);
    v = vx_pack(c);
    for (; x0 <= x1; x0++)
        p[x0] = v;
}
static void vx_hspan_add(int32_t x0, int32_t x1, int32_t y, vx_col_t c)   /* added to what is there: light */
{
    uint16_t *p;
    if (!vx_in_strip(y))
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 > 239)
        x1 = 239;
    p = vx_row(y);
    for (; x0 <= x1; x0++) {
        vx_col_t o = vx_unpack(p[x0]);
        uint32_t r = o.r + (uint32_t)(c.r * vx_bright), g = o.g + (uint32_t)(c.g * vx_bright), b = o.b + (uint32_t)(c.b * vx_bright);
        uint32_t v = ((r > 255u ? 255u : r) >> 3) << 11 | ((g > 255u ? 255u : g) >> 2) << 5 | ((b > 255u ? 255u : b) >> 3);
        p[x0] = (uint16_t)(((v >> 8) & 0xFFu) | ((v & 0xFFu) << 8));
    }
}
static void vx_dot(int32_t x, int32_t y, vx_col_t c)
{
    if ((uint32_t)x < 240u && vx_in_strip(y))
        vx_row(y)[x] = vx_pack(c);
}
static void vx_gradient(vx_col_t top, vx_col_t bot, int32_t y_top, int32_t y_bot)   /* the strip's rows, top to bottom */
{
    int32_t y;
    for (y = vx_sy; y < vx_sy + VIS_STRIP; y++) {
        float t = (float)(y - y_top) / (float)(y_bot - y_top);
        vx_hspan(0, 239, y, vx_mix(top, bot, t));
    }
}
static void vx_disc(float cx, float cy, float r, vx_col_t c, int add)
{
    int32_t y, y0 = (int32_t)(cy - r), y1 = (int32_t)(cy + r) + 1;
    if (r <= 0.0f || y1 < vx_sy || y0 >= vx_sy + VIS_STRIP)
        return;
    if (y0 < vx_sy)
        y0 = vx_sy;
    if (y1 > vx_sy + VIS_STRIP - 1)
        y1 = vx_sy + VIS_STRIP - 1;
    for (y = y0; y <= y1; y++) {
        float dy = (float)y + 0.5f - cy, w = r * r - dy * dy;
        int32_t dx;
        if (w <= 0.0f)
            continue;
        dx = (int32_t)fm_sqrtf(w);
        if (add)
            vx_hspan_add((int32_t)cx - dx, (int32_t)cx + dx, y, c);
        else
            vx_hspan((int32_t)cx - dx, (int32_t)cx + dx, y, c);
    }
}
static void vx_ring(float cx, float cy, float r0, float r1, vx_col_t c, int add)   /* r0 .. r1: an annulus */
{
    int32_t y, y0 = (int32_t)(cy - r1), y1 = (int32_t)(cy + r1) + 1;
    if (r1 <= 0.0f || y1 < vx_sy || y0 >= vx_sy + VIS_STRIP)
        return;
    if (r0 < 0.0f)
        r0 = 0.0f;
    if (y0 < vx_sy)
        y0 = vx_sy;
    if (y1 > vx_sy + VIS_STRIP - 1)
        y1 = vx_sy + VIS_STRIP - 1;
    for (y = y0; y <= y1; y++) {
        float dy = (float)y + 0.5f - cy, wo = r1 * r1 - dy * dy, wi = r0 * r0 - dy * dy;
        int32_t xo, xi;
        if (wo <= 0.0f)
            continue;
        xo = (int32_t)fm_sqrtf(wo);
        if (wi <= 0.0f) {
            (add ? vx_hspan_add : vx_hspan)((int32_t)cx - xo, (int32_t)cx + xo, y, c);
        } else {
            xi = (int32_t)fm_sqrtf(wi);
            (add ? vx_hspan_add : vx_hspan)((int32_t)cx - xo, (int32_t)cx - xi, y, c);
            (add ? vx_hspan_add : vx_hspan)((int32_t)cx + xi, (int32_t)cx + xo, y, c);
        }
    }
}
static void vx_glow(float cx, float cy, float r, vx_col_t c)   /* a soft light: discs added, wide and faint to small and vx_bright */
{
    vx_disc(cx, cy, r, vx_scale(c, 0.18f), 1);
    vx_disc(cx, cy, r * 0.6f, vx_scale(c, 0.25f), 1);
    vx_disc(cx, cy, r * 0.3f, vx_scale(c, 0.5f), 1);
}
static void vx_line(float x0, float y0, float x1, float y1, vx_col_t c, int thick)
{
    float dx = x1 - x0, dy = y1 - y0, len = fm_sqrtf(dx * dx + dy * dy), i, n;
    if (len < 0.5f) {
        vx_dot((int32_t)x0, (int32_t)y0, c);
        return;
    }
    if ((y0 < (float)vx_sy - 4.0f && y1 < (float)vx_sy - 4.0f) || (y0 > (float)(vx_sy + VIS_STRIP + 4) && y1 > (float)(vx_sy + VIS_STRIP + 4)))
        return;
    n = len;
    for (i = 0.0f; i <= n; i += 1.0f) {
        float x = x0 + dx * i / n, y = y0 + dy * i / n;
        if (thick > 1)
            vx_hspan((int32_t)x - thick / 2, (int32_t)x + thick / 2, (int32_t)y, c);
        else
            vx_dot((int32_t)x, (int32_t)y, c);
    }
}
static void vx_tri(float ax, float ay, float bx, float by, float cx, float cy, vx_col_t c)
{
    int32_t y, ymin, ymax;
    float t;
    if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
    if (ay > cy) { t = ax; ax = cx; cx = t; t = ay; ay = cy; cy = t; }
    if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; }
    ymin = (int32_t)ay;
    ymax = (int32_t)cy;
    if (ymax < vx_sy || ymin >= vx_sy + VIS_STRIP)
        return;
    if (ymin < vx_sy)
        ymin = vx_sy;
    if (ymax > vx_sy + VIS_STRIP - 1)
        ymax = vx_sy + VIS_STRIP - 1;
    for (y = ymin; y <= ymax; y++) {
        float fy = (float)y + 0.5f, xa, xb;
        xa = cy != ay ? ax + (cx - ax) * (fy - ay) / (cy - ay) : ax;
        if (fy < by)
            xb = by != ay ? ax + (bx - ax) * (fy - ay) / (by - ay) : bx;
        else
            xb = cy != by ? bx + (cx - bx) * (fy - by) / (cy - by) : bx;
        if (xa > xb) { t = xa; xa = xb; xb = t; }
        vx_hspan((int32_t)xa, (int32_t)xb, y, c);
    }
}

/* the vx_stars: a fixed field, each with its own depth; drift moves them, trail draws their streaks back along it */
static inline uint32_t vx_h32(uint32_t a)
{
    a ^= a >> 16;
    a *= 0x7FEB352Du;
    a ^= a >> 15;
    a *= 0x846CA68Bu;
    return a ^ (a >> 16);
}
static void vx_stars(int n, float drift_x, float drift_y, float trail, vx_col_t c, float twinkle_t)
{
    int i;
    for (i = 0; i < n; i++) {
        uint32_t h = vx_h32((uint32_t)i * 2654435761u + 17u);
        float depth = 0.3f + (float)(h & 255u) / 255.0f * 0.7f;
        float x = (float)((h >> 8) % 240u) + drift_x * depth, y = (float)((h >> 16) % 240u) + drift_y * depth;
        float tw = 0.55f + 0.45f * fm_sin_turns(twinkle_t * (0.5f + depth) + (float)(h & 1023u) / 1024.0f);
        x -= 240.0f * fm_floorf(x / 240.0f);
        y -= 240.0f * fm_floorf(y / 240.0f);
        if (trail > 0.5f)
            vx_line(x, y, x - drift_x * 0.0f - trail * depth, y, vx_scale(c, depth * 0.45f * tw), 1);
        vx_dot((int32_t)x, (int32_t)y, vx_scale(c, depth * tw));
        if (depth > 0.85f) {
            vx_dot((int32_t)x + 1, (int32_t)y, vx_scale(c, 0.5f * tw));
            vx_dot((int32_t)x, (int32_t)y + 1, vx_scale(c, 0.5f * tw));
        }
    }
}

/* the effects every scene shares, vx_after it is drawn: iris (SWEEP), bands (PHASER), coarse pixels (GRIT) */
static void vx_after(const vis_state_t *v)
{
    int32_t y, x;
    float s = v->sweep;
    if (s < 0.46f || s > 0.54f) {        /* the iris: closing from the edges, or a hole opening in the middle */
        int low = s < 0.46f;
        float amt = low ? (0.46f - s) / 0.46f : (s - 0.54f) / 0.46f;
        float r = low ? 175.0f - 135.0f * amt : 12.0f + 110.0f * amt;
        vx_col_t shade = {2, 2, 10};
        for (y = vx_sy; y < vx_sy + VIS_STRIP; y++) {
            float dy = (float)y - 120.0f, w = r * r - dy * dy;
            int32_t dx = w > 0.0f ? (int32_t)fm_sqrtf(w) : -1;
            if (low) {
                if (dx < 0) {
                    vx_hspan(0, 239, y, shade);
                } else {
                    vx_hspan(0, 120 - dx - 1, y, shade);
                    vx_hspan(120 + dx + 1, 239, y, shade);
                    vx_dot(120 - dx, y, vx_scale(vx_MAGENTA, 0.6f));
                    vx_dot(120 + dx, y, vx_scale(vx_MAGENTA, 0.6f));
                }
            } else if (dx >= 0) {
                vx_hspan(120 - dx, 120 + dx, y, shade);
                vx_dot(120 - dx, y, vx_scale(vx_CYAN, 0.7f));
                vx_dot(120 + dx, y, vx_scale(vx_CYAN, 0.7f));
            }
        }
    }
    if (v->ph_amt > 0.03f) {             /* the phaser's bands: diagonal, sliding with its sweep */
        int32_t off = (int32_t)((v->phaser * 0.5f + 0.5f) * 80.0f) + (int32_t)(v->t * 30.0f);
        vx_col_t c = vx_scale(vx_hue(v->phaser * 0.25f + 0.55f), 0.35f * v->ph_amt);
        for (y = vx_sy; y < vx_sy + VIS_STRIP; y++) {
            int32_t start = ((y + off) % 40 + 40) % 40;
            for (x = -start; x < 240; x += 40)
                vx_hspan_add(x, x + 9, y, c);
        }
    }
    if (v->grit > 0.04f) {               /* coarse pixels: blocks of 2 .. 8 */
        int32_t b = v->grit < 0.3f ? 2 : v->grit < 0.55f ? 3 : v->grit < 0.8f ? 4 : 6, yy, xx;
        for (y = 0; y < VIS_STRIP; y += b)
            for (x = 0; x < 240; x += b) {
                uint16_t c = cv_px[(uint32_t)y * 240u + (uint32_t)x];
                for (yy = y; yy < y + b && yy < VIS_STRIP; yy++)
                    for (xx = x; xx < x + b && xx < 240; xx++)
                        cv_px[(uint32_t)yy * 240u + (uint32_t)xx] = c;
            }
    }
}

/* ------------------------------------------------------------ the scenes --- */
/* each draws the whole picture for the strip; ghosts (ECHO) are its moving part drawn again at earlier clocks */
#define GHOSTS(v) ((v)->echo > 0.05f ? 1 + (int)((v)->echo * 3.0f) : 0)

/* DRUMS: BOX. A city of windows at night; each hit lights a vx_row of them, the hats sparkle above */
static void sc_box(const vis_state_t *v)
{
    int i, j;
    float warm = v->tone;
    vx_gradient(vx_SPACE, vx_mix(vx_DEEP, vx_ORANGE, 0.25f), 0, 240);
    vx_stars(50, -v->run[0] * 4.0f, 0.0f, v->space * 12.0f, vx_WHITE, v->t);
    for (i = 0; i < 9; i++) {            /* the towers */
        uint32_t h = vx_h32((uint32_t)i + 5u);
        float x = (float)i * 27.0f + 2.0f, top = 90.0f + (float)(h % 90u), w = 22.0f;
        vx_col_t body = vx_mix(vx_NAVY, vx_DEEP, (float)(h & 255u) / 255.0f);
        for (j = (int32_t)top; j < 240; j++)
            if (vx_in_strip(j))
                vx_hspan((int32_t)x, (int32_t)(x + w), j, body);
        for (j = 0; j < 14; j++) {       /* the windows: lit by the hits travelling up */
            float wy = top + 6.0f + (float)j * 10.0f;
            uint32_t wh = vx_h32((uint32_t)(i * 31 + j));
            float lit = v->pulse * (1.0f - (float)j * (0.12f - 0.08f * v->motion)) + ((wh & 3u) == 0u ? 0.45f : 0.0f);
            vx_col_t wc = vx_mix(vx_mix(vx_CYAN, vx_GOLD, warm), vx_WHITE, v->pulse * 0.6f);
            if (wy > 236.0f)
                break;
            if (lit > 0.1f) {
                int32_t yy;
                for (yy = (int32_t)wy; yy < (int32_t)wy + 4; yy++) {
                    vx_hspan((int32_t)x + 4, (int32_t)x + 8, yy, vx_scale(wc, lit));
                    vx_hspan((int32_t)x + 13, (int32_t)x + 17, yy, vx_scale(wc, lit * 0.8f));
                }
            }
        }
    }
    if (v->level[0] > 0.05f)             /* the hats: sparks over the roofs */
        for (i = 0; i < 12; i++) {
            uint32_t h = vx_h32((uint32_t)i * 97u + (uint32_t)(v->t * 12.0f));
            vx_dot((int32_t)(h % 240u), 70 + (int32_t)((h >> 8) % 50u), vx_scale(vx_GOLD, v->level[0]));
        }
}

/* DRUMS: BOOM. A ringed planet; each kick sends a vx_ring out from it */
static void sc_boom(const vis_state_t *v)
{
    int g;
    vx_col_t body = vx_mix(vx_VIOLET, vx_ORANGE, v->tone);
    vx_gradient(vx_SPACE, vx_NAVY, 0, 240);
    vx_stars(70, v->run[0] * 3.0f, v->run[0] * 1.0f, v->space * 10.0f, vx_WHITE, v->t);
    for (g = GHOSTS(v); g >= 1; g--)     /* the rings before, fading out */
        vx_ring(120.0f, 130.0f, 40.0f + (v->run[1] - fm_floorf(v->run[1]) + (float)g * 0.25f) * 110.0f,
             42.0f + (v->run[1] - fm_floorf(v->run[1]) + (float)g * 0.25f) * 110.0f,
             vx_scale(body, v->echo * 0.4f / (float)g), 1);
    vx_ring(120.0f, 130.0f, 40.0f + (1.0f - v->pulse) * 120.0f, 44.0f + (1.0f - v->pulse) * 120.0f,
         vx_scale(vx_mix(body, vx_WHITE, 0.5f), v->pulse), 1);
    vx_glow(120.0f, 130.0f, 80.0f, vx_scale(body, 0.6f + 0.4f * v->pulse));
    vx_disc(120.0f, 130.0f, 38.0f + 4.0f * v->pulse, body, 0);
    vx_disc(108.0f, 118.0f, 22.0f, vx_mix(body, vx_WHITE, 0.25f), 0);   /* lit from the upper left */
    vx_ring(120.0f, 130.0f, 56.0f, 60.0f + 6.0f * v->motion, vx_scale(vx_GOLD, 0.8f), 0);
    vx_disc(120.0f, 130.0f, 38.0f, body, 0);   /* the planet in front of the vx_ring's far side, in flat colours */
    vx_disc(110.0f, 120.0f, 20.0f, vx_mix(body, vx_WHITE, 0.3f), 0);
    if (v->ch_amt > 0.05f)
        vx_ring(120.0f + 6.0f * v->chorus, 130.0f, 38.0f, 40.0f, vx_scale(vx_CYAN, v->ch_amt), 1);
}

/* DRUMS: CLUB. A dance floor running to the horizon; the floor lights on the steps, the kick strobes */
static void sc_club(const vis_state_t *v)
{
    int i;
    float hz = 100.0f, scroll = v->run[0] - fm_floorf(v->run[0]);
    vx_gradient(vx_mix(vx_SPACE, vx_MAGENTA, 0.15f + 0.4f * v->pulse), vx_SPACE, 0, (int32_t)hz);
    for (i = (int32_t)hz; i < 240; i++)
        if (vx_in_strip(i))
            vx_hspan(0, 239, i, vx_mix(vx_DEEP, vx_SPACE, (float)(i - (int32_t)hz) / 140.0f));
    for (i = 0; i < 12; i++) {           /* the rows, coming forward */
        float z = (float)i + scroll, y = hz + 140.0f * (z * z) / 144.0f;
        vx_col_t c = i == (v->step & 7) ? vx_mix(vx_MAGENTA, vx_WHITE, 0.4f) : vx_scale(vx_mix(vx_MAGENTA, vx_CYAN, v->tone), 0.5f + 0.5f * v->pulse);
        if (y < 240.0f)
            vx_hspan(0, 239, (int32_t)y, c);
    }
    for (i = -8; i <= 8; i++)            /* the lines to the vanishing point */
        vx_line(120.0f, hz, 120.0f + (float)i * 40.0f, 240.0f, vx_scale(vx_CYAN, 0.6f), 1);
    if (v->ch_amt > 0.05f)
        for (i = -8; i <= 8; i++)
            vx_line(120.0f + 4.0f * v->chorus, hz, 120.0f + (float)i * 40.0f + 8.0f * v->chorus, 240.0f,
                 vx_scale(vx_MAGENTA, 0.4f * v->ch_amt), 1);
    vx_glow(120.0f, hz, 60.0f + 40.0f * v->pulse, vx_scale(vx_GOLD, 0.5f + 0.5f * v->pulse));
    vx_stars(30, -v->run[1] * 6.0f, 0.0f, v->space * 16.0f, vx_PINK, v->t);
}

/* BASS: PLUCK. Four strings across the screen, the one playing shaking with its note */
static void sc_pluck(const vis_state_t *v)
{
    int s, x;
    vx_gradient(vx_NAVY, vx_SPACE, 0, 240);
    vx_stars(40, v->run[0] * 2.0f, 0.0f, v->space * 8.0f, vx_CYAN, v->t);
    for (s = 0; s < 4; s++) {
        float y0 = 70.0f + (float)s * 34.0f, amp = (s == (int)(v->pitch * 3.99f) ? 1.0f : 0.25f) * (0.08f + v->level[1]) * 60.0f;
        float k = 1.0f + (float)s;
        vx_col_t c = vx_mix(vx_MAGENTA, vx_GOLD, (float)s / 3.0f + v->tone * 0.3f);
        int g;
        for (g = GHOSTS(v); g >= 0; g--) {
            float ph = v->run[1] * 6.0f - (float)g * 0.15f, a = amp * (g ? v->echo * 0.5f / (float)g : 1.0f);
            float py = y0;
            for (x = 0; x < 240; x += 2) {
                float u = (float)x / 240.0f;
                float y = y0 + a * fm_sin_turns(u * k * 0.5f) * fm_sin_turns(ph * k * (0.5f + v->motion));
                vx_line((float)x - 2.0f, py, (float)x, y, g ? vx_scale(c, 0.4f) : c, g ? 1 : 2);
                py = y;
            }
        }
        if (v->ch_amt > 0.05f)
            vx_hspan(0, 239, (int32_t)(y0 + 3.0f + 3.0f * v->chorus), vx_scale(vx_CYAN, 0.4f * v->ch_amt));
    }
}

/* BASS: ACID. A molten sun whose rim boils with the filter */
static void sc_acid(const vis_state_t *v)
{
    int32_t y;
    float r = 50.0f + 25.0f * v->level[1], boil = 3.0f + 12.0f * v->tone + 20.0f * v->level[1];
    vx_gradient(vx_mix(vx_SPACE, vx_RED, 0.2f), vx_SPACE, 0, 240);
    vx_stars(40, v->run[0] * 2.0f, v->run[0], v->space * 10.0f, vx_ORANGE, v->t);
    vx_glow(120.0f, 120.0f, r * 2.2f, vx_scale(vx_ORANGE, 0.7f));
    for (y = vx_sy; y < vx_sy + VIS_STRIP; y++) {   /* the rim: a radius that wobbles with the angle */
        float dy = (float)y - 120.0f;
        int32_t x;
        if (dy * dy > (r + boil) * (r + boil))
            continue;
        for (x = 0; x < 240; x++) {
            float dx = (float)x - 120.0f, d2 = dx * dx + dy * dy, a, rr;
            if (d2 > (r + boil) * (r + boil))
                continue;
            a = (dx > 0.0f ? dy / (fm_fabsf(dx) + fm_fabsf(dy) + 0.01f) : 2.0f - dy / (fm_fabsf(dx) + fm_fabsf(dy) + 0.01f));
            rr = r + boil * 0.5f * (fm_sin_turns(a * 1.5f + v->run[1] * (1.0f + v->motion)) +
                                    0.6f * fm_sin_turns(a * 3.5f - v->run[1] * 1.7f));
            if (d2 < rr * rr) {
                float k = fm_sqrtf(d2) / (rr + 0.01f);
                vx_row(y)[x] = vx_pack(vx_mix(vx_mix(vx_WHITE, vx_GOLD, k * 1.5f), vx_RED, k * k));
            }
        }
    }
    if (v->ch_amt > 0.05f)
        vx_ring(120.0f + 8.0f * v->chorus, 120.0f, r + boil, r + boil + 2.0f, vx_scale(vx_MAGENTA, v->ch_amt), 1);
}

/* BASS: ROUND. A deep-sea light; rings rise from it slowly, one a note */
static void sc_round(const vis_state_t *v)
{
    int i;
    vx_gradient(vx_mix(vx_NAVY, vx_TEAL, 0.25f), vx_SPACE, 0, 240);
    for (i = 0; i < 6 + GHOSTS(v) * 2; i++) {
        float age = v->run[1] * 0.4f + (float)i / 6.0f;
        float f = age - fm_floorf(age), r = 20.0f + f * 120.0f;
        vx_ring(120.0f, 200.0f - f * 90.0f, r, r + 2.0f + 3.0f * v->level[1],
             vx_scale(vx_mix(vx_TEAL, vx_CYAN, v->tone), (1.0f - f) * (0.4f + v->level[1])), 1);
    }
    vx_glow(120.0f, 200.0f, 50.0f + 40.0f * v->level[1], vx_scale(vx_CYAN, 0.6f));
    vx_disc(120.0f, 200.0f, 10.0f + 10.0f * v->level[1], vx_mix(vx_CYAN, vx_WHITE, 0.6f), 0);
    for (i = 0; i < 30; i++) {           /* motes, drifting up */
        uint32_t h = vx_h32((uint32_t)i * 13u);
        float y = (float)(h % 240u) - v->run[0] * 10.0f * (0.5f + (float)(h & 7u) / 8.0f);
        y -= 240.0f * fm_floorf(y / 240.0f);
        vx_dot((int32_t)((h >> 8) % 240u), (int32_t)y, vx_scale(vx_CYAN, 0.6f));
    }
    if (v->ch_amt > 0.05f)
        vx_ring(120.0f + 5.0f * v->chorus, 200.0f, 22.0f, 24.0f, vx_scale(vx_MAGENTA, v->ch_amt), 1);
}

/* CHORDS: PAD. A nebula: clouds of colour that drift, one tinted by each note of the chord */
static void sc_pad(const vis_state_t *v)
{
    int i;
    vx_gradient(vx_SPACE, vx_DEEP, 0, 240);
    vx_stars(90, v->run[0] * 2.0f, v->run[0] * 0.5f, v->space * 14.0f, vx_WHITE, v->t);
    for (i = 0; i < 6; i++) {
        uint32_t h = vx_h32((uint32_t)i * 71u);
        float ang = v->run[1] * 0.05f * (1.0f + v->motion) + (float)i / 6.0f;
        float x = 120.0f + 70.0f * fm_sin_turns(ang + (float)(h & 255u) / 255.0f);
        float y = 120.0f + 60.0f * fm_sin_turns(ang * 0.7f + 0.25f + (float)((h >> 8) & 255u) / 255.0f);
        vx_col_t c = vx_hue(v->pitch * 0.5f + (float)i * 0.12f + v->tone * 0.3f);
        float r = 45.0f + 30.0f * v->level[2] + (float)(h % 20u);
        vx_disc(x, y, r, vx_scale(c, 0.10f + 0.25f * v->level[2]), 1);
        vx_disc(x, y, r * 0.55f, vx_scale(c, 0.10f + 0.2f * v->level[2]), 1);
        if (v->ch_amt > 0.05f)
            vx_disc(x + 10.0f * v->chorus, y, r * 0.5f, vx_scale(vx_hue((float)i * 0.12f + 0.5f), 0.12f * v->ch_amt), 1);
    }
}

/* CHORDS: STAB. Beams from the centre; each chord struck fires a fan of them that fades */
static void sc_stab(const vis_state_t *v)
{
    int i, g;
    vx_gradient(vx_SPACE, vx_NAVY, 0, 240);
    vx_stars(50, 0.0f, v->run[0] * 3.0f, v->space * 10.0f, vx_WHITE, v->t);
    for (g = GHOSTS(v); g >= 0; g--) {
        float rot = v->run[1] * 0.02f - (float)g * 0.03f, k = (0.12f + v->level[2] * 2.2f) * (g ? v->echo * 0.5f / (float)g : 1.0f);
        int n = 6 + (int)(v->tone * 10.0f);
        for (i = 0; i < n; i++) {
            float a = rot + (float)i / (float)n;
            float len = 40.0f + 140.0f * k;
            vx_col_t c = vx_scale(i & 1 ? vx_CYAN : vx_MAGENTA, k);
            vx_line(120.0f, 120.0f, 120.0f + len * fm_sin_turns(a + 0.25f), 120.0f + len * fm_sin_turns(a), c, g ? 1 : 3);
        }
    }
    vx_glow(120.0f, 120.0f, 30.0f + 60.0f * v->level[2], vx_scale(vx_WHITE, 0.6f));
    if (v->ch_amt > 0.05f)
        vx_ring(120.0f, 120.0f, 34.0f + 10.0f * v->chorus, 36.0f + 10.0f * v->chorus, vx_scale(vx_GOLD, v->ch_amt), 1);
}

/* CHORDS: WIND. A tunnel of streaks rushing past; the whistle's pitch is where they bunch */
static void sc_wind(const vis_state_t *v)
{
    int i;
    float band = 40.0f + (1.0f - v->pitch) * 160.0f;
    vx_gradient(vx_mix(vx_SPACE, vx_TEAL, 0.15f), vx_SPACE, 0, 240);
    for (i = 0; i < 120; i++) {
        uint32_t h = vx_h32((uint32_t)i * 101u);
        float depth = 0.2f + (float)(h & 255u) / 255.0f * 0.8f;
        float y = band + ((float)((h >> 8) % 200u) - 100.0f) * (0.3f + 0.7f * (1.0f - v->level[2]));
        float x = (float)((h >> 16) % 240u) - v->run[0] * 120.0f * depth;
        float len = (8.0f + 50.0f * v->motion + 30.0f * v->space) * depth;
        vx_col_t c = vx_scale(vx_mix(vx_TEAL, vx_WHITE, depth), depth * (0.4f + v->level[2] * 1.5f));
        x -= 280.0f * fm_floorf(x / 280.0f);
        vx_line(x, y, x + len, y, c, 1);
        if (v->ch_amt > 0.05f && (h & 3u) == 0u)
            vx_line(x, y + 3.0f * v->chorus, x + len, y + 3.0f * v->chorus, vx_scale(vx_MAGENTA, v->ch_amt * depth), 1);
    }
    vx_glow(20.0f, band, 40.0f + 40.0f * v->level[2], vx_scale(vx_CYAN, 0.6f * v->level[2] + 0.1f));
}

/* LEAD: BRASS. A starship over a planet's horizon; it climbs with the pitch, its trail the notes before */
static void sc_brass(const vis_state_t *v)
{
    int g;
    float y = 170.0f - v->pitch * 110.0f, x = 90.0f + 30.0f * fm_sin_turns(v->run[1] * 0.07f);
    vx_gradient(vx_mix(vx_SPACE, vx_ORANGE, 0.1f), vx_mix(vx_DEEP, vx_MAGENTA, 0.3f), 0, 200);
    vx_stars(60, -v->run[0] * 8.0f, 0.0f, 6.0f + v->space * 30.0f, vx_WHITE, v->t);
    vx_disc(120.0f, 520.0f, 330.0f, vx_mix(vx_DEEP, vx_MAGENTA, 0.35f), 0);   /* the horizon */
    vx_ring(120.0f, 520.0f, 328.0f, 331.0f, vx_mix(vx_MAGENTA, vx_GOLD, v->tone), 0);
    for (g = GHOSTS(v) + 3; g >= 1; g--) {   /* the trail: where it was */
        float gy = 170.0f - v->pitch * 110.0f + fm_sin_turns(v->t * 0.5f + (float)g * 0.1f) * 6.0f * (float)g;
        vx_line(x - (float)g * 22.0f, gy, x - (float)(g - 1) * 22.0f - 10.0f, y, vx_scale(vx_GOLD, 0.6f / (float)g * (0.4f + v->level[3])), 2);
    }
    vx_tri(x - 14.0f, y - 7.0f, x - 14.0f, y + 7.0f, x + 26.0f, y, vx_mix(vx_WHITE, vx_CYAN, v->tone));   /* the hull */
    vx_tri(x - 8.0f, y, x - 18.0f, y - 14.0f, x + 4.0f, y, vx_mix(vx_MAGENTA, vx_VIOLET, 0.5f));          /* the fins */
    vx_tri(x - 8.0f, y, x - 18.0f, y + 14.0f, x + 4.0f, y, vx_mix(vx_MAGENTA, vx_VIOLET, 0.5f));
    vx_glow(x - 16.0f, y, 10.0f + 16.0f * v->level[3], vx_scale(vx_GOLD, 0.9f));
    if (v->ch_amt > 0.05f)
        vx_tri(x - 14.0f + 6.0f * v->chorus, y - 9.0f, x - 14.0f + 6.0f * v->chorus, y + 9.0f, x + 28.0f + 6.0f * v->chorus, y,
            vx_scale(vx_CYAN, 0.35f * v->ch_amt));
}

/* LEAD: GLASS. A crystal turning in space, its facets ringing with each note */
static void sc_glass(const vis_state_t *v)
{
    int i, g, n = 5 + (int)(v->tone * 4.0f);
    vx_gradient(vx_SPACE, vx_mix(vx_NAVY, vx_CYAN, 0.15f), 0, 240);
    vx_stars(60, 0.0f, 0.0f, v->space * 6.0f, vx_CYAN, v->t);
    for (g = GHOSTS(v); g >= 0; g--) {
        float rot = v->run[1] * 0.05f * (0.4f + v->motion) - (float)g * 0.02f;
        float r = 50.0f + 50.0f * v->level[3] + (float)g * 10.0f;
        vx_col_t c = g ? vx_scale(vx_CYAN, v->echo * 0.4f / (float)g) : vx_mix(vx_CYAN, vx_WHITE, v->level[3]);
        for (i = 0; i < n; i++) {
            float a0 = rot + (float)i / (float)n, a1 = rot + (float)(i + 2) / (float)n;   /* a star polygon */
            vx_line(120.0f + r * fm_sin_turns(a0 + 0.25f), 120.0f + r * fm_sin_turns(a0),
                 120.0f + r * fm_sin_turns(a1 + 0.25f), 120.0f + r * fm_sin_turns(a1), c, g ? 1 : 2);
        }
        if (v->ch_amt > 0.05f && !g)
            for (i = 0; i < n; i++) {
                float a0 = rot + 0.01f * v->chorus + (float)i / (float)n, a1 = a0 + 2.0f / (float)n;
                vx_line(120.0f + r * 1.05f * fm_sin_turns(a0 + 0.25f), 120.0f + r * 1.05f * fm_sin_turns(a0),
                     120.0f + r * 1.05f * fm_sin_turns(a1 + 0.25f), 120.0f + r * 1.05f * fm_sin_turns(a1),
                     vx_scale(vx_MAGENTA, v->ch_amt), 1);
            }
    }
    vx_glow(120.0f, 120.0f, 40.0f + 50.0f * v->level[3], vx_scale(vx_CYAN, 0.5f));
}

/* LEAD: FUNK. A skyline of bars, each a band of the sound, bouncing; the note playing lights its own */
static void sc_funk(const vis_state_t *v)
{
    int i, g;
    vx_gradient(vx_mix(vx_SPACE, vx_VIOLET, 0.2f), vx_SPACE, 0, 240);
    vx_stars(40, -v->run[0] * 5.0f, 0.0f, v->space * 10.0f, vx_WHITE, v->t);
    for (i = 0; i < 16; i++) {
        float x = 6.0f + (float)i * 14.5f;
        float wob = 0.5f + 0.5f * fm_sin_turns(v->run[1] * (0.6f + 0.4f * v->motion) + (float)i * 0.13f);
        float lv = v->level[3] * (0.4f + 0.6f * wob) + v->level[0] * 0.3f * (i < 4);
        int lit = i == (int)(v->pitch * 15.99f);
        float h = 10.0f + 170.0f * lv * (lit ? 1.3f : 1.0f);
        vx_col_t c = lit ? vx_WHITE : vx_hue((float)i / 16.0f * 0.5f + v->tone * 0.4f);
        int32_t y;
        for (g = GHOSTS(v); g >= 1; g--)     /* the tops before, left hanging */
            vx_hspan((int32_t)x, (int32_t)x + 10, (int32_t)(230.0f - h - (float)g * 8.0f), vx_scale(c, v->echo * 0.5f / (float)g));
        for (y = (int32_t)(230.0f - h); y < 230; y += 4)
            vx_hspan((int32_t)x, (int32_t)x + 10, y, vx_scale(c, 0.6f + 0.4f * (float)(230 - y) / (h + 1.0f)));
        if (v->ch_amt > 0.05f)
            vx_hspan((int32_t)(x + 3.0f * v->chorus), (int32_t)(x + 10.0f + 3.0f * v->chorus), (int32_t)(228.0f - h),
                  vx_scale(vx_CYAN, v->ch_amt));
    }
}

/* VOX: CHOIR. A halo of light; its petals open and close with the vowel, and sway with the vibrato */
static void sc_choir(const vis_state_t *v)
{
    int i, g, n = 12;
    vx_gradient(vx_DEEP, vx_SPACE, 0, 240);
    vx_stars(70, 0.0f, -v->run[0] * 2.0f, v->space * 10.0f, vx_GOLD, v->t);
    for (g = GHOSTS(v); g >= 0; g--) {
        float open = 0.3f + 0.7f * v->vowel, r = 40.0f + 40.0f * v->level[4] + (float)g * 12.0f;
        vx_col_t c = g ? vx_scale(vx_GOLD, v->echo * 0.4f / (float)g) : vx_mix(vx_GOLD, vx_WHITE, v->level[4]);
        for (i = 0; i < n; i++) {
            float a = (float)i / (float)n + v->run[1] * 0.01f, sway = 0.03f * fm_sin_turns(v->run[1] * v->motion + (float)i);
            float x1 = 120.0f + r * fm_sin_turns(a + 0.25f), y1 = 120.0f + r * fm_sin_turns(a);
            float x2 = 120.0f + (r + 40.0f * open) * fm_sin_turns(a + 0.25f + sway), y2 = 120.0f + (r + 40.0f * open) * fm_sin_turns(a + sway);
            vx_line(x1, y1, x2, y2, c, g ? 1 : 2);
        }
        vx_ring(120.0f, 120.0f, r - 2.0f, r, c, 1);
    }
    vx_glow(120.0f, 120.0f, 50.0f + 40.0f * v->level[4], vx_scale(vx_mix(vx_GOLD, vx_PINK, v->tone), 0.6f));
    if (v->ch_amt > 0.05f)
        vx_ring(120.0f + 6.0f * v->chorus, 120.0f, 38.0f, 40.0f, vx_scale(vx_CYAN, v->ch_amt), 1);
}

/* VOX: ROBOT. An oscilloscope's trace as a mouth: its shape is the vowel, its size the voice */
static void sc_robot(const vis_state_t *v)
{
    int x, g, y;
    float open = 6.0f + 60.0f * v->level[4] * (0.3f + 0.7f * v->vowel), harm = 1.0f + (float)(int)(v->vowel * 4.0f);
    vx_gradient(vx_mix(vx_SPACE, vx_TEAL, 0.2f), vx_SPACE, 0, 240);
    for (y = 20; y < 240; y += 20)       /* the screen's graticule */
        vx_hspan(0, 239, y, vx_scale(vx_TEAL, 0.25f));
    for (x = 20; x < 240; x += 20)
        for (y = vx_sy; y < vx_sy + VIS_STRIP; y++)
            if (vx_in_strip(y) && (y & 1))
                vx_dot(x, y, vx_scale(vx_TEAL, 0.25f));
    for (g = GHOSTS(v); g >= 0; g--) {
        float py = 120.0f;
        vx_col_t c = g ? vx_scale(vx_CYAN, v->echo * 0.4f / (float)g) : vx_mix(vx_mix(vx_CYAN, vx_WHITE, 0.4f), vx_GOLD, v->tone);
        for (x = 10; x < 230; x += 2) {
            float u = (float)(x - 10) / 220.0f, env = fm_sin_turns(u * 0.5f);
            float s = fm_sin_turns(u * harm * 2.0f + v->run[1] * 0.3f - (float)g * 0.05f);
            float yy = 120.0f + open * env * (s > 0.0f ? 1.0f : -1.0f) * fm_sqrtf(fm_fabsf(s));   /* stepped, square-ish */
            vx_line((float)x - 2.0f, py, (float)x, yy, c, g ? 1 : 2);
            py = yy;
        }
    }
    if (v->ch_amt > 0.05f)
        vx_hspan(10, 230, (int32_t)(124.0f + 4.0f * v->chorus), vx_scale(vx_MAGENTA, v->ch_amt));
}

/* VOX: MONK. A great moon over still water; the voice sends ripples across its reflection */
static void sc_monk(const vis_state_t *v)
{
    int i;
    int32_t y;
    vx_gradient(vx_mix(vx_SPACE, vx_VIOLET, 0.25f), vx_DEEP, 0, 140);
    vx_stars(50, v->run[0] * 0.5f, 0.0f, v->space * 6.0f, vx_WHITE, v->t);
    vx_glow(120.0f, 80.0f, 80.0f, vx_scale(vx_mix(vx_GOLD, vx_PINK, v->tone), 0.4f + 0.4f * v->level[4]));
    vx_disc(120.0f, 80.0f, 40.0f, vx_mix(vx_mix(vx_GOLD, vx_PINK, v->tone), vx_WHITE, 0.4f), 0);
    for (y = 140; y < 240; y++) {        /* the water, and the moon in it, broken by the ripples */
        float d = (float)(y - 140);
        float wob = (2.0f + 14.0f * v->level[4]) * fm_sin_turns(d * 0.06f - v->run[1] * (0.3f + 0.7f * v->motion));
        float w = 40.0f - d * 0.25f;
        if (!vx_in_strip(y))
            continue;
        vx_hspan(0, 239, y, vx_mix(vx_DEEP, vx_SPACE, d / 100.0f));
        if (w > 0.0f && ((int)d % 3) != 0)
            vx_hspan((int32_t)(120.0f - w + wob), (int32_t)(120.0f + w + wob), y,
                  vx_scale(vx_mix(vx_GOLD, vx_PINK, v->tone), 0.6f - d / 220.0f));
    }
    for (i = 0; i < 4 + GHOSTS(v); i++) {   /* rings on the water */
        float f = v->run[1] * 0.2f + (float)i * 0.25f, r;
        f -= fm_floorf(f);
        r = 10.0f + f * 120.0f;
        for (y = 150; y < 240; y++)
            if (vx_in_strip(y)) {
                float dy = ((float)y - 190.0f) * 3.0f, ww = r * r - dy * dy;
                if (ww > 0.0f && ww < r * 12.0f) {
                    int32_t dx = (int32_t)fm_sqrtf(ww);
                    vx_dot(120 - dx, y, vx_scale(vx_WHITE, (1.0f - f) * v->level[4] * 1.5f));
                    vx_dot(120 + dx, y, vx_scale(vx_WHITE, (1.0f - f) * v->level[4] * 1.5f));
                }
            }
    }
    if (v->ch_amt > 0.05f)
        vx_ring(120.0f + 6.0f * v->chorus, 80.0f, 40.0f, 42.0f, vx_scale(vx_CYAN, v->ch_amt), 1);
}

typedef void (*vx_scene_fn)(const vis_state_t *);
static const vx_scene_fn vx_SCENES[NTRACK][ENG_PATCHES] = {
    {sc_box, sc_boom, sc_club}, {sc_pluck, sc_acid, sc_round}, {sc_pad, sc_stab, sc_wind},
    {sc_brass, sc_glass, sc_funk}, {sc_choir, sc_robot, sc_monk},
};

/* ------------------------------------------------------------ the frame --- */
static struct {
    uint32_t last_ms;
    float run[NTRACK][ENG_PATCHES][4];
    float pitch[NTRACK];
    float env[NTRACK];                   /* each track's level as the pictures see it: up at once, down slowly */
} vx_vis;

uint16_t vis_track_rgb(int track)
{
    static const uint16_t C[NTRACK] = {RGB(255, 196, 60), RGB(255, 50, 170), RGB(50, 220, 255), RGB(255, 120, 40),
                                       RGB(150, 100, 255)};
    return track >= 0 && track < NTRACK ? C[track] : C_WHITE;
}

void vis_init(void) { memset(&vx_vis, 0, sizeof vx_vis); }

void vis_gather(vis_state_t *v, int track, int patch)
{
    uint32_t now = plat_ms();
    float dt = vx_vis.last_ms ? (float)(now - vx_vis.last_ms) * 0.001f : 0.0f, *r;
    int t;
    if (dt > 0.2f)
        dt = 0.2f;
    vx_vis.last_ms = now;
    memset(v, 0, sizeof *v);
    v->track = track;
    v->patch = patch;
    for (t = 0; t < NTRACK; t++) {
        float l = fm_minf(eng_level(t) * 1.8f, 1.0f);
        vx_vis.env[t] = l > vx_vis.env[t] ? l : vx_vis.env[t] * fm_expf(-dt * 3.0f);   /* a third of a second to fall */
        v->level[t] = vx_vis.env[t];
    }
    v->pulse = eng_pulse();
    v->duck = eng_duck();
    v->phaser = eng_phase(0);
    v->chorus = eng_phase(1);
    v->vowel = eng_vowel();
    v->tone = eng_knob_get(track, K_TONE);
    v->motion = eng_knob_get(track, K_MOTION);
    v->pump = eng_knob_get(track, K_PUMP);
    v->sweep = eng_knob_get(track, K_SWEEP);
    v->space = eng_knob_get(track, K_SPACE);
    v->ph_amt = eng_knob_get(track, K_PHASER);
    v->ch_amt = eng_knob_get(track, K_CHORUS);
    v->grit = eng_knob_get(track, K_GRIT);
    v->echo = eng_knob_get(track, K_ECHO);
    v->note = eng_note_now(track);
    if (v->note >= 0)
        vx_vis.pitch[track] = fm_clampf((float)(v->note - 36) / 48.0f, 0.0f, 1.0f);
    v->pitch = vx_vis.pitch[track];
    v->beat = (float)seq_beat_phase() / (float)SEQ_PPQ;
    v->t = (float)now * 0.001f;
    v->playing = seq_playing();
    v->step = seq_step_now(track) < 0 ? 0 : seq_step_now(track);
    r = vx_vis.run[track][patch];
    /* the clocks: [0] the background's drift, steady with MOTION; [1] the moving part's, quicker with the sound */
    r[0] += dt * (0.3f + 1.5f * v->motion);
    r[1] += dt * (1.0f + 3.0f * v->motion) * (0.4f + 1.2f * v->level[track]);
    r[2] += dt;
    r[3] += dt * seq_tempo_get() / 60.0f;
    memcpy(v->run, r, sizeof v->run);
}

void vis_draw(const vis_state_t *v, int32_t y0)
{
    vx_sy = y0;
    vx_bright = 1.0f - 0.55f * v->duck;     /* the pump: the light dips with the bus */
    vx_SCENES[v->track % NTRACK][v->patch % ENG_PATCHES](v);
    vx_after(v);
    vx_bright = 1.0f;
}
