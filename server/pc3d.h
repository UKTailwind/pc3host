/*
 * pc3d.h - the PC3 device server's own view of things.
 *
 * The server plays the kernel's part for the display: it owns the
 * framebuffers, the window and the per-connection state, and it answers
 * the ioctl codes as misc.c does.  display.c (the kernel's portable
 * display core) and fonts.c compile into it unchanged, against the
 * hooks in disphw.c and the two stub kernel headers in kstub/.
 */
#ifndef PC3D_H
#define PC3D_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

/* The process table entry, as far as the display core needs one: who
 * owns a framebuffer or a font, and whose child a caller is.  The
 * kernel's struct p_tab is much more; the core only ever takes its
 * address and asks for p_pptr. */
struct p_tab {
	struct p_tab *p_pptr;
	int p_pid;
};

#define PC3D_MAX_CLIENTS 32
#define PC3D_UFONTS 7			/* fonts 10-16, as fonts.c */

#ifndef PC3D_VERSION
#define PC3D_VERSION "0.2.0"
#endif

/* crash.c: a note on stderr when the server dies of a signal */
void crash_handlers(void);

struct client {
	struct p_tab pt;		/* FIRST: the core's handle is &pt */
	int fd;				/* -1 = free slot */
	int ppid;
	int hello;			/* HELLO seen */
	int mirror;			/* CONMIRROR: the display half is on */
	int vsync_wait;			/* 0 none; 1 VSYNC; 2 VSYNCTRY */
	long long vsync_deadline;	/* VSYNCTRY: answer 0 at this time if no frame first */
	uint16_t tok;			/* the 16-bit owner id the sound core knows us by */
	int pcm_wait;			/* SNDIOC_PCMWAIT outstanding, to pcm_mark */
	uint32_t pcm_mark;
	unsigned keychan;		/* 0, or this connection's order as a key channel */
	unsigned char *ufont[PC3D_UFONTS];	/* copies of FONTDEF data */
	unsigned char *buf;		/* request payload */
	size_t bufcap;
};

/* What a request produced.  data/len is the reply payload; owned is
 * freed after sending; defer means "answer at the next frame". */
struct reply {
	int32_t ret;
	int32_t err;
	const void *data;
	uint32_t len;
	void *owned;
	int defer;
};

/* dispatch.c: misc.c's plt_dev_ioctl, for the codes the display
 * server answers. */
void pc3d_dispatch(struct client *c, uint16_t code, uint32_t arg,
		   const unsigned char *pl, uint32_t len, struct reply *r);
/* a connection ended: give back what it held */
void pc3d_client_gone(struct client *c);

/* disphw.c: the hooks display_priv.h asks for, and the framebuffers */
void disphw_init(void);
int  disphw_raster(void);		/* DISP_RASTER_VGA / XGA now */

/* present.c: the framebuffer as the monitor would show it */
void present_set_scale(int s);		/* window = 640x480 raster times s (1-4) */
void present_size(int *w, int *h);	/* window size for the live mode */
void present_frame(uint32_t *out);	/* fill out[w*h] as 0x00RRGGBB */
const char *present_mode_name(void);

/* presenter.c: the window on its own thread, as the scanout is core1's.
 * The request loop holds pc3d_display_lock around every dispatch; the
 * presenter takes it while it turns the framebuffer into window pixels
 * and pushes them without it.  A tick wakes it; a dirty mark says a
 * frame is worth pushing; it writes a byte to the wake pipe when the
 * window has produced key events. */
extern pthread_mutex_t pc3d_display_lock;
int  presenter_start(int wake_write_fd);
void presenter_stop(void);
void presenter_wake(void);
void presenter_mark_dirty(void);
unsigned long presenter_stats(unsigned long *us, unsigned long *idle_pumps);
/* pc3d.c: the window went away - stop, and wake the loop to notice */
void pc3d_window_closed(void);

/* window.c: MiniFB - on the presenter's thread only */
int  win_open(int w, int h);		/* (re)opens to this size; 0 ok */
int  win_present(const uint32_t *buf, int w, int h);	/* -1 = closed */
int  win_pump(void);			/* events only; -1 = closed */
void win_title(const char *t);
void win_close(void);

/* keyboard.c: the window's keys through kbd_decode.c */
void keyboard_event(int mfb_key, int pressed);	/* from MiniFB's callback (presenter thread) */
void keyboard_char(unsigned codepoint);		/* from MiniFB's char callback (presenter thread) */
int  keyboard_pending(void);			/* events queued and not yet pumped */
void keyboard_pump(void);			/* the request loop takes the queue */
void keyboard_tick(void);			/* every frame: auto-repeat */
void keyboard_reset(void);			/* the window closed */
void keyboard_inject(int mfb_key, int pressed);	/* PC3_INJECT */
int  keyboard_keydown(int n);			/* KEYDOWN(n) */
int  keyboard_set_layout(const char *name);	/* "UK", "US", ... */
const char *keyboard_layout_name(void);
void keyboard_set_log(int on);
/* pc3d.c: a decoded byte for the key channel */
void pc3d_key_byte(uint8_t c);

/* sndhw.c: the kernel's sound core behind miniaudio.  The core owns
 * and reaps by a 16-bit token (a Fuzix pid is one; a Linux pid is not),
 * which pc3d.c hands each connection and translates back. */
int  sndhw_init(const char *mode);		/* "auto" or "null"; 0 ok */
void sndhw_close(void);
const char *sndhw_backend(void);
unsigned long sndhw_blocks(void);
void sndhw_lock(void);				/* around every call into sound.c */
void sndhw_unlock(void);
int  sndhw_pcm_ready(uint16_t tok, uint32_t mark);	/* PCMWAIT can be answered */
void sndhw_client_gone(uint16_t tok);
/* pc3d.c: the token table */
int  pc3d_tok_alive(uint16_t tok);
int  pc3d_tok_pid(uint16_t tok);

/* netinfo.c: NETIOC_STATUS from the machine's own network */
struct net_status;
void netinfo_status(struct net_status *st);

extern int pc3d_verbose;

#endif /* PC3D_H */
