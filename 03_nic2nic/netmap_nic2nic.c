#include <poll.h>
#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

struct nm_desc *nm_desc1, *nm_desc2;

static void
swapto(struct nm_desc *desc, struct netmap_slot *rxslot)
{
	struct netmap_ring *txring;
	int i;
	uint32_t t, cur;

	for (i = desc->first_tx_ring; i <= desc->last_tx_ring; i++) {
		txring = NETMAP_TXRING(desc->nifp, i);
		if (nm_ring_empty(txring))
			continue;

		cur = txring->cur;

		/* swap buf_idx */
		t = txring->slot[cur].buf_idx;
		txring->slot[cur].buf_idx = rxslot->buf_idx;
		rxslot->buf_idx = t;

		/* set len */
		txring->slot[cur].len = rxslot->len;
		if (txring->slot[cur].len < 64)
			txring->slot[cur].len = 64;

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
	unsigned int cur, n, i;
	struct netmap_ring *rxring;
	struct pollfd pollfd[2];

	fprintf(stderr, "in advance, 'ifconfig igb2 promisc' and 'ifconfig igb3 promisc' for bridge\n");
	nm_desc1 = nm_open("netmap:igb2", NULL, 0, NULL);
	nm_desc2 = nm_open("netmap:igb4", NULL, NM_OPEN_NO_MMAP, nm_desc1);

	for (;;) {
		pollfd[0].fd = nm_desc1->fd;
		pollfd[0].events = POLLIN;
		pollfd[1].fd = nm_desc2->fd;
		pollfd[1].events = POLLIN;
		poll(pollfd, 2, 500);

		/* from nm_desc1 to nm_desc2 */
		for (i = nm_desc1->first_rx_ring; i <= nm_desc1->last_rx_ring; i++) {
			rxring = NETMAP_RXRING(nm_desc1->nifp, i);
			cur = rxring->cur;
			for (n = nm_ring_space(rxring); n > 0; n--, cur = nm_ring_next(rxring, cur)) {
				swapto(nm_desc2, &rxring->slot[cur]);
			}
			rxring->head = rxring->cur = cur;
		}

		/* from nm_desc2 to nm_desc1 */
		for (i = nm_desc2->first_rx_ring; i <= nm_desc2->last_rx_ring; i++) {
			rxring = NETMAP_RXRING(nm_desc2->nifp, i);
			cur = rxring->cur;
			for (n = nm_ring_space(rxring); n > 0; n--, cur = nm_ring_next(rxring, cur)) {
				swapto(nm_desc1, &rxring->slot[cur]);
			}
			rxring->head = rxring->cur = cur;
		}
	}
}
