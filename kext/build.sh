#!/bin/sh
# Builds RTW89Hold.kext (x86_64) from src/RTW89Hold.cpp and the Info.plist in the bundle.
set -e
cd "$(dirname "$0")"
SDK=$(xcrun --show-sdk-path)
K=$SDK/System/Library/Frameworks/Kernel.framework/Headers
mkdir -p RTW89Hold.kext/Contents/MacOS
xcrun clang++ -arch x86_64 -mmacosx-version-min=11.0 -std=c++17 -mkernel -fapple-kext -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector -nostdlib \
  -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT -DKMOD_NAME=RTW89Hold \
  -isysroot "$SDK" -I"$K" -I"$K/libkern/c++" -c src/RTW89Hold.cpp -o /tmp/RTW89Hold.o
xcrun clang++ -arch x86_64 -mmacosx-version-min=11.0 -nostdlib -Xlinker -kext -Xlinker -no_data_const -isysroot "$SDK" \
  /tmp/RTW89Hold.o -L"$SDK/usr/lib" -lkmod -lkmodc++ -o RTW89Hold.kext/Contents/MacOS/RTW89Hold
codesign -s - --force RTW89Hold.kext 2>/dev/null || true
