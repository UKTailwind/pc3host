/* fcntl.h - MinGW's, plus fcntl() with the non-blocking flag. */
#ifndef PC3W_FCNTL_H
#define PC3W_FCNTL_H
#include_next <fcntl.h>
#include <pc3w.h>
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x100000
#endif
#ifndef O_NDELAY
#define O_NDELAY O_NONBLOCK
#endif
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1
static inline int pc3w_fcntl2(int fd, int cmd) { return pc3w_fcntl(fd, cmd, 0); }
static inline int pc3w_fcntl3(int fd, int cmd, long arg) { return pc3w_fcntl(fd, cmd, arg); }
#define PC3W_FCNTL_PICK(a, b, c, name, ...) name
#define fcntl(...) PC3W_FCNTL_PICK(__VA_ARGS__, pc3w_fcntl3, pc3w_fcntl2, 0)(__VA_ARGS__)
#endif
