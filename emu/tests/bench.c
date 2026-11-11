/* bench.c — where does the time go under the x86→wasm JIT?  Small, deterministic workloads shaped like the code real
 * programs spend their time in, each timed with QueryPerformanceCounter and finished with a checksum (so the interpreter,
 * the JIT and the native runner can be compared on both speed and correctness):
 *   vm_switch  — a bytecode interpreter with switch dispatch (jmp [table+idx*8]) = what the JVM template interpreter and
 *                every scripting language look like to the emulator: short blocks ending in an indirect jump
 *   vm_goto    — the same interpreter with computed goto (threaded code): one indirect jump per handler
 *   calls      — recursive fib: call/ret/push/pop, stack traffic
 *   memcpy     — byte/word/qword copy loops and a strlen-like scan
 *   hash       — xorshift + random table reads/writes (cache-missing loads through the TLB)
 *   fpu        — scalar SSE float/double arithmetic (nbody step), cvt between int and float
 *   bits       — bt/bts/bsf/bswap/shifts/rotates (the opcodes HotSpot's generated code leans on)
 *   zig cc -target x86_64-windows-gnu -O2 -o bench.exe emu/tests/bench.c   (and -target x86-windows-gnu)
 *   node emu/tests/wasm_run.mjs --jit 1 bench.exe  |  node emu/tests/wasm_run.mjs --jit 0 bench.exe  |  ./emu/x64/x64run bench.exe */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static double now_ms(void) { static LARGE_INTEGER f; LARGE_INTEGER c; if (!f.QuadPart) QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return c.QuadPart * 1000.0 / f.QuadPart; }
static uint32_t rng = 0x12345678u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* ---- bytecode VM: a stack machine running a nested loop that sums i*j ---- */
enum { OP_PUSH, OP_LOAD, OP_STORE, OP_ADD, OP_SUB, OP_MUL, OP_AND, OP_JNZ, OP_JMP, OP_DUP, OP_POP, OP_LT, OP_HALT };
static const uint8_t prog[] = {
  /* 0 */ OP_PUSH, 0, 0, 0, 0, OP_STORE, 0,          /* acc = 0 */
  /* 7 */ OP_PUSH, 200, 0, 0, 0, OP_STORE, 1,        /* i = 200 */
  /* 14: outer */ OP_PUSH, 150, 0, 0, 0, OP_STORE, 2,/* j = 150 */
  /* 21: inner */ OP_LOAD, 0, OP_LOAD, 1, OP_LOAD, 2, OP_MUL, OP_ADD, OP_PUSH, 0xff, 0xff, 0xff, 0x0f, OP_AND, OP_STORE, 0,
  /* 37 */ OP_LOAD, 2, OP_PUSH, 1, 0, 0, 0, OP_SUB, OP_DUP, OP_STORE, 2, OP_JNZ, 21, 0,
  /* 51 */ OP_LOAD, 1, OP_PUSH, 1, 0, 0, 0, OP_SUB, OP_DUP, OP_STORE, 1, OP_JNZ, 14, 0,
  /* 65 */ OP_HALT };
static uint32_t vm_switch(int rounds) {
  uint32_t h = 2166136261u;
  for (int r = 0; r < rounds; r++) {
    int32_t stack[64], vars[8] = { 0 }; int sp = 0, pc = 0;
    for (;;) {
      uint8_t op = prog[pc++];
      switch (op) {
        case OP_PUSH: { int32_t v; memcpy(&v, prog + pc, 4); pc += 4; stack[sp++] = v; break; }
        case OP_LOAD: stack[sp++] = vars[prog[pc++]]; break;
        case OP_STORE: vars[prog[pc++]] = stack[--sp]; break;
        case OP_ADD: sp--; stack[sp - 1] += stack[sp]; break;
        case OP_SUB: sp--; stack[sp - 1] -= stack[sp]; break;
        case OP_MUL: sp--; stack[sp - 1] *= stack[sp]; break;
        case OP_AND: sp--; stack[sp - 1] &= stack[sp]; break;
        case OP_JNZ: { int16_t d; memcpy(&d, prog + pc, 2); pc += 2; if (stack[--sp]) pc = d; break; }
        case OP_JMP: { int16_t d; memcpy(&d, prog + pc, 2); pc = d; break; }
        case OP_DUP: stack[sp] = stack[sp - 1]; sp++; break;
        case OP_POP: sp--; break;
        case OP_LT: sp--; stack[sp - 1] = stack[sp - 1] < stack[sp]; break;
        default: goto done;
      }
    }
  done: h = (h ^ (uint32_t)vars[0]) * 0x01000193u;
  }
  return h;
}
static uint32_t vm_goto(int rounds) {
  static const void *tab[] = { &&l_push, &&l_load, &&l_store, &&l_add, &&l_sub, &&l_mul, &&l_and, &&l_jnz, &&l_jmp, &&l_dup, &&l_pop, &&l_lt, &&l_halt };
  uint32_t h = 2166136261u;
  for (int r = 0; r < rounds; r++) {
    int32_t stack[64], vars[8] = { 0 }; int sp = 0, pc = 0;
#define NEXT goto *tab[prog[pc++]]
    NEXT;
  l_push: { int32_t v; memcpy(&v, prog + pc, 4); pc += 4; stack[sp++] = v; NEXT; }
  l_load: stack[sp++] = vars[prog[pc++]]; NEXT;
  l_store: vars[prog[pc++]] = stack[--sp]; NEXT;
  l_add: sp--; stack[sp - 1] += stack[sp]; NEXT;
  l_sub: sp--; stack[sp - 1] -= stack[sp]; NEXT;
  l_mul: sp--; stack[sp - 1] *= stack[sp]; NEXT;
  l_and: sp--; stack[sp - 1] &= stack[sp]; NEXT;
  l_jnz: { int16_t d; memcpy(&d, prog + pc, 2); pc += 2; if (stack[--sp]) pc = d; NEXT; }
  l_jmp: { int16_t d; memcpy(&d, prog + pc, 2); pc = d; NEXT; }
  l_dup: stack[sp] = stack[sp - 1]; sp++; NEXT;
  l_pop: sp--; NEXT;
  l_lt: sp--; stack[sp - 1] = stack[sp - 1] < stack[sp]; NEXT;
  l_halt: h = (h ^ (uint32_t)vars[0]) * 0x01000193u;
#undef NEXT
  }
  return h;
}
/* ---- calls ---- */
static int __attribute__((noinline)) fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
static uint32_t calls(void) { return (uint32_t)fib(32) * 2654435761u; }
/* ---- memory ---- */
static uint8_t buf_a[1 << 20], buf_b[1 << 20];
static uint32_t memcpy_bench(void) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < (1 << 20); i++) buf_a[i] = (uint8_t)(i * 7 + 3);
  for (int r = 0; r < 40; r++) {
    for (int i = 0; i < (1 << 20); i++) buf_b[i] = buf_a[i];                                    /* byte copy */
    for (int i = 0; i < (1 << 20) / 8; i++) ((volatile uint64_t *)buf_a)[i] = ((uint64_t *)buf_b)[i] + 1; /* qword copy */
    int n = 0; for (const uint8_t *p = buf_b; *p; p++) n++;                                      /* strlen-like scan */
    h = (h ^ (uint32_t)n) * 0x01000193u; h = (h ^ buf_a[r * 1000 + 5]) * 0x01000193u;
  }
  return h;
}
static uint32_t table[1 << 18];
static uint32_t hash_bench(void) {
  uint32_t h = 2166136261u; rng = 0x9e3779b9u;
  for (int i = 0; i < 10000000; i++) { uint32_t k = rnd(); uint32_t *p = &table[k & ((1 << 18) - 1)]; *p = *p * 31 + k; h ^= *p; }
  return h;
}
/* ---- floats: nbody step on 5 bodies, 200k steps ---- */
static uint32_t fpu_bench(void) {
  double x[5] = { 0, 4.84, 8.34, 12.89, 15.37 }, y[5] = { 0, -1.16, 4.12, -15.11, -25.91 }, vx[5] = { 0, 0.6, -1.0, 0.1, 0.9 }, vy[5] = { 0, 2.8, 2.3, 2.0, 1.6 }, m[5] = { 39.4, 0.037, 0.011, 0.0017, 0.002 };
  float fs = 0; int32_t is = 0;
  for (int s = 0; s < 400000; s++) {
    for (int i = 0; i < 5; i++) for (int j = i + 1; j < 5; j++) { double dx = x[i] - x[j], dy = y[i] - y[j], d2 = dx * dx + dy * dy + 0.01, mag = 0.01 / (d2 * __builtin_sqrt(d2));
      vx[i] -= dx * m[j] * mag; vy[i] -= dy * m[j] * mag; vx[j] += dx * m[i] * mag; vy[j] += dy * m[i] * mag; }
    for (int i = 0; i < 5; i++) { x[i] += 0.01 * vx[i]; y[i] += 0.01 * vy[i]; }
    fs += (float)x[1] * 0.5f; is += (int32_t)(fs * 3.0f);
  }
  uint32_t h = 2166136261u; for (int i = 0; i < 5; i++) { uint64_t u; memcpy(&u, &x[i], 8); h = (h ^ (uint32_t)(u >> 40)) * 0x01000193u; } return (h ^ (uint32_t)is) * 0x01000193u;
}
/* ---- bit ops ---- */
static uint32_t bits_bench(void) {
  uint64_t v = 0x0123456789abcdefull; uint32_t h = 2166136261u;
  for (int i = 0; i < 4000000; i++) {
    v ^= v << 7; v ^= v >> 9; v = __builtin_bswap64(v) ^ (uint64_t)i;
    int tz = __builtin_ctzll(v | 1), lz = __builtin_clzll(v | 1);
    v |= 1ull << (tz & 63); v ^= (v >> (lz & 31)) & 1;
    v = (v << 13) | (v >> 51);
    h = (h ^ (uint32_t)v ^ (uint32_t)tz) * 0x01000193u;
  }
  return h;
}

/* ---- jorbis-like: what Ogg Vorbis decoding looks like in a C1-compiled JVM (Shattered Pixel Dungeon spends ~60 % of its
 * instructions here): scalar float butterflies over float[] with Java bounds checks (cmp [len]; jae cold) on every access,
 * plus a byte-at-a-time stream read (DirectByteBuffer.get) ---- */
typedef struct { volatile int len; float *volatile d; } FArr;                 /* volatile: the length and data pointer are re-read on every access, as C1 code does (no hoisting, no vectorisation) */
typedef struct { volatile int len; volatile int pos; unsigned char *volatile d; } BBuf;
static float jx_d[1024], jy_d[1024], jw_d[1024]; static unsigned char jb_d[65536];
static void __attribute__((noinline, cold)) oob(void) { fputs("bounds\n", stderr); exit(9); }
static inline float fld(const FArr *a, int i) { if (__builtin_expect((unsigned)i >= (unsigned)a->len, 0)) oob(); return a->d[i]; }
static inline void fst(FArr *a, int i, float v) { if (__builtin_expect((unsigned)i >= (unsigned)a->len, 0)) oob(); a->d[i] = v; }
static inline int bget(BBuf *b) { int p = b->pos; if (__builtin_expect((unsigned)p >= (unsigned)b->len, 0)) oob(); b->pos = p + 1; return b->d[p]; }
static uint32_t jorbis_bench(void) {
  FArr x = { 1024, jx_d }, y = { 1024, jy_d }, w = { 1024, jw_d }; BBuf bb = { 65536, 0, jb_d };
  for (int i = 0; i < 1024; i++) { jx_d[i] = (float)((i * 37) % 101) * 0.01f - 0.5f; jw_d[i] = (float)((i * 11) % 61) * 0.015f - 0.4f; }
  for (int i = 0; i < 65536; i++) jb_d[i] = (unsigned char)(i * 13 + 7);
  int acc = 0;
  for (int r = 0; r < 3000; r++) {
    int n = x.len;
    for (int i = 0; i < n / 2; i++) {
      float a = fld(&x, i), b = fld(&x, n - 1 - i), w0 = fld(&w, 2 * i), w1 = fld(&w, 2 * i + 1);
      fst(&y, i, a * w0 - b * w1); fst(&y, n - 1 - i, a * w1 + b * w0);
    }
    for (int i = 0; i < n; i++) fst(&x, i, fld(&y, i) * 0.999f + fld(&w, (i * 7) & 1023) * 0.001f);
    if (bb.pos + 64 > bb.len) bb.pos = 0;
    for (int k = 0; k < 64; k++) acc = (acc * 31 + bget(&bb)) & 0xffffff;
  }
  uint32_t h = 2166136261u; for (int i = 0; i < 1024; i += 16) { uint32_t u; memcpy(&u, &jx_d[i], 4); h = (h ^ u) * 0x01000193u; } return (h ^ (uint32_t)acc) * 0x01000193u;
}

int main(void) {
  struct { const char *name; uint32_t (*fn)(void); } tests[] = { { "calls", calls }, { "memcpy", memcpy_bench }, { "hash", hash_bench }, { "fpu", fpu_bench }, { "bits", bits_bench }, { "jorbis", jorbis_bench } };
  double t0 = now_ms(); uint32_t h = vm_switch(100); double t1 = now_ms(); printf("vm_switch %8.1f ms  %08x\n", t1 - t0, h); fflush(stdout);
  t0 = now_ms(); h = vm_goto(100); t1 = now_ms(); printf("vm_goto   %8.1f ms  %08x\n", t1 - t0, h); fflush(stdout);
  for (unsigned i = 0; i < sizeof tests / sizeof tests[0]; i++) { t0 = now_ms(); h = tests[i].fn(); t1 = now_ms(); printf("%-9s %8.1f ms  %08x\n", tests[i].name, t1 - t0, h); fflush(stdout); }
  printf("bench done\n");
  return 0;
}
