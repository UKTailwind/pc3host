/* sys/ioctl.h - TIOCGWINSZ on the console; everything else is ENOTTY,
 * which is what the programs expect of a descriptor that is not
 * /dev/sys (that one goes through pc3client).  A function, declared
 * with the shape bcrun.c declares it in - int, int, ... - so the two
 * agree in one translation unit. */
#ifndef PC3W_SYS_IOCTL_H
#define PC3W_SYS_IOCTL_H
#include <pc3w.h>
struct winsize {
	unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel;
};
#define TIOCGWINSZ 0x5413
int ioctl(int fd, int req, ...);
#endif
