#!/bin/sh
# usage: chk.sh file.c [N]   -- syntax-check with the compat layer
cd "$(dirname "$0")"
clang -fsyntax-only -w -ferror-limit=${2:-25} -DKBUILD_MODNAME='"rtw89"' -I compat -I compat/uapi -I ../rtw89/src "$1" 2>&1 | grep -E "error|fatal" | head -${2:-25}
