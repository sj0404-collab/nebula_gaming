# Windows runtime components inside the Nebula x64 emulator

Programs built with Visual Studio need the *Visual C++ Redistributable* (`msvcp140.dll`, `vcruntime140_1.dll`,
`concrt140.dll`, `vcomp140.dll`, `mfc140u.dll`, …). On a real Windows the installer puts them into
`C:\Windows\System32`; inside the emulator they are provided by the **runtime pack**.

## What the pack is

`emu/tools/runtime-pack.sh` downloads the official `vc_redist.x64.exe` (https://aka.ms/vs/17/release/vc_redist.x64.exe),
unpacks its cabinets (`cabextract`) and writes the 26 x64 DLLs plus `manifest.json` (name, size, sha256, file version)
into `public/x64/sys32/`. The files are **not committed** — the CI job *Windows runtime pack* builds them on every
run and the other jobs download the artifact before `npm run build`, so the deployed site and the APK serve them at
`/x64/sys32/`.

| file | version (CI 2026‑11) | notes |
|---|---|---|
| msvcp140.dll, msvcp140_1/_2, msvcp140_atomic_wait, msvcp140_codecvt_ids | 14.44.35211 | C++ standard library |
| vcruntime140.dll, vcruntime140_1.dll, vcruntime140_threads.dll | 14.44.35211 | `vcruntime140.dll` itself stays **built-in** (memcpy/EH primitives are shims); `_1`/`_threads` load for real |
| concrt140.dll, vcomp140.dll, vcamp140.dll, vccorlib140.dll | 14.44.35211 | ConcRT / OpenMP / C++ AMP / WinRT helpers |
| mfc140.dll, mfc140u.dll, mfcm140*.dll, mfc140{chs,cht,deu,enu,esn,fra,ita,jpn,kor,rus}.dll | 14.44.35211 | MFC (needs user32/gdi32 beyond the current subset for real MFC apps) |

## How the emulator uses it

* **DLL search order** (`emu/x64/win32.c`, `mod_load`): the exe's directory → directories added with
  `AddDllDirectory`/`SetDllDirectory` → `C:\Windows\System32` → cwd → `C:\nebula` → `PATH`.
* **Policy** (`pe_dll_native_ok` in `emu/x64/pe.c`): core OS DLLs (kernel32, ntdll, ucrtbase, api-ms-win-*, user32,
  gdi32, ws2_32, …) are *always* the built-in implementation. For vendor-runtime names (`msvcp*`, `msvcr*`, `concrt*`,
  `vcomp*`, `vcamp*`, `vccorlib*`, `mfc*`, `vcruntime140_1`, `vcruntime140_threads`, `libstdc++`, `libgcc_s`,
  `libwinpthread`, `gdiplus`, `d3dcompiler_*`, `d3dx9/10/11_*`, `d3dcsx*`, `xinput1_3`, `x3daudio*`) a real file found
  on the virtual disk is loaded like any program DLL; otherwise the built-in shim module answers (few C++ names).
* The real DLLs import UCRT *internals* that no ordinary program uses; they live in `emu/x64/win32_rt.inc`
  (`_lock_locales`, `___lc_locale_name_func`, `_Getdays/_Getmonths/_Gettnames`, `_Strftime/_Wcsftime`,
  `_get_stream_buffer_pointers`, `__processing_throw`, `__uncaught_exception(s)`, `_invoke_watson`, `<fenv.h>`, …)
  and `emu/x64/win32_math.inc` (float/long-double variants, C99 complex functions with the MSVC x64 struct ABI,
  `_dpcomp`, `__stdio_common_vsscanf/vfscanf`).
* In the browser: x64 Runner shows a **VC++ runtime** button (state in `data-x64-pack`): *available* → click installs
  the DLLs into `C:\Windows\System32` of the virtual disk; it also happens automatically when the selected exe imports
  one of the pack's DLLs. Natively: `x64run --mount public/x64/sys32=C:\Windows\System32 …`.

## Verified

All 26 DLLs of the pack load from `C:\Windows\System32` and run their `DllMain`/static initialisers to completion
(`emu/tests/run.sh` section `[5z]`, `msvcp.exe <dll>`): concrt140, mfc140, mfc140u, mfcm140(u), the 10 MFC language
DLLs, msvcp140(_1/_2/_atomic_wait/_codecvt_ids), vcamp140, vccorlib140, vcomp140, vcruntime140(_1/_threads).

```bash
./emu/tools/runtime-pack.sh                                   # → public/x64/sys32
cd emu && ./tests/run.sh                                      # [5x] msvcp.exe: real msvcp140 → "42 3.25", "MSVCP OK"
# numpy 2.5.4 (official wheel) on the official CPython 3.12 build
pip download --only-binary=:all: --platform win_amd64 --python-version 3.12 --implementation cp numpy  # then unzip into ./site
./emu/x64/x64run --quiet --no-stdin --mount ./py --mount './site=C:\site' ./py/python.exe 'C:\site\t_np.py'
# numpy 2.5.4 import 32 s · sum/mean/inv/@/fft/sort/default_rng → identical to host numpy
```

numpy loads its own `numpy.libs/msvcp140-<hash>.dll` (via `AddDllDirectory`) and `vcruntime140_1.dll`; OpenBLAS
sizes its kernels from CPUID leaves 2/4/0x80000006, numpy's dispatcher requires x86-64-v2 (`LAHF` in 0x80000001).

## Honest limits

* **.NET Framework / .NET (Core)** is not emulated — a CLR (JIT, GC, BCL) is a project of its own; `.NET` programs stop at `mscoree.dll!_CorExeMain`.
* **DirectX redistributables** (`d3dx9_43`, `d3dcompiler_43`, XAudio2): the loader policy already prefers real files, but the emulator implements only a Direct3D 11 subset (see `docs/D3D11-X64.md`); Direct3D 9/10/12 and audio are not implemented.
* The kernel32 **thread-pool API** is implemented (`emu/x64/win32_tp.inc`: work, timers, waits, simple callbacks, instance helpers, pools, cleanup groups — `emu/tests/tpool.c` 14/14) **except thread-pool I/O** (`CreateThreadpoolIo` → stops with the API name). Cancelling a pending `SetThreadpoolWait` cannot interrupt the parked helper until its handle signals; cleanup groups do not track members.
* `fetestexcept` only sees the MXCSR flags the interpreter raises (few), so numpy's "divide by zero" runtime warnings are mostly silent.
* MFC GUI applications also need USER32 dialog resources and common controls that the emulator does not have.
