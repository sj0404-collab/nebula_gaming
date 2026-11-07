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
  printf("procmem: %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
