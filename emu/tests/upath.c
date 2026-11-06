/* upath.c — a program whose own file name is Cyrillic with spaces: every W path API must return UTF-16 unit counts that match
   the string (no junk after the NUL), WideCharToMultiByte(CP_ACP) must round-trip, and the exe must be able to reopen itself. */
#include <windows.h>
#include <stdio.h>
static int fails = 0;
static void expect(const char *what, int ok) { if (!ok) { printf("FAIL %s\n", what); fails++; } }
int main(void) {
  WCHAR me[MAX_PATH]; DWORD n = GetModuleFileNameW(0, me, MAX_PATH); expect("GetModuleFileNameW len", n == lstrlenW(me));
  int cyr = 0; for (DWORD i = 0; i < n; i++) if (me[i] >= 0x400 && me[i] < 0x500) cyr++; expect("name has Cyrillic", cyr > 0);
  WCHAR full[MAX_PATH]; WCHAR *fp = 0; DWORD fl = GetFullPathNameW(me, MAX_PATH, full, &fp); expect("GetFullPathNameW len", fl == (DWORD)lstrlenW(full)); expect("GetFullPathNameW filepart", fp && fp > full && fp[-1] == L'\\' && wcschr(fp, L'\\') == 0);
  WCHAR lng[MAX_PATH]; DWORD ll = GetLongPathNameW(me, lng, MAX_PATH); expect("GetLongPathNameW len", ll == (DWORD)lstrlenW(lng) && ll == n);
  WCHAR sp[MAX_PATH]; WCHAR *spp = 0; DWORD sl = SearchPathW(0, fp, 0, MAX_PATH, sp, &spp); expect("SearchPathW len", sl == (DWORD)lstrlenW(sp) && sl == n); expect("SearchPathW filepart", spp && lstrcmpW(spp, fp) == 0);
  char a8[MAX_PATH * 3]; int al = WideCharToMultiByte(CP_ACP, 0, me, -1, a8, sizeof a8, 0, 0); int need = WideCharToMultiByte(CP_ACP, 0, me, -1, 0, 0, 0, 0); expect("WideCharToMultiByte size query == written", al == need && al > (int)n);
  WCHAR back[MAX_PATH]; int bl = MultiByteToWideChar(CP_ACP, 0, a8, -1, back, MAX_PATH); expect("CP_ACP round trip", bl == (int)n + 1 && lstrcmpW(back, me) == 0);
  char a2[MAX_PATH]; DWORD an = GetModuleFileNameA(0, a2, MAX_PATH); expect("GetModuleFileNameA == WideCharToMultiByte(CP_ACP)", an == strlen(a2) && strcmp(a2, a8) == 0);
  HANDLE h = CreateFileW(me, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0); expect("CreateFileW(self)", h != INVALID_HANDLE_VALUE); if (h != INVALID_HANDLE_VALUE) { DWORD sz = GetFileSize(h, 0); expect("file size", sz > 1000); CloseHandle(h); }
  h = CreateFileA(a2, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0); expect("CreateFileA(self)", h != INVALID_HANDLE_VALUE); if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
  WCHAR dir[MAX_PATH]; lstrcpyW(dir, me); *wcsrchr(dir, L'\\') = 0; expect("SetCurrentDirectoryW", SetCurrentDirectoryW(dir)); WCHAR cwd[MAX_PATH]; DWORD cl = GetCurrentDirectoryW(MAX_PATH, cwd); expect("GetCurrentDirectoryW len", cl == (DWORD)lstrlenW(cwd) && lstrcmpiW(cwd, dir) == 0);
  WCHAR env[MAX_PATH]; SetEnvironmentVariableW(L"UPATH", me); DWORD el = GetEnvironmentVariableW(L"UPATH", env, MAX_PATH); expect("GetEnvironmentVariableW len", el == n && lstrcmpW(env, me) == 0);
  WCHAR ex[MAX_PATH]; DWORD xl = ExpandEnvironmentStringsW(L"%UPATH%", ex, MAX_PATH); expect("ExpandEnvironmentStringsW len", xl == n + 1 && lstrcmpW(ex, me) == 0);
  WIN32_FIND_DATAW fd; HANDLE f = FindFirstFileW(me, &fd); expect("FindFirstFileW", f != INVALID_HANDLE_VALUE && lstrcmpW(fd.cFileName, fp) == 0); if (f != INVALID_HANDLE_VALUE) FindClose(f);
  printf(fails ? "UPATH FAILED %d\n" : "UPATH OK\n", fails); return fails ? 1 : 0;
}
