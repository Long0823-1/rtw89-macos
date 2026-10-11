#ifndef NET_H
#define NET_H
#include <stddef.h>
typedef int (*net_tx_fn)(const unsigned char *da, unsigned short ethertype, const unsigned char *payload, size_t len);
void net_init(net_tx_fn tx, const unsigned char mac[6]);
void net_rx_eth(const unsigned char *eth, size_t len);
/* runs DHCP, creates utun, configures addressing/routes/DNS; returns 0 on success */
int net_up(int timeout_s, int set_default);
void net_down(void);
int net_is_up(void);
void net_set_default(int on);
int net_bench(int seconds, int size, char *out, size_t outn);
void net_get_ip(char *buf, size_t n);
extern void (*net_on_lease_lost)(void);
#endif
