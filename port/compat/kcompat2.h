/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Second batch of compat definitions, added incrementally while bringing up net/*.h and rtw89. */
#ifndef KCOMPAT2_H
#define KCOMPAT2_H
#include "kcompat.h"

static inline int skb_network_offset(const struct sk_buff *s) { return 0; }
static inline int skb_transport_offset(const struct sk_buff *s) { return 0; }
#define ETHTOOL_FWVERS_LEN 32
#define NETDEV_ALIGN 32
typedef struct { struct net *net; } possible_net_t;
#define read_pnet(p) ((p)->net)
#define write_pnet(p, n) do { (p)->net = (n); } while (0)
#define DEFINE_GUARD(...)
#define DEFINE_LOCK_GUARD_1(...)
#define DEFINE_GUARD_COND(...)
#define DEFINE_CLASS(...)
#define NETIF_F_GSO_SOFTWARE 0
#define MAX_ADDR_LEN 32
#define NL_SET_ERR_MSG(e, m) do {} while (0)
#define NL_SET_ERR_MSG_MOD(e, m) do {} while (0)
#define NL_SET_ERR_MSG_ATTR(e, a, m) do {} while (0)
struct netlink_ext_ack { const char *_msg; };
#define ARPHRD_IEEE80211_RADIOTAP 803


#define __attribute_const__ __attribute__((const))
#define CLOCK_BOOTTIME 7
#define HRTIMER_MODE_REL 1
#define hrtimer_setup(...) do {} while (0)
#define netif_running(d) 0
static inline u64 ether_addr_to_u64(const u8 *a) { u64 u = 0; for (int i = 0; i < 6; i++) u = u << 8 | a[i]; return u; }
static inline void u64_to_ether_addr(u64 u, u8 *a) { for (int i = 5; i >= 0; i--) { a[i] = u & 0xff; u >>= 8; } }
enum rfkill_hard_block_reasons { RFKILL_HARD_BLOCK_SIGNAL = 1, RFKILL_HARD_BLOCK_NOT_OWNER = 2 };
#define rfkill_pause_polling(r) do {} while (0)
#define rfkill_resume_polling(r) do {} while (0)
#define memset_after(obj, v, member) memset((u8 *)(obj) + offsetofend(__typeof__(*(obj)), member), v, sizeof(*(obj)) - offsetofend(__typeof__(*(obj)), member))
typedef u64 netdev_features_t;
#define NETIF_F_HW_CSUM 1
#define NETIF_F_SG 2
#define NETIF_F_RXCSUM 4
#define NETIF_F_IP_CSUM 8
#define NETIF_F_IPV6_CSUM 16
#define NETIF_F_TSO 32
#define NETIF_F_TSO6 64
#define NETIF_F_GRO 128
#define NETIF_F_LLTX 256
#define NETIF_F_HW_VLAN_CTAG_TX 512

#define BUILD_BUG_ON_NOT_POWER_OF_2(n) BUILD_BUG_ON(((n) & ((n) - 1)) != 0)
#define BITS_TO_BYTES(n) (((n) + 7) / 8)
static inline void kc_guard_noop(void) {}
#define guard(x) kc_guard_noop
#define scoped_guard(...) for (int _sg = 0; _sg < 1; _sg++)
typedef struct { atomic_t refs; } refcount_t;
#define refcount_set(r, n) atomic_set(&(r)->refs, n)
#define refcount_read(r) atomic_read(&(r)->refs)
#define refcount_inc(r) atomic_inc(&(r)->refs)
#define refcount_dec(r) atomic_dec(&(r)->refs)
#define refcount_dec_and_test(r) atomic_dec_and_test(&(r)->refs)
#define refcount_inc_not_zero(r) atomic_inc_not_zero(&(r)->refs)
#define REFCOUNT_INIT(n) { ATOMIC_INIT(n) }
enum led_brightness { LED_OFF = 0, LED_ON = 1, LED_HALF = 127, LED_FULL = 255 };
struct led_classdev { const char *name; int dummy; };
struct led_classdev_mc { struct led_classdev led_cdev; int num_colors; void *subled_info; };
struct mc_subled { int color_index; int brightness; int intensity; int channel; };

#define atomic64_inc_return(v) __atomic_add_fetch(&(v)->counter, 1, __ATOMIC_SEQ_CST)
#define umin(a, b) ((unsigned long long)(a) < (unsigned long long)(b) ? (unsigned long long)(a) : (unsigned long long)(b))
#define umax(a, b) ((unsigned long long)(a) > (unsigned long long)(b) ? (unsigned long long)(a) : (unsigned long long)(b))
#define round_jiffies_relative(x) (x)
#define round_jiffies(x) (x)
#define round_jiffies_up(x) (x)
#define div_u64_rem(a, b, rem) ({ u32 _d = (b); *(rem) = (u32)((u64)(a) % _d); (u64)(a) / _d; })
static inline void *memchr_inv(const void *s, int c, size_t n) { const u8 *p = s; for (size_t i = 0; i < n; i++) if (p[i] != (u8)c) return (void *)(p + i); return NULL; }
static inline void list_splice_tail(struct list_head *l, struct list_head *h) { if (!list_empty(l)) { struct list_head *f = l->next, *la = l->prev, *at = h->prev; f->prev = at; at->next = f; la->next = h; h->prev = la; } }
static inline void skb_queue_splice(struct sk_buff_head *l, struct sk_buff_head *h) { struct sk_buff *s; struct sk_buff *tmp; (void)tmp; while ((s = __skb_dequeue_tail(l))) __skb_queue_head(h, s); }
#define dev_info_once(...) do {} while (0)
#define dev_warn_ratelimited dev_warn
#define dev_err_ratelimited dev_err
#define field_get(mask, v) (((v) & (mask)) >> __builtin_ctzll(mask))
#define field_prep(mask, v) (((u64)(v) << __builtin_ctzll(mask)) & (mask))
static struct net_device kc_dummy_netdev;
#define alloc_netdev_dummy(n) (&kc_dummy_netdev)
#define free_netdev(d) do {} while (0)
#define netif_napi_add(d, n, p) do {} while (0)
#define netif_napi_add_weight(d, n, p, w) do {} while (0)
#define netif_napi_del(n) do {} while (0)
#define napi_enable(n) do {} while (0)
#define napi_disable(n) do {} while (0)
#define napi_synchronize(n) do {} while (0)
#define napi_schedule(n) do {} while (0)
#define napi_complete_done(n, w) true
#define napi_complete(n) true
#define napi_is_scheduled(n) false
#define napi_schedule_prep(n) false
#define __napi_schedule(n) do {} while (0)

#define devm_kmemdup(d, s, n, g) kmemdup(s, n, g)
struct dmi_system_id { const char *ident; void *driver_data; };
#define dmi_first_match(t) ((const struct dmi_system_id *)NULL)
#define roundup_u64(x, y) ((((u64)(x) + (y) - 1) / (y)) * (y))
#define __test_and_set_bit test_and_set_bit
#define __test_and_clear_bit test_and_clear_bit
#define le16_add_cpu(p, v) do { *(p) = (u16)(*(p) + (v)); } while (0)
#define le32_add_cpu(p, v) do { *(p) = (u32)(*(p) + (v)); } while (0)
#define FIELD_GET_SIGNED(mask, v) ({ u64 _fv = ((u64)(v) & (mask)) >> __builtin_ctzll(mask); int _fw = __builtin_popcountll(mask); (s64)(_fv << (64 - _fw)) >> (64 - _fw); })
static inline void bitmap_or(unsigned long *d, const unsigned long *a, const unsigned long *b, unsigned int n) { for (unsigned i = 0; i < BITS_TO_LONGS(n); i++) d[i] = a[i] | b[i]; }
static inline void bitmap_and(unsigned long *d, const unsigned long *a, const unsigned long *b, unsigned int n) { for (unsigned i = 0; i < BITS_TO_LONGS(n); i++) d[i] = a[i] & b[i]; }
#define struct_size_t(type, member, n) (sizeof(type) + sizeof(((type *)0)->member[0]) * (size_t)(n))
static inline void skb_reset_tail_pointer(struct sk_buff *s) { s->tail = s->data; }
#define ECOMM 70
#define WQ_BH 8
#define WQ_PERCPU 16
struct attribute { const char *name; };
struct device_attribute { struct attribute attr; ssize_t (*show)(struct device *, struct device_attribute *, char *); };
#define DEVICE_ATTR_RO(n) struct device_attribute dev_attr_##n = { .attr = { #n }, .show = n##_show }
struct attribute_group { const char *name; struct attribute **attrs; bool (*is_visible)(struct kobject *, struct attribute *, int); };
#define __ATTRIBUTE_GROUPS(n) static const struct attribute_group *n##_groups[] = { &n##_group, NULL }
#define DEFINE_SIMPLE_SYSFS_GROUP_VISIBLE(n)
#define SYSFS_GROUP_VISIBLE(n) NULL
#define kobj_to_dev(k) ((struct device *)(k))
#endif
