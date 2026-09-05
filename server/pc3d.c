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

#define _GNU_SOURCE
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
static volatile sig_atomic_t stopping;

/* The rasters' frame periods, from the kernel's video timing: 800x525
 * at 25 MHz and 1328x806 at 75 MHz. */
#define VGA_FRAME_US 16800
#define XGA_FRAME_US 14272

static uint32_t *frame;			/* the window's pixels, sized at each change */
static size_t frame_cap;
static unsigned long frames;

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

static struct client *client_new(int fd)
{
	int i;

	for (i = 0; i < PC3D_MAX_CLIENTS; i++)
		if (clients[i].fd < 0) {
			struct client *c = &clients[i];
			memset(c, 0, sizeof *c);
			c->fd = fd;
			c->mirror = 1;
			return c;
		}
	return NULL;
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

	pc3d_dispatch(c, rq.code, rq.arg, c->buf, rq.len, &r);
	if (r.defer) {
		/* VSYNCTRY: is the next frame inside the budget?  If not,
		 * the answer is 0 now; if so, 1 when it comes. */
		if (c->vsync_wait == 2) {
			long long left = next_tick - now_us();
			if (left > (long long)r.ret) {
				c->vsync_wait = 0;
				return send_reply(c, 0, 0, NULL, 0);
			}
		}
		return 0;
	}
	{
		int rc = send_reply(c, r.ret, r.err, r.data, r.len);
		free(r.owned);
		return rc;
	}
}

/* --- the frame ---------------------------------------------------------------------- */

static int last_w, last_h;
static char snap_path[4096];

/* Size the window buffer for the live mode. */
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

	if (!snap_path[0] || frame_fit(&w, &h) < 0)
		return;
	present_frame(frame);
	f = fopen(snap_path, "wb");
	if (!f)
		return;
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

static int tick(void)
{
	int i, w, h;

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
	if (headless)
		return 0;

	if (frame_fit(&w, &h) < 0)
		return -1;
	if (w != last_w || h != last_h) {
		if (win_open(w, h) < 0)
			return -1;
		win_title(present_mode_name());
		last_w = w;
		last_h = h;
	}
	present_frame(frame);
	if (win_present(frame, w, h) < 0) {
		if (pc3d_verbose)
			fprintf(stderr, "pc3d: window closed\n");
		return -1;
	}
	key_flush();			/* what the window's events produced */
	return 0;
}

/* --- main ----------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
	int i;
	struct pollfd pfd[PC3D_MAX_CLIENTS + 1];
	struct sigaction sa;

	sock_path[0] = 0;
	for (i = 1; i < argc; i++) {
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
		else {
			fprintf(stderr, "usage: pc3d [--headless] [--socket PATH] "
					"[--scale 1-4] [--snapshot FILE.ppm] "
					"[--keymap UK|US|DE|FR|ES|BE] [--keylog] [--verbose]\n");
			return 2;
		}
	}
	if (!sock_path[0])
		socket_default(sock_path, sizeof sock_path);

	for (i = 0; i < PC3D_MAX_CLIENTS; i++)
		clients[i].fd = -1;

	disphw_init();
	display_gfx_mode(0xFF);			/* the console, as at boot */

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
		fprintf(stderr, "pc3d: listening on %s%s, keyboard layout %s\n",
			sock_path, headless ? " (headless)" : "",
			keyboard_layout_name());

	next_tick = now_us() + frame_period();
	if (!headless && tick() < 0)
		goto out;

	while (!stopping) {
		int n = 0, r;
		long long left;

		pfd[n].fd = listen_fd;
		pfd[n].events = POLLIN;
		n++;
		for (i = 0; i < PC3D_MAX_CLIENTS; i++)
			if (clients[i].fd >= 0) {
				pfd[n].fd = clients[i].fd;
				/* a client waiting for the frame is not read
				 * until it has its answer */
				pfd[n].events = clients[i].vsync_wait ? 0 : POLLIN;
				n++;
			}
		left = next_tick - now_us();
		if (left < 0)
			left = 0;
		r = poll(pfd, (nfds_t)n, (int)((left + 999) / 1000));
		if (r < 0 && errno != EINTR) {
			perror("pc3d: poll");
			break;
		}
		if (r > 0) {
			int k = 1;
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
		if (now_us() >= next_tick) {
			if (tick() < 0)
				break;
			next_tick += frame_period();
			if (next_tick < now_us())	/* fell behind: resync */
				next_tick = now_us() + frame_period();
		} else if (!headless) {
			if (win_pump() < 0)
				break;
			key_flush();
		}
	}
out:
	win_close();
	for (i = 0; i < PC3D_MAX_CLIENTS; i++)
		if (clients[i].fd >= 0)
			client_close(&clients[i]);
	close(listen_fd);
	unlink(sock_path);
	if (pc3d_verbose)
		fprintf(stderr, "pc3d: %lu frames, exiting\n", frames);
	return 0;
}
