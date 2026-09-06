/* termios.h - the console, in the terms the programs speak.
 *
 * tcgetattr and tcsetattr keep a termios for descriptor 0 when it is a
 * console.  Clearing ICANON puts the console into raw mode - no line
 * input, no echo, virtual-terminal input so arrows and function keys
 * arrive as the escape sequences a terminal would send; ISIG kept on
 * leaves Ctrl-C a signal, as ISIG does.  VMIN and VTIME decide what
 * pc3w_read() waits for, exactly as the tty's line discipline would:
 * VMIN 0 VTIME 0 returns what is there, VMIN 0 VTIME n waits n tenths
 * of a second, VMIN 1 waits for a byte.  ICRNL turns the Enter key's
 * CR into NL, as a tty does. */
#ifndef PC3W_TERMIOS_H
#define PC3W_TERMIOS_H

typedef unsigned char cc_t;
typedef unsigned int  tcflag_t;
typedef unsigned int  speed_t;
#define NCCS 32
struct termios {
	tcflag_t c_iflag, c_oflag, c_cflag, c_lflag;
	cc_t c_cc[NCCS];
};

/* c_lflag */
#define ISIG    0x0001
#define ICANON  0x0002
#define ECHO    0x0008
#define ECHOE   0x0010
#define ECHOK   0x0020
#define ECHONL  0x0040
#define IEXTEN  0x8000
/* c_iflag */
#define BRKINT  0x0002
#define INPCK   0x0010
#define ISTRIP  0x0020
#define ICRNL   0x0100
#define IXON    0x0400
/* c_oflag */
#define OPOST   0x0001
#define ONLCR   0x0004
/* c_cflag */
#define CS8     0x0030
/* c_cc */
#define VINTR   0
#define VEOF    4
#define VTIME   5
#define VMIN    6

#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int action, const struct termios *t);

static inline void cfmakeraw(struct termios *t)
{
	t->c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
	t->c_oflag &= ~OPOST;
	t->c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	t->c_cc[VMIN] = 1;
	t->c_cc[VTIME] = 0;
}

#endif /* PC3W_TERMIOS_H */
