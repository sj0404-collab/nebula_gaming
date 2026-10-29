#!/bin/sh
# Native capability tests for the Nebula x64 emulator (run by CI; mirrors what the browser build does).
set -e
cd "$(dirname "$0")/.."
X=./x64/x64run; S=../public/x64/samples; fail=0
check() { # name, expected-substring, output
  if printf '%s' "$3" | grep -q -- "$2"; then echo "  ok   $1"; else echo "  FAIL $1: expected '$2' in:"; printf '%s\n' "$3" | head -20; fail=1; fi
}
echo "[1] hello.exe (UCRT console)"
rc=0; out=$($X --quiet tests/hello.exe a b 2>&1) || rc=$?
check "greeting"      "Hello from x86-64 Windows! argc=3" "$out"
check "64-bit fnv"    "fnv=76a25757d9d78283" "$out"
check "printf/math"   "sqrt(2)=1.414214 sin(1)=0.8415 pow(2,10)=1024" "$out"
[ "$rc" = 7 ] && echo "  ok   exit code 7" || { echo "  FAIL exit code: $rc"; fail=1; }
echo "[2] bench.exe (CPU)"
out=$($X --quiet --stats tests/bench.exe 2>&1 || true)
check "sieve"         "sieve(2M): 148933 primes" "$out"
check "mandelbrot"    "mandelbrot(200x100x200): 5108 inside" "$out"
echo "[3] busybox64.exe (real third-party mingw64 binary)"
check "echo"    "hello from busybox"          "$($X --quiet $S/busybox64.exe echo hello from busybox 2>&1 || true)"
check "awk"     "42"                          "$($X --quiet $S/busybox64.exe awk 'BEGIN{print(6*7)}' 2>&1 || true)"
check "sha256"  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" "$($X --quiet $S/busybox64.exe sha256sum </dev/null 2>&1 || true)"
check "date"    "2025-10-29T15:00:00"         "$($X --quiet $S/busybox64.exe date -u -d @1761750000 +%Y-%m-%dT%H:%M:%S 2>&1 || true)"
check "factor"  "1234567891: 1234567891"      "$($X --quiet $S/busybox64.exe factor 1234567891 2>&1 || true)"
check "uname"   "Windows_NT nebula"           "$($X --quiet $S/busybox64.exe uname -a 2>&1 || true)"
echo "[3b] virtual file system + stdin (busybox on a mounted host folder)"
M=$(mktemp -d); mkdir -p "$M/sub"; printf 'pear\napple\nmango\n' > "$M/words.txt"; printf 'x' > "$M/sub/b.bin"
check "ls"       "words.txt"                   "$($X --quiet --mount "$M" $S/busybox64.exe ls </dev/null 2>&1 || true)"
check "sort file" "apple
mango
pear"                                            "$($X --quiet --mount "$M" $S/busybox64.exe sort words.txt </dev/null 2>&1 || true)"
check "stdin"    "cherry
banana
apple"                                           "$(printf 'banana\ncherry\napple\n' | $X --quiet $S/busybox64.exe sort -r 2>&1 || true)"
check "wc"       "3         3        17 words.txt" "$($X --quiet --mount "$M" $S/busybox64.exe wc words.txt </dev/null 2>&1 || true)"
check "find"     "./sub/b.bin"                  "$($X --quiet --mount "$M" $S/busybox64.exe find . </dev/null 2>&1 || true)"
check "sed"      "APPLE"                        "$($X --quiet --mount "$M" $S/busybox64.exe sed 's/apple/APPLE/' words.txt </dev/null 2>&1 || true)"
check "diff"     "+x"                           "$($X --quiet --mount "$M" $S/busybox64.exe diff words.txt sub/b.bin </dev/null 2>&1 || true)"
check "sh vars"  "n1
n2
n3
10"                                              "$($X --quiet $S/busybox64.exe sh -c 'for i in 1 2 3; do echo n$i; done; X=5; echo $((X*2))' </dev/null 2>&1 || true)"
check "sh read"  "got:typed line"               "$(echo 'typed line' | $X --quiet $S/busybox64.exe sh -c 'read x; echo got:$x' 2>&1 || true)"
check "env"      "TEMP=C:.tmp"                 "$($X --quiet $S/busybox64.exe env </dev/null 2>&1 || true)"
rm -rf "$M"
echo "[4] plink.exe (real MSVC/clang-cl PuTTY 0.85 binary)"
check "plink -V" "plink: Release 0.85"        "$($X --quiet $S/plink.exe -V 2>&1 || true)"
echo "[5] NebulaStrike.exe (Win32 GDI game) — 2 s headless, keys"
out=$($X --max-ms 2000 --key 0x25@300:400 --key 0x20@500:1200 ../public/downloads/NebulaStrike.exe 2>&1 || true)
frames=$(printf '%s' "$out" | sed -n 's/.* = \([0-9.]*\) MIPS, \([0-9]*\) frames.*/\2/p')
echo "  frames presented: ${frames:-0}"
[ "${frames:-0}" -ge 60 ] && echo "  ok   >= 60 frames in 2 s" || { echo "  FAIL frames: $out"; fail=1; }
echo "[6] honest rejections"
check "32-bit PE"   '"verdict":"unsupported"' "$(printf 'MZ' | head -c 2 > /tmp/pe32.bin; python3 - <<'PY'
import struct
b=bytearray(0x200); b[0:2]=b'MZ'; struct.pack_into('<I',b,0x3c,0x80); b[0x80:0x84]=b'PE\0\0'; struct.pack_into('<H',b,0x84,0x14c); struct.pack_into('<H',b,0x94,0xe0); struct.pack_into('<H',b,0x98,0x10b)
open('/tmp/pe32.exe','wb').write(b)
PY
$X --inspect /tmp/pe32.exe 2>&1)"
check "not a PE"    '"ok":false' "$($X --inspect tests/run.sh 2>&1 || true)"
[ $fail = 0 ] && echo "ALL OK" || { echo "FAILURES"; exit 1; }
