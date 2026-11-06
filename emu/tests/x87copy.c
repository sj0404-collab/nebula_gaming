/* x87copy.c — the x87 copy idioms that must be bit-exact even though the emulator keeps ST(i) as 53-bit doubles:
 *   fild m64 → fistp m64 (Delphi's System.Move copies 8-byte blocks this way: a lossy FPU would turn "system" into "syttem"),
 *   fld m80 → fstp m80 (mingw long double passing), and the two-load/two-store overlap pattern of Move for 9..16 bytes.
 * Self-checking: prints "x87 roundtrip ok 3/3". Built by run.sh with zig cc for x64 and x86. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
int main(void) {
  const char *txt = "The system cannot find the file specified.\r\n";
  uint16_t w[64]; memset(w, 0, sizeof w); int n = (int)strlen(txt); for (int i = 0; i < n; i++) w[i] = (uint16_t)txt[i];
  uint64_t src[16], dst[16]; memcpy(src, w, sizeof src); memset(dst, 0, sizeof dst);
  for (int i = 0; i < 16; i++) __asm__ volatile("fildq %1\n\tfistpq %0" : "=m"(dst[i]) : "m"(src[i]));
  int ok1 = memcmp(src, dst, sizeof src) == 0;
  unsigned char a[16] = { 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0x00, 0x40, 0, 0, 0, 0, 0, 0 }, b[16]; memset(b, 0, sizeof b);
  __asm__ volatile("fldt %1\n\tfstpt %0" : "=m"(*(long double *)b) : "m"(*(long double *)a));
  int ok2 = memcmp(a, b, 10) == 0;
  uint64_t p[2] = { 0x0065006800740073ull, 0x0064006900660069ull }, q[2] = { 0, 0 };   /* two loads, fxch, two stores (overlapping tail copy) */
  __asm__ volatile("fildq %2\n\tfildq %3\n\tfxch\n\tfistpq %0\n\tfistpq %1" : "=m"(q[0]), "=m"(q[1]) : "m"(p[0]), "m"(p[1]));
  int ok3 = q[0] == p[0] && q[1] == p[1];
  printf("x87 roundtrip ok %d/3 (%d %d %d) %016llx\n", ok1 + ok2 + ok3, ok1, ok2, ok3, (unsigned long long)dst[1]);
  return 0;
}
