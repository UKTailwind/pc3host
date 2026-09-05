/*
 * xkeys - type into the PC3 window through the X server.
 *
 *   xkeys [-w title] ITEM...      ITEM: name       tap a key (X keysym name:
 *                                       a, Up, Return, F1, Shift_L ...)
 *                                       +name      press and hold
 *                                       -name      release
 *                                       NNNms      pause
 *
 * pc3key presses keys INSIDE the server, bypassing the window; this
 * presses them at the X server with the XTEST extension, so the whole
 * path a person's keyboard takes - X event, MiniFB, the report builder,
 * the decoder, the key channel - is the path exercised.  The window
 * whose title begins with "Pico Computer 3" (or -w) is given the focus
 * first.  A development tool; not installed.
 *
 *   gcc -o xkeys xkeys.c -lX11 -lXtst
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

static Window find_window(Display *d, Window w, const char *title)
{
	Window root, parent, *kids, found = 0;
	unsigned n, i;
	char *name = NULL;

	if (XFetchName(d, w, &name) && name) {
		int hit = strncmp(name, title, strlen(title)) == 0;
		XFree(name);
		if (hit)
			return w;
	}
	if (!XQueryTree(d, w, &root, &parent, &kids, &n))
		return 0;
	for (i = 0; i < n && !found; i++)
		found = find_window(d, kids[i], title);
	if (kids)
		XFree(kids);
	return found;
}

static void key(Display *d, const char *name, int down, int up)
{
	KeySym ks = XStringToKeysym(name);
	KeyCode kc;

	if (ks == NoSymbol) {
		fprintf(stderr, "xkeys: no keysym %s\n", name);
		exit(1);
	}
	kc = XKeysymToKeycode(d, ks);
	if (!kc) {
		fprintf(stderr, "xkeys: no keycode for %s\n", name);
		exit(1);
	}
	if (down) {
		XTestFakeKeyEvent(d, kc, True, CurrentTime);
		XFlush(d);
		usleep(20000);
	}
	if (up) {
		XTestFakeKeyEvent(d, kc, False, CurrentTime);
		XFlush(d);
		usleep(20000);
	}
}

int main(int argc, char **argv)
{
	Display *d = XOpenDisplay(NULL);
	const char *title = "Pico Computer 3";
	Window w;
	int i = 1, ev, err, maj, min;

	if (!d) {
		fprintf(stderr, "xkeys: no display\n");
		return 1;
	}
	if (!XTestQueryExtension(d, &ev, &err, &maj, &min)) {
		fprintf(stderr, "xkeys: no XTEST extension\n");
		return 1;
	}
	if (argc > 2 && !strcmp(argv[1], "-w")) {
		title = argv[2];
		i = 3;
	}
	w = find_window(d, DefaultRootWindow(d), title);
	if (!w) {
		fprintf(stderr, "xkeys: no window titled %s...\n", title);
		return 1;
	}
	XRaiseWindow(d, w);
	XSetInputFocus(d, w, RevertToParent, CurrentTime);
	XSync(d, False);
	usleep(200000);

	for (; i < argc; i++) {
		const char *a = argv[i];
		size_t l = strlen(a);

		if (l > 2 && !strcmp(a + l - 2, "ms") && a[0] >= '0' && a[0] <= '9')
			usleep((useconds_t)atoi(a) * 1000);
		else if (a[0] == '+')
			key(d, a + 1, 1, 0);
		else if (a[0] == '-')
			key(d, a + 1, 0, 1);
		else
			key(d, a, 1, 1);
	}
	XSync(d, False);
	XCloseDisplay(d);
	return 0;
}
