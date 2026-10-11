// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// ACPI SAR/DSM tables do not exist on macOS: report "not available".
#include "core.h"
#include "acpi.h"
enum rtw89_acpi_sar_subband rtw89_acpi_sar_get_subband(struct rtw89_dev *rtwdev, u32 center_freq) { return 0; }
enum rtw89_band rtw89_acpi_sar_subband_to_band(struct rtw89_dev *rtwdev, enum rtw89_acpi_sar_subband subband) { return RTW89_BAND_2G; }
int rtw89_acpi_evaluate_dsm(struct rtw89_dev *rtwdev, enum rtw89_acpi_dsm_func func, struct rtw89_acpi_dsm_result *res) { return -ENOENT; }
int rtw89_acpi_evaluate_rtag(struct rtw89_dev *rtwdev, struct rtw89_acpi_rtag_result *res) { return -ENOENT; }
int rtw89_acpi_evaluate_sar(struct rtw89_dev *rtwdev, struct rtw89_sar_cfg_acpi *cfg) { return -ENOENT; }
int rtw89_acpi_evaluate_dynamic_sar_indicator(struct rtw89_dev *rtwdev, struct rtw89_sar_cfg_acpi *cfg, bool *poll_changed) { return -ENOENT; }
