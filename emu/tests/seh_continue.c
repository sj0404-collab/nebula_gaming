#include <windows.h>
#include <stdio.h>
static volatile int hits;
static LONG CALLBACK veh(EXCEPTION_POINTERS *ep) {
  if (ep->ExceptionRecord->ExceptionCode == 0x406D1388) { hits++; return EXCEPTION_CONTINUE_EXECUTION; }
  return EXCEPTION_CONTINUE_SEARCH;
}
__attribute__((noinline)) static unsigned mix(unsigned a, unsigned b, unsigned c) { return a * 7 + b * 13 + c * 31; }
__attribute__((noinline)) static unsigned run(unsigned seed) {
  unsigned a = mix(seed, 1, 2), b = mix(seed, 3, 4), d = mix(seed, 5, 6);   /* live across the call → callee-saved registers */
  ULONG_PTR info[4] = { 0x1000, (ULONG_PTR)"worker", (ULONG_PTR)-1, 0 };
  size_t sp0, sp1;
#if defined(__i386__)
  __asm__ volatile("movl %%esp, %0" : "=r"(sp0));
#else
  __asm__ volatile("mov %%rsp, %0" : "=r"(sp0));
#endif
  RaiseException(0x406D1388, 0, 4, info);
#if defined(__i386__)
  __asm__ volatile("movl %%esp, %0" : "=r"(sp1));
#else
  __asm__ volatile("mov %%rsp, %0" : "=r"(sp1));
#endif
  printf("sp delta %ld\n", (long)(sp0 - sp1));
  return mix(a, b, d);
}
int main(void) {
  AddVectoredExceptionHandler(1, veh);
  unsigned r = run(42), want = mix(mix(42, 1, 2), mix(42, 3, 4), mix(42, 5, 6));
  printf("hits %d result %08x %s\n", hits, r, r == want ? "OK" : "MISMATCH");
  return r == want && hits == 1 ? 0 : 1;
}
