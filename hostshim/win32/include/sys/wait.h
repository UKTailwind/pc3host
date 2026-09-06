/* sys/wait.h - waitpid over process handles.  A status is exit<<8, as a
 * POSIX status is for a normal exit; nothing here ever "died on a
 * signal", so WIFSIGNALED is false and WIFEXITED true for any status. */
#ifndef PC3W_SYS_WAIT_H
#define PC3W_SYS_WAIT_H
#include <pc3w.h>
#define WNOHANG 1
#define WIFEXITED(s)   1
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WIFSIGNALED(s) 0
#define WTERMSIG(s)    0
static inline int waitpid(int pid, int *status, int options)
{
	int st = 0, r = pc3w_wait(pid, &st, options & WNOHANG);
	if (status)
		*status = st;
	return r;
}
#endif
