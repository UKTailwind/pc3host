/*
 * dispatch.c - plt_dev_ioctl, for a PC.
 *
 * This is the display and console section of the kernel's misc.c,
 * request for request, with uget and uput replaced by the payload the
 * client flattened for us (pc3proto.h).  The validation is the
 * kernel's: the same limits, the same errno for the same refusal, so a
 * program that is refused here would have been refused on the board.
 * Where the kernel reads an array where it lies, the array arrived in
 * the message; where it writes one back, it goes in the reply.
 *
 * udata.u_ptab is the requesting client for the duration, as it is the
 * requesting process in the kernel - fonts.c reads it to keep one
 * program's font slots invisible to another.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "display.h"
#include "sound.h"
#include "sound_priv.h"
#include "kstub/kdata.h"
#include "pico_ioctl.h"
#include "pc3proto.h"
#include "pc3d.h"

struct u_data udata;

static void fail(struct reply *r, int err)
{
	r->ret = -1;
	r->err = err;
}

static void ok(struct reply *r, int ret)
{
	r->ret = ret;
	r->err = 0;
}

/* Reply with a copy of a structure. */
static void put(struct reply *r, const void *p, uint32_t n)
{
	void *m = malloc(n ? n : 1);

	if (!m) {
		fail(r, ENOMEM);
		return;
	}
	memcpy(m, p, n);
	r->data = m;
	r->owned = m;
	r->len = n;
}

void pc3d_dispatch(struct client *c, uint16_t code, uint32_t arg,
		   const unsigned char *pl, uint32_t len, struct reply *r)
{
	struct p_tab *who = &c->pt;

	memset(r, 0, sizeof *r);
	udata.u_ptab = who;

	/* Everything below draws through the CALLER's own write target,
	 * so point the primitives at it before any of them runs - the
	 * kernel's rule, unconditionally. */
	display_fb_enter(who);

	switch (code) {
	case PICOIOC_CONMIRROR:
		c->mirror = arg ? 1 : 0;
		ok(r, 0);
		return;

	case PICOIOC_KEYDOWN: {
		/* MMBasic's fun_keydown by index, the whole snapshot at once
		 * so count and codes are the same instant - misc.c's shape,
		 * from the same decoder. */
		struct kbd_down d;
		int i;
		d.count = (uint8_t)keyboard_keydown(0);
		d.mods = (uint8_t)keyboard_keydown(7);
		d.locks = (uint8_t)keyboard_keydown(8);
		d.pad = 0;
		for (i = 0; i < 6; i++)
			d.key[i] = (uint8_t)keyboard_keydown(i + 1);
		d.pad2[0] = d.pad2[1] = 0;
		put(r, &d, sizeof d);
		ok(r, 0);
		return;
	}

	case PICOIOC_KBDMAP: {
		/* OPTION KEYBOARD: two letters */
		char name[3];
		if (len < 2) { fail(r, EFAULT); return; }
		name[0] = (char)pl[0];
		name[1] = (char)pl[1];
		name[2] = 0;
		if (keyboard_set_layout(name)) { fail(r, EINVAL); return; }
		ok(r, 0);
		return;
	}

	case PICOIOC_BOARD: {
		/* The number in the name the banner prints.  A PC is not a
		 * PC3, but a program asking MM.DEVICE$ wants the machine it
		 * was written for, and that is this one - see PHASE1.md. */
		int n = 3;
		put(r, &n, sizeof n);
		ok(r, 0);
		return;
	}

	case GFXIOC_MODE:
		if (display_gfx_mode((int)arg) < 0) {
			fail(r, EINVAL);
			return;
		}
		ok(r, 0);
		return;

	case GFXIOC_PAL:
		display_gfx_pal((arg >> 8) & 15, arg & 15);
		ok(r, 0);
		return;

	case GFXIOC_PIXEL:
		ok(r, display_gfx_pixel(arg & 0x3FF, (arg >> 10) & 0x1FF,
					display_gfx_curcol()));
		return;

	case GFXIOC_COLOUR:
		display_gfx_colour(arg);
		ok(r, display_gfx_curcol());
		return;

	case GFXIOC_GETPIXEL:
		ok(r, display_gfx_getpixel(arg & 0x3FF, (arg >> 10) & 0x1FF));
		return;

	case GFXIOC_RECT: {
		struct gfx_rect gr;
		if (len < sizeof gr) { fail(r, EFAULT); return; }
		memcpy(&gr, pl, sizeof gr);
		ok(r, display_gfx_rect(gr.x1, gr.y1, gr.x2, gr.y2,
				       display_gfx_curcol()));
		return;
	}

	case GFXIOC_PIXELS:
	case GFXIOC_RECTS: {
		struct pc3w_batch b;
		uint32_t isz, bytes, cb;
		const void *items;
		const uint32_t *cols = NULL;
		uint32_t *colcopy = NULL;
		void *itemcopy;
		int ret;

		if (len < sizeof b) { fail(r, EFAULT); return; }
		memcpy(&b, pl, sizeof b);
		if (b.count == 0) { ok(r, 0); return; }
		if (b.count > GFX_BATCH_MAX || b.flags) { fail(r, EINVAL); return; }
		isz = (code == GFXIOC_PIXELS) ? sizeof(struct gfx_pt)
					      : sizeof(struct gfx_rc);
		bytes = b.count * isz;
		cb = b.has_colours ? b.count * 4u : 0;
		if (len < sizeof b + bytes + cb) { fail(r, EFAULT); return; }
		/* aligned copies: the payload is a byte stream */
		itemcopy = malloc(bytes);
		if (!itemcopy) { fail(r, ENOMEM); return; }
		memcpy(itemcopy, pl + sizeof b, bytes);
		items = itemcopy;
		if (cb) {
			colcopy = malloc(cb);
			if (!colcopy) { free(itemcopy); fail(r, ENOMEM); return; }
			memcpy(colcopy, pl + sizeof b + bytes, cb);
			cols = colcopy;
		}
		if (code == GFXIOC_PIXELS)
			ret = display_gfx_pixels(items, b.count, cols);
		else
			ret = display_gfx_rects(items, b.count, cols);
		free(itemcopy);
		free(colcopy);
		ok(r, ret);
		return;
	}

	case GFXIOC_BITMAP: {
		struct pc3w_bitmap gb;
		uint32_t nbytes;
		if (len < sizeof gb) { fail(r, EFAULT); return; }
		memcpy(&gb, pl, sizeof gb);
		if (gb.width == 0 || gb.height == 0 || gb.scale == 0) {
			fail(r, EINVAL);
			return;
		}
		nbytes = ((uint32_t)gb.width * gb.height + 7) / 8;
		if (len < sizeof gb + nbytes) { fail(r, EFAULT); return; }
		ok(r, display_gfx_bitmap(gb.x, gb.y, gb.width, gb.height, gb.scale,
					 display_gfx_map((uint32_t)gb.fg),
					 gb.bg < 0 ? -1 : display_gfx_map((uint32_t)gb.bg),
					 pl + sizeof gb));
		return;
	}

	case GFXIOC_FBOPEN: {
		int v = (int)arg;
		int rc = display_fb_open(who, v & 0xFF, (v >> 8) & 0xFF);
		if (rc) {
			fail(r, rc == -2 ? EBUSY : EINVAL);
			return;
		}
		ok(r, 0);
		return;
	}

	case GFXIOC_FBSEL:
		if (display_fb_select(who, (int)arg)) { fail(r, EINVAL); return; }
		ok(r, 0);
		return;

	case GFXIOC_FBCOPY2: {
		int v = (int)arg;
		if (display_fb_copy(who, (v >> 4) & 0xF, v & 0xF)) {
			fail(r, EINVAL);
			return;
		}
		ok(r, 0);
		return;
	}

	case GFXIOC_MERGE:
		if (display_fb_merge(who, (int)arg)) { fail(r, EINVAL); return; }
		ok(r, 0);
		return;

	case GFXIOC_VSYNC:
		/* answered by the frame tick */
		c->vsync_wait = 1;
		r->defer = 1;
		ok(r, 0);
		return;

	case GFXIOC_VSYNCTRY:
		/* the budget is honoured by pc3d.c: if the next frame is
		 * inside it the reply waits and says 1, else 0 at once */
		c->vsync_wait = 2;
		r->defer = 1;
		r->ret = (int32_t)(arg > 20000u ? 20000u : arg);
		return;

	case GFXIOC_SCROLL: {
		int rows = (int)(int8_t)(arg >> 24);
		if (display_gfx_scroll(rows, display_gfx_map(arg & 0xFFFFFF))) {
			fail(r, EINVAL);
			return;
		}
		ok(r, 0);
		return;
	}

	case GFXIOC_SCROLL2: {
		struct gfx_scroll2 s2;
		int fillarg;
		if (len < sizeof s2) { fail(r, EFAULT); return; }
		memcpy(&s2, pl, sizeof s2);
		fillarg = (s2.fill < 0) ? (int)s2.fill
					: (int)display_gfx_map((uint32_t)s2.fill & 0xFFFFFF);
		if (s2.fill < -2 || display_gfx_scroll2(s2.dx, s2.dy, fillarg)) {
			fail(r, EINVAL);
			return;
		}
		ok(r, 0);
		return;
	}

	case GFXIOC_TEXT: {
		struct pc3w_text gt;
		if (len < sizeof gt) { fail(r, EFAULT); return; }
		memcpy(&gt, pl, sizeof gt);
		if (gt.len == 0) { ok(r, 0); return; }
		if (gt.len > GFX_TEXT_MAX || GFX_TEXT_SCALE(gt.scale) == 0 ||
		    GFX_TEXT_ORIENT(gt.scale) > GORIENT_D) {
			fail(r, EINVAL);
			return;
		}
		if (len < sizeof gt + gt.len) { fail(r, EFAULT); return; }
		if (!display_font(gt.font ? gt.font : 1, 0, 0, 0, 0)) {
			fail(r, EINVAL);
			return;
		}
		ok(r, display_gfx_text(gt.x, gt.y, gt.font ? gt.font : 1,
				       GFX_TEXT_SCALE(gt.scale),
				       display_gfx_map((uint32_t)gt.fg),
				       gt.bg < 0 ? -1 : display_gfx_map((uint32_t)gt.bg),
				       pl + sizeof gt, (int)gt.len,
				       GFX_TEXT_ORIENT(gt.scale)));
		return;
	}

	case GFXIOC_MAP:
		if (display_gfx_remap((int)(arg >> 24) & 0xFF, arg & 0xFFFFFF)) {
			fail(r, EINVAL);
			return;
		}
		ok(r, 0);
		return;

	case GFXIOC_MAPCTL: {
		int rc = ((int)arg == GFX_MAP_RESET) ? display_gfx_remap_reset()
						    : display_gfx_remap_apply();
		if (rc) { fail(r, EINVAL); return; }
		ok(r, 0);
		return;
	}

	case GFXIOC_FONTINFO: {
		struct gfx_fontinfo gf;
		int w = 0, h = 0, first = 0, count = 0;
		if (len < sizeof gf) { fail(r, EFAULT); return; }
		memcpy(&gf, pl, sizeof gf);
		display_font(gf.font, &w, &h, &first, &count);
		gf.width = (uint8_t)w;
		gf.height = (uint8_t)h;
		gf.first = (uint8_t)first;
		gf.count = (uint16_t)count;
		gf.nfonts = (uint16_t)display_font_count();
		put(r, &gf, sizeof gf);
		ok(r, 0);
		return;
	}

	case GFXIOC_FONTADDR: {
		/* The kernel hands out a flash address.  We hand out the
		 * font itself, after the structure, and the client puts it
		 * where the program can read it. */
		struct gfx_fontaddr ga;
		const unsigned char *fp;
		int w = 0, h = 0, first = 0, count = 0;
		uint32_t bytes;
		unsigned char *m;
		if (len < sizeof ga) { fail(r, EFAULT); return; }
		memcpy(&ga, pl, sizeof ga);
		fp = display_font(ga.font, &w, &h, &first, &count);
		ga.pad[0] = ga.pad[1] = ga.pad[2] = 0;
		ga.addr = 0;
		bytes = fp ? (uint32_t)(4 + count * ((w * h) / 8)) : 0;
		ga.bytes = bytes;
		m = malloc(sizeof ga + bytes);
		if (!m) { fail(r, ENOMEM); return; }
		memcpy(m, &ga, sizeof ga);
		if (bytes)
			memcpy(m + sizeof ga, fp, bytes);
		r->data = m;
		r->owned = m;
		r->len = (uint32_t)(sizeof ga + bytes);
		ok(r, 0);
		return;
	}

	case GFXIOC_FONTDEF: {
		struct pc3w_fontdef fd;
		const unsigned char *hdr;
		uint32_t need;
		unsigned char *copy;
		int slot;
		if (len < sizeof fd) { fail(r, EFAULT); return; }
		memcpy(&fd, pl, sizeof fd);
		if (fd.bytes < 4 || len < sizeof fd + fd.bytes) { fail(r, EFAULT); return; }
		hdr = pl + sizeof fd;
		if (!hdr[0] || !hdr[1] || !hdr[3] || ((hdr[0] * hdr[1]) & 7)) {
			fail(r, EINVAL);
			return;
		}
		need = 4u + (uint32_t)hdr[3] * ((uint32_t)hdr[0] * (uint32_t)hdr[1] / 8u);
		if (fd.bytes < need) { fail(r, EINVAL); return; }
		if (fd.font < 10 || fd.font > 16) { fail(r, EINVAL); return; }
		copy = malloc(fd.bytes);
		if (!copy) { fail(r, ENOMEM); return; }
		memcpy(copy, hdr, fd.bytes);
		if (display_font_set(fd.font, copy, who)) {
			free(copy);
			fail(r, EINVAL);
			return;
		}
		slot = fd.font - 10;
		free(c->ufont[slot]);
		c->ufont[slot] = copy;
		ok(r, 0);
		return;
	}

	case GFXIOC_INFO: {
		struct gfx_info gi;
		display_gfx_geom(&gi.width, &gi.height, &gi.stride, &gi.bpp, &gi.mode);
		put(r, &gi, sizeof gi);
		ok(r, 0);
		return;
	}

	case GFXIOC_BLIT: {
		struct pc3w_blit gb;
		int size = display_gfx_fbsize();
		if (len < sizeof gb) { fail(r, EFAULT); return; }
		memcpy(&gb, pl, sizeof gb);
		if (size == 0 || gb.offset >= (uint32_t)size ||
		    gb.len > (uint32_t)size - gb.offset) {
			fail(r, EINVAL);
			return;
		}
		if (len < sizeof gb + gb.len) { fail(r, EFAULT); return; }
		memcpy(display_fb_target() + gb.offset, pl + sizeof gb, gb.len);
		ok(r, 0);
		return;
	}

	case GFXIOC_BLITRD: {
		struct pc3w_blit gb;
		int size = display_gfx_fbsize();
		if (len < sizeof gb) { fail(r, EFAULT); return; }
		memcpy(&gb, pl, sizeof gb);
		if (size == 0 || gb.offset >= (uint32_t)size ||
		    gb.len > (uint32_t)size - gb.offset) {
			fail(r, EINVAL);
			return;
		}
		put(r, display_fb_target() + gb.offset, gb.len);
		ok(r, 0);
		return;
	}

	case GFXIOC_BLITR:
	case GFXIOC_BLITRDR: {
		struct pc3w_blitr gr;
		int size = display_gfx_fbsize();
		uint32_t last, n;
		uint8_t *t;
		unsigned row;
		if (len < sizeof gr) { fail(r, EFAULT); return; }
		memcpy(&gr, pl, sizeof gr);
		if (size == 0 || gr.rows == 0 || gr.len == 0 ||
		    gr.stride == 0 || gr.len > gr.stride) {
			fail(r, EINVAL);
			return;
		}
		last = gr.offset + (uint32_t)(gr.rows - 1) * gr.stride + gr.len;
		if (gr.offset >= (uint32_t)size || last > (uint32_t)size) {
			fail(r, EINVAL);
			return;
		}
		n = (uint32_t)gr.rows * gr.len;
		t = display_fb_target() + gr.offset;
		if (code == GFXIOC_BLITR) {
			if (len < sizeof gr + n) { fail(r, EFAULT); return; }
			for (row = 0; row < gr.rows; row++, t += gr.stride)
				memcpy(t, pl + sizeof gr + row * gr.len, gr.len);
			ok(r, 0);
			return;
		} else {
			unsigned char *m = malloc(n);
			if (!m) { fail(r, ENOMEM); return; }
			for (row = 0; row < gr.rows; row++, t += gr.stride)
				memcpy(m + row * gr.len, t, gr.len);
			r->data = m;
			r->owned = m;
			r->len = n;
			ok(r, 0);
			return;
		}
	}

	/* ---- sound: misc.c's SNDIOC section, the kernel's own sound.c
	 * behind it (sndhw.c).  The owner is the connection's token, and
	 * every call into the core is under the core's lock, because the
	 * audio thread is filling from it at the same time. ---- */
	case SNDIOC_SOUND: {
		struct snd_cmd sc;
		int rc;

		if (len < sizeof sc) { fail(r, EFAULT); return; }
		memcpy(&sc, pl, sizeof sc);
		sndhw_lock();
		rc = sound_cmd(sc.chan, sc.amp, sc.pitch, sc.dur);
		sndhw_unlock();
		if (rc) { fail(r, EAGAIN); return; }
		ok(r, 0);
		return;
	}
	case SNDIOC_ENV:
		if (len < 14) { fail(r, EFAULT); return; }
		sndhw_lock();
		sound_envelope(pl);
		sndhw_unlock();
		ok(r, 0);
		return;
	case SNDIOC_QUIET:
		sndhw_lock();
		sound_quiet();
		sndhw_unlock();
		ok(r, 0);
		return;
	case SNDIOC_PCMOPEN: {
		struct snd_pcm p;
		int rc;

		if (len < sizeof p) { fail(r, EFAULT); return; }
		memcpy(&p, pl, sizeof p);
		if (p.bits != 16) { fail(r, EINVAL); return; }
		/* The stream belongs to a PROCESS, not to a file handle:
		 * the pid is what a dead owner can be detected by, and what
		 * BASIC's PLAY STOP has to signal. */
		sndhw_lock();
		rc = sound_pcm_open(p.rate, p.channels, c->tok);
		sndhw_unlock();
		if (rc) { fail(r, rc == -2 ? EBUSY : EINVAL); return; }
		ok(r, 0);
		return;
	}
	case SNDIOC_PCMWRITE: {
		int n;

		/* the payload IS the samples; a short count is the ring
		 * being full, and the player comes back */
		sndhw_lock();
		n = sound_pcm_write(pl, len, c->tok);
		sndhw_unlock();
		if (n < 0) { fail(r, EINVAL); return; }
		ok(r, n);
		return;
	}
	case SNDIOC_PCMSTAT: {
		struct snd_stat st;

		sndhw_lock();
		sound_pcm_stat(&st.space, &st.queued, &st.underruns);
		sndhw_unlock();
		put(r, &st, sizeof st);
		if (r->err) return;
		ok(r, 0);
		return;
	}
	case SNDIOC_PCMWAIT: {
		/* arg is the low-water mark in bytes.  Answered now if
		 * there is room, else deferred: pc3d.c's loop replies when
		 * the ring has drained to it - the kernel's tick, at 2 ms
		 * rather than 5. */
		uint32_t q;
		int rc;

		sndhw_lock();
		rc = sound_pcm_queued(c->tok, &q);
		sndhw_unlock();
		if (rc < 0) { fail(r, EINVAL); return; }
		if (q <= arg) { ok(r, 0); return; }
		c->pcm_wait = 1;
		c->pcm_mark = arg;
		r->defer = 1;
		return;
	}
	case SNDIOC_PCMCLOSE:
		sndhw_lock();
		sound_pcm_close(c->tok);
		sndhw_unlock();
		ok(r, 0);
		return;
	case SNDIOC_PCMOWNER: {
		uint16_t tok;

		sndhw_lock();
		tok = sound_pcm_owner();
		sndhw_unlock();
		/* the real pid, which is what kill() wants */
		ok(r, tok ? pc3d_tok_pid(tok) : 0);
		return;
	}
	case SNDIOC_MMCMD: {
		struct snd_mmcmd m;
		int rc;

		if (len < sizeof m) { fail(r, EFAULT); return; }
		memcpy(&m, pl, sizeof m);
		sndhw_lock();
		rc = sound_mm_cmd(m.op, m.a, m.b, m.p1, m.p2, m.p3, c->tok);
		sndhw_unlock();
		if (rc) { fail(r, EBUSY); return; }	/* an MP3/MOD player holds it */
		ok(r, 0);
		return;
	}
	case SNDIOC_MMSTOP:
		sndhw_lock();
		sound_mm_stop();
		sndhw_unlock();
		ok(r, 0);
		return;

	default:
		break;
	}
	fail(r, ENOTTY);
}

void pc3d_client_gone(struct client *c)
{
	int i;

	/* what the kernel's pagemap_free does for a dying process */
	display_fb_release(&c->pt);
	display_font_release(&c->pt);
	sndhw_client_gone(c->tok);
	for (i = 0; i < PC3D_UFONTS; i++) {
		free(c->ufont[i]);
		c->ufont[i] = NULL;
	}
}
