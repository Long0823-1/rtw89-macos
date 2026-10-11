// SPDX-License-Identifier: BSD-3-Clause
// WPA3-Personal SAE (IEEE 802.11-2016 12.4), group 19 (P-256), hunting-and-pecking password element.
#include <stdlib.h>
#include <string.h>
#include "sae.h"
#include "crypto.h"

static void rand_scalar(bn *r)		/* uniform in [1, q-1] (rejection sampling) */
{
	uint8_t b[32];
	for (;;) {
		wpa_random(b, 32);
		bn_from_be(r, b);
		if (!bn_is_zero(r) && bn_cmp(r, &P256_Q) < 0) return;
	}
}

static void put_point(uint8_t out[64], const ecpt *p) { bn_to_be(&p->x, out); bn_to_be(&p->y, out + 32); }

static bool get_point(ecpt *p, const uint8_t in[64])
{
	bn_from_be(&p->x, in); bn_from_be(&p->y, in + 32); p->inf = false;
	return p256_on_curve(p);
}

int sae_init(struct sae *s, const uint8_t own[6], const uint8_t peer[6], const char *password)
{
	memset(s, 0, sizeof *s);
	memcpy(s->mac_a, own, 6); memcpy(s->mac_b, peer, 6);
	s->password = password;
	s->send_confirm = 1;
	/* password element: hunting and pecking */
	uint8_t key[12], pbuf[64 + 1 + 1];
	size_t plen = strlen(password);
	if (plen > 64) return -1;
	int cmp = memcmp(own, peer, 6);
	memcpy(key, cmp > 0 ? own : peer, 6);			/* max(a,b) || min(a,b) */
	memcpy(key + 6, cmp > 0 ? peer : own, 6);
	uint8_t p_be[32];
	bn_to_be(&P256_P, p_be);
	memcpy(pbuf, password, plen);
	for (int counter = 1; counter < 256; counter++) {
		uint8_t seed[32], pwd[32];
		pbuf[plen] = counter;
		wpa_hmac_sha256(key, 12, pbuf, plen + 1, seed);
		wpa_kdf_sha256(seed, 32, "SAE Hunting and Pecking", p_be, 32, pwd, 256);
		bn x;
		bn_from_be(&x, pwd);
		if (bn_cmp(&x, &P256_P) >= 0) continue;
		/* y^2 = x^3 - 3x + b */
		bn x2, x3, three, ax, rhs, b_c;
		bn_set_u32(&three, 3);
		mod_mul(&x2, &x, &x, &P256_P);
		mod_mul(&x3, &x2, &x, &P256_P);
		mod_mul(&ax, &three, &x, &P256_P);
		mod_sub(&rhs, &x3, &ax, &P256_P);
		/* b of the curve */
		{ ecpt tmp = P256_G; bn y2, gx2, gx3, gax, gsub; mod_mul(&y2, &tmp.y, &tmp.y, &P256_P); mod_mul(&gx2, &tmp.x, &tmp.x, &P256_P); mod_mul(&gx3, &gx2, &tmp.x, &P256_P); mod_mul(&gax, &three, &tmp.x, &P256_P); mod_sub(&gsub, &gx3, &gax, &P256_P); mod_sub(&b_c, &y2, &gsub, &P256_P); }
		mod_add(&rhs, &rhs, &b_c, &P256_P);
		bn y;
		if (!p256_is_qr(&rhs) || !p256_sqrt(&y, &rhs)) continue;
		/* pick the root whose parity matches the least significant bit of the seed */
		if ((y.w[0] & 1) != (seed[31] & 1)) mod_sub(&y, &P256_P, &y, &P256_P);
		s->pwe.x = x; s->pwe.y = y; s->pwe.inf = false;
		return 0;
	}
	return -1;
}

void sae_commit(struct sae *s, uint8_t out[96])
{
	for (;;) {
		rand_scalar(&s->rand);
		rand_scalar(&s->mask);
		mod_add(&s->scalar, &s->rand, &s->mask, &P256_Q);
		bn one; bn_set_u32(&one, 1);
		if (bn_cmp(&s->scalar, &one) <= 0) continue;
		ecpt mp;
		p256_mul(&mp, &s->mask, &s->pwe);
		p256_neg(&s->element, &mp);
		break;
	}
	bn_to_be(&s->scalar, out);
	put_point(out + 32, &s->element);
	s->have_commit = true;
}

int sae_process_commit(struct sae *s, const uint8_t peer[96])
{
	bn one; bn_set_u32(&one, 1);
	bn_from_be(&s->peer_scalar, peer);
	if (bn_cmp(&s->peer_scalar, &one) <= 0 || bn_cmp(&s->peer_scalar, &P256_Q) >= 0) return -1;
	if (!get_point(&s->peer_element, peer + 32)) return -1;
	if (!bn_cmp(&s->peer_scalar, &s->scalar) && !bn_cmp(&s->peer_element.x, &s->element.x)) return -1;	/* reflection */
	/* K = rand * (peer_scalar * PWE + peer_element) */
	ecpt t, sum, k;
	p256_mul(&t, &s->peer_scalar, &s->pwe);
	p256_add(&sum, &t, &s->peer_element);
	if (sum.inf) return -1;
	p256_mul(&k, &s->rand, &sum);
	if (k.inf) return -1;
	uint8_t kx[32], zero[32] = {0}, keyseed[32], ctx[32], out[64];
	bn_to_be(&k.x, kx);
	wpa_hmac_sha256(zero, 32, kx, 32, keyseed);
	bn sc; mod_add(&sc, &s->scalar, &s->peer_scalar, &P256_Q);
	bn_to_be(&sc, ctx);
	wpa_kdf_sha256(keyseed, 32, "SAE KCK and PMK", ctx, 32, out, 512);
	memcpy(s->kck, out, 32); memcpy(s->pmk, out + 32, 32);
	memcpy(s->pmkid, ctx, 16);
	s->have_keys = true;
	return 0;
}

static void cn(const uint8_t kck[32], uint16_t sc, const bn *sa, const ecpt *ea, const bn *sb, const ecpt *eb, uint8_t out[32])
{
	uint8_t buf[2 + 32 + 64 + 32 + 64];
	buf[0] = sc & 0xff; buf[1] = sc >> 8;
	bn_to_be(sa, buf + 2); put_point(buf + 34, ea);
	bn_to_be(sb, buf + 98); put_point(buf + 130, eb);
	wpa_hmac_sha256(kck, 32, buf, sizeof buf, out);
}

void sae_confirm(struct sae *s, uint8_t out[32])
{
	cn(s->kck, s->send_confirm, &s->scalar, &s->element, &s->peer_scalar, &s->peer_element, out);
}

int sae_check_confirm(struct sae *s, uint16_t peer_send_confirm, const uint8_t conf[32])
{
	uint8_t exp[32];
	cn(s->kck, peer_send_confirm, &s->peer_scalar, &s->peer_element, &s->scalar, &s->element, exp);
	return memcmp(exp, conf, 32) ? -1 : 0;
}
