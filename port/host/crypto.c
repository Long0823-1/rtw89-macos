// SPDX-License-Identifier: BSD-3-Clause
// WPA2 crypto primitives on top of macOS CommonCrypto.
#include <CommonCrypto/CommonCrypto.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#include <CommonCrypto/CommonSymmetricKeywrap.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "crypto.h"

void wpa_pbkdf2(const char *pass, const uint8_t *ssid, size_t ssid_len, uint8_t out[32])
{
	CCKeyDerivationPBKDF(kCCPBKDF2, pass, strlen(pass), ssid, ssid_len, kCCPRFHmacAlgSHA1, 4096, out, 32);
}

void wpa_hmac_sha1(const uint8_t *key, size_t keylen, const uint8_t *data, size_t len, uint8_t out[20])
{
	CCHmac(kCCHmacAlgSHA1, key, keylen, data, len, out);
}

/* IEEE 802.11 PRF: HMAC-SHA1(K, A || 0 || B || i) */
void wpa_prf_sha1(const uint8_t *key, size_t keylen, const char *label, const uint8_t *data, size_t dlen, uint8_t *out, size_t outlen)
{
	size_t llen = strlen(label);
	uint8_t *buf = malloc(llen + 1 + dlen + 1);
	uint8_t digest[20];
	size_t pos = 0;
	memcpy(buf, label, llen);
	buf[llen] = 0;
	memcpy(buf + llen + 1, data, dlen);
	for (uint8_t i = 0; pos < outlen; i++) {
		buf[llen + 1 + dlen] = i;
		wpa_hmac_sha1(key, keylen, buf, llen + 1 + dlen + 1, digest);
		size_t n = outlen - pos < 20 ? outlen - pos : 20;
		memcpy(out + pos, digest, n);
		pos += n;
	}
	free(buf);
}

int wpa_aes_unwrap(const uint8_t kek[16], const uint8_t *wrapped, size_t wlen, uint8_t *out, size_t *outlen)
{
	size_t n = wlen;
	int r = CCSymmetricKeyUnwrap(kCCWRAPAES, CCrfc3394_iv, CCrfc3394_ivLen, kek, 16, wrapped, wlen, out, &n);
	if (r != kCCSuccess) return -1;
	*outlen = n;
	return 0;
}

void wpa_random(uint8_t *buf, size_t n)
{
	arc4random_buf(buf, n);
}

/* ---------------- SHA-256 based primitives (WPA3 / 802.11w) ---------------- */
void wpa_hmac_sha256(const uint8_t *key, size_t keylen, const uint8_t *data, size_t len, uint8_t out[32])
{
	CCHmac(kCCHmacAlgSHA256, key, keylen, data, len, out);
}

/* IEEE 802.11 KDF-Hash-Length: K(i) = HMAC(key, i(LE16) || label || context || length(LE16)) */
void wpa_kdf_sha256(const uint8_t *key, size_t keylen, const char *label, const uint8_t *ctx, size_t ctxlen, uint8_t *out, size_t bits)
{
	size_t llen = strlen(label), n = (bits + 255) / 256, pos = 0, outlen = (bits + 7) / 8;
	uint8_t *buf = malloc(2 + llen + ctxlen + 2);
	uint8_t d[32];
	memcpy(buf + 2, label, llen);
	memcpy(buf + 2 + llen, ctx, ctxlen);
	buf[2 + llen + ctxlen] = bits & 0xff;
	buf[2 + llen + ctxlen + 1] = bits >> 8;
	for (size_t i = 1; i <= n; i++) {
		buf[0] = i & 0xff; buf[1] = i >> 8;
		wpa_hmac_sha256(key, keylen, buf, 2 + llen + ctxlen + 2, d);
		size_t c = outlen - pos < 32 ? outlen - pos : 32;
		memcpy(out + pos, d, c);
		pos += c;
	}
	if (bits % 8) out[outlen - 1] &= (uint8_t)(0xff << (8 - bits % 8)) ;	/* keep the leading bits */
	free(buf);
}

/* AES-128-CMAC (RFC 4493) */
static void aes_ecb(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
	size_t moved = 0;
	CCCrypt(kCCEncrypt, kCCAlgorithmAES, kCCOptionECBMode, key, 16, NULL, in, 16, out, 16, &moved);
}

static void xor_dbl(uint8_t *b)		/* GF(2^128) doubling */
{
	uint8_t carry = b[0] >> 7;
	for (int i = 0; i < 15; i++) b[i] = (b[i] << 1) | (b[i + 1] >> 7);
	b[15] <<= 1;
	if (carry) b[15] ^= 0x87;
}

void wpa_aes_cmac(const uint8_t key[16], const uint8_t *data, size_t len, uint8_t out[16])
{
	uint8_t k1[16] = {0}, k2[16], x[16] = {0}, y[16], last[16];
	aes_ecb(key, k1, k1);
	xor_dbl(k1);
	memcpy(k2, k1, 16);
	xor_dbl(k2);
	size_t n = (len + 15) / 16;
	bool complete = n && (len % 16 == 0);
	if (!n) n = 1;
	for (size_t i = 0; i + 1 < n; i++) {
		for (int j = 0; j < 16; j++) y[j] = x[j] ^ data[16 * i + j];
		aes_ecb(key, y, x);
	}
	size_t rem = len - 16 * (n - 1);
	if (complete) { for (int j = 0; j < 16; j++) last[j] = data[16 * (n - 1) + j] ^ k1[j]; }
	else {
		uint8_t pad[16] = {0};
		memcpy(pad, data + 16 * (n - 1), rem);
		pad[rem] = 0x80;
		for (int j = 0; j < 16; j++) last[j] = pad[j] ^ k2[j];
	}
	for (int j = 0; j < 16; j++) y[j] = x[j] ^ last[j];
	aes_ecb(key, y, out);
}
