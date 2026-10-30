# Design review request: threads for the Nebula x64 emulator

Context. Nebula x64 is an x86-64 **interpreter** written in freestanding C, compiled to wasm64 (runs in the
browser / Android WebView) and natively for tests. It loads real Windows PE32+ executables (mingw-w64 and MSVC
builds: busybox-w32, PuTTY plink, our own test programs) and implements KERNEL32 / msvcrt / UCRT / USER32 /
GDI32 as "shims": every import slot is a 16-byte trampoline; a call lands in C code that reads guest memory.
There is **no host threading** (wasm, single JS thread). Version 1.5.0 added a process table: each process
has its own `Mem` (page regions), `Cpu` (registers, rip, rsp, fs/gs base) and `Win32` state (heap, handles,
fd table, GDI). A cooperative round-robin scheduler runs each process for a slice of N instructions; blocking
shims park the process (reason + object) and re-execute the shim when the condition is satisfied
("retry_and_yield": push rip, jump back to the trampoline).

Memory layout per process: image at ImageBase, main stack 8 MB at 0x10000000, heap from 0x20000000,
shim region 0x200000 (TEB at +0x10000, PEB at +0x14000), DIB sections from 0x70000000.
TEB fields set today: +0x08/+0x10 stack base/limit, +0x30 self, +0x40 pid, +0x48 tid, +0x58 ThreadLocalStoragePointer
(array with one entry: the module's static TLS block copied from the TLS directory, `_tls_index` = 0), +0x60 PEB,
+0x1480 TlsSlots[64]. gs_base = fs_base = TEB.

## Proposed thread design (please critique)

1. `Thread { Cpu cpu; u64 teb, stack_base, stack_size; int tid, state, exit_code; ThreadCtx ctx; }`, up to 64 per process.
   `ThreadCtx` holds what is per-thread in today's `Win32`: last_error, TlsSlots values, Fls values, the continuation
   stack used for guest callbacks (wndproc, qsort comparator, TLS callbacks, atexit), sleep deadline, block reason/object,
   the "partial pipe write" resume record, the `_spawn P_WAIT` record. On a thread switch the scheduler swaps these
   fields between `Win32` and the thread (memcpy of ~6 KB) and sets `w->cpu`.
2. Each new thread gets: its own 1 MB stack mapped at 0x60000000 + k·0x100000 (guard page below), its own TEB in the
   shim region (0x2000 bytes each) with self/stack/pid/tid/PEB fields, a fresh static-TLS block (copy of raw data +
   zero fill) referenced from its own ThreadLocalStoragePointer array, and `gs_base` pointing at that TEB.
3. `CreateThread(sa, stack, start, param, flags, &tid)`: thread starts at a tiny guest stub in the shim region that
   calls `start(param)` (rcx = param, 32-byte shadow space, 16-byte aligned rsp with return address pushed) and then
   calls the ExitThread shim with rax. `CREATE_SUSPENDED` respected; `ResumeThread/SuspendThread` keep a suspend count.
   `_beginthreadex/_beginthread/_endthreadex/_endthread` map onto the same path (handle returned as uintptr).
   Thread handles live in the per-process handle table (same table as files/pipes/process handles), so `CloseHandle`,
   `DuplicateHandle`, `WaitForSingleObject(hThread)` and `GetExitCodeThread` work uniformly; `GetCurrentThread()` = -2.
4. Scheduler: flat round robin over (process, thread) pairs per slice (so even spin-waits with Interlocked ops make
   progress — the interpreter executes `lock` prefixed instructions atomically by construction). Process exit
   (`ExitProcess`, last thread gone, unhandled fault in any thread) kills all its threads. `ExitThread` on the main
   thread keeps the process alive while other threads run (Windows semantics).
5. Sync objects as kernel objects in a table referenced by handle-table entries: Event (manual/auto reset),
   Mutex (owner tid, recursion count, abandoned), Semaphore (count/max), Thread, Process. `WaitForSingleObject(Ex)`
   and `WaitForMultipleObjects(Ex)` (wait-all / wait-any, timeout → WAIT_TIMEOUT 0x102, INFINITE) use one predicate
   `ready(obj)`; on success the side effects (reset auto event, take mutex, decrement semaphore) are applied for all
   objects atomically in the wait-all case. Blocking = park thread on {handles, all, deadline}.
6. CRITICAL_SECTION implemented on the real 40-byte guest structure: LockCount (+8), RecursionCount (+12),
   OwningThread (+16). Enter: free or owned-by-me → take (recursion++), else block on the address until OwningThread == 0.
   TryEnter, Leave (release at recursion 0). SRW locks on the 8-byte guest word: bit0 exclusive, upper bits shared count;
   Acquire blocks on the address. Condition variables: `SleepConditionVariableCS/SRW` release the lock, park on the
   CV address with a generation counter stored in the 8-byte CV word, `Wake/WakeAll` bump the generation; on wake the
   lock is re-acquired (blocking again if needed) before returning TRUE; timeout → FALSE with ERROR_TIMEOUT 1460.
   `WaitOnAddress/WakeByAddressSingle/All`: compare the value at the address with the expected buffer, park on the
   address; wake bumps a per-address generation. `Sleep(0)`/`SwitchToThread` yield the slice.
7. Interlocked* APIs that are imports (not intrinsics) — `InterlockedIncrement/Decrement/Exchange/CompareExchange/
   ExchangeAdd`, 64-bit variants — operate on guest memory directly (single-stepped interpreter, so trivially atomic).
8. UCRT/mingw expectations we believe matter: `_beginthreadex` return value is a real handle; `GetCurrentThreadId`
   must differ per thread; TLS callbacks with DLL_THREAD_ATTACH/DETACH (reason 2/3) are invoked for new threads;
   mingw-w64 winpthreads (`pthread_create`) uses `_beginthreadex`, `CreateEvent/SetEvent`, `CreateSemaphore`,
   `WaitForSingleObject`, Interlocked ops, `TlsAlloc`, SRW locks/condition variables when available via GetProcAddress;
   C11 `thrd_create` in mingw goes through the same. Our test exes will be built with `zig cc -target x86_64-windows-gnu`.

## Questions

A. What have we missed that mingw-w64 / UCRT thread start-up or shutdown needs (e.g. fields in the TEB that
   `__tls_get_addr`-free static TLS access on x64 uses, `_tls_index` handling, TEB +0x1480 vs the TlsExpansionSlots,
   stack-limit fields used by `__chkstk`, `NtCurrentTeb()->ThreadLocalStoragePointer` layout)?
B. Any pitfall in the CRITICAL_SECTION / SRW / condition-variable semantics as described (e.g. the fact that the real
   RTL_CRITICAL_SECTION.LockCount starts at -1 and encodes waiters; programs that read it directly; `DeleteCriticalSection`
   while owned; `SleepConditionVariableCS` spurious wake-ups; which error codes callers check)?
C. Scheduling: with slice-based preemption at ~400k instructions, which common patterns could deadlock or starve
   (e.g. a thread spinning on a flag while the setter is blocked on us, timeouts measured in wall time while the
   browser tab throttles requestAnimationFrame)? What would you change?
D. Is there anything about winpthreads' `pthread_mutex`/`pthread_cond` implementation (mingw-w64 13) that would make
   our events/semaphores insufficient — e.g. reliance on `NtWaitForKeyedEvent`, `WaitForMultipleObjects` on >64,
   APCs, `GetThreadContext`, `QueueUserAPC`?
E. Give a short list of the most valuable test cases (observable behaviour) for a threads test exe.

Please be concrete; where you are unsure say so. Keep it under 1200 words.
