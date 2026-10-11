#ifndef ECP256_H
#define ECP256_H
#include <stdint.h>
#include <stdbool.h>
typedef struct { uint32_t w[8]; } bn;		/* 256-bit little-endian limbs */
typedef struct { bn x, y; bool inf; } ecpt;
extern const bn P256_P, P256_Q;
void bn_from_be(bn *r, const uint8_t b[32]);
void bn_to_be(const bn *a, uint8_t b[32]);
int bn_cmp(const bn *a, const bn *b);
bool bn_is_zero(const bn *a);
void bn_set_u32(bn *r, uint32_t v);
void mod_add(bn *r, const bn *a, const bn *b, const bn *m);
void mod_sub(bn *r, const bn *a, const bn *b, const bn *m);
void mod_mul(bn *r, const bn *a, const bn *b, const bn *m);
void mod_inv(bn *r, const bn *a, const bn *m);
bool p256_is_qr(const bn *a);
bool p256_sqrt(bn *r, const bn *a);
bool p256_on_curve(const ecpt *p);
void p256_mul(ecpt *r, const bn *k, const ecpt *p);
void p256_add(ecpt *r, const ecpt *a, const ecpt *b);
void p256_neg(ecpt *r, const ecpt *a);
extern const ecpt P256_G;
#endif
