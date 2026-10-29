#!/bin/sh
# Builds the emulator: native test runner (x64run) and the wasm64 module used by Nebula OS.
set -e
cd "$(dirname "$0")"
ZIG=${ZIG:-zig}
SRC="lib.c cpu.c pe.c win32.c gdi.c vfs.c api.c"
echo "[1/2] native x64run"
cc -O2 -Wall -Wno-unused-function -Wno-unused-parameter -Wno-misleading-indentation -o x64run $SRC host_native.c main_native.c -lm
echo "[2/2] wasm64 module"
mkdir -p ../../public/x64
$ZIG cc -target wasm64-freestanding -O2 -fno-builtin-memcpy -fno-builtin-memset \
  -Wl,--no-entry -Wl,--strip-all -Wl,--export-memory -Wl,-z,stack-size=1048576 -Wl,--initial-memory=67108864 -Wl,--max-memory=4294901760 \
  -Wl,--export=__heap_base \
  $SRC host_wasm.c -o ../../public/x64/nebula_x64.wasm
ls -la x64run ../../public/x64/nebula_x64.wasm
