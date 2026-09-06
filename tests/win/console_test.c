/*
 * console_test.c - the Windows console shim, driven without a person.
 *
 * Opens a console of its own, puts it into the raw mode the runtime
 * and the editor ask for through termios, feeds it key records the way
 * a keyboard would through WriteConsoleInput, and reads them back
 * through pc3w_read: a letter, Enter (CR turned to NL by ICRNL, as a
 * tty does), an arrow (the shim's own translation, since injected
 * records bypass the console's virtual-terminal translation), F2, and
 * the VMIN/VTIME rules - nothing waiting returns at once, a bare ESC
 * waits a tenth of a second for the rest of a sequence.  Then the
 * window size through ioctl.  Exit status 0 when every answer is right.
 */
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <pc3w.h>

static int fails;

static void expect(const char *what, const char *got, int n, const char *want)
{
	if (n == (int)strlen(want) && memcmp(got, want, (size_t)n) == 0) {
		printf("pass  %s\n", what);
		return;
	}
	printf("FAIL  %s: got %d bytes", what, n);
	for (int i = 0; i < n; i++)
		printf(" %02x", (unsigned char)got[i]);
	printf(", wanted");
	for (const char *p = want; *p; p++)
		printf(" %02x", (unsigned char)*p);
	printf("\n");
	fails++;
}

static void press(HANDLE h, WCHAR ch, WORD vk, DWORD ctl)
{
	INPUT_RECORD r[2];
	DWORD n;

	memset(r, 0, sizeof r);
	r[0].EventType = KEY_EVENT;
	r[0].Event.KeyEvent.bKeyDown = TRUE;
	r[0].Event.KeyEvent.wRepeatCount = 1;
	r[0].Event.KeyEvent.wVirtualKeyCode = vk;
	r[0].Event.KeyEvent.uChar.UnicodeChar = ch;
	r[0].Event.KeyEvent.dwControlKeyState = ctl;
	r[1] = r[0];
	r[1].Event.KeyEvent.bKeyDown = FALSE;
	WriteConsoleInputW(h, r, 2, &n);
}

int main(void)
{
	struct termios t;
	struct winsize ws;
	HANDLE hin;
	char buf[16];
	int n;
	DWORD t0;

	FreeConsole();
	if (!AllocConsole()) {
		printf("no console\n");
		return 2;
	}
	/* the C runtime's descriptor 0 has to be the new console */
	freopen("CONIN$", "r", stdin);
	hin = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
			  NULL, OPEN_EXISTING, 0, NULL);
	SetStdHandle(STD_INPUT_HANDLE, hin);

	if (tcgetattr(0, &t) < 0) {
		printf("FAIL  tcgetattr: descriptor 0 is not the console\n");
		return 1;
	}
	printf("pass  tcgetattr (cooked: ICANON %d ECHO %d)\n",
	       !!(t.c_lflag & ICANON), !!(t.c_lflag & ECHO));

	/* the runtime's INKEY$ mode: raw, return what is there */
	t.c_lflag &= ~(ICANON | ECHO);
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &t);

	n = pc3w_read(0, buf, 1);
	expect("nothing waiting returns at once", buf, n < 0 ? 0 : n, "");

	press(hin, L'a', 'A', 0);
	n = pc3w_read(0, buf, 1);
	expect("a letter", buf, n, "a");

	press(hin, L'\r', VK_RETURN, 0);
	n = pc3w_read(0, buf, 1);
	expect("Enter is NL (ICRNL)", buf, n, "\n");

	press(hin, 0, VK_UP, 0);
	n = pc3w_read(0, buf, 3);
	expect("Up arrow", buf, n, "\033[A");

	press(hin, 0, VK_F2, 0);
	n = pc3w_read(0, buf, 3);
	expect("F2", buf, n, "\033OQ");

	press(hin, 0, VK_DELETE, 0);
	n = pc3w_read(0, buf, 4);
	expect("Delete", buf, n, "\033[3~");

	press(hin, 0, VK_F1, SHIFT_PRESSED);
	n = pc3w_read(0, buf, 6);
	expect("Shift-F1", buf, n, "\033[1;2P");

	press(hin, 3, 'C', LEFT_CTRL_PRESSED);
	n = pc3w_read(0, buf, 1);
	expect("Ctrl-C as a byte with ISIG off in the record path", buf, n, "\003");

	/* the escape-sequence wait: VMIN 0 VTIME 1 blocks a tenth */
	t.c_cc[VTIME] = 1;
	tcsetattr(0, TCSANOW, &t);
	t0 = GetTickCount();
	n = pc3w_read(0, buf, 1);
	printf("%s  VTIME 1 with nothing waiting took %lu ms\n",
	       (n == 0 && GetTickCount() - t0 >= 80) ? "pass" : "FAIL", (unsigned long)(GetTickCount() - t0));
	if (!(n == 0 && GetTickCount() - t0 >= 80))
		fails++;

	/* back to cooked, and the size */
	t.c_lflag |= ICANON | ECHO;
	tcsetattr(0, TCSANOW, &t);
	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0)
		printf("pass  TIOCGWINSZ %dx%d\n", ws.ws_col, ws.ws_row);
	else {
		printf("FAIL  TIOCGWINSZ\n");
		fails++;
	}
	printf("%s\n", fails ? "CONSOLE SHIM: FAILURES" : "CONSOLE SHIM: all pass");
	fflush(stdout);
	return fails ? 1 : 0;
}
