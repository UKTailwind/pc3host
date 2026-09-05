/*
 * gfxc.c - a C program for the PC3, run on a PC.
 *
 * Compiled by the Compiler Kit's cc to bytecode and run by bcrun, so
 * its ioctls reach the server through bcrun's own libcalls - the path
 * a hand-written C program takes, as distinct from the runtime's.  The
 * structures here are the board's 32-bit ones, which is exactly what
 * bcrun has to translate (host_sys_ioctl).
 *
 * MODE 0: the other raster, 640x256 one bit a pixel.  Draws with the
 * calls the PC3 C manual teaches - COLOUR, RECT, a batch of points, a
 * run of text - and prints what GETPIXEL and INFO say.
 */
#include <stdio.h>
#include <stdint.h>
#include "pico_ioctl.h"

int open(const char *path, int flags, ...);
int ioctl(int fd, int rq, ...);
int close(int fd);

#define O_RDWR 2

int main(void)
{
	int fd, mode, i, r;
	struct gfx_info gi;
	struct gfx_rect rc;
	struct gfx_pt pts[8];
	struct gfx_batch b;
	struct gfx_text t;
	const char *msg = "MODE 0 FROM C";

	fd = open("/dev/sys", O_RDWR);
	if (fd < 0) {
		printf("no display\n");
		return 1;
	}
	mode = 0;
	if (ioctl(fd, GFXIOC_MODE, &mode) < 0) {
		printf("mode 0 refused\n");
		return 1;
	}
	ioctl(fd, GFXIOC_INFO, &gi);
	printf("%d x %d stride %d bpp %d mode %d\n", gi.width, gi.height,
	       gi.stride, gi.bpp, gi.mode);

	r = ioctl(fd, GFXIOC_COLOUR, (void *)0xFFFFFFL);
	printf("colour index %d\n", r);
	rc.x1 = 20; rc.y1 = 20; rc.x2 = 200; rc.y2 = 120;
	ioctl(fd, GFXIOC_RECT, &rc);
	ioctl(fd, GFXIOC_COLOUR, (void *)0L);
	rc.x1 = 40; rc.y1 = 40; rc.x2 = 180; rc.y2 = 100;
	ioctl(fd, GFXIOC_RECT, &rc);
	ioctl(fd, GFXIOC_COLOUR, (void *)0xFFFFFFL);

	for (i = 0; i < 8; i++) {
		pts[i].x = 300 + i * 20;
		pts[i].y = 128 + i * 10;
	}
	b.count = 8;
	b.flags = 0;
	b.items = pts;
	b.colours = 0;
	ioctl(fd, GFXIOC_PIXELS, &b);

	t.x = 20; t.y = 200; t.scale = 2; t.font = 1;
	t.fg = 0xFFFFFF; t.bg = -1; t.len = 13; t.str = (void *)msg;
	r = ioctl(fd, GFXIOC_TEXT, &t);
	printf("text ended at %d\n", r);

	printf("pixel 30,30 %d\n", ioctl(fd, GFXIOC_GETPIXEL,
				       (void *)(long)GFX_PIXEL_PACK(30, 30)));
	printf("pixel 60,60 %d\n", ioctl(fd, GFXIOC_GETPIXEL,
				       (void *)(long)GFX_PIXEL_PACK(60, 60)));
	printf("pixel 320,138 %d\n", ioctl(fd, GFXIOC_GETPIXEL,
					 (void *)(long)GFX_PIXEL_PACK(320, 138)));
	printf("pixel 639,255 %d\n", ioctl(fd, GFXIOC_GETPIXEL,
					 (void *)(long)GFX_PIXEL_PACK(639, 255)));
	close(fd);
	return 0;
}
