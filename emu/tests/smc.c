#include <windows.h>
#include <stdio.h>
#include <stdint.h>
typedef uint32_t (*fn_t)(void);
static uint32_t h(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
int main(void) {
  unsigned char *p = VirtualAlloc(0, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  uint32_t acc = 0; unsigned char *p2 = p + 128, *p3 = p + 256;
  /* 2: hot callee, patched rarely (every 4096 calls) — must see the new code right after the write */
  acc = 0; p2[0] = 0xB8; *(uint32_t *)(p2 + 1) = 7; p2[5] = 0xC3;
  for (int i = 0; i < 400000; i++) { if ((i & 4095) == 0) *(uint32_t *)(p2 + 1) = h(i); acc += ((fn_t)p2)() ^ (uint32_t)i; }
  printf("smc2 %08x\n", acc);
  /* 3: data next to code on the same page (written constantly) — must not make the code stale */
  volatile uint32_t *d = (volatile uint32_t *)(p + 2048); acc = 0; p3[0] = 0xB8; *(uint32_t *)(p3 + 1) = 0x1234; p3[5] = 0xC3;
  for (int i = 0; i < 400000; i++) { d[i & 63] = (uint32_t)i; acc += ((fn_t)p3)() + d[(i * 7) & 63]; }
  printf("smc3 %08x\n", acc);
  /* 1: patch the immediate of a tiny function on every call */
  p[0] = 0xB8; p[5] = 0xC3; acc = 0;
  for (int i = 0; i < 50000; i++) { *(uint32_t *)(p + 1) = h(i); acc += ((fn_t)p)(); }
  printf("smc1 %08x\n", acc);
  /* 4: two functions on the same page, one rewritten by a memcpy of the other's bytes */
  unsigned char *q = p + 64; q[0] = 0xB8; *(uint32_t *)(q + 1) = 99; q[5] = 0xC3; acc = 0;
  for (int i = 0; i < 20000; i++) { *(uint32_t *)(p + 1) = h(i); memcpy(q, p, 6); acc += ((fn_t)q)(); }
  printf("smc4 %08x\n", acc);
  return 0;
}
