#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
void sse_log_call(void *fn, double x, double *out);
static double frombits(uint64_t u) { double x; memcpy(&x, &u, sizeof x); return x; }
static uint64_t bits(double x) { uint64_t u; memcpy(&u, &x, sizeof u); return u; }
int main(void) {
  HMODULE m = LoadLibraryA("api-ms-win-crt-math-l1-1-0.dll");
  void *fn = m ? (void *)GetProcAddress(m, "_libm_sse2_log_precise") : NULL;
  if (!fn) { puts("sse-log: missing FAIL"); return 1; }
  double y = 99;
  sse_log_call(fn, 1.0, &y); if (y != 0.0) { puts("sse-log: log(1) FAIL"); return 2; }
  sse_log_call(fn, 2.0, &y); if (!(y > 0.69314718055994 && y < 0.69314718055996)) { puts("sse-log: log(2) FAIL"); return 3; }
  sse_log_call(fn, 0.0, &y); if (bits(y) != UINT64_C(0xfff0000000000000)) { puts("sse-log: log(0) FAIL"); return 4; }
  sse_log_call(fn, -1.0, &y); if ((bits(y) & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000) || !(bits(y) & UINT64_C(0x000fffffffffffff))) { puts("sse-log: log(-1) FAIL"); return 5; }
  sse_log_call(fn, frombits(UINT64_C(0x7ff0000000000000)), &y); if (bits(y) != UINT64_C(0x7ff0000000000000)) { puts("sse-log: log(inf) FAIL"); return 6; }
  puts("sse-log: PASS"); return 0;
}
