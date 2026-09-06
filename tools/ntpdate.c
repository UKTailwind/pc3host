/*
 * ntpdate - what WEB NTP runs, on a PC.
 *
 *   ntpdate [-s] [-O seconds] server
 *
 * On the board WEB NTP is mmb_net.h's argv for ntpdate(8): -s sets the
 * clock and prints nothing, -O adds the time zone, and a non-zero exit
 * is the statement's error.  A PC keeps its own clock and a program
 * has no business setting it, so this asks the server the SNTP
 * question ntpdate asks, and exits 0 if it answered - the statement
 * succeeds, retic's retry loop moves on, and the clock is the one the
 * operating system already keeps.  Without -s the server's time is
 * printed, as ntpdate prints it, so the answer can be seen.
 *
 * bcrun puts its own directory first on the PATH, so a program's
 * "ntpdate" is this one, and the machine's ntpdate, if it has one, is
 * not touched.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>

#ifdef _WIN32
#define sock_close(fd) pc3w_sock_close(fd)
#define PC3_RCVTIMEO(v, s) DWORD v = (s) * 1000	/* Winsock: milliseconds */
#else
#define sock_close(fd) close(fd)
#define PC3_RCVTIMEO(v, s) struct timeval v = { (s), 0 }
#endif

#define NTP_EPOCH_OFFSET 2208988800UL	/* 1900 to 1970 */

int main(int argc, char **argv)
{
	const char *server = NULL;
	int quiet = 0, i, fd, tries;
	long offset = 0;
	struct addrinfo hints, *res, *ai;
	unsigned char pkt[48];

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-s"))
			quiet = 1;
		else if (!strcmp(argv[i], "-O") && i + 1 < argc)
			offset = atol(argv[++i]);
		else if (argv[i][0] == '-' && argv[i][1]) {
			fprintf(stderr, "usage: ntpdate [-s] [-O seconds] server\n");
			return 2;
		} else
			server = argv[i];
	}
	if (!server) {
		fprintf(stderr, "usage: ntpdate [-s] [-O seconds] server\n");
		return 2;
	}
	(void)offset;			/* the PC's clock is its own affair */

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo(server, "123", &hints, &res) != 0) {
		fprintf(stderr, "ntpdate: cannot resolve %s\n", server);
		return 1;
	}
	for (ai = res; ai; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0)
			continue;
		for (tries = 0; tries < 3; tries++) {
			PC3_RCVTIMEO(tv, 2);
			socklen_t sl = ai->ai_addrlen;
			struct sockaddr_storage from;
			int n;

			memset(pkt, 0, sizeof pkt);
			pkt[0] = 0x1B;		/* LI 0, version 3, client */
			setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
			if (sendto(fd, pkt, sizeof pkt, 0, ai->ai_addr, ai->ai_addrlen) < 0)
				break;
			sl = sizeof from;
			n = (int)recvfrom(fd, pkt, sizeof pkt, 0, (struct sockaddr *)&from, &sl);
			if (n >= 48 && (pkt[0] & 7) == 4) {	/* mode 4: server */
				unsigned long secs = ((unsigned long)pkt[40] << 24) |
						     ((unsigned long)pkt[41] << 16) |
						     ((unsigned long)pkt[42] << 8) | pkt[43];
				time_t t = (time_t)(secs - NTP_EPOCH_OFFSET);

				if (!quiet) {
					char buf[64];
					struct tm *tm = gmtime(&t);
					strftime(buf, sizeof buf, "%d %b %H:%M:%S", tm);
					printf("%s ntpdate: %s answered; the clock is the system's\n",
					       buf, server);
				}
				sock_close(fd);
				freeaddrinfo(res);
				return 0;
			}
		}
		sock_close(fd);
	}
	freeaddrinfo(res);
	fprintf(stderr, "ntpdate: no server suitable for synchronization found\n");
	return 1;
}
