#ifndef PEND_H
#define PEND_H
/* packets waiting for ARP / neighbour resolution (never block the TX thread) */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#define PEND_MAX 16
struct pend {
	int used;
	unsigned char key[16];		/* next-hop address (v4 uses the first 4 bytes) */
	unsigned char src[16];
	unsigned char *pkt;
	size_t len;
	int tries;
	uint64_t last_ms;
};
static inline uint64_t pend_now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000; }
#endif
