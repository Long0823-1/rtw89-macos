// RTL8852B firmware download over USB (port of rtw89 fw.c / mac.c fwdl path).
// usage: fwdl parse <fw.bin>   (offline: show container/sections)
//        fwdl run   <fw.bin>   (power on + download + wait for FW ready)
#define main poweron_main
#include "poweron.c"
#undef main
#include "fwdefs.h"
#include "prefw.h"
#include <stdlib.h>
#include <string.h>

typedef struct { u8 type; u32 len, dladdr; const u8 *addr; int ignore; } secinfo;
typedef struct { u32 hdr_len, dyn_hdr_len, part_size; int nsec, hdr_ver, idmem_share, secure_section_exist, dsp_checksum; secinfo sec[16]; } fwinfo;

static u8 *load(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc(*len); fread(b, 1, *len, f); fclose(f); return b;
}

static int mfw_pick(const u8 *fw, size_t len, int type, u8 hal_cv, const u8 **data, u32 *size) {
    const struct mfw_hdr *h = (const void *)fw;
    if (len < sizeof *h || h->sig != RTW89_MFW_SIG) return -1;
    const struct mfw_info *best = NULL;
    for (int i = 0; i < h->fw_nr; i++) {
        const struct mfw_info *t = &h->info[i];
        if (t->type != type) continue;
        if (t->cv <= hal_cv && !t->mp && (!best || best->cv < t->cv)) best = t;
    }
    if (!best) return -1;
    *data = fw + best->shift; *size = best->size;
    if (*data + *size > fw + len) return -2;
    return 0;
}

static int parse_hdr(const u8 *fw, u32 len, fwinfo *in, int secure_boot) {
    const u32 *w = (const u32 *)fw;
    in->hdr_ver = FIELD_GET(FW_HDR_W3_HDR_VER, w[3]);
    int v1 = in->hdr_ver == 1;
    if (in->hdr_ver > 1) { fprintf(stderr, "unknown hdr ver %d\n", in->hdr_ver); return -1; }
    u32 w5 = w[5], w6 = w[6], w7 = w[7];
    in->nsec = FIELD_GET(GENMASK(15, 8), w6);
    in->dsp_checksum = v1 ? !!(w6 & FW_HDR_V1_W6_DSP_CHKSUM) : 0;
    u32 base = (v1 ? 48 : 32) + in->nsec * 16;
    int dyn = !!(w7 & BIT(16));
    in->idmem_share = FIELD_GET(GENMASK(21, 18), w7);
    in->part_size = FWDL_SECTION_PER_PKT_LEN;
    if (dyn) {
        in->hdr_len = v1 ? FIELD_GET(FW_HDR_V1_W5_HDR_SIZE, w5) : FIELD_GET(FW_HDR_W3_LEN, w[3]);
        in->dyn_hdr_len = in->hdr_len - base;
        if (*(const u32 *)(fw + base) != in->dyn_hdr_len) { fprintf(stderr, "bad dyn hdr len\n"); return -1; }
    } else { in->hdr_len = base; in->dyn_hdr_len = 0; }
    const u8 *bin = fw + in->hdr_len;
    const struct fw_sec *sec = (const struct fw_sec *)(fw + (v1 ? 48 : 32));
    for (int i = 0; i < in->nsec; i++) {
        secinfo *s = &in->sec[i];
        s->type = FIELD_GET(FWSEC_W1_SECTIONTYPE, sec[i].w1);
        s->len = FIELD_GET(FWSEC_W1_SEC_SIZE, sec[i].w1);
        if (sec[i].w1 & FWSEC_W1_CHECKSUM) s->len += FWDL_SECTION_CHKSUM_LEN;
        s->dladdr = sec[i].w0 & (v1 ? 0xffffffffu : 0x1fffffffu);
        s->addr = bin; s->ignore = 0;
        u32 mssc_len = 0;
        if (s->type == FWDL_SECURITY_SECTION_TYPE) {
            u32 mssc = v1 ? FIELD_GET(FWSEC_V1_W2_MSSC, sec[i].w2) : sec[i].w2;
            if (v1 && mssc == FORMATTED_MSSC) {
                const u8 *mss = bin + s->len;
                if (memcmp(mss, "MSSKPOOL", 8)) { fprintf(stderr, "bad MSS signature\n"); return -1; }
                u32 rmpfmt = mss[0x14], mssdev_max = mss[0x15];
                u16 keypair_num = *(const u16 *)(mss + 0x16), msscust_max = *(const u16 *)(mss + 0x18), msskey_num_max = *(const u16 *)(mss + 0x1a);
                u32 defen = mss[0x10];
                u32 rmp = ((u32)msskey_num_max * msscust_max * mssdev_max) >> 3; if (defen) rmp += 8;
                (void)rmpfmt;
                u32 ksl = *(const u16 *)(bin + 60) >> 2; if (!ksl) ksl = 512;
                if (in->dsp_checksum) ksl += FWDL_SECURITY_CHKSUM_LEN;
                mssc_len = 0x1c + rmp + keypair_num * ksl;   // sizeof(mss_pool_hdr)=0x1c
                if (in->secure_section_exist) s->ignore = 1; else in->secure_section_exist = 1;
            } else {
                mssc_len = mssc * FWDL_SECURITY_SIGLEN;
                if (in->dsp_checksum) mssc_len += mssc * FWDL_SECURITY_CHKSUM_LEN;
                in->secure_section_exist = 1;
            }
            if (secure_boot) { fprintf(stderr, "secure boot not supported in this prototype\n"); return -1; }
        }
        bin += s->len + mssc_len;
    }
    if (bin != fw + len) { fprintf(stderr, "fw size mismatch: end %td vs %u\n", bin - fw, len); return -1; }
    return 0;
}

static void dump_info(const fwinfo *in) {
    printf("hdr_ver=%d sections=%d hdr_len=%u dyn=%u part=%u idmem=%d dsp_chk=%d\n", in->hdr_ver, in->nsec, in->hdr_len, in->dyn_hdr_len, in->part_size, in->idmem_share, in->dsp_checksum);
    for (int i = 0; i < in->nsec; i++) printf("  sec[%d] type=%u len=0x%x dladdr=0x%08x ignore=%d\n", i, in->sec[i].type, in->sec[i].len, in->sec[i].dladdr, in->sec[i].ignore);
}

// ---- USB bulk ----
static IOUSBInterfaceInterface942 **g_if;
static UInt8 out_pipe[8]; static int n_out;

static int open_iface(void) {
    IOUSBFindInterfaceRequest rq = { kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare };
    io_iterator_t it; (*g_di)->CreateInterfaceIterator(g_di, &rq, &it);
    io_service_t s = IOIteratorNext(it); if (!s) { fprintf(stderr, "no interface (device unconfigured?)\n"); return -1; }
    IOCFPlugInInterface **pl; SInt32 sc;
    IOCreatePlugInInterfaceForService(s, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &pl, &sc);
    (*pl)->QueryInterface(pl, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID942), (LPVOID *)&g_if);
    IODestroyPlugInInterface(pl);
    IOReturn r = (*g_if)->USBInterfaceOpenSeize(g_if);
    if (r) { fprintf(stderr, "interface open: 0x%x\n", r); return -1; }
    UInt8 n; (*g_if)->GetNumEndpoints(g_if, &n);
    for (UInt8 i = 1; i <= n; i++) {
        UInt8 dir, num, tt, in; UInt16 mps;
        (*g_if)->GetPipeProperties(g_if, i, &dir, &num, &tt, &mps, &in);
        printf("  pipe %d: ep %d %s type %d mps %d\n", i, num, dir == kUSBIn ? "IN" : "OUT", tt, mps);
        if (dir == kUSBOut && tt == kUSBBulk && n_out < 8) out_pipe[n_out++] = i;
    }
    return 0;
}

static int bulk_out(int bulkout_id, const u8 *data, u32 len) {
    if (bulkout_id >= n_out) { fprintf(stderr, "bulkout id %d >= %d out pipes\n", bulkout_id, n_out); return -1; }
    IOReturn r = (*g_if)->WritePipeTO(g_if, out_pipe[bulkout_id], (void *)data, len, 2000, 4000);
    if (r) { fprintf(stderr, "bulk write (pipe %d, %u bytes) failed 0x%x\n", out_pipe[bulkout_id], len, r); return -1; }
    return 0;
}

static u32 h2c_seq;
// send one FWCMD skb: payload (already containing H2C header if any) with 24B TX desc, fw_dl flag
static int h2c_tx(const u8 *payload, u32 plen, int fw_dl) {
    u32 pkt = plen, pad = 0;
    if (((pkt + TXWD_SIZE) % 512) == 0) { pad = 4; pkt += 4; }
    u8 *buf = calloc(1, TXWD_SIZE + pkt);
    u32 d0 = FIELD_PREP(RTW89_TXWD_BODY0_CHANNEL_DMA, RTW89_DMA_H2C) | (fw_dl ? RTW89_TXWD_BODY0_FW_DL : 0);
    u32 d2 = FIELD_PREP(RTW89_TXWD_BODY2_TXPKT_SIZE, pkt);
    memcpy(buf, &d0, 4); memcpy(buf + 8, &d2, 4);
    memcpy(buf + TXWD_SIZE, payload, plen);
    int r = bulk_out(2 /* bulkout_id[H2C] */, buf, TXWD_SIZE + pkt);
    free(buf); (void)pad; return r;
}

static int poll_ready(u8 mask, int ms) {
    for (int i = 0; i < ms; i++) { if (rd8(R_AX_WCPU_FW_CTRL) & mask) return 0; usleep(1000); }
    return -1;
}
static int fwdl_status(void) { return FIELD_GET(B_AX_WCPU_FWDL_STS_MASK, rd8(R_AX_WCPU_FW_CTRL)); }

static void disable_cpu(void) {
    clr32(R_AX_PLATFORM_ENABLE, B_AX_WCPU_EN);
    clr32(R_AX_WCPU_FW_CTRL, B_AX_WCPU_FWDL_EN | B_AX_H2C_PATH_RDY | B_AX_FWDL_PATH_RDY);
    clr32(R_AX_SYS_CLK_CTRL, B_AX_CPU_CLK_EN);
    clr32(R_AX_PLATFORM_ENABLE, B_AX_APB_WRAP_EN); set32(R_AX_PLATFORM_ENABLE, B_AX_APB_WRAP_EN);  // 885xb wdt disable
    clr32(R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN); set32(R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
}
static int enable_cpu_dlfw(void) {
    if (rd32(R_AX_PLATFORM_ENABLE) & B_AX_WCPU_EN) return -1;
    wr32(R_AX_UDM1, 0); wr32(R_AX_UDM2, 0);
    wr32(R_AX_HALT_H2C_CTRL, 0); wr32(R_AX_HALT_C2H_CTRL, 0); wr32(R_AX_HALT_H2C, 0); wr32(R_AX_HALT_C2H, 0);
    set32(R_AX_SYS_CLK_CTRL, B_AX_CPU_CLK_EN);
    u32 val = rd32(R_AX_WCPU_FW_CTRL);
    val &= ~(B_AX_WCPU_FWDL_EN | B_AX_H2C_PATH_RDY | B_AX_FWDL_PATH_RDY);
    val = (val & ~B_AX_WCPU_FWDL_STS_MASK) | FIELD_PREP(B_AX_WCPU_FWDL_STS_MASK, RTW89_FWDL_INITIAL_STATE);
    val |= B_AX_WCPU_FWDL_EN;
    wr32(R_AX_WCPU_FW_CTRL, val);
    mask32(R_AX_SEC_CTRL, B_AX_SEC_IDMEM_SIZE_CONFIG_MASK, 0x2);
    wr16(R_AX_BOOT_REASON, (rd16(R_AX_BOOT_REASON) & ~B_AX_BOOT_REASON_MASK) | 0);
    set32(R_AX_PLATFORM_ENABLE, B_AX_WCPU_EN);
    return 0;
}

static int download(const u8 *fw, u32 len, fwinfo *in) {
    disable_cpu();
    if (enable_cpu_dlfw()) { fprintf(stderr, "enable cpu failed\n"); return -1; }
    if (poll_ready(B_AX_H2C_PATH_RDY, 500)) { fprintf(stderr, "H2C path not ready (ctrl=0x%08x)\n", rd32(R_AX_WCPU_FW_CTRL)); return -1; }
    // header download
    u32 hlen = in->hdr_len - in->dyn_hdr_len;
    u8 *pkt = calloc(1, H2C_HEADER_LEN + hlen);
    memcpy(pkt + H2C_HEADER_LEN, fw, hlen);
    u32 *hw = (u32 *)(pkt + H2C_HEADER_LEN);
    int v1 = in->hdr_ver == 1;
    hw[7] = (hw[7] & ~GENMASK(15, 0)) | in->part_size;                    // PART_SIZE
    u32 plen = hlen;
    if (v1) {                                                              // compact ignored sections
        struct fw_sec *s = (struct fw_sec *)(pkt + H2C_HEADER_LEN + 48);
        int d = 0;
        for (int i = 0; i < in->nsec; i++) { if (in->sec[i].ignore) continue; if (d != i) s[d] = s[i]; d++; }
        hw[6] = (hw[6] & ~GENMASK(15, 8)) | ((u32)d << 8);
        plen -= (in->nsec - d) * 16;
    }
    u32 h0 = FIELD_PREP(H2C_HDR_DEL_TYPE, FWCMD_TYPE_H2C) | FIELD_PREP(H2C_HDR_CAT, H2C_CAT_MAC) | FIELD_PREP(H2C_HDR_CLASS, H2C_CL_MAC_FWDL) |
                FIELD_PREP(H2C_HDR_FUNC, H2C_FUNC_MAC_FWHDR_DL) | FIELD_PREP(H2C_HDR_H2C_SEQ, h2c_seq);
    u32 h1 = FIELD_PREP(H2C_HDR_TOTAL_LEN, plen + H2C_HEADER_LEN);
    memcpy(pkt, &h0, 4); memcpy(pkt + 4, &h1, 4);
    if (h2c_tx(pkt, plen + H2C_HEADER_LEN, 0)) return -1;
    free(pkt);
    if (poll_ready(B_AX_FWDL_PATH_RDY, 500)) { fprintf(stderr, "FWDL path not ready (ctrl=0x%08x)\n", rd32(R_AX_WCPU_FW_CTRL)); return -1; }
    wr32(R_AX_HALT_H2C_CTRL, 0); wr32(R_AX_HALT_C2H_CTRL, 0);
    printf("header sent, path ready (status %d)\n", fwdl_status());
    // sections
    for (int i = 0; i < in->nsec; i++) {
        secinfo *s = &in->sec[i];
        if (s->ignore) continue;
        u32 left = s->len; const u8 *p = s->addr;
        while (left) {
            u32 n = left < in->part_size ? left : in->part_size;
            if (h2c_tx(p, n, 1)) { fprintf(stderr, "section %d failed at offset %td\n", i, p - s->addr); return -1; }
            p += n; left -= n;
        }
        printf("section %d (%u bytes) sent\n", i, s->len);
    }
    h2c_seq = 0;
    usleep(5000);
    for (int i = 0; i < 400; i++) {
        int st = fwdl_status();
        if (st == RTW89_FWDL_WCPU_FW_INIT_RDY) { printf("FIRMWARE READY (FWCTRL=0x%08x)\n", rd32(R_AX_WCPU_FW_CTRL)); return 0; }
        usleep(1000);
    }
    fprintf(stderr, "firmware not ready: status=%d FWCTRL=0x%08x\n", fwdl_status(), rd32(R_AX_WCPU_FW_CTRL));
    return -1;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) { fprintf(stderr, "usage: %s parse|run fw.bin\n", argv[0]); return 1; }
    int run = !strcmp(argv[1], "run");
    size_t flen; u8 *file = load(argv[2], &flen);
    u8 cv = 2;
    const u8 *fw; u32 fsz; int type = RTW89_FW_NORMAL_CE;
    if (mfw_pick(file, flen, type, cv, &fw, &fsz)) { type = RTW89_FW_NORMAL; if (mfw_pick(file, flen, type, cv, &fw, &fsz)) { fprintf(stderr, "no suitable fw\n"); return 2; } }
    printf("using fw type %d, size %u\n", type, fsz);
    fwinfo in = {0};
    if (parse_hdr(fw, fsz, &in, 0)) return 3;
    dump_info(&in);
    if (!run) return 0;
    if (rtw_open()) { fprintf(stderr, "cannot open 35bc:0108\n"); return 1; }
    if (power_switch_on()) { fprintf(stderr, "power on failed\n"); return 4; }
    // secure-boot check (efuse 0x5EC/0x5ED): 0xff,0xff = not secure
    wr16(R_AX_SYS_ISO_CTRL, rd16(R_AX_SYS_ISO_CTRL) | 0);
    if (open_iface()) return 5;
    if (pre_fwdl_init()) { fprintf(stderr, "pre-fwdl init failed\n"); return 7; }
    printf("pre-fwdl init ok\n");
    int r = download(fw, fsz, &in);
    printf("download: %s\n", r ? "FAILED" : "ok");
    rtw_close();
    return r ? 6 : 0;
}
