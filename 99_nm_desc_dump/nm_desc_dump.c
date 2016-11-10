#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <err.h>

#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

struct nm_desc *nm_desc;
int dflag;

static void
usage(void)
{
	fprintf(stderr, "nm_desc_dump [-d] <netmap|vale>:<interface>[<suffix>]\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	struct netmap_ring *ring;
	int i, j, ch;

	while ((ch = getopt(argc, argv, "d")) != -1) {
		switch (ch) {
		case 'd':
			dflag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	nm_desc = nm_open(argv[0], NULL, 0, NULL);
	if (nm_desc == NULL)
		err(1, "nm_open");

	printf("nm_desc->fd            = %d\n", nm_desc->fd);
	printf("nm_desc->mem           = 0x%016llx\n", (unsigned long long)nm_desc->mem);
	printf("nm_desc->memsize       = 0x%016llx\n", (unsigned long long)nm_desc->memsize);

	printf("nm_desc->buf_start     = 0x%016llx\n", (unsigned long long)nm_desc->buf_start);
	printf("nm_desc->buf_end       = 0x%016llx\n", (unsigned long long)nm_desc->buf_end);

	printf("nm_desc->first_tx_ring = %d\n", nm_desc->first_tx_ring);
	printf("nm_desc->last_tx_ring  = %d\n", nm_desc->last_tx_ring);
	printf("nm_desc->cur_tx_ring   = %d\n", nm_desc->cur_tx_ring);

	printf("nm_desc->first_rx_ring = %d\n", nm_desc->first_rx_ring);
	printf("nm_desc->last_rx_ring  = %d\n", nm_desc->last_rx_ring);
	printf("nm_desc->cur_rx_ring   = %d\n", nm_desc->cur_rx_ring);

	printf("nm_desc->req.nr_name   = %s\n", nm_desc->req.nr_name);
	printf("nm_desc->req.nr_flags  = 0x%08x\n", nm_desc->req.nr_flags);
	printf("nm_desc->req.nr_ringid = 0x%08x\n", nm_desc->req.nr_ringid);

	printf("nm_desc->nifp              = %p\n", nm_desc->nifp);
	printf("nm_desc->nifp->ni_name     = %s\n", nm_desc->nifp->ni_name);
	printf("nm_desc->nifp->ni_version  = %d\n", nm_desc->nifp->ni_version);
	printf("nm_desc->nifp->ni_flags    = 0x%08x\n", nm_desc->nifp->ni_flags);
	printf("nm_desc->nifp->ni_tx_rings = %u\n", nm_desc->nifp->ni_tx_rings);
	printf("nm_desc->nifp->ni_rx_rings = %u\n", nm_desc->nifp->ni_rx_rings);

	printf("\n");

	for (i = nm_desc->first_tx_ring; i <= nm_desc->last_tx_ring; i++) {
		ring = NETMAP_TXRING(nm_desc->nifp, i);
		printf("TXring[%d].buf_ofs     = +0x%llx (0x%llx)\n", i,
		    (unsigned long long)ring->buf_ofs,
		    (unsigned long long)((char *)ring + ring->buf_ofs));
		printf("TXring[%d].num_slots   = %u\n", i, ring->num_slots);
		printf("TXring[%d].nr_buf_size = %u\n", i, ring->nr_buf_size);
		printf("TXring[%d].ringid      = %u\n", i, ring->ringid);
		printf("TXring[%d].dir         = %u\n", i, ring->dir);
		printf("TXring[%d].head        = %u\n", i, ring->head);
		printf("TXring[%d].cur         = %u\n", i, ring->cur);
		printf("TXring[%d].tail        = %u\n", i, ring->tail);
		printf("TXring[%d].flags       = %x\n", i, ring->flags);
		if (dflag) {
			for (j = 0; j < ring->num_slots; j++) {
				printf("TXring[%d].slot[%d].buf_idx = %u\n", i,
				    j, ring->slot[j].buf_idx);
			}
		}
	}
	for (i = nm_desc->first_rx_ring; i <= nm_desc->last_rx_ring; i++) {
		ring = NETMAP_RXRING(nm_desc->nifp, i);
		printf("RXring[%d].buf_ofs     = +0x%llx (0x%llx)\n", i,
		    (unsigned long long)ring->buf_ofs,
		    (unsigned long long)((char *)ring + ring->buf_ofs));
		printf("RXring[%d].num_slots   = %u\n", i, ring->num_slots);
		printf("RXring[%d].nr_buf_size = %u\n", i, ring->nr_buf_size);
		printf("RXring[%d].ringid      = %u\n", i, ring->ringid);
		printf("RXring[%d].dir         = %u\n", i, ring->dir);
		printf("RXring[%d].head        = %u\n", i, ring->head);
		printf("RXring[%d].cur         = %u\n", i, ring->cur);
		printf("RXring[%d].tail        = %u\n", i, ring->tail);
		printf("RXring[%d].flags       = %x\n", i, ring->flags);
		if (dflag) {
			for (j = 0; j < ring->num_slots; j++) {
				printf("RXrings[%d].slot[%d].buf_idx = %u\n", i,
				    j, ring->slot[j].buf_idx);
			}
		}
	}

	return 0;
}
