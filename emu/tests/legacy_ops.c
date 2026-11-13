/* legacy one-byte opcodes 32-bit code still uses: xlat, daa/das/aaa/aas/aam/aad, push/pop segment registers, mov sreg, bound, into, salc (x86); mov sreg + push/pop fs/gs (x64) */
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
int main(void) {
  unsigned char tab[256]; for (int i = 0; i < 256; i++) tab[i] = (unsigned char)(255 - i); unsigned ok = 0;
  for (unsigned v = 0; v < 256; v++) { unsigned char r; __asm__ volatile("xlat" : "=a"(r) : "a"((unsigned char)v), "b"(tab) : "memory"); if (r == tab[v]) ok++; } CHECK(ok == 256, "xlat %u", ok);
  unsigned short ds = 0, cs = 0, fs = 0, gs = 0; __asm__ volatile("mov %%ds, %0" : "=r"(ds)); __asm__ volatile("mov %%cs, %0" : "=r"(cs)); __asm__ volatile("mov %%fs, %0" : "=r"(fs)); __asm__ volatile("mov %%gs, %0" : "=r"(gs));
  printf("cs %04x ds %04x fs %04x gs %04x\n", cs, ds, fs, gs); CHECK(ds == 0x2b && fs == 0x53 && (cs == 0x23 || cs == 0x33), "selectors");
  unsigned short m = 0x1234; __asm__ volatile("mov %%ss, %0" : "=m"(m)); CHECK(m == 0x2b, "mov m16, ss %04x", m);
  unsigned long long pf = 0, pg = 0;
#if defined(__i386__)
  __asm__ volatile("push %%fs\n\tpop %0" : "=r"(pf)); __asm__ volatile("push %%gs\n\tpop %0" : "=r"(pg));
#else
  __asm__ volatile(".byte 0x0f, 0xa0\n\tpop %0" : "=r"(pf)); __asm__ volatile(".byte 0x0f, 0xa8\n\tpop %0" : "=r"(pg));   /* push fs / push gs are valid in 64-bit mode; the assembler just refuses the mnemonic */
#endif
  CHECK((pf & 0xffff) == 0x53, "push fs/pop %llx", pf); CHECK((pg & 0xffff) == 0x2b, "push gs/pop %llx", pg);
#if defined(__i386__)
  unsigned short es = 0x2b; __asm__ volatile("mov %0, %%es" : : "r"(es)); __asm__ volatile("mov %%es, %0" : "=r"(es)); CHECK(es == 0x2b, "mov es");
#endif
#if defined(__i386__)
  unsigned pd = 0; __asm__ volatile("push %%ds\n\tpop %0" : "=r"(pd)); CHECK(pd == 0x2b, "push ds/pop %x", pd);
  /* daa: 0x15 + 0x27 = 0x3c → 0x42 (BCD 15 + 27 = 42); 0x99 + 0x01 = 0x9a → 0x00 CF=1 */
  unsigned al, fl; __asm__ volatile("mov $0x15, %%al\n\tadd $0x27, %%al\n\tdaa\n\tpushf\n\tpop %1\n\tmovzbl %%al, %0" : "=r"(al), "=r"(fl) : : "eax", "cc"); CHECK(al == 0x42 && !(fl & 1), "daa %02x fl %x", al, fl);
  __asm__ volatile("mov $0x99, %%al\n\tadd $0x01, %%al\n\tdaa\n\tpushf\n\tpop %1\n\tmovzbl %%al, %0" : "=r"(al), "=r"(fl) : : "eax", "cc"); CHECK(al == 0x00 && (fl & 1) && (fl & 0x40), "daa carry %02x fl %x", al, fl);
  /* das: 0x42 - 0x27 = 0x1b → 0x15; 0x00 - 0x01 → 0x99 CF=1 */
  __asm__ volatile("mov $0x42, %%al\n\tsub $0x27, %%al\n\tdas\n\tpushf\n\tpop %1\n\tmovzbl %%al, %0" : "=r"(al), "=r"(fl) : : "eax", "cc"); CHECK(al == 0x15 && !(fl & 1), "das %02x fl %x", al, fl);
  __asm__ volatile("mov $0x00, %%al\n\tsub $0x01, %%al\n\tdas\n\tpushf\n\tpop %1\n\tmovzbl %%al, %0" : "=r"(al), "=r"(fl) : : "eax", "cc"); CHECK(al == 0x99 && (fl & 1), "das borrow %02x fl %x", al, fl);
  /* aaa: AX=0x0009 + 0x05 → 0x0e → 0x0104; aas: AX=0x0103 - 0x05 → 0x01fe → 0x0008 */
  unsigned ax; __asm__ volatile("mov $0x0009, %%ax\n\tadd $5, %%al\n\taaa\n\tmovzwl %%ax, %0" : "=r"(ax) : : "eax", "cc"); CHECK(ax == 0x0104, "aaa %04x", ax);
  __asm__ volatile("mov $0x0103, %%ax\n\tsub $5, %%al\n\taas\n\tmovzwl %%ax, %0" : "=r"(ax) : : "eax", "cc"); CHECK(ax == 0x0008, "aas %04x", ax);
  /* aam: AL=0x35 → AH=5 AL=3; aam 16: 0x35 → AH=3 AL=5; aad: AH=5 AL=3 → AL=53 (0x35) */
  __asm__ volatile("mov $0x35, %%al\n\taam\n\tmovzwl %%ax, %0" : "=r"(ax) : : "eax", "cc"); CHECK(ax == 0x0503, "aam %04x", ax);
  __asm__ volatile("mov $0x35, %%al\n\t.byte 0xd4, 0x10\n\tmovzwl %%ax, %0" : "=r"(ax) : : "eax", "cc"); CHECK(ax == 0x0305, "aam 16 %04x", ax);
  __asm__ volatile("mov $0x0503, %%ax\n\taad\n\tmovzwl %%ax, %0" : "=r"(ax) : : "eax", "cc"); CHECK(ax == 0x0035, "aad %04x", ax);
  __asm__ volatile("mov $0x0503, %%ax\n\t.byte 0xd5, 0x10\n\tmovzwl %%ax, %0" : "=r"(ax) : : "eax", "cc"); CHECK(ax == 0x0053, "aad 16 %04x", ax);
  /* salc: CF=1 → AL=0xff, CF=0 → 0 */
  __asm__ volatile("stc\n\t.byte 0xd6\n\tmovzbl %%al, %0" : "=r"(al) : : "eax", "cc"); CHECK(al == 0xff, "salc %02x", al);
  __asm__ volatile("clc\n\t.byte 0xd6\n\tmovzbl %%al, %0" : "=r"(al) : : "eax", "cc"); CHECK(al == 0x00, "salc clc %02x", al);
  /* bound inside the range: no trap; into without OF: no trap; opcode 0x82 alias (82 /0 ib = add r/m8, imm8) */
  int bounds[2] = { 10, 20 }; int idx = 15; __asm__ volatile("bound %0, %1" : : "r"(idx), "m"(bounds));
  __asm__ volatile("clc\n\tadd $0, %%eax\n\tinto" : : : "eax", "cc");
  unsigned char b8 = 5; __asm__ volatile(".byte 0x82, 0xc0, 0x07\n\tmovzbl %%al, %0" : "=r"(al) : "a"(b8) : "cc"); CHECK(al == 12, "0x82 alias %u", al);
#endif
  printf(fails ? "LEGACY FAILED %d\n" : "LEGACY OK\n", fails); return fails ? 1 : 0;
}
