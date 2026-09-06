/*
 * pc3proto.h - the wire between a PC3 program and the device server.
 *
 * The protocol IS pico_ioctl.h: every request carries an ioctl code and
 * the server answers as misc.c's plt_dev_ioctl would.  What this header
 * adds is only the framing, and a flattened form for the handful of
 * structures that carry pointers - a batch's points, a bitmap's bits, a
 * text run's string, a blit's buffer - because a pointer cannot cross
 * a socket.  Everything else in pico_ioctl.h is fixed-width and goes
 * over as its own bytes.
 *
 * Both ends include this and pico_ioctl.h, and nothing else is agreed
 * anywhere.  ioctlcheck.sh in the FUZIX tree keeps the codes unique;
 * PC3_PROTO_VERSION here changes when the framing does.
 *
 *   request:  struct pc3_req, then len bytes of payload
 *   reply:    struct pc3_rep, then len bytes of payload
 *
 * Codes whose ioctl data "is the value" (GFXIOC_PIXEL's packed x,y,
 * GFXIOC_COLOUR's RGB888, the FB and MAP calls) put it in req.arg and
 * send no payload.  GFXIOC_MODE and GFXIOC_PAL point at an int on the
 * board; the client reads it and sends it the same way.
 */
#ifndef PC3PROTO_H
#define PC3PROTO_H

#include <stdint.h>

#define PC3_PROTO_VERSION 1

/* The socket.  PC3_SOCKET overrides; otherwise $XDG_RUNTIME_DIR/pc3d.sock,
 * or /tmp/pc3d-<uid>.sock. */
#define PC3_SOCKET_ENV   "PC3_SOCKET"
#define PC3_SOCKET_NAME  "pc3d.sock"
/* PC3_DISPLAY=off makes pc3_sys_open() fail as it does on a machine
 * with no /dev/sys - the gates run that way, so a program's output is
 * what it always was. */
#define PC3_DISPLAY_ENV  "PC3_DISPLAY"
/* PC3_AUTOSTART=0 stops the client starting a server when there is
 * none listening; PC3D names the server binary to start. */
#define PC3_AUTOSTART_ENV "PC3_AUTOSTART"
#define PC3_SERVER_ENV    "PC3D"

struct pc3_req {
	uint32_t len;		/* payload bytes that follow */
	uint16_t code;		/* the ioctl code, or PC3_HELLO */
	uint16_t flags;		/* 0 */
	uint32_t arg;		/* the value, for codes whose data is one */
};

struct pc3_rep {
	uint32_t len;		/* payload bytes that follow */
	int32_t ret;		/* the ioctl's return value */
	int32_t err;		/* errno when ret < 0 (Linux numbering) */
};

/* The first message on a connection.  pid and ppid let the server give
 * a child the framebuffer target its parent selected, as the kernel
 * does through p_pptr (display_fb_enter). */
#define PC3_HELLO 0xFFFF
struct pc3_hello {
	uint32_t version;
	int32_t pid;
	int32_t ppid;
};

/* A KEY CHANNEL.  Sent once, after HELLO, on a connection of its own:
 * from then on the server writes the keyboard's decoded bytes into it
 * and expects nothing back - it is the console tty's input queue, as a
 * stream.  The newest key channel gets the keys, as the program in the
 * foreground on the board gets the console; a program that closes its
 * channel hands the keyboard back to the one before it.  Bytes typed
 * while no channel is open wait in the server, as they wait in the
 * kernel's ring, for the next one. */
#define PC3_KEYCHAN 0xFFFE

/* A synthetic key event, exactly as the window would have delivered
 * it: a MiniFB key code, pressed or released.  For tests and scripts
 * (tools/pc3key.c); the server treats it as it treats the window. */
#define PC3_INJECT 0xFFFD

/* What version is the server?  The reply's payload is its version
 * string.  A server outlives the programs that use it and so outlives a
 * package upgrade; the client asks once and says if the answer is not
 * its own version - or if the server does not know the question. */
#define PC3_VERSION_REQ 0xFFFC
struct pc3_inject {
	int32_t key;		/* MFB_KB_KEY_* */
	int32_t pressed;	/* 1 down, 0 up */
	int32_t mods;		/* reserved, 0 */
};

/* Payload ceiling.  The biggest legitimate payload is a BLIT of a whole
 * framebuffer (40,960 bytes) or a user font; a megabyte is generous and
 * bounds a bad client. */
#define PC3_MAX_PAYLOAD (1u << 20)

/* ---- flattened forms of the pointer-bearing structures ----------------
 * Fixed part first, then the arrays in the order named. */

/* GFXIOC_PIXELS / GFXIOC_RECTS: then items[count] (gfx_pt or gfx_rc),
 * then colours[count] as uint32 if has_colours. */
struct pc3w_batch {
	uint16_t count;
	uint16_t flags;
	uint16_t has_colours;
	uint16_t pad;
};

/* GFXIOC_BITMAP: then bits[(width*height+7)/8]. */
struct pc3w_bitmap {
	int16_t x, y;
	uint8_t width, height, scale, pad;
	int32_t fg, bg;
};

/* GFXIOC_TEXT: then str[len]. */
struct pc3w_text {
	int16_t x, y;
	uint8_t scale, font;
	int32_t fg, bg;
	uint16_t len;
	uint16_t pad;
};

/* GFXIOC_BLIT: then buf[len] in the request.
 * GFXIOC_BLITRD: buf[len] comes back in the reply. */
struct pc3w_blit {
	uint32_t offset;
	uint32_t len;
};

/* GFXIOC_BLITR: then buf[rows*len] in the request.
 * GFXIOC_BLITRDR: buf[rows*len] comes back in the reply. */
struct pc3w_blitr {
	uint32_t offset;
	uint16_t len, rows, stride, pad;
};

/* GFXIOC_FONTDEF: then the font, bytes long, header first.  The server
 * keeps a copy for the connection - it cannot read a program's memory
 * where the kernel could - and forgets it when the connection ends. */
struct pc3w_fontdef {
	uint8_t font;
	uint8_t pad[3];
	uint32_t bytes;
};

/* GFXIOC_FONTADDR: the request is struct gfx_fontaddr as it stands; the
 * reply carries it back with bytes filled in, followed by the font
 * itself, which the client places in memory it can address and reports
 * as addr. */

#endif /* PC3PROTO_H */
