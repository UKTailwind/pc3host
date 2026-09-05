/*
 * keyboard.c - the window's keyboard, through the PC3's own decoder.
 *
 * On the board a USB keyboard sends 8-byte HID boot reports and
 * kbd_decode.c turns them into console bytes, lock states, auto-repeat
 * and the six-slot held-key table behind KEYDOWN.  Here MiniFB tells us
 * which key went down or up and we build the same reports, so the same
 * 534 lines decide everything - layouts, AltGr, Caps Lock, repeat
 * timing, KEYDOWN - and a program cannot tell the difference.  The
 * MicroPython emulator did exactly this with SDL (kbd_sdl.c); the one
 * thing SDL gave for free that MiniFB does not is the HID usage: SDL
 * scancodes ARE usages, MiniFB keys are GLFW-style names the host has
 * already put through its layout, so a table stands between them.
 *
 * THE TABLE ASSUMES THE KEYS ARE WHERE A US KEYBOARD HAS THEM, and the
 * decoder is then run with the layout the user's keyboard really has
 * (UK by default, the board's default; --keymap and OPTION KEYBOARD
 * change it).  That works because MiniFB derives the key from the
 * UNSHIFTED keysym of the physical key under the host's layout: the
 * key left of Z on a UK layout yields backslash, which the table sends
 * as HID 0x64 (the UK key) when the layout is UK, and shift-2 arrives
 * as the 2 key with shift held, which the UK table turns into a quote
 * as the board does.  Two things do not fit and are handled apart:
 * MiniFB has no key for the UK # key (XK_numbersign), so a key it
 * reports as unknown is taken from its character callback instead,
 * and X11 auto-repeat arrives as release/press pairs, which are
 * recognised and dropped so the decoder's own 600/150 ms repeat is the
 * one a program sees.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <MiniFB_enums.h>

#include "kbd_decode.h"
#include "keyboard_maps.h"
#include "pc3d.h"

/* --- what the decoder asks of the platform ---------------------------------- */

const int *kbd_layout = UKkeyValue;
const char *kbd_layout_name = "UK";

static const struct {
	const char *name;
	const int *table;
} layouts[] = {
	{ "US", USkeyValue }, { "UK", UKkeyValue }, { "DE", DEkeyValue },
	{ "FR", FRkeyValue }, { "ES", ESkeyValue }, { "BE", BEkeyValue },
};

int keyboard_set_layout(const char *name)
{
	unsigned i;

	if (!name || !name[0] || !name[1])
		return -1;
	for (i = 0; i < sizeof layouts / sizeof layouts[0]; i++)
		if ((layouts[i].name[0] | 0x20) == (name[0] | 0x20) &&
		    (layouts[i].name[1] | 0x20) == (name[1] | 0x20)) {
			kbd_layout = layouts[i].table;
			kbd_layout_name = layouts[i].name;
			return 0;
		}
	return -1;
}

const char *keyboard_layout_name(void)
{
	return kbd_layout_name;
}

static int keylog;

void keyboard_set_log(int on)
{
	keylog = on;
}

/* A decoded byte: the console's input queue, which here is a client's
 * key channel (pc3d.c). */
void kbd_push(uint8_t c)
{
	if (keylog) {
		if (c >= 32 && c < 127)
			fprintf(stderr, "kbd: -> 0x%02x '%c'\n", c, c);
		else
			fprintf(stderr, "kbd: -> 0x%02x\n", c);
	}
	pc3d_key_byte(c);
}

void kbd_backend_set_leds(int slot, uint8_t leds)
{
	(void)slot;
	(void)leds;		/* no lights on a PC keyboard we can reach */
}

uint32_t kbd_ticks_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void kbd_backend_on_key(int code)
{
	(void)code;		/* MicroPython's on_key hook; nothing here */
}

void kbd_backend_msg(const char *s)
{
	fputs(s, stderr);
}

/* --- MiniFB key -> HID usage ------------------------------------------------- */

static uint8_t usage_of(int key)
{
	if (key >= MFB_KB_KEY_A && key <= MFB_KB_KEY_Z)
		return (uint8_t)(0x04 + key - MFB_KB_KEY_A);
	if (key >= MFB_KB_KEY_1 && key <= MFB_KB_KEY_9)
		return (uint8_t)(0x1E + key - MFB_KB_KEY_1);
	if (key >= MFB_KB_KEY_F1 && key <= MFB_KB_KEY_F12)
		return (uint8_t)(0x3A + key - MFB_KB_KEY_F1);
	if (key >= MFB_KB_KEY_F13 && key <= MFB_KB_KEY_F24)
		return (uint8_t)(0x68 + key - MFB_KB_KEY_F13);
	if (key >= MFB_KB_KEY_KP_1 && key <= MFB_KB_KEY_KP_9)
		return (uint8_t)(0x59 + key - MFB_KB_KEY_KP_1);
	switch (key) {
	case MFB_KB_KEY_0:            return 0x27;
	case MFB_KB_KEY_ENTER:        return 0x28;
	case MFB_KB_KEY_ESCAPE:       return 0x29;
	case MFB_KB_KEY_BACKSPACE:    return 0x2A;
	case MFB_KB_KEY_TAB:          return 0x2B;
	case MFB_KB_KEY_SPACE:        return 0x2C;
	case MFB_KB_KEY_MINUS:        return 0x2D;
	case MFB_KB_KEY_EQUAL:        return 0x2E;
	case MFB_KB_KEY_LEFT_BRACKET: return 0x2F;
	case MFB_KB_KEY_RIGHT_BRACKET:return 0x30;
	case MFB_KB_KEY_BACKSLASH:
		/* On a US layout this is the key above Enter (0x31).  On a
		 * UK layout the only key that yields backslash unshifted is
		 * the one left of Z, which a UK keyboard reports as 0x64. */
		return (kbd_layout == UKkeyValue) ? 0x64 : 0x31;
	case MFB_KB_KEY_SEMICOLON:    return 0x33;
	case MFB_KB_KEY_APOSTROPHE:   return 0x34;
	case MFB_KB_KEY_GRAVE_ACCENT: return 0x35;
	case MFB_KB_KEY_COMMA:        return 0x36;
	case MFB_KB_KEY_PERIOD:       return 0x37;
	case MFB_KB_KEY_SLASH:        return 0x38;
	case MFB_KB_KEY_CAPS_LOCK:    return 0x39;
	case MFB_KB_KEY_PRINT_SCREEN: return 0x46;
	case MFB_KB_KEY_SCROLL_LOCK:  return 0x47;
	case MFB_KB_KEY_PAUSE:        return 0x48;
	case MFB_KB_KEY_INSERT:       return 0x49;
	case MFB_KB_KEY_HOME:         return 0x4A;
	case MFB_KB_KEY_PAGE_UP:      return 0x4B;
	case MFB_KB_KEY_DELETE:       return 0x4C;
	case MFB_KB_KEY_END:          return 0x4D;
	case MFB_KB_KEY_PAGE_DOWN:    return 0x4E;
	case MFB_KB_KEY_RIGHT:        return 0x4F;
	case MFB_KB_KEY_LEFT:         return 0x50;
	case MFB_KB_KEY_DOWN:         return 0x51;
	case MFB_KB_KEY_UP:           return 0x52;
	case MFB_KB_KEY_NUM_LOCK:     return 0x53;
	case MFB_KB_KEY_KP_DIVIDE:    return 0x54;
	case MFB_KB_KEY_KP_MULTIPLY:  return 0x55;
	case MFB_KB_KEY_KP_SUBTRACT:  return 0x56;
	case MFB_KB_KEY_KP_ADD:       return 0x57;
	case MFB_KB_KEY_KP_ENTER:     return 0x58;
	case MFB_KB_KEY_KP_0:         return 0x62;
	case MFB_KB_KEY_KP_DECIMAL:   return 0x63;
	case MFB_KB_KEY_WORLD_1:      return 0x64;	/* the key left of Z */
	case MFB_KB_KEY_MENU:         return 0x65;
	case MFB_KB_KEY_KP_EQUAL:     return 0x67;
	case MFB_KB_KEY_LEFT_CONTROL: return 0xE0;
	case MFB_KB_KEY_LEFT_SHIFT:   return 0xE1;
	case MFB_KB_KEY_LEFT_ALT:     return 0xE2;
	case MFB_KB_KEY_LEFT_SUPER:   return 0xE3;
	case MFB_KB_KEY_RIGHT_CONTROL:return 0xE4;
	case MFB_KB_KEY_RIGHT_SHIFT:  return 0xE5;
	case MFB_KB_KEY_RIGHT_ALT:    return 0xE6;
	case MFB_KB_KEY_RIGHT_SUPER:  return 0xE7;
	default:                      return 0;
	}
}

/* --- the boot report, kept as a boot keyboard keeps it ------------------------ */

static uint8_t held[6];		/* usages down, in report order; 0 = free */
static uint8_t modbits;		/* bit 0 LCtrl ... bit 7 RGui */

static void send_report(void)
{
	uint8_t r[8] = { modbits, 0, held[0], held[1], held[2], held[3],
			 held[4], held[5] };

	kbd_process_report(r, 8, -1);
}

static void transition(uint8_t usage, int down)
{
	int i;

	if (usage < 4)
		return;
	if (usage >= 0xE0) {
		uint8_t bit = (uint8_t)(1u << (usage - 0xE0));
		if (down)
			modbits |= bit;
		else
			modbits &= (uint8_t)~bit;
		send_report();
		return;
	}
	if (down) {
		for (i = 0; i < 6; i++)
			if (held[i] == usage)
				return;			/* already down */
		for (i = 0; i < 6; i++)
			if (held[i] == 0) {
				held[i] = usage;
				break;			/* a seventh key is dropped, as a boot keyboard drops it */
			}
	} else {
		for (i = 0; i < 6; i++)
			if (held[i] == usage)
				held[i] = 0;
	}
	send_report();
}

/* --- events from the window --------------------------------------------------- */

/* MiniFB delivers events during its pump; they are queued and processed
 * after it, so that X11's auto-repeat - a release and a press of the
 * same key in the same batch - can be seen for what it is. */
struct ev {
	int key;
	int pressed;
};
static struct ev queue[128];
static int nq;
static int unknown_pending;	/* a press MiniFB could not name */

void keyboard_event(int key, int pressed)
{
	if (key == MFB_KB_KEY_UNKNOWN) {
		unknown_pending = pressed;
		return;
	}
	if (nq < (int)(sizeof queue / sizeof queue[0])) {
		queue[nq].key = key;
		queue[nq].pressed = pressed;
		nq++;
	}
}

/* The character MiniFB made of the last key.  Only used for a key it
 * could not name - the UK # key is the one that matters - and only for
 * printable ASCII, which is what the decoder would have produced. */
void keyboard_char(unsigned cp)
{
	if (!unknown_pending)
		return;
	unknown_pending = 0;
	if (cp >= 0x20 && cp < 0x7F) {
		if (keylog)
			fprintf(stderr, "kbd: unnamed key, char '%c'\n", (int)cp);
		kbd_push((uint8_t)cp);
	}
}

void keyboard_pump(void)
{
	int i;

	for (i = 0; i < nq; i++) {
		int key = queue[i].key, pressed = queue[i].pressed;
		uint8_t u;

		if (!pressed && i + 1 < nq && queue[i + 1].pressed &&
		    queue[i + 1].key == key) {
			i++;			/* X11 auto-repeat: release+press, dropped */
			continue;
		}
		u = usage_of(key);
		if (keylog)
			fprintf(stderr, "kbd: key %d %s usage 0x%02x\n", key,
				pressed ? "down" : "up", u);
		transition(u, pressed);
	}
	nq = 0;
}

void keyboard_tick(void)
{
	kbd_repeat_check();
}

/* The window went away: every key is up, as when a keyboard is unplugged. */
void keyboard_reset(void)
{
	memset(held, 0, sizeof held);
	modbits = 0;
	nq = 0;
	unknown_pending = 0;
	kbd_stop_repeat();
	kbd_clear_state();
}

/* A synthetic event: straight through, no pairing. */
void keyboard_inject(int key, int pressed)
{
	uint8_t u = usage_of(key);

	if (keylog)
		fprintf(stderr, "kbd: inject %d %s usage 0x%02x\n", key,
			pressed ? "down" : "up", u);
	transition(u, pressed);
}

int keyboard_keydown(int n)
{
	return usb_kbd_keydown(n);
}
