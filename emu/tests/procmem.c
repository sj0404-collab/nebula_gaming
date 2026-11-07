/* Regression for WriteProcessMemory in both x86 and x64 guest processes. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

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
  printf("procmem: %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
