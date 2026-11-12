#include <windows.h>
#include <stdio.h>
static volatile LONG fired, once;
static void CALLBACK cb(PVOID p, BOOLEAN timerOrWait) { if (timerOrWait) InterlockedIncrement((LONG *)p); }
int main(void) {
  HANDLE tq = CreateTimerQueue(), t1 = NULL, t2 = NULL;
  if (!CreateTimerQueueTimer(&t1, tq, cb, (PVOID)&fired, 20, 30, 0)) { printf("CreateTimerQueueTimer periodic failed %lu\n", GetLastError()); return 1; }
  if (!CreateTimerQueueTimer(&t2, NULL, cb, (PVOID)&once, 10, 0, WT_EXECUTEONLYONCE)) { printf("CreateTimerQueueTimer once failed %lu\n", GetLastError()); return 1; }
  Sleep(400);
  LONG f = fired, o = once;
  DeleteTimerQueueTimer(tq, t1, INVALID_HANDLE_VALUE);
  DeleteTimerQueueTimer(NULL, t2, INVALID_HANDLE_VALUE);
  Sleep(150); LONG f2 = fired;
  DeleteTimerQueueEx(tq, INVALID_HANDLE_VALUE);
  printf("periodic %ld (after delete %ld) once %ld\n", (long)f, (long)f2, (long)o);
  ULONG num = 0, size = 0; wchar_t buf[64];
  BOOL q = GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, NULL, &size);
  size = 64; BOOL g = GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, buf, &size);
  printf("ui lang %d %d num %lu size %lu '%ls'\n", q, g, num, size, buf);
  SYSTEMTIME st; GetLocalTime(&st); BOOL s = SetLocalTime(&st); printf("SetLocalTime %d err %lu\n", s, GetLastError());
  int ok = f >= 8 && f <= 14 && f2 == f && o == 1 && q && g && num == 1 && size == 7 && buf[0] == 'e' && !s;
  printf(ok ? "OK\n" : "MISMATCH\n"); return ok ? 0 : 1;
}
