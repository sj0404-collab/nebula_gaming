/* Regression for WriteProcessMemory in both x86 and x64 guest processes. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int count_a, count_w;
static BOOL CALLBACK locale_a(LPSTR name) { if (!strcmp(name, "00000409")) count_a++; return FALSE; }
static BOOL CALLBACK locale_w(LPWSTR name) { if (!wcscmp(name, L"00000409")) count_w++; return FALSE; }

int main(void) {
  char target[16] = "original";
  const char data[] = "changed";
  SIZE_T count = 0, guard = (SIZE_T)0x12345678;
  struct { SIZE_T n; SIZE_T guard; } result = { 0, guard };
  int ok = WriteProcessMemory(GetCurrentProcess(), target, data, sizeof(data), &result.n)
    && !strcmp(target, data) && result.n == sizeof(data) && result.guard == guard;
  char output[16] = {0};
  ok = ok && ReadProcessMemory(GetCurrentProcess(), target, output, sizeof(data), &count)
    && !strcmp(output, data) && count == sizeof(data);
  result.n = 99; result.guard = guard;
  BOOL invalid = WriteProcessMemory((HANDLE)(ULONG_PTR)0x123456, target, data, sizeof(data), &result.n);
  ok = ok && !invalid && result.guard == guard;
  ok = ok && EnumSystemLocalesA(locale_a, 0) && EnumSystemLocalesW(locale_w, 0) && count_a == 1 && count_w == 1;
  typedef LPWSTR (WINAPI *StrCpyWFn)(LPWSTR, LPCWSTR);
  HMODULE shlwapi = LoadLibraryW(L"shlwapi.dll");
  StrCpyWFn copy_w = shlwapi ? (StrCpyWFn)GetProcAddress(shlwapi, "StrCpyW") : NULL;
  WCHAR wbuf[32] = {0};
  ok = ok && copy_w && copy_w(wbuf, L"Clover Ω") == wbuf && !wcscmp(wbuf, L"Clover Ω");
  BYTE saved[] = {0, 1, 42, 128, 255}, restored[5] = {0};
  ok = ok && WritePrivateProfileStructW(L"binary", L"key", saved, sizeof(saved), L"C:\\test-profile.ini")
    && GetPrivateProfileStructW(L"binary", L"key", restored, sizeof(restored), L"C:\\test-profile.ini")
    && !memcmp(saved, restored, sizeof(saved));
  ok = ok && !GetPrivateProfileStructW(L"binary", L"missing", restored, sizeof(restored), L"C:\\test-profile.ini");
  printf("procmem: %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
