/*
 * pc3client.c - the client half of /dev/sys on a PC.
 *
 * What the kernel did with uget and valaddr - reach into the caller's
 * memory for the arrays a request points at - this does by copying them
 * into the message.  The server never sees a pointer.  What comes back
 * is copied into the caller's structure exactly where the kernel's uput
 * would have put it.
 *
 * Three families never reach the server at all, because on a PC they
 * are the process's own business: the PSRAM arena is anonymous memory
 * mapped below 4G (a base has to fit the uint32 the board hands out),
 * PICOIOC_RANDOM is the kernel's entropy source, and PICOIOC_LIBM is
 * refused - bcrun on a PC links the host's libm and never asks.
 *
 * Every code not listed is answered ENOTTY, which is what a PC3 kernel
 * without that ioctl says, and what every caller in the runtime treats
 * as "not available here".  Sound, the peripherals and the keyboard
 * join the list in their own phases.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/wait.h>
#include <time.h>
#include <poll.h>
#include <termios.h>

#include "pico_ioctl.h"
#include "pc3proto.h"
#include "pc3client.h"

/* The socket, read and written as one on both worlds: recv and send
 * are read and write on a stream socket, and on Windows the descriptor
 * is the shim's (pc3w.h), which read() and close() do not know. */
#ifdef _WIN32
#define sock_read(fd, b, n)  pc3w_recv((fd), (b), (n), 0)
#define sock_write(fd, b, n) pc3w_send((fd), (b), (n), 0)
#define sock_close(fd)       pc3w_sock_close(fd)
#else
#define sock_read(fd, b, n)  read((fd), (b), (n))
#define sock_write(fd, b, n) write((fd), (b), (n))
#define sock_close(fd)       close(fd)
#endif
#ifdef _WIN32
#define PATH_SEP ';'
#else
#define PATH_SEP ':'
#endif

/* --- the descriptors that are ours ----------------------------------------- */

#define MAXFD 16
static int ours[MAXFD];
static int nours;

int pc3_sys_isfd(int fd)
{
	int i;

	for (i = 0; i < nours; i++)
		if (ours[i] == fd)
			return 1;
	return 0;
}

static void remember(int fd)
{
	if (nours < MAXFD)
		ours[nours++] = fd;
}

static void forget(int fd)
{
	int i;

	for (i = 0; i < nours; i++)
		if (ours[i] == fd) {
			ours[i] = ours[--nours];
			return;
		}
}

/* --- the socket ------------------------------------------------------------ */

static void socket_path(char *buf, size_t n)
{
	const char *e = getenv(PC3_SOCKET_ENV);
	const char *x;

	if (e && *e) {
		snprintf(buf, n, "%s", e);
		return;
	}
#ifdef _WIN32
	/* the temporary directory: AF_UNIX on Windows wants a path too */
	(void)x;
	snprintf(buf, n, "%s/%s", pc3w_tmpdir(), PC3_SOCKET_NAME);
#else
	x = getenv("XDG_RUNTIME_DIR");
	if (x && *x)
		snprintf(buf, n, "%s/%s", x, PC3_SOCKET_NAME);
	else
		snprintf(buf, n, "/tmp/pc3d-%u.sock", (unsigned)getuid());
#endif
}

static int try_connect(const char *path)
{
	struct sockaddr_un sa;
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
		int e = errno;
		sock_close(fd);
		errno = e;
		return -1;
	}
	return fd;
}

/* Start a server.  The binary is $PC3D, or pc3d beside the program
 * running (the build tree and an installed layout both put them
 * together), or pc3d on the PATH.  Detached: a program's exit must
 * not take the display with it, any more than a process exiting takes
 * the kernel. */
static void start_server(void)
{
	char exe[4096], cand[4096 + 8];
	const char *bin = getenv(PC3_SERVER_ENV);
#ifndef _WIN32
	ssize_t n;
	pid_t pid;
#endif

	if (!bin || !*bin) {
#ifdef _WIN32
		if (pc3w_exe_dir(exe, sizeof exe) == 0) {
			snprintf(cand, sizeof cand, "%s/pc3d.exe", exe);
			if (access(cand, 0) == 0)
				bin = cand;
		}
#else
		n = readlink("/proc/self/exe", exe, sizeof exe - 1);
		if (n > 0) {
			char *slash;
			exe[n] = 0;
			slash = strrchr(exe, '/');
			if (slash) {
				*slash = 0;
				snprintf(cand, sizeof cand, "%s/pc3d", exe);
				if (access(cand, X_OK) == 0)
					bin = cand;
			}
		}
#endif
	}
	if (!bin || !*bin)
		bin = "pc3d";

#ifdef _WIN32
	{
		/* detached - no console, nobody waiting - with its messages
		 * in a log in the temporary directory, beside its socket */
		char log[4200];
		char *av[2];

		snprintf(log, sizeof log, "%s/pc3d.log", pc3w_tmpdir());
		av[0] = (char *)bin;
		av[1] = NULL;
		pc3w_spawn(bin, av, -1, -1, 1, log);
		return;
	}
#else
	pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		/* the grandchild is the server: no controlling terminal,
		 * no parent to wait for it, stdin from nowhere, and its
		 * messages in a log beside its socket rather than on the
		 * program's stdout - which, in a pipeline, it would have
		 * held open for as long as it lived */
		char log[4096];
		const char *x;
		int nul, lf;

		if (fork() != 0)
			_exit(0);
		setsid();
		nul = open("/dev/null", O_RDONLY);
		if (nul >= 0) {
			dup2(nul, 0);
			close(nul);
		}
		x = getenv("XDG_RUNTIME_DIR");
		if (x && *x)
			snprintf(log, sizeof log, "%s/pc3d.log", x);
		else
			snprintf(log, sizeof log, "/tmp/pc3d-%u.log", (unsigned)getuid());
		lf = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (lf >= 0) {
			dup2(lf, 1);
			dup2(lf, 2);
			close(lf);
		}
		execlp(bin, bin, (char *)NULL);
		_exit(127);
	}
	waitpid(pid, NULL, 0);		/* the middle process, at once */
#endif
}

static long exchange(int fd, uint16_t code, uint32_t arg,
		     const void *p0, size_t n0, const void *p1, size_t n1,
		     const void *p2, size_t n2, int *ret);
static unsigned char *repbuf;

/*
 * The server outlives the programs that use it - the picture survives
 * a program's exit, as on the board - so it outlives a package upgrade
 * too, and a new program then talks to the old server without knowing.
 * Once per process, ask the server its version and say if it is not
 * this one's; an older server that does not know the question is told
 * apart the same way.  Nothing else changes: the protocol is the same
 * and the program goes on.
 */
#ifndef PC3_CLIENT_VERSION
#define PC3_CLIENT_VERSION "?"
#endif
static void version_check(int fd)
{
	static int done;
	int ret;
	long n;
	char got[64];

	if (done)
		return;
	done = 1;
	n = exchange(fd, PC3_VERSION_REQ, 0, NULL, 0, NULL, 0, NULL, 0, &ret);
	if (n < 0 || ret < 0) {
		fprintf(stderr, "pc3: the display server running is older than this "
				"program (%s); stop it (pkill pc3d) and run again\n",
			PC3_CLIENT_VERSION);
		return;
	}
	if (n >= (long)sizeof got)
		n = sizeof got - 1;
	memcpy(got, repbuf, (size_t)n);
	got[n] = 0;
	if (strcmp(got, PC3_CLIENT_VERSION) != 0)
		fprintf(stderr, "pc3: the display server running is %s, this program "
				"is %s; stop it (pkill pc3d) and run again\n",
			got, PC3_CLIENT_VERSION);
}

int pc3_sys_open(void)
{
	char path[4096];
	const char *e = getenv(PC3_DISPLAY_ENV);
	int fd, tries;
	struct pc3_req rq;
	struct pc3_hello h;
	struct iovec iov[2];

	if (e && (!strcmp(e, "off") || !strcmp(e, "0") || !strcmp(e, "none"))) {
		errno = ENOENT;
		return -1;
	}
	socket_path(path, sizeof path);
	fd = try_connect(path);
	if (fd < 0) {
		e = getenv(PC3_AUTOSTART_ENV);
		if (e && (!strcmp(e, "0") || !strcmp(e, "off"))) {
			errno = ENOENT;
			return -1;
		}
		start_server();
		for (tries = 0; tries < 60 && fd < 0; tries++) {
			struct timespec ts = { 0, 50 * 1000 * 1000 };
			nanosleep(&ts, NULL);
			fd = try_connect(path);
		}
		if (fd < 0) {
			errno = ENOENT;
			return -1;
		}
	}
	/* HELLO: who we are, and whose child */
	rq.len = sizeof h;
	rq.code = PC3_HELLO;
	rq.flags = 0;
	rq.arg = 0;
	h.version = PC3_PROTO_VERSION;
	h.pid = (int32_t)getpid();
	h.ppid = (int32_t)getppid();
	iov[0].iov_base = &rq;
	iov[0].iov_len = sizeof rq;
	iov[1].iov_base = &h;
	iov[1].iov_len = sizeof h;
	if (writev(fd, iov, 2) != (ssize_t)(sizeof rq + sizeof h)) {
		sock_close(fd);
		errno = ENOENT;
		return -1;
	}
	/* A pipe closing under us is an ordinary error here, not a death:
	 * the runtime treats a failed ioctl as "no display". */
	signal(SIGPIPE, SIG_IGN);
	remember(fd);
	version_check(fd);
	return fd;
}

int pc3_sys_close(int fd)
{
	if (!pc3_sys_isfd(fd))
		return close(fd);
	forget(fd);
	return sock_close(fd);
}

/* --- the exchange ------------------------------------------------------------ */

/*
 * The server went away under a running program - its window was closed,
 * or it died.  On the board the display never goes away; on a PC the
 * closed window IS the user stopping the program, so the program is
 * interrupted as the window's Ctrl-C interrupts it (pc3_rd1), once.
 * Without this a program in an INKEY$ loop ran on unseen with nothing
 * to draw on, and one drawing in a loop drew into the void until it
 * ended on its own.
 */
static void server_gone(void)
{
	static int done;
	struct sigaction old;

	if (done)
		return;
	done = 1;
	/* a program started in the background by a non-interactive shell
	 * has SIGINT ignored; the runtime's guards cover SIGTERM too */
	if (sigaction(SIGINT, NULL, &old) == 0 && old.sa_handler == SIG_IGN)
		raise(SIGTERM);
	else
		raise(SIGINT);
}

static int read_full(int fd, void *buf, size_t n)
{
	unsigned char *p = buf;

	while (n) {
		ssize_t r = sock_read(fd, p, n);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (r == 0) {
			errno = EPIPE;
			return -1;
		}
		p += r;
		n -= (size_t)r;
	}
	return 0;
}

static unsigned char *repbuf;
static size_t repcap;

/* Send one request with up to three payload pieces and collect the
 * reply.  Returns the reply's payload length, or -1 with errno set from
 * the reply (or from the transport).  ret is the ioctl's return. */
static long exchange(int fd, uint16_t code, uint32_t arg,
		     const void *p0, size_t n0,
		     const void *p1, size_t n1,
		     const void *p2, size_t n2, int *ret)
{
	struct pc3_req rq;
	struct pc3_rep rp;
	struct iovec iov[4];
	int niov = 1;
	size_t total = sizeof rq;

	rq.len = (uint32_t)(n0 + n1 + n2);
	rq.code = code;
	rq.flags = 0;
	rq.arg = arg;
	iov[0].iov_base = &rq;
	iov[0].iov_len = sizeof rq;
	if (n0) { iov[niov].iov_base = (void *)p0; iov[niov].iov_len = n0; niov++; total += n0; }
	if (n1) { iov[niov].iov_base = (void *)p1; iov[niov].iov_len = n1; niov++; total += n1; }
	if (n2) { iov[niov].iov_base = (void *)p2; iov[niov].iov_len = n2; niov++; total += n2; }

	/* writev may be short on a big BLIT; finish it by hand */
	{
		ssize_t w = writev(fd, iov, niov);
		if (w < 0) {
			if (errno == EPIPE || errno == ECONNRESET)
				server_gone();
			return -1;
		}
		if ((size_t)w != total) {
			/* rare: fall back to writing the remainder piecewise */
			size_t done = (size_t)w;
			int i;
			for (i = 0; i < niov; i++) {
				size_t l = iov[i].iov_len;
				const unsigned char *b = iov[i].iov_base;
				if (done >= l) { done -= l; continue; }
				b += done; l -= done; done = 0;
				while (l) {
					ssize_t k = sock_write(fd, b, l);
					if (k < 0) {
						if (errno == EINTR) continue;
						return -1;
					}
					b += k; l -= (size_t)k;
				}
			}
		}
	}
	if (read_full(fd, &rp, sizeof rp) < 0) {
		if (errno == EPIPE || errno == ECONNRESET)
			server_gone();
		return -1;
	}
	if (rp.len > PC3_MAX_PAYLOAD) {
		errno = EPROTO;
		return -1;
	}
	if (rp.len > repcap) {
		unsigned char *nb = realloc(repbuf, rp.len);
		if (!nb) {
			errno = ENOMEM;
			return -1;
		}
		repbuf = nb;
		repcap = rp.len;
	}
	if (rp.len && read_full(fd, repbuf, rp.len) < 0)
		return -1;
	*ret = rp.ret;
	if (rp.ret < 0)
		errno = rp.err ? rp.err : EIO;
	return (long)rp.len;
}

/* A scalar-argument request with no payload and no reply data. */
static int simple(int fd, uint16_t code, uint32_t arg)
{
	int ret;

	if (exchange(fd, code, arg, NULL, 0, NULL, 0, NULL, 0, &ret) < 0)
		return -1;
	return ret;
}

/* A request that sends a structure and gets it (or another) back. */
static int roundtrip(int fd, uint16_t code, const void *in, size_t nin,
		     void *out, size_t nout)
{
	int ret;
	long got = exchange(fd, code, 0, in, nin, NULL, 0, NULL, 0, &ret);

	if (got < 0)
		return -1;
	if (out && nout) {
		if ((size_t)got < nout) {
			errno = EPROTO;
			return -1;
		}
		memcpy(out, repbuf, nout);
	}
	return ret;
}

/* --- the local families ------------------------------------------------------ */

/* Memory a program may hold as a 32-bit base: mapped below 4G. */
static void *low_map(size_t n)
{
	void *p;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_32BIT
	flags |= MAP_32BIT;
#endif
	n = (n + 4095) & ~(size_t)4095;
	p = mmap(NULL, n, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (p == MAP_FAILED)
		return NULL;
	if ((uintptr_t)p > 0xFFFFFFFFUL - n) {
		munmap(p, n);
		return NULL;
	}
	return p;
}

#define ARENA_SLOTS 64
static struct { uint32_t base; uint32_t len; } arena[ARENA_SLOTS];
#define ARENA_TOTAL (8u * 1024 * 1024)		/* the board's PSRAM */

static int arena_ioctl(unsigned long code, void *arg)
{
	struct psram_req *rq = arg;
	int i;

	switch (code) {
	case PSRAMIOC_ALLOC: {
		void *p;
		for (i = 0; i < ARENA_SLOTS && arena[i].base; i++)
			;
		if (i == ARENA_SLOTS || rq->len == 0) {
			errno = ENOMEM;
			return -1;
		}
		p = low_map(rq->len);
		if (!p) {
			errno = ENOMEM;
			return -1;
		}
		arena[i].base = (uint32_t)(uintptr_t)p;
		arena[i].len = rq->len;
		rq->base = arena[i].base;
		return 0;
	}
	case PSRAMIOC_REALLOC: {
		void *p;
		for (i = 0; i < ARENA_SLOTS; i++)
			if (arena[i].base && arena[i].base == rq->base)
				break;
		if (i == ARENA_SLOTS) {
			errno = EINVAL;
			return -1;
		}
		p = low_map(rq->len);
		if (!p) {
			errno = ENOMEM;
			return -1;
		}
		memcpy(p, (void *)(uintptr_t)arena[i].base,
		       rq->len < arena[i].len ? rq->len : arena[i].len);
		munmap((void *)(uintptr_t)arena[i].base,
		       (arena[i].len + 4095) & ~4095u);
		arena[i].base = (uint32_t)(uintptr_t)p;
		arena[i].len = rq->len;
		rq->base = arena[i].base;
		return 0;
	}
	case PSRAMIOC_FREE: {
		uint32_t b = *(uint32_t *)arg;
		for (i = 0; i < ARENA_SLOTS; i++)
			if (arena[i].base && arena[i].base == b) {
				munmap((void *)(uintptr_t)b,
				       (arena[i].len + 4095) & ~4095u);
				arena[i].base = 0;
				arena[i].len = 0;
				return 0;
			}
		errno = EINVAL;
		return -1;
	}
	case PSRAMIOC_STAT: {
		struct psram_stat *st = arg;
		uint32_t used = 0;
		for (i = 0; i < ARENA_SLOTS; i++)
			used += arena[i].base ? arena[i].len : 0;
		st->total = ARENA_TOTAL;
		st->free = used < ARENA_TOTAL ? ARENA_TOTAL - used : 0;
		st->largest = st->free;
		return 0;
	}
	}
	errno = ENOTTY;
	return -1;
}

/* Kernel fonts a program asked the address of: each fetched once and
 * kept below 4G for the life of the process, as the flash copy is for
 * the life of the board. */
static struct { int font; void *addr; uint32_t bytes; } fontcache[16];
static int nfontcache;

/* --- the ioctl ---------------------------------------------------------------- */

int pc3_sys_ioctl(int fd, unsigned long code, void *arg)
{
	int ret;

	if (!pc3_sys_isfd(fd))
		return ioctl(fd, code, arg);

	switch (code) {
	/* the value is the argument */
	case GFXIOC_PIXEL:
	case GFXIOC_COLOUR:
	case GFXIOC_GETPIXEL:
	case GFXIOC_FBOPEN:
	case GFXIOC_FBSEL:
	case GFXIOC_FBCOPY2:
	case GFXIOC_VSYNCTRY:
	case GFXIOC_MERGE:
	case GFXIOC_SCROLL:
	case GFXIOC_MAP:
	case GFXIOC_MAPCTL:
	case PICOIOC_CONMIRROR:
		return simple(fd, (uint16_t)code, (uint32_t)(uintptr_t)arg);

	/* an int, pointed at */
	case GFXIOC_MODE:
	case GFXIOC_PAL:
		if (!arg) {
			errno = EFAULT;
			return -1;
		}
		return simple(fd, (uint16_t)code, (uint32_t)*(int *)arg);

	case GFXIOC_VSYNC:
		return simple(fd, (uint16_t)code, 0);

	/* fixed-width structures, in */
	case GFXIOC_RECT:
		return roundtrip(fd, (uint16_t)code, arg, sizeof(struct gfx_rect), NULL, 0);
	case GFXIOC_SCROLL2:
		return roundtrip(fd, (uint16_t)code, arg, sizeof(struct gfx_scroll2), NULL, 0);

	/* fixed-width structures, in and out */
	case GFXIOC_FONTINFO:
		return roundtrip(fd, (uint16_t)code, arg, sizeof(struct gfx_fontinfo),
				 arg, sizeof(struct gfx_fontinfo));
	case GFXIOC_INFO:
		return roundtrip(fd, (uint16_t)code, NULL, 0, arg, sizeof(struct gfx_info));
	case PICOIOC_KEYDOWN:
		return roundtrip(fd, (uint16_t)code, NULL, 0, arg, sizeof(struct kbd_down));
	case PICOIOC_BOARD:
		return roundtrip(fd, (uint16_t)code, NULL, 0, arg, sizeof(int));

	/* the pointer-bearing ones */
	case GFXIOC_PIXELS:
	case GFXIOC_RECTS: {
		const struct gfx_batch *b = arg;
		struct pc3w_batch w;
		size_t isz = (code == GFXIOC_PIXELS) ? sizeof(struct gfx_pt)
						     : sizeof(struct gfx_rc);
		if (!b) { errno = EFAULT; return -1; }
		if (b->count == 0)
			return 0;
		if (b->count > GFX_BATCH_MAX || b->flags || !b->items) {
			errno = EINVAL;
			return -1;
		}
		w.count = b->count;
		w.flags = b->flags;
		w.has_colours = b->colours ? 1 : 0;
		w.pad = 0;
		if (exchange(fd, (uint16_t)code, 0, &w, sizeof w,
			     b->items, (size_t)b->count * isz,
			     b->colours, b->colours ? (size_t)b->count * 4 : 0,
			     &ret) < 0)
			return -1;
		return ret;
	}
	case GFXIOC_BITMAP: {
		const struct gfx_bitmap *gb = arg;
		struct pc3w_bitmap w;
		size_t nbits;
		if (!gb) { errno = EFAULT; return -1; }
		if (gb->width == 0 || gb->height == 0 || gb->scale == 0 || !gb->bits) {
			errno = EINVAL;
			return -1;
		}
		nbits = ((size_t)gb->width * gb->height + 7) / 8;
		w.x = gb->x; w.y = gb->y;
		w.width = gb->width; w.height = gb->height; w.scale = gb->scale;
		w.pad = 0; w.fg = gb->fg; w.bg = gb->bg;
		if (exchange(fd, (uint16_t)code, 0, &w, sizeof w, gb->bits, nbits,
			     NULL, 0, &ret) < 0)
			return -1;
		return ret;
	}
	case GFXIOC_TEXT: {
		const struct gfx_text *gt = arg;
		struct pc3w_text w;
		if (!gt) { errno = EFAULT; return -1; }
		if (gt->len == 0)
			return 0;
		if (gt->len > GFX_TEXT_MAX || !gt->str) {
			errno = EINVAL;
			return -1;
		}
		w.x = gt->x; w.y = gt->y; w.scale = gt->scale; w.font = gt->font;
		w.fg = gt->fg; w.bg = gt->bg; w.len = gt->len; w.pad = 0;
		if (exchange(fd, (uint16_t)code, 0, &w, sizeof w, gt->str, gt->len,
			     NULL, 0, &ret) < 0)
			return -1;
		return ret;
	}
	case GFXIOC_BLIT: {
		const struct gfx_blit *gb = arg;
		struct pc3w_blit w;
		if (!gb) { errno = EFAULT; return -1; }
		w.offset = gb->offset; w.len = gb->len;
		if (exchange(fd, (uint16_t)code, 0, &w, sizeof w, gb->buf, gb->len,
			     NULL, 0, &ret) < 0)
			return -1;
		return ret;
	}
	case GFXIOC_BLITRD: {
		const struct gfx_blit *gb = arg;
		struct pc3w_blit w;
		long got;
		if (!gb) { errno = EFAULT; return -1; }
		w.offset = gb->offset; w.len = gb->len;
		got = exchange(fd, (uint16_t)code, 0, &w, sizeof w, NULL, 0, NULL, 0, &ret);
		if (got < 0)
			return -1;
		if (ret >= 0) {
			if ((size_t)got < gb->len) { errno = EPROTO; return -1; }
			memcpy(gb->buf, repbuf, gb->len);
		}
		return ret;
	}
	case GFXIOC_BLITR:
	case GFXIOC_BLITRDR: {
		const struct gfx_blitr *gr = arg;
		struct pc3w_blitr w;
		size_t n;
		long got;
		if (!gr) { errno = EFAULT; return -1; }
		n = (size_t)gr->rows * gr->len;
		w.offset = gr->offset; w.len = gr->len; w.rows = gr->rows;
		w.stride = gr->stride; w.pad = 0;
		if (code == GFXIOC_BLITR) {
			if (exchange(fd, (uint16_t)code, 0, &w, sizeof w, gr->buf, n,
				     NULL, 0, &ret) < 0)
				return -1;
			return ret;
		}
		got = exchange(fd, (uint16_t)code, 0, &w, sizeof w, NULL, 0, NULL, 0, &ret);
		if (got < 0)
			return -1;
		if (ret >= 0) {
			if ((size_t)got < n) { errno = EPROTO; return -1; }
			memcpy(gr->buf, repbuf, n);
		}
		return ret;
	}
	case GFXIOC_FONTDEF: {
		const struct gfx_fontdef *fd_ = arg;
		struct pc3w_fontdef w;
		if (!fd_) { errno = EFAULT; return -1; }
		if (fd_->bytes < 4 || fd_->addr == 0) {
			errno = EFAULT;
			return -1;
		}
		w.font = fd_->font;
		w.pad[0] = w.pad[1] = w.pad[2] = 0;
		w.bytes = fd_->bytes;
		if (exchange(fd, (uint16_t)code, 0, &w, sizeof w,
			     (const void *)(uintptr_t)fd_->addr, fd_->bytes,
			     NULL, 0, &ret) < 0)
			return -1;
		return ret;
	}
	case GFXIOC_FONTADDR: {
		struct gfx_fontaddr *ga = arg;
		struct gfx_fontaddr back;
		long got;
		int i;
		if (!ga) { errno = EFAULT; return -1; }
		for (i = 0; i < nfontcache; i++)
			if (fontcache[i].font == ga->font) {
				ga->addr = (uint32_t)(uintptr_t)fontcache[i].addr;
				ga->bytes = fontcache[i].bytes;
				return 0;
			}
		got = exchange(fd, (uint16_t)code, 0, ga, sizeof *ga, NULL, 0, NULL, 0, &ret);
		if (got < 0)
			return -1;
		if (ret < 0)
			return ret;
		if ((size_t)got < sizeof back) { errno = EPROTO; return -1; }
		memcpy(&back, repbuf, sizeof back);
		ga->pad[0] = ga->pad[1] = ga->pad[2] = 0;
		ga->addr = 0;
		ga->bytes = back.bytes;
		if (back.bytes && (size_t)got >= sizeof back + back.bytes) {
			void *p = low_map(back.bytes);
			if (p) {
				memcpy(p, repbuf + sizeof back, back.bytes);
				ga->addr = (uint32_t)(uintptr_t)p;
				if (nfontcache < 16) {
					fontcache[nfontcache].font = ga->font;
					fontcache[nfontcache].addr = p;
					fontcache[nfontcache].bytes = back.bytes;
					nfontcache++;
				}
			}
		}
		return ret;
	}

	/* the process's own business */
	case PSRAMIOC_ALLOC:
	case PSRAMIOC_REALLOC:
	case PSRAMIOC_FREE:
	case PSRAMIOC_STAT:
		if (!arg) { errno = EFAULT; return -1; }
		return arena_ioctl(code, arg);
	case PICOIOC_RANDOM: {
		uint32_t r;
		if (!arg) { errno = EFAULT; return -1; }
		if (getrandom(&r, sizeof r, 0) != (ssize_t)sizeof r) {
			errno = EIO;
			return -1;
		}
		*(uint32_t *)arg = r;
		return 0;
	}
	case PICOIOC_KBDMAP:
		/* two letters, the layout's name */
		if (!arg) { errno = EFAULT; return -1; }
		return roundtrip(fd, (uint16_t)code, arg, 2, NULL, 0);

	/* ---- sound: fixed-width structures in, a structure back for STAT,
	 * and the PCM samples themselves as PCMWRITE's payload ---- */
	case SNDIOC_SOUND:
		if (!arg) { errno = EFAULT; return -1; }
		return roundtrip(fd, (uint16_t)code, arg, sizeof(struct snd_cmd), NULL, 0);
	case SNDIOC_ENV:
		if (!arg) { errno = EFAULT; return -1; }
		return roundtrip(fd, (uint16_t)code, arg, 14, NULL, 0);
	case SNDIOC_PCMOPEN:
		if (!arg) { errno = EFAULT; return -1; }
		return roundtrip(fd, (uint16_t)code, arg, sizeof(struct snd_pcm), NULL, 0);
	case SNDIOC_MMCMD:
		if (!arg) { errno = EFAULT; return -1; }
		return roundtrip(fd, (uint16_t)code, arg, sizeof(struct snd_mmcmd), NULL, 0);
	case SNDIOC_PCMSTAT:
		if (!arg) { errno = EFAULT; return -1; }
		return roundtrip(fd, (uint16_t)code, NULL, 0, arg, sizeof(struct snd_stat));
	case SNDIOC_PCMWRITE: {
		const struct snd_buf *b = arg;
		size_t n;

		if (!b || (!b->base && b->len)) { errno = EFAULT; return -1; }
		/* a short write is legal, so the payload ceiling is one */
		n = b->len > PC3_MAX_PAYLOAD ? PC3_MAX_PAYLOAD : b->len;
		if (exchange(fd, (uint16_t)code, 0, b->base, n, NULL, 0, NULL, 0, &ret) < 0)
			return -1;
		return ret;
	}
	case SNDIOC_PCMWAIT:
		/* the mark is the argument; the reply comes when the ring
		 * has drained to it, which is the blocking the kernel does */
		return simple(fd, (uint16_t)code, (uint32_t)(uintptr_t)arg);
	case SNDIOC_QUIET:
	case SNDIOC_PCMCLOSE:
	case SNDIOC_PCMOWNER:
	case SNDIOC_MMSTOP:
		return simple(fd, (uint16_t)code, 0);

	/* ---- the network: the status structure back, the join in, the
	 * CA bundle's bytes as the payload (a pointer in struct net_ca) */
	case NETIOC_STATUS:
		if (!arg) { errno = EFAULT; return -1; }
		return roundtrip(fd, (uint16_t)code, NULL, 0, arg, sizeof(struct net_status));
	case NETIOC_UP:
		if (!arg) { errno = EFAULT; return -1; }
		return roundtrip(fd, (uint16_t)code, arg, sizeof(struct net_join), NULL, 0);
	case NETIOC_DOWN:
		return simple(fd, (uint16_t)code, 0);
	case NETIOC_TLSCA: {
		const struct net_ca *ca = arg;
		size_t n;

		if (!ca || (!ca->buf && ca->len)) { errno = EFAULT; return -1; }
		n = ca->len > PC3_MAX_PAYLOAD ? PC3_MAX_PAYLOAD : ca->len;
		if (exchange(fd, (uint16_t)code, 0, ca->buf, n, NULL, 0, NULL, 0, &ret) < 0)
			return -1;
		return ret;
	}

	case PICOIOC_LIBM:
	default:
		break;
	}
	errno = ENOTTY;
	return -1;
}

/* --- the keyboard ------------------------------------------------------------ */

static int keyfd = -1;			/* the key channel, or -1 */
static time_t key_retry;		/* do not hammer connect() from a poll loop */
static int stdin_eof;			/* fd 0 is not a terminal and has ended */

/* Open the key channel to a RUNNING server - no autostart: a text
 * program polling INKEY$ should not conjure a window.  A program with
 * the display open reaches the same server, since there is one. */
static int key_open(void)
{
	char path[4096];
	const char *e = getenv(PC3_DISPLAY_ENV);
	struct pc3_req rq;
	struct pc3_hello h;
	struct iovec iov[2];
	int fd;
	time_t now = time(NULL);

	if (keyfd >= 0)
		return keyfd;
	if (e && (!strcmp(e, "off") || !strcmp(e, "0") || !strcmp(e, "none")))
		return -1;
	if (key_retry && now == key_retry)
		return -1;
	key_retry = now;
	socket_path(path, sizeof path);
	fd = try_connect(path);
	if (fd < 0)
		return -1;
	rq.len = sizeof h;
	rq.code = PC3_HELLO;
	rq.flags = 0;
	rq.arg = 0;
	h.version = PC3_PROTO_VERSION;
	h.pid = (int32_t)getpid();
	h.ppid = (int32_t)getppid();
	iov[0].iov_base = &rq;
	iov[0].iov_len = sizeof rq;
	iov[1].iov_base = &h;
	iov[1].iov_len = sizeof h;
	if (writev(fd, iov, 2) != (ssize_t)(sizeof rq + sizeof h)) {
		sock_close(fd);
		return -1;
	}
	rq.len = 0;
	rq.code = PC3_KEYCHAN;
	if (sock_write(fd, &rq, sizeof rq) != (ssize_t)sizeof rq) {
		sock_close(fd);
		return -1;
	}
	signal(SIGPIPE, SIG_IGN);
	keyfd = fd;
	return fd;
}

int pc3_key_ready(void)
{
	return key_open() >= 0;
}

int pc3_rd1(void)
{
	struct pollfd p[2];
	int n = 0, timeout = 0, r, ki = -1, ti = -1;
	unsigned char c;

	/* The terminal decides the wait: VMIN 0 and VTIME 0 is "what is
	 * there"; VTIME alone is the escape-sequence gap the runtime sets
	 * with tcsetattr; VMIN without VTIME is a blocking read. */
	if (!stdin_eof) {
		struct termios t;
		if (isatty(0) && tcgetattr(0, &t) == 0) {
			if (t.c_cc[VMIN] == 0)
				timeout = t.c_cc[VTIME] * 100;
			else
				timeout = t.c_cc[VTIME] ? t.c_cc[VTIME] * 100 : -1;
		}
		p[n].fd = 0;
		p[n].events = POLLIN;
		ti = n++;
	}
	if (key_open() >= 0) {
		p[n].fd = keyfd;
		p[n].events = POLLIN;
		ki = n++;
	}
	if (n == 0)
		return -1;
	r = poll(p, (nfds_t)n, timeout);
	if (r <= 0)
		return -1;
	/* the window first: its bytes are already decoded and never part
	 * of a sequence the terminal is in the middle of */
	if (ki >= 0 && (p[ki].revents & (POLLIN | POLLHUP | POLLERR))) {
		ssize_t k = sock_read(keyfd, &c, 1);
		if (k == 1) {
			if (c == 3) {
				raise(SIGINT);	/* the tty's ISIG, for the window */
				return -1;
			}
			return (int)c;
		}
		sock_close(keyfd);	/* the server went away */
		keyfd = -1;
		key_retry = 0;
		server_gone();		/* and with it the program's display */
		return -1;
	}
	if (ti >= 0 && (p[ti].revents & (POLLIN | POLLHUP | POLLERR))) {
#ifdef _WIN32
		ssize_t k = pc3w_read(0, &c, 1);	/* the console, as set */
#else
		ssize_t k = read(0, &c, 1);
#endif
		if (k == 1)
			return (int)c;
		if (k == 0 && !isatty(0))
			stdin_eof = 1;	/* /dev/null: stop asking it */
	}
	return -1;
}

int pc3_inject_key(int fd, int mfb_key, int pressed)
{
	struct pc3_inject in;
	int ret;

	if (!pc3_sys_isfd(fd)) {
		errno = EBADF;
		return -1;
	}
	in.key = (int32_t)mfb_key;
	in.pressed = pressed ? 1 : 0;
	in.mods = 0;
	if (exchange(fd, PC3_INJECT, 0, &in, sizeof in, NULL, 0, NULL, 0, &ret) < 0)
		return -1;
	return ret;
}

/* --- where things are ---------------------------------------------------------- */

int pc3_exe_dir(char *buf, size_t n)
{
#ifdef _WIN32
	return pc3w_exe_dir(buf, n);
#else
	ssize_t k = readlink("/proc/self/exe", buf, n - 1);
	char *slash;

	if (k <= 0)
		return -1;
	buf[k] = 0;
	slash = strrchr(buf, '/');
	if (!slash)
		return -1;
	*slash = 0;
	return 0;
#endif
}

void pc3_path_prepend(void)
{
	char dir[4096];
	const char *old = getenv("PATH");
	char *np;
	size_t n;

	if (pc3_exe_dir(dir, sizeof dir) < 0)
		return;
	if (old && strncmp(old, dir, strlen(dir)) == 0 &&
	    (old[strlen(dir)] == PATH_SEP || old[strlen(dir)] == 0))
		return;			/* already first */
	n = strlen(dir) + 1 + (old ? strlen(old) : 0) + 1;
	np = malloc(n);
	if (!np)
		return;
	snprintf(np, n, "%s%c%s", dir, old ? PATH_SEP : 0, old ? old : "");
	setenv("PATH", np, 1);
	free(np);
}
