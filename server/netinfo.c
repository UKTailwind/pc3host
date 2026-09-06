/*
 * netinfo.c - NETIOC_STATUS for a machine whose network is already up.
 *
 * On the board the radio joins at boot from /etc/wifi.conf and a
 * program asks NETIOC_STATUS whether it has an address; WEB CONNECT
 * raises "WIFI not connected" until link is 3.  A PC's network is the
 * operating system's affair, so this answers with what the machine
 * has: the interface the default route leaves by, its address and
 * mask, the gateway, the interface's hardware address, and the
 * nameservers from /etc/resolv.conf.  No default route, no link -
 * which is honest for a machine that is offline.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "pico_ioctl.h"
#include "pc3d.h"

/* the interface and gateway of the default route, from /proc/net/route:
 * Iface Destination Gateway Flags ... Mask, the addresses as the
 * 32-bit word in memory printed in hex, i.e. network order */
static int default_route(char *ifname, size_t n, uint32_t *gw)
{
	FILE *f = fopen("/proc/net/route", "r");
	char line[512];
	int found = 0;

	if (!f)
		return 0;
	while (fgets(line, sizeof line, f)) {
		char name[64];
		unsigned long dest, gate;
		unsigned flags;

		if (sscanf(line, "%63s %lx %lx %x", name, &dest, &gate, &flags) != 4)
			continue;
		if (dest == 0 && (flags & 1)) {		/* RTF_UP */
			snprintf(ifname, n, "%s", name);
			*gw = ntohl((uint32_t)gate);
			found = 1;
			break;
		}
	}
	fclose(f);
	return found;
}

static void mac_of(const char *ifname, uint8_t mac[6])
{
	char path[128];
	FILE *f;
	unsigned m[6];

	snprintf(path, sizeof path, "/sys/class/net/%s/address", ifname);
	f = fopen(path, "r");
	if (!f)
		return;
	if (fscanf(f, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
		int i;
		for (i = 0; i < 6; i++)
			mac[i] = (uint8_t)m[i];
	}
	fclose(f);
}

static void nameservers(uint32_t dns[2])
{
	FILE *f = fopen("/etc/resolv.conf", "r");
	char line[512];
	int n = 0;

	if (!f)
		return;
	while (n < 2 && fgets(line, sizeof line, f)) {
		char word[64], addr[64];
		struct in_addr a;

		if (sscanf(line, " %63s %63s", word, addr) == 2 &&
		    strcmp(word, "nameserver") == 0 && inet_aton(addr, &a))
			dns[n++] = ntohl(a.s_addr);
	}
	fclose(f);
}

void netinfo_status(struct net_status *st)
{
	char ifname[64];
	uint32_t gw = 0;
	struct ifaddrs *list, *p;

	memset(st, 0, sizeof *st);
	st->present = 1;
	st->ready = 1;
	if (!default_route(ifname, sizeof ifname, &gw))
		return;				/* link 0: not connected */
	st->gw = gw;
	mac_of(ifname, st->mac);
	if (getifaddrs(&list) == 0) {
		for (p = list; p; p = p->ifa_next) {
			if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET ||
			    strcmp(p->ifa_name, ifname) != 0)
				continue;
			st->ip = ntohl(((struct sockaddr_in *)p->ifa_addr)->sin_addr.s_addr);
			if (p->ifa_netmask)
				st->mask = ntohl(((struct sockaddr_in *)p->ifa_netmask)->sin_addr.s_addr);
			break;
		}
		freeifaddrs(list);
	}
	if (st->ip)
		st->link = 3;			/* cyw43's "has an IP" */
	nameservers(st->dns);
}
