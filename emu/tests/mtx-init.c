#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef void (__cdecl *init_fn)(void *, int);
int main(void) {
  HMODULE h = LoadLibraryA("msvcp140.dll");
  init_fn init = h ? (init_fn)GetProcAddress(h, "_Mtx_init_in_situ") : NULL;
  if (!init) { puts("mtx-init: missing FAIL"); return 1; }
  struct { uint32_t before; union { uint64_t align; unsigned char bytes[80]; } m; uint32_t after; } test;
  memset(&test, 0xa5, sizeof test); test.before = 0x12345678; test.after = 0x87654321;
  init(test.m.bytes, 0x102);
  uint32_t type, tid, count; memcpy(&type, test.m.bytes, 4);
  unsigned tid_off = sizeof(void*) == 4 ? 40 : 72;
  memcpy(&tid, test.m.bytes + tid_off, 4); memcpy(&count, test.m.bytes + tid_off + 4, 4);
  if (type != 0x102 || tid != 0xffffffffu || count != 0) { puts("mtx-init: fields FAIL"); return 2; }
  unsigned cs = sizeof(void*) == 4 ? 4 : 8;
  for (unsigned i=0; i<2*sizeof(void*); i++) if (test.m.bytes[cs+i]) { puts("mtx-init: SRW state FAIL"); return 3; }
  if (test.before != 0x12345678 || test.after != 0x87654321) { puts("mtx-init: guard FAIL"); return 4; }
  if (sizeof(void*) == 4) for (unsigned i=48; i<80; i++) if (test.m.bytes[i] != 0xa5) { puts("mtx-init: x86 bounds FAIL"); return 5; }
  puts("mtx-init: PASS"); return 0;
}
