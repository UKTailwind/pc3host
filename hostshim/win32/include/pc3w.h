/*
 * pc3w.h - what the hosted PC3 programs need of POSIX, on Windows.
 *
 * The tools, the runtime and the server were written against a small
 * POSIX surface: sockets and poll, a raw terminal with VMIN/VTIME, fork
 * and exec for the programs a program runs, mmap below 4G, and a few
 * odds and ends.  This directory gives a MinGW build that surface.  The
 * headers under include/ stand in for the ones MinGW lacks (termios.h,
 * poll.h, sys/socket.h and the rest) and route the calls here, so the
 * sources compile as they are wherever the semantics carry over, and
 * carry an #ifdef _WIN32 only where they do not - the spawn sites,
 * where there is no fork to call.
 *
 * Descriptors.  A Winsock SOCKET is not a C runtime descriptor, and a
 * program stores both as an int.  Sockets are therefore pseudo-
 * descriptors from PC3W_SOCK_BASE up, translated by the calls below;
 * pc3w_is_sock() tells the two apart wherever a read, write or close
 * could meet either.  The console is descriptor 0 and is read through
 * pc3w_read(), which honours the termios the program set on it.
 */
#ifndef PC3W_H
#define PC3W_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct iovec;
struct pollfd;

/* ---- sockets --------------------------------------------------------- */
#define PC3W_SOCK_BASE 0x4000
#define PC3W_SOCK_MAX  256

int pc3w_is_sock(int fd);
uintptr_t pc3w_sock(int fd);		/* the SOCKET behind it (INVALID_SOCKET if none) */
int pc3w_socket(int domain, int type, int protocol);
int pc3w_connect(int fd, const void *sa, int len);
int pc3w_bind(int fd, const void *sa, int len);
int pc3w_listen(int fd, int backlog);
int pc3w_accept(int fd, void *sa, int *len);
long pc3w_send(int fd, const void *buf, size_t n, int flags);
long pc3w_recv(int fd, void *buf, size_t n, int flags);
long pc3w_sendto(int fd, const void *buf, size_t n, int flags, const void *sa, int len);
long pc3w_recvfrom(int fd, void *buf, size_t n, int flags, void *sa, int *len);
int pc3w_setsockopt(int fd, int level, int opt, const void *v, int len);
int pc3w_shutdown(int fd, int how);
int pc3w_sock_close(int fd);
int pc3w_nonblock(int fd, int on);	/* FIONBIO, remembered */
int pc3w_is_nonblock(int fd);
int pc3w_socketpair(int fds[2]);	/* a connected loopback pair */
long pc3w_writev(int fd, const struct iovec *iov, int n);
void pc3w_seterrno(void);		/* errno from WSAGetLastError() */

/* ---- poll: pseudo-descriptors, the console, pipes ----------------------- */
int pc3w_poll(struct pollfd *p, unsigned n, int timeout_ms);

/* ---- the console ----------------------------------------------------------- */
int pc3w_con_isatty(int fd);
int pc3w_read(int fd, void *buf, size_t n);	/* read(), termios honoured on 0 */
int pc3w_con_size(int *rows, int *cols);
int pc3w_fcntl(int fd, int cmd, long arg);
int pc3w_ioctl(int fd, unsigned long req, void *arg);

/* ---- processes ------------------------------------------------------------ */
/* Start file with argv; fd_in/fd_out (-1 = inherit) become its stdin
 * and stdout.  detached: no console, no wait, output appended to
 * logpath.  Returns the process id, or -1 with errno set. */
int pc3w_spawn(const char *file, char *const argv[], int fd_in, int fd_out,
	       int detached, const char *logpath);
int pc3w_wait(int pid, int *status, int nohang);	/* pid, 0 = still running, -1 */
int pc3w_kill(int pid, int sig);			/* 0 = exists?; else terminate */
int pc3w_exec(const char *file, char *const argv[]);	/* run, wait, exit with its status */
int pc3w_getppid(void);

/* ---- memory below 4G ------------------------------------------------------ */
void *pc3w_map_low(void *hint, size_t n);
int pc3w_unmap(void *p, size_t n);

/* ---- a FIFO, as a named pipe ------------------------------------------------
 * The player daemons take PLAY commands through a FIFO (mmb_playctl.h):
 * the daemon creates it and reads it without blocking each pass, a
 * program opens it for writing, writes one record, closes.  The same
 * five steps over a Windows named pipe; the name is the FIFO path's
 * basename.  A client open with nobody serving fails with ENXIO, as a
 * FIFO's O_NDELAY open does. */
int pc3w_fifo_server(const char *path);		/* create and open: a pseudo-fd */
int pc3w_fifo_client(const char *path);		/* open for one write */
int pc3w_fifo_read(int fd, void *buf, size_t n);	/* -1/EAGAIN when empty */
int pc3w_fifo_write(int fd, const void *buf, size_t n);
int pc3w_fifo_close(int fd);

/* ---- odds and ends ------------------------------------------------------- */
/* "/tmp/x" is where a program from the board expects a temporary file;
 * on Windows that is the temporary directory.  Any other path is
 * returned as it is.  One static buffer: use it at once. */
const char *pc3w_hostpath(const char *path);
int pc3w_exe_dir(char *buf, size_t n);		/* forward slashes, no trailing one */
int pc3w_random(void *buf, size_t n);
void pc3w_alarm(unsigned sec, void (*fn)(int));	/* 0 cancels */
const char *pc3w_tmpdir(void);
int pc3w_setenv(const char *name, const char *value, int overwrite);
int pc3w_unsetenv(const char *name);

#endif /* PC3W_H */
