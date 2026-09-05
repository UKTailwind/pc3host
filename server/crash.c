/*
 * crash.c - say where it went wrong.
 *
 * A server that dies on a machine its author cannot see has to leave a
 * note.  On a fatal signal this prints the signal, the version, and the
 * return addresses of the stack to stderr, then dies as it would have
 * died - so the desktop's crash reporter still sees a crash, and the
 * addresses can be turned into lines with addr2line against the same
 * binary, which is not stripped and, when static, not position
 * independent, so the addresses printed are the ones in the file.
 *
 *     addr2line -e /opt/pc3/bin/pc3d 0x4a1b2c 0x4a0f10 ...
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <execinfo.h>
#include "pc3d.h"

static const char *signame(int sig)
{
	switch (sig) {
	case SIGSEGV: return "SIGSEGV (bad memory access)";
	case SIGBUS:  return "SIGBUS (bus error)";
	case SIGILL:  return "SIGILL (illegal instruction)";
	case SIGFPE:  return "SIGFPE (arithmetic)";
	case SIGABRT: return "SIGABRT (abort)";
	default:      return "signal";
	}
}

static void on_crash(int sig)
{
	void *frames[48];
	int n, i;
	char line[64];

	/* write(2) only: printf is not safe here, and neither is malloc */
	write(2, "\npc3d " PC3D_VERSION ": ", sizeof "\npc3d " PC3D_VERSION ": " - 1);
	write(2, signame(sig), strlen(signame(sig)));
	write(2, "\nbacktrace (addr2line -e pc3d ...):\n", 37);
	n = backtrace(frames, 48);
	for (i = 0; i < n; i++) {
		int k = snprintf(line, sizeof line, "  %p\n", frames[i]);
		write(2, line, (size_t)k);
	}
	signal(sig, SIG_DFL);
	raise(sig);
}

void crash_handlers(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_crash;
	sa.sa_flags = SA_RESETHAND;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);
}
