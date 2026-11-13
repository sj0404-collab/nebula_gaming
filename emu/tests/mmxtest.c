/* MMX self-check through inline assembly (compilers lower __m64 intrinsics to SSE2 nowadays, so the mm registers are
 * driven directly): every no-prefix 0F MMX opcode, register and memory forms, the SSE-era extensions (pshufw, pinsrw,
 * pextrw, pmovmskb, pavg, pmin/pmax, psadbw, pmulhuw, maskmovq, movntq), cvtpi2ps/cvt(t)ps2pi, emms. The same
 * program on the host CPU (gcc, native) prints the same FNV-1a hash; run.sh compares against that constant. */
#include <stdio.h>
#include <string.h>
static unsigned long long h = 1469598103934665603ull;
static int verbose, step;
static void mix(const void *p, int n) { const unsigned char *b = p; for (int k = 0; k < n; k++) { h ^= b[k]; h *= 1099511628211ull; } if (verbose) { printf("%3d:", ++step); for (int k = 0; k < n; k++) printf(" %02x", b[k]); printf("\n"); } }
static unsigned long long A, B, R;
#define RR(op)  do { __asm__ volatile("movq %1, %%mm0\n\tmovq %2, %%mm1\n\t" op " %%mm1, %%mm0\n\tmovq %%mm0, %0" : "=m"(R) : "m"(A), "m"(B) : "mm0", "mm1", "memory"); mix(&R, 8); } while (0)
#define RM(op)  do { __asm__ volatile("movq %1, %%mm2\n\t" op " %2, %%mm2\n\tmovq %%mm2, %0" : "=m"(R) : "m"(A), "m"(B) : "mm2", "memory"); mix(&R, 8); } while (0)
#define RI(op, imm) do { __asm__ volatile("movq %1, %%mm3\n\t" op " $" #imm ", %%mm3\n\tmovq %%mm3, %0" : "=m"(R) : "m"(A) : "mm3", "memory"); mix(&R, 8); } while (0)
int main(int argc, char **argv) {
  verbose = argc > 1; unsigned rnd = 12345; unsigned char buf[16]; for (int k = 0; k < 16; k++) { rnd = rnd * 1103515245u + 12345u; buf[k] = (unsigned char)(rnd >> 16); }
  buf[3] = 0x80; buf[9] = 0x7f; buf[12] = 0xff; buf[13] = 0xff; memcpy(&A, buf, 8); memcpy(&B, buf + 8, 8);
  for (int pass = 0; pass < 2; pass++) {
    RR("paddb"); RR("paddw"); RR("paddd"); RR("paddq"); RR("psubb"); RR("psubw"); RR("psubd"); RR("psubq");
    RR("paddsb"); RR("paddsw"); RR("paddusb"); RR("paddusw"); RR("psubsb"); RR("psubsw"); RR("psubusb"); RR("psubusw");
    RR("pmullw"); RR("pmulhw"); RR("pmulhuw"); RR("pmaddwd"); RR("pmuludq"); RR("psadbw"); RR("pavgb"); RR("pavgw");
    RR("pminub"); RR("pmaxub"); RR("pminsw"); RR("pmaxsw"); RR("pand"); RR("pandn"); RR("por"); RR("pxor");
    RR("pcmpeqb"); RR("pcmpeqw"); RR("pcmpeqd"); RR("pcmpgtb"); RR("pcmpgtw"); RR("pcmpgtd");
    RR("punpcklbw"); RR("punpckhbw"); RR("punpcklwd"); RR("punpckhwd"); RR("punpckldq"); RR("punpckhdq");
    RR("packsswb"); RR("packssdw"); RR("packuswb");
    RR("psllw"); RR("psrlw"); RR("psraw"); RR("pslld"); RR("psrld"); RR("psrad"); RR("psllq"); RR("psrlq");
    RM("paddw"); RM("pmaddwd"); RM("punpckhbw"); RM("packuswb"); RM("pcmpgtw"); RM("pminub"); RM("movq"); RM("psrlq");
    RI("psllw", 3); RI("psrlw", 3); RI("psraw", 3); RI("pslld", 5); RI("psrld", 5); RI("psrad", 5); RI("psllq", 7); RI("psrlq", 7); RI("psraw", 20); RI("psrld", 40); RI("psllq", 70);
    __asm__ volatile("movq %1, %%mm3\n\tpshufw $0x1B, %%mm3, %%mm3\n\tmovq %%mm3, %0" : "=m"(R) : "m"(A) : "mm3", "memory"); mix(&R, 8);
    __asm__ volatile("pshufw $0xC6, %1, %%mm3\n\tmovq %%mm3, %0" : "=m"(R) : "m"(B) : "mm3", "memory"); mix(&R, 8);
    unsigned long long t = A; t ^= 0x5555aaaa12345678ull; A = B; B = t;   /* second pass with swapped/perturbed operands */
  }
  { unsigned ex, mk, lo; __asm__ volatile("movq %3, %%mm4\n\tpinsrw $2, %4, %%mm4\n\tmovq %%mm4, %0\n\tpextrw $3, %%mm4, %1\n\tpmovmskb %%mm4, %2" : "=m"(R), "=r"(ex), "=r"(mk) : "m"(B), "r"(0x1234) : "mm4", "memory"); mix(&R, 8); mix(&ex, 4); mix(&mk, 4);
    __asm__ volatile("movq %1, %%mm5\n\tmovd %%mm5, %0" : "=r"(lo) : "m"(B) : "mm5"); mix(&lo, 4);
    __asm__ volatile("movd %1, %%mm6\n\tmovq %%mm6, %0" : "=m"(R) : "r"(-7) : "mm6", "memory"); mix(&R, 8);
    __asm__ volatile("movd %1, %%mm6\n\tmovq %%mm6, %0" : "=m"(R) : "m"(lo) : "mm6", "memory"); mix(&R, 8);
    int d2 = 0x7eadbeef; __asm__ volatile("movq %1, %%mm7\n\tmovd %%mm7, %0" : "=m"(d2) : "m"(A) : "mm7", "memory"); mix(&d2, 4); }
  { float ff[4] = { 0, 0, 9.0f, 9.0f }; __asm__ volatile("movq %1, %%mm0\n\tmovups %0, %%xmm0\n\tcvtpi2ps %%mm0, %%xmm0\n\tmovups %%xmm0, %0" : "+m"(*(float (*)[4])ff) : "m"(A) : "mm0", "xmm0", "memory"); mix(ff, 16);
    float in[4] = { 1.5f, -2.5f, 3.7f, -4.2f }; __asm__ volatile("movups %1, %%xmm1\n\tcvtps2pi %%xmm1, %%mm1\n\tmovq %%mm1, %0" : "=m"(R) : "m"(*(float (*)[4])in) : "mm1", "xmm1", "memory"); mix(&R, 8);
    __asm__ volatile("movups %1, %%xmm1\n\tcvttps2pi %%xmm1, %%mm1\n\tmovq %%mm1, %0" : "=m"(R) : "m"(*(float (*)[4])in) : "mm1", "xmm1", "memory"); mix(&R, 8);
    __asm__ volatile("cvtps2pi %1, %%mm1\n\tmovq %%mm1, %0" : "=m"(R) : "m"(*(float (*)[4])in) : "mm1", "memory"); mix(&R, 8);
    double dd[2] = { 2.5, -1e9 }; __asm__ volatile("movupd %1, %%xmm2\n\tcvttpd2pi %%xmm2, %%mm2\n\tmovq %%mm2, %0" : "=m"(R) : "m"(*(double (*)[2])dd) : "mm2", "xmm2", "memory"); mix(&R, 8);
    __asm__ volatile("movq %1, %%mm3\n\tcvtpi2pd %%mm3, %%xmm3\n\tmovupd %%xmm3, %0" : "=m"(*(double (*)[2])dd) : "m"(A) : "mm3", "xmm3", "memory"); mix(dd, 16); }
  { unsigned char mm[8] = { 0 }; void *p = mm;
#if defined(__x86_64__)
    __asm__ volatile("movq %1, %%mm0\n\tmovq %2, %%mm1\n\tmaskmovq %%mm1, %%mm0" : : "D"(p), "m"(A), "m"(B) : "mm0", "mm1", "memory");
#else
    __asm__ volatile("movq %1, %%mm0\n\tmovq %2, %%mm1\n\tmaskmovq %%mm1, %%mm0" : : "D"(p), "m"(A), "m"(B) : "mm0", "mm1", "memory");
#endif
    mix(mm, 8);
    unsigned char st[16]; memset(st, 0xAA, 16); __asm__ volatile("movq %1, %%mm0\n\tmovntq %%mm0, %0" : "+m"(*(unsigned char (*)[16])st) : "m"(A) : "mm0", "memory"); mix(st, 16); }
  { unsigned char dq[16] = { 0 }; void *p = dq; unsigned char data[16], msk[16]; for (int k = 0; k < 16; k++) { data[k] = (unsigned char)(k * 17 + 1); msk[k] = (k & 1) ? 0x80 : 0x7f; }
    __asm__ volatile("movups %1, %%xmm0\n\tmovups %2, %%xmm1\n\tmaskmovdqu %%xmm1, %%xmm0" : : "D"(p), "m"(*(unsigned char (*)[16])data), "m"(*(unsigned char (*)[16])msk) : "xmm0", "xmm1", "memory"); mix(dq, 16); }   /* 66 0F F7 */
  __asm__ volatile("emms");
  volatile double fd = 1.0 / 3.0; double v = fd * 3.0; mix(&v, 8);   /* x87 still works after emms */
  printf("mmx hash %016llx\n", h); return 0;
}
