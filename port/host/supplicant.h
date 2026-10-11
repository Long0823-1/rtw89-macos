#ifndef SUPPLICANT_H
#define SUPPLICANT_H
void supplicant_start(const char *ssid, const char *psk);
void supplicant_start_sae(const unsigned char pmk[32]);
void supplicant_stop(void);
#endif
