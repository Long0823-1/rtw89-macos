#ifndef NET6_H
#define NET6_H
#include <stddef.h>
#include "net.h"
typedef void (*net6_write_fn)(const unsigned char *ip6, size_t len);
void net6_init(net_tx_fn tx, const unsigned char mac[6], const char *ifname, net6_write_fn wr);
void net6_tx(const unsigned char *ip6, size_t len);
void net6_rx(const unsigned char *eth, size_t len);
void net6_send_rs(void);
void net6_tick(void);
typedef void (*net6_prefix_fn)(const unsigned char *prefix, int plen, const unsigned char *router_ll);
void net6_set_prefix_cb(net6_prefix_fn cb);
#endif
