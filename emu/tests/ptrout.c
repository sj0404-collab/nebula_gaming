/* Pointer-sized out-parameters must be written with the guest pointer size:
   on x86 an 8-byte store into a BSTR* / HANDLE* / char** slot clobbers the
   neighbouring dword (this is exactly what broke PascalScript's variant stack
   in the GOG Inno installer: SysReAllocStringLen zeroed the next FType). */
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
struct S { void *slot; unsigned guard; };
static int bad = 0;
#define CHECK(s, what) do { if ((s).guard != 0xA5A5A5A5u) { printf("%s clobbered guard: %08x\n", what, (s).guard); bad = 1; } } while (0)
int main(void) {
  struct S s;
  s.slot = 0; s.guard = 0xA5A5A5A5u;
  if (!SysReAllocStringLen((BSTR *)&s.slot, L"Start", 5)) { printf("SysReAllocStringLen failed\n"); bad = 1; }
  if (!s.slot || SysStringLen((BSTR)s.slot) != 5 || wcscmp((BSTR)s.slot, L"Start") != 0) { printf("SysReAllocStringLen bad result\n"); bad = 1; }
  CHECK(s, "SysReAllocStringLen");
  if (!SysReAllocStringLen((BSTR *)&s.slot, L"Longer text", 11)) { printf("SysReAllocStringLen#2 failed\n"); bad = 1; }
  if (!s.slot || SysStringLen((BSTR)s.slot) != 11) { printf("SysReAllocStringLen#2 bad result\n"); bad = 1; }
  CHECK(s, "SysReAllocStringLen#2");
  SysFreeString((BSTR)s.slot);
  struct S r, w; r.slot = w.slot = 0; r.guard = w.guard = 0xA5A5A5A5u;
  if (!CreatePipe((HANDLE *)&r.slot, (HANDLE *)&w.slot, NULL, 0)) { printf("CreatePipe failed\n"); bad = 1; }
  CHECK(r, "CreatePipe read"); CHECK(w, "CreatePipe write");
  struct S d; d.slot = 0; d.guard = 0xA5A5A5A5u;
  if (!DuplicateHandle(GetCurrentProcess(), r.slot, GetCurrentProcess(), (HANDLE *)&d.slot, 0, FALSE, DUPLICATE_SAME_ACCESS)) { printf("DuplicateHandle failed\n"); bad = 1; }
  CHECK(d, "DuplicateHandle");
  CloseHandle(d.slot); CloseHandle(r.slot); CloseHandle(w.slot);
  struct S lo, hi; lo.slot = hi.slot = 0; lo.guard = hi.guard = 0xA5A5A5A5u;
  void (WINAPI *pLim)(ULONG_PTR *, ULONG_PTR *) = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetCurrentThreadStackLimits");
  if (!pLim) { printf("no GetCurrentThreadStackLimits\n"); bad = 1; } else { pLim((ULONG_PTR *)&lo.slot, (ULONG_PTR *)&hi.slot);
  if (!(lo.slot < hi.slot)) { printf("GetCurrentThreadStackLimits bad: %p %p\n", lo.slot, hi.slot); bad = 1; } }
  CHECK(lo, "GetCurrentThreadStackLimits lo"); CHECK(hi, "GetCurrentThreadStackLimits hi");
  struct S hm; hm.slot = 0; hm.guard = 0xA5A5A5A5u; DWORD need = 0;
  HMODULE (WINAPI *pEnum)(HANDLE, HMODULE *, DWORD, LPDWORD) = (void *)GetProcAddress(LoadLibraryA("psapi.dll"), "EnumProcessModules");
  if (!pEnum) { printf("no EnumProcessModules\n"); bad = 1; }
  if (pEnum) { pEnum(GetCurrentProcess(), (HMODULE *)&hm.slot, sizeof(HMODULE), &need); if (hm.slot != (void *)GetModuleHandleA(NULL) || need != sizeof(HMODULE)) { printf("EnumProcessModules bad: %p need=%lu\n", hm.slot, (unsigned long)need); bad = 1; } CHECK(hm, "EnumProcessModules"); }
  char buf[64]; struct S ctx; ctx.slot = 0; ctx.guard = 0xA5A5A5A5u; strcpy(buf, "a,b,c");
  char *t = strtok_s(buf, ",", (char **)&ctx.slot); if (!t || strcmp(t, "a")) { printf("strtok_s bad\n"); bad = 1; }
  t = strtok_s(NULL, ",", (char **)&ctx.slot); if (!t || strcmp(t, "b")) { printf("strtok_s#2 bad\n"); bad = 1; }
  CHECK(ctx, "strtok_s");
  /* pseudo-handles: a 32-bit caller passes GetCurrentProcess()/GetCurrentThread() as 0xFFFFFFFF/0xFFFFFFFE */
  if (GetProcessId(GetCurrentProcess()) != GetCurrentProcessId()) { printf("GetProcessId(GetCurrentProcess()) bad\n"); bad = 1; }
  { DWORD code = 7; if (!GetExitCodeProcess(GetCurrentProcess(), &code) || code != STILL_ACTIVE) { printf("GetExitCodeProcess(self) bad: %lu\n", (unsigned long)code); bad = 1; } }
  if (WaitForSingleObject(GetCurrentProcess(), 0) != WAIT_TIMEOUT) { printf("WaitForSingleObject(self) bad\n"); bad = 1; }
  if (GetThreadId(GetCurrentThread()) != GetCurrentThreadId()) { printf("GetThreadId(GetCurrentThread()) bad\n"); bad = 1; }
  printf(bad ? "PTROUT FAIL\n" : "PTROUT OK\n");
  return bad;
}
