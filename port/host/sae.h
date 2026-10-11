#ifndef SAE_H
#define SAE_H
#include <stdint.h>
#include <stdbool.h>
#include "ecp256.h"

struct sae {
	uint8_t mac_a[6], mac_b[6];		/* own address (a), peer address (b) */
	const char *password;
	ecpt pwe;
	bn rand, mask, scalar, peer_scalar;
	ecpt element, peer_element;
	uint8_t kck[32], pmk[32], pmkid[16];
	uint16_t send_confirm;
	bool have_commit, have_keys;
};

int sae_init(struct sae *s, const uint8_t own[6], const uint8_t peer[6], const char *password);
void sae_commit(struct sae *s, uint8_t out[96]);			/* scalar(32) || element x(32) || y(32), big-endian */
int sae_process_commit(struct sae *s, const uint8_t peer[96]);	/* derives kck/pmk/pmkid */
void sae_confirm(struct sae *s, uint8_t out[32]);
int sae_check_confirm(struct sae *s, uint16_t peer_send_confirm, const uint8_t conf[32]);
#endif
