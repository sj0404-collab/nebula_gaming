/* xlat (0xD7): AL = [(e/r)bx + AL], with a segment override and a 0x67 address-size prefix; the loop shape MSVCR100's _ismbb* table lookups use */
#include <stdio.h>
#include <string.h>
int main(void) {
  unsigned char tab[256]; for (int i = 0; i < 256; i++) tab[i] = (unsigned char)(255 - i); tab[0x41] = 0x61;
  unsigned ok = 0; for (unsigned v = 0; v < 256; v++) { unsigned char al = (unsigned char)v, r;
    __asm__ volatile("xlat" : "=a"(r) : "a"(al), "b"(tab) : "memory"); if (r == tab[v]) ok++; }
  unsigned char r2 = 0, r3 = 0;
#if defined(__x86_64__)
  __asm__ volatile("addr32 xlat" : "=a"(r2) : "a"((unsigned char)0x41), "b"(tab) : "memory");   /* 67 D7: ebx-relative (the table is below 4 GB in the emulator) */
#else
  __asm__ volatile("xlat" : "=a"(r2) : "a"((unsigned char)0x41), "b"(tab) : "memory");
#endif
  __asm__ volatile("ds xlat" : "=a"(r3) : "a"((unsigned char)0x7f), "b"(tab) : "memory");       /* 3E D7: explicit segment prefix */
  printf("xlat %u/256 prefixed %02x %02x\n", ok, r2, r3);
  return ok == 256 && r2 == 0x61 && r3 == tab[0x7f] ? 0 : 1;
}
