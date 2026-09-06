/* signal.h - MinGW's, plus the names the sources use.  signal() on a
 * number MinGW does not deliver returns SIG_ERR and nothing else
 * happens, which is what the sources already allow for. */
#ifndef PC3W_SIGNAL_H
#define PC3W_SIGNAL_H
#include_next <signal.h>
#include <pc3w.h>
#ifndef SIGHUP
#define SIGHUP  1
#endif
#ifndef SIGQUIT
#define SIGQUIT 3
#endif
#ifndef SIGBUS
#define SIGBUS  7
#endif
#ifndef SIGKILL
#define SIGKILL 9
#endif
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIGALRM
#define SIGALRM 14
#endif
#ifndef SIGCHLD
#define SIGCHLD 17
#endif
typedef int sigset_t;
struct sigaction {
	void (*sa_handler)(int);
	sigset_t sa_mask;
	int sa_flags;
};
static inline int sigemptyset(sigset_t *s) { *s = 0; return 0; }
static inline int sigaction(int sig, const struct sigaction *act, struct sigaction *old)
{
	void (*prev)(int);
	if (act)
		prev = signal(sig, act->sa_handler);
	else {
		prev = signal(sig, SIG_DFL);
		if (prev != SIG_ERR)
			signal(sig, prev);
	}
	if (old) {
		old->sa_handler = prev == SIG_ERR ? SIG_DFL : prev;
		old->sa_mask = 0;
		old->sa_flags = 0;
	}
	return prev == SIG_ERR ? -1 : 0;
}
#define kill(pid, sig) pc3w_kill((int)(pid), (sig))
#endif
