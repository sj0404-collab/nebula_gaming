#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
void sse_log_call(void *fn, double x, double *out);
void sse_binary_call(void *fn, double x, double y, double *out);
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
  fn = m ? (void *)GetProcAddress(m, "_libm_sse2_sqrt_precise") : NULL;
  if (!fn) { puts("sse-sqrt: missing FAIL"); return 7; }
  sse_log_call(fn, 4.0, &y); if (y != 2.0) { puts("sse-sqrt: sqrt(4) FAIL"); return 8; }
  sse_log_call(fn, -0.0, &y); if (bits(y) != UINT64_C(0x8000000000000000)) { puts("sse-sqrt: sqrt(-0) FAIL"); return 9; }
  sse_log_call(fn, -1.0, &y); if ((bits(y) & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000) || !(bits(y) & UINT64_C(0x000fffffffffffff))) { puts("sse-sqrt: sqrt(-1) FAIL"); return 10; }
  sse_log_call(fn, frombits(UINT64_C(0x7ff0000000000000)), &y); if (bits(y) != UINT64_C(0x7ff0000000000000)) { puts("sse-sqrt: sqrt(inf) FAIL"); return 11; }
  puts("sse-sqrt: PASS");
  struct { const char *name; double x, lo, hi; } unary[] = {
    {"_libm_sse2_atan_precise", 1.0, .78539816339744, .78539816339746},
    {"_libm_sse2_acos_precise", 0.0, 1.57079632679488, 1.57079632679491},
    {"_libm_sse2_asin_precise", 0.5, .52359877559828, .52359877559831},
    {"_libm_sse2_cos_precise", 0.0, .99999999999999, 1.00000000000001},
    {"_libm_sse2_sin_precise", 0.0, -1e-15, 1e-15},
    {"_libm_sse2_tan_precise", 0.0, -1e-15, 1e-15},
    {"_libm_sse2_exp_precise", 1.0, 2.71828182845903, 2.71828182845906},
    {"_libm_sse2_log10_precise", 100.0, 1.99999999999999, 2.00000000000001},
  };
  for (unsigned i = 0; i < sizeof unary / sizeof unary[0]; i++) {
    fn = (void *)GetProcAddress(m, unary[i].name);
    if (!fn) { printf("sse-extra: missing %s FAIL\n", unary[i].name); return 12; }
    sse_log_call(fn, unary[i].x, &y);
    if (!(y >= unary[i].lo && y <= unary[i].hi)) { printf("sse-extra: value %s FAIL\n", unary[i].name); return 13; }
  }
  fn = (void *)GetProcAddress(m, "_libm_sse2_pow_precise");
  if (!fn) { puts("sse-extra: pow missing FAIL"); return 14; }
  sse_binary_call(fn, 2.0, 3.0, &y); if (y != 8.0) { puts("sse-extra: pow XMM1 FAIL"); return 15; }
  sse_binary_call(fn, -2.0, 3.0, &y); if (y != -8.0) { puts("sse-extra: negative pow FAIL"); return 16; }
  sse_binary_call(fn, -1.0, 0.5, &y); if ((bits(y) & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000) || !(bits(y) & UINT64_C(0x000fffffffffffff))) { puts("sse-extra: pow NaN FAIL"); return 17; }
  fn = (void *)GetProcAddress(m, "_libm_sse2_atan2_precise");
  if (!fn) { puts("sse-extra: atan2 missing FAIL"); return 18; }
  sse_binary_call(fn, 1.0, 0.0, &y); if (!(y > 1.57079632679488 && y < 1.57079632679491)) { puts("sse-extra: atan2 argument order FAIL"); return 19; }
  fn = (void *)GetProcAddress(m, "_libm_sse2_atan_precise");
  sse_log_call(fn, -0.0, &y); if (bits(y) != UINT64_C(0x8000000000000000)) { puts("sse-extra: signed zero FAIL"); return 20; }
  sse_log_call(fn, frombits(UINT64_C(0x7ff0000000000000)), &y); if (!(y > 1.57079632679488 && y < 1.57079632679491)) { puts("sse-extra: infinity FAIL"); return 21; }
  puts("sse-extra: PASS");
  puts("sse-log: PASS"); return 0;
}
