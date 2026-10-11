// Port of rtw89_mac_power_switch() for RTL8852B over USB (rfe_type assumed != 5, efuse not yet valid).
#include "rtw.h"
#include <string.h>

static int poll32(u32 a, u32 m, int want_set) {
    for (int i = 0; i < 200; i++) { if (!!(rd32(a) & m) == want_set) return 0; usleep(1000); }
    fprintf(stderr, "poll timeout reg 0x%x mask 0x%x want %d (val 0x%x)\n", a, m, want_set, rd32(a));
    return -1;
}
static int xtal_w(u8 off, u8 val, u8 mask) {
    u32 v = FIELD_PREP(B_AX_WL_XTAL_SI_ADDR_MASK, off) | FIELD_PREP(B_AX_WL_XTAL_SI_DATA_MASK, val) |
            FIELD_PREP(B_AX_WL_XTAL_SI_BITMASK_MASK, mask) | FIELD_PREP(B_AX_WL_XTAL_SI_MODE_MASK, XTAL_SI_NORMAL_WRITE) |
            B_AX_WL_XTAL_SI_CMD_POLL;
    wr32(R_AX_WLAN_XTAL_SI_CTRL, v);
    return poll32(R_AX_WLAN_XTAL_SI_CTRL, B_AX_WL_XTAL_SI_CMD_POLL, 0);
}
#define X(off, val, mask) do { if (xtal_w(off, val, mask)) return -1; } while (0)

static void boot_mode(void) {
    if (!(rd32(R_AX_GPIO_MUXCFG) & B_AX_BOOT_MODE)) return;
    clr32(R_AX_SYS_PW_CTRL, B_AX_APFN_ONMAC);
    clr32(R_AX_SYS_STATUS1, B_AX_AUTO_WLPON);
    clr32(R_AX_GPIO_MUXCFG, B_AX_BOOT_MODE);
    clr32(R_AX_RSV_CTRL, B_AX_R_DIS_PRST);
}

static int pwr_off(void) {
    X(XTAL_SI_ANAPAR_WL, XTAL_SI_RFC2RF, XTAL_SI_RFC2RF);
    X(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_OFF_EI);
    X(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_OFF_WEI);
    X(XTAL_SI_WL_RFC_S0, 0, XTAL_SI_RF00);
    X(XTAL_SI_WL_RFC_S1, 0, XTAL_SI_RF10);
    X(XTAL_SI_ANAPAR_WL, XTAL_SI_SRAM2RFC, XTAL_SI_SRAM2RFC);
    X(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_PON_EI);
    X(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_PON_WEI);
    set32(R_AX_SYS_PW_CTRL, B_AX_EN_WLON);
    clr32(R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
    clr8(R_AX_SYS_FUNC_EN, B_AX_FEN_BB_GLB_RSTN | B_AX_FEN_BBRSTB);
    clr32(R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_RFC_1P3);
    X(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_SHDN_WL);
    clr32(R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_PTA_1P3);
    X(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_GND_SHDN_WL);
    set32(R_AX_SYS_PW_CTRL, B_AX_APFM_OFFMAC);
    if (poll32(R_AX_SYS_PW_CTRL, B_AX_APFM_OFFMAC, 0)) return -1;
    clr32(R_AX_SYS_PW_CTRL, B_AX_SOP_EDSWR);          // USB
    set32(R_AX_SYS_SWR_CTRL1, B_AX_SYM_CTRL_SPS_PWMFREQ);
    mask32(R_AX_SPS_DIG_ON_CTRL0, B_AX_REG_ZCDC_H_MASK, 0x3);
    u32 v = rd32(R_AX_SYS_PW_CTRL);                   // USB
    v &= ~B_AX_AFSM_PCIE_SUS_EN; v |= B_AX_AFSM_WLSUS_EN;
    wr32(R_AX_SYS_PW_CTRL, v);
    return 0;
}

static int pwr_on(void) {
    clr32(R_AX_SYS_PW_CTRL, B_AX_AFSM_WLSUS_EN | B_AX_AFSM_PCIE_SUS_EN);
    set32(R_AX_SYS_PW_CTRL, B_AX_DIS_WLBT_PDNSUSEN_SOPC);
    set32(R_AX_WLLPS_CTRL, B_AX_DIS_WLBT_LPSEN_LOPC);
    clr32(R_AX_SYS_PW_CTRL, B_AX_APDM_HPDN);
    clr32(R_AX_SYS_PW_CTRL, B_AX_APFM_SWLPS);
    if (poll32(R_AX_SYS_PW_CTRL, B_AX_RDY_SYSPWR, 1)) return -1;
    set32(R_AX_AFE_LDO_CTRL, B_AX_AON_OFF_PC_EN);
    if (poll32(R_AX_AFE_LDO_CTRL, B_AX_AON_OFF_PC_EN, 1)) return -1;
    mask32(R_AX_SPS_DIG_OFF_CTRL0, B_AX_C1_L1_MASK, 0x1);   // sps_dig_off (rfe != 5)
    mask32(R_AX_SPS_DIG_OFF_CTRL0, B_AX_C3_L1_MASK, 0x3);
    set32(R_AX_SYS_PW_CTRL, B_AX_EN_WLON);
    set32(R_AX_SYS_PW_CTRL, B_AX_APFN_ONMAC);
    if (poll32(R_AX_SYS_PW_CTRL, B_AX_APFN_ONMAC, 0)) return -1;
    set8(R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN); clr8(R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
    set8(R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN); clr8(R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
    set8(R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
    set32(R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_PTA_1P3);
    X(XTAL_SI_ANAPAR_WL, XTAL_SI_GND_SHDN_WL, XTAL_SI_GND_SHDN_WL);
    set32(R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_RFC_1P3);
    X(XTAL_SI_ANAPAR_WL, XTAL_SI_SHDN_WL, XTAL_SI_SHDN_WL);
    X(XTAL_SI_ANAPAR_WL, XTAL_SI_OFF_WEI, XTAL_SI_OFF_WEI);
    X(XTAL_SI_ANAPAR_WL, XTAL_SI_OFF_EI, XTAL_SI_OFF_EI);
    X(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_RFC2RF);
    X(XTAL_SI_ANAPAR_WL, XTAL_SI_PON_WEI, XTAL_SI_PON_WEI);
    X(XTAL_SI_ANAPAR_WL, XTAL_SI_PON_EI, XTAL_SI_PON_EI);
    X(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_SRAM2RFC);
    X(XTAL_SI_SRAM_CTRL, 0, XTAL_SI_SRAM_DIS);
    X(XTAL_SI_XTAL_XMD_2, 0, XTAL_SI_LDO_LPS);
    X(XTAL_SI_XTAL_XMD_4, 0, XTAL_SI_LPS_CAP);
    set32(R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
    set32(R_AX_SYS_ISO_CTRL, B_AX_ISO_EB2CORE);
    clr32(R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B15);
    usleep(1000);
    clr32(R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B14);
    clr32(R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
    set32(R_AX_DMAC_FUNC_EN, B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN | B_AX_MPDU_PROC_EN | B_AX_WD_RLS_EN | B_AX_DLE_WDE_EN |
          B_AX_TXPKT_CTRL_EN | B_AX_STA_SCH_EN | B_AX_DLE_PLE_EN | B_AX_PKT_BUF_EN | B_AX_DMAC_TBL_EN | B_AX_PKT_IN_EN |
          B_AX_DLE_CPUIO_EN | B_AX_DISPATCHER_EN | B_AX_BBRPT_EN | B_AX_MAC_SEC_EN | B_AX_DMACREG_GCKEN);
    set32(R_AX_CMAC_FUNC_EN, B_AX_CMAC_EN | B_AX_CMAC_TXEN | B_AX_CMAC_RXEN | B_AX_FORCE_CMACREG_GCKEN | B_AX_PHYINTF_EN |
          B_AX_CMAC_DMA_EN | B_AX_PTCLTOP_EN | B_AX_SCHEDULER_EN | B_AX_TMAC_EN | B_AX_RMAC_EN);
    mask32(R_AX_EECS_EESK_FUNC_SEL, B_AX_PINMUX_EESK_FUNC_SEL_MASK, PINMUX_EESK_FUNC_SEL_BT_LOG);
    return 0;
}

static int power_switch_on(void) {
    boot_mode();
    u32 st = FIELD_GET(B_AX_WLMAC_PWR_STE_MASK, rd32(R_AX_IC_PWR_STATE));
    printf("initial MAC power state = %u (0 off,1 on,2 lps)\n", st);
    if (st == MAC_AX_MAC_ON) {            // came from mass-storage mode: power off first, as rtw89 does on -EAGAIN
        printf("already on -> power off/on cycle\n");
        if (pwr_off()) return -1;
        boot_mode();
    }
    return pwr_on();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (rtw_open()) { fprintf(stderr, "cannot open 35bc:0108\n"); return 1; }
    printf("SYS_CFG1 = 0x%08x\n", rd32(R_AX_SYS_CFG1));
    int r = power_switch_on();
    printf("power on: %s\n", r ? "FAILED" : "ok");
    printf("IC_PWR_STATE = 0x%08x  (MAC state %u)\n", rd32(R_AX_IC_PWR_STATE), FIELD_GET(B_AX_WLMAC_PWR_STE_MASK, rd32(R_AX_IC_PWR_STATE)));
    printf("SYS_PW_CTRL = 0x%08x  DMAC_FUNC_EN = 0x%08x  CMAC_FUNC_EN = 0x%08x\n", rd32(R_AX_SYS_PW_CTRL), rd32(R_AX_DMAC_FUNC_EN), rd32(R_AX_CMAC_FUNC_EN));
    rtw_close();
    return r ? 2 : 0;
}
