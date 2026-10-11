/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Minimal Linux kernel API compatibility layer for building rtw89 in macOS userspace. */
#ifndef KCOMPAT_H
#define KCOMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <arpa/inet.h>

#ifndef ENOTSUPP
#define ENOTSUPP 524
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP 45
#endif

/* ---- basic types ---- */
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef uint8_t __u8; typedef uint16_t __u16; typedef uint32_t __u32; typedef uint64_t __u64;
typedef int8_t __s8; typedef int16_t __s16; typedef int32_t __s32; typedef int64_t __s64;
typedef uint16_t __le16; typedef uint32_t __le32; typedef uint64_t __le64;
typedef uint16_t __be16; typedef uint32_t __be32; typedef uint64_t __be64;
typedef uint16_t __sum16; typedef uint32_t __wsum;
typedef int64_t ktime_t;
typedef unsigned int gfp_t;
typedef unsigned int fmode_t;
typedef long long loff_t;
typedef unsigned long long phys_addr_t;
typedef unsigned long long dma_addr_t;
typedef unsigned long uintptr_t_k;
typedef int8_t __s8_k;
#define __bitwise
#define __force
#define __user
#define __iomem
#define __rcu
#define __percpu
#define __must_check
#define __init
#define __exit
#define __initdata
#define __ro_after_init
#define __read_mostly
#define __cold
#define __visible
#define __always_unused __attribute__((unused))
#define __maybe_unused __attribute__((unused))
#define __must_hold(x)
#define __acquires(x)
#define __releases(x)
#define __acquire(x) (void)0
#define __release(x) (void)0
#define __counted_by(x)
#define __counted_by_le(x)
#define __counted_by_be(x)
#define __no_randomize_layout
#define __nonstring
#define __printf(a, b) __attribute__((format(printf, a, b)))
#define __pure __attribute__((pure))
#define noinline __attribute__((noinline))
#undef __packed
#define __packed __attribute__((packed))
#undef __aligned
#define __aligned(x) __attribute__((aligned(x)))
#define __always_inline inline __attribute__((always_inline))
#define __deprecated
#define __devinit
#define __devexit
#define fallthrough __attribute__((fallthrough))
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define __bf_cast_unsigned(type, x) ((type)(x))
#define typeof_member(T, m) typeof(((T *)0)->m)
#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))
#define static_assert _Static_assert
#define BUILD_BUG_ON(c) ((void)sizeof(char[1 - 2 * !!(c)]))
#define BUILD_BUG_ON_MSG(c, m) BUILD_BUG_ON(c)
#define BUILD_BUG_ON_ZERO(e) ((int)sizeof(struct { int : (-!!(e)); }))
#define BUILD_BUG() __builtin_trap()
#define BITS_PER_LONG 64
#define BITS_PER_BYTE 8
#define BITS_PER_TYPE(t) (sizeof(t) * 8)
#define BITS_TO_LONGS(n) (((n) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define BIT(nr) (1UL << (nr))
#define BIT_ULL(nr) (1ULL << (nr))
#define BIT_MASK(nr) (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr) ((nr) / BITS_PER_LONG)
#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define GENMASK_ULL(h, l) (((~0ULL) << (l)) & (~0ULL >> (64 - 1 - (h))))
#define GENMASK_U8(h, l) ((u8)GENMASK(h, l))
#define GENMASK_U16(h, l) ((u16)GENMASK(h, l))
#define GENMASK_U32(h, l) ((u32)GENMASK(h, l))
#define BIT_U8(n) ((u8)BIT(n))
#define BIT_U16(n) ((u16)BIT(n))
#define BIT_U32(n) ((u32)BIT(n))
#define U8_MAX 255
#define S8_MAX 127
#define S8_MIN (-128)
#define U16_MAX 65535
#define S16_MAX 32767
#define S16_MIN (-32768)
#define U32_MAX 0xffffffffU
#define S32_MAX 2147483647
#define S32_MIN (-2147483647 - 1)
#define U64_MAX 0xffffffffffffffffULL
#define S64_MAX 0x7fffffffffffffffLL
#define SZ_1K 1024
#define SZ_4K 4096
#define SZ_1M (1024 * 1024)
#define HZ 1000
#define NSEC_PER_SEC 1000000000L
#define NSEC_PER_MSEC 1000000L
#define NSEC_PER_USEC 1000L
#define USEC_PER_SEC 1000000L
#define USEC_PER_MSEC 1000L
#define MSEC_PER_SEC 1000L
#define ETH_ALEN 6
#define ETH_HLEN 14
#define ETH_P_IP 0x0800
#define ETH_P_ARP 0x0806
#define ETH_P_IPV6 0x86DD
#define ETH_P_PAE 0x888E
#define ETH_P_ALL 0x0003
#define ETH_P_80221 0x8917
#define ETH_P_TDLS 0x890D
#define ETH_FRAME_LEN 1514
#define ETH_ZLEN 60
#define ETH_DATA_LEN 1500
#define ETH_P_8021Q 0x8100
#define ETH_P_PREAUTH 0x88C7
#define ETH_P_MPLS_UC 0x8847
#define ETH_P_MPLS_MC 0x8848
#define ETH_P_AARP 0x80F3
#define ETH_P_IPX 0x8137
#define ETH_P_802_3_MIN 0x0600
#define ETH_P_802_3 0x0001
#define ETH_P_802_2 0x0004
#define ETH_P_LINK_CTL 0x886c
#define ETH_P_OUI 0x88B7
#define ETH_P_TEB 0x6558
#define ETH_P_8021AD 0x88A8
#define ETH_P_802_EX1 0x88B5
#define IFNAMSIZ 16
#define NET_IP_ALIGN 0
#define NET_SKB_PAD 32
#define SMP_CACHE_BYTES 64
#define L1_CACHE_BYTES 64
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12
#define KERN_ERR ""
#define KERN_WARNING ""
#define KERN_INFO ""
#define KERN_DEBUG ""
#define KERN_CONT ""
#define IS_ENABLED(x) 0
#define IS_BUILTIN(x) 0
#define IS_MODULE(x) 0
#define CONFIG_HZ HZ
#define LINUX_VERSION_CODE 0x060f00
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))

/* ---- integer helpers ---- */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define container_of_const container_of
#define offsetofend(T, m) (offsetof(T, m) + sizeof(((T *)0)->m))
#define sizeof_field(T, m) sizeof(((T *)0)->m)
#define struct_size(p, member, n) (sizeof(*(p)) + sizeof((p)->member[0]) * (size_t)(n))
#define flex_array_size(p, member, n) (sizeof((p)->member[0]) * (size_t)(n))
#define array_size(a, b) ((size_t)(a) * (size_t)(b))
#define min(a, b) ({ __typeof__(a) _min_a = (a); __typeof__(b) _min_b = (b); _min_a < _min_b ? _min_a : _min_b; })
#define max(a, b) ({ __typeof__(a) _max_a = (a); __typeof__(b) _max_b = (b); _max_a > _max_b ? _max_a : _max_b; })
#define min_t(t, a, b) ({ t _mt_a = (a); t _mt_b = (b); _mt_a < _mt_b ? _mt_a : _mt_b; })
#define max_t(t, a, b) ({ t _xt_a = (a); t _xt_b = (b); _xt_a > _xt_b ? _xt_a : _xt_b; })
#define min3(a, b, c) min(min(a, b), c)
#define max3(a, b, c) max(max(a, b), c)
#define clamp(v, lo, hi) min((__typeof__(v))max(v, (__typeof__(v))(lo)), (__typeof__(v))(hi))
#define clamp_t(t, v, lo, hi) min_t(t, max_t(t, v, lo), hi)
#define clamp_val(v, lo, hi) clamp_t(__typeof__(v), v, lo, hi)
#define abs_k(x) ((x) < 0 ? -(x) : (x))
#undef abs
#define abs(x) ({ __typeof__(x) _abs_x = (x); _abs_x < 0 ? -_abs_x : _abs_x; })
#define swap(a, b) do { __typeof__(a) _sw = (a); (a) = (b); (b) = _sw; } while (0)
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define DIV_ROUND_UP_ULL(n, d) DIV_ROUND_UP((unsigned long long)(n), d)
#define DIV_ROUND_CLOSEST(x, d) ((((x) < 0) == ((d) < 0)) ? (((x) + ((d) / 2)) / (d)) : (((x) - ((d) / 2)) / (d)))
#define DIV_ROUND_CLOSEST_ULL DIV_ROUND_CLOSEST
#define DIV_ROUND_DOWN_ULL(n, d) ((n) / (d))
#define DIV_S64_ROUND_CLOSEST DIV_ROUND_CLOSEST
#define ALIGN(x, a) (((x) + ((__typeof__(x))(a) - 1)) & ~((__typeof__(x))(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((__typeof__(x))(a) - 1))
#define IS_ALIGNED(x, a) (((x) & ((__typeof__(x))(a) - 1)) == 0)
#define PTR_ALIGN(p, a) ((__typeof__(p))ALIGN((uintptr_t)(p), a))
#define round_up(x, y) ((((x) - 1) | ((__typeof__(x))((y) - 1))) + 1)
#define round_down(x, y) ((x) & ~((__typeof__(x))(y) - 1))
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define is_power_of_2(n) ((n) != 0 && (((n) & ((n) - 1)) == 0))
#define upper_32_bits(n) ((u32)(((n) >> 16) >> 16))
#define lower_32_bits(n) ((u32)((n) & 0xffffffff))
#define lower_16_bits(n) ((u16)((n) & 0xffff))
#define upper_16_bits(n) ((u16)((n) >> 16))
#define sign_extend32(v, idx) ((s32)((u32)(v) << (31 - (idx))) >> (31 - (idx)))
#define sign_extend64(v, idx) ((s64)((u64)(v) << (63 - (idx))) >> (63 - (idx)))
#define div_u64(a, b) ((u64)(a) / (u32)(b))
#define div64_u64(a, b) ((u64)(a) / (u64)(b))
#define div_s64(a, b) ((s64)(a) / (s32)(b))
#define do_div(n, base) ({ u32 _rem = (u32)((n) % (base)); (n) /= (base); _rem; })
#define mult_frac(x, numer, denom) ((x) / (denom) * (numer) + ((x) % (denom)) * (numer) / (denom))
#define roundup_pow_of_two(n) ({ unsigned long _n = (n); unsigned long _r = 1; while (_r < _n) _r <<= 1; _r; })
#define ilog2(n) ((int)(63 - __builtin_clzll((unsigned long long)(n))))
#define order_base_2(n) ((n) <= 1 ? 0 : (int)(64 - __builtin_clzll((unsigned long long)(n) - 1)))
#define hweight8(w) __builtin_popcount((u8)(w))
#define hweight16(w) __builtin_popcount((u16)(w))
#define hweight32(w) __builtin_popcount((u32)(w))
#define hweight64(w) __builtin_popcountll((u64)(w))
#define hweight_long(w) __builtin_popcountl((unsigned long)(w))
static inline unsigned long __ffs(unsigned long w) { return (unsigned long)__builtin_ctzl(w); }
static inline unsigned long __fls(unsigned long w) { return 63 - (unsigned long)__builtin_clzl(w); }
static inline int fls_k(unsigned int x) { return x ? 32 - __builtin_clz(x) : 0; }
static inline int fls64(u64 x) { return x ? 64 - __builtin_clzll(x) : 0; }
static inline unsigned long ffz(unsigned long w) { return __builtin_ctzl(~w); }
#define rol32(w, s) (((w) << (s)) | ((w) >> (32 - (s))))
#define ror32(w, s) (((w) >> (s)) | ((w) << (32 - (s))))
#define ARRAY_SIZE_K ARRAY_SIZE
#define BUG() do { fprintf(stderr, "BUG at %s:%d\n", __FILE__, __LINE__); abort(); } while (0)
#define BUG_ON(c) do { if (unlikely(c)) BUG(); } while (0)
extern void kc_warn(const char *file, int line);
#define WARN_ON(c) ({ int _w = !!(c); if (unlikely(_w)) kc_warn(__FILE__, __LINE__); unlikely(_w); })
#define WARN_ON_ONCE(c) ({ static int _once; int _w = !!(c); if (unlikely(_w) && !_once) { _once = 1; kc_warn(__FILE__, __LINE__); } unlikely(_w); })
#define WARN_ONCE(c, ...) WARN_ON_ONCE(c)
#define WARN(c, ...) WARN_ON(c)
#define WARN_ON_FUNCTION_MISMATCH(x, y)
#define might_sleep() do {} while (0)
#define might_sleep_if(c) do {} while (0)
#define cant_sleep() do {} while (0)
#define lockdep_assert_held(x) do {} while (0)
#define lockdep_assert_wiphy(x) do {} while (0)
#define lockdep_assert_held_once(x) do {} while (0)
#define lockdep_assert_not_held(x) do {} while (0)
#define lockdep_assert_irqs_disabled() do {} while (0)
#define lockdep_is_held(x) 1
#define lockdep_rtnl_is_held() 1
#define ASSERT_RTNL() do {} while (0)
#define rtnl_lock() do {} while (0)
#define rtnl_unlock() do {} while (0)
#define rcu_read_lock() do {} while (0)
#define rcu_read_unlock() do {} while (0)
#define rcu_read_lock_held() 1
#define rcu_dereference(p) (p)
#define rcu_dereference_protected(p, c) (p)
#define rcu_dereference_check(p, c) (p)
#define rcu_dereference_raw(p) (p)
#define rcu_access_pointer(p) (p)
#define rcu_assign_pointer(p, v) do { __atomic_thread_fence(__ATOMIC_RELEASE); (p) = (v); } while (0)
#define RCU_INIT_POINTER(p, v) do { (p) = (v); } while (0)
#define synchronize_rcu() do {} while (0)
#define rcu_barrier() do {} while (0)
#define call_rcu(h, f) do { f(h); } while (0)
struct rcu_head { void *next; void (*func)(struct rcu_head *); };
#define kfree_rcu(p, m) free(p)
#define READ_ONCE(x) (*(volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, v) (*(volatile __typeof__(x) *)&(x) = (v))
#define ACCESS_ONCE READ_ONCE
#define smp_mb() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define smp_rmb() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define smp_wmb() __atomic_thread_fence(__ATOMIC_RELEASE)
#define mb() smp_mb()
#define barrier() __asm__ __volatile__("" ::: "memory")
#define cpu_relax() do {} while (0)
#define prefetch(x) do {} while (0)
#define unreachable() __builtin_unreachable()
#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)
#define EXPORT_SYMBOL_NS(x, ns)
#define EXPORT_SYMBOL_NS_GPL(x, ns)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_LICENSE(x)
#define MODULE_FIRMWARE(x)
#define MODULE_VERSION(x)
#define MODULE_IMPORT_NS(x)
#define MODULE_DEVICE_TABLE(a, b)
#define MODULE_ALIAS(x)
#define module_param(a, b, c)
#define module_param_named(a, b, c, d)
#define module_param_array(a, b, c, d)
#define MODULE_PARM_DESC(a, b)
#define THIS_MODULE NULL
#define KBUILD_MODNAME "rtw89"
#define module_init(x)
#define module_exit(x)
#define pr_fmt(x) x
#define __stringify_1(x) #x
#define __stringify(x) __stringify_1(x)
#define RTW89_GEN_MODULE_FWNAME(...)
#define __is_defined(x) 0
#define IS_ERR_VALUE(x) ((unsigned long)(void *)(x) >= (unsigned long)-4095)
static inline void *ERR_PTR(long e) { return (void *)e; }
static inline long PTR_ERR(const void *p) { return (long)p; }
static inline bool IS_ERR(const void *p) { return IS_ERR_VALUE(p); }
static inline bool IS_ERR_OR_NULL(const void *p) { return !p || IS_ERR_VALUE(p); }
static inline void *ERR_CAST(const void *p) { return (void *)p; }

/* ---- endianness (little endian host) ---- */
#define cpu_to_le16(x) ((__le16)(x))
#define cpu_to_le32(x) ((__le32)(x))
#define cpu_to_le64(x) ((__le64)(x))
#define le16_to_cpu(x) ((u16)(x))
#define le32_to_cpu(x) ((u32)(x))
#define le64_to_cpu(x) ((u64)(x))
#define le16_to_cpup(p) (*(p))
#define le32_to_cpup(p) (*(p))
#define cpu_to_be16(x) ((__be16)__builtin_bswap16((u16)(x)))
#define cpu_to_be32(x) ((__be32)__builtin_bswap32((u32)(x)))
#define cpu_to_be64(x) ((__be64)__builtin_bswap64((u64)(x)))
#define be16_to_cpu(x) ((u16)__builtin_bswap16((u16)(x)))
#define be32_to_cpu(x) ((u32)__builtin_bswap32((u32)(x)))
#define be64_to_cpu(x) ((u64)__builtin_bswap64((u64)(x)))
#define be16_to_cpup(p) be16_to_cpu(*(p))
#define swab16(x) __builtin_bswap16(x)
#define swab32(x) __builtin_bswap32(x)
#undef htons
#undef ntohs
#undef htonl
#undef ntohl
#define htons(x) cpu_to_be16(x)
#define ntohs(x) be16_to_cpu(x)
#define htonl(x) cpu_to_be32(x)
#define ntohl(x) be32_to_cpu(x)
#define __cpu_to_le16 cpu_to_le16
#define __cpu_to_le32 cpu_to_le32
#define __le16_to_cpu le16_to_cpu
#define __le32_to_cpu le32_to_cpu
#define __constant_cpu_to_le16 cpu_to_le16
#define __constant_cpu_to_le32 cpu_to_le32
#define __cpu_to_be16 cpu_to_be16
#define __be16_to_cpu be16_to_cpu
#define cpu_to_le16_const cpu_to_le16
#define cpu_to_le32_const cpu_to_le32
#define cpu_to_be16_const cpu_to_be16
#define __constant_htons(x) cpu_to_be16(x)
#define __LITTLE_ENDIAN 1234
#define __LITTLE_ENDIAN_BITFIELD
#define __BIG_ENDIAN 4321

/* unaligned access */
#define get_unaligned(p) ({ __typeof__(*(p)) _v; memcpy(&_v, (p), sizeof(_v)); _v; })
#define put_unaligned(v, p) do { __typeof__(*(p)) _v = (v); memcpy((p), &_v, sizeof(_v)); } while (0)
#define get_unaligned_le16(p) ({ u16 _v; memcpy(&_v, (p), 2); _v; })
#define get_unaligned_le32(p) ({ u32 _v; memcpy(&_v, (p), 4); _v; })
#define get_unaligned_le64(p) ({ u64 _v; memcpy(&_v, (p), 8); _v; })
#define get_unaligned_be16(p) ({ u16 _v; memcpy(&_v, (p), 2); __builtin_bswap16(_v); })
#define get_unaligned_be32(p) ({ u32 _v; memcpy(&_v, (p), 4); __builtin_bswap32(_v); })
#define put_unaligned_le16(v, p) do { u16 _v = (v); memcpy((p), &_v, 2); } while (0)
#define put_unaligned_le32(v, p) do { u32 _v = (v); memcpy((p), &_v, 4); } while (0)
#define put_unaligned_le64(v, p) do { u64 _v = (v); memcpy((p), &_v, 8); } while (0)
#define put_unaligned_be16(v, p) do { u16 _v = __builtin_bswap16(v); memcpy((p), &_v, 2); } while (0)
#define put_unaligned_be32(v, p) do { u32 _v = __builtin_bswap32(v); memcpy((p), &_v, 4); } while (0)

/* ---- bitfield.h ---- */
#define __bf_shf(x) (__builtin_ffsll(x) - 1)
#define FIELD_PREP(mask, val) (((__typeof__(mask))(val) << __bf_shf(mask)) & (mask))
#define FIELD_PREP_CONST(mask, val) FIELD_PREP(mask, val)
#define FIELD_GET(mask, reg) ((__typeof__(mask))(((reg) & (mask)) >> __bf_shf(mask)))
#define FIELD_FIT(mask, val) (!(((u64)(val) << __bf_shf(mask)) & ~(u64)(mask)))
#define FIELD_MAX(mask) ((__typeof__(mask))((mask) >> __bf_shf(mask)))
#define __BF_FIELD_CHECK(...)
#define ____MAKE_OP(type, base, to, from) \
static inline __##type type##_encode_bits(base v, base field) { return to((v << __bf_shf(field)) & field); } \
static inline __##type type##_replace_bits(__##type old, base val, base field) { return (old & ~to(field)) | type##_encode_bits(val, field); } \
static inline void type##p_replace_bits(__##type *p, base val, base field) { *p = (*p & ~to(field)) | type##_encode_bits(val, field); } \
static inline base type##_get_bits(__##type v, base field) { return (from(v) & field) >> __bf_shf(field); }
#define __MAKE_OP(size) \
	____MAKE_OP(le##size, u##size, cpu_to_le##size, le##size##_to_cpu) \
	____MAKE_OP(be##size, u##size, cpu_to_be##size, be##size##_to_cpu) \
	____MAKE_OP(u##size, u##size, , )
____MAKE_OP(u8, u8, , )
__MAKE_OP(16)
__MAKE_OP(32)
__MAKE_OP(64)

/* ---- bitops ---- */
#define DECLARE_BITMAP(name, bits) unsigned long name[BITS_TO_LONGS(bits)]
static inline void set_bit(long nr, volatile unsigned long *addr) { __atomic_or_fetch(&addr[nr / 64], 1UL << (nr % 64), __ATOMIC_SEQ_CST); }
static inline void clear_bit(long nr, volatile unsigned long *addr) { __atomic_and_fetch(&addr[nr / 64], ~(1UL << (nr % 64)), __ATOMIC_SEQ_CST); }
static inline void __set_bit(long nr, volatile unsigned long *addr) { set_bit(nr, addr); }
static inline void __clear_bit(long nr, volatile unsigned long *addr) { clear_bit(nr, addr); }
static inline int test_bit(long nr, const volatile unsigned long *addr) { return (addr[nr / 64] >> (nr % 64)) & 1; }
static inline int test_and_set_bit(long nr, volatile unsigned long *addr) { unsigned long m = 1UL << (nr % 64); return !!(__atomic_fetch_or(&addr[nr / 64], m, __ATOMIC_SEQ_CST) & m); }
static inline int test_and_clear_bit(long nr, volatile unsigned long *addr) { unsigned long m = 1UL << (nr % 64); return !!(__atomic_fetch_and(&addr[nr / 64], ~m, __ATOMIC_SEQ_CST) & m); }
static inline void change_bit(long nr, volatile unsigned long *addr) { __atomic_xor_fetch(&addr[nr / 64], 1UL << (nr % 64), __ATOMIC_SEQ_CST); }
static inline unsigned long find_next_bit(const unsigned long *addr, unsigned long size, unsigned long off) { for (; off < size; off++) if (test_bit(off, addr)) return off; return size; }
static inline unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size, unsigned long off) { for (; off < size; off++) if (!test_bit(off, addr)) return off; return size; }
#define find_first_bit(a, s) find_next_bit(a, s, 0)
#define find_first_zero_bit(a, s) find_next_zero_bit(a, s, 0)
#define for_each_set_bit(b, a, s) for ((b) = find_next_bit((a), (s), 0); (b) < (s); (b) = find_next_bit((a), (s), (b) + 1))
#define for_each_set_bit_from(b, a, s) for ((b) = find_next_bit((a), (s), (b)); (b) < (s); (b) = find_next_bit((a), (s), (b) + 1))
#define for_each_clear_bit(b, a, s) for ((b) = find_next_zero_bit((a), (s), 0); (b) < (s); (b) = find_next_zero_bit((a), (s), (b) + 1))
static inline void bitmap_zero(unsigned long *d, unsigned int n) { memset(d, 0, BITS_TO_LONGS(n) * 8); }
static inline void bitmap_fill(unsigned long *d, unsigned int n) { memset(d, 0xff, BITS_TO_LONGS(n) * 8); }
static inline void bitmap_copy(unsigned long *d, const unsigned long *s, unsigned int n) { memcpy(d, s, BITS_TO_LONGS(n) * 8); }
static inline int bitmap_empty(const unsigned long *s, unsigned int n) { for (unsigned i = 0; i < n; i++) if (test_bit(i, s)) return 0; return 1; }
static inline int bitmap_weight(const unsigned long *s, unsigned int n) { int c = 0; for (unsigned i = 0; i < n; i++) c += test_bit(i, s); return c; }
static inline void bitmap_set(unsigned long *m, unsigned int s, unsigned int n) { for (unsigned i = 0; i < n; i++) set_bit(s + i, m); }
static inline void bitmap_clear(unsigned long *m, unsigned int s, unsigned int n) { for (unsigned i = 0; i < n; i++) clear_bit(s + i, m); }

/* ---- atomics ---- */
typedef struct { int counter; } atomic_t;
typedef struct { long long counter; } atomic64_t;
#define ATOMIC_INIT(i) { (i) }
#define atomic_set(v, i) __atomic_store_n(&(v)->counter, (i), __ATOMIC_SEQ_CST)
#define atomic_read(v) __atomic_load_n(&(v)->counter, __ATOMIC_SEQ_CST)
#define atomic_inc(v) __atomic_add_fetch(&(v)->counter, 1, __ATOMIC_SEQ_CST)
#define atomic_dec(v) __atomic_sub_fetch(&(v)->counter, 1, __ATOMIC_SEQ_CST)
#define atomic_add(i, v) __atomic_add_fetch(&(v)->counter, (i), __ATOMIC_SEQ_CST)
#define atomic_sub(i, v) __atomic_sub_fetch(&(v)->counter, (i), __ATOMIC_SEQ_CST)
#define atomic_inc_return(v) __atomic_add_fetch(&(v)->counter, 1, __ATOMIC_SEQ_CST)
#define atomic_dec_return(v) __atomic_sub_fetch(&(v)->counter, 1, __ATOMIC_SEQ_CST)
#define atomic_dec_and_test(v) (atomic_dec_return(v) == 0)
#define atomic_inc_not_zero(v) ({ int _c = atomic_read(v); _c ? (atomic_inc(v), 1) : 0; })
#define atomic_cmpxchg(v, o, n) ({ int _o = (o); __atomic_compare_exchange_n(&(v)->counter, &_o, (n), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); _o; })
#define atomic64_set(v, i) __atomic_store_n(&(v)->counter, (i), __ATOMIC_SEQ_CST)
#define atomic64_read(v) __atomic_load_n(&(v)->counter, __ATOMIC_SEQ_CST)
#define atomic64_inc(v) __atomic_add_fetch(&(v)->counter, 1, __ATOMIC_SEQ_CST)
#define atomic64_add(i, v) __atomic_add_fetch(&(v)->counter, (i), __ATOMIC_SEQ_CST)
#define atomic_long_t atomic64_t

/* ---- list.h ---- */
struct list_head { struct list_head *next, *prev; };
struct hlist_node { struct hlist_node *next, **pprev; };
struct hlist_head { struct hlist_node *first; };
#define LIST_HEAD_INIT(n) { &(n), &(n) }
#define LIST_HEAD(n) struct list_head n = LIST_HEAD_INIT(n)
static inline void INIT_LIST_HEAD(struct list_head *l) { l->next = l; l->prev = l; }
static inline void __list_add(struct list_head *n, struct list_head *p, struct list_head *x) { x->prev = n; n->next = x; n->prev = p; p->next = n; }
static inline void list_add(struct list_head *n, struct list_head *h) { __list_add(n, h, h->next); }
static inline void list_add_tail(struct list_head *n, struct list_head *h) { __list_add(n, h->prev, h); }
static inline void __list_del(struct list_head *p, struct list_head *n) { n->prev = p; p->next = n; }
static inline void list_del(struct list_head *e) { __list_del(e->prev, e->next); e->next = (void *)0; e->prev = (void *)0; }
static inline void list_del_init(struct list_head *e) { __list_del(e->prev, e->next); INIT_LIST_HEAD(e); }
static inline void list_move(struct list_head *l, struct list_head *h) { __list_del(l->prev, l->next); list_add(l, h); }
static inline void list_move_tail(struct list_head *l, struct list_head *h) { __list_del(l->prev, l->next); list_add_tail(l, h); }
static inline int list_empty(const struct list_head *h) { return h->next == h; }
static inline int list_is_last(const struct list_head *l, const struct list_head *h) { return l->next == h; }
static inline int list_is_first(const struct list_head *l, const struct list_head *h) { return l->prev == h; }
static inline int list_is_singular(const struct list_head *h) { return !list_empty(h) && h->next == h->prev; }
static inline void list_replace(struct list_head *o, struct list_head *n) { n->next = o->next; n->next->prev = n; n->prev = o->prev; n->prev->next = n; }
static inline void list_splice_init(struct list_head *l, struct list_head *h) { if (!list_empty(l)) { struct list_head *f = l->next, *la = l->prev, *at = h->next; f->prev = h; h->next = f; la->next = at; at->prev = la; INIT_LIST_HEAD(l); } }
static inline void list_splice_tail_init(struct list_head *l, struct list_head *h) { if (!list_empty(l)) { struct list_head *f = l->next, *la = l->prev, *at = h->prev; f->prev = at; at->next = f; la->next = h; h->prev = la; INIT_LIST_HEAD(l); } }
static inline void list_splice(struct list_head *l, struct list_head *h) { if (!list_empty(l)) { struct list_head *f = l->next, *la = l->prev, *at = h->next; f->prev = h; h->next = f; la->next = at; at->prev = la; } }
#define list_entry(p, t, m) container_of(p, t, m)
#define list_first_entry(p, t, m) list_entry((p)->next, t, m)
#define list_last_entry(p, t, m) list_entry((p)->prev, t, m)
#define list_first_entry_or_null(p, t, m) (!list_empty(p) ? list_first_entry(p, t, m) : NULL)
#define list_next_entry(p, m) list_entry((p)->m.next, __typeof__(*(p)), m)
#define list_prev_entry(p, m) list_entry((p)->m.prev, __typeof__(*(p)), m)
#define list_for_each(p, h) for ((p) = (h)->next; (p) != (h); (p) = (p)->next)
#define list_for_each_safe(p, n, h) for ((p) = (h)->next, (n) = (p)->next; (p) != (h); (p) = (n), (n) = (p)->next)
#define list_for_each_entry(p, h, m) for ((p) = list_first_entry(h, __typeof__(*(p)), m); &(p)->m != (h); (p) = list_next_entry(p, m))
#define list_for_each_entry_safe(p, n, h, m) for ((p) = list_first_entry(h, __typeof__(*(p)), m), (n) = list_next_entry(p, m); &(p)->m != (h); (p) = (n), (n) = list_next_entry(n, m))
#define list_for_each_entry_reverse(p, h, m) for ((p) = list_last_entry(h, __typeof__(*(p)), m); &(p)->m != (h); (p) = list_prev_entry(p, m))
#define list_for_each_entry_rcu(p, h, m, ...) list_for_each_entry(p, h, m)
#define list_for_each_entry_continue(p, h, m) for ((p) = list_next_entry(p, m); &(p)->m != (h); (p) = list_next_entry(p, m))
#define list_for_each_entry_continue_reverse(p, h, m) for ((p) = list_prev_entry(p, m); &(p)->m != (h); (p) = list_prev_entry(p, m))
#define list_add_rcu list_add
#define list_add_tail_rcu list_add_tail
#define list_del_rcu list_del
#define list_cut_position(...) BUG()
static inline void INIT_HLIST_HEAD(struct hlist_head *h) { h->first = NULL; }

/* ---- memory ---- */
#define GFP_KERNEL 0u
#define GFP_ATOMIC 1u
#define GFP_NOWAIT 2u
#define GFP_ZERO 4u
#define __GFP_ZERO 4u
#define __GFP_NOWARN 8u
#define __GFP_NOFAIL 16u
#define __GFP_RETRY_MAYFAIL 32u
static inline void *kmalloc(size_t n, gfp_t g) { return malloc(n ? n : 1); }
static inline void *kzalloc(size_t n, gfp_t g) { return calloc(1, n ? n : 1); }
static inline void *kcalloc(size_t n, size_t s, gfp_t g) { return calloc(n ? n : 1, s ? s : 1); }
static inline void *kmalloc_array(size_t n, size_t s, gfp_t g) { return malloc(n * s ? n * s : 1); }
static inline void *krealloc(void *p, size_t n, gfp_t g) { return realloc(p, n); }
static inline void kfree(const void *p) { free((void *)p); }
static inline void kfree_sensitive(const void *p) { free((void *)p); }
static inline void *kvmalloc(size_t n, gfp_t g) { return malloc(n ? n : 1); }
static inline void *kvzalloc(size_t n, gfp_t g) { return calloc(1, n ? n : 1); }
static inline void *kvcalloc(size_t n, size_t s, gfp_t g) { return calloc(n ? n : 1, s ? s : 1); }
static inline void *kvmalloc_array(size_t n, size_t s, gfp_t g) { return malloc(n * s ? n * s : 1); }
static inline void kvfree(const void *p) { free((void *)p); }
static inline void *vmalloc(size_t n) { return malloc(n ? n : 1); }
static inline void *vzalloc(size_t n) { return calloc(1, n ? n : 1); }
static inline void vfree(const void *p) { free((void *)p); }
static inline void *kmemdup(const void *s, size_t n, gfp_t g) { void *p = malloc(n ? n : 1); if (p) memcpy(p, s, n); return p; }
static inline void *kmemdup_array(const void *s, size_t n, size_t sz, gfp_t g) { return kmemdup(s, n * sz, g); }
static inline char *kstrdup(const char *s, gfp_t g) { return strdup(s); }
#define kmalloc_obj(...) KC_KMALLOC_OBJ(__VA_ARGS__, 0, 0)
#define KC_KMALLOC_OBJ(v, ...) ((__typeof__(&(v)))malloc(sizeof(v)))
#define kzalloc_obj(...) KC_KZALLOC_OBJ(__VA_ARGS__, 0, 0)
#define KC_KZALLOC_OBJ(v, ...) ((__typeof__(&(v)))calloc(1, sizeof(v)))
#define kzalloc_objs(v, n, ...) ((__typeof__(&(v)))calloc((n) ? (n) : 1, sizeof(v)))
#define kzalloc_flex(v, member, n, ...) ((__typeof__(&(v)))calloc(1, sizeof(v) + sizeof((&(v))->member[0]) * (size_t)(n)))
#define devm_kzalloc(d, n, g) kzalloc(n, g)
#define devm_kcalloc(d, n, s, g) kcalloc(n, s, g)
#define devm_kfree(d, p) kfree(p)
#define memzero_explicit(p, n) memset(p, 0, n)
#define memset_startat(p, v, f) memset(&(p)->f, v, sizeof(*(p)) - offsetof(__typeof__(*(p)), f))
#define memcpy_and_pad(d, dl, s, sl, pad) do { memset(d, pad, dl); memcpy(d, s, (sl) < (dl) ? (sl) : (dl)); } while (0)
#define strscpy(d, s, n) ({ strncpy(d, s, (n) - 1); (d)[(n) - 1] = 0; (ssize_t)strlen(d); })
#define strscpy_pad strscpy
#define sysfs_emit(buf, ...) sprintf(buf, __VA_ARGS__)
#define ARRAY_ELEMS ARRAY_SIZE
static inline int memcmp_k(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }
static inline int strcasecmp_k(const char *a, const char *b) { return strcasecmp(a, b); }
#define sort(base, n, size, cmp, swapf) qsort(base, n, size, cmp)
#define clamp_k clamp
#define str_read_write(x) ((x) ? "read" : "write")
#define str_on_off(x) ((x) ? "on" : "off")
#define str_enable_disable(x) ((x) ? "enable" : "disable")
#define str_enabled_disabled(x) ((x) ? "enabled" : "disabled")
#define str_yes_no(x) ((x) ? "yes" : "no")
#define str_true_false(x) ((x) ? "true" : "false")
#define str_high_low(x) ((x) ? "high" : "low")
#define str_up_down(x) ((x) ? "up" : "down")
#define str_plural(n) ((n) == 1 ? "" : "s")
#define str_write_read(x) ((x) ? "write" : "read")
#define str_on_off_k str_on_off

/* ---- time ---- */
extern u64 kc_now_ms(void);
extern u64 kc_now_ns(void);
extern volatile unsigned long jiffies;
#define get_jiffies_64() kc_now_ms()
#define jiffies_64 ((u64)jiffies)
#define msecs_to_jiffies(x) ((unsigned long)(x))
#define usecs_to_jiffies(x) ((unsigned long)((x) / 1000))
#define jiffies_to_msecs(x) ((unsigned long)(x))
#define jiffies_to_usecs(x) ((unsigned long)(x) * 1000)
#define time_after(a, b) ((long)((b) - (a)) < 0)
#define time_before(a, b) time_after(b, a)
#define time_after_eq(a, b) ((long)((a) - (b)) >= 0)
#define time_before_eq(a, b) time_after_eq(b, a)
#define time_is_before_jiffies(a) time_after(jiffies, a)
#define time_is_after_jiffies(a) time_before(jiffies, a)
#define time_is_before_eq_jiffies(a) time_after_eq(jiffies, a)
#define time_is_after_eq_jiffies(a) time_before_eq(jiffies, a)
#define ktime_get() ((ktime_t)kc_now_ns())
#define ktime_get_boottime() ((ktime_t)kc_now_ns())
#define ktime_get_real() ((ktime_t)kc_now_ns())
#define ktime_get_ns() ((u64)kc_now_ns())
#define ktime_get_boottime_ns() ((u64)kc_now_ns())
#define ktime_get_real_ns() ((u64)kc_now_ns())
#define ktime_get_real_seconds() ((s64)time(NULL))
#define ktime_get_seconds() ((s64)(kc_now_ns() / 1000000000ULL))
#define ktime_to_ms(k) ((s64)(k) / 1000000)
#define ktime_to_us(k) ((s64)(k) / 1000)
#define ktime_to_ns(k) ((s64)(k))
#define ktime_us_delta(a, b) (((a) - (b)) / 1000)
#define ktime_ms_delta(a, b) (((a) - (b)) / 1000000)
#define ktime_sub(a, b) ((a) - (b))
#define ktime_add(a, b) ((a) + (b))
#define ktime_add_ns(a, b) ((a) + (b))
#define ktime_add_us(a, b) ((a) + (s64)(b) * 1000)
#define ktime_add_ms(a, b) ((a) + (s64)(b) * 1000000)
#define ns_to_ktime(n) ((ktime_t)(n))
#define ms_to_ktime(n) ((ktime_t)(n) * 1000000)
#define ktime_after(a, b) ((a) > (b))
#define ktime_before(a, b) ((a) < (b))
#define ktime_compare(a, b) ((a) < (b) ? -1 : (a) > (b))
#define ktime_set(s, n) ((ktime_t)(s) * 1000000000LL + (n))
#define ktime_to_timespec64 ktime_to_ns
#define ktime_zero 0
struct timespec64 { s64 tv_sec; long tv_nsec; };
extern void kc_sleep_us(unsigned long us);
#define udelay(x) kc_sleep_us(x)
#define ndelay(x) kc_sleep_us(1)
#define mdelay(x) kc_sleep_us((unsigned long)(x) * 1000)
#define msleep(x) kc_sleep_us((unsigned long)(x) * 1000)
#define msleep_interruptible(x) kc_sleep_us((unsigned long)(x) * 1000)
#define usleep_range(a, b) kc_sleep_us(a)
#define usleep_range_state(a, b, s) kc_sleep_us(a)
#define fsleep(x) kc_sleep_us(x)
#define ssleep(x) kc_sleep_us((unsigned long)(x) * 1000000)
#define cond_resched() do {} while (0)
#define schedule() kc_sleep_us(100)
#define yield() kc_sleep_us(100)
#define TASK_UNINTERRUPTIBLE 0
#define TASK_INTERRUPTIBLE 1
#define in_interrupt() 0
#define in_atomic() 0
#define irqs_disabled() 0
#define in_softirq() 0
#define local_bh_disable() do {} while (0)
#define local_bh_enable() do {} while (0)
#define local_irq_save(f) do { (void)(f); } while (0)
#define local_irq_restore(f) do { (void)(f); } while (0)
#define preempt_disable() do {} while (0)
#define preempt_enable() do {} while (0)
#define smp_processor_id() 0
#define get_cpu() 0
#define put_cpu() do {} while (0)

/* iopoll.h */
#define read_poll_timeout(op, val, cond, sleep_us, timeout_us, sleep_before_read, args...) \
({ \
	u64 __timeout_us = (u64)(timeout_us) * 20; /* IOKit USB latency is far higher than in-kernel */ \
	u64 __start = kc_now_ns() / 1000; \
	unsigned long __sleep_us = (sleep_us); \
	int __ret = 0; \
	if (sleep_before_read && __sleep_us) kc_sleep_us(__sleep_us); \
	for (;;) { \
		(val) = op(args); \
		if (cond) break; \
		if (__timeout_us && (kc_now_ns() / 1000 - __start) > __timeout_us) { (val) = op(args); __ret = (cond) ? 0 : -ETIMEDOUT; break; } \
		if (__sleep_us) kc_sleep_us(__sleep_us); \
	} \
	__ret; \
})
#define read_poll_timeout_atomic(op, val, cond, delay_us, timeout_us, delay_before_read, args...) \
	read_poll_timeout(op, val, cond, delay_us, timeout_us, delay_before_read, args)
#define readx_poll_timeout(op, addr, val, cond, sleep_us, timeout_us) read_poll_timeout(op, val, cond, sleep_us, timeout_us, false, addr)

/* ---- sync ---- */
typedef struct { pthread_mutex_t m; } spinlock_t;
typedef spinlock_t raw_spinlock_t;
typedef struct { pthread_mutex_t m; } rwlock_t;
struct mutex { pthread_mutex_t m; };
#define __KC_MUTEX_INIT { PTHREAD_RECURSIVE_MUTEX_INITIALIZER }
extern void kc_mutex_init(pthread_mutex_t *m);
#define spin_lock_init(l) kc_mutex_init(&(l)->m)
#define mutex_init(l) kc_mutex_init(&(l)->m)
#define rwlock_init(l) kc_mutex_init(&(l)->m)
#define __SPIN_LOCK_UNLOCKED(x) { PTHREAD_MUTEX_INITIALIZER }
#define DEFINE_SPINLOCK(n) spinlock_t n = { PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP }
#define DEFINE_MUTEX(n) struct mutex n = { PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP }
#define spin_lock(l) pthread_mutex_lock(&(l)->m)
#define spin_unlock(l) pthread_mutex_unlock(&(l)->m)
#define spin_trylock(l) (pthread_mutex_trylock(&(l)->m) == 0)
#define spin_lock_bh(l) pthread_mutex_lock(&(l)->m)
#define spin_unlock_bh(l) pthread_mutex_unlock(&(l)->m)
#define spin_lock_irq(l) pthread_mutex_lock(&(l)->m)
#define spin_unlock_irq(l) pthread_mutex_unlock(&(l)->m)
#define spin_lock_irqsave(l, f) do { (void)(f); pthread_mutex_lock(&(l)->m); } while (0)
#define spin_unlock_irqrestore(l, f) do { (void)(f); pthread_mutex_unlock(&(l)->m); } while (0)
#define raw_spin_lock_irqsave spin_lock_irqsave
#define raw_spin_unlock_irqrestore spin_unlock_irqrestore
#define raw_spin_lock_init spin_lock_init
#define raw_spin_lock spin_lock
#define raw_spin_unlock spin_unlock
#define read_lock(l) pthread_mutex_lock(&(l)->m)
#define read_unlock(l) pthread_mutex_unlock(&(l)->m)
#define write_lock(l) pthread_mutex_lock(&(l)->m)
#define write_unlock(l) pthread_mutex_unlock(&(l)->m)
#define mutex_lock(l) pthread_mutex_lock(&(l)->m)
#define mutex_unlock(l) pthread_mutex_unlock(&(l)->m)
#define mutex_trylock(l) (pthread_mutex_trylock(&(l)->m) == 0)
#define mutex_lock_interruptible(l) (pthread_mutex_lock(&(l)->m), 0)
#define mutex_is_locked(l) 1
#define mutex_destroy(l) do {} while (0)
#define assert_spin_locked(l) do {} while (0)
#define lockdep_register_key(k) do {} while (0)
#define lockdep_unregister_key(k) do {} while (0)
struct lock_class_key { int dummy; };
struct seqcount_k { unsigned seq; };
typedef struct { int dummy; } seqcount_t;
#define seqcount_init(x) do {} while (0)

/* completion & waitqueue */
struct completion { pthread_mutex_t m; pthread_cond_t c; unsigned int done; };
extern void kc_init_completion(struct completion *x);
#define init_completion(x) kc_init_completion(x)
#define reinit_completion(x) do { pthread_mutex_lock(&(x)->m); (x)->done = 0; pthread_mutex_unlock(&(x)->m); } while (0)
extern void complete(struct completion *x);
extern void complete_all(struct completion *x);
extern void wait_for_completion(struct completion *x);
extern unsigned long wait_for_completion_timeout(struct completion *x, unsigned long timeout_ms);
extern int wait_for_completion_interruptible_timeout(struct completion *x, unsigned long timeout_ms);
extern bool try_wait_for_completion(struct completion *x);
extern bool completion_done(struct completion *x);
#define DECLARE_COMPLETION_ONSTACK(n) struct completion n = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0 }

/* ---- workqueue / timers ---- */
struct work_struct;
typedef void (*work_func_t)(struct work_struct *);
struct work_struct { struct list_head entry; work_func_t func; int pending; void *wq; };
struct delayed_work { struct work_struct work; u64 expires_ms; int armed; };
struct workqueue_struct { int dummy; };
#define INIT_WORK(w, f) do { INIT_LIST_HEAD(&(w)->entry); (w)->func = (f); (w)->pending = 0; (w)->wq = NULL; } while (0)
#define INIT_DELAYED_WORK(w, f) do { INIT_WORK(&(w)->work, f); (w)->armed = 0; } while (0)
#define to_delayed_work(w) container_of(w, struct delayed_work, work)
extern bool queue_work(struct workqueue_struct *wq, struct work_struct *w);
extern bool schedule_work(struct work_struct *w);
extern bool queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *w, unsigned long delay_ms);
extern bool schedule_delayed_work(struct delayed_work *w, unsigned long delay_ms);
extern bool mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *w, unsigned long delay_ms);
extern bool cancel_work_sync(struct work_struct *w);
extern bool cancel_delayed_work(struct delayed_work *w);
extern bool cancel_delayed_work_sync(struct delayed_work *w);
extern bool flush_work(struct work_struct *w);
extern bool flush_delayed_work(struct delayed_work *w);
extern void flush_workqueue(struct workqueue_struct *wq);
extern struct workqueue_struct *alloc_workqueue(const char *fmt, unsigned int flags, int max_active, ...);
extern struct workqueue_struct *create_singlethread_workqueue(const char *name);
extern void destroy_workqueue(struct workqueue_struct *wq);
#define WQ_UNBOUND 1
#define WQ_MEM_RECLAIM 2
#define WQ_HIGHPRI 4
#define system_wq ((struct workqueue_struct *)0)
#define system_unbound_wq ((struct workqueue_struct *)0)
#define system_highpri_wq ((struct workqueue_struct *)0)
#define system_long_wq ((struct workqueue_struct *)0)
#define system_power_efficient_wq ((struct workqueue_struct *)0)
struct tasklet_struct { void (*func)(unsigned long); unsigned long data; };
struct timer_list { struct list_head entry; u64 expires; void (*function)(struct timer_list *); u32 flags; int armed; };
#define timer_setup(t, cb, fl) do { INIT_LIST_HEAD(&(t)->entry); (t)->function = (cb); (t)->flags = (fl); (t)->armed = 0; } while (0)
#define from_timer(var, t, field) container_of(t, __typeof__(*(var)), field)
#define timer_container_of(var, t, field) container_of(t, __typeof__(*(var)), field)
extern int mod_timer(struct timer_list *t, unsigned long expires_ms);
extern int add_timer(struct timer_list *t);
extern int del_timer(struct timer_list *t);
extern int del_timer_sync(struct timer_list *t);
#define timer_delete(t) del_timer(t)
#define timer_delete_sync(t) del_timer_sync(t)
#define timer_pending(t) ((t)->armed)
#define timer_shutdown_sync(t) del_timer_sync(t)
struct hrtimer { int dummy; };

/* ---- print / debug ---- */

struct device { const char *name; void *driver_data; struct device *parent; const struct attribute_group **groups; };
#define dev_get_drvdata(d) ((d)->driver_data)
#define dev_set_drvdata(d, p) ((d)->driver_data = (p))
extern void kc_printk(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#define printk(...) kc_printk(0, __VA_ARGS__)
#define pr_err(...) kc_printk(3, __VA_ARGS__)
#define pr_warn(...) kc_printk(4, __VA_ARGS__)
#define pr_info(...) kc_printk(6, __VA_ARGS__)
#define pr_debug(...) do {} while (0)
#define pr_cont(...) kc_printk(6, __VA_ARGS__)
#define pr_err_ratelimited pr_err
#define pr_warn_ratelimited pr_warn
#define dev_err(d, ...) kc_printk(3, __VA_ARGS__)
#define dev_warn(d, ...) kc_printk(4, __VA_ARGS__)
#define dev_info(d, ...) kc_printk(6, __VA_ARGS__)
#define dev_dbg(d, ...) do {} while (0)
#define dev_notice(d, ...) kc_printk(5, __VA_ARGS__)
#define dev_err_once dev_err
#define dev_warn_once dev_warn
#define dev_name(d) ((d) ? (d)->name : "rtw89")
#define dev_kfree_skb_any_k dev_kfree_skb
#define print_hex_dump(...) do {} while (0)
#define print_hex_dump_bytes(...) do {} while (0)
#define DUMP_PREFIX_NONE 0
#define DUMP_PREFIX_OFFSET 1
#define net_ratelimit() 0
#define printk_ratelimit() 0
#define panic(...) abort()
#define snprintf_k snprintf
#define scnprintf(buf, size, ...) ({ int _n = snprintf(buf, size, __VA_ARGS__); (int)((size_t)_n >= (size_t)(size) ? (size) - 1 : _n); })
#define vscnprintf(buf, size, fmt, ap) ({ int _n = vsnprintf(buf, size, fmt, ap); (int)((size_t)_n >= (size_t)(size) ? (size) - 1 : _n); })
#define kasprintf(g, ...) kc_asprintf(__VA_ARGS__)
extern char *kc_asprintf(const char *fmt, ...);
#define num_possible_cpus() 1
#define for_each_possible_cpu(c) for ((c) = 0; (c) < 1; (c)++)
#define dump_stack() do {} while (0)
#define is_kernel_rodata(x) 0
#define ratelimit_state_k int
#define random_get_u32 kc_random32
extern u32 kc_random32(void);
#define get_random_u32() kc_random32()
#define get_random_bytes(buf, n) do { for (size_t _i = 0; _i < (n); _i++) ((u8 *)(buf))[_i] = (u8)kc_random32(); } while (0)
#define prandom_u32() kc_random32()
#define get_random_u8() ((u8)kc_random32())
#define random_u32_below(n) (kc_random32() % (n))
#define get_random_u32_below(n) (kc_random32() % (n))

/* ---- etherdevice ---- */
static inline bool is_zero_ether_addr(const u8 *a) { return !(a[0] | a[1] | a[2] | a[3] | a[4] | a[5]); }
static inline bool is_multicast_ether_addr(const u8 *a) { return a[0] & 1; }
static inline bool is_broadcast_ether_addr(const u8 *a) { return (a[0] & a[1] & a[2] & a[3] & a[4] & a[5]) == 0xff; }
static inline bool is_unicast_ether_addr(const u8 *a) { return !is_multicast_ether_addr(a); }
static inline bool is_valid_ether_addr(const u8 *a) { return !is_multicast_ether_addr(a) && !is_zero_ether_addr(a); }
static inline bool is_local_ether_addr(const u8 *a) { return a[0] & 2; }
static inline bool ether_addr_equal(const u8 *a, const u8 *b) { return memcmp(a, b, 6) == 0; }
static inline bool ether_addr_equal_unaligned(const u8 *a, const u8 *b) { return memcmp(a, b, 6) == 0; }
static inline bool ether_addr_equal_64bits(const u8 *a, const u8 *b) { return memcmp(a, b, 6) == 0; }
static inline void ether_addr_copy(u8 *d, const u8 *s) { memcpy(d, s, 6); }
static inline void eth_broadcast_addr(u8 *a) { memset(a, 0xff, 6); }
static inline void eth_zero_addr(u8 *a) { memset(a, 0, 6); }
static inline void eth_random_addr(u8 *a) { for (int i = 0; i < 6; i++) a[i] = (u8)kc_random32(); a[0] &= 0xfe; a[0] |= 2; }
static inline u32 ether_addr_to_u64_k(const u8 *a) { return a[0]; }
struct ethhdr { u8 h_dest[6]; u8 h_source[6]; __be16 h_proto; } __packed;
struct iphdr { u8 ihl_ver; u8 tos; __be16 tot_len; __be16 id; __be16 frag_off; u8 ttl; u8 protocol; __sum16 check; __be32 saddr; __be32 daddr; } __packed;
struct udphdr { __be16 source; __be16 dest; __be16 len; __sum16 check; } __packed;
struct arphdr { __be16 ar_hrd; __be16 ar_pro; u8 ar_hln; u8 ar_pln; __be16 ar_op; } __packed;
struct ipv6hdr { u8 ver_tc[4]; __be16 payload_len; u8 nexthdr; u8 hop_limit; u8 saddr[16]; u8 daddr[16]; } __packed;
#define ARPOP_REQUEST 1
#define ARPOP_REPLY 2
#define ARPHRD_ETHER 1
#define IPPROTO_UDP_K 17
#define IPPROTO_TCP_K 6
#define IPPROTO_ICMP_K 1
#define IPPROTO_ICMPV6_K 58
#define IPPROTO_UDP 17
#define IPPROTO_TCP 6
#define IPPROTO_ICMPV6 58
static const u8 rfc1042_header[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
static const u8 bridge_tunnel_header[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };
#define ip_hdr(skb) ((struct iphdr *)(skb)->data)
#define udp_hdr(skb) ((struct udphdr *)(skb)->data)
#define ipv6_hdr(skb) ((struct ipv6hdr *)(skb)->data)
#define eth_hdr(skb) ((struct ethhdr *)(skb)->data)
#define arp_hdr(skb) ((struct arphdr *)(skb)->data)
#define ip_fast_csum(a, b) 0
#define csum_fold(x) (x)

/* misc kernel objects */
struct kref { atomic_t refcount; };
struct attribute_group;
struct net_device { char name[16]; u8 dev_addr[6]; u8 *dev_addr_k; };
struct net { int dummy; };
struct dentry { int dummy; };
struct file { void *private_data; };
struct inode { void *i_private; };
struct seq_file { void *private; };
struct bus_type { int dummy; };
struct module { int dummy; };
struct page { int dummy; };
struct rfkill { int dummy; };
struct sock { int dummy; };
struct iw_request_info;
struct netlink_callback;
struct genl_info;
struct ethtool_ops;
struct ethtool_drvinfo;
struct pm_qos_request { int dummy; };
struct dma_pool;
struct napi_struct { int dummy; };
struct kobject { int dummy; };

struct nla_policy { u16 type; u16 len; };
struct nlattr { u16 nla_len; u16 nla_type; };
struct sk_buff;
struct sk_buff_head;
struct netdev_hw_addr { int dummy; };
struct u64_stats_sync { int dummy; };
struct gro_list { int dummy; };
struct xarray { int dummy; };
struct idr { int dummy; };
struct rhashtable { int dummy; };
struct rhashtable_params { int dummy; };
struct rhash_head { int dummy; };
struct sk_buff_k;
struct cpumask { int dummy; };
struct tasklet_struct;
struct rb_node { int dummy; };
struct rb_root { int dummy; };
struct notifier_block { int dummy; };

struct kernel_param_ops { int dummy; };
struct bio { int dummy; };
struct scatterlist { int dummy; };
struct netdev_features { int dummy; };
struct lockdep_map { int dummy; };
struct ctl_table { int dummy; };
struct uuid_t_k { u8 b[16]; };
#define kref_init(k) atomic_set(&(k)->refcount, 1)
#define kref_get(k) atomic_inc(&(k)->refcount)
#define kref_put(k, rel) (atomic_dec_and_test(&(k)->refcount) ? (rel(k), 1) : 0)
#define NUMA_NO_NODE (-1)
#define pm_qos_k int

/* ---- sk_buff ---- */
struct sk_buff_head { struct sk_buff *next, *prev; u32 qlen; spinlock_t lock; };
struct sk_buff {
	struct sk_buff *next, *prev;
	struct sk_buff_head *list;
	struct net_device *dev;
	struct sock *sk;
	ktime_t tstamp;
	unsigned char *head, *data, *tail, *end;
	unsigned int len, data_len, truesize;
	__be16 protocol;
	u16 queue_mapping;
	u16 mac_len_k;
	u32 priority, mark;
	u8 ip_summed, pkt_type, cloned_k;
	u8 pfmemalloc;
	char cb[48] __aligned(8);
};
extern struct sk_buff *alloc_skb(unsigned int size, gfp_t gfp);
#define __dev_alloc_skb(n, g) alloc_skb(n, g)
#define dev_alloc_skb(n) alloc_skb(n, GFP_ATOMIC)
#define netdev_alloc_skb(d, n) alloc_skb(n, GFP_ATOMIC)
#define __netdev_alloc_skb(d, n, g) alloc_skb(n, g)
extern void kfree_skb(struct sk_buff *skb);
#define dev_kfree_skb(s) kfree_skb(s)
#define dev_kfree_skb_any(s) kfree_skb(s)
#define dev_kfree_skb_irq(s) kfree_skb(s)
#define consume_skb(s) kfree_skb(s)
#define kfree_skb_reason(s, r) kfree_skb(s)
static inline unsigned int skb_headroom(const struct sk_buff *s) { return s->data - s->head; }
static inline int skb_tailroom(const struct sk_buff *s) { return s->end - s->tail; }
static inline unsigned char *skb_tail_pointer(const struct sk_buff *s) { return s->tail; }
static inline unsigned char *skb_end_pointer(const struct sk_buff *s) { return s->end; }
static inline void skb_reserve(struct sk_buff *s, int n) { s->data += n; s->tail += n; }
static inline void *skb_put(struct sk_buff *s, unsigned int n) { void *t = s->tail; s->tail += n; s->len += n; if (s->tail > s->end) BUG(); return t; }
static inline void *__skb_put(struct sk_buff *s, unsigned int n) { return skb_put(s, n); }
static inline void *skb_put_zero(struct sk_buff *s, unsigned int n) { void *t = skb_put(s, n); memset(t, 0, n); return t; }
static inline void *skb_put_data(struct sk_buff *s, const void *d, unsigned int n) { void *t = skb_put(s, n); memcpy(t, d, n); return t; }
static inline u8 *skb_put_u8(struct sk_buff *s, u8 v) { u8 *t = skb_put(s, 1); *t = v; return t; }
static inline void *skb_push(struct sk_buff *s, unsigned int n) { s->data -= n; s->len += n; if (s->data < s->head) BUG(); return s->data; }
static inline void *__skb_push(struct sk_buff *s, unsigned int n) { return skb_push(s, n); }
static inline void *skb_pull(struct sk_buff *s, unsigned int n) { if (n > s->len) return NULL; s->len -= n; return s->data += n; }
static inline void *__skb_pull(struct sk_buff *s, unsigned int n) { return skb_pull(s, n); }
static inline void *skb_pull_data(struct sk_buff *s, size_t n) { void *d = s->data; if (n > s->len) return NULL; skb_pull(s, n); return d; }
static inline void skb_trim(struct sk_buff *s, unsigned int len) { if (s->len > len) { s->len = len; s->tail = s->data + len; } }
static inline void __skb_trim(struct sk_buff *s, unsigned int len) { skb_trim(s, len); }
static inline bool pskb_may_pull(struct sk_buff *s, unsigned int n) { return n <= s->len; }
static inline int skb_linearize(struct sk_buff *s) { return 0; }
static inline bool skb_is_nonlinear(const struct sk_buff *s) { return false; }
static inline bool skb_cloned(const struct sk_buff *s) { return false; }
static inline bool skb_shared(const struct sk_buff *s) { return false; }
static inline int skb_headlen(const struct sk_buff *s) { return s->len - s->data_len; }
static inline void skb_set_queue_mapping(struct sk_buff *s, u16 q) { s->queue_mapping = q; }
static inline u16 skb_get_queue_mapping(const struct sk_buff *s) { return s->queue_mapping; }
static inline void skb_reset_network_header(struct sk_buff *s) {}
static inline void skb_reset_mac_header(struct sk_buff *s) {}
static inline void skb_reset_transport_header(struct sk_buff *s) {}
static inline void skb_set_network_header(struct sk_buff *s, int o) {}
static inline unsigned char *skb_network_header(const struct sk_buff *s) { return s->data; }
static inline void skb_orphan(struct sk_buff *s) {}
static inline void skb_tx_timestamp(struct sk_buff *s) {}
extern struct sk_buff *skb_copy(const struct sk_buff *s, gfp_t gfp);
extern struct sk_buff *skb_clone(struct sk_buff *s, gfp_t gfp);
extern struct sk_buff *skb_copy_expand(const struct sk_buff *s, int newheadroom, int newtailroom, gfp_t gfp);
extern int pskb_expand_head(struct sk_buff *s, int nhead, int ntail, gfp_t gfp);
extern int skb_ensure_writable(struct sk_buff *s, int write_len);
#define skb_unshare(s, g) (s)
#define skb_get(s) (s)
static inline void skb_queue_head_init(struct sk_buff_head *l) { l->prev = l->next = (struct sk_buff *)l; l->qlen = 0; spin_lock_init(&l->lock); }
static inline void __skb_queue_head_init(struct sk_buff_head *l) { skb_queue_head_init(l); }
static inline u32 skb_queue_len(const struct sk_buff_head *l) { return l->qlen; }
static inline u32 skb_queue_len_lockless(const struct sk_buff_head *l) { return l->qlen; }
static inline int skb_queue_empty(const struct sk_buff_head *l) { return l->next == (const struct sk_buff *)l; }
static inline int skb_queue_empty_lockless(const struct sk_buff_head *l) { return skb_queue_empty(l); }
static inline struct sk_buff *skb_peek(const struct sk_buff_head *l) { struct sk_buff *s = l->next; return s == (struct sk_buff *)l ? NULL : s; }
static inline struct sk_buff *skb_peek_tail(const struct sk_buff_head *l) { struct sk_buff *s = l->prev; return s == (struct sk_buff *)l ? NULL : s; }
static inline struct sk_buff *skb_queue_next(const struct sk_buff_head *l, const struct sk_buff *s) { return s->next; }
static inline void __skb_insert(struct sk_buff *n, struct sk_buff *prev, struct sk_buff *next, struct sk_buff_head *l) { n->next = next; n->prev = prev; next->prev = n; prev->next = n; n->list = l; l->qlen++; }
static inline void __skb_queue_before(struct sk_buff_head *l, struct sk_buff *nxt, struct sk_buff *n) { __skb_insert(n, nxt->prev, nxt, l); }
static inline void __skb_queue_after(struct sk_buff_head *l, struct sk_buff *prv, struct sk_buff *n) { __skb_insert(n, prv, prv->next, l); }
static inline void __skb_queue_tail(struct sk_buff_head *l, struct sk_buff *n) { __skb_queue_before(l, (struct sk_buff *)l, n); }
static inline void __skb_queue_head(struct sk_buff_head *l, struct sk_buff *n) { __skb_queue_after(l, (struct sk_buff *)l, n); }
static inline void skb_queue_tail(struct sk_buff_head *l, struct sk_buff *n) { pthread_mutex_lock(&l->lock.m); __skb_queue_tail(l, n); pthread_mutex_unlock(&l->lock.m); }
static inline void skb_queue_head(struct sk_buff_head *l, struct sk_buff *n) { pthread_mutex_lock(&l->lock.m); __skb_queue_head(l, n); pthread_mutex_unlock(&l->lock.m); }
static inline void __skb_unlink(struct sk_buff *s, struct sk_buff_head *l) { struct sk_buff *n = s->next, *p = s->prev; l->qlen--; n->prev = p; p->next = n; s->next = s->prev = NULL; s->list = NULL; }
static inline void skb_unlink(struct sk_buff *s, struct sk_buff_head *l) { pthread_mutex_lock(&l->lock.m); __skb_unlink(s, l); pthread_mutex_unlock(&l->lock.m); }
static inline struct sk_buff *__skb_dequeue(struct sk_buff_head *l) { struct sk_buff *s = skb_peek(l); if (s) __skb_unlink(s, l); return s; }
static inline struct sk_buff *__skb_dequeue_tail(struct sk_buff_head *l) { struct sk_buff *s = skb_peek_tail(l); if (s) __skb_unlink(s, l); return s; }
static inline struct sk_buff *skb_dequeue(struct sk_buff_head *l) { pthread_mutex_lock(&l->lock.m); struct sk_buff *s = __skb_dequeue(l); pthread_mutex_unlock(&l->lock.m); return s; }
static inline struct sk_buff *skb_dequeue_tail(struct sk_buff_head *l) { pthread_mutex_lock(&l->lock.m); struct sk_buff *s = __skb_dequeue_tail(l); pthread_mutex_unlock(&l->lock.m); return s; }
static inline void skb_queue_purge(struct sk_buff_head *l) { struct sk_buff *s; while ((s = skb_dequeue(l))) kfree_skb(s); }
static inline void __skb_queue_purge(struct sk_buff_head *l) { struct sk_buff *s; while ((s = __skb_dequeue(l))) kfree_skb(s); }
static inline void skb_queue_splice_tail_init(struct sk_buff_head *l, struct sk_buff_head *h) { struct sk_buff *s; while ((s = __skb_dequeue(l))) __skb_queue_tail(h, s); }
static inline void skb_queue_splice_init(struct sk_buff_head *l, struct sk_buff_head *h) { struct sk_buff *s; while ((s = __skb_dequeue_tail(l))) __skb_queue_head(h, s); }
#define skb_queue_walk(q, s) for ((s) = (q)->next; (s) != (struct sk_buff *)(q); (s) = (s)->next)
#define skb_queue_walk_safe(q, s, tmp) for ((s) = (q)->next, (tmp) = (s)->next; (s) != (struct sk_buff *)(q); (s) = (tmp), (tmp) = (s)->next)
#define skb_queue_reverse_walk(q, s) for ((s) = (q)->prev; (s) != (struct sk_buff *)(q); (s) = (s)->prev)
#define skb_queue_reverse_walk_safe(q, s, tmp) for ((s) = (q)->prev, (tmp) = (s)->prev; (s) != (struct sk_buff *)(q); (s) = (tmp), (tmp) = (s)->prev)
#define skb_get_hash(s) 0
#define skb_checksum_none_assert(s) do {} while (0)
#define skb_frag_k int
#define CHECKSUM_NONE 0
#define PACKET_HOST 0
#define PACKET_BROADCAST 1
#define PACKET_MULTICAST 2
#define PACKET_OTHERHOST 3
#define skb_is_gso(s) 0
#define skb_shinfo_k int
struct skb_shared_info { unsigned char nr_frags; };
#define skb_shinfo(s) ((struct skb_shared_info *)(s)->end)
#define skb_has_frag_list(s) 0
#define napi_gro_receive(n, s) kfree_skb(s)
#define netif_receive_skb(s) kfree_skb(s)
#define netif_rx(s) kfree_skb(s)
#define skb_set_hash(...) do {} while (0)
#define ip_summed_k int
#define skb_orphan_k int
#define skb_mark_not_on_list_k int
#define skb_list_del_init_k int
#define skb_copy_bits(s, off, to, len) (memcpy(to, (s)->data + (off), len), 0)
#define skb_copy_from_linear_data(s, to, len) memcpy(to, (s)->data, len)
#define skb_copy_to_linear_data(s, from, len) memcpy((s)->data, from, len)
#define skb_trim_k int

/* ---- firmware ---- */
struct firmware { size_t size; const u8 *data; };
extern int request_firmware(const struct firmware **fw, const char *name, struct device *dev);
#define firmware_request_nowarn(fw, name, dev) request_firmware(fw, name, dev)
extern void release_firmware(const struct firmware *fw);

/* ---- misc helpers ---- */
#define DEFINE_WAIT(x) int x = 0
#define __user_k
#define sprintf_k sprintf
#define uuid_t_k_alias int
#define __ALIGN_KERNEL(x, a) ALIGN(x, a)
#define hex_asc_k "0123456789abcdef"
#define bin2hex_k
#define SET_NETDEV_DEV(a, b) do {} while (0)
#define dmi_check_system(x) 0
#define dmi_match(a, b) 0
#define dmi_get_system_info(x) NULL
#define dev_coredumpv(...) do {} while (0)
#define try_module_get(m) 1
#define module_put(m) do {} while (0)
#define cpu_to_node(x) 0
#define cpumask_k int
#define rwsem_k int
#define device_k int
#define is_vmalloc_addr(x) 0
#define virt_to_page(x) NULL
#define crc32_le_k int
#define current_k int
#define from_kuid_k int
extern int kstrtou8(const char *s, unsigned int base, u8 *res);
extern int kstrtou32(const char *s, unsigned int base, u32 *res);
extern int kstrtoint(const char *s, unsigned int base, int *res);
#define sscanf_k sscanf
#define simple_strtoul_k strtoul
static inline int __attribute__((format(printf, 3, 4))) snprintf_pad_k(char *b, size_t n, const char *f, ...) { return 0; }
#define ARRAY_SIZE_CHECK(x)
#define typecheck(t, x) 1
#define typecheck_fn(t, f) 1
#define __same_type_k
#define devm_add_action_or_reset(...) 0
#define DEFINE_STATIC_KEY_FALSE(x) int x
#define static_branch_unlikely(x) 0
#define NR_CPUS 1
#define local_k int
#define task_k int
#define in_task() 1
#define int_sqrt(x) ((unsigned long)__builtin_sqrt((double)(x)))
#define sqrt_k
#define ilog2_k
#define __builtin_choose_expr_k
#define STRUCT_GROUP(...) __VA_ARGS__
#define struct_group(name, ...) union { struct { __VA_ARGS__ }; struct { __VA_ARGS__ } name; }
#define struct_group_tagged(tag, name, ...) union { struct { __VA_ARGS__ }; struct tag { __VA_ARGS__ } name; }
#define __struct_group(tag, name, attrs, ...) union { struct { __VA_ARGS__ }; struct tag { __VA_ARGS__ } name; }
#define DECLARE_FLEX_ARRAY(T, m) T m[0]
#define __DECLARE_FLEX_ARRAY(T, m) T m[0]
#define DECLARE_KFIFO_k
#define NLA_POLICY_EXACT_LEN(n) { .len = (n) }
#define NLA_POLICY_MIN_LEN(n) { .len = (n) }
#define NLA_POLICY_MAX_LEN(n) { .len = (n) }
#define NLA_POLICY_RANGE(t, a, b) { .type = t }
#define NLA_POLICY_MAX(t, a) { .type = t }
#define NLA_POLICY_MIN(t, a) { .type = t }
#define NLA_POLICY_NESTED(p) { .type = 0 }
#define NLA_POLICY_NESTED_ARRAY(p) { .type = 0 }
#define NLA_POLICY_FULL_RANGE(t, r) { .type = t }
#define NLA_POLICY_FULL_RANGE_SIGNED(t, r) { .type = t }
#define NLA_POLICY_VALIDATE_FN(t, fn, ...) { .type = t }
#define NLA_POLICY_BITFIELD32(m) { .type = 0 }

#include "kcompat2.h"
#endif /* KCOMPAT_H */
