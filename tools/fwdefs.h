// Firmware container / header definitions (from rtw89 fw.h, core.h)
#pragma once
#include "rtw.h"
#define RTW89_MFW_SIG 0xFF
#define RTW89_FW_NORMAL 1
#define RTW89_FW_NORMAL_CE 5
struct mfw_info { u8 cv, type, mp, rsvd; u32 shift, size; u8 rsvd2[4]; } __attribute__((packed));
struct mfw_hdr { u8 sig, fw_nr, rsvd0[2]; u8 ver[4]; u8 rsvd1[8]; struct mfw_info info[]; } __attribute__((packed));
struct fw_sec { u32 w0, w1, w2, w3; } __attribute__((packed));
struct fw_hdr_v0 { u32 w0, w1, w2, w3, w4, w5, w6, w7; struct fw_sec sec[]; } __attribute__((packed));
struct fw_hdr_v1 { u32 w[12]; struct fw_sec sec[]; } __attribute__((packed));
#define FW_HDR_W3_LEN GENMASK(23, 16)
#define FW_HDR_W3_HDR_VER GENMASK(31, 24)
#define FW_HDR_W6_SEC_NUM GENMASK(15, 8)
#define FW_HDR_W7_PART_SIZE GENMASK(15, 0)
#define FW_HDR_W7_DYN_HDR BIT(16)
#define FW_HDR_W7_IDMEM_SHARE_MODE GENMASK(21, 18)
#define FWSEC_W1_SEC_SIZE GENMASK(23, 0)
#define FWSEC_W1_SECTIONTYPE GENMASK(27, 24)
#define FWSEC_W1_CHECKSUM BIT(28)
#define FWSEC_W1_REDL BIT(29)
#define FW_HDR_V1_W5_HDR_SIZE GENMASK(31, 16)
#define FW_HDR_V1_W6_SEC_NUM GENMASK(15, 8)
#define FW_HDR_V1_W6_DSP_CHKSUM BIT(24)
#define FW_HDR_V1_W7_PART_SIZE GENMASK(15, 0)
#define FW_HDR_V1_W7_DYN_HDR BIT(16)
#define FW_HDR_V1_W7_IDMEM_SHARE_MODE GENMASK(21, 18)
#define FWDL_SECTION_CHKSUM_LEN 8
#define FWDL_SECTION_PER_PKT_LEN 2020
#define FWDL_SECURITY_SECTION_TYPE 9
#define FWDL_SECURITY_SIGLEN 512
#define FWDL_SECURITY_CHKSUM_LEN 8
#define FORMATTED_MSSC 0xFF
#define FWSEC_V1_W2_MSSC GENMASK(7, 0)
// H2C header
#define H2C_HEADER_LEN 8
#define H2C_HDR_CAT GENMASK(1, 0)
#define H2C_HDR_CLASS GENMASK(7, 2)
#define H2C_HDR_FUNC GENMASK(15, 8)
#define H2C_HDR_DEL_TYPE GENMASK(19, 16)
#define H2C_HDR_H2C_SEQ GENMASK(31, 24)
#define H2C_HDR_TOTAL_LEN GENMASK(13, 0)
#define H2C_CAT_MAC 1
#define H2C_CL_MAC_FWDL 3
#define H2C_FUNC_MAC_FWHDR_DL 0
#define FWCMD_TYPE_H2C 0
// TX descriptor body (8852B: struct rtw89_txwd_body = 6 dwords = 24 bytes)
#define TXWD_SIZE 24
#define RTW89_TXWD_BODY0_FW_DL BIT(20)
#define RTW89_TXWD_BODY0_CHANNEL_DMA GENMASK(19, 16)
#define RTW89_TXWD_BODY2_TXPKT_SIZE GENMASK(13, 0)
#define RTW89_DMA_H2C 12
// FWDL status
#define RTW89_FWDL_INITIAL_STATE 0
#define RTW89_FWDL_WCPU_FW_INIT_RDY 7
