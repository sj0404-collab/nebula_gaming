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
int main(void) {
  printf("camera   %08x\n", t_camera());
  printf("scalar   %08x\n", t_scalar());
  printf("cmp      %08x\n", t_cmp());
  printf("packed   %08x\n", t_packed());
  printf("jitdiff done\n");
  return 0;
}
