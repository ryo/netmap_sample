#include <sys/param.h>
#include <unistd.h>
#include <poll.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <libutil.h>
#include <sys/sbuf.h>
#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

struct nm_desc *nm_desc;


struct filterrule {
	int dir;
	int proto;
	struct in_addr srcaddr, srcaddr_mask;
	struct in_addr dstaddr, dstaddr_mask;
	int srcport;
	int dstport;
} filterrule[] = {
	{
		.dir          = 0,		/* 0:in, 1:out */
		.proto        = IPPROTO_TCP,
		.srcaddr      = { 0x0a000000 },	/* 10.0.0.0 */
		.srcaddr_mask = { 0xff000000 },	/* 255.0.0.0 */
		.dstaddr      = { 0x00000000 },	/* 0.0.0.0 */
		.dstaddr_mask = { 0x00000000 },	/* 0.0.0.0 */
		.srcport      = -1,		/* any */
		.dstport      = 80
	},
	{
		.dir          = 1,		/* 0:in, 1:out */
		.proto        = IPPROTO_ICMP,
		.srcaddr      = { 0x0a000000 },	/* 10.0.0.0 */
		.srcaddr_mask = { 0xffff0000 },	/* 255.255.0.0 */
		.dstaddr      = { 0x00000000 },	/* 0.0.0.0 */
		.dstaddr_mask = { 0x00000000 },	/* 0.0.0.0 */
	},
	/* ... */
};


/*
 * dir: 0 = in  = NIC to HOST
 *      1 = out = HOST to NIC
 * buf: packet data;
 * len: packet length
 */
static int
packetfilter(int dir, void *buf, unsigned int len)
{
	char *payload;
	struct ether_header *ether;
	struct ip *ip;
	struct tcphdr *tcp;
	struct udphdr *udp;
	int i;

	ether = (struct ether_header *)buf;
	ip = (struct ip *)(ether + 1);
	payload = (char *)ip + ip->ip_hl * 4;

	if (ip->ip_v == IPVERSION) {
		for (i = 0; i < nitems(filterrule); i++) {
			if (filterrule[i].dir != dir)
				continue;

			/* test protocol */
			if ((filterrule[i].proto != -1) && (filterrule[i].proto != ip->ip_p))
				continue;

			/* test src addr */
			if ((filterrule[i].srcaddr.s_addr & filterrule[i].srcaddr_mask.s_addr) !=
			    (ntohl(ip->ip_src.s_addr) & filterrule[i].srcaddr_mask.s_addr))
				continue;

			/* test dst addr */
			if ((filterrule[i].dstaddr.s_addr & filterrule[i].dstaddr_mask.s_addr) !=
			    (ntohl(ip->ip_dst.s_addr) & filterrule[i].dstaddr_mask.s_addr))
				continue;

			/* test src/dst port */
			if (ip->ip_p == IPPROTO_TCP) {
				tcp = (struct tcphdr *)payload;
				if ((filterrule[i].srcport != -1) &&
				    ntohs(tcp->th_sport) != filterrule[i].srcport)
					continue;
				if ((filterrule[i].dstport != -1) &&
				    ntohs(tcp->th_dport) != filterrule[i].dstport)
					continue;
			} else if (ip->ip_p == IPPROTO_UDP) {
				udp = (struct udphdr *)payload;
				if ((filterrule[i].srcport != -1) &&
				    ntohs(udp->uh_sport) != filterrule[i].srcport)
					continue;
				if ((filterrule[i].dstport != -1) &&
				    ntohs(udp->uh_dport) != filterrule[i].dstport)
					continue;
			}

			/* matched */
			return 1;
		}
	}

	return 0;
}

static void
swapto(int to_hostring, struct netmap_slot *rxslot)
{
	struct netmap_ring *txring;
	int i, first, last;
	uint32_t t, cur;

	if (to_hostring) {
		first = last = nm_desc->last_tx_ring;
	} else {
		first = nm_desc->first_tx_ring;
		last = nm_desc->last_tx_ring - 1;
	}

	for (i = first; i <= last; i++) {
		txring = NETMAP_TXRING(nm_desc->nifp, i);
		if (nm_ring_empty(txring))
			continue;

		cur = txring->cur;

		/* swap buf_idx */
		t = txring->slot[cur].buf_idx;
		txring->slot[cur].buf_idx = rxslot->buf_idx;
		rxslot->buf_idx = t;

		/* set len */
		txring->slot[cur].len = rxslot->len;

		/* update flags */
		txring->slot[cur].flags |= NS_BUF_CHANGED;
		rxslot->flags |= NS_BUF_CHANGED;

		/* update ring pointer */
		cur = nm_ring_next(txring, cur);
		txring->head = txring->cur = cur;

		break;
	}
}

int
main(int argc, char *argv[])
{
	unsigned int cur, n, i, is_hostring;
	struct netmap_ring *rxring;
	struct pollfd pollfd[1];

#define NM_IFNAME	"netmap:igb2*"
	/* "igb2*" - open NIC-ring and SW-ring */
	nm_desc = nm_open(NM_IFNAME, NULL, 0, NULL);

	for (;;) {
		pollfd[0].fd = nm_desc->fd;
		pollfd[0].events = POLLIN;
		poll(pollfd, 1, 100);

		for (i = nm_desc->first_rx_ring; i <= nm_desc->last_rx_ring; i++) {
			/* last ring is host ring */
			is_hostring = (i == nm_desc->last_rx_ring);

			rxring = NETMAP_RXRING(nm_desc->nifp, i);
			cur = rxring->cur;
			for (n = nm_ring_space(rxring); n > 0; n--, cur = nm_ring_next(rxring, cur)) {

				/* test packet. block when return !0 */
				if (packetfilter(is_hostring, NETMAP_BUF(rxring, rxring->slot[cur].buf_idx), rxring->slot[cur].len)) {
					printf("BLOCK:\n");
					hexdump(NETMAP_BUF(rxring, rxring->slot[cur].buf_idx), rxring->slot[cur].len, "  ", 0);
					continue;	/* discard */
				}

				swapto(!is_hostring, &rxring->slot[cur]);
			}
			rxring->head = rxring->cur = cur;
		}
	}
}
