/*
 * winshim.c - the POSIX surface the hosted PC3 programs use, on Windows.
 *
 * See pc3w.h for the contract and include/ for the headers that route
 * the sources here.  This file talks to Winsock and Win32 directly and
 * must not see the renaming macros in the stand-in headers, so it
 * includes the system headers itself and only pc3w.h, termios.h and
 * sys/uio.h of its own - none of which rename anything.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <afunix.h>
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>

#include "pc3w.h"
#include "termios.h"
#include "sys/uio.h"

/* the values the stand-in fcntl.h gives the sources; kept in step */
#define PC3W_O_NONBLOCK 0x100000
#define PC3W_F_GETFD 1
#define PC3W_F_SETFD 2
#define PC3W_F_GETFL 3
#define PC3W_F_SETFL 4
#define PC3W_TIOCGWINSZ 0x5413

/* ---- errno -------------------------------------------------------------- */

void pc3w_seterrno(void)
{
	int e;

	switch (WSAGetLastError()) {
	case WSAEWOULDBLOCK:   e = EAGAIN; break;
	case WSAEINPROGRESS:   e = EINPROGRESS; break;
	case WSAEALREADY:      e = EALREADY; break;
	case WSAEISCONN:       e = EISCONN; break;
	case WSAECONNREFUSED:  e = ECONNREFUSED; break;
	case WSAECONNRESET:    e = ECONNRESET; break;
	case WSAECONNABORTED:  e = ECONNABORTED; break;
	case WSAETIMEDOUT:     e = ETIMEDOUT; break;
	case WSAEADDRINUSE:    e = EADDRINUSE; break;
	case WSAEADDRNOTAVAIL: e = EADDRNOTAVAIL; break;
	case WSAENOTCONN:      e = ENOTCONN; break;
	case WSAENETUNREACH:   e = ENETUNREACH; break;
	case WSAEHOSTUNREACH:  e = EHOSTUNREACH; break;
	case WSAENETDOWN:      e = ENETDOWN; break;
	case WSAEINTR:         e = EINTR; break;
	case WSAENOTSOCK:      e = EBADF; break;
	case WSAEBADF:         e = EBADF; break;
	case WSAEACCES:        e = EACCES; break;
	case WSAEAFNOSUPPORT:  e = EAFNOSUPPORT; break;
	case WSAEMSGSIZE:      e = EMSGSIZE; break;
	case WSAEINVAL:        e = EINVAL; break;
	case WSAESHUTDOWN:     e = EPIPE; break;
	case WSAEMFILE:        e = EMFILE; break;
	case WSAENOBUFS:       e = ENOMEM; break;
	default:               e = EIO; break;
	}
	errno = e;
}

static long fail(void)
{
	pc3w_seterrno();
	return -1;
}

/* ---- sockets -------------------------------------------------------------- */

static SOCKET socks[PC3W_SOCK_MAX];
static unsigned char sock_used[PC3W_SOCK_MAX];
static unsigned char sock_nb[PC3W_SOCK_MAX];
static int wsa_up;

static void wsa_init(void)
{
	WSADATA w;

	if (!wsa_up) {
		WSAStartup(MAKEWORD(2, 2), &w);
		wsa_up = 1;
	}
}

static int sock_alloc(SOCKET s)
{
	int i;

	for (i = 0; i < PC3W_SOCK_MAX; i++)
		if (!sock_used[i]) {
			sock_used[i] = 1;
			sock_nb[i] = 0;
			socks[i] = s;
			return PC3W_SOCK_BASE + i;
		}
	closesocket(s);
	errno = EMFILE;
	return -1;
}

int pc3w_is_sock(int fd)
{
	int i = fd - PC3W_SOCK_BASE;

	return i >= 0 && i < PC3W_SOCK_MAX && sock_used[i];
}

uintptr_t pc3w_sock(int fd)
{
	return pc3w_is_sock(fd) ? (uintptr_t)socks[fd - PC3W_SOCK_BASE]
				: (uintptr_t)INVALID_SOCKET;
}

#define S(fd) ((SOCKET)pc3w_sock(fd))

static int badfd(int fd)
{
	if (!pc3w_is_sock(fd)) {
		errno = EBADF;
		return 1;
	}
	return 0;
}

int pc3w_socket(int domain, int type, int protocol)
{
	SOCKET s;

	wsa_init();
	/* Not inheritable: a program this one starts must not hold its
	 * connection to the server open after it has gone. */
	s = WSASocketA(domain, type, protocol, NULL, 0, WSA_FLAG_NO_HANDLE_INHERIT);
	if (s == INVALID_SOCKET)
		return (int)fail();
	return sock_alloc(s);
}

int pc3w_connect(int fd, const void *sa, int len)
{
	if (badfd(fd))
		return -1;
	if (connect(S(fd), (const struct sockaddr *)sa, len) == 0)
		return 0;
	pc3w_seterrno();
	/* a non-blocking connect: Winsock says "would block" where POSIX
	 * says "in progress", and the program's loop knows the latter */
	if (errno == EAGAIN)
		errno = EINPROGRESS;
	return -1;
}

int pc3w_bind(int fd, const void *sa, int len)
{
	if (badfd(fd))
		return -1;
	return bind(S(fd), (const struct sockaddr *)sa, len) ? (int)fail() : 0;
}

int pc3w_listen(int fd, int backlog)
{
	if (badfd(fd))
		return -1;
	return listen(S(fd), backlog) ? (int)fail() : 0;
}

int pc3w_accept(int fd, void *sa, int *len)
{
	SOCKET s;

	if (badfd(fd))
		return -1;
	s = accept(S(fd), (struct sockaddr *)sa, len);
	if (s == INVALID_SOCKET)
		return (int)fail();
	SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0);
	return sock_alloc(s);
}

long pc3w_send(int fd, const void *buf, size_t n, int flags)
{
	int r;

	if (badfd(fd))
		return -1;
	r = send(S(fd), (const char *)buf, (int)n, flags);
	return r == SOCKET_ERROR ? fail() : r;
}

long pc3w_recv(int fd, void *buf, size_t n, int flags)
{
	int r;

	if (badfd(fd))
		return -1;
	r = recv(S(fd), (char *)buf, (int)n, flags);
	return r == SOCKET_ERROR ? fail() : r;
}

long pc3w_sendto(int fd, const void *buf, size_t n, int flags, const void *sa, int len)
{
	int r;

	if (badfd(fd))
		return -1;
	r = sendto(S(fd), (const char *)buf, (int)n, flags, (const struct sockaddr *)sa, len);
	return r == SOCKET_ERROR ? fail() : r;
}

long pc3w_recvfrom(int fd, void *buf, size_t n, int flags, void *sa, int *len)
{
	int r;

	if (badfd(fd))
		return -1;
	r = recvfrom(S(fd), (char *)buf, (int)n, flags, (struct sockaddr *)sa, len);
	return r == SOCKET_ERROR ? fail() : r;
}

int pc3w_setsockopt(int fd, int level, int opt, const void *v, int len)
{
	if (badfd(fd))
		return -1;
	return setsockopt(S(fd), level, opt, (const char *)v, len) ? (int)fail() : 0;
}

int pc3w_shutdown(int fd, int how)
{
	if (badfd(fd))
		return -1;
	return shutdown(S(fd), how) ? (int)fail() : 0;
}

int pc3w_sock_close(int fd)
{
	int i;

	if (badfd(fd))
		return -1;
	i = fd - PC3W_SOCK_BASE;
	closesocket(socks[i]);
	sock_used[i] = 0;
	return 0;
}

int pc3w_nonblock(int fd, int on)
{
	u_long v = on ? 1 : 0;

	if (badfd(fd))
		return -1;
	if (ioctlsocket(S(fd), FIONBIO, &v))
		return (int)fail();
	sock_nb[fd - PC3W_SOCK_BASE] = on ? 1 : 0;
	return 0;
}

int pc3w_is_nonblock(int fd)
{
	return pc3w_is_sock(fd) && sock_nb[fd - PC3W_SOCK_BASE];
}

/* A connected pair over the loopback: what socketpair() gives, which
 * Winsock has not got.  The wake pipe between the server's threads. */
int pc3w_socketpair(int fds[2])
{
	SOCKET l = INVALID_SOCKET, a = INVALID_SOCKET, b = INVALID_SOCKET;
	struct sockaddr_in sa;
	int len = sizeof sa;

	wsa_init();
	l = socket(AF_INET, SOCK_STREAM, 0);
	if (l == INVALID_SOCKET)
		goto bad;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = 0;
	if (bind(l, (struct sockaddr *)&sa, sizeof sa) || listen(l, 1) ||
	    getsockname(l, (struct sockaddr *)&sa, &len))
		goto bad;
	a = WSASocketA(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_NO_HANDLE_INHERIT);
	if (a == INVALID_SOCKET || connect(a, (struct sockaddr *)&sa, sizeof sa))
		goto bad;
	b = accept(l, NULL, NULL);
	if (b == INVALID_SOCKET)
		goto bad;
	SetHandleInformation((HANDLE)b, HANDLE_FLAG_INHERIT, 0);
	closesocket(l);
	fds[0] = sock_alloc(b);
	fds[1] = sock_alloc(a);
	if (fds[0] < 0 || fds[1] < 0)
		return -1;
	return 0;
bad:
	pc3w_seterrno();
	if (l != INVALID_SOCKET) closesocket(l);
	if (a != INVALID_SOCKET) closesocket(a);
	if (b != INVALID_SOCKET) closesocket(b);
	return -1;
}

long pc3w_writev(int fd, const struct iovec *iov, int n)
{
	WSABUF b[16];
	DWORD sent = 0;
	int i;

	if (badfd(fd))
		return -1;
	if (n > 16)
		n = 16;
	for (i = 0; i < n; i++) {
		b[i].buf = (char *)iov[i].iov_base;
		b[i].len = (ULONG)iov[i].iov_len;
	}
	if (WSASend(S(fd), b, (DWORD)n, &sent, 0, NULL, NULL) == SOCKET_ERROR)
		return fail();
	return (long)sent;
}

/* ---- the console ------------------------------------------------------------ */

static HANDLE con_in = INVALID_HANDLE_VALUE;
static int con_known;		/* con_is set */
static int con_is;		/* descriptor 0 is a console */
static DWORD con_mode0;		/* as we found it */
static int con_saved;
static int fd0_nonblock;	/* fcntl O_NONBLOCK on descriptor 0 */
static struct termios cur;
static int cur_set;

static void cur_init(void)
{
	if (cur_set)
		return;
	cur_set = 1;
	memset(&cur, 0, sizeof cur);
	cur.c_iflag = ICRNL | IXON | BRKINT;
	cur.c_oflag = OPOST | ONLCR;
	cur.c_cflag = CS8;
	cur.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
	cur.c_cc[VMIN] = 1;
	cur.c_cc[VTIME] = 0;
}

static int con_check(void)
{
	DWORD m;

	if (!con_known) {
		con_known = 1;
		con_in = GetStdHandle(STD_INPUT_HANDLE);
		con_is = (con_in != INVALID_HANDLE_VALUE && con_in != NULL &&
			  GetConsoleMode(con_in, &m)) ? 1 : 0;
		cur_init();
	}
	return con_is;
}

int pc3w_con_isatty(int fd)
{
	if (fd == 0)
		return con_check();
	return _isatty(fd);
}

/* Escape sequences and ANSI colour on the way out: on by default in
 * Windows Terminal, not in the classic console. */
static void con_out_init(void)
{
	static int done;
	HANDLE h;
	DWORD m;

	if (done)
		return;
	done = 1;
	h = GetStdHandle(STD_OUTPUT_HANDLE);
	if (h != INVALID_HANDLE_VALUE && h != NULL && GetConsoleMode(h, &m))
		SetConsoleMode(h, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	h = GetStdHandle(STD_ERROR_HANDLE);
	if (h != INVALID_HANDLE_VALUE && h != NULL && GetConsoleMode(h, &m))
		SetConsoleMode(h, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

/* Apply the termios: raw means no line input and no echo, with the
 * console translating keys into the sequences a terminal sends; ISIG
 * keeps Ctrl-C a signal.  Cooked puts the console back as found. */
static void con_apply(void)
{
	DWORD m;

	if (!con_check())
		return;
	if (!con_saved) {
		GetConsoleMode(con_in, &con_mode0);
		con_saved = 1;
	}
	if (cur.c_lflag & ICANON) {
		SetConsoleMode(con_in, con_mode0);
		return;
	}
	m = con_mode0 & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
	m |= ENABLE_VIRTUAL_TERMINAL_INPUT;
	if (cur.c_lflag & ISIG)
		m |= ENABLE_PROCESSED_INPUT;
	else
		m &= ~ENABLE_PROCESSED_INPUT;
	if (!SetConsoleMode(con_in, m)) {
		/* a console too old for virtual-terminal input: raw without it,
		 * and the fallback below translates the keys itself */
		m &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
		SetConsoleMode(con_in, m);
	}
}

/* the bytes decoded from key events and not yet read */
static unsigned char kq[1024];
static unsigned kq_h, kq_n;

static void kq_push(unsigned char c)
{
	if (kq_n < sizeof kq)
		kq[(kq_h + kq_n++) % sizeof kq] = c;
}

static int kq_pop(void)
{
	int c;

	if (!kq_n)
		return -1;
	c = kq[kq_h];
	kq_h = (kq_h + 1) % sizeof kq;
	kq_n--;
	return c;
}

static void kq_str(const char *s)
{
	while (*s)
		kq_push((unsigned char)*s++);
}

static void push_utf8(unsigned cp)
{
	if (cp < 0x80)
		kq_push((unsigned char)cp);
	else if (cp < 0x800) {
		kq_push((unsigned char)(0xC0 | (cp >> 6)));
		kq_push((unsigned char)(0x80 | (cp & 0x3F)));
	} else {
		kq_push((unsigned char)(0xE0 | (cp >> 12)));
		kq_push((unsigned char)(0x80 | ((cp >> 6) & 0x3F)));
		kq_push((unsigned char)(0x80 | (cp & 0x3F)));
	}
}

/* A key the console did not translate (no virtual-terminal input):
 * the xterm sequences, modifiers in the ";m" form the editor decodes. */
static void push_vk(WORD vk, DWORD ctl)
{
	int mod = 1;
	char buf[16];
	const char *tail = NULL;
	int num = 0;

	if (ctl & SHIFT_PRESSED) mod += 1;
	if (ctl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) mod += 2;
	if (ctl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) mod += 4;
	switch (vk) {
	case VK_UP:    tail = "A"; break;
	case VK_DOWN:  tail = "B"; break;
	case VK_RIGHT: tail = "C"; break;
	case VK_LEFT:  tail = "D"; break;
	case VK_HOME:  tail = "H"; break;
	case VK_END:   tail = "F"; break;
	case VK_INSERT: num = 2; break;
	case VK_DELETE: num = 3; break;
	case VK_PRIOR:  num = 5; break;
	case VK_NEXT:   num = 6; break;
	case VK_F1: case VK_F2: case VK_F3: case VK_F4:
		if (mod == 1)
			snprintf(buf, sizeof buf, "\033O%c", 'P' + (vk - VK_F1));
		else
			snprintf(buf, sizeof buf, "\033[1;%d%c", mod, 'P' + (vk - VK_F1));
		kq_str(buf);
		return;
	case VK_F5:  num = 15; break;
	case VK_F6:  num = 17; break;
	case VK_F7:  num = 18; break;
	case VK_F8:  num = 19; break;
	case VK_F9:  num = 20; break;
	case VK_F10: num = 21; break;
	case VK_F11: num = 23; break;
	case VK_F12: num = 24; break;
	default:
		return;
	}
	if (tail) {
		if (mod == 1)
			snprintf(buf, sizeof buf, "\033[%s", tail);
		else
			snprintf(buf, sizeof buf, "\033[1;%d%s", mod, tail);
	} else {
		if (mod == 1)
			snprintf(buf, sizeof buf, "\033[%d~", num);
		else
			snprintf(buf, sizeof buf, "\033[%d;%d~", num, mod);
	}
	kq_str(buf);
}

static void con_decode(const INPUT_RECORD *r)
{
	const KEY_EVENT_RECORD *k;
	int rep;

	if (r->EventType != KEY_EVENT || !r->Event.KeyEvent.bKeyDown)
		return;
	k = &r->Event.KeyEvent;
	rep = k->wRepeatCount ? k->wRepeatCount : 1;
	while (rep-- > 0) {
		if (k->uChar.UnicodeChar)
			push_utf8(k->uChar.UnicodeChar);
		else
			push_vk(k->wVirtualKeyCode, k->dwControlKeyState);
	}
}

/* Fill the queue from the console, waiting up to timeout_ms (-1 =
 * forever) for a key.  Returns the number of bytes queued. */
static int con_take(int timeout_ms)
{
	ULONGLONG t0 = GetTickCount64();

	if (kq_n)
		return (int)kq_n;
	if (!con_check())
		return 0;
	for (;;) {
		DWORD wait, w, cnt = 0, got = 0;
		INPUT_RECORD rec[64];

		if (timeout_ms < 0)
			wait = INFINITE;
		else {
			ULONGLONG used = GetTickCount64() - t0;
			wait = used >= (ULONGLONG)timeout_ms ? 0 : (DWORD)(timeout_ms - used);
		}
		w = WaitForSingleObject(con_in, wait);
		if (w != WAIT_OBJECT_0)
			return 0;
		if (!GetNumberOfConsoleInputEvents(con_in, &cnt))
			return 0;
		while (cnt > 0) {
			DWORD take = cnt > 64 ? 64 : cnt;
			DWORD i;
			if (!ReadConsoleInputW(con_in, rec, take, &got) || !got)
				break;
			for (i = 0; i < got; i++)
				con_decode(&rec[i]);
			cnt -= got;
		}
		if (kq_n)
			return (int)kq_n;
		/* only mouse, focus or resize records: keep waiting */
		if (wait == 0)
			return 0;
	}
}

int pc3w_read(int fd, void *buf, size_t n)
{
	if (fd == 0 && con_check()) {
		unsigned char *b = buf;
		size_t got = 0;
		int timeout;

		if (cur.c_lflag & ICANON)
			return _read(0, buf, (unsigned)n);
		if (fd0_nonblock)
			timeout = 0;
		else if (cur.c_cc[VMIN] == 0)
			timeout = cur.c_cc[VTIME] * 100;
		else
			timeout = cur.c_cc[VTIME] ? cur.c_cc[VTIME] * 100 : -1;
		while (got < n) {
			int c;
			if (!con_take(got ? 0 : timeout))
				break;
			c = kq_pop();
			if (c == '\r' && (cur.c_iflag & ICRNL))
				c = '\n';
			b[got++] = (unsigned char)c;
		}
		if (got == 0 && fd0_nonblock) {
			errno = EAGAIN;
			return -1;
		}
		return (int)got;
	}
	return _read(fd, buf, (unsigned)n);
}

int tcgetattr(int fd, struct termios *t)
{
	if (fd != 0 || !con_check()) {
		errno = ENOTTY;
		return -1;
	}
	*t = cur;
	return 0;
}

int tcsetattr(int fd, int action, const struct termios *t)
{
	(void)action;
	if (fd != 0 || !con_check()) {
		errno = ENOTTY;
		return -1;
	}
	cur = *t;
	con_apply();
	con_out_init();
	return 0;
}

int pc3w_con_size(int *rows, int *cols)
{
	CONSOLE_SCREEN_BUFFER_INFO ci;
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	int ok = 0;

	if (h != INVALID_HANDLE_VALUE && h != NULL && GetConsoleScreenBufferInfo(h, &ci))
		ok = 1;
	if (!ok) {
		/* stdout is a file or a pipe: the console itself, if the
		 * process has one */
		h = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (h != INVALID_HANDLE_VALUE) {
			ok = GetConsoleScreenBufferInfo(h, &ci) ? 1 : 0;
			CloseHandle(h);
		}
	}
	if (!ok)
		return -1;
	*rows = ci.srWindow.Bottom - ci.srWindow.Top + 1;
	*cols = ci.srWindow.Right - ci.srWindow.Left + 1;
	return 0;
}

int pc3w_fcntl(int fd, int cmd, long arg)
{
	switch (cmd) {
	case PC3W_F_GETFL:
		if (pc3w_is_sock(fd))
			return pc3w_is_nonblock(fd) ? PC3W_O_NONBLOCK : 0;
		if (fd == 0)
			return fd0_nonblock ? PC3W_O_NONBLOCK : 0;
		return 0;
	case PC3W_F_SETFL:
		if (pc3w_is_sock(fd))
			return pc3w_nonblock(fd, (arg & PC3W_O_NONBLOCK) != 0);
		if (fd == 0)
			fd0_nonblock = (arg & PC3W_O_NONBLOCK) != 0;
		return 0;
	case PC3W_F_GETFD:
	case PC3W_F_SETFD:
		return 0;
	}
	errno = EINVAL;
	return -1;
}

int ioctl(int fd, int req, ...)
{
	va_list ap;
	void *arg;

	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);
	return pc3w_ioctl(fd, (unsigned long)(unsigned)req, arg);
}

int pc3w_ioctl(int fd, unsigned long req, void *arg)
{
	if (req == PC3W_TIOCGWINSZ && fd <= 2 && arg) {
		int r, c;
		unsigned short *ws = arg;	/* struct winsize: row, col, x, y */
		if (pc3w_con_size(&r, &c) < 0) {
			errno = ENOTTY;
			return -1;
		}
		ws[0] = (unsigned short)r;
		ws[1] = (unsigned short)c;
		ws[2] = ws[3] = 0;
		return 0;
	}
	errno = ENOTTY;
	return -1;
}

/* ---- poll ------------------------------------------------------------------- */

/* readiness of descriptor 0 when it is not a console */
static int fd0_ready(int *hup)
{
	HANDLE h = (HANDLE)_get_osfhandle(0);
	DWORD avail = 0;

	*hup = 0;
	if (h == INVALID_HANDLE_VALUE) {
		*hup = 1;
		return 0;
	}
	if (GetFileType(h) == FILE_TYPE_PIPE) {
		if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
			*hup = 1;
			return 0;
		}
		return avail > 0;
	}
	return 1;			/* a file answers a read at once */
}

int pc3w_poll(struct pollfd *p, unsigned n, int timeout_ms)
{
	WSAPOLLFD wp[64];
	int idx[64];
	unsigned i, ns, has0 = 0;
	ULONGLONG t0 = GetTickCount64();

	wsa_init();
	if (n > 64)
		n = 64;
	for (i = 0; i < n; i++) {
		p[i].revents = 0;
		if ((int)p[i].fd == 0)
			has0 = 1;
	}
	for (;;) {
		int ready = 0, left, r;

		if (timeout_ms < 0)
			left = -1;
		else {
			ULONGLONG used = GetTickCount64() - t0;
			left = used >= (ULONGLONG)timeout_ms ? 0 : (int)(timeout_ms - used);
		}
		ns = 0;
		for (i = 0; i < n; i++) {
			int fd = (int)p[i].fd;
			p[i].revents = 0;
			if (fd == 0) {
				int hup = 0;
				if (con_check()) {
					if (con_take(0))
						p[i].revents = POLLIN;
				} else if (fd0_ready(&hup))
					p[i].revents = POLLIN;
				else if (hup)
					p[i].revents = POLLHUP;
			} else if (pc3w_is_sock(fd)) {
				wp[ns].fd = S(fd);
				wp[ns].events = p[i].events;
				wp[ns].revents = 0;
				idx[ns] = (int)i;
				ns++;
			} else if (fd < 0)
				;
			else
				p[i].revents = POLLIN;	/* a file: always ready */
		}
		/* the sockets, waiting only when nothing else is ready and
		 * no descriptor 0 has to be watched alongside */
		if (ns) {
			int wait = 0;
			for (i = 0; i < n; i++)
				if (p[i].revents)
					wait = -2;	/* something is ready already */
			if (wait == 0 && !has0)
				wait = left;
			r = WSAPoll(wp, ns, wait == -2 ? 0 : wait);
			if (r == SOCKET_ERROR)
				return (int)fail();
			for (i = 0; i < ns; i++)
				p[idx[i]].revents = wp[i].revents;
		}
		for (i = 0; i < n; i++)
			if (p[i].revents)
				ready++;
		if (ready || left == 0)
			return ready;
		if (!has0) {
			if (ns == 0) {
				Sleep(left < 0 ? INFINITE : (DWORD)left);
				return 0;
			}
			continue;	/* WSAPoll timed out: the loop's left is 0 now */
		}
		/* descriptor 0 and, maybe, sockets: wait for whichever first */
		if (con_check()) {
			HANDLE hs[65];
			WSAEVENT ev[64];
			DWORD nh = 0, w;
			hs[nh++] = con_in;
			for (i = 0; i < ns; i++) {
				long want = FD_READ | FD_ACCEPT | FD_CLOSE | FD_CONNECT;
				if (p[idx[i]].events & POLLOUT)
					want |= FD_WRITE;
				ev[i] = WSACreateEvent();
				WSAEventSelect(wp[i].fd, ev[i], want);
				hs[nh++] = ev[i];
			}
			w = WaitForMultipleObjects(nh, hs, FALSE, left < 0 ? INFINITE : (DWORD)left);
			for (i = 0; i < ns; i++) {
				u_long z = 0;
				WSAEventSelect(wp[i].fd, NULL, 0);
				WSACloseEvent(ev[i]);
				if (!sock_nb[(int)p[idx[i]].fd - PC3W_SOCK_BASE])
					ioctlsocket(wp[i].fd, FIONBIO, &z);
			}
			if (w == WAIT_TIMEOUT)
				return 0;
			/* something moved: the loop above finds out what */
		} else {
			/* a pipe: look again shortly */
			Sleep(left < 0 || left > 10 ? 10 : (DWORD)left);
		}
	}
}

/* ---- processes -------------------------------------------------------------- */

static struct { int pid; HANDLE h; } kids[64];

static void kid_add(int pid, HANDLE h)
{
	int i;

	for (i = 0; i < 64; i++)
		if (!kids[i].h) {
			kids[i].pid = pid;
			kids[i].h = h;
			return;
		}
	CloseHandle(h);
}

static HANDLE kid_find(int pid, int take)
{
	int i;

	for (i = 0; i < 64; i++)
		if (kids[i].h && kids[i].pid == pid) {
			HANDLE h = kids[i].h;
			if (take)
				kids[i].h = NULL;
			return h;
		}
	return NULL;
}

/* Windows command-line quoting, as CommandLineToArgvW undoes it. */
static size_t quoted_len(const char *a)
{
	size_t n = 2, bs = 0;
	const char *p;

	for (p = a; *p; p++) {
		if (*p == '\\')
			bs++;
		else if (*p == '"') {
			n += bs * 2 + 2;
			bs = 0;
		} else {
			n += bs + 1;
			bs = 0;
		}
	}
	return n + bs * 2;
}

static char *quote_into(char *o, const char *a)
{
	size_t bs = 0;
	const char *p;

	if (*a && !strpbrk(a, " \t\"")) {
		strcpy(o, a);
		return o + strlen(a);
	}
	*o++ = '"';
	for (p = a; *p; p++) {
		if (*p == '\\')
			bs++;
		else if (*p == '"') {
			size_t k;
			for (k = 0; k < bs * 2 + 1; k++)
				*o++ = '\\';
			*o++ = '"';
			bs = 0;
		} else {
			while (bs) { *o++ = '\\'; bs--; }
			*o++ = *p;
		}
	}
	while (bs) { *o++ = '\\'; *o++ = '\\'; bs--; }
	*o++ = '"';
	*o = 0;
	return o;
}

static char *cmdline(char *const argv[])
{
	size_t n = 1;
	int i;
	char *s, *o;

	for (i = 0; argv[i]; i++)
		n += quoted_len(argv[i]) + 1;
	s = malloc(n);
	if (!s)
		return NULL;
	o = s;
	for (i = 0; argv[i]; i++) {
		if (i)
			*o++ = ' ';
		o = quote_into(o, argv[i]);
	}
	*o = 0;
	return s;
}

/* The program to run: a path as given (with .exe if that is what is
 * there), else the application's directory, the current directory and
 * the PATH, which is SearchPath's order. */
static int resolve(const char *file, char *out, size_t n)
{
	if (strchr(file, '/') || strchr(file, '\\') || (file[0] && file[1] == ':')) {
		snprintf(out, n, "%s", file);
		if (_access(out, 0) == 0)
			return 0;
		snprintf(out, n, "%s.exe", file);
		if (_access(out, 0) == 0)
			return 0;
		errno = ENOENT;
		return -1;
	}
	if (SearchPathA(NULL, file, ".exe", (DWORD)n, out, NULL) > 0)
		return 0;
	if (SearchPathA(NULL, file, NULL, (DWORD)n, out, NULL) > 0)
		return 0;
	errno = ENOENT;
	return -1;
}

static HANDLE inheritable(HANDLE h)
{
	HANDLE d;

	if (h == INVALID_HANDLE_VALUE || h == NULL)
		return NULL;
	if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &d, 0, TRUE,
			     DUPLICATE_SAME_ACCESS))
		return NULL;
	return d;
}

int pc3w_spawn(const char *file, char *const argv[], int fd_in, int fd_out,
	       int detached, const char *logpath)
{
	char path[4096];
	char *cmd;
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	HANDLE hin = NULL, hout = NULL, herr = NULL;
	DWORD flags = 0;
	BOOL ok;

	if (resolve(file, path, sizeof path) < 0)
		return -1;
	cmd = cmdline(argv);
	if (!cmd) {
		errno = ENOMEM;
		return -1;
	}
	memset(&si, 0, sizeof si);
	si.cb = sizeof si;
	si.dwFlags = STARTF_USESTDHANDLES;
	if (detached) {
		SECURITY_ATTRIBUTES sa;
		sa.nLength = sizeof sa;
		sa.lpSecurityDescriptor = NULL;
		sa.bInheritHandle = TRUE;
		hin = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
				  &sa, OPEN_EXISTING, 0, NULL);
		if (logpath)
			hout = CreateFileA(logpath, FILE_APPEND_DATA,
					   FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
					   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (!hout || hout == INVALID_HANDLE_VALUE)
			hout = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
					   &sa, OPEN_EXISTING, 0, NULL);
		herr = inheritable(hout);
		flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
	} else {
		hin = inheritable(fd_in >= 0 ? (HANDLE)_get_osfhandle(fd_in)
					     : GetStdHandle(STD_INPUT_HANDLE));
		hout = inheritable(fd_out >= 0 ? (HANDLE)_get_osfhandle(fd_out)
					       : GetStdHandle(STD_OUTPUT_HANDLE));
		herr = inheritable(GetStdHandle(STD_ERROR_HANDLE));
	}
	si.hStdInput = hin;
	si.hStdOutput = hout;
	si.hStdError = herr;
	ok = CreateProcessA(path, cmd, NULL, NULL, TRUE, flags, NULL, NULL, &si, &pi);
	if (hin) CloseHandle(hin);
	if (hout) CloseHandle(hout);
	if (herr) CloseHandle(herr);
	free(cmd);
	if (!ok) {
		errno = GetLastError() == ERROR_FILE_NOT_FOUND ? ENOENT : EACCES;
		return -1;
	}
	CloseHandle(pi.hThread);
	if (detached)
		CloseHandle(pi.hProcess);
	else
		kid_add((int)pi.dwProcessId, pi.hProcess);
	return (int)pi.dwProcessId;
}

int pc3w_wait(int pid, int *status, int nohang)
{
	HANDLE h = kid_find(pid, 0);
	int own = 0;
	DWORD w, code = 0;

	if (!h) {
		h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
		if (!h) {
			errno = ECHILD;
			return -1;
		}
		own = 1;
	}
	w = WaitForSingleObject(h, nohang ? 0 : INFINITE);
	if (w == WAIT_TIMEOUT) {
		if (own)
			CloseHandle(h);
		return 0;
	}
	GetExitCodeProcess(h, &code);
	CloseHandle(h);
	if (!own)
		kid_find(pid, 1);
	if (status)
		*status = (int)((code & 0xff) << 8);
	return pid;
}

int pc3w_kill(int pid, int sig)
{
	HANDLE h = kid_find(pid, 0);
	int own = 0, r = 0;
	DWORD code = 0;

	if (!h) {
		h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
		if (!h) {
			errno = ESRCH;
			return -1;
		}
		own = 1;
	}
	if (sig == 0) {
		if (!GetExitCodeProcess(h, &code) || code != STILL_ACTIVE) {
			errno = ESRCH;
			r = -1;
		}
	} else if (!TerminateProcess(h, 128 + (DWORD)sig)) {
		errno = EPERM;
		r = -1;
	}
	if (own)
		CloseHandle(h);
	return r;
}

int pc3w_exec(const char *file, char *const argv[])
{
	int pid, status = 0;

	fflush(NULL);
	pid = pc3w_spawn(file, argv, -1, -1, 0, NULL);
	if (pid < 0)
		return -1;
	pc3w_wait(pid, &status, 0);
	exit((status >> 8) & 0xff);
}

int pc3w_getppid(void)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 pe;
	DWORD me = GetCurrentProcessId();
	int ppid = 0;

	if (snap == INVALID_HANDLE_VALUE)
		return 0;
	pe.dwSize = sizeof pe;
	if (Process32First(snap, &pe)) {
		do {
			if (pe.th32ProcessID == me) {
				ppid = (int)pe.th32ParentProcessID;
				break;
			}
		} while (Process32Next(snap, &pe));
	}
	CloseHandle(snap);
	return ppid;
}

/* ---- memory below 4G ---------------------------------------------------- */

void *pc3w_map_low(void *hint, size_t n)
{
	void *p;
	uintptr_t a;

	n = (n + 0xFFFF) & ~(size_t)0xFFFF;
	if (hint) {
		p = VirtualAlloc(hint, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (p)
			return p;
	}
	for (a = 0x30000000; a + n <= 0xF0000000; a += 0x1000000) {
		p = VirtualAlloc((void *)a, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (p)
			return p;
	}
	errno = ENOMEM;
	return NULL;
}

int pc3w_unmap(void *p, size_t n)
{
	(void)n;
	return VirtualFree(p, 0, MEM_RELEASE) ? 0 : -1;
}

/* ---- a FIFO, as a named pipe ---------------------------------------------------- */

#define FIFO_BASE 0x8000
#define FIFO_MAX  8
static struct { HANDLE h; int server; } fifos[FIFO_MAX];

static void fifo_name(const char *path, char *out, size_t n)
{
	const char *b = strrchr(path, '/');
	const char *b2 = strrchr(path, '\\');

	if (b2 > b)
		b = b2;
	b = b ? b + 1 : path;
	while (*b == '.')
		b++;
	snprintf(out, n, "\\\\.\\pipe\\pc3-%s", b);
}

static int fifo_alloc(HANDLE h, int server)
{
	int i;

	for (i = 0; i < FIFO_MAX; i++)
		if (!fifos[i].h) {
			fifos[i].h = h;
			fifos[i].server = server;
			return FIFO_BASE + i;
		}
	CloseHandle(h);
	errno = EMFILE;
	return -1;
}

static int fifo_slot(int fd)
{
	int i = fd - FIFO_BASE;

	if (i < 0 || i >= FIFO_MAX || !fifos[i].h) {
		errno = EBADF;
		return -1;
	}
	return i;
}

int pc3w_fifo_server(const char *path)
{
	char name[256];
	HANDLE h;

	fifo_name(path, name, sizeof name);
	/* message mode: one client write is one record; PIPE_NOWAIT makes
	 * the reads and the connection wait for nobody */
	h = CreateNamedPipeA(name, PIPE_ACCESS_INBOUND,
			     PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT,
			     1, 4096, 4096, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		errno = GetLastError() == ERROR_PIPE_BUSY ? EEXIST : EACCES;
		return -1;
	}
	ConnectNamedPipe(h, NULL);	/* listening; a client may come at any time */
	return fifo_alloc(h, 1);
}

int pc3w_fifo_client(const char *path)
{
	char name[256];
	HANDLE h;
	int tries;

	fifo_name(path, name, sizeof name);
	for (tries = 0; tries < 20; tries++) {
		h = CreateFileA(name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (h != INVALID_HANDLE_VALUE)
			return fifo_alloc(h, 0);
		if (GetLastError() != ERROR_PIPE_BUSY)
			break;
		/* the daemon has not yet noticed the last client leave:
		 * it looks every pass, so this is a few milliseconds */
		Sleep(5);
	}
	errno = GetLastError() == ERROR_PIPE_BUSY ? EAGAIN : ENXIO;
	return -1;
}

int pc3w_fifo_read(int fd, void *buf, size_t n)
{
	int i = fifo_slot(fd);
	DWORD got = 0, e;

	if (i < 0)
		return -1;
	if (ReadFile(fifos[i].h, buf, (DWORD)n, &got, NULL))
		return (int)got;
	e = GetLastError();
	if (fifos[i].server && (e == ERROR_BROKEN_PIPE || e == ERROR_NO_DATA)) {
		/* the client has written and gone: listen for the next one */
		DisconnectNamedPipe(fifos[i].h);
		ConnectNamedPipe(fifos[i].h, NULL);
	}
	errno = EAGAIN;		/* nothing yet, nobody yet, or somebody just left */
	return -1;
}

int pc3w_fifo_write(int fd, const void *buf, size_t n)
{
	int i = fifo_slot(fd);
	DWORD put = 0;

	if (i < 0)
		return -1;
	if (!WriteFile(fifos[i].h, buf, (DWORD)n, &put, NULL)) {
		errno = EPIPE;
		return -1;
	}
	FlushFileBuffers(fifos[i].h);
	return (int)put;
}

int pc3w_fifo_close(int fd)
{
	int i = fifo_slot(fd);

	if (i < 0)
		return -1;
	if (fifos[i].server)
		DisconnectNamedPipe(fifos[i].h);
	CloseHandle(fifos[i].h);
	fifos[i].h = NULL;
	return 0;
}

/* ---- odds and ends ------------------------------------------------------------ */

/* The BSD pair the Fuzix libc and glibc both carry and MinGW does not. */
size_t strlcpy(char *dst, const char *src, size_t n)
{
	size_t len = strlen(src);

	if (n) {
		size_t k = len >= n ? n - 1 : len;
		memcpy(dst, src, k);
		dst[k] = 0;
	}
	return len;
}

size_t strlcat(char *dst, const char *src, size_t n)
{
	size_t dl = strnlen(dst, n);

	if (dl == n)
		return n + strlen(src);
	return dl + strlcpy(dst + dl, src, n - dl);
}

const char *pc3w_hostpath(const char *path)
{
	static char out[4200];

	if (strncmp(path, "/tmp/", 5) == 0) {
		snprintf(out, sizeof out, "%s/%s", pc3w_tmpdir(), path + 5);
		return out;
	}
	return path;
}

int pc3w_exe_dir(char *buf, size_t n)
{
	DWORD k = GetModuleFileNameA(NULL, buf, (DWORD)n);
	char *p;

	if (k == 0 || k >= n)
		return -1;
	for (p = buf; *p; p++)
		if (*p == '\\')
			*p = '/';
	p = strrchr(buf, '/');
	if (!p)
		return -1;
	*p = 0;
	return 0;
}

int pc3w_random(void *buf, size_t n)
{
	return BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)n,
			       BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
}

static void (*alarm_fn)(int);
static unsigned alarm_gen;

static DWORD WINAPI alarm_thread(LPVOID arg)
{
	struct { unsigned gen; unsigned sec; } *a = arg;
	unsigned gen = a->gen, sec = a->sec;

	free(a);
	Sleep(sec * 1000);
	if (gen == alarm_gen && alarm_fn)
		alarm_fn(14);		/* SIGALRM */
	return 0;
}

void pc3w_alarm(unsigned sec, void (*fn)(int))
{
	struct { unsigned gen; unsigned sec; } *a;
	HANDLE t;

	alarm_gen++;
	alarm_fn = fn;
	if (sec == 0)
		return;
	a = malloc(sizeof *a);
	if (!a)
		return;
	a->gen = alarm_gen;
	a->sec = sec;
	t = CreateThread(NULL, 0, alarm_thread, a, 0, NULL);
	if (t)
		CloseHandle(t);
	else
		free(a);
}

const char *pc3w_tmpdir(void)
{
	static char dir[MAX_PATH + 1];

	if (!dir[0]) {
		DWORD k = GetTempPathA(sizeof dir, dir);
		if (k == 0 || k >= sizeof dir)
			strcpy(dir, ".");
		else if (k > 1 && (dir[k - 1] == '\\' || dir[k - 1] == '/'))
			dir[k - 1] = 0;
	}
	return dir;
}

int pc3w_setenv(const char *name, const char *value, int overwrite)
{
	if (!overwrite && getenv(name))
		return 0;
	return _putenv_s(name, value) ? -1 : 0;
}

int pc3w_unsetenv(const char *name)
{
	return _putenv_s(name, "") ? -1 : 0;
}

/* ---- at start ----------------------------------------------------------------- */

/* Binary I/O whenever a descriptor is not the console: the compiler
 * passes stream binary objects through stdout, and a program's output
 * compared against a file must have the bytes it had on Linux.  The
 * console keeps the runtime's text translation.  ANSI output is
 * enabled so the editor and PRINT's colours show. */
__attribute__((constructor)) static void pc3w_startup(void)
{
	_fmode = _O_BINARY;
	if (!_isatty(0))
		_setmode(0, _O_BINARY);
	if (!_isatty(1))
		_setmode(1, _O_BINARY);
	if (!_isatty(2))
		_setmode(2, _O_BINARY);
	con_out_init();
	cur_init();
}
