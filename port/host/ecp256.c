// SPDX-License-Identifier: BSD-3-Clause
// Minimal NIST P-256 arithmetic for SAE (WPA3). Simple and not constant-time: used for a handful of operations per connection.
#include <string.h>
#include "ecp256.h"

const bn P256_P = {{0xffffffff, 0xffffffff, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0xffffffff}};
const bn P256_Q = {{0xfc632551, 0xf3b9cac2, 0xa7179e84, 0xbce6faad, 0xffffffff, 0xffffffff, 0x00000000, 0xffffffff}};
static const bn P256_B = {{0x27d2604b, 0x3bce3c3e, 0xcc53b0f6, 0x651d06b0, 0x769886bc, 0xb3ebbd55, 0xaa3a93e7, 0x5ac635d8}};
const ecpt P256_G = {
	.x = {{0xd898c296, 0xf4a13945, 0x2deb33a0, 0x77037d81, 0x63a440f2, 0xf8bce6e5, 0xe12c4247, 0x6b17d1f2}},
	.y = {{0x37bf51f5, 0xcbb64068, 0x6b315ece, 0x2bce3357, 0x7c0f9e16, 0x8ee7eb4a, 0xfe1a7f9b, 0x4fe342e2}},
	.inf = false };

void bn_from_be(bn *r, const uint8_t b[32]) { for (int i = 0; i < 8; i++) r->w[7 - i] = ((uint32_t)b[4 * i] << 24) | (b[4 * i + 1] << 16) | (b[4 * i + 2] << 8) | b[4 * i + 3]; }
void bn_to_be(const bn *a, uint8_t b[32]) { for (int i = 0; i < 8; i++) { uint32_t v = a->w[7 - i]; b[4 * i] = v >> 24; b[4 * i + 1] = v >> 16; b[4 * i + 2] = v >> 8; b[4 * i + 3] = v; } }
void bn_set_u32(bn *r, uint32_t v) { memset(r, 0, sizeof *r); r->w[0] = v; }
bool bn_is_zero(const bn *a) { uint32_t o = 0; for (int i = 0; i < 8; i++) o |= a->w[i]; return o == 0; }
int bn_cmp(const bn *a, const bn *b) { for (int i = 7; i >= 0; i--) { if (a->w[i] > b->w[i]) return 1; if (a->w[i] < b->w[i]) return -1; } return 0; }

static uint32_t bn_add_raw(bn *r, const bn *a, const bn *b) { uint64_t c = 0; for (int i = 0; i < 8; i++) { c += (uint64_t)a->w[i] + b->w[i]; r->w[i] = (uint32_t)c; c >>= 32; } return (uint32_t)c; }
static uint32_t bn_sub_raw(bn *r, const bn *a, const bn *b) { int64_t c = 0; for (int i = 0; i < 8; i++) { c += (int64_t)a->w[i] - b->w[i]; r->w[i] = (uint32_t)c; c >>= 32; } return (uint32_t)(c & 1); }

void mod_add(bn *r, const bn *a, const bn *b, const bn *m)
{
	bn t;
	uint32_t c = bn_add_raw(&t, a, b);
	if (c || bn_cmp(&t, m) >= 0) bn_sub_raw(&t, &t, m);
	*r = t;
}

void mod_sub(bn *r, const bn *a, const bn *b, const bn *m)
{
	bn t;
	uint32_t borrow = bn_sub_raw(&t, a, b);
	if (borrow) bn_add_raw(&t, &t, m);
	*r = t;
}

void mod_mul(bn *r, const bn *a, const bn *b, const bn *m)
{
	bn acc;
	memset(&acc, 0, sizeof acc);
	for (int i = 255; i >= 0; i--) {
		mod_add(&acc, &acc, &acc, m);
		if ((b->w[i / 32] >> (i % 32)) & 1) mod_add(&acc, &acc, a, m);
	}
	*r = acc;
}

static void mod_pow(bn *r, const bn *base, const bn *e, const bn *m)
{
	bn res; bn_set_u32(&res, 1);
	for (int i = 255; i >= 0; i--) {
		mod_mul(&res, &res, &res, m);
		if ((e->w[i / 32] >> (i % 32)) & 1) mod_mul(&res, &res, base, m);
	}
	*r = res;
}

void mod_inv(bn *r, const bn *a, const bn *m)
{
	bn two, e;
	bn_set_u32(&two, 2);
	mod_sub(&e, m, &two, m);			/* m - 2 (m is prime) */
	if (bn_cmp(m, &two) <= 0) { *r = *a; return; }
	bn t; bn_sub_raw(&t, m, &two);
	mod_pow(r, a, &t, m);
}

bool p256_is_qr(const bn *a)
{
	bn e, one, r;
	bn_set_u32(&one, 1);
	/* (p-1)/2 */
	bn_sub_raw(&e, &P256_P, &one);
	for (int i = 0; i < 8; i++) e.w[i] = (e.w[i] >> 1) | (i < 7 ? e.w[i + 1] << 31 : 0);
	mod_pow(&r, a, &e, &P256_P);
	return bn_cmp(&r, &one) == 0 || bn_is_zero(a);
}

bool p256_sqrt(bn *r, const bn *a)
{
	bn e, one, chk;
	bn_set_u32(&one, 1);
	bn_add_raw(&e, &P256_P, &one);			/* p + 1 (fits: p < 2^256 - 1) */
	for (int i = 0; i < 8; i++) e.w[i] = (e.w[i] >> 2) | (i < 7 ? e.w[i + 1] << 30 : 0);
	mod_pow(r, a, &e, &P256_P);
	mod_mul(&chk, r, r, &P256_P);
	return bn_cmp(&chk, a) == 0;
}

bool p256_on_curve(const ecpt *p)
{
	if (p->inf) return false;
	if (bn_cmp(&p->x, &P256_P) >= 0 || bn_cmp(&p->y, &P256_P) >= 0) return false;
	bn y2, x2, x3, ax, rhs, three;
	bn_set_u32(&three, 3);
	mod_mul(&y2, &p->y, &p->y, &P256_P);
	mod_mul(&x2, &p->x, &p->x, &P256_P);
	mod_mul(&x3, &x2, &p->x, &P256_P);
	mod_mul(&ax, &three, &p->x, &P256_P);
	mod_sub(&rhs, &x3, &ax, &P256_P);
	mod_add(&rhs, &rhs, &P256_B, &P256_P);
	return bn_cmp(&y2, &rhs) == 0;
}

/* Jacobian arithmetic, a = -3 */
typedef struct { bn x, y, z; } jac;

static void jdbl(jac *r, const jac *p)
{
	if (bn_is_zero(&p->z) || bn_is_zero(&p->y)) { memset(r, 0, sizeof *r); return; }
	bn delta, gamma, beta, alpha, t1, t2, x3, y3, z3, eight_gamma2, four_beta, three;
	bn_set_u32(&three, 3);
	mod_mul(&delta, &p->z, &p->z, &P256_P);
	mod_mul(&gamma, &p->y, &p->y, &P256_P);
	mod_mul(&beta, &p->x, &gamma, &P256_P);
	mod_sub(&t1, &p->x, &delta, &P256_P);
	mod_add(&t2, &p->x, &delta, &P256_P);
	mod_mul(&alpha, &t1, &t2, &P256_P);
	mod_mul(&alpha, &alpha, &three, &P256_P);
	mod_mul(&x3, &alpha, &alpha, &P256_P);
	mod_add(&t1, &beta, &beta, &P256_P); mod_add(&t1, &t1, &t1, &P256_P); four_beta = t1;
	mod_add(&t1, &t1, &t1, &P256_P);			/* 8*beta */
	mod_sub(&x3, &x3, &t1, &P256_P);
	mod_add(&t1, &p->y, &p->z, &P256_P);
	mod_mul(&z3, &t1, &t1, &P256_P);
	mod_sub(&z3, &z3, &gamma, &P256_P);
	mod_sub(&z3, &z3, &delta, &P256_P);
	mod_sub(&t1, &four_beta, &x3, &P256_P);
	mod_mul(&y3, &alpha, &t1, &P256_P);
	mod_mul(&eight_gamma2, &gamma, &gamma, &P256_P);
	mod_add(&eight_gamma2, &eight_gamma2, &eight_gamma2, &P256_P);
	mod_add(&eight_gamma2, &eight_gamma2, &eight_gamma2, &P256_P);
	mod_add(&eight_gamma2, &eight_gamma2, &eight_gamma2, &P256_P);
	mod_sub(&y3, &y3, &eight_gamma2, &P256_P);
	r->x = x3; r->y = y3; r->z = z3;
}

static void jadd(jac *r, const jac *a, const jac *b)
{
	if (bn_is_zero(&a->z)) { *r = *b; return; }
	if (bn_is_zero(&b->z)) { *r = *a; return; }
	bn z1z1, z2z2, u1, u2, s1, s2, h, i, j, rr, v, t, x3, y3, z3;
	mod_mul(&z1z1, &a->z, &a->z, &P256_P);
	mod_mul(&z2z2, &b->z, &b->z, &P256_P);
	mod_mul(&u1, &a->x, &z2z2, &P256_P);
	mod_mul(&u2, &b->x, &z1z1, &P256_P);
	mod_mul(&t, &b->z, &z2z2, &P256_P); mod_mul(&s1, &a->y, &t, &P256_P);
	mod_mul(&t, &a->z, &z1z1, &P256_P); mod_mul(&s2, &b->y, &t, &P256_P);
	mod_sub(&h, &u2, &u1, &P256_P);
	if (bn_is_zero(&h)) {
		if (bn_cmp(&s1, &s2) == 0) { jdbl(r, a); return; }
		memset(r, 0, sizeof *r); return;			/* a = -b */
	}
	mod_add(&t, &h, &h, &P256_P); mod_mul(&i, &t, &t, &P256_P);
	mod_mul(&j, &h, &i, &P256_P);
	mod_sub(&t, &s2, &s1, &P256_P); mod_add(&rr, &t, &t, &P256_P);
	mod_mul(&v, &u1, &i, &P256_P);
	mod_mul(&x3, &rr, &rr, &P256_P); mod_sub(&x3, &x3, &j, &P256_P);
	mod_add(&t, &v, &v, &P256_P); mod_sub(&x3, &x3, &t, &P256_P);
	mod_sub(&t, &v, &x3, &P256_P); mod_mul(&y3, &rr, &t, &P256_P);
	mod_mul(&t, &s1, &j, &P256_P); mod_add(&t, &t, &t, &P256_P); mod_sub(&y3, &y3, &t, &P256_P);
	mod_add(&t, &a->z, &b->z, &P256_P); mod_mul(&z3, &t, &t, &P256_P);
	mod_sub(&z3, &z3, &z1z1, &P256_P); mod_sub(&z3, &z3, &z2z2, &P256_P); mod_mul(&z3, &z3, &h, &P256_P);
	r->x = x3; r->y = y3; r->z = z3;
}

static void to_jac(jac *r, const ecpt *p)
{
	if (p->inf) { memset(r, 0, sizeof *r); return; }
	r->x = p->x; r->y = p->y; bn_set_u32(&r->z, 1);
}

static void to_aff(ecpt *r, const jac *p)
{
	if (bn_is_zero(&p->z)) { r->inf = true; return; }
	bn zi, zi2, zi3;
	mod_inv(&zi, &p->z, &P256_P);
	mod_mul(&zi2, &zi, &zi, &P256_P);
	mod_mul(&zi3, &zi2, &zi, &P256_P);
	mod_mul(&r->x, &p->x, &zi2, &P256_P);
	mod_mul(&r->y, &p->y, &zi3, &P256_P);
	r->inf = false;
}

void p256_mul(ecpt *r, const bn *k, const ecpt *p)
{
	jac acc, base;
	memset(&acc, 0, sizeof acc);
	to_jac(&base, p);
	for (int i = 255; i >= 0; i--) {
		jdbl(&acc, &acc);
		if ((k->w[i / 32] >> (i % 32)) & 1) jadd(&acc, &acc, &base);
	}
	to_aff(r, &acc);
}

void p256_add(ecpt *r, const ecpt *a, const ecpt *b)
{
	jac ja, jb, jr;
	to_jac(&ja, a); to_jac(&jb, b);
	jadd(&jr, &ja, &jb);
	to_aff(r, &jr);
}

void p256_neg(ecpt *r, const ecpt *a)
{
	*r = *a;
	if (!a->inf) mod_sub(&r->y, &P256_P, &a->y, &P256_P);
}
