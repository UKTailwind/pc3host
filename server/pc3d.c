/*
 * pc3d - the PC3 device server.
 *
 * On the Pico Computer 3 the kernel owns the display and every program
 * reaches it through /dev/sys.  On a PC this process owns the display:
 * it listens on a Unix socket, answers the same ioctl codes with the
 * same structures (dispatch.c), keeps the kernel's own display core
 * (display.c) fed through the seam in disphw.c, and once a frame shows
 * the framebuffer in a MiniFB window the way the monitor would show it
 * (present.c, window.c).
 *
 * One thread.  The loop is poll() over the listening socket and every
 * client, with a timeout that expires at the next frame, so requests
 * are answered as they arrive and the frame tick lands on time.  The
 * frame tick is what the PC3's vertical blanking is to a program: a
 * VSYNC waits for it, a VSYNCTRY asks whether it comes soon, and the
 * picture is taken from the framebuffer as it stands at that instant.
 *
 * A connection is a process.  What a process on the board holds -
 * a framebuffer claim, a write target, font slots, the console mirror -
 * a connection holds here, and it is given back when the connection
 * closes, which is the kernel's pagemap_free in one place.
 *
 *   pc3d [--headless] [--socket PATH] [--scale 1-4] [--snapshot FILE.ppm]
 *        [--keymap UK|US|DE|FR|ES|BE] [--keylog] [--verbose]
 *
 * --headless answers the protocol without a window, for tests.  The
 * default socket is $XDG_RUNTIME_DIR/pc3d.sock or /tmp/pc3d-<uid>.sock;
 * a client finds it the same way (pc3proto.h).  --scale is how many
 * window pixels a 640x480 raster pixel gets (present.c); 2 by default,
 * so the console is 1280x960 and a MODE 2 pixel is four times its size.
 * --keymap is the keyboard layout the decoder runs with (keyboard.c),
 * UK by default as on the board; --keylog prints every key event and
 * the byte it became, for checking a layout.
 */

/* No _GNU_SOURCE: it would make glibc 2.38+ headers rename strtol to a
 * symbol older systems lack, and everything used here is POSIX. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/stat.h>

#include "display.h"
#include "display_priv.h"
#include "pico_ioctl.h"
#include "pc3proto.h"
#include "pc3d.h"

int pc3d_verbose;

static struct client clients[PC3D_MAX_CLIENTS];
static int listen_fd = -1;
static char sock_path[4096];
static int headless;
static const char *audio_mode = "auto";
static volatile sig_atomic_t stopping;

/* The rasters' frame periods, from the kernel's video timing: 800x525
 * at 25 MHz and 1328x806 at 75 MHz. */
#define VGA_FRAME_US 16800
#define XGA_FRAME_US 14272

static unsigned long frames;
static int wake_r = -1, wake_w = -1;	/* the presenter's way of waking the loop */

static long long now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static long long next_tick;
static void snapshot(void);

static int frame_period(void)
{
	return disphw_raster() == DISP_RASTER_XGA ? XGA_FRAME_US : VGA_FRAME_US;
}

static void on_signal(int sig)
{
	(void)sig;
	stopping = 1;
}

/* The audio device's shutdown did not come back: leave without it. */
static void on_exit_alarm(int sig)
{
	(void)sig;
	_exit(0);
}

/* The presenter found the window closed: the display has gone away. */
void pc3d_window_closed(void)
{
	char x = 'x';

	if (pc3d_verbose)
		fprintf(stderr, "pc3d: window closed\n");
	stopping = 1;
	if (wake_w >= 0 && write(wake_w, &x, 1) < 0) { /* the loop will notice anyway */ }
}

/* Does a request change the picture?  The readers do not, and nor do
 * the sound and network families; everything else is a reason to show
 * a frame. */
static int draws(uint16_t code)
{
	switch (code) {
	case GFXIOC_GETPIXEL:
	case GFXIOC_INFO:
	case GFXIOC_FONTINFO:
	case GFXIOC_FONTADDR:
	case GFXIOC_BLITRD:
	case GFXIOC_BLITRDR:
	case GFXIOC_VSYNC:
	case GFXIOC_VSYNCTRY:
	case PICOIOC_KEYDOWN:
	case PICOIOC_BOARD:
	case PICOIOC_KBDMAP:
	case PICOIOC_CONMIRROR:
		return 0;
	default:
		break;
	}
	if (code >= NETIOC_UP && code <= NETIOC_TLSCA)
		return 0;
	if (code == SNDIOC_SOUND || code == SNDIOC_ENV || code == SNDIOC_QUIET ||
	    (code >= SNDIOC_PCMOPEN && code <= SNDIOC_PCMOWNER) ||
	    code == SNDIOC_PCMWAIT || code == SNDIOC_MMCMD || code == SNDIOC_MMSTOP)
		return 0;
	return 1;
}

/* --- the socket --------------------------------------------------------------- */

static void socket_default(char *buf, size_t n)
{
	const char *e = getenv(PC3_SOCKET_ENV);
	const char *x;

	if (e && *e) {
		snprintf(buf, n, "%s", e);
		return;
	}
	x = getenv("XDG_RUNTIME_DIR");
	if (x && *x)
		snprintf(buf, n, "%s/%s", x, PC3_SOCKET_NAME);
	else
		snprintf(buf, n, "/tmp/pc3d-%u.sock", (unsigned)getuid());
}

static int listen_on(const char *path)
{
	struct sockaddr_un sa;
	int fd, probe;

	/* A stale socket from a server that died is unlinked; a live one
	 * is left alone and we decline to start. */
	probe = socket(AF_UNIX, SOCK_STREAM, 0);
	if (probe >= 0) {
		memset(&sa, 0, sizeof sa);
		sa.sun_family = AF_UNIX;
		snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
		if (connect(probe, (struct sockaddr *)&sa, sizeof sa) == 0) {
			close(probe);
			fprintf(stderr, "pc3d: a server is already listening on %s\n", path);
			return -1;
		}
		close(probe);
	}
	unlink(path);

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("pc3d: socket");
		return -1;
	}
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
	if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
		perror("pc3d: bind");
		close(fd);
		return -1;
	}
	chmod(path, 0600);
	if (listen(fd, 8) < 0) {
		perror("pc3d: listen");
		close(fd);
		return -1;
	}
	return fd;
}

/* --- clients ---------------------------------------------------------------------- */

/* The sound core's idea of a process is a 16-bit pid, so each connection
 * gets a 16-bit token, never 0 and never one still in use. */
static uint16_t tok_next(void)
{
	static unsigned seq;
	int i, clash;

	do {
		seq = (seq + 1) & 0xFFFF;
		clash = (seq == 0);
		for (i = 0; i < PC3D_MAX_CLIENTS && !clash; i++)
			if (clients[i].fd >= 0 && clients[i].tok == seq)
				clash = 1;
	} while (clash);
	return (uint16_t)seq;
}

static struct client *client_new(int fd)
{
	int i;

	for (i = 0; i < PC3D_MAX_CLIENTS; i++)
		if (clients[i].fd < 0) {
			struct client *c = &clients[i];
			memset(c, 0, sizeof *c);
			c->fd = fd;
			c->mirror = 1;
			c->tok = tok_next();
			return c;
		}
	return NULL;
}

int pc3d_tok_alive(uint16_t tok)
{
	int i;

	for (i = 0; i < PC3D_MAX_CLIENTS; i++)
		if (clients[i].fd >= 0 && clients[i].tok == tok)
			return 1;
	return 0;
}

int pc3d_tok_pid(uint16_t tok)
{
	int i;

	for (i = 0; i < PC3D_MAX_CLIENTS; i++)
		if (clients[i].fd >= 0 && clients[i].tok == tok)
			return clients[i].pt.p_pid;
	return 0;
}

static struct client *client_by_pid(int pid)
{
	int i;

	for (i = 0; i < PC3D_MAX_CLIENTS; i++)
		if (clients[i].fd >= 0 && clients[i].hello && clients[i].pt.p_pid == pid)
			return &clients[i];
	return NULL;
}

static void client_close(struct client *c)
{
	int i;

	if (pc3d_verbose)
		fprintf(stderr, "pc3d: pid %d gone\n", c->pt.p_pid);
	snapshot();			/* what the window showed when it left */
	pc3d_client_gone(c);
	/* children lose their parent link, as they do when a parent dies */
	for (i = 0; i < PC3D_MAX_CLIENTS; i++)
		if (clients[i].fd >= 0 && clients[i].pt.p_pptr == &c->pt)
			clients[i].pt.p_pptr = NULL;
	close(c->fd);
	free(c->buf);
	c->buf = NULL;
	c->bufcap = 0;
	c->fd = -1;
}

static int read_full(int fd, void *buf, size_t n)
{
	unsigned char *p = buf;

	while (n) {
		ssize_t r = read(fd, p, n);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (r == 0)
			return -1;
		p += r;
		n -= (size_t)r;
	}
	return 0;
}

static int send_reply(struct client *c, int32_t ret, int32_t err,
		      const void *data, uint32_t len)
{
	struct pc3_rep rp;
	struct iovec iov[2];
	size_t total = sizeof rp + len;
	ssize_t w;

	rp.len = len;
	rp.ret = ret;
	rp.err = err;
	iov[0].iov_base = &rp;
	iov[0].iov_len = sizeof rp;
	iov[1].iov_base = (void *)data;
	iov[1].iov_len = len;
	w = writev(c->fd, iov, len ? 2 : 1);
	if (w < 0)
		return -1;
	if ((size_t)w < total) {
		/* finish a short write */
		size_t done = (size_t)w;
		const unsigned char *b;
		size_t l;
		if (done < sizeof rp) {
			b = (const unsigned char *)&rp + done;
			l = sizeof rp - done;
			while (l) {
				ssize_t k = write(c->fd, b, l);
				if (k < 0) return -1;
				b += k; l -= (size_t)k;
			}
			done = 0;
		} else
			done -= sizeof rp;
		b = (const unsigned char *)data + done;
		l = len - done;
		while (l) {
			ssize_t k = write(c->fd, b, l);
			if (k < 0) return -1;
			b += k; l -= (size_t)k;
		}
	}
	return 0;
}

/* --- the keyboard's output ------------------------------------------------------ */

static unsigned keychan_seq;

/* Typed before anyone listened: the kernel's input ring holds 64 bytes
 * for the next reader, and so does this. */
static uint8_t keyring[64];
static int keyring_n;

static struct client *key_owner(void)
{
	struct client *best = NULL;
	int i;

	for (i = 0; i < PC3D_MAX_CLIENTS; i++)
		if (clients[i].fd >= 0 && clients[i].keychan &&
		    (!best || clients[i].keychan > best->keychan))
			best = &clients[i];
	return best;
}

static void keyring_flush(struct client *c)
{
	int i;

	for (i = 0; i < keyring_n; i++)
		if (write(c->fd, &keyring[i], 1) != 1)
			break;
	keyring_n = 0;
}

/* The decoder's bytes for one event are gathered and written in ONE
 * write, so an arrow key's ESC [ A reaches the program whole: its
 * INKEY$ reads the ESC and looks for the rest at once, and on a tty
 * the kernel would have queued all three together.  Flushed after
 * every pump, tick and injection. */
static uint8_t keyout[64];
static int keyout_n;

void pc3d_key_byte(uint8_t c)
{
	if (keyout_n < (int)sizeof keyout)
		keyout[keyout_n++] = c;
}

static void key_flush(void)
{
	struct client *k;
	int i;

	if (!keyout_n)
		return;
	k = key_owner();
	if (!k) {
		/* nobody listening: keep it for the next channel, as the
		 * kernel's ring keeps what was typed before a read */
		for (i = 0; i < keyout_n && keyring_n < (int)sizeof keyring; i++)
			keyring[keyring_n++] = keyout[i];
		keyout_n = 0;
		return;
	}
	/* non-blocking: a program that never reads must not stall the
	 * display; a full pipe drops the bytes, as a full ring would */
	if (write(k->fd, keyout, (size_t)keyout_n) < 0 && errno != EAGAIN)
		client_close(k);
	keyout_n = 0;
}

/* One request from a client.  Returns -1 if the connection is finished. */
static int serve(struct client *c)
{
	struct pc3_req rq;
	struct reply r;

	if (read_full(c->fd, &rq, sizeof rq) < 0)
		return -1;
	if (rq.len > PC3_MAX_PAYLOAD)
		return -1;
	if (rq.len > c->bufcap) {
		unsigned char *nb = realloc(c->buf, rq.len);
		if (!nb)
			return -1;
		c->buf = nb;
		c->bufcap = rq.len;
	}
	if (rq.len && read_full(c->fd, c->buf, rq.len) < 0)
		return -1;

	if (rq.code == PC3_HELLO) {
		struct pc3_hello h;
		struct client *parent;
		if (rq.len < sizeof h)
			return -1;
		memcpy(&h, c->buf, sizeof h);
		if (h.version != PC3_PROTO_VERSION) {
			fprintf(stderr, "pc3d: client speaks protocol %u, this is %u\n",
				h.version, PC3_PROTO_VERSION);
			return -1;
		}
		c->pt.p_pid = h.pid;
		c->ppid = h.ppid;
		c->hello = 1;
		parent = client_by_pid(h.ppid);
		c->pt.p_pptr = parent ? &parent->pt : NULL;
		if (pc3d_verbose)
			fprintf(stderr, "pc3d: pid %d connected%s\n", h.pid,
				parent ? " (child of a client)" : "");
		return 0;			/* HELLO has no reply */
	}
	if (!c->hello)
		return -1;

	if (rq.code == PC3_KEYCHAN) {
		/* this connection is the keyboard's output from now on;
		 * whatever was typed before anyone listened goes first */
		c->keychan = ++keychan_seq;
		fcntl(c->fd, F_SETFL, fcntl(c->fd, F_GETFL) | O_NONBLOCK);
		if (pc3d_verbose)
			fprintf(stderr, "pc3d: pid %d takes the keyboard\n", c->pt.p_pid);
		keyring_flush(c);
		return 0;			/* no reply */
	}
	if (rq.code == PC3_INJECT) {
		struct pc3_inject in;
		if (rq.len < sizeof in)
			return -1;
		memcpy(&in, c->buf, sizeof in);
		keyboard_inject(in.key, in.pressed);
		key_flush();
		return send_reply(c, 0, 0, NULL, 0);
	}
	if (rq.code == PC3_VERSION_REQ)
		return send_reply(c, 0, 0, PC3D_VERSION, (uint32_t)strlen(PC3D_VERSION));

	/* the display core is shared with the presenter's expansion */
	pthread_mutex_lock(&pc3d_display_lock);
	pc3d_dispatch(c, rq.code, rq.arg, c->buf, rq.len, &r);
	pthread_mutex_unlock(&pc3d_display_lock);
	if (!headless && draws(rq.code))
		presenter_mark_dirty();
	if (r.defer) {
		/* VSYNCTRY: the kernel spends the budget waiting for the top
		 * of blanking and answers 1 if it came, 0 when the budget is
		 * gone.  So here: 1 at the frame tick if it falls inside the
		 * budget, else 0 when the budget has elapsed - the loop below
		 * keeps the deadline.  The first version answered 0 AT ONCE
		 * when the frame was further off than the budget, so the
		 * runtime's 32 tries were over in a millisecond and a copy
		 * ",B" never waited for anything. */
		if (c->vsync_wait == 2)
			c->vsync_deadline = now_us() + (long long)r.ret;
		return 0;
	}
	{
		int rc = send_reply(c, r.ret, r.err, r.data, r.len);
		free(r.owned);
		return rc;
	}
}

/* --- the frame ---------------------------------------------------------------------- */

static char snap_path[4096];

/*
 * --snapshot FILE: the window's pixels as a binary PPM, written every
 * time a client disconnects - so a script can run a program and then
 * look at what the window showed, headless or not.  It is what the
 * server presents, scale and all, not the framebuffer: saveimage
 * already gives the framebuffer.
 */
static void snapshot(void)
{
	int w, h;
	FILE *f;
	int x, y;
	uint32_t *frame;

	if (!snap_path[0])
		return;
	/* the expansion shares the display core and present.c's
	 * scratch with the presenter, so it is taken under the lock */
	pthread_mutex_lock(&pc3d_display_lock);
	present_size(&w, &h);
	frame = malloc((size_t)w * h * sizeof *frame);
	if (frame)
		present_frame(frame);
	pthread_mutex_unlock(&pc3d_display_lock);
	if (!frame)
		return;
	f = fopen(snap_path, "wb");
	if (f) {
		fprintf(f, "P6\n%d %d\n255\n", w, h);
		for (y = 0; y < h; y++)
			for (x = 0; x < w; x++) {
				uint32_t c = frame[(size_t)y * w + x];
				unsigned char rgb[3] = { (unsigned char)(c >> 16),
							 (unsigned char)(c >> 8),
							 (unsigned char)c };
				fwrite(rgb, 1, 3, f);
			}
		fclose(f);
		if (pc3d_verbose)
			fprintf(stderr, "pc3d: snapshot %dx%d -> %s\n", w, h, snap_path);
	}
	free(frame);
}

/* The frame tick: the PC3's vertical blanking, as a program sees it.
 * The picture itself is the presenter's business - it is woken here
 * and shows the frame if anything has been drawn. */
static void tick(void)
{
	int i;

	frames++;
	keyboard_tick();		/* auto-repeat, at the decoder's timing */
	key_flush();
	/* the waiters: VSYNC says 0, VSYNCTRY says 1 */
	for (i = 0; i < PC3D_MAX_CLIENTS; i++) {
		struct client *c = &clients[i];
		if (c->fd >= 0 && c->vsync_wait) {
			int32_t ret = (c->vsync_wait == 2) ? 1 : 0;
			c->vsync_wait = 0;
			if (send_reply(c, ret, 0, NULL, 0) < 0)
				client_close(c);
		}
	}
	if (!headless)
		presenter_wake();
}

/* --- main ----------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
	int i;
	struct pollfd pfd[PC3D_MAX_CLIENTS + 2];
	struct sigaction sa;

	crash_handlers();
	sock_path[0] = 0;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) {
			printf("pc3d %s\n", PC3D_VERSION);
			return 0;
		}
		if (!strcmp(argv[i], "--headless"))
			headless = 1;
		else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v"))
			pc3d_verbose = 1;
		else if (!strcmp(argv[i], "--socket") && i + 1 < argc)
			snprintf(sock_path, sizeof sock_path, "%s", argv[++i]);
		else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
			present_set_scale(atoi(argv[++i]));
		else if (!strcmp(argv[i], "--snapshot") && i + 1 < argc)
			snprintf(snap_path, sizeof snap_path, "%s", argv[++i]);
		else if (!strcmp(argv[i], "--keymap") && i + 1 < argc) {
			if (keyboard_set_layout(argv[++i])) {
				fprintf(stderr, "pc3d: no keyboard layout %s "
						"(US UK DE FR ES BE)\n", argv[i]);
				return 2;
			}
		} else if (!strcmp(argv[i], "--keylog"))
			keyboard_set_log(1);
		else if (!strcmp(argv[i], "--audio") && i + 1 < argc) {
			audio_mode = argv[++i];
			if (strcmp(audio_mode, "auto") && strcmp(audio_mode, "null")) {
				fprintf(stderr, "pc3d: --audio auto|null\n");
				return 2;
			}
		} else {
			fprintf(stderr, "usage: pc3d [--headless] [--socket PATH] "
					"[--scale 1-4] [--snapshot FILE.ppm] "
					"[--keymap UK|US|DE|FR|ES|BE] [--keylog] "
					"[--audio auto|null] [--verbose]\n");
			return 2;
		}
	}
	if (!sock_path[0])
		socket_default(sock_path, sizeof sock_path);

	for (i = 0; i < PC3D_MAX_CLIENTS; i++)
		clients[i].fd = -1;

	disphw_init();
	display_gfx_mode(0xFF);			/* the console, as at boot */
	if (sndhw_init(audio_mode) < 0)
		fprintf(stderr, "pc3d: no audio at all; sound requests will hang\n");

	listen_fd = listen_on(sock_path);
	if (listen_fd < 0)
		return 1;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_signal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	if (pc3d_verbose)
		fprintf(stderr, "pc3d %s: listening on %s%s, keyboard layout %s, audio %s\n",
			PC3D_VERSION, sock_path, headless ? " (headless)" : "",
			keyboard_layout_name(), sndhw_backend());

	next_tick = now_us() + frame_period();
	if (!headless) {
		int pf[2];

		/* the presenter's wake pipe: a key event on the window's
		 * thread is a byte here, and the loop's poll returns */
		if (pipe(pf) == 0) {
			fcntl(pf[0], F_SETFL, fcntl(pf[0], F_GETFL) | O_NONBLOCK);
			fcntl(pf[1], F_SETFL, fcntl(pf[1], F_GETFL) | O_NONBLOCK);
			wake_r = pf[0];
			wake_w = pf[1];
		}
		if (presenter_start(wake_w) < 0) {
			fprintf(stderr, "pc3d: cannot start the presenter thread\n");
			goto out;
		}
	}

	while (!stopping) {
		int n = 0, r, waiting = 0, wi = -1;
		long long left;

		pfd[n].fd = listen_fd;
		pfd[n].events = POLLIN;
		n++;
		for (i = 0; i < PC3D_MAX_CLIENTS; i++)
			if (clients[i].fd >= 0) {
				pfd[n].fd = clients[i].fd;
				/* a client waiting for the frame, or for the
				 * ring to drain, is not read until it has its
				 * answer */
				pfd[n].events = (clients[i].vsync_wait || clients[i].pcm_wait)
						? 0 : POLLIN;
				if (clients[i].pcm_wait)
					waiting = 1;
				n++;
			}
		if (wake_r >= 0) {
			pfd[n].fd = wake_r;
			pfd[n].events = POLLIN;
			wi = n++;
		}
		left = next_tick - now_us();
		if (left < 0)
			left = 0;
		/* A PCMWAIT is answered from this loop, so while one is
		 * outstanding the loop runs at the kernel's tick rather
		 * than the frame's: 5 ms there, 2 here. */
		if (waiting && left > 2000)
			left = 2000;
		/* and a VSYNCTRY's budget may run out before the frame */
		for (i = 0; i < PC3D_MAX_CLIENTS; i++)
			if (clients[i].fd >= 0 && clients[i].vsync_wait == 2) {
				long long d = clients[i].vsync_deadline - now_us();
				if (d < 0)
					d = 0;
				if (d < left)
					left = d;
			}
		r = poll(pfd, (nfds_t)n, (int)((left + 999) / 1000));
		if (r < 0 && errno != EINTR) {
			perror("pc3d: poll");
			break;
		}
		if (r > 0) {
			int k = 1;
			if (wi >= 0 && (pfd[wi].revents & POLLIN)) {
				char junk[64];
				while (read(wake_r, junk, sizeof junk) > 0)
					;
			}
			if (pfd[0].revents & POLLIN) {
				int fd = accept(listen_fd, NULL, NULL);
				if (fd >= 0) {
					struct client *c = client_new(fd);
					if (!c)
						close(fd);
				}
			}
			for (i = 0; i < PC3D_MAX_CLIENTS; i++) {
				struct client *c = &clients[i];
				if (c->fd < 0)
					continue;
				/* pfd entries were built in client order */
				if (k < n && pfd[k].fd == c->fd) {
					if (pfd[k].revents & (POLLIN | POLLHUP | POLLERR)) {
						if (pfd[k].revents & POLLIN) {
							if (serve(c) < 0)
								client_close(c);
						} else
							client_close(c);
					}
					k++;
				}
			}
		}
		/* VSYNCTRY budgets that ran out before the frame: 0 */
		for (i = 0; i < PC3D_MAX_CLIENTS; i++) {
			struct client *c = &clients[i];
			if (c->fd >= 0 && c->vsync_wait == 2 &&
			    now_us() >= c->vsync_deadline && now_us() < next_tick) {
				c->vsync_wait = 0;
				if (send_reply(c, 0, 0, NULL, 0) < 0)
					client_close(c);
			}
		}
		/* the players sleeping on the ring: the kernel's
		 * sound_pcm_tick, answered when the level has dropped to
		 * the mark or the stream has gone */
		for (i = 0; i < PC3D_MAX_CLIENTS; i++) {
			struct client *c = &clients[i];
			if (c->fd >= 0 && c->pcm_wait &&
			    sndhw_pcm_ready(c->tok, c->pcm_mark)) {
				c->pcm_wait = 0;
				if (send_reply(c, 0, 0, NULL, 0) < 0)
					client_close(c);
			}
		}
		/* the window's keys, queued on the presenter's thread */
		if (!headless) {
			keyboard_pump();
			key_flush();
		}
		if (now_us() >= next_tick) {
			tick();
			next_tick += frame_period();
			if (next_tick < now_us())	/* fell behind: resync */
				next_tick = now_us() + frame_period();
		}
	}
out:
	/* The socket first: from here on nobody can connect to a server
	 * that is going.  A program whose window has gone found the
	 * server still listening while the audio device was being shut
	 * down, and on a desktop where that shutdown stalls it found it
	 * for good - a live socket, no window, and a reboot to clear it. */
	close(listen_fd);
	unlink(sock_path);
	presenter_stop();		/* it closes the window on its own thread */
	for (i = 0; i < PC3D_MAX_CLIENTS; i++)
		if (clients[i].fd >= 0)
			client_close(&clients[i]);
	/* and the audio device gets two seconds; the process is exiting
	 * and the sound server reclaims a stream whose client has gone */
	signal(SIGALRM, on_exit_alarm);
	alarm(2);
	sndhw_close();
	alarm(0);
	if (pc3d_verbose) {
		unsigned long us = 0, pumps = 0, np = presenter_stats(&us, &pumps);

		fprintf(stderr, "pc3d: %lu frames, %lu presented (%.1f ms each), "
				"%lu sound blocks, exiting\n",
			frames, np, np ? (double)us / np / 1000.0 : 0.0, sndhw_blocks());
	}
	return 0;
}
