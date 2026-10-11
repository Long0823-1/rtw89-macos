# Build rtw89 (upstream, unmodified) against the macOS compat layer.
SRC := ../rtw89/src
CFLAGS := -MMD -MP -O2 -g -w -DCONFIG_RTW89_DEBUGMSG -fno-strict-aliasing -fms-extensions -DKBUILD_MODNAME='"rtw89"' -Icompat -Icompat/uapi -I$(SRC) -Ihost
RTW := core fw mac phy chan efuse cam ps sar regd ser util rtw8852b rtw8852b_common rtw8852b_rfk rtw8852b_rfk_table rtw8852b_table rtw8852bu usb coex mac80211
HOST := ksupport debug_macos mm_core mlme datapath supplicant sae acpi_stub daemon airport_bridge main
SYSHOST := usb_shim crypto net net6 ecp256
OBJ := $(addprefix build/rtw_,$(addsuffix .o,$(RTW))) $(addprefix build/host_,$(addsuffix .o,$(HOST))) $(addprefix build/sys_,$(addsuffix .o,$(SYSHOST)))

all: rtw89d
rtw89d: $(OBJ)
	clang -o $@ $(OBJ) -lpthread -framework CoreFoundation -framework IOKit
build/rtw_regd.o: $(SRC)/regd.c patches/regd_null.sed
	sed -f patches/regd_null.sed $(SRC)/regd.c > build/regd_patched.c
	clang $(CFLAGS) -I$(SRC) -c build/regd_patched.c -o $@
build/rtw_%.o: $(SRC)/%.c
	clang $(CFLAGS) -c $< -o $@
build/host_%.o: host/%.c
	clang $(CFLAGS) -c $< -o $@
build/sys_%.o: host/%.c
	clang $(CFLAGS) -c $< -o $@
clean:
	rm -f build/*.o rtw89d

-include $(wildcard build/*.d)
