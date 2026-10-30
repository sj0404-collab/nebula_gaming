# v1.7.0 — Nebula x64: real third‑party applications, DLL loader, x87

**Five unmodified, officially distributed Windows programs now run inside Nebula OS** (wasm64 + WebGL, also on Android) and ship as
one‑click samples in the x64 Runner (`public/x64/apps/`, licences in `public/x64/samples/README.md`):

| app | what was verified |
|---|---|
| SQLite 3.50.4 `sqlite3.exe` (MSVC) | `:memory:` queries; **file databases**: `create table`/2000 inserts/`create index`/`group by`/`pragma integrity_check` → `ok`; the db file persists on the virtual disk between runs |
| jq 1.8.1 `jq.exe` | filters, `select`, object construction, `-s`, `sqrt`, coloured and `-M` output |
| Lua 5.4.2 `lua54.exe` + **`lua54.dll`** | script execution, closures, `string.format`, `io.open`/`io.lines`, `os.date` |
| 7‑Zip 25.01 `7z.exe` + **`7z.dll`** (and `7za.exe`) | `a`/`x`/`l`/`i`/`b`: 7z (LZMA2) and zip round trips are bit‑exact; `7z i` lists 100+ formats/codecs from the DLL |
| Zstandard 1.5.7 `zstd.exe` (MSVC) | compress/decompress round trips, `-b1 -e3` benchmark (≈1 MB/s compress, 2.4 MB/s decompress in the emulator) |

What had to be built for that:

* **Private DLL loader** (`pe.c: pe_map/pe_load_dll`, `win32.c: mod_*`): DLLs imported statically or via `LoadLibrary(Ex)A/W` are found on the
  virtual disk (as given → next to the exe → `C:\nebula`), mapped at their preferred base when free (else relocated above `0x190000000`),
  their imports resolved recursively (system DLLs → shims, other private DLLs → exports; forwarders `KERNEL32.Foo` followed),
  `DllMain(base, DLL_PROCESS_ATTACH, 0)` run as a guest continuation before TLS callbacks/`main` (and synchronously inside `LoadLibrary`),
  exports by name or ordinal for `GetProcAddress`, `GetModuleHandle(Ex)A/W` (incl. `FROM_ADDRESS`), `GetModuleFileName` for module handles.
  Circular dependencies terminate (module registered before its imports are walked).
* **Static TLS per image**: every exe/DLL with a `.tls` directory gets its own slot in the per‑thread TLS pointer array (`TEB+0x58`),
  `_tls_index` is written into the image, blocks are created for all live threads at load time and for every thread created later
  (`emu/tests/tlsuse.c`: `_Thread_local` in a DLL used by 3 threads).
* **x87 FPU subset** (`emu/x64/x87.inc`): the register stack, `fld/fst/fild/fist(t)p/fbld`, arithmetic incl. reverse/pop forms,
  `fcom/fucom/fcomi`, `fxch`, `fchs/fabs/fsqrt/fscale/frndint/fprem/fsin/fcos/fptan/fpatan/fyl2x/f2xm1`, `fldcw/fnstcw/fnstsw`, `fnstenv/fldenv`,
  `fxsave/fxrstor` registers. Needed by `long double` printf (mingw), `strtold`, and the MSVC/UCRT math paths in SQLite/zstd.
  **Precision is 53‑bit** (host doubles), not 64‑bit — documented limit.
* **CPU**: SSE3 `movddup`/`movsldup`/`movshdup` (F2 0F 12 was executed as `movlps` — found by zstd), `popcnt`, `bsf/bsr`, `cmpxchg16b` paths exercised by MSVC CRT.
* **Win32 fixes found by the apps**: `ReadFile`/`WriteFile` honour `OVERLAPPED.Offset/OffsetHigh` (x64 layout, +16/+20 — SQLite's `pread`‑style
  VFS depended on it), `LockFileEx/UnlockFileEx`, `GetFileInformationByHandle`, `GetTempPath/GetFullPathName`, `FILE` EOF flags on every read path,
  `vfs_truncate` zero‑fill on growth, `GetConsoleMode`/`GetFileType` say *not a console* for redirected std handles (SQLite in a pipeline used to
  read from the console), `SetFileAttributes`, UTF‑8 → UTF‑16 for `W` strings, ~90 msvcrt/UCRT/kernel32 compat shims (`emu/x64/win32_compat.inc`).
* **Inspector**: imports from a program's own DLLs are reported separately (`privateDllImports`, "Own DLLs" row, verdict *partial* with the
  explanation) instead of being counted as missing.
* **x64 Runner**: real apps appear in the executable list with ready‑made default arguments and demo inputs (`demo.sql`, `demo.json`, `demo.lua`,
  `demo.txt`); all samples are also placed on the virtual disk so `busybox64.exe sh -c './7z.exe …'` pipelines work.
* Tests: `emu/tests/run.sh` 45 → 54 checks (`[9] private DLLs`, `[10] real applications`, `[11] x87`), e2e 27 → 28 scenarios (all five apps in Chromium).
* Honest limits: C++ exceptions / SEH unwinding are not implemented (7‑Zip's *error* paths that `throw` stop with a fault; the success paths are fine),
  no sockets/TLS, no GUI beyond GDI, DLL `DLL_THREAD_ATTACH/DETACH` and `PROCESS_DETACH` notifications are not delivered, interpreter speed ≈ 30 MIPS
  (7‑Zip `-mx5` on megabytes takes minutes — use `-mx1` or small inputs).

# v1.6.0 — Nebula x64: threads, synchronisation, a real heap

* **Threads** (`emu/x64/win32_threads.inc`): `CreateThread`/`_beginthread(ex)`/`_endthreadex`/`ExitThread` create real guest threads — own 1 MB
  stack, own TEB and static‑TLS block, thread id — that the emulator schedules round‑robin together with the process scheduler of v1.5
  (400 k‑instruction slices, a blocked thread never burns CPU). `GetCurrentThreadId`, `GetThreadId`, `OpenThread`, `SuspendThread`/`ResumeThread`
  (create‑suspended), `GetExitCodeThread`, `TerminateThread`, `Get/SetThreadPriority`, `GetThreadTimes`, `SwitchToThread`, `SetThreadDescription`,
  `QueueUserAPC` (+ alertable waits), `TlsAlloc/Get/Set` per thread, `__tls_index`/static TLS copies per thread.
* **Synchronisation with Windows semantics**: critical sections (recursive, `TryEnter`, spin count), SRW locks (shared/exclusive, `Try*`),
  condition variables (`SleepConditionVariableCS/SRW`, `Wake(All)ConditionVariable`), events (manual/auto reset, `PulseEvent`, direct hand‑off to one
  waiter), mutexes (recursive, abandoned → `WAIT_ABANDONED`, `ERROR_NOT_OWNER`), semaphores (`ERROR_TOO_MANY_POSTS`), waitable timers,
  `WaitForSingleObject(Ex)`/`WaitForMultipleObjects(Ex)` (wait‑any and wait‑all, timeouts → `WAIT_TIMEOUT`, thread/process handles as waitable),
  `SignalObjectAndWait`, `WaitOnAddress`/`WakeByAddressSingle/All`, `InitOnceExecuteOnce/BeginInitialize/Complete`, named objects shared between
  processes (`ERROR_ALREADY_EXISTS`), `Interlocked*` are honoured by the single‑core model. A deadlock of *all* threads of *all* processes is reported
  as a fault with the blocked objects, never a hang.
* **mingw winpthreads works unmodified**: `pthread_create/join/detach`, mutex, cond, rwlock, barrier, once, keys, `sched_yield`, spin locks
  (`emu/tests/pthreads.c`, 9 checks) — plus `_set_errno/_get_errno`, `GetSystemTimeAdjustment`, `SetProcessAffinityMask`, `SetSystemTime`,
  `mbstowcs/mbsrtowcs` so the import scan says *should run*.
* **Heap rewritten**: 16‑byte headers in guest memory + size‑class free lists → `malloc/free/realloc/HeapAlloc/HeapFree/HeapSize` are O(1)
  regardless of the number of live blocks (was an O(n) table capped at 65 536 blocks), and a block can no longer straddle two 16 MB heap regions
  (that was the real cause of busybox `sort -n` corrupting its line array at exactly 16 384 lines). `seq 1 100000 | sort -rn` now works.
* **Two CPU/ABI bugs found by the thread tests and fixed**: `punpcklqdq` (0F 6C) returned the *high* qwords (it broke clang's vectorised loops
  comparing four counters at once), and callback continuations (`qsort`/`bsearch` comparators, window procedures) leaked 32–48 bytes of guest
  stack per call — `qsort` of 10^6 elements overflowed the 8 MB stack. The qsort shim is now a bottom‑up merge sort (was insertion sort).
* UI: *N threads* badge in the x64 Runner status bar (`x64_thread_count` export), samples `threads.exe`/`pthreads.exe`, verdict text updated
  (threads/processes/pipes are emulated; GPU/audio/network are not).
* Tests: `emu/tests/run.sh` 40 → 45 checks (`[7] threads & synchronisation`, `[8] heap`), e2e 26 → 27 scenarios (threads in the browser).
* Known limits: a single wasm thread executes all guest threads (no parallel speed‑up, correct interleaving); `SuspendThread` of a running thread
  takes effect at the next slice boundary; kernel‑object handles have no generation counter (a stale handle re‑used after `CloseHandle` may address a new object).

# v1.5.0 — Nebula x64: processes and pipes

* **Process table + cooperative scheduler** (`emu/x64/api.c`): every `CreateProcessA/W` / `_spawnv(e)` / `_execv(e)` loads the target PE into a *new*
  address space (own `Mem`, `Cpu`, `Win32`, fd table) inside the same wasm module; up to 32 live processes are round‑robined by `x64_run` with
  blocking rules for stdin, child exit (`WaitForSingleObject(Ex)`, `WaitForMultipleObjects`, `_cwait`), pipe full/empty and `Sleep`.
  Root exit ends the session; a faulting child becomes exit code `0xC0000005` (busybox prints `SEGV`), never a host crash. Memory of exited
  processes goes back to a pool. A deadlock (everyone blocked on each other) is reported as a fault instead of a hang.
* **Anonymous pipes** (`CreatePipe`, `_pipe`, `PeekNamedPipe`, `GetFileType`=PIPE): 64 KB ring buffers; readers see EOF when the last writer closes,
  writers get `ERROR_NO_DATA`/`EPIPE` when the last reader is gone (`yes | head -1` terminates). Partial writes resume correctly after a block.
* **Handle inheritance** done the Windows way: `SECURITY_ATTRIBUTES.bInheritHandle`, `_O_NOINHERIT`, `SetHandleInformation/GetHandleInformation`,
  `DuplicateHandle`, `STARTF_USESTDHANDLES`; children get a copy of the parent's inheritable fd table at the same indexes (msvcrt semantics) plus
  the parent's environment; `_spawnve` honours `envp`. Closed std descriptors behave POSIX‑like (`close(0); open("/dev/null")` returns 0).
* **File mappings become kernel objects** that survive across processes (busybox‑w32 `--forkshell` passes a mapping handle to the child);
  views are flushed on `UnmapViewOfFile`/`FlushViewOfFile`/before spawning.
* **Process introspection** for `ps`/`kill`: `CreateToolhelp32Snapshot` + `Process32First/Next(W)` with correct x64 `PROCESSENTRY32` layout,
  `GetCurrentProcessId/_getpid`, `OpenProcess`, `GetProcessId`, `GetExitCodeProcess`, `TerminateProcess`, `GetProcessTimes` (real creation time),
  psapi `GetProcessImageFileNameA/W`, `QueryFullProcessImageNameA/W`, `EnumProcessModules(Ex)`, `GetModuleFileNameExA/W`, `GetModuleBaseNameA`,
  `ReadProcessMemory` (busybox `ps` reads the command line out of sibling processes), `IsWow64Process(2)`,
  `CreateRemoteThread(ExitProcess, code)` = how busybox `kill` delivers signals. A synthetic parent `explorer.exe` (pid 1000) keeps `getppid()!=1`.
* **CRT additions**: `sscanf` (was an unimplemented‑API stop), `_putenv` rebuilds `_environ` immediately (busybox `clearenv()` loops on it),
  `NUL`/`/dev/null`/`/dev/zero` devices, `strerror` for all CRT errnos (`Broken pipe`), MSVC command‑line quoting rules in the argv splitter.
* **Measured on busybox‑w32 `sh`** (native + wasm64 in Chromium): `echo hello | tr a-z A-Z`, `seq 1 5 | sort -rn | head -2`, `x=$(echo sub)`,
  nested `$( $( ) )`, `false; (exit 7); sh -c "exit 3"` exit codes, `sleep 1 & ps; wait`, `kill -9 $!` → 137, `yes | head`,
  `head -c 1000000 /dev/zero | wc -c` (busybox spawns `dd` for `/dev/zero`), `printf … | while read`, `xargs -I{} sh -c`, `FOO=baz sh -c`,
  stdin shared between parent and child, `echo payload | gzip -c | gunzip -c`, 40 sequential spawns in 0.46 s native.
* `x64run --trace` / `--trace2` print the scheduler decisions and per‑process API deltas (`X64_LOG=1`); `x64_proc_count` export and a
  “N processes” badge in the x64 Runner status bar.
* Tests: `emu/tests/run.sh` 26 → 40 checks (new section `[3c] processes + pipes`), e2e 25 → 26 scenarios (busybox pipelines/`ps` in the browser).
* Still missing (honest): threads, sockets, Direct3D/OpenGL/audio, dialog resources; a pipeline deeper than 32 stages fails with “unable to spawn shell”.

# v1.4.0 — Nebula x64: virtual disk, stdin, environment

* **Virtual file system** (`emu/x64/vfs.c`): an in‑memory `C:\` drive shared by every program run in the session (cwd `C:\nebula`, `C:\tmp`).
  Win32: `CreateFileA/W, ReadFile, WriteFile, CloseHandle, GetFileSize(Ex), SetFilePointer(Ex), SetEndOfFile, GetFileType, GetFileAttributes(Ex)A/W,
  GetFileInformationByHandle, FindFirstFile(Ex)A/W / FindNextFile / FindClose (wildcards), DeleteFile, CreateDirectory, RemoveDirectory, MoveFile(Ex), CopyFile,
  Get/SetCurrentDirectory, GetFullPathName, GetTempPath, GetTempFileName, CreateFileMapping/MapViewOfFile (copy semantics)`.
  CRT: `fopen/_wfopen/fopen_s/freopen/fclose/fread/fwrite/fgets/fgetc/getc/getchar/ungetc/fseek/_fseeki64/ftell/rewind/feof/clearerr/_fileno/_fdopen,
  _filbuf/_flsbuf (MinGW's inline getc/putc), _open/_wopen/_sopen_s/_close/_read/_write/_lseek(i64)/_tell/_eof/_chsize/_dup/_dup2, _stat64*/_fstat64*,
  _access/_unlink/remove/rename/_mkdir/_rmdir/_getcwd/_chdir/_fullpath, _findfirst64/_findnext64`.
  std handles can be redirected (`dup2` onto 0/1/2), so busybox `sh` redirections `>`, `>>`, `<`, `1>&2` work.
* **stdin**: blocking reads (`ReadFile`, `ReadConsoleA/W`, `ReadConsoleInputW`, `_read`, `fgets`, `getchar`, `_getch`, `gets_s`) suspend the guest until the
  console input line (Enter) or **EOF** button delivers data — no busy loop; the native `x64run` reads the real stdin (`--no-stdin` to force EOF).
* **Environment block** is dynamic now: `getenv/_wgetenv/getenv_s/_dupenv_s/_putenv/_putenv_s/GetEnvironmentVariable/SetEnvironmentVariable/
  ExpandEnvironmentStrings/GetEnvironmentStrings/_environ` all see the same table (`busybox env` shows what the program exported).
* **Fixed silent stubs** that returned 0: `strspn/strcspn/strpbrk/strtok/strtok_s/_strlwr/_strupr/_strrev/_itoa/_ltoa/_ultoa/_i64toa/strerror/strcpy_s/strcat_s/
  strncpy_s/memcpy_s/_mktemp`, `bsearch` with a guest comparator (was a hard fault), `setbuf/setvbuf`, Toolhelp `Process32First/Next`.
* **x64 Runner UI**: “Open folder…” mounts a whole directory under `C:\nebula\` (and lists the .exe files it contains), **Files** tab shows the virtual disk
  with *written* badges, download and delete, drag‑and‑drop of data files; console gets an input line + EOF; new status `WAITING-STDIN`.
* `x64run --mount <dir>[=C:\path]` copies a host folder into the VFS for headless tests; `emu/tests/run.sh` grew a VFS/stdin section (26 checks).
* Measured on busybox‑w32: `ls -l cat wc sort head tail grep sed awk cut find diff cmp cp tee xxd base64 md5sum sha256sum stat du mkdir sort -r (stdin) sh -c "for…; read x; [ -f ]; > <"` work.
* Still missing (honest): child processes / pipes (`sh` pipelines, `$(…)`, non‑builtin applets inside `sh` say “unable to spawn shell”), threads, sockets,
  Direct3D/OpenGL/audio, dialog resources — planned next: in‑emulator process table + anonymous pipes.
* e2e: 25 scenarios (6 for the emulator; the new one mounts a file, sorts it, feeds stdin to `sort -r`, writes `out.txt` with `tee`, reads it back, downloads it).

