// Minimal userspace shim of rtw89 register access over IOKit, for RTL8852B @ 35bc:0108.
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
#define BIT(n) (1u << (n))
#define GENMASK(h, l) (((~0u) >> (31 - (h))) & ((~0u) << (l)))
#define XTAL_SI_BITOP
#include "../rtw89/src/reg.h"
#define FIELD_PREP(mask, v) (((u32)(v) << __builtin_ctz(mask)) & (mask))
#define FIELD_GET(mask, v) (((u32)(v) & (mask)) >> __builtin_ctz(mask))
// XTAL SI offsets/bits (from mac.h)
#define XTAL_SI_WL_RFC_S0 0x80
#define XTAL_SI_RF00 BIT(0)
#define XTAL_SI_WL_RFC_S1 0x81
#define XTAL_SI_RF10 BIT(0)
#define XTAL_SI_ANAPAR_WL 0x90
#define XTAL_SI_SRAM2RFC BIT(7)
#define XTAL_SI_GND_SHDN_WL BIT(6)
#define XTAL_SI_SHDN_WL BIT(5)
#define XTAL_SI_RFC2RF BIT(4)
#define XTAL_SI_OFF_EI BIT(3)
#define XTAL_SI_OFF_WEI BIT(2)
#define XTAL_SI_PON_EI BIT(1)
#define XTAL_SI_PON_WEI BIT(0)
#define XTAL_SI_SRAM_CTRL 0xA1
#define XTAL_SI_SRAM_DIS BIT(1)
#define XTAL_SI_XTAL_XMD_2 0x24
#define XTAL_SI_LDO_LPS GENMASK(6, 4)
#define XTAL_SI_XTAL_XMD_4 0x26
#define XTAL_SI_LPS_CAP GENMASK(3, 0)

extern IOUSBDeviceInterface942 **g_di;
int rtw_open(void);
void rtw_close(void);
u32 rd32(u32 a); u16 rd16(u32 a); u8 rd8(u32 a);
void wr32(u32 a, u32 v); void wr16(u32 a, u16 v); void wr8(u32 a, u8 v);
static inline void set32(u32 a, u32 b) { wr32(a, rd32(a) | b); }
static inline void clr32(u32 a, u32 b) { wr32(a, rd32(a) & ~b); }
static inline void set8(u32 a, u8 b) { wr8(a, rd8(a) | b); }
static inline void clr8(u32 a, u8 b) { wr8(a, rd8(a) & ~b); }
static inline void mask32(u32 a, u32 m, u32 v) { wr32(a, (rd32(a) & ~m) | FIELD_PREP(m, v)); }
