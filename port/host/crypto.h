#ifndef CRYPTO_H
#define CRYPTO_H
#include <stdint.h>
#include <stddef.h>
void wpa_pbkdf2(const char *pass, const uint8_t *ssid, size_t ssid_len, uint8_t out[32]);
void wpa_hmac_sha1(const uint8_t *key, size_t keylen, const uint8_t *data, size_t len, uint8_t out[20]);
void wpa_prf_sha1(const uint8_t *key, size_t keylen, const char *label, const uint8_t *data, size_t dlen, uint8_t *out, size_t outlen);
int wpa_aes_unwrap(const uint8_t kek[16], const uint8_t *wrapped, size_t wlen, uint8_t *out, size_t *outlen);
void wpa_random(uint8_t *buf, size_t n);
void wpa_hmac_sha256(const uint8_t *key, size_t keylen, const uint8_t *data, size_t len, uint8_t out[32]);
void wpa_kdf_sha256(const uint8_t *key, size_t keylen, const char *label, const uint8_t *ctx, size_t ctxlen, uint8_t *out, size_t bits);
void wpa_aes_cmac(const uint8_t key[16], const uint8_t *data, size_t len, uint8_t out[16]);
#endif
