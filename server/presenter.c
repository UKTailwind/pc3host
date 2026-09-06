/*
 * presenter.c - the window, on a thread of its own.
 *
 * On the board the scanout is core1's: it reads the framebuffer and
 * drives the monitor whatever core0 is doing, and a program's ioctls
 * are never held up by a frame being shown.  The first server presented
 * from its request loop - expand the framebuffer, push it to the X
 * server, then go back to answering - and while the push was in flight
 * nobody was answered.  Under WSLg a push is a millisecond and it did
 * not show; on a desktop whose compositor holds a client until it has
 * taken the previous frame, a push can be most of a frame, and a game
 * making 1,200 round trips a frame (TILEMAP DRAW is one per tile) ran
 * at a crawl.  So the window lives here, on its own thread, and the
 * request loop never waits for it.
 *
 * The two threads share the display core's state.  The presenter takes
 * pc3d_display_lock for the expansion only - a millisecond or two, the
 * time it takes to turn the framebuffer into window pixels - and pushes
 * without it; the request loop holds the same lock around every
 * dispatch.  MiniFB's calls, and the X11 connection under them, are all
 * made from this thread; the key events its callbacks deliver are
 * queued (keyboard.c) and taken by the request loop, which is told to
 * look through the wake pipe.
 *
 * A frame is presented when the request loop's tick says so and
 * something has been drawn since the last one; a static screen costs
 * nothing.  If a push takes longer than a frame the next tick's wake is
 * simply the next one taken - frames coalesce, as they do on a board
 * whose program draws faster than the monitor shows.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

#include "display.h"
#include "display_priv.h"
#include "pc3d.h"

pthread_mutex_t pc3d_display_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_t thr;
static int running;
static pthread_mutex_t cv_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int woken, stop_req;
static volatile int dirty = 1;
static int wake_fd = -1;

static uint32_t *frame;
static size_t frame_cap;
static int last_w, last_h;
static unsigned long presents, present_us, pumps;

static long long now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* Size the window buffer for the live mode.  Under the display lock. */
static int frame_fit(int *w, int *h)
{
	present_size(w, h);
	if ((size_t)*w * *h > frame_cap) {
		uint32_t *nf = realloc(frame, (size_t)*w * *h * sizeof *frame);
		if (!nf)
			return -1;
		frame = nf;
		frame_cap = (size_t)*w * *h;
	}
	return 0;
}

void presenter_mark_dirty(void)
{
	dirty = 1;
}

void presenter_wake(void)
{
	pthread_mutex_lock(&cv_lock);
	woken = 1;
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&cv_lock);
}

static void *run(void *arg)
{
	(void)arg;
	for (;;) {
		struct timespec ts;
		int st;

		/* the tick wakes us; failing that, half a frame later, so
		 * the window's events are pumped whatever the clients do */
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_nsec += 8 * 1000000L;
		if (ts.tv_nsec >= 1000000000L) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000L;
		}
		pthread_mutex_lock(&cv_lock);
		while (!woken && !stop_req)
			if (pthread_cond_timedwait(&cv, &cv_lock, &ts) == ETIMEDOUT)
				break;
		woken = 0;
		st = stop_req;
		pthread_mutex_unlock(&cv_lock);
		if (st)
			break;

		if (dirty) {
			int w, h;
			char title[64];
			long long t0 = now_us();

			dirty = 0;
			pthread_mutex_lock(&pc3d_display_lock);
			if (frame_fit(&w, &h) < 0) {
				pthread_mutex_unlock(&pc3d_display_lock);
				continue;
			}
			present_frame(frame);
			snprintf(title, sizeof title, "%s", present_mode_name());
			pthread_mutex_unlock(&pc3d_display_lock);

			if (w != last_w || h != last_h) {
				if (win_open(w, h) < 0) {
					pc3d_window_closed();
					break;
				}
				win_title(title);
				last_w = w;
				last_h = h;
			}
			if (win_present(frame, w, h) < 0) {
				pc3d_window_closed();
				break;
			}
			presents++;
			present_us += (unsigned long)(now_us() - t0);
		} else {
			if (win_pump() < 0) {
				pc3d_window_closed();
				break;
			}
			pumps++;
		}
		if (keyboard_pending() && wake_fd >= 0) {
			char k = 'k';
			if (write(wake_fd, &k, 1) < 0) { /* the loop is awake anyway */ }
		}
	}
	win_close();			/* on the thread the window lives on */
	return NULL;
}

int presenter_start(int wake_write_fd)
{
	wake_fd = wake_write_fd;
	if (pthread_create(&thr, NULL, run, NULL) != 0)
		return -1;
	running = 1;
	return 0;
}

void presenter_stop(void)
{
	if (!running)
		return;
	pthread_mutex_lock(&cv_lock);
	stop_req = 1;
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&cv_lock);
	pthread_join(thr, NULL);
	running = 0;
}

unsigned long presenter_stats(unsigned long *us, unsigned long *idle_pumps)
{
	if (us)
		*us = present_us;
	if (idle_pumps)
		*idle_pumps = pumps;
	return presents;
}
