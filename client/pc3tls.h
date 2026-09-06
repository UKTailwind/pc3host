/*
 * pc3tls.h - TLS sockets for a program that expects the board's.
 *
 * On the Pico Computer 3 a TLS connection is a kernel socket:
 * socket(AF_INET, SOCK_STREAM, IPPROTO_TLS), SIOCTLSHOST with the host
 * name, then connect, read, write and close on an ordinary descriptor,
 * with the CA bundle handed to the kernel once through NETIOC_TLSCA.
 * A PC's kernel has no such socket, so the hosted bcrun keeps a table
 * of descriptors that were asked for as TLS and routes their calls
 * here, where mbedtls - the library the board's kernel uses - does the
 * handshake and the records over a plain TCP socket.  The descriptor
 * the program holds IS the TCP socket, so fcntl and the non-blocking
 * flag work on it unchanged.
 *
 * The handshake is driven from connect(): the program's loop calls
 * connect until it answers 0, and a TLS connect answers EINPROGRESS
 * until the TCP connection is up and the handshake has finished, so
 * the same loop and the same timeout cover both.  A handshake that
 * fails - the certificate did not verify, the peer is not TLS -
 * answers ECONNREFUSED, which the program reports as its timeout
 * message, as the board does for any handshake failure.
 *
 * Verification: the system's CA bundle (/etc/ssl/certs/ca-certificates.crt)
 * is loaded at first use if it exists, and WEB TLS CA replaces it; with
 * a bundle the peer must verify, without one the session is encrypted
 * but not authenticated, which is the board's state before WEB TLS CA.
 * A host name set through SIOCTLSHOST is checked against the
 * certificate; a connection by dotted address checks the chain alone.
 */
#ifndef PC3TLS_H
#define PC3TLS_H

#include <stdint.h>
#include <sys/socket.h>

int pc3_tls_socket(void);			/* a TCP socket marked for TLS */
int pc3_tls_isfd(int fd);
int pc3_tls_sethost(int fd, const char *host);	/* SNI and the name to verify */
int pc3_tls_ca(const void *buf, uint32_t len);	/* PEM (NUL-terminated) or DER */
int pc3_tls_connect(int fd, const struct sockaddr *sa, socklen_t len);
long pc3_tls_read(int fd, void *buf, size_t n);
long pc3_tls_write(int fd, const void *buf, size_t n);
int pc3_tls_close(int fd);

#endif /* PC3TLS_H */
