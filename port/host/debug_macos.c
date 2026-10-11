// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Replacement for rtw89's debug.c: env RTW89_DEBUG=<hex mask> enables rtw89_debug() categories.
#include "core.h"
#include "debug.h"
#include "reg.h"

unsigned int rtw89_debug_mask;

void rtw89_debug(struct rtw89_dev *rtwdev, enum rtw89_debug_mask mask, const char *fmt, ...)
{
	static int init;
	char buf[1024];
	va_list ap;
	if (!init) {
		const char *e = getenv("RTW89_DEBUG");
		rtw89_debug_mask = e ? (unsigned int)strtoul(e, NULL, 16) : 0;
		init = 1;
	}
	if (!(rtw89_debug_mask & mask)) return;
	va_start(ap, fmt);
	extern int kc_vsnprintf_pub(char *, size_t, const char *, va_list);
	kc_vsnprintf_pub(buf, sizeof buf, fmt, ap);
	va_end(ap);
	fputs(buf, stderr);
}

/* firmware self-halt code (R_AX_HALT_C2H); nonzero means the MAC reported a fatal error */
u32 rtw89_host_fw_halt_code(void *hw_priv)
{
	return rtw89_read32((struct rtw89_dev *)hw_priv, R_AX_HALT_C2H);
}
