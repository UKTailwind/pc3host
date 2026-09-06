/*
 * xclose - press the close button of the PC3 window, through the X
 * server: a WM_DELETE_WINDOW client message, which is what a window
 * manager sends when the X in the corner is clicked.  For testing the
 * server's exit path.  A development tool; not installed.
 *
 *   xclose [-w title]
 *   gcc -o xclose xclose.c -lX11
 */
#include <stdio.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

static Window find(Display *d, Window w, const char *prefix)
{
	Window root, parent, *kids, found = 0;
	unsigned n, i;
	char *name = NULL;

	if (XFetchName(d, w, &name) && name) {
		int hit = strncmp(name, prefix, strlen(prefix)) == 0;
		XFree(name);
		if (hit)
			return w;
	}
	if (!XQueryTree(d, w, &root, &parent, &kids, &n))
		return 0;
	for (i = 0; i < n && !found; i++)
		found = find(d, kids[i], prefix);
	if (kids)
		XFree(kids);
	return found;
}

int main(int argc, char **argv)
{
	const char *title = "Pico Computer 3";
	Display *d;
	Window w;
	XEvent ev;

	if (argc > 2 && !strcmp(argv[1], "-w"))
		title = argv[2];
	d = XOpenDisplay(NULL);
	if (!d) {
		fprintf(stderr, "xclose: no display\n");
		return 1;
	}
	w = find(d, DefaultRootWindow(d), title);
	if (!w) {
		fprintf(stderr, "xclose: no window titled %s...\n", title);
		return 1;
	}
	memset(&ev, 0, sizeof ev);
	ev.xclient.type = ClientMessage;
	ev.xclient.window = w;
	ev.xclient.message_type = XInternAtom(d, "WM_PROTOCOLS", False);
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = XInternAtom(d, "WM_DELETE_WINDOW", False);
	ev.xclient.data.l[1] = CurrentTime;
	XSendEvent(d, w, False, NoEventMask, &ev);
	XFlush(d);
	XCloseDisplay(d);
	printf("xclose: sent WM_DELETE_WINDOW to 0x%lx\n", (unsigned long)w);
	return 0;
}
