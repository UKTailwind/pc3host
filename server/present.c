/*
 * present.c - what the monitor would show.
 *
 * The PC3's core1 expands the framebuffer into scanlines for the HSTX
 * (display_hstx.c, disp_fill_loop); this does the same expansion into
 * a 32-bit picture for the window, once a frame.  The five expanders
 * are that loop's five cases, written for a PC rather than a deadline:
 *
 *   console      640x480, one bit per pixel, an RGB332 pair per 8x12 cell
 *   MODE 7       320x240 4bpp, doubled both ways into the 640x480 raster
 *   modes 1/4    320x256 4bpp, x3 both ways, 960 wide with 32px borders
 *   modes 2/5    160x256 4bpp, x6 across, x3 down, the same borders
 *   modes 0/3    640x256 1bpp, 5:8 across with the kernel's coverage
 *                blend, x3 down: 1024x768 edge to edge
 *
 * The 5:8 blend is copied from gfx_lut_rebuild exactly - the weights,
 * the arithmetic in RGB332 space, the truncation - so a MODE 0 screen
 * here has the same anti-aliased edges as the monitor's.
 *
 * Colours are RGB332 bytes from gfx_pal (or the console tiles) turned
 * into 0x00RRGGBB with the same replication display_gfx_getpixel uses
 * (rgb332_to_888), so what PIXEL() reports and what the window shows
 * agree.
 */

#include <stdio.h>
#include <string.h>
#include "display.h"
#include "display_priv.h"
#include "pc3d.h"

static uint32_t argb[256];		/* RGB332 -> 0x00RRGGBB */
static int argb_ready;

static void argb_init(void)
{
	int c;

	for (c = 0; c < 256; c++) {
		uint32_t r = ((c >> 5) & 7) * 255u / 7u;
		uint32_t g = ((c >> 2) & 7) * 255u / 7u;
		uint32_t b = (c & 3) * 255u / 3u;
		argb[c] = (r << 16) | (g << 8) | b;
	}
	argb_ready = 1;
}

/*
 * The window is the raster times an integer.  A 640x480 raster is small
 * on a modern desktop, so it is shown doubled by default - 1280x960 -
 * which makes a MODE 2 pixel (already 2x2 in the raster) four times its
 * size, and the console's 8x12 cells 16x24.  The 1024x768 raster stays
 * at one to one; doubling it would want a 2048x1536 window.  Every
 * pixel is still one the expander made: the scale is applied after the
 * expansion, so nothing is resampled.
 */
static int vga_scale = 2;

void present_set_scale(int s)
{
	if (s < 1)
		s = 1;
	if (s > 4)
		s = 4;
	vga_scale = s;
}

static void raster_size(int *w, int *h)
{
	switch (gfx_exp) {
	case EXP_CONSOLE:
	case EXP_4BPP_X2:
		*w = 640; *h = 480;
		break;
	default:
		*w = 1024; *h = 768;
		break;
	}
}

static int scale_now(void)
{
	return (gfx_exp == EXP_CONSOLE || gfx_exp == EXP_4BPP_X2) ? vga_scale : 1;
}

void present_size(int *w, int *h)
{
	int s = scale_now();

	raster_size(w, h);
	*w *= s;
	*h *= s;
}

const char *present_mode_name(void)
{
	uint16_t w, h, s;
	uint8_t bpp, mode;
	static char name[48];

	display_gfx_geom(&w, &h, &s, &bpp, &mode);
	if (mode == 0xFF)
		snprintf(name, sizeof name, "Pico Computer 3 - console");
	else
		snprintf(name, sizeof name, "Pico Computer 3 - MODE %d (%dx%d)",
			 mode, w, h);
	return name;
}

/* the 5:8 coverage table, from gfx_lut_rebuild: {left src, right src,
 * left weight in fifths} for each of the eight output pixels */
static const uint8_t cov[8][3] = {
	{ 0, 0, 5 }, { 0, 1, 3 }, { 1, 1, 5 }, { 1, 2, 1 },
	{ 2, 3, 4 }, { 3, 3, 5 }, { 3, 4, 2 }, { 4, 4, 5 },
};

static void expand_5to8(const uint8_t *src, uint32_t *out)
{
	/* 80 source bytes = 640 pixels = 128 groups of five; each group
	 * makes eight output pixels: 1024. */
	int g, i;
	uint8_t ink = gfx_pal[1], paper = gfx_pal[0];

	for (g = 0; g < 128; g++) {
		int bit = g * 5;
		uint8_t c[5];
		for (i = 0; i < 5; i++) {
			int b = bit + i;
			c[i] = (src[b >> 3] & (0x80 >> (b & 7))) ? ink : paper;
		}
		for (i = 0; i < 8; i++) {
			uint8_t ca = c[cov[i][0]], cb = c[cov[i][1]];
			uint8_t wa = cov[i][2], wb = (uint8_t)(5 - wa);
			uint8_t r = (uint8_t)(((ca >> 5) * wa + (cb >> 5) * wb) / 5);
			uint8_t gg = (uint8_t)((((ca >> 2) & 7) * wa + ((cb >> 2) & 7) * wb) / 5);
			uint8_t bl = (uint8_t)(((ca & 3) * wa + (cb & 3) * wb) / 5);
			*out++ = argb[(r << 5) | (gg << 2) | bl];
		}
	}
}

/* the raster, before scaling: 1024x768 is the largest */
static uint32_t raster[1024 * 768];

static void expand_raster(uint32_t *out);

void present_frame(uint32_t *out)
{
	int rw, rh, s = scale_now();

	if (!argb_ready)
		argb_init();
	if (s == 1) {
		expand_raster(out);
		return;
	}
	expand_raster(raster);
	raster_size(&rw, &rh);
	{
		/* each raster row becomes s rows of s-fold pixels */
		int y, x, k;
		for (y = 0; y < rh; y++) {
			const uint32_t *src = raster + y * rw;
			uint32_t *row = out + (size_t)y * s * rw * s;
			uint32_t *p = row;
			for (x = 0; x < rw; x++)
				for (k = 0; k < s; k++)
					*p++ = src[x];
			for (k = 1; k < s; k++)
				memcpy(row + (size_t)k * rw * s, row,
				       (size_t)rw * s * sizeof *row);
		}
	}
}

static void expand_raster(uint32_t *out)
{
	int w, h, y, x;

	raster_size(&w, &h);

	switch (gfx_exp) {
	case EXP_CONSOLE:
		for (y = 0; y < 480; y++) {
			const uint8_t *fc = &disp_tile_fg[(y / DISP_CELL_H) * DISP_COLS];
			const uint8_t *bc = &disp_tile_bg[(y / DISP_CELL_H) * DISP_COLS];
			const uint8_t *d = &disp_fb[y * DISP_STRIDE];
			uint32_t *p = out + y * 640;
			for (x = 0; x < DISP_COLS; x++) {
				uint8_t v = d[x];
				uint32_t f = argb[fc[x]], b = argb[bc[x]];
				int k;
				for (k = 7; k >= 0; k--)
					*p++ = (v & (1 << k)) ? f : b;
			}
		}
		break;

	case EXP_4BPP_X2:
		for (y = 0; y < 480; y++) {
			const uint8_t *s = &disp_fb[(y >> 1) * 160];
			uint32_t *p = out + y * 640;
			for (x = 0; x < 160; x++) {
				uint32_t c1 = argb[gfx_pal[s[x] >> 4]];
				uint32_t c2 = argb[gfx_pal[s[x] & 15]];
				p[0] = p[1] = c1;
				p[2] = p[3] = c2;
				p += 4;
			}
		}
		break;

	case EXP_4BPP_X3:
		for (y = 0; y < 768; y++) {
			const uint8_t *s = &disp_fb[(y / 3) * 160];
			uint32_t *p = out + y * 1024;
			memset(p, 0, 32 * sizeof *p);
			p += 32;
			for (x = 0; x < 160; x++) {
				uint32_t c1 = argb[gfx_pal[s[x] >> 4]];
				uint32_t c2 = argb[gfx_pal[s[x] & 15]];
				p[0] = p[1] = p[2] = c1;
				p[3] = p[4] = p[5] = c2;
				p += 6;
			}
			memset(p, 0, 32 * sizeof *p);
		}
		break;

	case EXP_4BPP_X6:
		for (y = 0; y < 768; y++) {
			const uint8_t *s = &disp_fb[(y / 3) * 80];
			uint32_t *p = out + y * 1024;
			int k;
			memset(p, 0, 32 * sizeof *p);
			p += 32;
			for (x = 0; x < 80; x++) {
				uint32_t c1 = argb[gfx_pal[s[x] >> 4]];
				uint32_t c2 = argb[gfx_pal[s[x] & 15]];
				for (k = 0; k < 6; k++)
					p[k] = c1;
				for (k = 6; k < 12; k++)
					p[k] = c2;
				p += 12;
			}
			memset(p, 0, 32 * sizeof *p);
		}
		break;

	case EXP_1BPP_5TO8:
		for (y = 0; y < 768; y++)
			expand_5to8(&disp_fb[(y / 3) * 80], out + y * 1024);
		break;
	}
}
