/*
 * disphw.c - the hardware half of the display, for a PC.
 *
 * display_priv.h names what the kernel's display core asks of the
 * scanout; display_hstx.c answers for the PC3 and this file answers
 * for the server.  There is no scanout here: the framebuffers are
 * plain arrays, a "raster change" is a note for the window code, the
 * barrier is nothing (one thread), and blanking is the frame tick in
 * pc3d.c.  So most of these do nothing but record, and that is the
 * property the seam was cut to have.
 */

#include <string.h>
#include "display.h"
#include "display_priv.h"
#include "pc3d.h"

/* The three framebuffers.  On the board disp_fb is SRAM and the other
 * two are the PSRAM window; here they are all just memory. */
uint8_t disp_fb[DISP_FB_POOL] __attribute__((aligned(4)));
uint8_t disp_fb2[DISP_FB_POOL] __attribute__((aligned(4)));
uint8_t disp_fb3[DISP_FB_POOL] __attribute__((aligned(4)));

int display_fb2_ok(void) { return 1; }
int display_fb3_ok(void) { return 1; }

static int raster = DISP_RASTER_VGA;

int disphw_raster(void)
{
	return raster;
}

/* --- the mode switch, as the core sequences it -------------------------------- */

int disp_hw_mode_prepare(int r)
{
	int rebuild = (r != raster);

	raster = r;
	return rebuild;
}

void disp_hw_mode_tables(enum gexp ex)
{
	(void)ex;		/* the presenter reads gfx_pal directly */
}

void disp_hw_mode_handover(enum gexp ex, int r)
{
	(void)ex;
	(void)r;		/* the core stored gfx_exp; nothing to make visible */
}

void disp_hw_mode_finish(int rebuild)
{
	(void)rebuild;		/* the window follows the raster at the next frame */
}

void disp_hw_palette_changed(enum gexp ex)
{
	(void)ex;		/* the presenter reads gfx_pal at frame time */
}

/* --- the rest of display.h's hardware side -------------------------------------- */

void display_stack_check(void) { }

/* MAP SET and MAP RESET wait for blanking before rewriting the live
 * palette.  Here the palette is only ever read at a frame boundary, on
 * this thread, so the rewrite is atomic with respect to the picture and
 * there is nothing to wait for.  A program's own GFXIOC_VSYNC is
 * answered by the frame tick, not through here. */
void display_wait_vblank(void) { }

struct p_tab *disp_who_parent(struct p_tab *who)
{
	return who->p_pptr;
}

/* The console's mode callback.  The kernel repaints its text console
 * into the framebuffer here; on a PC the terminal is the text console
 * and the window shows the pixels, so there is nothing to repaint.
 * The window console is a later phase. */
void console_gfx(int active)
{
	(void)active;
}

void disphw_init(void)
{
	/* The console's default cell colours: ANSI 7 on ANSI 0, as
	 * console.c's sgr_reset leaves them (concolours[7] is 0xB6). */
	memset(disp_tile_fg, 0xB6, sizeof disp_tile_fg);
	memset(disp_tile_bg, 0x00, sizeof disp_tile_bg);
	memset(disp_fb, 0, sizeof disp_fb);
	memset(disp_fb2, 0, sizeof disp_fb2);
	memset(disp_fb3, 0, sizeof disp_fb3);
}
