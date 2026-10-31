# nebula_gaming — Nebula OS + **Nebula x64**: real Windows x86‑64 executables in the browser (wasm64 + WebGL)

`nebula_gaming` = the Nebula OS web desktop **plus an x86‑64 user‑mode emulator with a Win32 personality**,
written in C, compiled with `zig cc -target wasm64-freestanding` (true 64‑bit WebAssembly, memory64) and
presented through WebGL.  It runs unmodified PE32+ `.exe` files inside the **x64 Runner** app — in the
browser, in the PWA and in the Android APK (any Chromium ≥ 133 / WebView ≥ 133).

![NebulaStrike.exe running inside the emulator](docs/x64-nebulastrike.png)
![PuTTY plink.exe (official MSVC build) running inside the emulator](docs/x64-plink.png)
![SQLite 3.50.4 shell (official MSVC build) building a file database inside the emulator, in Chromium](docs/x64-sqlite.png)
![7-Zip 25.01 — 7z.exe loads 7z.dll through LoadLibrary/GetProcAddress inside the emulator](docs/x64-7zip.png)
![C++ exceptions and 7-Zip's MSVC error path (System ERROR via __CxxFrameHandler) inside the emulator](docs/x64-exceptions.png)
![Rust programs: ripgrep and fd (official MSVC builds) running inside the emulator](docs/x64-rust.png)
![CPython 3.12.10 (official python.org embeddable build) — interactive REPL over the console inside the emulator, in Chromium](docs/x64-python.png)

## What the emulator is (and is not)

| layer | implemented | notes |
|---|---|---|
| CPU | x86‑64 integer ISA, SSE/SSE2 (+ the SSE4.1/AVX‑free subset compilers emit at `-O2`, SSE3 `movddup`/`movs[lh]dup`), **x87 FPU subset** (load/store/arith/compare/transcendentals, 53‑bit precision) | interpreter, ~20–35 MIPS (wasm64 ≈ 60 % of native C) |
| Loader | PE32+ exe **and the program's own DLLs** (relocations, imports by name/ordinal, exports + forwarders, `DllMain`, static TLS per image and per thread, TLS callbacks, data imports such as `_environ`/`_iob`), `LoadLibrary`/`GetProcAddress`/`GetModuleHandle` on real modules, command line, environment, TEB/PEB | 32‑bit PE and .NET are rejected with an honest verdict; system DLLs are always the emulator's shims |
| KERNEL32 | heap, virtual memory, **threads + synchronisation** (`CreateThread`, critical sections, SRW locks, condition variables, events, mutexes, semaphores, `WaitFor{Single,Multiple}Objects(Ex)`, `WaitOnAddress`, `InitOnce`, `QueueUserAPC`, thread TLS/static TLS, winpthreads), TLS/FLS, time/QPC, console I/O **+ stdin**, **in‑memory file system** (`CreateFile/ReadFile/WriteFile/SetFilePointer/FindFirstFile/GetFileAttributes/Create‑Delete‑Move/Current directory/file mappings`), dynamic environment block, **processes** (`CreateProcessA/W`, `WaitForSingleObject/WaitForMultipleObjects`, `GetExitCodeProcess`, `TerminateProcess`, `OpenProcess`, Toolhelp snapshot, `ReadProcessMemory`, psapi names), **anonymous pipes** (`CreatePipe`, `PeekNamedPipe`, handle inheritance via `SECURITY_ATTRIBUTES`/`SetHandleInformation`/`STARTF_USESTDHANDLES`), locale stubs | up to 32 processes and their threads share one cooperative scheduler inside the wasm module; the disk lives in RAM — mount a folder, download what the program wrote |
| UCRT / msvcrt | printf family (ANSI + wide), string/memory/ctype, math (via host), malloc, qsort/bsearch, time/strftime, getenv/putenv, atexit, setjmp, **stdio on real files** (`fopen/fread/fgets/getc(_filbuf)/fseek/ftell/_open/_read/_write/_lseek/_stat/_access/_unlink/rename/_mkdir/_getcwd/dup/dup2`) | redirections `>`/`<` work in busybox `sh`; pipes/`$(…)` need child processes |
| **SEH / C++ exceptions** | x64 table‑based unwinding (`.pdata`/`UNWIND_INFO`, chained info, epilogue detection): `RaiseException`, two‑phase dispatch, `RtlUnwindEx`/`RtlRestoreContext` incl. `STATUS_UNWIND_CONSOLIDATE` and collided unwinds, `RtlVirtualUnwind`/`RtlLookupFunctionEntry`/`RtlCaptureContext`, `__C_specific_handler` (`__try/__except/__finally`), **MSVC `__CxxFrameHandler`/`__CxxFrameHandler3`** (FuncInfo tables, cleanup funclets, catch by value/reference/`...`, nested catches, rethrow) and the gcc/libunwind path (`_CxxThrowException`, `STATUS_GCC_THROW`) **Hardware faults of the interpreter (access violation, divide by zero, `ud2`) are dispatched as SEH exceptions** at the faulting instruction (`EXCEPTION_RECORD` with address/rw, continuable), vectored exception handlers (`AddVectoredExceptionHandler`, `EXCEPTION_CONTINUE_EXECUTION` with a patched `CONTEXT`), `SetUnhandledExceptionFilter` callback | `__CxxFrameHandler4` (VS2019 compressed tables) not yet; `_set_se_translator`/`exception_ptr` stubs |
| USER32 / GDI32 | windows, message loop, timers, keyboard/mouse, DCs, DIBs, BitBlt/StretchBlt, pens/brushes, text (bitmap font) | **no OpenGL/Vulkan, no audio, no dialog resources** |
| **D3D11 / DXGI (stage 1)** | the DXVK‑style translation layer, started: real COM vtables generated from `d3d11.h`/`dxgi.h`, `D3D11CreateDevice(AndSwapChain)`, device/context/swap chain objects, textures, render‑target/depth/shader‑resource views, buffers, shaders, states, `Map/Unmap/UpdateSubresource`, `ClearRenderTargetView` + `Present` into the window (WebGL2 canvas / PPM frames) — `tests/d3d11clear.exe` shows three cleared frames | **no draw calls yet**: `Draw*` stop honestly with the method name until the DXBC → GLSL translator + WebGL2 backend (stage 2) lands; even then the CPU interpreter (40–65 MIPS) caps it at demos, not games  |
| **WS2_32** | Winsock 2 over the host TCP stack (native runner): `socket/connect/bind/listen/accept`, `send/recv`, `WSASend/WSARecv` + IOCP, `ConnectEx/AcceptEx`, `select`/`WSAPoll`, `getaddrinfo`; this is what libuv (node.exe) and uSockets (Bun) use — HTTPS works end‑to‑end (OpenSSL in node, BoringSSL in Bun) | the browser has no raw TCP: in wasm64 every socket call fails with `WSAENETDOWN` instead of hanging |
| **NTDLL** | the `Nt*/Rtl*` subset that Zig std, libuv and Bun call directly (`NtCreateFile/NtReadFile/NtWriteFile/NtQueryInformationFile/NtSetInformationFile/NtQueryDirectoryFile/Nt*VirtualMemory/NtQuerySystemInformation`, `RtlNtStatusToDosError`, …), PEB `ProcessParameters` with real std handles/command line/environment | — |
| **Node‑API / Bun add‑ons** | `.node` add‑ons delay‑load `napi_*` from `node.exe`; `GetProcAddress` resolves them from the host exe's own export table (Bun exports 512 symbols) | — |
| ADVAPI32 / COMCTL32 | stubs that fail cleanly | — |

The **PE compatibility scanner** (`x64_inspect`) reads any `.exe` and reports architecture, subsystem, every
import and whether it is implemented, then gives a verdict: *should run* / *partial* / *unlikely* / *unsupported*.
At runtime the first unimplemented call stops the program with the API name, RIP and opcode bytes — never a silent hang.

### Measured (CI + this repo's test suite)

| executable | origin | result |
|---|---|---|
| `hello.exe` (UCRT console) | this repo | all output correct, exit code 7, 4.4 M instr |
| `bench.exe` (sieve/mandelbrot/memcpy) | this repo | correct; 36 MIPS native, 22 MIPS wasm64 (V8) |
| `NebulaStrike.exe` (Win32 GDI game, PE32+) | this repo | plays at 60 fps in the browser, ~245 fps headless |
| `busybox64.exe` (busybox‑w32, mingw64‑gcc 16) | frippery.org | file applets work on the virtual disk: `ls -l cat wc sort head tail grep sed awk cut find diff cmp cp tee xxd base64 md5sum sha256sum stat du mkdir gzip` + stdin; **`sh` is a real shell now**: pipelines (`seq 1 5 \| sort -rn \| head -2`), `$(…)`, background jobs `&`/`wait`/`kill`, `ps` (reads sibling processes' command lines through `ReadProcessMemory`), `xargs -I{} sh -c`, `yes \| head` (EPIPE), 1 MB through a pipe, `time` — each stage is a separate emulated process (busybox‑w32 `--forkshell` via `_spawnve` + inherited file mapping); sockets still stop at Winsock |
| `threads.exe` / `pthreads.exe` (Win32 threads + sync / mingw winpthreads) | this repo | 39/39 and 9/9 checks native and in Chromium (wasm64); 4 CPU‑bound threads 43 MIPS aggregate |
| **`sqlite3.exe` (SQLite 3.50.4, official MSVC build)** | sqlite.org | `:memory:` and **file databases** (LockFileEx, OVERLAPPED offsets): 2000‑row table + index + `pragma integrity_check` → ok, in the browser 17 MIPS |
| **`jq.exe` (jq 1.8.1)** | github.com/jqlang | filters, `-s`, math, `--version` — correct output |
| **`lua54.exe` + `lua54.dll` (Lua 5.4.2)** | luabinaries | the exe imports the interpreter DLL → loaded from the virtual disk at its preferred base; scripts, `io.open`, `os.date` work (31 M instr / 2 s wasm64) |
| **`7z.exe` + `7z.dll` (7‑Zip 25.01)** and `7za.exe` | 7‑zip.org | `7z.dll` loaded via `LoadLibrary` + `GetProcAddress("CreateObject")`; `a`/`x`/`l`/`i`/`b` work, 7z/zip round trips bit‑exact; `b -mmt1` ≈ 34 MIPS |
| **`zstd.exe` (Zstandard 1.5.7, MSVC)** | github.com/facebook/zstd | compress/decompress round trips, built‑in benchmark (`-b1 -e3`) runs |
| **`rg.exe` (ripgrep 14.1.1, Rust, x86_64-pc-windows-msvc)** | github.com/BurntSushi/ripgrep | single files and directory trees, regex, `-c`/`-n`/`--stats`, `-j4` worker threads — identical counts to GNU grep; Rust std's Win32 layer (`NtReadFile`, SRW locks, `WaitOnAddress`, waitable timers, `GetFinalPathNameByHandleW`, `ProcessPrng`) |
| **`fd.exe` (fd 10.2.0, Rust)** | github.com/sharkdp/fd | file finder: `-e`, `-t f`, `-S`, coloured output; `-x cmd /c echo found {}` and `-X jq.exe` spawn children through the built‑in `cmd.exe` |
| **`cmd.exe` (the emulator's own Command Prompt, 53 KB, kernel32 only)** | this repo (`emu/cmd/cmd.c`) | embedded at `C:\Windows\System32\cmd.exe`, so `system()`, `_popen`, `os.system`, `os.popen`, `subprocess(shell=True)` and `rg --pre` work; batch files (`demo.bat`), `/c` `/k` and the interactive prompt; builtins `echo set set /a if for (/l /f) call goto exit /b dir type copy del ren md rd cd pushd popd find findstr sort more ver date time pause rem`, `& && \|\| \|`, `> >> < 2>&1`, `(…)` groups, `%VAR%` `%~n1` `%ERRORLEVEL%` `%RANDOM%`; **not** implemented: delayed expansion `!V!`, `usebackq`, `attrib`, `mklink`, `choice`, `%%` stays literal in `/c` mode |
| **`python.exe` (CPython 3.12.10, official python.org embeddable build, MSVC v.1943)** | python.org | the whole interpreter: `python312.dll` (4.3 MB), stdlib from `python312.zip` (zipimport), C extensions `_sqlite3`/`_decimal`/`_ctypes`/`_bz2`/`_lzma`/`pyexpat`/`unicodedata`/`_asyncio`/… as `.pyd`; `-c`, scripts, the **interactive REPL over console stdin** (`ReadConsoleW`), `threading`, `subprocess` with pipes (`python.exe` spawning `python.exe`), `ctypes.windll.kernel32`, file I/O, tracebacks, exit codes; `math` within 1 ulp of the host. Not available: `_ssl`/sockets (no network), `tkinter`, `_wmi`. Start‑up ≈ 60 M instr (2 s native, ~4 s wasm64); the bundle (15 MB) is downloaded lazily on first run |
| **`node.exe` (Node.js 24.11.0, official win‑x64 build, 90 MB)** | nodejs.org | V8 + libuv: `-e` scripts in ~3 s, `npm --version`, `child_process.spawnSync/exec` (libuv named pipes + IOCP zero‑reads → real child stdout), `require('https').get()` → real TLS to the Internet (3.6 s for 70 KB); `fetch()` fails only because undici's wasm llhttp needs a 10 GB reservation |
| **`bun.exe` (Bun 1.4.4, win‑x64‑baseline, 115 MB)** | github.com/oven-sh/bun | JavaScriptCore without JIT: `bun -e` in 3 M instructions (0.08 s), `fetch("https://…")` → status 200 in 0.55 s (uSockets over libuv `select` slow‑poll + BoringSSL), `Bun.spawn`, SQLite, workers |
| **`opencode.exe` (opencode 1.19.5, Bun standalone, 211 MB)** | npm `opencode-windows-x64-baseline` | **the real OpenTUI terminal UI renders and is interactive**: `--version` in 77 s; the TUI (main thread + 12 worker threads, SQLite, 2 Bun DLLs incl. a Node‑API add‑on, the in‑process HTTP server) draws its first frame after ≈ 9 min / 42 G instructions at 65 MIPS, keyboard input reaches the prompt, Enter creates a session and the LLM request goes out over HTTPS — see `docs/screenshots/` |
| `hwexc.exe` (hardware exceptions) | this repo | AV / div0 / `ud2` caught by `__except` with correct `ExceptionInformation`, `__finally` during the unwind, vectored handler patches `Rip`/`Rax` and continues, 2000 faults in a loop, top-level filter → `ExitProcess(77)` |
| `cxxexc.exe` / `sehtest.c` (zig c++ / `-fms-extensions`) | this repo | `throw`/`catch std::exception`, destructors during unwinding, nested rethrow, `vector::at`, 1000 throws in a loop; `__try/__except/__finally` incl. nested — native and in Chromium |
| **7‑Zip error paths** (`x missing.7z`, `-tbogus`, wrong password) | 7‑zip.org | the MSVC `throw` → `__CxxFrameHandler` → catch chain inside `7z.exe`/`7z.dll` works: `System ERROR: The system cannot find the file specified.`, exit code 2 (used to stop the emulator) |
| `shelltest.exe` (C: `system`, `_popen` r/w) and Python `os.system` / `os.popen` / `subprocess(shell=True)` | this repo | exit codes propagate, pipes both ways, `sort` fed through `_popen("w")`, `SleepEx` alertable + `ReadFileEx`/APC path used by Rust `std::process` |
| `plink.exe` (PuTTY 0.85, MSVC/clang‑cl) | putty.org | `-V` runs to completion; real sessions need Winsock |
| `putty.exe` (GUI) | putty.org | runs up to `CreateDialogParamA` (dialog resources not implemented) |
| `curl.exe` (8.23, mingw) | curl.se | stops at `Secur32!InitSecurityInterfaceA` (TLS stack) |
| `d3d11clear.exe` (this repo, Direct3D 11) | `emu/tests/d3d11clear.c` | `D3D11CreateDeviceAndSwapChain` → `GetBuffer` → `CreateRenderTargetView` → 3× `ClearRenderTargetView` + `Present`: the frames come out in the requested colours (`docs/D3D11-X64.md`) |
| Unity / D3D11 / Vulkan games (e.g. Nine Sols) | — | **unsupported**: need a GPU API, JIT and GBs of RAM — not feasible in a wasm64 interpreter (the D3D11 layer above makes small D3D11 programs possible, not AAA games) |

```bash
# native build + capability tests (needs zig 0.17 for the wasm step)
ZIG=zig ./emu/x64/build.sh      # → emu/x64/x64run + public/x64/nebula_x64.wasm (≈210 KB)
./emu/tests/run.sh              # hello/bench/busybox (files, stdin, processes+pipes)/plink/NebulaStrike/threads/heap/DLLs/real apps/x87/SEH+C++ exceptions/hardware exceptions/Rust apps/CPython/cmd.exe+system()/rejections — 96 checks
BUILD_CMD=1 ZIG=zig ./emu/x64/build.sh   # also rebuilds emu/cmd/cmd.exe + the embedded emu/x64/cmd_exe.h from emu/cmd/cmd.c
./emu/x64/x64run --inspect some.exe         # JSON compatibility report
./emu/x64/x64run --max-ms 5000 some.exe a b # run headless (frames to .ppm with --frames N)
```

---

# Nebula OS Shell — web desktop with a real AI agent TUI

A Windows‑10‑style desktop that runs entirely in the browser (React 19 + Vite + Tailwind):
File Explorer, Command Prompt, Edge, Steam/Epic mock‑ups, the **Nebula Strike** game
(a Zig game compiled to **win64 / wasm32 / wasm64**) and an **opencode AI TUI** window
backed by the real [opencode](https://opencode.ai) agent through a Bun bridge.

![opencode live](docs/opencode-live.png)

## Quick start

```bash
npm ci
npm run dev            # http://localhost:5173  (demo mode for the opencode window)

# real AI agent (free model opencode/big-pickle, no API key):
bun install -g opencode-ai
npm run opencode       # Bun bridge :8787 → opencode serve :4096 (vite proxies /api/opencode)
```

Scripts

| script | what |
|---|---|
| `npm run dev` / `build` / `preview` | Vite dev server / production build to `dist/` / preview |
| `npm run lint` | ESLint (0 problems) |
| `npm run opencode` | `bun run opencode/bridge.ts` — real agent bridge |
| `npm run opencode:compile` | `bun build --compile` → single `opencode-bridge` binary (serves `dist/` + API) |
| `npm run test:opencode` | `bun test` — end‑to‑end test of the bridge + real model answer |
| `npm run test:e2e` | Playwright desktop e2e (31 scenarios incl. the x64 emulator, CPython, virtual disk + stdin, processes + pipes, threads: windows, terminal, game autotest, opencode demo/live, phone viewport) |
| `node e2e/with-bridge.mjs` | starts the real bridge, runs e2e with the LIVE opencode window, stops it |
| `npm test` / `npm run test:all` | lint + typecheck + e2e (+ bridge test) |
| `npm run opencode:compile:all` | binaries for linux‑x64/arm64, macOS arm64/x64, windows‑x64 → `release/` |

## Platforms

| Platform | How |
|---|---|
| **Web** (any modern browser, desktop/mobile) | `npm run dev` / `npm run preview` / the bridge itself serves `dist/` on :8787 |
| **PWA** (installable on Android, iOS, ChromeOS, Windows, macOS) | open the site → "Install app" / "Add to Home screen"; offline shell, `/api` never cached |
| **Android APK** (Capacitor) | CI builds a signed `NebulaOS-vX.Y.Z-android.apk` (Releases). **No PC needed**: the opencode agent runs natively on the phone (see below). A PC bridge is optional (⚙ → mode `bridge`, `http://<pc-ip>:8787`) |
| **Windows / macOS / Linux** | `npm run opencode` or the single‑file `opencode-bridge-*` binary from Releases (serves the desktop + real agent) |

Touch devices get on‑screen controls in Nebula Strike (◀ ▶ FIRE), pointer‑event window dragging and maximized windows under 640 px.

### Native opencode agent (no bridge, no PC)

`src/lib/agent/core.ts` is an in‑app implementation of the opencode agent loop: the same system prompt, tool catalogue
(`bash edit glob grep read write webfetch todowrite task …`), request/session identifiers and SSE protocol as the opencode
CLI 1.19.5, talking to the free **OpenCode Zen** endpoint (`opencode/big-pickle`, `Bearer public`; your own key can be
entered in ⚙). The loop is platform‑agnostic — only the *host* differs:

| Build | Model transport | Tools |
|---|---|---|
| **Android APK** | `NebulaNativePlugin.httpStream` — streaming HTTPS from Java, straight from the phone | real `/system/bin/sh`, real files in `Android/data/com.bigpickle.nebulaos/files/workspace`, `webfetch` |
| **Web / PWA** | bridge pass‑through `/api/opencode/zen/*` (browsers cannot call opencode.ai directly: no CORS, forbidden `User-Agent`) | in‑browser workspace (localStorage), file tools + `webfetch` |
| **CI / Bun** (`opencode/native-agent.test.ts`) | `fetch` | Bun shell + fs |

Mode is chosen in the opencode window (⚙): `auto` (native in the APK, bridge on the web), `native`, `bridge`.

### WebGL renderer
Nebula Strike is drawn by `src/lib/webgl.ts` (WebGL2, WebGL1 fallback, Canvas 2D only if the GPU is unavailable):
procedural nebula background shader, one dynamic VBO for solid geometry, an additive glow pass and a HUD texture.
With the wasm64 core the vertex data is read **directly from the module's 64‑bit linear memory** (`NebulaWasm64.views()`),
no per‑entity JS objects. The badge in the window shows `WASM64 · MEMORY64 · WEBGL2` and the GPU string.

### Android build locally
```bash
npm run build && npx cap sync android
cd android && ./gradlew assembleRelease      # android/app/build/outputs/apk/release/app-release.apk
```
(JDK 17+, Android SDK; the demo keystore `android/keystore/nebula-release.jks` is in the repo — override with `NEBULA_KEYSTORE*` env vars for your own key.)

## Cross‑platform

* Runs on **Windows, macOS and Linux**: the bridge finds `opencode` in PATH, `~/.bun/bin`, `node_modules/.bin`,
  `%APPDATA%\npm`, `/opt/homebrew/bin`, wraps `.cmd` shims on Windows and falls back to `bunx opencode-ai`.
* CI (`.github/workflows/ci.yml`) runs lint → typecheck → build → bun bridge test (real model) → native agent loop test (real model) → Playwright e2e
  on **ubuntu‑latest, windows‑latest and macos‑latest**, then builds single‑file bridge executables for all
  platforms; a `v*` tag publishes them (with `dist/`) as a GitHub Release.
* The desktop works with mouse, touch and pen (pointer events), fits phone screens (windows open maximized
  under 640 px, are clamped to the viewport on resize/rotation) and needs only a modern browser.

## Project layout

```
src/                 desktop shell (App, WindowFrame, Taskbar, StartMenu, apps/*)
src/components/apps/OpencodeApp.tsx   real/demo opencode TUI window
src/lib/agent.ts     scripted "Big Pickle" demo used when the bridge is offline
opencode/bridge.ts   Bun bridge (spawns opencode serve, proxies REST + SSE, serves dist/)
opencode/bridge.test.ts  bun test
opencode.json        opencode config (model opencode/big-pickle, permissions)
game/                Nebula Strike sources (Zig) + build/autotest scripts
public/downloads/    built NebulaStrike.exe, wasm32/wasm64, source zip, autotest results
TEST_REPORT.md       test report (Russian): fixes, benchmarks, opencode integration
```

## In‑desktop tips

* Command Prompt: `help`, `dir`, `nebula`, `opencode` (opens the AI TUI), **`real`** — hands over to the real `cmd.exe` running in the x86‑64 emulator (x64 Runner).
* opencode window: green badge **live** = real agent working inside this repository
  (it can read/edit files and run commands there); yellow **demo** = bridge not running.
* Nebula Strike: wasm64 needs Chrome ≥ 133 / Node ≥ 22; wasm32 works everywhere.

See [opencode/README.md](opencode/README.md) for the bridge API and environment variables.
