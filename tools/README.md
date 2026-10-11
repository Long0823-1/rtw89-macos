# Early prototypes (kept for reference)

Single-file IOKit programs written while bringing the RTL8852B up, before the upstream rtw89 driver was ported:
`modeswitch*.c` (0bda:1a2b virtual CD -> Wi-Fi mode), `regdump.c`, `poweron.c`, `efuse.c`, `fwdl.c` (firmware download).
They are superseded by `port/` and are not built by it.  Build one with e.g.
`clang -o regdump regdump.c rtw.c -framework IOKit -framework CoreFoundation`.
