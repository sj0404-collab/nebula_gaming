/* Exercise the real 32-bit Microsoft VC10 runtime, not a replacement shim. */
#include <windows.h>
#include <stdio.h>
typedef void *(__cdecl *allocfn)(size_t);
typedef void (__cdecl *freefn)(void *);
int main(void) {
  /* The C++ DLL depends on the C runtime: load it FIRST to check dependency initialisation. */
  HMODULE b=LoadLibraryA("msvcp100.dll"), a=LoadLibraryA("msvcr100.dll");
  if(!a || !b) { printf("VC10 LOAD FAIL %lu\n", (unsigned long)GetLastError()); return 1; }
  allocfn alloc=(allocfn)GetProcAddress(a,"_malloc_crt");
  freefn release=(freefn)GetProcAddress(a,"free");
  if(!alloc || !release) { puts("VC10 EXPORT FAIL"); return 2; }
  unsigned char *p=alloc(256); if(!p) { puts("VC10 ALLOC FAIL"); return 3; }
  for(int i=0;i<256;i++) p[i]=(unsigned char)i;
  for(int i=0;i<256;i++) if(p[i]!=(unsigned char)i) return 4;
  release(p); puts("VC10 PROBE PASS"); return 0;
}
