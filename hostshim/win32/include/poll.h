/* poll.h - poll() over pc3w's pseudo-descriptors, the console and pipes.
 * struct pollfd and the POLL* bits are Winsock's own (WSAPOLLFD), so a
 * source that includes both this and sys/socket.h sees one definition. */
#ifndef PC3W_POLL_H
#define PC3W_POLL_H
#include <sys/socket.h>
typedef unsigned long nfds_t;
#define poll(p, n, t) pc3w_poll((p), (unsigned)(n), (t))
#endif
