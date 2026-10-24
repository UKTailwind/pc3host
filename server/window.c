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
#include <stdlib.h>
#include <stdbool.h>
#include <MiniFB.h>
#include "pc3d.h"

static struct mfb_window *win;
static int ww, wh;

/* The keyboard: every press and release, and the character the host
 * made of it, handed to keyboard.c - which queues them until the pump
 * is over (see there for why).
 *
 * But one pair is the window's own: Ctrl+Alt+Up and Ctrl+Alt+Down turn
 * the volume up and down (sndhw.c), and the program never sees the
 * arrow.  It does see Ctrl and Alt go down and up, which is harmless.
 * Holding the pair repeats, as the desktop repeats any key. */
static mfb_key vol_key;		/* the arrow of a volume pair, until it is let go */

static void on_key(struct mfb_window *w, mfb_key key, mfb_key_mod mod, bool pressed)
{
	(void)w;
	if ((key == MFB_KB_KEY_UP || key == MFB_KB_KEY_DOWN) &&
	    (pressed ? (mod & MFB_KB_MOD_CONTROL) && (mod & MFB_KB_MOD_ALT)
		     : key == vol_key)) {
		if (pressed) {
			sndhw_set_volume(sndhw_volume() + (key == MFB_KB_KEY_UP
					 ? PC3D_VOLUME_STEP : -PC3D_VOLUME_STEP));
			presenter_show_volume();
			vol_key = key;
		} else
			vol_key = (mfb_key)0;
		return;
	}
	keyboard_event((int)key, pressed ? 1 : 0);
}

static void on_char(struct mfb_window *w, unsigned int code)
{
	(void)w;
	keyboard_char(code);
}

int win_open(int w, int h)
{
	if (win && (ww != w || wh != h)) {
		/* a raster change: the old window's keys are gone with it */
		mfb_close(win);
		win = NULL;
		keyboard_reset();
	}
	if (!win) {
		/* The desktop's input method (ibus, fcitx) has no part here:
		 * this server IS a keyboard decoder, and takes raw key events.
		 * MiniFB opens an X input method unconditionally; with the
		 * modifier set to none that is a local no-op rather than a
		 * connection to whatever the desktop runs, which a statically
		 * linked binary is better off not making. */
#ifndef _WIN32
		setenv("XMODIFIERS", "@im=none", 1);
#endif
		win = mfb_open_ex("Pico Computer 3", (unsigned)w, (unsigned)h, 0);
		if (!win) {
			fprintf(stderr, "pc3d: cannot open a %dx%d window\n", w, h);
			return -1;
		}
		mfb_set_keyboard_callback(win, on_key);
		mfb_set_char_input_callback(win, on_char);
		ww = w;
		wh = h;
	}
	return 0;
}

/* Both of these run on the presenter's thread (presenter.c); the key
 * events they deliver through the callbacks above are queued in
 * keyboard.c and taken by the request loop. */
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
