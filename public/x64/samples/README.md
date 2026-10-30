# Bundled sample executables (unmodified third-party binaries)

| file | origin | licence |
|---|---|---|
| hello.exe, bench.exe, threads.exe, pthreads.exe | built from emu/tests/*.c with `zig cc -target x86_64-windows-gnu` | MIT (this repo) |
| busybox64.exe | https://frippery.org/busybox/ (busybox-w32, FRP-6354) | GPL-2.0 — source at the link |
| plink.exe | https://www.chiark.greenend.org.uk/~sgtatham/putty/ (PuTTY 0.85, 64-bit) | MIT |

They are used by the x64 Runner app as ready-made test subjects and by the CI capability tests.

## Real applications in `public/x64/apps/` (unmodified official binaries)

| file | origin | licence |
|---|---|---|
| sqlite3.exe | https://sqlite.org/download.html — sqlite-tools-win-x64-3500400 (SQLite 3.50.4) | Public domain |
| jq.exe | https://github.com/jqlang/jq/releases/tag/jq-1.8.1 (jq-windows-amd64) | MIT |
| lua54.exe, lua54.dll | https://luabinaries.sourceforge.net/ — lua-5.4.2_Win64_bin | MIT |
| 7z.exe, 7z.dll | https://www.7-zip.org/ — 7-Zip 25.01 x64 | LGPL-2.1 + unRAR restriction, see 7-Zip-License.txt |
| zstd.exe | https://github.com/facebook/zstd/releases/tag/v1.5.7 (zstd-v1.5.7-win64) | BSD-3-Clause / GPL-2.0 |

demo.sql / demo.json / demo.lua / demo.txt are tiny inputs so each app has something to work on in the x64 Runner.
