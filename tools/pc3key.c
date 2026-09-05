/*
 * pc3key - press keys in the PC3 display server from a script.
 *
 *   pc3key press KEY        hold a key down
 *   pc3key release KEY      let it go
 *   pc3key tap KEY...       press and release each in turn
 *   pc3key type TEXT        tap the keys that spell TEXT (letters, digits,
 *                           space, and enter for a newline; shift for capitals)
 *
 * KEY is a MiniFB key name without its prefix: A..Z, 0..9, SPACE, ENTER,
 * ESCAPE, TAB, BACKSPACE, UP, DOWN, LEFT, RIGHT, HOME, END, PAGE_UP,
 * PAGE_DOWN, INSERT, DELETE, F1..F12, LEFT_SHIFT, RIGHT_SHIFT,
 * LEFT_CONTROL, LEFT_ALT, CAPS_LOCK, KP_0..KP_9 and the punctuation
 * names MINUS EQUAL COMMA PERIOD SLASH SEMICOLON APOSTROPHE GRAVE_ACCENT
 * LEFT_BRACKET RIGHT_BRACKET BACKSLASH.
 *
 * The server treats these exactly as keys from the window, through the
 * same decoder: what a program reads depends on the layout the server
 * runs with, so `type` spells with the US positions and is right for
 * letters, digits and space under any of them.  The end-to-end test is
 * its user; a person is welcome to it as well.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <MiniFB_enums.h>
#include "pc3proto.h"
#include "pc3client.h"

static const struct {
	const char *name;
	int key;
} names[] = {
	{ "SPACE", MFB_KB_KEY_SPACE }, { "ENTER", MFB_KB_KEY_ENTER },
	{ "ESCAPE", MFB_KB_KEY_ESCAPE }, { "TAB", MFB_KB_KEY_TAB },
	{ "BACKSPACE", MFB_KB_KEY_BACKSPACE }, { "INSERT", MFB_KB_KEY_INSERT },
	{ "DELETE", MFB_KB_KEY_DELETE }, { "RIGHT", MFB_KB_KEY_RIGHT },
	{ "LEFT", MFB_KB_KEY_LEFT }, { "DOWN", MFB_KB_KEY_DOWN },
	{ "UP", MFB_KB_KEY_UP }, { "PAGE_UP", MFB_KB_KEY_PAGE_UP },
	{ "PAGE_DOWN", MFB_KB_KEY_PAGE_DOWN }, { "HOME", MFB_KB_KEY_HOME },
	{ "END", MFB_KB_KEY_END }, { "CAPS_LOCK", MFB_KB_KEY_CAPS_LOCK },
	{ "NUM_LOCK", MFB_KB_KEY_NUM_LOCK },
	{ "LEFT_SHIFT", MFB_KB_KEY_LEFT_SHIFT }, { "RIGHT_SHIFT", MFB_KB_KEY_RIGHT_SHIFT },
	{ "LEFT_CONTROL", MFB_KB_KEY_LEFT_CONTROL }, { "RIGHT_CONTROL", MFB_KB_KEY_RIGHT_CONTROL },
	{ "LEFT_ALT", MFB_KB_KEY_LEFT_ALT }, { "RIGHT_ALT", MFB_KB_KEY_RIGHT_ALT },
	{ "MINUS", MFB_KB_KEY_MINUS }, { "EQUAL", MFB_KB_KEY_EQUAL },
	{ "COMMA", MFB_KB_KEY_COMMA }, { "PERIOD", MFB_KB_KEY_PERIOD },
	{ "SLASH", MFB_KB_KEY_SLASH }, { "SEMICOLON", MFB_KB_KEY_SEMICOLON },
	{ "APOSTROPHE", MFB_KB_KEY_APOSTROPHE }, { "GRAVE_ACCENT", MFB_KB_KEY_GRAVE_ACCENT },
	{ "LEFT_BRACKET", MFB_KB_KEY_LEFT_BRACKET }, { "RIGHT_BRACKET", MFB_KB_KEY_RIGHT_BRACKET },
	{ "BACKSLASH", MFB_KB_KEY_BACKSLASH },
};

static int key_of(const char *s)
{
	unsigned i;

	if (strlen(s) == 1) {
		int c = toupper((unsigned char)s[0]);
		if (c >= 'A' && c <= 'Z')
			return MFB_KB_KEY_A + (c - 'A');
		if (c >= '0' && c <= '9')
			return MFB_KB_KEY_0 + (c - '0');
	}
	if ((s[0] == 'F' || s[0] == 'f') && isdigit((unsigned char)s[1])) {
		int n = atoi(s + 1);
		if (n >= 1 && n <= 25)
			return MFB_KB_KEY_F1 + (n - 1);
	}
	if (!strncasecmp(s, "KP_", 3) && isdigit((unsigned char)s[3]))
		return MFB_KB_KEY_KP_0 + (s[3] - '0');
	for (i = 0; i < sizeof names / sizeof names[0]; i++)
		if (!strcasecmp(names[i].name, s))
			return names[i].key;
	return -1;
}

static int fd;

static void act(int key, int pressed)
{
	if (pc3_inject_key(fd, key, pressed) < 0) {
		perror("pc3key: inject");
		exit(1);
	}
}

static void tap(int key)
{
	act(key, 1);
	act(key, 0);
}

int main(int argc, char **argv)
{
	int i;

	if (argc < 3) {
		fprintf(stderr, "usage: pc3key press|release|tap KEY...   |   pc3key type TEXT\n");
		return 2;
	}
	setenv(PC3_AUTOSTART_ENV, "0", 0);	/* press keys in a server that exists */
	fd = pc3_sys_open();
	if (fd < 0) {
		fprintf(stderr, "pc3key: no display server\n");
		return 1;
	}
	if (!strcmp(argv[1], "type")) {
		const char *s;
		for (s = argv[2]; *s; s++) {
			int c = (unsigned char)*s, key, shift = 0;
			if (c == '\n' || c == '\r')
				key = MFB_KB_KEY_ENTER;
			else if (c == ' ')
				key = MFB_KB_KEY_SPACE;
			else if (isdigit(c))
				key = MFB_KB_KEY_0 + (c - '0');
			else if (isalpha(c)) {
				key = MFB_KB_KEY_A + (toupper(c) - 'A');
				shift = isupper(c);
			} else {
				fprintf(stderr, "pc3key: cannot type '%c'\n", c);
				return 1;
			}
			if (shift)
				act(MFB_KB_KEY_LEFT_SHIFT, 1);
			tap(key);
			if (shift)
				act(MFB_KB_KEY_LEFT_SHIFT, 0);
		}
		return 0;
	}
	for (i = 2; i < argc; i++) {
		int key = key_of(argv[i]);
		if (key < 0) {
			fprintf(stderr, "pc3key: no key called %s\n", argv[i]);
			return 1;
		}
		if (!strcmp(argv[1], "press"))
			act(key, 1);
		else if (!strcmp(argv[1], "release"))
			act(key, 0);
		else if (!strcmp(argv[1], "tap"))
			tap(key);
		else {
			fprintf(stderr, "pc3key: %s?\n", argv[1]);
			return 2;
		}
	}
	return 0;
}
