// Port of rtw89_mac_partial_init() pre-firmware steps for RTL8852B over USB:
//  ctrl_hci_dma_trx, dmac_pre_init (hci_func_en, dmac_func_pre_en, DLE init DLFW, HFC init DLFW), usb mac_pre_init.
#pragma once
#define DLE_BOUND_UNIT (8 * 1024)
#define S_AX_WDE_PAGE_SEL_64 0
#define S_AX_PLE_PAGE_SEL_128 1

static int pre_poll32(u32 a, u32 m) {
    for (int i = 0; i < 200; i++) { if ((rd32(a) & m) == m) return 0; usleep(100); }
    return -1;
}

static int dle_init_dlfw(void) {
    // DLFW tables: wde_size9 {PG_64, lnk 0, unlnk 1024}; ple_size8 {PG_128, lnk 64, unlnk 960}
    // quotas: wde_qt4 = zeros (min/max); ple_qt13 = {0,0,c2h 16,h2c 48,0,...}; ext wde min wcpu from SCC wde_qt30.wcpu = 2
    const u32 wde_lnk = 0, wde_unlnk = 1024, wde_pg = 64, ple_lnk = 64;
    u32 val;
    clr32(R_AX_DMAC_FUNC_EN, B_AX_DLE_WDE_EN | B_AX_DLE_PLE_EN);          // dle_func_en(false)
    set32(R_AX_DMAC_CLK_EN, B_AX_DLE_WDE_CLK_EN | B_AX_DLE_PLE_CLK_EN);    // dle_clk_en(true)
    // dle_mix_cfg
    val = rd32(R_AX_WDE_PKTBUF_CFG);
    val = (val & ~B_AX_WDE_PAGE_SEL_MASK) | FIELD_PREP(B_AX_WDE_PAGE_SEL_MASK, S_AX_WDE_PAGE_SEL_64);
    val = (val & ~B_AX_WDE_START_BOUND_MASK) | FIELD_PREP(B_AX_WDE_START_BOUND_MASK, 0);
    val = (val & ~B_AX_WDE_FREE_PAGE_NUM_MASK) | FIELD_PREP(B_AX_WDE_FREE_PAGE_NUM_MASK, wde_lnk);
    wr32(R_AX_WDE_PKTBUF_CFG, val);
    u32 bound = (wde_lnk + wde_unlnk) * wde_pg / DLE_BOUND_UNIT;
    val = rd32(R_AX_PLE_PKTBUF_CFG);
    val = (val & ~B_AX_PLE_PAGE_SEL_MASK) | FIELD_PREP(B_AX_PLE_PAGE_SEL_MASK, S_AX_PLE_PAGE_SEL_128);
    val = (val & ~B_AX_PLE_START_BOUND_MASK) | FIELD_PREP(B_AX_PLE_START_BOUND_MASK, bound);
    val = (val & ~B_AX_PLE_FREE_PAGE_NUM_MASK) | FIELD_PREP(B_AX_PLE_FREE_PAGE_NUM_MASK, ple_lnk);
    wr32(R_AX_PLE_PKTBUF_CFG, val);
    // WDE quota (min wcpu overridden by ext = 2, max wcpu = 0)
    #define Q(mn, mx, reg) wr32(reg, FIELD_PREP(GENMASK(11, 0), mn) | FIELD_PREP(GENMASK(27, 16), mx))
    Q(0, 0, R_AX_WDE_QTA0_CFG); Q(2, 0, R_AX_WDE_QTA1_CFG); Q(0, 0, R_AX_WDE_QTA3_CFG); Q(0, 0, R_AX_WDE_QTA4_CFG);
    // PLE quota ple_qt13 order: cma0_tx,cma1_tx,c2h,h2c,wcpu,mpdu_proc,cma0_dma,cma1_dma,bb_rpt,wd_rel,cpu_io
    static const u16 ple[11] = {0, 0, 16, 48, 0, 0, 0, 0, 0, 0, 0};
    static const u32 reg[11] = {R_AX_PLE_QTA0_CFG, R_AX_PLE_QTA1_CFG, R_AX_PLE_QTA2_CFG, R_AX_PLE_QTA3_CFG, R_AX_PLE_QTA4_CFG,
        R_AX_PLE_QTA5_CFG, R_AX_PLE_QTA6_CFG, R_AX_PLE_QTA7_CFG, R_AX_PLE_QTA8_CFG, R_AX_PLE_QTA9_CFG, R_AX_PLE_QTA10_CFG};
    for (int i = 0; i < 11; i++) Q(ple[i], ple[i], reg[i]);
    set32(R_AX_DMAC_FUNC_EN, B_AX_DLE_WDE_EN | B_AX_DLE_PLE_EN);           // dle_func_en(true)
    if (pre_poll32(R_AX_WDE_INI_STATUS, WDE_MGN_INI_RDY)) { fprintf(stderr, "WDE not ready: 0x%x\n", rd32(R_AX_WDE_INI_STATUS)); return -1; }
    if (pre_poll32(R_AX_PLE_INI_STATUS, PLE_MGN_INI_RDY)) { fprintf(stderr, "PLE not ready: 0x%x\n", rd32(R_AX_PLE_INI_STATUS)); return -1; }
    #undef Q
    return 0;
}

static void hfc_init_dlfw(void) {
    // reset=true, en=false, h2c_en=true -> hfc_func_en(false,false); hfc_h2c_cfg; hfc_func_en(false,true)
    // DLFW param: prec_cfg = rtw8852b_hfc_preccfg_usb {9, 32(H2C pre-cost), 64, 24, 1, 1, 1, 1}, mode STF(1)
    u32 v = rd32(R_AX_HCI_FC_CTRL);
    wr32(R_AX_HCI_FC_CTRL, v & ~(B_AX_HCI_FC_EN | B_AX_HCI_FC_CH12_EN));
    wr32(R_AX_CH_PAGE_CTRL, FIELD_PREP(B_AX_PREC_PAGE_CH12_MASK, 32));      // hfc_h2c_cfg
    mask32(R_AX_HCI_FC_CTRL, B_AX_HCI_FC_CH12_FULL_COND_MASK, 1);
    v = rd32(R_AX_HCI_FC_CTRL);
    wr32(R_AX_HCI_FC_CTRL, (v & ~B_AX_HCI_FC_EN) | B_AX_HCI_FC_CH12_EN);   // hfc_func_en(false, true)
}

static int pre_fwdl_init(void) {
    set32(R_AX_HCI_FUNC_EN, B_AX_HCI_TXDMA_EN | B_AX_HCI_RXDMA_EN);          // rtw89_mac_ctrl_hci_dma_trx(true)
    wr32(R_AX_DMAC_FUNC_EN, B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN | B_AX_DISPATCHER_EN | B_AX_PKT_BUF_EN);  // hci_func_en_ax
    wr32(R_AX_DMAC_CLK_EN, B_AX_DISPATCHER_CLK_EN);                           // dmac_func_pre_en_ax (8852B)
    if (dle_init_dlfw()) return -1;
    hfc_init_dlfw();
    // usb mac_pre_init
    set32(R_AX_USB_HOST_REQUEST_2, B_AX_R_USBIO_MODE);
    clr32(R_AX_USB_WLAN0_1, B_AX_USBRX_RST | B_AX_USBTX_RST);
    u32 v = rd32(R_AX_HCI_FUNC_EN);
    v &= ~(B_AX_HCI_RXDMA_EN | B_AX_HCI_TXDMA_EN);
    wr32(R_AX_HCI_FUNC_EN, v);
    v |= B_AX_HCI_RXDMA_EN | B_AX_HCI_TXDMA_EN;
    wr32(R_AX_HCI_FUNC_EN, v);
    return 0;
}
