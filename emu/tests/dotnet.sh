#!/bin/sh
# .NET capability tests for the Nebula x64 emulator: the REAL Microsoft .NET 10 runtime (CoreCLR + RyuJIT, win-x64)
# runs managed programs on the emulator.  Needs the .NET pack unpacked (emu/tools/dotnet-pack.sh <out> <plaindir>) and
# the test apps built with the .NET SDK (emu/tests/dotnet/app → net10.0, emu/tests/dotnet/fx → net48).
# usage: emu/tests/dotnet.sh [packdir=/var/tmp/dn/pack] [appdir=/var/tmp/dn/app] [fxdir=/var/tmp/dn/fx]
set -e
cd "$(dirname "$0")/.."
PACK=${1:-/var/tmp/dn/pack}; APP=${2:-/var/tmp/dn/app}; FX=${3:-/var/tmp/dn/fx}
[ -f "$PACK/dotnet.exe" ] || { echo "dotnet.sh: no dotnet.exe in $PACK (run emu/tools/dotnet-pack.sh public/x64/dotnet $PACK)"; exit 2; }
[ -f "$APP/nebula-dotnet-test.dll" ] || { echo "dotnet.sh: no nebula-dotnet-test.dll in $APP (dotnet publish emu/tests/dotnet/app -c Release -o $APP)"; exit 2; }
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
xr() { timeout 300 ./x64/x64run --quiet --no-stdin --max-ms 170000 --mount "$PACK=C:\\Program Files\\dotnet" --mount "$APP=C:\\app" --mount-rw "$TMP=C:\\tmp" "$@"; }
fail=0
check() { # name, expected-substring, output
  if printf '%s' "$3" | grep -q -F -- "$2"; then echo "  ok   $1"; else echo "  FAIL $1: expected '$2' in:"; printf '%s\n' "$3" | head -30; fail=1; fi
}
code() { [ "$1" = "$2" ] && echo "  ok   exit code $2" || { echo "  FAIL exit code: $1 (expected $2)"; fail=1; }; }
run() { rc=0; out=$(xr "$@" 2>&1) || rc=$?; }

echo "[D1] dotnet.exe exec app.dll — LINQ, exceptions (NRE as hardware fault → managed), JSON, reflection + dynamic, Regex, GC"
run "$PACK/dotnet.exe" 'C:\app\nebula-dotnet-test.dll' linq-throw-nre-json-reflect-regex-gc
check "banner"      "mode linq-throw-nre-json-reflect-regex-gc" "$out"
check "linq"        "primes 303" "$out"
check "nre"         "caught NRE" "$out"
check "throw"       "caught boom" "$out"
check "regex"       "regex 12,345,64" "$out"
check "reflection"  "reflect List\`1 1 6 Main,Run,Steps" "$out"
check "json"        'json {"name":"nebula","items":[2,3,5],"ok":true}' "$out"
check "json back"   "back nebula" "$out"
check "gc"          "gc ok gen0=1" "$out"
code "$rc" 3
echo "[D2] apphost (nebula-dotnet-test.exe, native launcher → hostfxr) — culture, process spawn (cmd.exe), async + JSON"
run "$APP/nebula-dotnet-test.exe" async-culture-proc-json-gc
check "culture"     "culture Invariant Language (Invariant Country) |" "$out"
check "upper"       "| STRAßE |" "$out"
check "timezone"    "tz=Coordinated Universal Time" "$out"
check "process"     "proc child-ok" "$out"
check "json"        "back nebula" "$out"
code "$rc" 3
echo "[D3] GC with other threads: a spinning background thread is suspended and redirected; thread pool; blocking threads"
run "$PACK/dotnet.exe" 'C:\app\nebula-dotnet-test.dll' bgspin-alloc-gc
check "gc+spin"     "gc ok gen0=" "$out"
code "$rc" 3
run "$PACK/dotnet.exe" 'C:\app\nebula-dotnet-test.dll' taskrun-bgsleep-alloc-gc
check "gc+pool"     "gc ok gen0=" "$out"
code "$rc" 3
echo "[D4] HttpClient (SocketsHttpHandler on Winsock IOCP: WSASend/WSARecv overlapped + completion port)"
run "$PACK/dotnet.exe" 'C:\app\nebula-dotnet-test.dll' http
if printf '%s' "$out" | grep -q "http fail"; then echo "  skip (no network: $(printf '%s' "$out" | grep 'http fail' | cut -c1-120))"; else check "http" "http " "$out"; check "bytes" " bytes" "$out"; code "$rc" 3; fi
echo "[D5] managed dll started directly → redirected to dotnet.exe exec"
run "$APP/nebula-dotnet-test.dll" throw-gc
check "redirect"    "caught boom" "$out"
code "$rc" 3
if [ -f "$FX/fx.exe" ]; then
  echo "[D6] classic .NET Framework 4.8 exe (AnyCPU, IL only) — runs on the .NET 10 runtime with a generated runtimeconfig"
  rc=0; out=$(xr --mount "$FX=C:\\fx" "$FX/fx.exe" a b 2>&1) || rc=$?
  check "hello"       "Hello from a .NET Framework 4.8 exe on 10.0." "$out"
  check "linq"        "linq: b,a" "$out"
  check "exception"   "caught: fx boom" "$out"
  check "file io"     "file: written by net48 app" "$out"
  check "args"        "args: a b" "$out"
  code "$rc" 5
else echo "[D6] fx.exe: skip (not built)"; fi
echo "[D7] honest rejection without the pack"
rc=0; out=$(timeout 60 ./x64/x64run --quiet --no-stdin --mount "$APP=C:\\app" "$APP/nebula-dotnet-test.dll" gc 2>&1) || rc=$?
check "message"     "is a .NET assembly: install the .NET runtime pack" "$out"
[ "$fail" = 0 ] && echo "ALL OK" || { echo "FAILURES"; exit 1; }
