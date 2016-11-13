#include <pthread.h>
#include <poll.h>
#include <err.h>
#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

#define MAXTHREAD	32
struct threadwork {
	int no;
	pthread_t thread;
	struct nm_desc *nm_desc_nic;
	struct nm_desc *nm_desc_host;
} threadwork[MAXTHREAD];

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

void *
pthread_main(void *arg)
{
	struct threadwork *work;
	struct pollfd pollfd[2];
	struct netmap_ring *rxring;
	unsigned int i, n, cur;

	work = (struct threadwork *)arg;

	printf("thread[%d] nm_desc_nic  = %p\n", work->no, work->nm_desc_nic);
	printf("thread[%d] nm_desc_host = %p\n", work->no, work->nm_desc_host);
	fflush(stdout);

	for (;;) {
		pollfd[0].fd = work->nm_desc_nic->fd;
		pollfd[0].events = POLLIN;
		if (work->no == 0) {
			/* only one thread forwards from HOST to NIC */
			pollfd[1].fd = work->nm_desc_host->fd;
			pollfd[1].events = POLLIN;
			poll(pollfd, 2, 100);
		} else {
			/* on other threads, no need to forward from HOST to NIC */
			poll(pollfd, 1, 100);
		}

		if (work->no == 0) {
			/* from HOST to NIC */
			for (i = work->nm_desc_host->first_rx_ring; i <= work->nm_desc_host->last_rx_ring; i++) {
				rxring = NETMAP_RXRING(work->nm_desc_host->nifp, i);
				cur = rxring->cur;
				for (n = nm_ring_space(rxring); n > 0; n--, cur = nm_ring_next(rxring, cur)) {
					swapto(work->nm_desc_nic, &rxring->slot[cur]);
				}
				rxring->head = rxring->cur = cur;
			}
		}

		/* from NIC to HOST */
		for (i = work->nm_desc_nic->first_rx_ring; i <= work->nm_desc_nic->last_rx_ring; i++) {
			rxring = NETMAP_RXRING(work->nm_desc_nic->nifp, i);
			cur = rxring->cur;
			for (n = nm_ring_space(rxring); n > 0; n--, cur = nm_ring_next(rxring, cur)) {
				swapto(work->nm_desc_host, &rxring->slot[cur]);
			}
			rxring->head = rxring->cur = cur;
		}
	}

	pthread_exit(NULL);
}

int
main(int argc, char *argv[])
{
	unsigned int nic_ring_num;
	struct nm_desc *nm_desc;
	char buf[128];
	int i;

	if (argc != 2) {
		fprintf(stderr, "usage: netmap_multiqueue <ifname>\n");
		exit(1);
	}

	snprintf(buf, sizeof(buf), "netmap:%s", argv[1]);
	nm_desc = nm_open(buf, NULL, 0, NULL);
	nic_ring_num = nm_desc->nifp->ni_rx_rings;
	nm_close(nm_desc);

	if (nic_ring_num > MAXTHREAD) {
		fprintf(stderr, "too many nic rings. increase MAXTHREAD\n");
		exit(1);
	}

	printf("%s has %d RX rings\n", argv[1], nic_ring_num);

	for (i = 0; i < nic_ring_num; i++) {
		threadwork[i].no = i;

		snprintf(buf, sizeof(buf), "netmap:%s^", argv[1]);
		threadwork[i].nm_desc_host = nm_open(buf, NULL, 0, NULL);
		if (threadwork[i].nm_desc_host == NULL)
			err(1, "nm_open: %s", buf);

		snprintf(buf, sizeof(buf), "netmap:%s-%d", argv[1], i);
		threadwork[i].nm_desc_nic = nm_open(buf, NULL,
		    NM_OPEN_NO_MMAP, threadwork[i].nm_desc_host);
		if (threadwork[i].nm_desc_nic== NULL)
			err(1, "nm_open: %s", buf);

		printf("create thread for %s\n", buf);
		pthread_create(&threadwork[i].thread, NULL,
		    pthread_main, &threadwork[i]);
	}

	for (i = 0; i < nic_ring_num; i++)
		pthread_join(threadwork[i].thread, NULL);

	return 0;
}
