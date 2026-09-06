/* unistd.h - MinGW's, plus what the sources call that it lacks. */
#ifndef PC3W_UNISTD_H
#define PC3W_UNISTD_H
#include_next <unistd.h>
#include <io.h>
#include <fcntl.h>
#include <pc3w.h>
static inline int getppid(void) { return pc3w_getppid(); }
static inline int getuid(void) { return 1000; }
static inline int geteuid(void) { return 1000; }
static inline void sync(void) { }
static inline int fsync(int fd) { return _commit(fd); }
static inline int pipe(int fds[2]) { return _pipe(fds, 65536, _O_BINARY | _O_NOINHERIT); }
#define setenv(n, v, o) pc3w_setenv((n), (v), (o))
#define unsetenv(n) pc3w_unsetenv(n)
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif
#endif
