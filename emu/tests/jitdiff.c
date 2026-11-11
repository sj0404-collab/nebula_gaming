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

/* 1. the noosa Camera matrix (what Shattered Pixel Dungeon computes every frame): ints → float, scale, translate */
static uint32_t t_camera(void) {
  uint32_t h = 2166136261u;
  for (int it = 0; it < 400; it++) {
    int x = (int)(rnd() % 7) - 3, y = (int)(rnd() % 7) - 3, width = 300 + (int)(rnd() % 40), height = 160 + (int)(rnd() % 30);
    float zoom = 1.0f + (float)(rnd() % 3), sx = (float)(rnd() % 100) * 0.01f, sy = (float)(rnd() % 100) * 0.01f;
    float invW2 = 2.0f / (float)width, invH2 = 2.0f / (float)height;
    float m0 = zoom * invW2, m5 = -zoom * invH2;
    float m12 = -1.0f + (float)x * invW2 - sx * m0, m13 = 1.0f - (float)y * invH2 - sy * m5;
    h = hf(hf(hf(hf(h, m0), m5), m12), m13);
  }
  return h;
}
/* 2. scalar float arithmetic chains with memory operands, int→float→int round trips */
static uint32_t t_scalar(void) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < N; i++) { fa[i] = (float)((int)(rnd() % 20001) - 10000) * 0.37f; fb[i] = (float)(rnd() % 1000) + 0.5f; ia[i] = (int)(rnd() % 100000) - 50000; la[i] = ((int64_t)rnd() << 20) - (1ll << 50); da[i] = (double)ia[i] * 1.75e-3; }
  float acc = 0.f; double dacc = 0.0; int32_t iacc = 0; int64_t lacc = 0;
  for (int rep = 0; rep < 6; rep++)
    for (int i = 0; i < N; i++) {
      float a = fa[i], b = fb[i]; int32_t k = ia[i]; int64_t l = la[i];
      float c = a * b + (float)k - b / (a + 1000.5f);
      float d = sqrtf(b) * 0.5f - (float)l * 1e-12f;
      double e = (double)c * 2.5 + da[i] - (double)d;
      float f = (float)e;
      acc += c - d + f;
      iacc += (int32_t)c + (int32_t)f;              /* cvttss2si */
      lacc += (int64_t)e + (int64_t)d;              /* cvttsd2si r64, cvttss2si r64 */
      dacc += e * 0.125 + (double)(int32_t)(c * 3.f);
      out[i] = c;
    }
  for (int i = 0; i < N; i += 97) h = hf(h, out[i]);
  return hd(h32(h32(hf(hd(h, dacc), acc), (uint32_t)iacc), (uint32_t)lacc), (double)lacc);
}
/* 3. compares and branches incl. NaN, min/max, abs/neg via sign masks, Math.round-like */
static uint32_t t_cmp(void) {
  uint32_t h = 2166136261u; float nan = 0.0f / 0.0f; (void)nan; volatile float zero = 0.f; float qnan = zero / zero;
  int lt = 0, gt = 0, eq = 0, un = 0, mn = 0; float mx = -1e30f, mnv = 1e30f, sgn = 0.f;
  for (int rep = 0; rep < 4; rep++)
    for (int i = 0; i < N; i++) {
      float a = fa[i], b = fb[i]; if ((i & 63) == 7) a = qnan;
      if (a < b) lt++; else if (a > b) gt++; else if (a == b) eq++; else un++;
      if (!(a >= b)) mn++;
      mx = fmaxf(mx, b); mnv = fminf(mnv, b);
      float na = -a, aa = fabsf(a); sgn += (na < 0.f ? 1.f : -1.f) + (aa >= 0.f ? 0.5f : -0.5f);
      int r = (int)floorf(b + 0.5f); lt += r & 1;
      double dd = (double)a; if (dd < (double)b) gt += 2;
    }
  return h32(h32(h32(h32(h32(hf(hf(hf(h, mx), mnv), sgn), (uint32_t)lt), (uint32_t)gt), (uint32_t)eq), (uint32_t)un), (uint32_t)mn);
}
/* 4. packed SSE (compiler vectorised loops) and GPR<->XMM moves, 32-bit zero-extension */
static uint32_t t_packed(void) {
  uint32_t h = 2166136261u; static float va[N], vb[N], vc[N]; static int32_t vi[N];
  for (int i = 0; i < N; i++) { va[i] = fa[i]; vb[i] = fb[i]; vi[i] = ia[i]; }
  for (int rep = 0; rep < 8; rep++) {
    for (int i = 0; i < N; i++) vc[i] = va[i] * vb[i] + (float)vi[i] * 0.25f - vc[i] * 0.5f;
    for (int i = 0; i < N; i++) vi[i] = (vi[i] ^ (int32_t)(vc[i] > 0.f ? 1 : 0)) + (int32_t)(vb[i] * 0.01f);
  }
  uint64_t z = 0; for (int i = 0; i < N; i++) { uint32_t u; memcpy(&u, &vc[i], 4); uint32_t w = u ^ (uint32_t)vi[i]; z += (uint64_t)w * 2654435761u; z ^= z >> 29; }
  for (int i = 0; i < N; i += 61) h = hf(h, vc[i]);
  return h32(h32(h, (uint32_t)z), (uint32_t)(z >> 32));
}
/* 5. the exact HotSpot / clang instruction forms, hand-written (x86-64 only): xorps-zero + cvtsi2ss/sd of negative ints, cvttss2si,
 *    cvtss2sd/cvtsd2ss, sqrtss, xorps sign-mask negation from memory, minss/maxss, ucomiss branches */
#if defined(__x86_64__)
static const float signmask[4] __attribute__((aligned(16))) = { -0.0f, -0.0f, -0.0f, -0.0f };
static uint32_t t_asm(void) {
  uint32_t h = 2166136261u;
  for (int rep = 0; rep < 3; rep++)
    for (int i = 0; i < N; i++) {
      int32_t k = ia[i]; int64_t l = la[i]; float f = fa[i], g = fb[i]; double d = da[i];
      float r1, r2, r3, r4, r5; double r6, r7; int32_t i1, i2; int64_t l1;
      __asm__ volatile("xorps %0, %0\n\tcvtsi2ss %1, %0" : "=&x"(r1) : "r"(k));                 /* (float)int, negative half the time */
      __asm__ volatile("xorps %0, %0\n\tcvtsi2ss %1, %0" : "=&x"(r2) : "r"(l));                 /* (float)long */
      __asm__ volatile("xorps %0, %0\n\tcvtsi2sd %1, %0" : "=&x"(r6) : "r"(k));                 /* (double)int */
      __asm__ volatile("xorps %0, %0\n\tcvtsi2ssl %1, %0" : "=&x"(r3) : "m"(ia[i]));            /* memory form */
      __asm__ volatile("cvttss2si %1, %0" : "=r"(i1) : "x"(f));
      __asm__ volatile("cvtss2si %1, %0" : "=r"(i2) : "x"(g));
      __asm__ volatile("cvttsd2si %1, %0" : "=r"(l1) : "x"(d));
      __asm__ volatile("xorps %0, %0\n\tcvtss2sd %1, %0" : "=&x"(r7) : "x"(f));
      __asm__ volatile("xorps %0, %0\n\tcvtsd2ss %1, %0" : "=&x"(r4) : "x"(d));
      __asm__ volatile("xorps %0, %0\n\tsqrtss %1, %0" : "=&x"(r5) : "x"(g));
      float neg; __asm__ volatile("movaps %1, %0\n\txorps %2, %0" : "=&x"(neg) : "x"(f), "m"(*(const float (*)[4])signmask));
      float mx, mn; __asm__ volatile("movaps %1, %0\n\tmaxss %2, %0" : "=&x"(mx) : "x"(f), "x"(g)); __asm__ volatile("movaps %1, %0\n\tminss %2, %0" : "=&x"(mn) : "x"(f), "x"(g));
      int fa_, fb_; __asm__ volatile("xor %0, %0\n\tucomiss %2, %1\n\tseta %b0" : "=&q"(fa_) : "x"(f), "x"(g)); __asm__ volatile("xor %0, %0\n\tucomiss %2, %1\n\tsetb %b0" : "=&q"(fb_) : "x"(f), "x"(g)); int flags = fa_ | (fb_ << 1);
      h = hf(hf(hf(hf(hf(hd(hd(h, r6), r7), r1), r2), r3), r4), r5);
      h = h32(h32(h32(h32(hf(hf(hf(h, neg), mx), mn), (uint32_t)i1), (uint32_t)i2), (uint32_t)l1), (uint32_t)flags);
    }
  return h;
}
#else
static uint32_t t_asm(void) { return 0; }
#endif
/* 6. integer forms added to the translator in v1.42 (both bitnesses): inc/dec r32, push imm, leave, bt/bts/btr/btc on registers and
 *    on memory bit strings (negative offsets), bt imm, bsf/bsr (incl. zero input: destination unchanged, ZF), bswap, shld/shrd,
 *    mul/imul/div/idiv one-operand forms (incl. the #DE-free slow paths), imul with live flags, shufps/shufpd, cvtdq2ps,
 *    movlps/movhps/movhlps/movlhps, 32-bit mode call/jmp/push through memory */
#include <xmmintrin.h>
#include <emmintrin.h>
static uint32_t bitstr[64];
typedef uint32_t (*fn_t)(uint32_t);
static uint32_t __attribute__((noinline)) f_a(uint32_t x) { return x * 3u + 1u; }
static uint32_t __attribute__((noinline)) f_b(uint32_t x) { return x ^ 0x5bd1e995u; }
static uint32_t __attribute__((noinline)) f_c(uint32_t x) { return (x << 5) | (x >> 27); }
static fn_t fns[3] = { f_a, f_b, f_c };
static uint32_t t_misc(void) {
  uint32_t h = 2166136261u; rng = 0x2545F491u;
  for (int i = 0; i < 64; i++) bitstr[i] = rnd();
  for (int it = 0; it < 2000; it++) {
    uint32_t a = rnd(), b = rnd() | 1, c = rnd();
    /* inc/dec, push imm, leave-ish frames come from the compiler; make sure some run with live flags */
    uint32_t x = a; for (int k = 0; k < (int)(b & 7) + 1; k++) { x++; if (x == 0) h ^= 1; } for (int k = 0; k < (int)(c & 3) + 1; k++) { x--; if ((int32_t)x < 0) h += 3; }
    /* bt family: register forms */
    uint32_t cf1, cf2, cf3, cf4, r1 = a, r2 = a, r3 = a;
    __asm__("xor %0, %0\n\tbt %2, %1\n\tsetc %b0" : "=&q"(cf1) : "r"(a), "r"(b) : "cc");
    __asm__("xor %0, %0\n\tbts %2, %1\n\tsetc %b0" : "=&q"(cf2), "+r"(r1) : "r"(c) : "cc");
    __asm__("xor %0, %0\n\tbtr %2, %1\n\tsetc %b0" : "=&q"(cf3), "+r"(r2) : "r"(c) : "cc");
    __asm__("xor %0, %0\n\tbtc %2, %1\n\tsetc %b0" : "=&q"(cf4), "+r"(r3) : "r"(c) : "cc");
    uint32_t cf5, cf6, r4 = a; __asm__("xor %0, %0\n\tbt $13, %1\n\tsetc %b0" : "=&q"(cf5) : "r"(a) : "cc"); __asm__("xor %0, %0\n\tbts $29, %1\n\tsetc %b0" : "=&q"(cf6), "+r"(r4) : : "cc");
    /* bt family: memory bit strings with signed offsets (-1024 .. 1023) */
    int32_t off = (int32_t)(c % 2048) - 1024; uint32_t cm1, cm2, cm3;
    __asm__("xor %0, %0\n\tbtl %2, %1\n\tsetc %b0" : "=&q"(cm1) : "m"(bitstr[32]), "r"(off) : "cc");
    __asm__("xor %0, %0\n\tbtsl %2, %1\n\tsetc %b0" : "=&q"(cm2), "+m"(bitstr[32]) : "r"(off) : "cc", "memory");
    __asm__("xor %0, %0\n\tbtcl %2, %1\n\tsetc %b0" : "=&q"(cm3), "+m"(bitstr[32]) : "r"(off) : "cc", "memory");
    uint32_t cm4; __asm__("xor %0, %0\n\tbtl $7, %1\n\tsetc %b0" : "=&q"(cm4) : "m"(bitstr[(c >> 3) & 63]) : "cc");
    uint32_t cm5; __asm__("xor %0, %0\n\tbtrl $31, %1\n\tsetc %b0" : "=&q"(cm5), "+m"(bitstr[(c >> 9) & 63]) : : "cc", "memory");
    /* bsf / bsr with zero input: destination stays, ZF set */
    uint32_t v = (it & 3) == 0 ? 0 : a, d1 = 0x77, d2 = 0x99, z1, z2;
    __asm__("bsf %2, %0\n\tsetz %b1" : "+r"(d1), "=&q"(z1) : "r"(v) : "cc"); __asm__("bsr %2, %0\n\tsetz %b1" : "+r"(d2), "=&q"(z2) : "r"(v) : "cc");
    uint32_t bs = __builtin_bswap32(a); uint64_t bs64 = __builtin_bswap64(((uint64_t)a << 32) | c);
    /* shld / shrd */
    uint32_t s1 = a, s2 = a; __asm__("shld $7, %1, %0" : "+r"(s1) : "r"(c) : "cc"); __asm__("shrd $13, %1, %0" : "+r"(s2) : "r"(c) : "cc");
    uint32_t s3 = a, s4 = a; uint32_t zf1, zf2; __asm__("shld $1, %2, %0\n\tsetc %b1" : "+r"(s3), "=&q"(zf1) : "r"(c) : "cc"); __asm__("shrd $31, %2, %0\n\tsets %b1" : "+r"(s4), "=&q"(zf2) : "r"(c) : "cc");
    /* mul / imul / div / idiv one-operand (32-bit) */
    uint32_t lo, hi, ilo, ihi, mf, imf;
    __asm__("mull %3\n\tsetc %b2" : "=a"(lo), "=d"(hi), "=&q"(mf) : "r"(b), "a"(a) : "cc");
    __asm__("imull %3\n\tseto %b2" : "=a"(ilo), "=d"(ihi), "=&q"(imf) : "r"(b), "a"(a) : "cc");
    uint32_t q, r; { uint32_t dhi = (it & 1) ? 0 : (c % b); __asm__("divl %4" : "=a"(q), "=d"(r) : "a"(a), "d"(dhi), "r"(b) : "cc"); }
    int32_t iq, ir; { int32_t num = (int32_t)a, den = (int32_t)(b | 2); if ((it & 7) == 3) { num = INT32_MIN; den = -1; } int32_t dhi = num >> 31;   /* INT32_MIN / -1 is #DE-free only through the 64-bit path: skip the trap case itself */
      if (den == -1 && num == INT32_MIN) { iq = 0; ir = 0; } else __asm__("idivl %4" : "=a"(iq), "=d"(ir) : "a"(num), "d"(dhi), "r"(den) : "cc"); }
    /* imul with live flags (overflow check), 64-bit unsigned/signed division through the compiler */
    int32_t p; int ov; { int32_t m1 = (int32_t)a, m2 = (int32_t)(c >> 4); ov = __builtin_mul_overflow(m1, m2, &p); }
    uint64_t u64 = ((uint64_t)a << 20) | c, q64 = u64 / (uint64_t)b, r64 = u64 % (uint64_t)b; int64_t s64 = (int64_t)(((uint64_t)c << 32) | a) - 0x7000000000000000ll, sq = s64 / (int64_t)(int32_t)b, sr = s64 % (int64_t)(int32_t)b;
    /* SSE shuffles and conversions */
    __m128 fx = _mm_set_ps((float)(int32_t)a, (float)(int32_t)b, 1.5f, -2.25f), fy = _mm_set_ps(3.f, (float)(int32_t)c, -7.f, 0.125f);
    __m128 sh1 = _mm_shuffle_ps(fx, fy, 0x1B), sh2 = _mm_shuffle_ps(fy, fx, 0xC9), sh3 = _mm_shuffle_ps(fx, fx, 0x4E);
    __m128i iv = _mm_set_epi32((int32_t)a, (int32_t)b, (int32_t)c, -12345); __m128 cv = _mm_cvtepi32_ps(iv);
    __m128 hl = _mm_movehl_ps(fx, fy), lh = _mm_movelh_ps(fx, fy); float two[2] = { (float)c, (float)a }; __m128 ld = _mm_loadl_pi(fx, (const __m64 *)two), hd_ = _mm_loadh_pi(fy, (const __m64 *)two);
    float st[2]; _mm_storeh_pi((__m64 *)st, sh1);
    __m128d dx = _mm_set_pd((double)(int32_t)a, 2.5), dy = _mm_set_pd(-1.0, (double)(int32_t)c); __m128d shd = _mm_shuffle_pd(dx, dy, 1);
    float o[4]; _mm_storeu_ps(o, sh1); for (int k = 0; k < 4; k++) h = hf(h, o[k]); _mm_storeu_ps(o, sh2); for (int k = 0; k < 4; k++) h = hf(h, o[k]); _mm_storeu_ps(o, sh3); for (int k = 0; k < 4; k++) h = hf(h, o[k]);
    _mm_storeu_ps(o, cv); for (int k = 0; k < 4; k++) h = hf(h, o[k]); _mm_storeu_ps(o, hl); for (int k = 0; k < 4; k++) h = hf(h, o[k]); _mm_storeu_ps(o, lh); for (int k = 0; k < 4; k++) h = hf(h, o[k]);
    _mm_storeu_ps(o, ld); for (int k = 0; k < 4; k++) h = hf(h, o[k]); _mm_storeu_ps(o, hd_); for (int k = 0; k < 4; k++) h = hf(h, o[k]); h = hf(hf(h, st[0]), st[1]);
    double od[2]; _mm_storeu_pd(od, shd); h = hd(hd(h, od[0]), od[1]);
    /* indirect calls through a table (jmp/call [mem] in both modes) */
    uint32_t fv = fns[c % 3](a); fv = fns[(c >> 2) % 3](fv);
    /* push esp / push rsp pushes the value the stack pointer had BEFORE the decrement (Intel SDM); pop esp loads the popped value */
    uintptr_t sp_before, sp_pushed, sp_after;
#if defined(__x86_64__)
    __asm__ volatile("mov %%rsp, %0\n\tpush %%rsp\n\tpop %1\n\tmov %%rsp, %2" : "=&r"(sp_before), "=&r"(sp_pushed), "=&r"(sp_after) : : "memory");
#else
    __asm__ volatile("mov %%esp, %0\n\tpush %%esp\n\tpop %1\n\tmov %%esp, %2" : "=&r"(sp_before), "=&r"(sp_pushed), "=&r"(sp_after) : : "memory");
#endif
    uint32_t spk = (uint32_t)(sp_pushed == sp_before) | ((uint32_t)(sp_after == sp_before) << 1) | ((uint32_t)(sp_pushed - sp_before) << 2);
    uint32_t vals[] = { x, cf1, cf2, cf3, cf4, cf5, cf6, r1, r2, r3, r4, cm1, cm2, cm3, cm4, cm5, d1, d2, z1, z2, bs, (uint32_t)bs64, (uint32_t)(bs64 >> 32), s1, s2, s3, s4, zf1, zf2, lo, hi, ilo, ihi, mf, imf, q, r, (uint32_t)iq, (uint32_t)ir,
      (uint32_t)p, (uint32_t)ov, (uint32_t)q64, (uint32_t)(q64 >> 32), (uint32_t)r64, (uint32_t)sq, (uint32_t)(sq >> 32), (uint32_t)sr, fv, spk };
    for (unsigned k = 0; k < sizeof vals / sizeof vals[0]; k++) h = h32(h, vals[k]);
  }
  for (int i = 0; i < 64; i++) h = h32(h, bitstr[i]);
  return h;
}
/* lock-prefixed forms (HotSpot: `lock addl [rsp],0` fences, lock cmpxchg CAS, lock xadd counters, lock or/and bit flags):
   values, the CAS success/failure paths and the flags (ZF of cmpxchg, CF/OF/SF of xadd/add) all go into the checksum */
static volatile int32_t at32[4]; static volatile int64_t at64[4]; static volatile uint8_t at8[4]; static volatile uint16_t at16[4];
static uint32_t t_atomic(void) {
  uint32_t h = 2166136261u; rng = 0x1234567u;
  for (int i = 0; i < 4000; i++) {
    int32_t k = (int32_t)rnd(); int64_t k64 = (int64_t)rnd() * 0x9E3779B97F4A7C15ll;
    int32_t a = __atomic_fetch_add(&at32[i & 3], k, __ATOMIC_SEQ_CST);                 /* lock xadd */
    int64_t b = __atomic_fetch_add(&at64[i & 3], k64, __ATOMIC_SEQ_CST);
    int32_t exp = at32[(i + 1) & 3]; int32_t ok = __atomic_compare_exchange_n(&at32[i & 3], &exp, k ^ 0x5a5a, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);   /* lock cmpxchg */
    int64_t exp64 = (i & 1) ? at64[i & 3] : k64; int32_t ok64 = __atomic_compare_exchange_n(&at64[i & 3], &exp64, k64 + 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    __atomic_fetch_or(&at32[(i + 2) & 3], 1 << (i & 31), __ATOMIC_SEQ_CST);          /* lock or [m], r */
    __atomic_fetch_and(&at32[(i + 3) & 3], ~(1 << ((i >> 1) & 31)), __ATOMIC_SEQ_CST);/* lock and [m], r */
    __atomic_fetch_add(&at32[(i + 1) & 3], 1, __ATOMIC_SEQ_CST);                      /* lock add/inc [m], 1 */
    __atomic_fetch_sub(&at64[(i + 2) & 3], 7, __ATOMIC_SEQ_CST);                      /* lock sub [m], 7 */
    __atomic_fetch_xor(&at8[i & 3], (uint8_t)k, __ATOMIC_SEQ_CST);                    /* lock xor byte */
    __atomic_fetch_add(&at16[i & 3], (uint16_t)k, __ATOMIC_SEQ_CST);                  /* 66 lock add word */
    uint32_t cf = 0, zf = 0;
#if defined(__x86_64__)
    __asm__ volatile("lock addl $0, (%%rsp)\n\tlock xaddl %2, %3\n\tsetc %b0\n\tsetz %b1" : "=&r"(cf), "=&r"(zf), "+r"(k), "+m"(at32[(i + 2) & 3]) : : "memory", "cc");
#else
    __asm__ volatile("lock addl $0, (%%esp)\n\tlock xaddl %2, %3\n\tsetc %b0\n\tsetz %b1" : "=&q"(cf), "=&q"(zf), "+q"(k), "+m"(at32[(i + 2) & 3]) : : "memory", "cc");
#endif
    h = h32(h, (uint32_t)a); h = h32(h, (uint32_t)b); h = h32(h, (uint32_t)(b >> 32)); h = h32(h, (uint32_t)ok | ((uint32_t)ok64 << 1) | (cf & 1) << 2 | (zf & 1) << 3); h = h32(h, (uint32_t)exp); h = h32(h, (uint32_t)exp64); h = h32(h, (uint32_t)k);
  }
  for (int i = 0; i < 4; i++) { h = h32(h, (uint32_t)at32[i]); h = h32(h, (uint32_t)at64[i]); h = h32(h, (uint32_t)(at64[i] >> 32)); h = h32(h, at8[i] | (uint32_t)at16[i] << 8); }
  return h;
}
int main(void) {
  printf("camera   %08x\n", t_camera());
  printf("scalar   %08x\n", t_scalar());
  printf("cmp      %08x\n", t_cmp());
  printf("packed   %08x\n", t_packed());
  printf("asm      %08x\n", t_asm());
  printf("misc     %08x\n", t_misc());
  printf("atomic   %08x\n", t_atomic());
  printf("jitdiff done\n");
  return 0;
}
