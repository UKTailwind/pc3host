/*
 * pc3client.h - /dev/sys on a PC.
 *
 * A PC3 program opens /dev/sys and ioctls it; this library gives it the
 * same two calls against the PC3 device server (pc3d).  The runtime
 * inside bcrun, a bytecode program's own open, and the image programs
 * all reach the server through these and nothing else.
 */
#ifndef PC3CLIENT_H
#define PC3CLIENT_H

#include <stddef.h>

/* Connect to the server, starting one if there is none and the
 * environment allows (pc3proto.h).  Returns a descriptor - a real one,
 * the socket - or -1 with errno set: ENOENT when PC3_DISPLAY=off or no
 * server could be reached. */
int pc3_sys_open(void);

/* An ioctl on a descriptor from pc3_sys_open().  Any other descriptor
 * goes to the real ioctl(), so callers can use this for everything.
 * Returns what the kernel would, errno set on -1. */
int pc3_sys_ioctl(int fd, unsigned long code, void *arg);

/* Close a descriptor from pc3_sys_open(). */
int pc3_sys_close(int fd);

/* Is this descriptor one of ours? */
int pc3_sys_isfd(int fd);

/* --- the keyboard ------------------------------------------------------------
 *
 * The window's keyboard reaches a program as a stream of the same bytes
 * the PC3's console tty would carry: the decoder's output, MMBasic key
 * codes for the special keys.  pc3_rd1() is the runtime's one read
 * primitive on a PC - one byte from the terminal or from the window,
 * whichever has one, or -1 - honouring the terminal's VMIN/VTIME so the
 * escape-sequence wait still works.  A Ctrl-C from the window raises
 * SIGINT, as the tty's ISIG would.  pc3_key_ready() says whether the
 * window's keyboard is available when there is no terminal at all. */
int pc3_rd1(void);
int pc3_key_ready(void);

/* A synthetic key event on an open connection (tests, pc3key). */
int pc3_inject_key(int fd, int mfb_key, int pressed);

/* --- where things are ---------------------------------------------------------
 *
 * The directory this executable is in, and PATH with it in front - so
 * cc finds mmbc and cpp, bcrun finds saveimage and loadjpg, and mmbedit
 * finds cc, wherever the tree was installed or built. */
int pc3_exe_dir(char *buf, size_t n);
void pc3_path_prepend(void);

#endif /* PC3CLIENT_H */
