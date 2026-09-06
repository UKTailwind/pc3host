/* sys/socket.h - BSD sockets on Winsock, through pc3w's pseudo-descriptors.
 * Every socket call a hosted program makes is renamed to the shim's; the
 * types and constants are Winsock's, which agree with Linux's for
 * everything a PC3 program uses (AF_INET 2, SOCK_STREAM 1, SOCK_DGRAM 2). */
#ifndef PC3W_SYS_SOCKET_H
#define PC3W_SYS_SOCKET_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <afunix.h>
#include <pc3w.h>

#ifndef SHUT_RDWR
#define SHUT_RD   SD_RECEIVE
#define SHUT_WR   SD_SEND
#define SHUT_RDWR SD_BOTH
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define socket      pc3w_socket
#define connect     pc3w_connect
#define bind        pc3w_bind
#define listen      pc3w_listen
#define accept      pc3w_accept
#define send        pc3w_send
#define recv        pc3w_recv
#define sendto      pc3w_sendto
#define recvfrom    pc3w_recvfrom
#define setsockopt  pc3w_setsockopt
#define shutdown    pc3w_shutdown
#define socketpair(d, t, p, fds) pc3w_socketpair(fds)

#endif /* PC3W_SYS_SOCKET_H */
