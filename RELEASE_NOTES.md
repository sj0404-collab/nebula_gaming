# v1.3.0 — Nebula x64: Windows x86‑64 executables in wasm64 + WebGL

* **New app “x64 Runner”** on the desktop / Start menu: open any `.exe` (file picker or drag‑and‑drop; files never leave the device),
  get a PE compatibility report (arch, subsystem, imports implemented/missing, verdict), run it; GUI programs render through WebGL,
  console programs stream to a console pane; keyboard, mouse and touch buttons are forwarded; status bar shows MIPS/frames/memory.
* **Emulator `emu/x64`** (C → `zig cc -target wasm64-freestanding`, 210 KB): x86‑64 + SSE2 interpreter, PE32+ loader, KERNEL32/UCRT/msvcrt/USER32/GDI32 shims,
  honest fault reports (API name, RIP, bytes).  Native runner `x64run` for CI/headless testing.
* **Verified on real third‑party binaries:** busybox‑w32 (mingw64) and PuTTY plink 0.85 (MSVC) run; putty.exe/curl.exe stop at documented APIs.
* CI: new `emulator` job (zig 0.17 build + native capability tests + wasm artifact), e2e grew to 24 scenarios (5 for the emulator), Android APK as before.
* Known limits: no file system / threads / network / Direct3D / audio / dialog resources; 32‑bit and .NET executables are rejected up front.
