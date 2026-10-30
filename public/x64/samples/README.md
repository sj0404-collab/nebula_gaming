# Bundled sample executables (unmodified third-party binaries)

| file | origin | licence |
|---|---|---|
| hello.exe, bench.exe, threads.exe, pthreads.exe | built from emu/tests/*.c with `zig cc -target x86_64-windows-gnu` | MIT (this repo) |
| busybox64.exe | https://frippery.org/busybox/ (busybox-w32, FRP-6354) | GPL-2.0 — source at the link |
| plink.exe | https://www.chiark.greenend.org.uk/~sgtatham/putty/ (PuTTY 0.85, 64-bit) | MIT |

They are used by the x64 Runner app as ready-made test subjects and by the CI capability tests.
