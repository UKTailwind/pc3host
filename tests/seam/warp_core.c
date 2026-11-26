/*
 * warp_core - FRAMEBUFFER WARPH, WARPV, AFFINE and SPAN's core
 * (display_warp.c) against a pixel-by-pixel model.
 *
 * The model is written from the rules - the PicoMite's design documents,
 * docs/FRAMEBUFFER_WARP_Design.md sections 4 to 6 and
 * docs/FRAMEBUFFER_AFFINE_SPAN_Design.md sections 4 and 8 - and not from
 * the code: each destination pixel is worked out on its own, AFFINE's
 * places in 128-bit arithmetic, and every byte of the destination is
 * compared, the pixels the command must not touch included.
 *
 * Built twice: with PC_HOST, in a PC's pixel order (the PicoMite's, the
 * left pixel in the low nibble), and without, in the board's (the left
 * pixel in the high nibble).  Both read display_priv.h's own PX4_ macros.
 *
 *   warp_core [cases [seed]]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "display.h"
#include "display_priv.h"

#define WARP_CORE_TEST
#include "display_warp.c"

typedef __int128 i128;

/* --- a small generator, so a run can be repeated from its seed ------------ */
static uint64_t rs;

static uint64_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}

/* lo to hi, both included */
static int64_t ri(int64_t lo, int64_t hi)
{
    /* in unsigned arithmetic: hi - lo can pass INT64_MAX */
    uint64_t span = (uint64_t)hi - (uint64_t)lo + 1;

    return (int64_t)((uint64_t)lo + rnd() % span);
}

static int chance(int pct)
{
    return (int)(rnd() % 100) < pct;
}

/* --- the buffers, and pixels read and written by the rule alone ------------ */
#define MAXW 640
#define MAXH 480
static uint8_t S[MAXW / 2 * MAXH], D[MAXW / 2 * MAXH], E[MAXW / 2 * MAXH];
static int W, H, ST;

/* the board keeps the left pixel in the high nibble, a PC in the low */
static int mget(const uint8_t *b, int x, int y)
{
    uint8_t v = b[y * ST + x / 2];
#ifdef PC_HOST
    return (x & 1) ? v >> 4 : v & 15;
#else
    return (x & 1) ? v & 15 : v >> 4;
#endif
}

static void mput(uint8_t *b, int x, int y, int c)
{
    uint8_t *p = &b[y * ST + x / 2];
#ifdef PC_HOST
    *p = (x & 1) ? (uint8_t)((*p & 0x0F) | (c << 4)) : (uint8_t)((*p & 0xF0) | c);
#else
    *p = (x & 1) ? (uint8_t)((*p & 0xF0) | c) : (uint8_t)((*p & 0x0F) | (c << 4));
#endif
}

static int64_t pmod(int64_t a, int64_t m)
{
    int64_t r = a % m;

    return r < 0 ? r + m : r;
}

static i128 floor128(i128 a, i128 b)       /* b > 0 */
{
    i128 q = a / b;

    return (a % b < 0) ? q - 1 : q;
}

static i128 mod128(i128 a, i128 m)
{
    i128 r = a % m;

    return r < 0 ? r + m : r;
}

static void fillrand(uint8_t *b)
{
    int i;

    for (i = 0; i < ST * H; i++)
        b[i] = (uint8_t)rnd();
}

static int fails, cases;

static void compare(const char *what, int c)
{
    int x, y, shown = 0;

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            if (mget(D, x, y) != mget(E, x, y)) {
                if (!shown++) {
                    fails++;
                    if (fails <= 10)
                        printf("FAIL case %d %s %dx%d: pixel %d,%d is %d, wants %d\n",
                               c, what, W, H, x, y, mget(D, x, y), mget(E, x, y));
                }
            }
    /* the bytes too: a 4-bit row has no padding, but say so if it had */
    if (!shown && memcmp(D, E, (size_t)ST * H) != 0) {
        fails++;
        printf("FAIL case %d %s: bytes differ where pixels agree\n", c, what);
    }
}

/* a size: mostly small, so a case is cheap and edges are close, sometimes
 * the PC3's own */
static void pick_size(void)
{
    int k = (int)ri(0, 99);

    if (k < 3)
        W = 320, H = 240;
    else if (k < 4)
        W = 640, H = 480;
    else if (k < 5)
        W = 160, H = 256;
    else
        W = 2 * (int)ri(1, 24), H = (int)ri(1, 24);
    ST = W / 2;
}

/* an edge as the request carries it: wrap, leave, or a pixel value */
static int pick_edge(int wrap_ok)
{
    if (wrap_ok && chance(45))
        return WARP_WRAP;
    if (chance(30))
        return WARP_LEAVE;
    return (int)ri(0, 15);
}

/* --- WARPH and WARPV -------------------------------------------------------- */
static void case_warp(int c)
{
    static int32_t boff[MAXW], brow[MAXW];
    static int64_t off[MAXW], rows[MAXW];
    struct wp_warp a;
    int vertical = chance(50), bands, size, n, step, first, edge, trans, b, x, y;
    int userows;
    int64_t phase;

    pick_size();
    bands = vertical ? W : H;
    size = vertical ? H : W;
    step = chance(40) ? 1 : (int)ri(1, bands < 40 ? bands : 40);
    first = chance(40) ? 0 : (int)ri(0, bands - 1);
    n = (int)ri(1, (bands - first + step - 1) / step);
    for (b = 0; b < n; b++) {
        int k = (int)ri(0, 9);
        off[b] = k < 6 ? ri(-size - 2, size + 2)
               : k < 8 ? ri(-3 * size, 3 * size)
               : ri(-((int64_t)1 << 40), (int64_t)1 << 40);
    }
    phase = chance(50) ? 0 : chance(80) ? ri(-50, 50) : ri(-((int64_t)1 << 40), (int64_t)1 << 40);
    edge = pick_edge(1);
    trans = chance(30) ? (int)ri(0, 15) : -1;
    userows = !vertical && chance(40);
    if (userows)
        for (b = 0; b < n; b++)
            rows[b] = ri(-1, H - 1);

    /* what the runtime hands over: band b takes element (b + phase) MOD
     * n, its offset reduced round the buffer or clamped to it */
    for (b = 0; b < n; b++) {
        int e = (int)pmod((int64_t)b + pmod(phase, n), n);
        int64_t o = off[e];

        if (edge == WARP_WRAP)
            o = pmod(o, size);
        else if (o > size)
            o = size;
        else if (o < -size)
            o = -size;
        boff[b] = (int32_t)o;
        brow[b] = (int32_t)rows[e];
    }

    fillrand(S);
    fillrand(D);
    memcpy(E, D, (size_t)ST * H);
    /* the model: every pixel of every band, from the raw arrays */
    for (b = 0; b < n; b++) {
        int e = (int)pmod((int64_t)b + pmod(phase, n), n);
        int lo = first + b * step, hi = lo + step > bands ? bands : lo + step;
        int64_t o = off[e];

        if (vertical) {
            for (x = lo; x < hi; x++)
                for (y = 0; y < H; y++) {
                    int64_t sy = (int64_t)y + (edge == WARP_WRAP ? pmod(o, H) : o);
                    int p;

                    if (edge == WARP_WRAP)
                        sy = pmod(sy, H);
                    else if (sy < 0 || sy >= H) {
                        if (edge >= 0)
                            mput(E, x, y, edge);
                        continue;
                    }
                    p = mget(S, x, (int)sy);
                    if (p != trans)
                        mput(E, x, y, p);
                }
        } else {
            if (userows && rows[e] < 0)
                continue;
            for (y = lo; y < hi; y++) {
                int sy = userows ? (int)pmod(rows[e] + (y - lo), H) : y;

                for (x = 0; x < W; x++) {
                    int64_t sx = (int64_t)x - (edge == WARP_WRAP ? pmod(o, W) : o);
                    int p;

                    if (edge == WARP_WRAP)
                        sx = pmod(sx, W);
                    else if (sx < 0 || sx >= W) {
                        if (edge >= 0)
                            mput(E, x, y, edge);
                        continue;
                    }
                    p = mget(S, (int)sx, sy);
                    if (p != trans)
                        mput(E, x, y, p);
                }
            }
        }
    }

    a.dst = D;
    a.src = S;
    a.w = W;
    a.h = H;
    a.stride = ST;
    a.vertical = vertical;
    a.n = n;
    a.step = step;
    a.first = first;
    a.boff = boff;
    a.brow = userows ? brow : NULL;
    a.edge = edge;
    a.trans = trans;
    wp_warp_run(&a);
    compare(vertical ? "WARPV" : "WARPH", c);
}

/* --- SPAN --------------------------------------------------------------------- */
static void case_span(int c)
{
    static int64_t lf[MAXH + 8], rt[MAXH + 8];
    struct wp_span a;
    int n, step, first, fill, trans, outside, same, b, x, y;

    pick_size();
    step = chance(40) ? 1 : (int)ri(1, H < 40 ? H : 40);
    n = (int)ri(1, H / step + 3);
    first = chance(40) ? 0 : (int)ri(-(n * step) / 2 - 2, H + 2);
    for (b = 0; b < n; b++) {
        lf[b] = chance(10) ? ri(-((int64_t)1 << 40), (int64_t)1 << 40) : ri(-10, W + 10);
        rt[b] = chance(10) ? ri(-((int64_t)1 << 40), (int64_t)1 << 40) : ri(-10, W + 10);
    }
    fill = chance(50) ? -1 : (int)ri(0, 15);
    trans = chance(30) ? (int)ri(0, 15) : -1;
    outside = chance(50);
    same = chance(20);

    fillrand(S);
    fillrand(D);
    memcpy(E, D, (size_t)ST * H);
    for (b = 0; b < n; b++)
        for (y = first + b * step; y < first + (b + 1) * step; y++) {
            if (y < 0 || y >= H)
                continue;
            for (x = 0; x < W; x++) {
                int inside = lf[b] <= x && x <= rt[b];

                if (inside != outside) {
                    if (!same) {
                        int p = mget(S, x, y);

                        if (p != trans)
                            mput(E, x, y, p);
                    }
                } else if (fill >= 0)
                    mput(E, x, y, fill);
            }
        }

    a.dst = D;
    a.src = same ? D : S;
    a.w = W;
    a.h = H;
    a.stride = ST;
    a.n = n;
    a.step = step;
    a.first = first;
    a.lf = lf;
    a.rt = rt;
    a.fill = fill;
    a.trans = trans;
    a.outside = outside;
    wp_span_run(&a);
    compare(same ? "SPAN (one buffer)" : "SPAN", c);
}

/* --- AFFINE ------------------------------------------------------------------- */
static void case_affine(int c)
{
    static int64_t u[MAXH + 8], v[MAXH + 8], du[MAXH + 8], dv[MAXH + 8];
    struct wp_affine a;
    int n, step, first, left, width, edge, trans, kind, b, x, y;
    const int64_t FP = 65536;

    pick_size();
    step = chance(40) ? 1 : (int)ri(1, H < 40 ? H : 40);
    n = (int)ri(1, H / step + 3);
    first = chance(40) ? 0 : (int)ri(-(n * step) / 2 - 2, H + 2);
    left = chance(50) ? 0 : (int)ri(-W / 2 - 2, W + 2);
    width = chance(50) ? W : (int)ri(1, W + 20);
    edge = pick_edge(1);
    trans = chance(30) ? (int)ri(0, 15) : -1;
    kind = (int)ri(0, 8);
    for (b = 0; b < n; b++)
        switch (kind) {
        case 0:     /* the picture as it is, moved */
            du[b] = FP, dv[b] = 0, u[b] = ri(-W, W) * FP, v[b] = ri(-H, H) * FP;
            break;
        case 1:     /* any small step */
            du[b] = ri(-3 * FP, 3 * FP), dv[b] = ri(-3 * FP, 3 * FP);
            u[b] = ri(-4 * W * FP, 4 * W * FP), v[b] = ri(-4 * H * FP, 4 * H * FP);
            break;
        case 2:     /* a quarter turn */
            du[b] = 0, dv[b] = FP * (2 * ri(0, 1) - 1);
            u[b] = ri(-W, W) * FP, v[b] = ri(-H, H) * FP;
            break;
        case 3:     /* far away, with long steps */
            du[b] = ri(-((int64_t)1 << 30), (int64_t)1 << 30);
            dv[b] = ri(-((int64_t)1 << 30), (int64_t)1 << 30);
            u[b] = ri(-((int64_t)1 << 44), (int64_t)1 << 44);
            v[b] = ri(-((int64_t)1 << 44), (int64_t)1 << 44);
            break;
        case 4:     /* steps of a few thousandths of a pixel */
            du[b] = ri(-300, 300), dv[b] = ri(-300, 300);
            u[b] = ri(0, W * FP), v[b] = ri(0, H * FP);
            break;
        case 5:     /* a mirrored row */
            du[b] = -FP, dv[b] = 0;
            u[b] = ri(-W, 2 * W) * FP + FP / 2, v[b] = ri(-H, 2 * H) * FP;
            break;
        case 6:     /* the extremes: the clamps, and wrapping from them */
            u[b] = chance(50) ? INT64_MAX - ri(0, 1000) : INT64_MIN + ri(0, 1000);
            v[b] = chance(50) ? INT64_MAX - ri(0, 1000) : ri(-((int64_t)1 << 62), (int64_t)1 << 62);
            du[b] = chance(50) ? ri(-((int64_t)1 << 40), (int64_t)1 << 40) : ri(-3 * FP, 3 * FP);
            dv[b] = chance(50) ? INT64_MIN + ri(0, 9) : ri(-3 * FP, 3 * FP);
            break;
        case 7: {   /* a step past the clamp, aimed so that the clamped
                       step, and only it, lands column i0 in the source */
            int64_t sg = chance(50) ? 1 : -1, i0 = ri(0, W - 1);
            int64_t big = sg * (((int64_t)1 << 35) + ri(1, (int64_t)1 << 30));
            int64_t aim = -sg * i0 * ((int64_t)1 << 35);

            if (chance(50)) {   /* along U */
                du[b] = big, dv[b] = 0;
                u[b] = aim + ri(0, W * FP - 1), v[b] = ri(0, H * FP - 1);
            } else {            /* along V */
                du[b] = 0, dv[b] = big;
                u[b] = ri(0, W * FP - 1), v[b] = aim + ri(0, H * FP - 1);
            }
            break;
        }
        default:    /* anything near the source */
            du[b] = ri(-2 * W * FP, 2 * W * FP), dv[b] = ri(-2 * H * FP, 2 * H * FP);
            u[b] = ri(-2 * W * FP, 3 * W * FP), v[b] = ri(-2 * H * FP, 3 * H * FP);
            break;
        }

    fillrand(S);
    fillrand(D);
    memcpy(E, D, (size_t)ST * H);
    for (b = 0; b < n; b++) {
        i128 ub = u[b], vb = v[b], dub = du[b], dvb = dv[b];

        if (edge != WARP_WRAP) {
            /* past these the place or the step is taken as the limit
             * (design section 4), so that no sum can overflow */
            const i128 P = (i128)1 << 60, Q = (i128)1 << 35;
            ub = ub > P ? P : ub < -P ? -P : ub;
            vb = vb > P ? P : vb < -P ? -P : vb;
            dub = dub > Q ? Q : dub < -Q ? -Q : dub;
            dvb = dvb > Q ? Q : dvb < -Q ? -Q : dvb;
        }
        for (y = first + b * step; y < first + (b + 1) * step; y++) {
            int j = y - (first + b * step);

            if (y < 0 || y >= H)
                continue;
            for (x = 0; x < W; x++) {
                int i = x - left, p;
                i128 U, V;
                int64_t sx, sy;

                if (i < 0 || i >= width)
                    continue;
                U = ub - j * dvb + i * dub;
                V = vb + j * dub + i * dvb;
                sx = (int64_t)(edge == WARP_WRAP ? mod128(floor128(U, FP), W) : 0);
                sy = (int64_t)(edge == WARP_WRAP ? mod128(floor128(V, FP), H) : 0);
                if (edge != WARP_WRAP) {
                    i128 fx = floor128(U, FP), fy = floor128(V, FP);

                    if (fx < 0 || fx >= W || fy < 0 || fy >= H) {
                        if (edge >= 0)
                            mput(E, x, y, edge);
                        continue;
                    }
                    sx = (int64_t)fx, sy = (int64_t)fy;
                }
                p = mget(S, (int)sx, (int)sy);
                if (p != trans)
                    mput(E, x, y, p);
            }
        }
    }

    a.dst = D;
    a.src = S;
    a.w = W;
    a.h = H;
    a.stride = ST;
    a.n = n;
    a.step = step;
    a.first = first;
    a.left = left;
    a.width = width;
    a.u = u;
    a.v = v;
    a.du = du;
    a.dv = dv;
    a.edge = edge;
    a.trans = trans;
    wp_affine_run(&a);
    compare("AFFINE", c);
}

/* --- BLIT RESIZE -------------------------------------------------------------- */
static void case_resize(int c)
{
    static uint8_t S0[MAXW / 2 * MAXH], scratch[MAXW / 2 * MAXH];
    struct wp_resize a;
    int same, sx, sy, sw, sh, dx, dy, dw, dh, trans, x, y;

    pick_size();
    same = chance(30);
    sw = (int)ri(1, W);
    sh = (int)ri(1, H);
    sx = (int)ri(0, W - sw);
    sy = (int)ri(0, H - sh);
    dw = chance(30) ? sw * (int)ri(1, 4) : chance(30) ? (sw + (int)ri(0, 3)) / (int)ri(1, 4) + 1
                                                       : (int)ri(1, 2 * W);
    dh = chance(30) ? sh * (int)ri(1, 4) : chance(30) ? (sh + (int)ri(0, 3)) / (int)ri(1, 4) + 1
                                                       : (int)ri(1, 2 * H);
    dx = (int)ri(-W, W);
    dy = (int)ri(-H, H);
    trans = chance(30) ? (int)ri(0, 15) : -1;

    fillrand(S);
    fillrand(D);
    memcpy(E, D, (size_t)ST * H);
    memcpy(S0, same ? D : S, (size_t)ST * H);   /* the source as it was */
    for (y = dy < 0 ? 0 : dy; y < dy + dh && y < H; y++)
        for (x = dx < 0 ? 0 : dx; x < dx + dw && x < W; x++) {
            int px = sx + (int)((int64_t)(x - dx) * sw / dw);
            int py = sy + (int)((int64_t)(y - dy) * sh / dh);
            int p = mget(S0, px, py);

            if (p != trans)
                mput(E, x, y, p);
        }

    a.dst = D;
    a.src = same ? D : S;
    a.w = W;
    a.h = H;
    a.stride = ST;
    a.sx = sx, a.sy = sy, a.sw = sw, a.sh = sh;
    a.dx = dx, a.dy = dy, a.dw = dw, a.dh = dh;
    a.trans = trans;
    a.scratch = scratch;
    wp_resize_run(&a);
    compare(same ? "RESIZE (one buffer)" : "RESIZE", c);
}

int main(int argc, char **argv)
{
    int total = argc > 1 ? atoi(argv[1]) : 100000, c;

    rs = argc > 2 ? strtoull(argv[2], NULL, 0) : 2026;
    if (rs == 0)
        rs = 1;
    for (c = 1; c <= total; c++) {
        switch (c % 5) {
        case 0: case 1: case_warp(c); break;
        case 2: case_span(c); break;
        case 3: case_affine(c); break;
        default: case_resize(c); break;
        }
        cases++;
    }
    printf("warp_core (%s order): %d cases, %d wrong\n",
#ifdef PC_HOST
           "PC",
#else
           "board",
#endif
           cases, fails);
    return fails != 0;
}
