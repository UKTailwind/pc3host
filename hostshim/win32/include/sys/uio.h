/* sys/uio.h - writev over a socket pseudo-descriptor. */
#ifndef PC3W_SYS_UIO_H
#define PC3W_SYS_UIO_H
#include <stddef.h>
#include <pc3w.h>
struct iovec {
	void *iov_base;
	size_t iov_len;
};
#define writev pc3w_writev
#endif
