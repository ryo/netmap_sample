#include <stdio.h>
#include <poll.h>
#include <libutil.h>
#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

struct nm_desc *nm_desc;
struct nmreq nmreq;

int
main(int argc, char *argv[])
{
	unsigned int cur, n, i;
	struct netmap_ring *rxring;
	struct pollfd pollfd[1];

	nm_desc = nm_open("netmap:igb0", &nmreq, 0, NULL);
	for (;;) {
		pollfd[0].fd = nm_desc->fd;
		pollfd[0].events = POLLIN;
		poll(pollfd, 1, 100);

		for (i = nm_desc->first_rx_ring; i <= nm_desc->last_rx_ring; i++) {
			rxring = NETMAP_RXRING(nm_desc->nifp, i);
			cur = rxring->cur;
			for (n = nm_ring_space(rxring); n > 0; n--, cur = nm_ring_next(rxring, cur)) {
				hexdump(NETMAP_BUF(rxring, rxring->slot[cur].buf_idx), rxring->slot[cur].len, NULL, 0);
			}
			rxring->head = rxring->cur = cur;
		}
	}
}
