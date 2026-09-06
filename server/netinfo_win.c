/*
 * netinfo_win.c - NETIOC_STATUS for a Windows machine whose network is
 * already up: netinfo.c's answer, from the IP helper API.
 *
 * The interface is the one the default route leaves by (GetBestRoute
 * towards 8.8.8.8, which asks the routing table without sending
 * anything); its address, mask, gateway, hardware address and DNS
 * servers come from GetAdaptersAddresses.  No default route, no link.
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico_ioctl.h"
#include "pc3d.h"

static uint32_t v4(const struct sockaddr *sa)
{
	if (!sa || sa->sa_family != AF_INET)
		return 0;
	return ntohl(((const struct sockaddr_in *)sa)->sin_addr.s_addr);
}

void netinfo_status(struct net_status *st)
{
	MIB_IPFORWARDROW route;
	IP_ADAPTER_ADDRESSES *list = NULL, *a;
	ULONG len = 0;
	DWORD ifindex;
	int n = 0;

	memset(st, 0, sizeof *st);
	st->present = 1;
	st->ready = 1;
	if (GetBestRoute(htonl(0x08080808u), 0, &route) != NO_ERROR)
		return;				/* link 0: not connected */
	ifindex = route.dwForwardIfIndex;
	st->gw = ntohl(route.dwForwardNextHop);

	if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
				 NULL, NULL, &len) != ERROR_BUFFER_OVERFLOW)
		return;
	list = malloc(len);
	if (!list)
		return;
	if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
				 NULL, list, &len) != NO_ERROR) {
		free(list);
		return;
	}
	for (a = list; a; a = a->Next) {
		IP_ADAPTER_UNICAST_ADDRESS *u;
		IP_ADAPTER_DNS_SERVER_ADDRESS *d;

		if (a->IfIndex != ifindex)
			continue;
		if (a->PhysicalAddressLength >= 6)
			memcpy(st->mac, a->PhysicalAddress, 6);
		for (u = a->FirstUnicastAddress; u; u = u->Next) {
			uint32_t ip = v4(u->Address.lpSockaddr);
			if (ip) {
				st->ip = ip;
				st->mask = u->OnLinkPrefixLength >= 32 ? 0xFFFFFFFFu
					 : ~(0xFFFFFFFFu >> u->OnLinkPrefixLength);
				break;
			}
		}
		for (d = a->FirstDnsServerAddress; d && n < 2; d = d->Next) {
			uint32_t ip = v4(d->Address.lpSockaddr);
			if (ip)
				st->dns[n++] = ip;
		}
		break;
	}
	free(list);
	if (st->ip)
		st->link = 3;			/* cyw43's "has an IP" */
}
