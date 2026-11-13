/* x87diff.c — prints every intermediate of the jitdiff x87 section (raw bits) so a checksum mismatch between hosts can be located: build like jitdiff.c and diff the outputs of x64run and wasm_run.mjs */
/* jitdiff.c — differential workload for the x86→wasm JIT: the same deterministic float/int/SSE computation is run hot
 * (so every loop body is translated) and printed as checksums; the output under `wasm_run.mjs --jit 1` must be identical
 * to `--jit 0` (interpreter) and to the native runner.  Patterns are the ones HotSpot / .NET / MSVC emit for scalar float
 * code: movss/movsd loads and stores, cvtsi2ss/sd from registers and memory, cvttss2si/cvtss2si, cvtss2sd/cvtsd2ss,
 * mulss/addss/subss/divss, sqrtss, minss/maxss, ucomiss/comiss + branches (incl. NaN), xorps/andps sign tricks,
 * movd/movq between GPRs and XMM, packed addps/mulps on 16-byte data, pxor/por/pand, 32-bit ops that must zero-extend.
 *   zig cc -target x86_64-windows-gnu -O2 -o jitdiff.exe jitdiff.c   (and -target x86-windows-gnu for the 32-bit variant)
 *   node emu/tests/wasm_run.mjs --jit 0 jitdiff.exe > a.txt; node emu/tests/wasm_run.mjs --jit 1 jitdiff.exe > b.txt; cmp a.txt b.txt */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

static uint32_t rng = 0x12345678u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint32_t h32(uint32_t h, uint32_t v) { h ^= v; h *= 0x01000193u; return h; }
static uint32_t hf(uint32_t h, float f) { uint32_t u; memcpy(&u, &f, 4); return h32(h, u); }
static uint32_t hd(uint32_t h, double d) { uint64_t u; memcpy(&u, &d, 8); return h32(h32(h, (uint32_t)u), (uint32_t)(u >> 32)); }

#define N 4096
static volatile float   fa[N], fb[N];
static volatile int32_t ia[N];
static volatile int64_t la[N];
static volatile double  da[N];
static float out[N];

static unsigned long long B(double d) { unsigned long long u; memcpy(&u, &d, 8); return u; }
static uint32_t t_x87(void) {
  uint32_t h = 2166136261u;
  static volatile double dd[64]; static volatile float ff[64]; static volatile int32_t ii[64]; static volatile int16_t ss[64]; static volatile int64_t ll[64];
  for (int i = 0; i < 64; i++) { dd[i] = (double)(int32_t)rnd() / 977.0; ff[i] = (float)(int32_t)rnd() / 31.0f; ii[i] = (int32_t)rnd() >> 8; ss[i] = (int16_t)rnd(); ll[i] = (int64_t)((uint64_t)rnd() << 20) * (rnd() & 1 ? 1 : -1); }
  for (int rep = 0; rep < 300; rep++) {
    for (int i = 0; i < 64; i++) {
      double r0, r1, r2, r3; float r4; int32_t j0, j1; int16_t j2; int64_t j3; uint16_t sw0 = 0, sw1 = 0, cw = 0; size_t fl0 = 0, fl1 = 0;
      int k = (i * 5 + rep) & 63;
      /* memory arithmetic + stack shuffles */
      __asm__ volatile("fldl %1\n\tfaddl %2\n\tfmuls %3\n\tfsubrl %1\n\tfdivl %4\n\tfchs\n\tfabs\n\tfstpl %0" : "=m"(r0) : "m"(dd[i]), "m"(dd[k]), "m"(ff[i]), "m"(dd[(i + 1) & 63]) : "memory");
      __asm__ volatile("fldl %1\n\tfldl %2\n\tfxch\n\tfld %%st(1)\n\tfadd %%st(1), %%st\n\tfmulp %%st, %%st(2)\n\tfsubrp %%st, %%st(1)\n\tfstpl %0" : "=m"(r1) : "m"(dd[k]), "m"(dd[i]) : "memory");
      __asm__ volatile("fildl %1\n\tfilds %2\n\tfaddp\n\tfildq %3\n\tfdivrp %%st, %%st(1)\n\tfld1\n\tfldz\n\tfsubp\n\tfaddp\n\tfstpl %0" : "=m"(r2) : "m"(ii[i]), "m"(ss[k]), "m"(ll[i]) : "memory");
      /* register-form divide and reverse subtract, st(i) destination forms, fst st(i) */
      __asm__ volatile("fldl %1\n\tfldl %2\n\tfldl %3\n\tfdiv %%st(2), %%st\n\tfsubr %%st, %%st(1)\n\tfst %%st(2)\n\tfdivrp %%st, %%st(2)\n\tfaddp\n\tfstpl %0" : "=m"(r3) : "m"(dd[i]), "m"(dd[k]), "m"(dd[(k + 7) & 63]) : "memory");
      /* stores: m32, fistp m32/m16/m64 (round to nearest default), fsqrt, frndint */
      __asm__ volatile("flds %1\n\tfabs\n\tfsqrt\n\tfmuls %2\n\tfstps %0" : "=m"(r4) : "m"(ff[i]), "m"(ff[k]) : "memory");
      __asm__ volatile("fldl %1\n\tfistpl %0" : "=m"(j0) : "m"(dd[i]) : "memory");
      __asm__ volatile("fldl %1\n\tfrndint\n\tfistpl %0" : "=m"(j1) : "m"(dd[k]) : "memory");
      __asm__ volatile("flds %1\n\tfistps %0" : "=m"(j2) : "m"(ff[(i + 3) & 63]) : "memory");
      __asm__ volatile("fldl %1\n\tfistpq %0" : "=m"(j3) : "m"(dd[(i + 9) & 63]) : "memory");
      /* compares: fcomp + fnstsw ax, fucomip/fcomip → flags (NaN-free data: C3/C2/C0 and ZF/PF/CF) */
      __asm__ volatile("fldl %1\n\tfcompl %2\n\tfnstsw %%ax" : "=a"(sw0) : "m"(dd[i]), "m"(dd[k]) : "memory");
      __asm__ volatile("fldl %1\n\tfldl %2\n\tfcompp\n\tfnstsw %%ax" : "=a"(sw1) : "m"(dd[k]), "m"(dd[i]) : "memory");
      __asm__ volatile("fldl %1\n\tfldl %2\n\tfucomip %%st(1), %%st\n\tfstp %%st(0)\n\tpushf\n\tpop %0" : "=r"(fl0) : "m"(dd[i]), "m"(dd[k]) : "cc", "memory");
      __asm__ volatile("fldl %1\n\tfldl %2\n\tfcomip %%st(1), %%st\n\tfstp %%st(0)\n\tpushf\n\tpop %0" : "=r"(fl1) : "m"(dd[k]), "m"(dd[i]) : "cc", "memory");
      /* control word: truncate, then restore */
      __asm__ volatile("fnstcw %0" : "=m"(cw) :: "memory");
      { uint16_t cw2 = cw | 0x0c00; int32_t t0, t1; if (rep < 2) printf("cw=%04x ", cw); __asm__ volatile("fldcw %2\n\tfldl %3\n\tfistpl %0\n\tfldcw %4\n\tfldl %3\n\tfistpl %1" : "=m"(t0), "=m"(t1) : "m"(cw2), "m"(dd[i]), "m"(cw) : "memory"); h = h32(h32(h, (uint32_t)t0), (uint32_t)t1); }
      if (rep < 2) printf("%d %d r0=%llx r1=%llx r2=%llx r3=%llx r4=%llx j0=%d j1=%d j2=%d j3=%lld sw0=%04x sw1=%04x fl0=%02x fl1=%02x\n", rep, i, B(r0), B(r1), B(r2), B(r3), B((double)r4), j0, j1, j2, (long long)j3, sw0 & 0x4500, sw1 & 0x4500, (unsigned)(fl0 & 0x45), (unsigned)(fl1 & 0x45));
      h = hd(h, r0); h = hd(h, r1); h = hd(h, r2); h = hd(h, r3); h = hf(h, r4); h = h32(h, (uint32_t)j0); h = h32(h, (uint32_t)j1); h = h32(h, (uint16_t)j2); h = h32(h32(h, (uint32_t)j3), (uint32_t)((uint64_t)j3 >> 32));
      h = h32(h, (sw0 & 0x4500) | ((sw1 & 0x4500) << 1)); h = h32(h, (uint32_t)((fl0 & 0x45) | ((fl1 & 0x45) << 8)));
    }
    /* exact scaling forms through the helper path (fsin/fyl2x are libm-dependent across native builds and stay out of the checksum) */
    { double a = dd[rep & 63], b = dd[(rep * 3) & 63], o1, o2, ex = (double)((rep & 15) - 8);
      __asm__ volatile("fldl %1\n\tfldl %2\n\tfprem\n\tfstp %%st(1)\n\tfstpl %0" : "=m"(o1) : "m"(a), "m"(b) : "memory");
      __asm__ volatile("fldl %1\n\tfldl %2\n\tfscale\n\tfstp %%st(1)\n\tfstpl %0" : "=m"(o2) : "m"(ex), "m"(a) : "memory");
      if (rep < 8) printf("rep %d o1=%llx o2=%llx\n", rep, B(o1), B(o2)); h = hd(h, o1); h = hd(h, o2); }
  }
  return h;
}
int main(void){ printf("x87 %08x\n", t_x87()); return 0; }
