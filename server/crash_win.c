/*
 * crash_win.c - say where it went wrong, on Windows.
 *
 * The same note crash.c leaves on a fatal signal: the exception, the
 * version, and the return addresses of the stack - as offsets from the
 * module's base, since Windows loads the program at a different
 * address each time.  addr2line reads them against the same binary
 * with the image base added:
 *
 *     x86_64-w64-mingw32-addr2line -e pc3d.exe 0x1400a1b2c
 *
 * (the image base of a MinGW executable is 0x140000000; the offsets
 * printed are added to it).
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "pc3d.h"

static const char *excname(DWORD code)
{
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION:      return "access violation (bad memory access)";
	case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
	case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds";
	default:                              return "exception";
	}
}

static LONG WINAPI on_crash(EXCEPTION_POINTERS *ep)
{
	void *frames[48];
	USHORT n, i;
	char line[96];
	HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
	DWORD w;
	uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);

	snprintf(line, sizeof line, "\npc3d " PC3D_VERSION ": %s (0x%08lx)\n",
		 excname(ep->ExceptionRecord->ExceptionCode),
		 (unsigned long)ep->ExceptionRecord->ExceptionCode);
	WriteFile(err, line, (DWORD)strlen(line), &w, NULL);
	WriteFile(err, "backtrace (offsets from the image base):\n", 41, &w, NULL);
	n = CaptureStackBackTrace(0, 48, frames, NULL);
	for (i = 0; i < n; i++) {
		snprintf(line, sizeof line, "  +0x%llx\n",
			 (unsigned long long)((uintptr_t)frames[i] - base));
		WriteFile(err, line, (DWORD)strlen(line), &w, NULL);
	}
	return EXCEPTION_CONTINUE_SEARCH;	/* and die as it would have died */
}

void crash_handlers(void)
{
	SetUnhandledExceptionFilter(on_crash);
}
