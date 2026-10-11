// Dump RTL8852B physical efuse (DDV), decode logical map, print USB MAC address (@0x488) and rfe_type.
#define main poweron_main
#include "poweron.c"
#undef main

#define PHY_SIZE 1216
#define LOG_SIZE 2048
#define SEC_CTRL 4

static int dump_phy(u8 *map) {
    set8(R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
    wr16(R_AX_SYS_ISO_CTRL, rd16(R_AX_SYS_ISO_CTRL) | B_AX_PWC_EV2EF_B14);
    usleep(1000);
    wr16(R_AX_SYS_ISO_CTRL, rd16(R_AX_SYS_ISO_CTRL) | B_AX_PWC_EV2EF_B15);
    wr16(R_AX_SYS_ISO_CTRL, rd16(R_AX_SYS_ISO_CTRL) & ~B_AX_ISO_EB2CORE);
    int ret = 0;
    for (u32 a = 0; a < PHY_SIZE && !ret; a++) {
        wr32(R_AX_EFUSE_CTRL, FIELD_PREP(B_AX_EF_ADDR_MASK, a) & ~B_AX_EF_RDY);
        u32 v = 0; int i;
        for (i = 0; i < 1000; i++) { v = rd32(R_AX_EFUSE_CTRL); if (v & B_AX_EF_RDY) break; usleep(100); }
        if (i == 1000) { fprintf(stderr, "efuse timeout at %u\n", a); ret = -1; break; }
        map[a] = v & 0xff;
    }
    wr16(R_AX_SYS_ISO_CTRL, rd16(R_AX_SYS_ISO_CTRL) | B_AX_ISO_EB2CORE);
    wr16(R_AX_SYS_ISO_CTRL, rd16(R_AX_SYS_ISO_CTRL) & ~B_AX_PWC_EV2EF_B15);
    usleep(1000);
    wr16(R_AX_SYS_ISO_CTRL, rd16(R_AX_SYS_ISO_CTRL) & ~B_AX_PWC_EV2EF_B14);
    clr8(R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
    return ret;
}

static int decode_logical(const u8 *phy, u8 *log) {
    u32 p = SEC_CTRL;
    while (p < PHY_SIZE - SEC_CTRL) {
        u8 h1 = phy[p], h2 = phy[p + 1];
        if (h1 == 0xff || h2 == 0xff) break;
        u32 blk = ((h2 & 0xf0) >> 4) | ((h1 & 0x0f) << 4);
        u8 we = h2 & 0xf;
        p += 2;
        for (int i = 0; i < 4; i++) {
            if (we & BIT(i)) continue;
            u32 li = (blk << 3) + (i << 1);
            if (p + 1 > PHY_SIZE - SEC_CTRL - 1 || li + 1 > LOG_SIZE) return -1;
            log[li] = phy[p]; log[li + 1] = phy[p + 1];
            p += 2;
        }
    }
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (rtw_open()) { fprintf(stderr, "cannot open 35bc:0108\n"); return 1; }
    if (power_switch_on()) { fprintf(stderr, "power on failed\n"); return 2; }
    printf("autoload ok: %d\n", !!(rd16(R_AX_SYS_WL_EFUSE_CTRL) & B_AX_AUTOLOAD_SUS));
    static u8 phy[PHY_SIZE], log[LOG_SIZE];
    if (dump_phy(phy)) return 3;
    memset(log, 0xff, sizeof log);
    if (decode_logical(phy, log)) { fprintf(stderr, "logical decode failed\n"); return 4; }
    FILE *f = fopen("efuse_phy.bin", "wb"); fwrite(phy, 1, PHY_SIZE, f); fclose(f);
    f = fopen("efuse_log.bin", "wb"); fwrite(log, 1, LOG_SIZE, f); fclose(f);
    const u8 *m = log + 0x488;
    printf("MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n", m[0], m[1], m[2], m[3], m[4], m[5]);
    printf("rfe_type=%u xtal_k=0x%02x country=%02x%02x\n", log[0x2ca], log[0x2b9], log[0x2cb], log[0x2cc]);
    rtw_close();
    return 0;
}
