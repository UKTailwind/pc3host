/*
 * display_seam - the kernel's display core, run on a PC.
 *
 * display.c (Kernel/platform/platform-rpipico) is the portable half of
 * the PC3 display driver.  This program is everything display_priv.h
 * and display.h say the hardware provides - the three framebuffers, the
 * raster and handover hooks, the blanking wait, the font table, the
 * console's mode callback - reduced to stand-ins that record what they
 * were asked, and then a walk through the primitives with the answers
 * checked against what the board's code must produce.
 *
 * Two things this proves, and one it does not.  It proves the seam is
 * clean: display.c compiles with -DPC3_HOST and no kernel header.  It
 * proves the primitives behave the same on a 64-bit little-endian host
 * as the RGB121 palette, MSB-first 1bpp and high-nibble-left 4bpp
 * layouts say they should.  It does NOT prove the kernel still drives
 * the monitor - only the board can (REVIEW.md, section 6, phase 0).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "display.h"
#include "display_priv.h"
#include "pico_ioctl.h"

/* --- what the kernel would have supplied ---------------------------------- */

uint8_t disp_fb[DISP_FB_POOL] __attribute__((aligned(4)));
uint8_t disp_fb2[DISP_FB_POOL] __attribute__((aligned(4)));
uint8_t disp_fb3[DISP_FB_POOL] __attribute__((aligned(4)));

int display_fb2_ok(void) { return 1; }
int display_fb3_ok(void) { return 1; }

static int raster_now = DISP_RASTER_VGA;    /* the console's, at boot */
static int n_prepare, n_tables, n_handover, n_finish, n_palette;
static int last_rebuild = -1, last_console_gfx = -1;
static int handover_before_store;       /* the contract, counted from inside */
static enum gexp handed = EXP_CONSOLE;

int disp_hw_mode_prepare(int raster)
{
    int rebuild = raster != raster_now;

    n_prepare++;
    raster_now = raster;
    return rebuild;
}

void disp_hw_mode_tables(enum gexp ex) { (void)ex; n_tables++; }

/* By the time this is called the core has stored gfx_exp itself; the
 * hardware only has to make it visible.  So a stand-in has nothing to
 * do but notice - which is the property that makes the seam safe to
 * implement on a PC. */
void disp_hw_mode_handover(enum gexp ex, int raster)
{
    (void)raster;
    handed = ex;
    if (gfx_exp != ex)
        handover_before_store++;
    n_handover++;
}

void disp_hw_mode_finish(int rebuild) { last_rebuild = rebuild; n_finish++; }
void disp_hw_palette_changed(enum gexp ex) { (void)ex; n_palette++; }

void display_stack_check(void) { }
void display_wait_vblank(void) { }

/* The process table, as far as the display core needs one. */
struct p_tab { struct p_tab *parent; };
struct p_tab *disp_who_parent(struct p_tab *who) { return who->parent; }

void console_gfx(int active) { last_console_gfx = active; }

/* One glyph, 'A', an 8x8 diagonal: MMBasic's layout, header then bits,
 * MSB first because 64 bits is a multiple of 8. */
static const unsigned char font_a[4 + 8] = {
    8, 8, 'A', 1,
    0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
};

const unsigned char *display_font(int font, int *w, int *h, int *first,
                                  int *count)
{
    if (font != 1)
        return NULL;
    *w = font_a[0]; *h = font_a[1]; *first = font_a[2]; *count = font_a[3];
    return font_a;
}

/* --- the checks ------------------------------------------------------------ */

static int fails;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        fails++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static void geom(uint16_t *w, uint16_t *h, uint16_t *s, uint8_t *bpp,
                 uint8_t *mode)
{
    display_gfx_geom(w, h, s, bpp, mode);
}

int main(void)
{
    uint16_t w, h, s;
    uint8_t bpp, mode;
    int r, i;

    /* --- MODE 7: MMBasic's MODE 2, 320x240 in sixteen colours --------- */
    r = display_gfx_mode(7);
    CHECK(r == 160 * 240, "mode 7 size %d", r);
    geom(&w, &h, &s, &bpp, &mode);
    CHECK(w == 320 && h == 240 && s == 160 && bpp == 4 && mode == 7,
          "mode 7 geometry %d x %d stride %d bpp %d mode %d", w, h, s, bpp, mode);
    CHECK(n_prepare == 1 && n_tables == 1 && n_handover == 1 && n_finish == 1,
          "hook sequence %d %d %d %d", n_prepare, n_tables, n_handover, n_finish);
    CHECK(last_rebuild == 0, "mode 7 shares the console's raster");
    CHECK(handed == EXP_4BPP_X2, "handover expander %d", (int)handed);
    CHECK(last_console_gfx == 1, "console told a graphics mode is live");
    CHECK(display_gfx_size() == 38400 && display_gfx_fbsize() == 38400,
          "sizes %d %d", display_gfx_size(), display_gfx_fbsize());

    /* RGB121: the index IS the colour's top bits, palette or no palette */
    CHECK(display_gfx_map(0xFF0000) == 8, "red -> 8");
    CHECK(display_gfx_map(0x00FF00) == 6, "green -> 6");
    CHECK(display_gfx_map(0x0000FF) == 1, "blue -> 1");
    CHECK(display_gfx_map(0xFFFFFF) == 15, "white -> 15");
    CHECK(display_gfx_map(0x000000) == 0, "black -> 0");

    /* one pixel, there and back */
    display_gfx_colour(0xFF0000);
    CHECK(display_gfx_curcol() == 8, "current colour %d", display_gfx_curcol());
    CHECK(display_gfx_pixel(10, 10, display_gfx_curcol()) == 0, "pixel");
    CHECK(display_gfx_getpixel(10, 10) == 0xFF0000, "getpixel %06x",
          display_gfx_getpixel(10, 10));
    CHECK(display_gfx_getpixel(11, 10) == 0x000000, "neighbour untouched");
    CHECK(disp_fb[10 * 160 + 5] == 0x80, "even x is the HIGH nibble: %02x",
          disp_fb[10 * 160 + 5]);
    CHECK(display_gfx_pixel(400, 10, 1) == 0, "off-screen pixel is not an error");
    CHECK(display_gfx_getpixel(400, 10) == -1, "off-screen read is -1");

    /* a batch with colours: the map is applied per item */
    {
        struct gfx_pt pts[2] = { { 1, 1 }, { 2, 1 } };
        uint32_t cols[2] = { 0x0000FF, 0x00FF00 };

        CHECK(display_gfx_pixels(pts, 2, cols) == 0, "pixels");
        CHECK(display_gfx_getpixel(1, 1) == 0x0000FF, "batch blue %06x",
              display_gfx_getpixel(1, 1));
        CHECK(display_gfx_getpixel(2, 1) == 0x00FF00, "batch green %06x",
              display_gfx_getpixel(2, 1));
    }

    /* a run of text through the font table */
    r = display_gfx_text(100, 100, 1, 1, 15, 0, (const uint8_t *)"A", 1,
                         GORIENT_N);
    CHECK(r == 108, "text advanced to %d", r);
    for (i = 0; i < 8; i++)
        CHECK(display_gfx_getpixel(100 + i, 100 + i) == 0xFFFFFF,
              "diagonal %d", i);
    CHECK(display_gfx_getpixel(101, 100) == 0x000000, "paper painted black");
    r = display_gfx_text(200, 100, 1, 2, 15, -1, (const uint8_t *)"A", 1,
                         GORIENT_N);
    CHECK(r == 216, "scaled text advanced to %d", r);
    CHECK(display_gfx_getpixel(214, 114) == 0xFFFFFF, "scale 2 bottom-right");

    /* a filled rectangle is a memset per row: every byte of the pool */
    CHECK(display_gfx_rect(0, 0, 319, 239, 15) == 0, "rect");
    for (i = 0; i < 38400; i++)
        if (disp_fb[i] != 0xFF)
            break;
    CHECK(i == 38400, "rect left byte %d as %02x", i, disp_fb[i]);
    CHECK(display_gfx_rect(0, 0, 319, 239, 0) == 0, "clear");
    CHECK(display_gfx_rect(3, 0, 3, 0, 5) == 0 && disp_fb[1] == 0x05,
          "odd-x single pixel is the LOW nibble: %02x", disp_fb[1]);

    /* scroll: picture up by five */
    memset(disp_fb, 0, sizeof disp_fb);
    display_gfx_pixel(5, 5, 15);
    CHECK(display_gfx_scroll(5, 0) == 0, "scroll");
    CHECK(display_gfx_getpixel(5, 0) == 0xFFFFFF && display_gfx_getpixel(5, 5) == 0,
          "scrolled pixel");
    /* two-axis with wrap */
    memset(disp_fb, 0, sizeof disp_fb);
    display_gfx_pixel(0, 0, 15);
    CHECK(display_gfx_scroll2(1, 0, -2) == 0, "scroll2");
    CHECK(display_gfx_getpixel(1, 0) == 0xFFFFFF, "scroll2 moved right");
    CHECK(display_gfx_scroll2(-1, 0, -2) == 0, "scroll2 back");
    CHECK(display_gfx_getpixel(0, 0) == 0xFFFFFF, "scroll2 wrapped back");

    /* MAP: pending until SET, then live.  Index 1 is RGB121 blue; index
     * 3 would be blue with the low green bit, 0x0048FF, which is the
     * kind of arithmetic this test exists to pin down. */
    memset(disp_fb, 0, sizeof disp_fb);
    display_gfx_pixel(9, 9, 3);
    CHECK(display_gfx_getpixel(9, 9) == 0x0048FF, "index 3 is %06x",
          display_gfx_getpixel(9, 9));
    display_gfx_pixel(9, 9, 1);
    CHECK(display_gfx_getpixel(9, 9) == 0x0000FF, "index 1 is blue %06x",
          display_gfx_getpixel(9, 9));
    CHECK(display_gfx_remap(1, 0x00FF00) == 0, "remap");
    CHECK(display_gfx_getpixel(9, 9) == 0x0000FF, "remap is pending");
    CHECK(display_gfx_remap_apply() == 0, "remap apply");
    CHECK(display_gfx_getpixel(9, 9) == 0x00FF00, "remap live %06x",
          display_gfx_getpixel(9, 9));
    CHECK(n_palette == 1, "palette hook %d", n_palette);
    CHECK(display_gfx_remap_reset() == 0 && display_gfx_getpixel(9, 9) == 0x0000FF,
          "remap reset");

    /* --- the off-screen buffers, and whose they are -------------------- */
    {
        struct p_tab me = { NULL }, kid = { &me }, other = { NULL };

        memset(disp_fb, 0, sizeof disp_fb);
        memset(disp_fb2, 0, sizeof disp_fb2);
        memset(disp_fb3, 0, sizeof disp_fb3);
        display_fb_enter(&me);
        CHECK(display_fb_open(&me, 1, DISP_FB_F) == 0, "create F");
        CHECK(display_fb_open(&other, 1, DISP_FB_L) == -2, "second claimant refused");
        CHECK(display_fb_select(&me, DISP_FB_F) == 0, "write F");
        CHECK(display_fb_target() == disp_fb2, "primitives point at F");
        display_gfx_pixel(3, 3, 15);            /* odd x: the low nibble */
        CHECK(disp_fb2[3 * 160 + 1] == 0x0F && disp_fb[3 * 160 + 1] == 0,
              "drawn into F, not the screen: F %02x screen %02x",
              disp_fb2[3 * 160 + 1], disp_fb[3 * 160 + 1]);
        display_fb_enter(&kid);
        CHECK(display_fb_target() == disp_fb2, "a child draws where the owner draws");
        display_fb_enter(&other);
        CHECK(display_fb_target() == disp_fb, "a stranger gets the screen");
        display_fb_enter(&me);
        CHECK(display_fb_select(&me, DISP_FB_N) == 0, "back to the screen");
        CHECK(display_gfx_getpixel(3, 3) == 0, "screen still clear");
        CHECK(display_fb_copy(&me, DISP_FB_F, DISP_FB_N) == 0, "copy F to N");
        CHECK(display_gfx_getpixel(3, 3) == 0xFFFFFF, "copied");

        /* the layer over F: transparent index 0 lets F show */
        CHECK(display_fb_open(&me, 1, DISP_FB_L) == 0, "create L");
        CHECK(display_fb_select(&me, DISP_FB_L) == 0, "write L");
        display_gfx_pixel(7, 7, 6);
        CHECK(display_fb_select(&me, DISP_FB_N) == 0, "screen");
        memset(disp_fb, 0, sizeof disp_fb);
        CHECK(display_fb_merge(&me, 0) == 0, "merge");
        CHECK(display_gfx_getpixel(3, 3) == 0xFFFFFF, "F came through");
        CHECK(display_gfx_getpixel(7, 7) == 0x00FF00, "layer on top %06x",
              display_gfx_getpixel(7, 7));
        CHECK(display_gfx_getpixel(8, 8) == 0, "transparent where the layer is clear");
        CHECK(display_fb_merge(&me, 16) == -1, "bad transparent index");
        display_fb_release(&me);
        CHECK(display_fb_target() == disp_fb, "released");
        CHECK(display_fb_open(&other, 1, DISP_FB_F) == 0, "free for the next");
        display_fb_release(&other);
    }

    /* --- MODE 0: 640x256, one bit a pixel, the other raster ------------- */
    r = display_gfx_mode(0);
    CHECK(r == 80 * 256, "mode 0 size %d", r);
    CHECK(last_rebuild == 1, "crossing to 1024x768 restarts the scanout");
    geom(&w, &h, &s, &bpp, &mode);
    CHECK(w == 640 && h == 256 && s == 80 && bpp == 1 && mode == 0,
          "mode 0 geometry %d x %d stride %d bpp %d", w, h, s, bpp);
    CHECK(display_gfx_map(0x123456) == 1 && display_gfx_map(0) == 0,
          "1bpp: anything but black is ink");
    display_gfx_pixel(639, 255, 1);
    CHECK(disp_fb[255 * 80 + 79] == 0x01, "MSB is the LEFTMOST pixel: %02x",
          disp_fb[255 * 80 + 79]);
    CHECK(display_gfx_getpixel(639, 255) == 0xFFFFFF, "1bpp read back");
    CHECK(display_gfx_remap(1, 0x00FF00) == -1, "no MAP in a two-colour mode");
    CHECK(display_gfx_mode(6) == -1 && display_gfx_mode(8) == -1,
          "modes we do not have");

    /* --- back to the console: drawable, with tile colours -------------- */
    r = display_gfx_mode(0xFF);
    CHECK(r == 0, "console returns 0");
    CHECK(last_rebuild == 1 && last_console_gfx == 0, "console repainted");
    CHECK(handover_before_store == 0,
          "gfx_exp was stored by the core before every handover (%d were not)",
          handover_before_store);
    geom(&w, &h, &s, &bpp, &mode);
    CHECK(w == 640 && h == 480 && s == 80 && bpp == 1 && mode == 0xFF,
          "console geometry %d x %d stride %d bpp %d mode %02x", w, h, s, bpp, mode);
    CHECK(display_gfx_fbsize() == 38400 && display_gfx_size() == 0,
          "console is drawable but has no graphics size");
    disp_tile_fg[0] = 0xE0;                 /* RGB332 red for cell 0 */
    disp_tile_bg[0] = 0x00;
    display_gfx_pixel(0, 0, 1);
    CHECK(display_gfx_getpixel(0, 0) == 0xFF0000, "console ink is the cell's fg %06x",
          display_gfx_getpixel(0, 0));
    CHECK(display_gfx_getpixel(1, 0) == 0x000000, "console paper is the cell's bg");

    if (fails) {
        printf("display seam: %d check(s) FAILED\n", fails);
        return 1;
    }
    printf("display seam: all checks passed "
           "(%d mode switches, %d palette rebuilds)\n", n_prepare, n_palette);
    return 0;
}
