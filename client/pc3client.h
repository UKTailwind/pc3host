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

#endif /* PC3CLIENT_H */
