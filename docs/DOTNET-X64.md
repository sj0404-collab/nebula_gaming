# .NET inside the Nebula x64 emulator (the ".NET pack")

Programs written in C#/VB/F# — classic *.NET Framework* exes and modern *.NET* apps — need a CLR. The emulator does
not re-implement one: it runs the **official Microsoft .NET Runtime 10 (win-x64)** — CoreCLR, RyuJIT, the GC, the
host (`dotnet.exe`, `hostfxr`, `hostpolicy`) and the framework assemblies — as ordinary x64 Windows code on top of its
Win32 layer. Nothing in the runtime is modified; the emulator only provides the Win32/NT surface the runtime calls.

## What the pack is

`emu/tools/dotnet-pack.sh` downloads the official
`https://builds.dotnet.microsoft.com/dotnet/Runtime/10.0.14/dotnet-runtime-10.0.14-win-x64.zip` (37 MB), gzips every
file and writes `public/x64/dotnet/<path>.gz` + `manifest.json` (`{pack, version, source, zip_sha512, built, guest_dir,
encoding, files:[{path,size,gz,gzsize,sha256}]}`): 191 files, 76.8 MB unpacked, 34.0 MB to download. The files are
**not committed** — the CI job *.NET pack* builds them on every run and the other jobs download the artifact before
`npm run build`, so the deployed site and the APK serve them at `/x64/dotnet/`.

In the browser the pack is installed into `C:\Program Files\dotnet` of the virtual disk by the **.NET 10** button of
x64 Runner, or automatically the first time a .NET program is started (6 parallel downloads, inflated with
`DecompressionStream`; a server that already sends `Content-Encoding: gzip` is handled too).

## How a .NET program is started

| what you open | what happens |
|---|---|
| `app.exe` **apphost** (modern .NET, `dotnet publish` output; `app.dll` + `app.runtimeconfig.json` next to it) | runs as the native launcher it is: it finds `hostfxr.dll` through `DOTNET_ROOT=C:\Program Files\dotnet` (set by the emulator) and starts `app.dll` |
| `app.dll` managed assembly with an entry point | rewritten to `"C:\Program Files\dotnet\dotnet.exe" exec "C:\…\app.dll" args` |
| `app.exe` **IL-only** exe (classic .NET Framework 2.0–4.8, AnyCPU or x64) | rewritten to `dotnet.exe exec --runtimeconfig C:\tmp\app.runtimeconfig.json "C:\…\app.exe" args` with a generated config: `net10.0`, `rollForward: LatestMajor`, invariant globalization, BinaryFormatter allowed — the compatibility mode in which .NET Framework console apps run on the modern runtime |
| `app.exe` managed, pack not installed | honest loader message: *`app.exe is a .NET assembly: install the .NET runtime pack …`* |
| mixed-mode (C++/CLI) image | *unsupported* in the PE report |

The same redirect applies to `CreateProcess` from inside the emulator (a .NET app starting another .NET app, cmd.exe
running one). `x64run --inspect` / the PE report expose `dotnet: "managed" | "managed-dll" | "apphost" | null`.

Environment defaults set by the emulator for the runtime: `DOTNET_EnableWriteXorExecute=0` (one mapping per page),
`DOTNET_GCRegionRange=0x30000000` (768 MB reservation; committed lazily), `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1` +
`PREDEFINED_CULTURES_ONLY=0` (no ICU inside the emulator — culture names are accepted, formatting is invariant),
`DOTNET_TieredPGO=0`, `DOTNET_ROOT`.

## What the emulator had to provide for CoreCLR (emu/x64/win32_clr.inc and friends)

* **Threads & GC suspension**: real `GetThreadContext`/`SetThreadContext` (the GC suspends a running managed thread
  with `SuspendThread`, reads its context, and *redirects* it by rewriting `Rip` so it parks at a safe point; blocked
  threads are stack-walked), `RtlVirtualUnwind` filling `KNONVOLATILE_CONTEXT_POINTERS` (otherwise the GC reports
  garbage roots), `QueueUserAPC2`/`InitializeContext2` absent on purpose (the runtime falls back to the classic path).
* **Memory**: `VirtualQuery` that knows every thread's stack and its guard pages (the runtime probes the stack bottom for
  `PAGE_GUARD`, otherwise prints *Stack overflow.*), W^X off, 768 MB GC region range, managed images mapped as data.
* **Exceptions**: hardware faults in JIT-compiled code (null dereference → `NullReferenceException`, integer division)
  go through the vectored handler / SEH path; C++ exceptions inside CoreCLR (`0xe06d7363`) and managed ones
  (`0xe0434352`) use the emulator's `RtlUnwindEx`/`__CxxFrameHandler` implementation.
* **Misc APIs**: `GetSystemTimes`, `QueryUnbiasedInterruptTime(Precise)`, wide ctype (`isw*`), `GetCPInfoExW`,
  `WinHttpGetIEProxyConfigForCurrentUser` (no proxy), `version.dll` stubs, `Ro*`/WinRT and `IErrorInfo` stubs,
  `GetModuleFileName`-relative DLL search for `clrjit.dll` (loaded by bare name from CoreCLR's directory).
* **Crypto (emu/x64/win32_bcrypt.inc)**: CNG hashing — `BCryptOpenAlgorithmProvider`/`CreateHash`/`HashData`/
  `FinishHash`/`DuplicateHash`/`BCryptHash` for MD5, SHA-1/256/384/512 and their HMACs (real + Windows 10 pseudo
  handles, reusable hashes) and `BCryptGenRandom`. `System.Security.Cryptography` hashing P/Invokes these; Roslyn
  fails fast without them (it hashes every source file and reference). No AES/RSA/ECDSA yet.
* **Networking**: `HttpClient` = `SocketsHttpHandler` on overlapped Winsock + I/O completion ports (`WSASend`, `WSARecv`
  with the data buffer captured at call time — the `WSABUF` array itself is a temporary in .NET — `ConnectEx`,
  `GetQueuedCompletionStatusEx`). HTTP only: SChannel/TLS is not implemented, so `https://` fails honestly.

## Measured (emu/tests/dotnet.sh, native x64run; also e2e in Chromium)

| test | result |
|---|---|
| D1 `dotnet.exe exec nebula-dotnet-test.dll linq-throw-nre-json-reflect-regex-gc` | `primes 303`, `caught NRE`, `caught boom`, `regex 12,345,64`, ``reflect List`1 1 6 Main,Run,Steps``, `json {"name":"nebula","items":[2,3,5],"ok":true}`, `back nebula`, `gc ok gen0=1`, exit 3 — **6.4 s** native, ~20 s in Chromium incl. the pack download |
| D2 apphost `nebula-dotnet-test.exe async-culture-proc-json-gc` | culture (invariant), `STRAßE`, `tz=Coordinated Universal Time`, `Process.Start("cmd.exe")` → `proc child-ok`, async + JSON, exit 3 |
| D3 `bgspin-alloc-gc` / `taskrun-bgsleep-alloc-gc` | GC with a *spinning* background thread (suspend + redirect) and with thread-pool / sleeping threads — 2.6 s / 2.1 s |
| D4 `http` | `HttpClient.GetStringAsync("http://example.com/")` → `http 577 bytes` (skipped without network) |
| D5 `nebula-dotnet-test.dll` opened directly | redirected to `dotnet.exe exec`, exit 3 |
| D6 `fx.exe a b` — a **.NET Framework 4.8** console exe (AnyCPU, IL only, 5 KB) | `Hello from a .NET Framework 4.8 exe on 10.0.14 …`, `linq: b,a`, `caught: fx boom`, `file: written by net48 app`, `args: a b`, exit 5 — 2.2 s |
| D8 **Roslyn**: `dotnet.exe csc.dll @hello.rsp` (the real C# compiler, IL-only `csc.dll` from the official `Microsoft.Net.Compilers.Toolset 5.0.0` NuGet package, 25 MB of managed code) | compiles `hello.cs` → `hello.dll` (4.6 KB) with no diagnostics, exit 0 — **~13 G instructions, 325 s** at 40 MIPS in the sandbox (faster on a desktop); the compiled program then runs: `hello from csc inside the emulator: 1,4,9,16,25`, exit 42 |
| D7 without the pack | `fx.exe is a .NET assembly: install the .NET runtime pack …` |

Test sources: `emu/tests/dotnet/app` (net10.0, `dotnet publish -r win-x64 --self-contained false`) and
`emu/tests/dotnet/fx` (net48). The built binaries are bundled as x64 Runner samples (`/x64/samples/dotnet/`).

Bugs found by Roslyn (fixed in v1.17.2): `RtlUnwindEx` captured the caller's context *after* reserving its scratch area
on the stack, so the first unwound frame saw a wrong RSP and the static CRT's `__GSHandlerCheck` reported a cookie
mismatch (`__fastfail` → exit 0xC0000409) on the first C++ `catch` inside CoreCLR; `UnmapViewOfFile` of a *read-only*
view marked the file dirty, and the write-back truncated the very host file the data was mmapped from
(`hello.runtimeconfig.json` became empty) — write-backs now go through a temp file + rename and only for changed data.

## Limits (honest)

* **HTTPS / TLS**: no SChannel → `HttpClient` works for `http://` only; `System.Net.Security` fails.
* **Globalization**: invariant mode (no ICU). `CultureInfo("ru-RU")` is accepted (`PredefinedCulturesOnly=0`) but
  dates/numbers format invariantly and `DisplayName` is *Invariant Language (Invariant Country)*; `TimeZoneInfo.Local`
  is UTC.
* **GUI**: WinForms/WPF need `System.Windows.Forms`/`PresentationCore` (not in the runtime pack — they are part of the
  *Windows Desktop* runtime, which also needs user32/comctl32/DirectX far beyond the emulator's subset). Console apps,
  libraries, workers, ASP.NET-style code paths without TLS are what runs.
* **.NET Framework compatibility** is the modern runtime's: no `AppDomain`s, no remoting, no `System.EnterpriseServices`,
  WCF only via CoreWCF-style packages; apps that P/Invoke into native DLLs need those DLLs on the virtual disk.
* **Self-contained / single-file** published apps: single-file bundles that extract native libraries at runtime are
  untested; framework-dependent is the supported shape. ReadyToRun images load (they are IL + precompiled code).
* **Speed**: everything is interpreted — JIT compilation itself runs on the interpreter, so start-up (~2 s native,
  ~5–8 s in the browser) dominates small programs; Roslyn (`csc.dll`) compiles a file in ~5 minutes.
* **Memory**: the runtime reserves ~1 GB of guest address space (GC range + loader heaps); pages are committed lazily by
  the browser, but phones with little RAM may still kill the tab under a large managed heap.
* No `QueueUserAPC2`, no thread-pool I/O (`CreateThreadpoolIo`), no `GetAddrInfoExCancel` (the runtime tolerates all
  three).
