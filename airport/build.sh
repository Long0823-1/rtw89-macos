#!/bin/sh
# Builds RTW89Airport.kext (x86_64). Needs Xcode (Kernel.framework headers). The Apple private-class headers in include/Airport come from itlwm (GPL-2.0).
set -e
cd "$(dirname "$0")"
SDK=$(xcrun --show-sdk-path)
K=$SDK/System/Library/Frameworks/Kernel.framework/Headers
mkdir -p RTW89Airport.kext/Contents/MacOS build
xcrun clang++ -arch x86_64 -mmacosx-version-min=12.0 -std=c++17 -mkernel -fapple-kext -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector -nostdlib \
  -D__IO80211_TARGET=120000 -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
  -Wno-everything -isysroot "$SDK" -include src/prefix.h -Iinclude -Iinclude/Airport -I"$K" -I"$K/libkern/c++" \
  -c src/RTW89Airport.cpp -o build/RTW89Airport.o
cat > build/kmod.c <<'C'
#include <mach/kmod.h>
extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(local.rtw89.airport, "0.1.0", _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
C
xcrun clang -arch x86_64 -mmacosx-version-min=12.0 -mkernel -fno-builtin -nostdlib -DKERNEL -isysroot "$SDK" -I"$K" -c build/kmod.c -o build/kmod.o
xcrun clang++ -arch x86_64 -mmacosx-version-min=12.0 -nostdlib -Xlinker -kext -Xlinker -no_data_const -isysroot "$SDK" \
  build/RTW89Airport.o build/kmod.o -L"$SDK/usr/lib" -lkmod -lkmodc++ -o RTW89Airport.kext/Contents/MacOS/RTW89Airport
codesign -s - --force RTW89Airport.kext 2>/dev/null || true
echo built
