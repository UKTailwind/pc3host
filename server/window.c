/*
 * window.c - the monitor, as a MiniFB window.
 *
 * One window, sized to the raster the kernel would be scanning out
 * (640x480 or 1024x768) and no larger, so a pixel here is a pixel the
 * expander made and nothing is rescaled twice.  A raster change closes
 * the window and opens a new one, which is what a monitor does when the
 * PC3 crosses from one to the other.  Closing the window from the
 * desktop is reported to the server, which treats it as the display
 * going away.
 */

#include <stdio.h>
#include <MiniFB.h>
#include "pc3d.h"

static struct mfb_window *win;
static int ww, wh;

int win_open(int w, int h)
{
	if (win && (ww != w || wh != h)) {
		mfb_close(win);
		win = NULL;
	}
	if (!win) {
		win = mfb_open_ex("Pico Computer 3", (unsigned)w, (unsigned)h, 0);
		if (!win) {
			fprintf(stderr, "pc3d: cannot open a %dx%d window\n", w, h);
			return -1;
		}
		ww = w;
		wh = h;
	}
	return 0;
}

int win_present(const uint32_t *buf, int w, int h)
{
	if (!win)
		return 0;
	if (mfb_update_ex(win, (void *)buf, (unsigned)w, (unsigned)h) != MFB_STATE_OK)
		return -1;
	return 0;
}

int win_pump(void)
{
	if (!win)
		return 0;
	if (mfb_update_events(win) != MFB_STATE_OK)
		return -1;
	return 0;
}

void win_title(const char *t)
{
	if (win)
		mfb_set_title(win, t);
}

void win_close(void)
{
	if (win) {
		mfb_close(win);
		win = NULL;
	}
}
