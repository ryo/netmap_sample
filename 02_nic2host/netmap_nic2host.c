#include <poll.h>
#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

struct nm_desc *nm_desc;

static void
swapto(int to_hostring, struct netmap_slot *rxslot)
{
	struct netmap_ring *txring;
	int i, first, last;
	uint32_t t, cur;

	if (to_hostring) {
		fprintf(stderr, "NIC to HOST\n");
		first = last = nm_desc->last_tx_ring;
	} else {
		fprintf(stderr, "HOST to NIC\n");
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
	struct netmap_ring *rxring, *txring;
	struct pollfd pollfd[1];

	/* "igb2*" - open NIC-ring and SW-ring */
	nm_desc = nm_open("netmap:igb2*", NULL, 0, NULL);

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
				swapto(!is_hostring, &rxring->slot[cur]);
			}
			rxring->head = rxring->cur = cur;
		}
	}
}
