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
echo "[1b] pebstd.exe (PEB std handles, MEM_DECOMMIT semantics)"
out=$($X --quiet tests/pebstd.exe 2>&1 || true)
check "peb handles"   "in=0000000000000010 out=0000000000000011 err=0000000000000012 flags=1" "$out"
check "peb WriteFile" "WriteFile(peb hStdOutput) -> 1, 87 bytes" "$out"
check "decommit"      "decommit -> 1, recommit same, nonzero after recommit: 0, untouched byte: ab" "$out"
echo "[1c] waitin.exe (console input handle is waitable)"
out=$( (sleep 1; printf 'hi') | $X --quiet tests/waitin.exe 2>&1 || true)
check "wait timeout"  "wait(50ms) before input -> 258" "$out"
check "wait signaled" "wait(5s) -> 0 (0=signaled)" "$out"
check "key records"   "ReadConsoleInput: 2 records 'h' 'i'" "$out"
echo "[1d] regwait.exe (RegisterWaitForSingleObject on console input)"
out=$( (sleep 1; printf 'hi') | $X --quiet tests/regwait.exe 2>&1 || true)
check "callback"      "callback ctx=0000000000001234 timedout=0" "$out"
check "event"         "event wait -> 0" "$out"
check "records"       "records=2" "$out"
echo "[1e] keys.exe (UTF-8 stdin -> UTF-16 key records)"
out=$(printf 'a\320\237\321\200\360\237\230\200z\n' | $X --quiet tests/keys.exe 2>&1 || true)
check "utf16 units"   "0061 041f 0440 d83d de00 007a 000d" "$out"
check "count"         "total=7" "$out"
echo "[1f] d3d11clear.exe (Direct3D 11 device + swap chain + clear + Present)"
rm -f /tmp/d3d_*.ppm; out=$(cd /tmp && X64_LOG=1 $OLDPWD/$X --quiet --no-stdin --frames 1 --max-ms 10000 $OLDPWD/tests/d3d11clear.exe 2>&1 || true)
check "device"        "device ok, feature level 0xb000" "$out"
check "backbuffer"    "backbuffer 640x480 format 87" "$out"
check "three frames"  "frame 2 presented (0.1 0.8 0.3)" "$out"
check "ok"            "d3d11clear ok" "$out"
check "frame pixels"  "frame_0001:(51, 102, 230) frame_0002:(230, 77, 26) frame_0003:(26, 204, 77)" "$(python3 - <<'PY'
import glob
def px(f):
    d=open(f,'rb').read(); parts=d.split(b'\n',3); w,h=map(int,parts[1].split()); o=parts[3]; i=(200*w+300)*3; return tuple(o[i:i+3])
print(' '.join(f.split('/')[-1][:-4]+':'+str(px(f)) for f in sorted(glob.glob('/tmp/frame_000[123].ppm'))))
PY
)"
rm -f /tmp/frame_*.ppm
echo "[1f2] d3d11tri.exe (DXBC → GLSL translation; the native runner has no GPU, so Draw must stop honestly)"
out=$(X64_LOG=1 $X --no-stdin --max-ms 10000 tests/d3d11tri.exe 2>&1 || true)
check "vs translated"  "vertex shader .*: 1432 bytes of DXBC → .* bytes of GLSL ES 3.00" "$out"
check "ps translated"  "pixel shader .*: 576 bytes of DXBC → .* bytes of GLSL ES 3.00" "$out"
check "honest draw"    "D3D11 Draw: this host has no GPU back end" "$out"
echo "[1g] uvpipe.exe (libuv-style child stdout: named pipe + IOCP zero-read + CreateProcess)"
out=$($X --quiet --no-stdin --max-ms 20000 tests/uvpipe.exe 2>&1 || true)
check "zero-read ready" "GQCS -> 1 bytes=0 key=42" "$out"
check "child output"    "data: \[hi from child" "$out"
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
echo "[3c] processes + pipes (busybox sh spawns real child processes inside the emulator)"
BB="$X --quiet --no-stdin --max-ms 20000 $S/busybox64.exe"
check "pipe"        "HELLO"                      "$($BB sh -c 'echo hello | tr a-z A-Z' 2>&1 || true)"
check "pipeline x3" "5
4"                        "$($BB sh -c 'seq 1 5 | sort -rn | head -2' 2>&1 || true)"
check "subst"       "got:sub"                    "$($BB sh -c 'x=$(echo sub); echo got:$x' 2>&1 || true)"
check "nested"      "deep"                       "$($BB sh -c 'echo $(echo $(echo deep))' 2>&1 || true)"
check "exit codes"  "1 7 3"                      "$($BB sh -c 'false; a=$?; (exit 7); b=$?; sh -c "exit 3"; echo $a $b $?' 2>&1 || true)"
check "bg + ps"     "sleep 1"                    "$($BB sh -c 'sleep 1 & ps; wait' 2>&1 || true)"
check "kill"        "rc=137"                     "$($BB sh -c 'sleep 5 & kill -9 $!; wait $!; echo rc=$?' 2>&1 || true)"
check "yes|head"    "y
y"                        "$($BB sh -c 'yes | head -2' 2>&1 || true)"
check "1 MB pipe"   "1000000"                    "$($BB sh -c 'head -c 1000000 /dev/zero | wc -c' 2>/dev/null || true)"
check "while read"  "<x><y>"                     "$($BB sh -c 'printf "x\ny\n" | while read l; do printf "<%s>" "$l"; done; echo' 2>&1 || true)"
check "xargs sh"    "item 2"                     "$($BB sh -c 'printf "1\n2\n" | xargs -I{} sh -c "echo item {}"' 2>&1 || true)"
check "env child"   "FOO=baz"                    "$($BB sh -c 'FOO=baz sh -c "echo FOO=\$FOO"' 2>&1 || true)"
check "stdin→child" "child:line1
parent:line2"  "$(printf 'line1\nline2\n' | $X --quiet --max-ms 20000 $S/busybox64.exe sh -c 'sh -c "read a; echo child:\$a"; read b; echo parent:$b' 2>&1 || true)"
check "gzip pipe"   "payload"                    "$($BB sh -c 'echo payload | gzip -c | gunzip -c' 2>&1 || true)"
echo "[4] plink.exe (real MSVC/clang-cl PuTTY 0.85 binary)"
check "plink -V" "plink: Release 0.85"        "$($X --quiet $S/plink.exe -V 2>&1 || true)"
echo "[5] NebulaStrike.exe (Win32 GDI game) — 2 s headless, keys"
out=$($X --max-ms 2000 --key 0x25@300:400 --key 0x20@500:1200 ../public/downloads/NebulaStrike.exe 2>&1 || true)
frames=$(printf '%s' "$out" | sed -n 's/.* = \([0-9.]*\) MIPS, \([0-9]*\) frames.*/\2/p')
echo "  frames presented: ${frames:-0}"
[ "${frames:-0}" -ge 60 ] && echo "  ok   >= 60 frames in 2 s" || { echo "  FAIL frames: $out"; fail=1; }
echo "[7] threads & synchronisation (real Win32 threads scheduled by the emulator)"
check "threads.exe"   "39 checks, 0 failed"          "$($X --quiet --max-ms 30000 tests/threads.exe 2>&1 | tail -1 || true)"
check "pthreads.exe"  "9 checks, 0 failed"           "$($X --quiet --max-ms 30000 tests/pthreads.exe 2>&1 | tail -1 || true)"
check "threads in child + pipe" "39 checks, 0 failed" "$($X --quiet --no-stdin --max-ms 30000 --mount "$PWD/tests" $S/busybox64.exe sh -c './threads.exe | tail -1' 2>&1 || true)"
echo "[7b] Winsock loopback: blocking send/recv of 12 MB between threads, overlapped WSASend + WSAGetOverlappedResult, shutdown/EOF, select, FIONREAD"
check "netloop.exe"   "netloop: 23 checks, 0 failed"  "$($X --quiet --no-stdin --max-ms 60000 tests/netloop.exe 2>&1 | tail -1 || true)"
echo "[8] heap: 10^5 live blocks, blocks spanning heap regions, sort of 100000 lines"
check "sort -n 20000"  "20000"                       "$($BB sh -c 'seq 1 20000 | sort -n | tail -1' 2>&1 || true)"
check "sort -rn 1e5"   "100000"                      "$($BB sh -c 'seq 1 100000 | sort -rn | head -1' 2>&1 || true)"
echo "[9] private DLLs: static imports, LoadLibrary, GetProcAddress, DllMain, per-thread TLS"
check "usedll.exe"   "static import: add3=6 name=mydll says hi
LoadLibrary=0000000180000000 same=1
GetProcAddress dll_hyp -> ok, f(3,4)=25.0
missing=0000000000000000 err=127
path=C:\\nebula\\mydll.dll
kernel32 GetTickCount=ok" "$($X --quiet --mount "$PWD/tests/dll" tests/dll/usedll.exe 2>&1 || true)"
check "DLL thread TLS" "thread 10: tls=57
thread 100: tls=507
main after threads: 9 (expect 9)" "$($X --quiet --mount "$PWD/tests/dll" tests/dll/tlsuse.exe 2>&1 | tail -3 || true)"
echo "[10] real applications (public/x64/apps: SQLite 3.50.4, jq 1.8.1, Lua 5.4.2 + DLL, 7-Zip 25.01 + DLL, zstd 1.5.7)"
A=../public/x64/apps
check "sqlite3 :memory:" "3|3.50.4"      "$($X --quiet --max-ms 30000 $A/sqlite3.exe :memory: 'select 1+2, sqlite_version();' 2>&1 || true)"
check "sqlite3 file db" "2000|ok|"       "$($X --quiet --no-stdin --max-ms 120000 --mount "$PWD/$A" $S/busybox64.exe sh -c './sqlite3.exe -init demo.sql demo.db .quit >/dev/null 2>&1; echo "select count(*) from t; pragma integrity_check;" | ./sqlite3.exe demo.db | tr -d "\\r" | tr "\\n" "|"' 2>&1 || true)"
check "jq"              '{"name":"cid","n":3}' "$($X --quiet --max-ms 30000 --mount "$PWD/$A" $A/jq.exe -M -c '.users[] | select(.age > 40) | {name, n: (.langs|length)}' demo.json 2>&1 || true)"
check "lua54 + lua54.dll" "fib(25) =	75025" "$($X --quiet --max-ms 60000 --mount "$PWD/$A" $A/lua54.exe demo.lua 2>&1 | head -1 | cut -f3- || true)"
check "7z.exe + 7z.dll round trip" "7Z_OK" "$($X --quiet --no-stdin --max-ms 120000 --mount "$PWD/$A" $S/busybox64.exe sh -c './7z.exe a -bb0 -mx1 d.7z demo.txt >/dev/null && ./7z.exe x -y -oout d.7z >/dev/null && cmp demo.txt out/demo.txt && echo 7Z_OK' 2>&1 || true)"
check "zstd round trip" "ZSTD_OK"        "$($X --quiet --no-stdin --max-ms 120000 --mount "$PWD/$A" $S/busybox64.exe sh -c './zstd.exe -q -f demo.txt -o d.zst && ./zstd.exe -q -d -f d.zst -o back.txt && cmp demo.txt back.txt && echo ZSTD_OK' 2>&1 || true)"
echo "[11] x87 FPU subset (long double printf / fsin / fyl2x)"
check "x87.exe" "ld: 1.9090909091 1909
sqrtl: 1.414213562
fistp: 1909090
fmodl: 1.5000 sinl: 0.841471
strtold: 2.718282" "$($X --quiet tests/x87.exe 2>&1 | grep -v 'via mingw\|cmp:' || true)"
echo "[12] structured exception handling: x64 SEH tables, libunwind C++ (zig c++), MSVC __CxxFrameHandler (7-Zip error paths)"
check "C++ throw/catch (gcc ABI)" "caught std::exception: value too large: 7" "$($X --quiet --max-ms 30000 tests/cxxexc.exe 2>&1 || true)"
check "1000 throws + dtors"       "1000 throws in a loop, total=503"        "$($X --quiet --max-ms 30000 tests/cxxexc.exe 2>&1 || true)"
check "__try/__finally unwinding" "inner __finally"                         "$($X --quiet --max-ms 30000 tests/sehtest.exe 2>&1 || true)"
check "nested __except"           "nested except: 1"                        "$($X --quiet --max-ms 30000 tests/sehtest.exe 2>&1 || true)"
check "7z: missing archive (MSVC C++ throw)" "System ERROR:" "$($X --quiet --max-ms 30000 --mount "$PWD/$A" $A/7z.exe x nonexistent.7z 2>&1 || true)"
check "7z: unsupported type"      "Unsupported archive type"               "$($X --quiet --max-ms 30000 --mount "$PWD/$A" $A/7z.exe a -tbogus x.7z demo.txt 2>&1 || true)"
echo "[13] hardware exceptions as SEH (AV / divide by zero / ud2 -> __except, vectored handlers, continue execution, top-level filter)"
hw="$($X --quiet --max-ms 30000 tests/hwexc.exe 2>&1 || true)"
check "AV caught with record"  "AV caught=1 addr_ok=3"            "$hw"
check "div0 + ud2 caught"       "div0 caught=2
ud2 caught=3"      "$hw"
check "finally during AV unwind" "nested AV caught=4 finally=1"    "$hw"
check "VEH fix-up + continue"   "veh calls=1 code=0xc0000094 result=42" "$hw"
check "2000 faults in a loop"   "loop caught=2000"                 "$hw"
check "SetUnhandledExceptionFilter" "top-level filter: code 0xc0000005" "$hw"
echo "[14] Rust programs (ripgrep 14.1.1, fd 10.2.0 — MSVC target, static CRT, Rust std on Win32)"
check "rg -c across the tree" "demo.txt:1922"            "$($X --quiet --max-ms 60000 --mount "$PWD/$A" $A/rg.exe --color never -c -w the 2>&1 || true)"
check "rg -n regex"           "fib(25)"                "$($X --quiet --max-ms 60000 --mount "$PWD/$A" $A/rg.exe -n -i --color never "lua|sqlite" demo.lua demo.sql demo.json 2>&1 || true)"
check "rg -j4 --stats"        "files searched"         "$($X --quiet --max-ms 60000 --mount "$PWD/$A" $A/rg.exe -j4 --stats -c the 2>&1 || true)"
check "fd by extension"       "demo.json
demo.lua
demo.sql" "$($X --quiet --max-ms 60000 --mount "$PWD/$A" $A/fd.exe --color never -e lua -e sql -e json 2>&1 || true)"
echo "[15] CPython 3.12.10 (official python.org embeddable build: python.exe + python312.dll + stdlib zip + .pyd extensions)"
PY=$A/py; PYR="$X --quiet --max-ms 120000 --mount $PWD/$PY"
check "print(6*7)"            "42"                                   "$($PYR $PY/python.exe -c "print(6*7)" 2>&1 || true)"
check "version + platform"    "3.12.10 win32 AMD64"                  "$($PYR $PY/python.exe -c "import sys, platform; print(sys.version.split()[0], sys.platform, platform.machine())" 2>&1 || true)"
check "zipimport stdlib"      '"b": "x"}'              "$($PYR $PY/python.exe -c "import json, re, collections; print(json.dumps({'b': 'x', 'a': [1, 2]}, sort_keys=True))" 2>&1 || true)"
check "sqlite3 .pyd + dll"    "(45,) 3.49.1"                         "$($PYR $PY/python.exe -c "import sqlite3; c = sqlite3.connect(':memory:'); c.execute('create table t(x)'); c.executemany('insert into t values (?)', [(i,) for i in range(10)]); print(c.execute('select sum(x) from t').fetchone(), sqlite3.sqlite_version)" 2>&1 || true)"
check "decimal/hashlib/math"  "3.3 ba7816bf -0.6931471805599453 1.4142135623730951" "$($PYR $PY/python.exe -c "import decimal, hashlib, math; print(decimal.Decimal('1.1') + decimal.Decimal('2.2'), hashlib.sha256(b'abc').hexdigest()[:8], math.log(0.5), math.pow(2, 0.5))" 2>&1 || true)"
check "threads"               "[49995000, 199990000, 449985000]"     "$($PYR $PY/python.exe -c "import threading, queue; q = queue.Queue(); ts = [threading.Thread(target=lambda n: q.put(sum(range(n))), args=(10000 * k,)) for k in (1, 2, 3)]; [t.start() for t in ts]; [t.join() for t in ts]; print(sorted(q.get() for _ in ts))" 2>&1 || true)"
check "ctypes + kernel32"     "4242 8"                               "$($PYR $PY/python.exe -c "import ctypes; print(ctypes.windll.kernel32.GetCurrentProcessId(), ctypes.sizeof(ctypes.c_void_p))" 2>&1 || true)"
check "file io + traceback"   "ZeroDivisionError: division by zero"  "$($PYR $PY/python.exe -c "open('o.txt', 'w').write('hi'); print(open('o.txt').read()); 1/0" 2>&1 || true)"
rc=0; $PYR $PY/python.exe -c "import sys; sys.exit(3)" >/dev/null 2>&1 || rc=$?
check "exit code"             "rc=3"                                 "rc=$rc"
check "subprocess pipes"      "returncode=3, stdout='42" "$($PYR $PY/python.exe -c "import subprocess, sys; print(subprocess.run(['python.exe', '-c', 'import sys; print(42); print(\"err\", file=sys.stderr); sys.exit(3)'], capture_output=True, text=True))" 2>&1 || true)"
check "REPL over stdin"       "55 привет"                            "$(printf 'x = [n * n for n in range(6)]\nprint(sum(x), %s)\n' "'привет'" | $PYR $PY/python.exe -i -q 2>&1 || true)"
check "demo.py"               "sqlite: (3, 84.92, 'carol')"          "$(cp $A/demo.py $PY/demo.py; $PYR $PY/python.exe demo.py 2>&1 || true; rm -f $PY/demo.py)"
echo "[16] cmd.exe (emu/cmd/cmd.c, embedded at C:\\Windows\\System32: shell for system()/_popen/subprocess(shell=True), batch files, pipelines)"
CMD="$X --quiet --max-ms 60000 --mount $PWD/$A $A/cmd.exe"
check "echo + variables"      "hello
Windows_NT C:\\nebula"                 "$($CMD /c "echo hello & echo %OS% %CD%" 2>&1 | tr -d '\r' || true)"
check "pipeline of builtins"  "demo.sql
demo.lua"                                "$($CMD /c "dir /b *.lua *.sql | sort /r" 2>&1 | tr -d '\r' || true)"
check "redirection + find /c" "2"                                    "$($CMD /c "echo one line> t_out.txt & echo two line>> t_out.txt & type t_out.txt | find /c line & del t_out.txt" 2>&1 | tr -d '\r' || true)"
printf '@echo off\r\nset /a R=6*7 >nul\r\nif %%R%% EQU 42 (set W=yes) else (set W=no)\r\necho %%R%% %%W%%\r\nfor /l %%%%i in (1,1,3) do @echo %%%%i\r\n' > $A/t_arith.bat
check "set /a, if, for /l"    "42 yes 1 2 3 "                        "$($CMD /c t_arith.bat 2>&1 | tr -d '\r' | tr '\n' ' ' || true)"
rm -f $A/t_arith.bat
check "external programs"     "ok3"                                    "$($CMD /c "jq.exe -c -M .users[1].name demo.json | findstr bob && echo ok3" 2>&1 | tr -d '\r' || true)"
rc=0; $CMD /c "exit 7" >/dev/null 2>&1 || rc=$?
check "exit code"             "rc=7"                                 "rc=$rc"
check "unknown command"       "'nosuch' is not recognized as an internal or external command"  "$($CMD /c "nosuch --flag" 2>&1 || true)"
check "batch file"            "count=3
lua present
at least three files"                    "$($CMD /c demo.bat 2>&1 | tr -d '\r' || true)"
check "batch: call :sub / exit /b" "sub got hello world (from demo)
errorlevel after sub: 7"               "$($CMD /c demo.bat 2>&1 | tr -d '\r' || true)"
check "system()/_popen (C)"   "system rc=5
popen: [demo.lua]
popen: [done]
popen lines=2 rc=0
apple
mango
pear
popen-w rc=0"                            "$($X --quiet --max-ms 60000 --mount $PWD/$A tests/shelltest.exe 2>&1 | tr -d '\r' || true)"
check "python os.system/popen/shell=True" "rc 3
'python.exe'
0 'shell=Windows_NT
1024
PIPED'"                                  "$(cp $A/py/python.exe /tmp/_py_probe.exe 2>/dev/null; $PYR $PY/python.exe -c "import os, subprocess; print('rc', os.system('echo from-system & exit 3')); print(repr(os.popen('dir /b *.exe').read().strip())); r = subprocess.run('echo shell=%OS% && python.exe -c \"print(2**10)\" | findstr 1024 && python.exe -c \"import sys; print(sys.stdin.read().upper())\"', shell=True, input='piped', capture_output=True, text=True); print(r.returncode, repr(r.stdout.strip()))" 2>&1 | tr -d '\r' || true)"
printf '@echo off\r\ntype %%1\r\necho extra line with the word the\r\n' > $A/pre.cmd
check "rg --pre (Rust pipes: CreateNamedPipeW + ReadFileEx/APC)" "demo.lua
8:extra line with the word the"      "$($X --quiet --max-ms 60000 --mount $PWD/$A $A/rg.exe --color never --pre C:\\nebula\\pre.cmd -n "extra line" demo.lua 2>&1 | tr -d '\r' || true)"
rm -f $A/pre.cmd
check "fd -x cmd /c"          "found ./demo.lua"                     "$($X --quiet --max-ms 60000 --mount $PWD/$A $A/fd.exe --color never -e lua -x cmd /c echo found {} 2>&1 | tr -d '\r' || true)"
rm -f $A/out.txt
echo "[17] powershell.exe (emu/pwsh/pwsh.c, embedded at C:/Windows/System32/WindowsPowerShell/v1.0: Expand-Archive and the scripting subset installers use)"
PSX="$X --quiet --max-ms 120000 --mount $PWD/tests=C:\\nebula $A/cmd.exe /c"
check "found on PATH"           "WindowsPowerShell.v1.0.powershell.exe" "$($PSX "powershell -NoProfile -Command (Get-Command powershell.exe)" 2>&1 || true)"
# the exact spawn opencode uses to unpack ripgrep (cmd.exe /d /s /c "powershell.exe ^"-NoProfile^" … Expand-Archive …")
printf '@echo off\r\ncmd.exe /d /s /c "powershell.exe ^"-NoProfile^" ^"-NonInteractive^" ^"-Command^" ^"$global:ProgressPreference^ =^ ^'"'"'SilentlyContinue^'"'"'^;^ Expand-Archive^ -LiteralPath^ ^'"'"'C:\\nebula\\pwtest.zip^'"'"'^ -DestinationPath^ ^'"'"'C:\\tmp\\pw^'"'"'^ -Force^""\r\necho exit=%%ERRORLEVEL%%\r\npowershell -NoProfile -Command "(Get-ChildItem -Recurse -File -Name C:\\tmp\\pw) -join '"'"' '"'"'; (Get-Content C:\\tmp\\pw\\pkg\\data\\numbers.txt | Measure-Object).Count; Get-Content C:\\tmp\\pw\\pkg\\data\\numbers.txt | Select-Object -Last 1"\r\n' > tests/t_pw.bat
check "Expand-Archive exactly as opencode spawns it (deflate + stored, nested dirs)" "exit=0 pkg.README.txt pkg.bin.tool.cmd pkg.data.numbers.txt 100 10000" "$($PSX C:\\nebula\\t_pw.bat 2>&1 | tr -d '\r' | tr '\n' ' ' || true)"
printf '%s\r\n' "[System.IO.Compression.ZipFile]::ExtractToDirectory('C:\nebula\pwtest.zip', 'C:\tmp\pw2')" "& 'C:\tmp\pw2\pkg\bin\tool.cmd' 42" "(Get-Content 'C:\tmp\pw2\pkg\README.txt' | Measure-Object).Count" > tests/t_zip.ps1
check "ZipFile::ExtractToDirectory + external program from the archive" "tool-ran 42
50" "$($PSX "powershell -NoProfile -File C:\\nebula\\t_zip.ps1" 2>&1 | tr -d '\r' || true)"
printf '%s\r\n' "param()" "\$ErrorActionPreference = 'Stop'" "\$dir = Join-Path \$env:TEMP 'pstest'" "New-Item -ItemType Directory -Path \$dir -Force | Out-Null" "Set-Content -Path (Join-Path \$dir 'a.txt') -Value \"alpha\`nbeta\`ngamma\"" "\$lines = Get-Content (Join-Path \$dir 'a.txt') | Where-Object { \$_ -like '*a*' }" "Write-Output (\"matched {0}: {1}\" -f \$lines.Count, (\$lines -join ','))" "if ((Test-Path \$dir) -and (\$lines.Count -eq 3)) { 'cond ok' } else { 'cond bad' }" "try { Get-Content C:\\missing.txt } catch { 'caught' }" "foreach (\$n in 1,2,3) { \$sum += \$n }" "\"sum=\$sum\"" "\$h = (Get-FileHash (Join-Path \$dir 'a.txt')).Hash" "\$h.Length" "cmd /c echo via-cmd" "exit 3" > tests/t_ps.ps1
check "script: pipeline, -f, if/-and, try/catch, foreach, hash, external" "matched 3: alpha,beta,gamma
cond ok
caught
sum=6
64
via-cmd" "$($PSX "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\nebula\\t_ps.ps1" 2>&1 | tr -d '\r' || true)"
rc=0; $PSX "powershell -NoProfile -File C:\\nebula\\t_ps.ps1" >/dev/null 2>&1 || rc=$?
check "script exit code"        "rc=3" "rc=$rc"
check "unknown cmdlet"          "The term 'Get-Nonsense' is not recognized" "$($PSX "powershell -Command Get-Nonsense" 2>&1 || true)"
rm -f tests/t_ps.ps1 tests/t_zip.ps1 tests/t_pw.bat
echo "[5x] msvcp.exe (REAL msvcp140.dll from the VC++ redistributable — needs the runtime pack in public/x64/sys32)"
if [ -f ../public/x64/sys32/msvcp140.dll ]; then
  out=$($X --quiet --no-stdin --mount '../public/x64/sys32=C:\Windows\System32' tests/msvcp.exe 2>&1 || true)
  check "cout << int/double"  "42 3.25" "$out"
  check "cout put/flush"      "msvcp140 says hello" "$out"
  check "done"                "MSVCP OK" "$out"
  out=$($X --quiet --no-stdin tests/msvcp.exe 2>&1 || true)
  check "no pack → honest"    "exports missing" "$out"
else
  echo "  skip (run emu/tools/runtime-pack.sh first)"
fi
echo "[5y] tpool.exe (kernel32 thread pool: work items, periodic/one-shot timers, waits, simple callbacks, *WhenCallbackReturns)"
out=$($X --quiet --no-stdin tests/tpool.exe 2>&1 || true)
check "24 work callbacks"      "24 work callbacks ran, sum 48000" "$out"
check "periodic timer"         "periodic timer fired 4..8 times" "$out"
check "timer cancel"           "no ticks after cancel" "$out"
check "one-shot timer"         "one-shot timer fired exactly once" "$out"
check "wait object"            "wait callback ran with WAIT_OBJECT_0" "$out"
check "wait timeout"           "wait callback got WAIT_TIMEOUT" "$out"
check "simple callback"        "simple callback ran" "$out"
check "all"                    "threadpool 14/14" "$out"
if [ -f ../public/x64/sys32/msvcp140.dll ]; then
  echo "[5z] every runtime-pack DLL loads and runs its DllMain (LoadLibrary from C:\\Windows\\System32)"
  for d in $(ls ../public/x64/sys32/*.dll | xargs -n1 basename); do
    out=$($X --quiet --no-stdin --mount '../public/x64/sys32=C:\Windows\System32' tests/msvcp.exe "$d" 2>&1 || true)
    case "$out" in *"exports missing"*|*"MSVCP OK"*) echo "  ok   $d";; *) echo "  FAIL $d: $(printf '%s' "$out" | head -3)"; fail=1;; esac
  done
fi
DXP=${DX_PACK:-/var/tmp/dx/pack}
echo "[5dx] DirectX pack: REAL D3DCompiler_43 / d3dx9_43 / d3dx11_43 / X3DAudio1_7 from the June 2010 redistributable (DX_PACK=$DXP)"
if [ -f "$DXP/D3DCompiler_43.dll" ]; then
  out=$($X --quiet --no-stdin --max-ms 280000 --mount "$DXP=C:\\Windows\\System32" tests/d3dcompile.exe 2>&1 || true)
  check "real compiler found"     "shader compiler: D3DCompiler_43.dll" "$out"
  check "vs_4_0 compiled"         "vs_main vs_4_0: 1096 bytes, magic DXBC, valid DXBC container" "$out"
  check "ps_4_0 compiled"         "ps_main ps_4_0: 580 bytes, magic DXBC, valid DXBC container" "$out"
  check "disassembly"             "contains mul/mad arithmetic" "$out"
  check "DXBC accepted by D3D11"  "runtime-compiled shaders bound" "$out"
  out=$($X --quiet --no-stdin --max-ms 280000 --mount "$DXP=C:\\Windows\\System32" tests/dxpack.exe 2>&1 || true)
  check "d3dx9 math"              "P*inverse(P) deviates from identity by 0.0000" "$out"
  check "d3dx11 compile"          "HLSL compiled through D3DCompiler_43" "$out"
  check "X3DAudio panning"        "left 0.000 right 1.000" "$out"
  check "XInput honest"           "XInputGetState(0) = 1167" "$out"
  check "dxpack all"              "dxpack: 0 failure(s)" "$out"
  out=$($X --quiet --no-stdin tests/d3dcompile.exe 2>&1 || true)
  check "no pack → honest"        "install the DirectX pack" "$out"
  echo "  every pack DLL loads and runs its DllMain (LoadLibrary from C:\\Windows\\System32)"
  for d in $(ls "$DXP"/*.dll | xargs -n1 basename); do
    out=$($X --quiet --no-stdin --max-ms 60000 --mount "$DXP=C:\\Windows\\System32" tests/msvcp.exe "$d" 2>&1 || true)
    case "$out" in *"exports missing"*|*"MSVCP OK"*) echo "  ok   $d";; *) echo "  FAIL $d: $(printf '%s' "$out" | head -3)"; fail=1;; esac
  done
else
  echo "  skip (run emu/tools/dx-pack.sh public/x64/dx /var/tmp/dx/pack first)"
fi
echo "[6] honest rejections"
check "32-bit PE"   '"verdict":"unsupported"' "$(printf 'MZ' | head -c 2 > /tmp/pe32.bin; python3 - <<'PY'
import struct
b=bytearray(0x200); b[0:2]=b'MZ'; struct.pack_into('<I',b,0x3c,0x80); b[0x80:0x84]=b'PE\0\0'; struct.pack_into('<H',b,0x84,0x14c); struct.pack_into('<H',b,0x94,0xe0); struct.pack_into('<H',b,0x98,0x10b)
open('/tmp/pe32.exe','wb').write(b)
PY
$X --inspect /tmp/pe32.exe 2>&1)"
check "not a PE"    '"ok":false' "$($X --inspect tests/run.sh 2>&1 || true)"
[ $fail = 0 ] && echo "ALL OK" || { echo "FAILURES"; exit 1; }
