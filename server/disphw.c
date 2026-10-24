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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <pc3w.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#endif
#include "display.h"
#include "display_priv.h"
#include "pc3d.h"

/* The three framebuffers, N then F then L.  On the board disp_fb is SRAM
 * and the other two are the PSRAM window; here they are one block of
 * shared memory, so that a program can map it and find its picture at
 * MM.INFO(WRITEBUFF), as a PicoMite program finds its own.  display.h
 * points disp_fb, disp_fb2 and disp_fb3 into it. */
uint8_t *disp_fbs;
static char fbs_name[60];

#ifndef _WIN32
/* A pc3d that died left its block in /dev/shm; one whose pid is gone is
 * nobody's. */
static void fbs_sweep(void)
{
	DIR *d = opendir("/dev/shm");
	struct dirent *e;
	char nm[300];
	int pid;

	if (d == NULL)
		return;
	while ((e = readdir(d)) != NULL) {
		if (sscanf(e->d_name, "pc3fb-%d", &pid) != 1 || pid <= 0)
			continue;
		if (kill(pid, 0) == 0 || errno != ESRCH)
			continue;
		snprintf(nm, sizeof nm, "/%s", e->d_name);
		shm_unlink(nm);
	}
	closedir(d);
}
#endif

/* Make the block.  Private memory if it cannot be shared: everything
 * draws as before, and only MM.INFO(WRITEBUFF) is refused. */
static void fbs_make(void)
{
	size_t n = 3 * (size_t)DISP_FB_POOL;
#ifdef _WIN32
	/* the object lives while any handle or view on it does */
	snprintf(fbs_name, sizeof fbs_name, "Local\\pc3fb-%d", (int)getpid());
	disp_fbs = (uint8_t *)pc3w_shm_make(fbs_name, n);
#else
	int fd;

	fbs_sweep();
	snprintf(fbs_name, sizeof fbs_name, "/pc3fb-%d", (int)getpid());
	fd = shm_open(fbs_name, O_CREAT | O_EXCL | O_RDWR, 0600);
	if (fd >= 0) {
		if (ftruncate(fd, (off_t)n) == 0) {
			void *p = mmap(NULL, n, PROT_READ | PROT_WRITE,
				       MAP_SHARED, fd, 0);
			if (p != MAP_FAILED)
				disp_fbs = (uint8_t *)p;
		}
		close(fd);
		if (disp_fbs == NULL)
			shm_unlink(fbs_name);
	}
#endif
	if (disp_fbs == NULL) {
		fbs_name[0] = 0;
		disp_fbs = (uint8_t *)calloc(1, n);
		if (disp_fbs == NULL) {
			fprintf(stderr, "pc3d: no memory for the framebuffers\n");
			exit(1);
		}
	}
}

const char *disphw_fb_name(void)
{
	return fbs_name;
}

/* At exit: the name goes, and the block with the last program to have
 * it mapped. */
void disphw_fb_done(void)
{
#ifndef _WIN32
	if (fbs_name[0])
		shm_unlink(fbs_name);
#endif
}

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
	if (disp_fbs == NULL)
		fbs_make();
	memset(disp_fbs, 0, 3 * (size_t)DISP_FB_POOL);
}
