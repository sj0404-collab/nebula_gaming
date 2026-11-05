/* ---------- JIT: hot x86 basic blocks → WebAssembly functions (included by cpu.c) ----------
 * The interpreter's decode cache (Dc) already holds the byte-level decode of every executed instruction.  Once a block
 * start has been entered JIT_HOT times, the straight-line instructions from it up to the next control transfer are
 * translated into one wasm function  f(c, code) -> next_rip  :
 *   - the common integer forms (mov/lea/alu/test/cmp/jcc/jmp/call/ret/push/pop/movzx/movsx/shift/setcc/cmov/inc/dec…)
 *     become inline wasm: registers live in the Cpu struct, guest memory goes through a module-local TLB probe;
 *   - every other instruction is executed by calling back into the interpreter for exactly one step (jit_step), so a
 *     block never has to stop at an instruction the translator does not know — only at real branches;
 *   - flags are computed inline, but only the flags a later instruction of the block (or the block exit) can observe:
 *     `add; add; cmp; jl` costs one SF/OF computation.  adc/sbb/rotates/shifts keep the interpreter's helpers (bit-exact).
 * The host (src/lib/x64/runtime.ts, emu/tests/wasm_run.mjs) compiles the emitted module, imports the main memory and
 * the jit_* exports below, and publishes the function in the main module's indirect table; the C side calls it through
 * that table index like any function pointer.  The function re-validates its own code bytes on entry (returns 1 =
 * stale), so patched / regenerated guest code (JVM, .NET) is handled exactly like the decode cache handles it.
 * Natively there is no wasm engine: host_jit_compile returns 0 and everything stays on the interpreter. */
#define JB_BITS 14
#define JIT_HOT 96
#define JIT_MAX_INSNS 64
#define JIT_MAX_BYTES 480
typedef struct { u64 rip; Mem *mem; const u8 *code; u32 gen; u32 fn; s32 count; u16 pc; u8 m32; } Jb;   /* fn: table index of the region function, pc: entry block inside it */
static Jb g_jb[1 << JB_BITS];
#define JB_SLOT(rip) (&g_jb[((rip) ^ ((rip) >> JB_BITS)) & ((1 << JB_BITS) - 1)])
int g_jit_on = 0;
static u32 g_jit_free[4096]; static int g_jit_nfree;
#define JIT_MAX_FN 65536
static u16 g_fnref[JIT_MAX_FN];   /* JB entries referencing a table slot; 0 → slot reusable */
#define JIT_MAX_BLOCKS 48
#define JIT_MAX_REGION_INSNS 640
struct JitStats { u64 blocks, stale, nodc, exec, fails, insns, steps, bytes, dsteps, dinterp; } g_jit_st;

#ifdef __wasm__
#define JEXPORT(name) __attribute__((export_name(#name)))
#else
#define JEXPORT(name)
#endif
static int cpu_step1(Cpu *c);
/* ---- helpers called from the generated code (bit-exact interpreter semantics) ---- */
static u32 g_step_hist[1024];   /* [op | op2<<8 for 0F | 512+ for prefixed] dynamic step counts (diagnostics: x64_jit_stats) */
JEXPORT(jit_step) u64 jit_step(Cpu *c, u64 rip) {
  c->rip = rip; g_jit_st.dsteps++;
  { Dc *e = DC_SLOT(rip); if (e->rip == rip) { int k = e->op == 0x0F ? 256 + e->op2 : e->op; if (e->seg || e->rep || e->lock || e->p66) k += 512; g_step_hist[k & 1023]++; } }
  cpu_step1(c); return c->rip;
}
JEXPORT(jit_mem) u64 jit_mem(Cpu *c, u64 a, u64 size, int write, u64 rip) {
  u8 *p = mem_ptr(c->mem, a, size, write);
  if (p) return (u64)(size_t)p;
  c->rip = rip; fault_av(c, a, write); c->err_rip = rip;
  if (c->exc_code && w32_hw_exception(c)) return 0;   /* dispatched: status is RUN_OK again, c->rip = handler */
  return 0;
}
JEXPORT(jit_alu) u64 jit_alu(Cpu *c, int op, u64 a, u64 b, int sz) { return alu(c, op, a, b, sz); }
JEXPORT(jit_shift) u64 jit_shift(Cpu *c, int op, u64 v, int cnt, int sz) { return shift(c, op, v, cnt, sz); }

/* ---- wasm byte emitter ---- */
typedef struct { u8 *b; u32 n, cap; int depth; const u64 *starts; int nblk, blk, m32; } Buf;   /* starts/nblk/blk: region blocks (sorted) and the one being emitted */
static void b8(Buf *B, u32 v) { if (B->n < B->cap) B->b[B->n] = (u8)v; B->n++; }
static void bu(Buf *B, u64 v) { do { u8 x = v & 0x7f; v >>= 7; if (v) x |= 0x80; b8(B, x); } while (v); }
static void bs(Buf *B, s64 v) { for (;;) { u8 x = v & 0x7f; v >>= 7; if ((v == 0 && !(x & 0x40)) || (v == -1 && (x & 0x40))) { b8(B, x); return; } b8(B, x | 0x80); } }
static void bbytes(Buf *B, const void *p, u32 n) { for (u32 i = 0; i < n; i++) b8(B, ((const u8 *)p)[i]); }
#define OP(x) b8(B, x)
enum { L_C = 0, L_CODE = 1, L_PC = 2, L_LIMIT = 3, L_EA = 4, L_HOST = 5, L_T0 = 6, L_T1 = 7, L_T2 = 8, L_RET = 9, L_T3 = 10, L_F0 = 11, L_F1 = 12 };
#define CO_X(ri)  ((u64)__builtin_offsetof(Cpu, x) + (u64)(ri) * 16)
#define CO_FS     ((u64)__builtin_offsetof(Cpu, fs_base))
#define CO_GS     ((u64)__builtin_offsetof(Cpu, gs_base))
enum { F64_LOAD = 0x2B, F32_LOAD = 0x2A, F64_STORE = 0x39, F32_STORE = 0x38, F64_ADD = 0xA0, F64_SUB = 0xA1, F64_MUL = 0xA2, F64_DIV = 0xA3, F64_SQRT = 0x9F, F64_NEAREST = 0x9E,
       F32_ADD = 0x92, F32_SUB = 0x93, F32_MUL = 0x94, F32_DIV = 0x95, F32_SQRT = 0x91, F64_EQ = 0x61, F64_NE = 0x62, F64_LT = 0x63, F64_GT = 0x64, F64_GE = 0x66, F32_LT = 0x5D, F32_GT = 0x5E,
       F64_PROMOTE = 0xBB, F32_DEMOTE = 0xB6, F64_CVT_I64S = 0xB9, F32_CVT_I64S = 0xB5, I32_TRUNC_F64S = 0xAA, I64_TRUNC_F64S = 0xB0, I64_EXT32_U = 0xAD };
static void e_f64c(Buf *B, double v) { OP(0x44); bbytes(B, &v, 8); }
enum { FN_STEP = 0, FN_MEM = 1, FN_ALU = 2, FN_SHIFT = 3, FN_BLOCK = 4, FN_TLB = 5 };
enum { BR = 0x0C, BR_TABLE = 0x0E };
/* label depths seen from the code of region block B->blk (see jit_translate for the layout) */
static int lbl_block(Buf *B, int j) { return j - 1 - B->blk + B->depth; }   /* forward target block j > blk */
static int lbl_loop(Buf *B) { return B->nblk - 1 - B->blk + B->depth; }
static int lbl_fault(Buf *B) { return B->nblk - B->blk + B->depth; }
static int blk_of(Buf *B, u64 rip) { for (int i = 0; i < B->nblk; i++) if (B->starts[i] == rip) return i; return -1; }
#define CO_R(ri)  ((u64)__builtin_offsetof(Cpu, r) + (u64)(ri) * 8)
#define CO_RIP    ((u64)__builtin_offsetof(Cpu, rip))
#define CO_RFLAGS ((u64)__builtin_offsetof(Cpu, rflags))
#define CO_ICOUNT ((u64)__builtin_offsetof(Cpu, icount))
#define CO_STATUS ((u64)__builtin_offsetof(Cpu, status))
#define CO_MEM    ((u64)__builtin_offsetof(Cpu, mem))
#define MO_TLB    ((u64)__builtin_offsetof(Mem, tlb))
enum { I64_ADD = 0x7C, I64_SUB = 0x7D, I64_MUL = 0x7E, I64_AND = 0x83, I64_OR = 0x84, I64_XOR = 0x85, I64_SHL = 0x86, I64_SHR_S = 0x87, I64_SHR_U = 0x88,
       I64_EQZ = 0x50, I64_EQ = 0x51, I64_NE = 0x52, I64_LT_U = 0x54, I64_LE_U = 0x58, I64_POPCNT = 0x7B, I64_EXT_U = 0xAD, I64_EXT32_S = 0xC4, I32_WRAP = 0xA7,
       I32_EQZ = 0x45, I32_AND = 0x71, I32_OR = 0x72, I32_XOR = 0x73, DROP = 0x1A, SELECT = 0x1B, RETURN = 0x0F, CALL = 0x10, BR_IF = 0x0D };
static void e_i64c(Buf *B, u64 v) { OP(0x42); bs(B, (s64)v); }
static void e_i32c(Buf *B, s32 v) { OP(0x41); bs(B, v); }
static void e_lget(Buf *B, int l) { OP(0x20); bu(B, l); }
static void e_lset(Buf *B, int l) { OP(0x21); bu(B, l); }
static void e_ltee(Buf *B, int l) { OP(0x22); bu(B, l); }
static void e_ld(Buf *B, u8 op, u32 align, u64 off) { OP(op); bu(B, align); bu(B, off); }
static void e_st(Buf *B, u8 op, u32 align, u64 off) { OP(op); bu(B, align); bu(B, off); }
static void e_if(Buf *B, int type) { OP(0x04); OP(type ? type : 0x40); B->depth++; }   /* type: 0 = void, 0x7E = i64 */
static void e_else(Buf *B) { OP(0x05); }
static void e_end(Buf *B) { OP(0x0B); B->depth--; }
static u8 ld_u(int sz) { return sz == 8 ? 0x29 : sz == 4 ? 0x35 : sz == 2 ? 0x33 : 0x31; }
static u8 st_n(int sz) { return sz == 8 ? 0x37 : sz == 4 ? 0x3E : sz == 2 ? 0x3D : 0x3C; }
static u32 al_of(int sz) { return sz == 8 ? 3 : sz == 4 ? 2 : sz == 2 ? 1 : 0; }
static void e_ldreg(Buf *B, int r, int sz) { e_lget(B, L_C); e_ld(B, ld_u(sz), al_of(sz), CO_R(r)); }
/* register store: the address (local c) must already be on the stack below the value */
static void e_streg(Buf *B, int r, int sz) { if (sz == 4) { e_i64c(B, 0xffffffffull); OP(I64_AND); } if (sz >= 4) e_st(B, 0x37, 3, CO_R(r)); else e_st(B, st_n(sz), al_of(sz), CO_R(r)); }
/* 8-bit register operand: ah/ch/dh/bh when no REX and reg 4..7 → byte 1 of rax/rcx/rdx/rbx */
static u64 r8_off(const Dc *e, int r) { return (!e->rex && r >= 4 && r < 8) ? CO_R(r - 4) + 1 : CO_R(r); }
static void e_ldr8(Buf *B, const Dc *e, int r) { e_lget(B, L_C); e_ld(B, 0x31, 0, r8_off(e, r)); }
static void e_icount(Buf *B, int k) { if (!k) return; e_lget(B, L_C); e_lget(B, L_C); e_ld(B, 0x29, 3, CO_ICOUNT); e_i64c(B, (u64)k); OP(I64_ADD); e_st(B, 0x37, 3, CO_ICOUNT); }
static void e_exit_const(Buf *B, u64 rip, int k) { e_icount(B, k); e_i64c(B, rip); OP(RETURN); }
static void e_exit_top(Buf *B, int k) { e_lset(B, L_RET); e_icount(B, k); e_lget(B, L_RET); OP(RETURN); }   /* next rip on the stack */
/* control transfer to a constant target: forward block → direct br to its label; backward block → budget check, pc, br to the dispatch loop; outside → return */
static void e_goto(Buf *B, u64 t, int k) {
  int j = blk_of(B, t);
  if (j < 0) { e_exit_const(B, t, k); return; }
  e_icount(B, k);
  if (j > B->blk) { OP(BR); bu(B, lbl_block(B, j)); return; }
  e_lget(B, L_C); e_ld(B, 0x29, 3, CO_ICOUNT); e_lget(B, L_LIMIT); OP(0x5A); e_if(B, 0); e_i64c(B, t); OP(RETURN); e_end(B);   /* i64.ge_u: time slice used up */
  e_i32c(B, j); e_lset(B, L_PC); OP(BR); bu(B, lbl_loop(B));
}
/* effective address of a memory operand → stack (i64) */
typedef struct { const Dc *e; u64 rip; u8 kind, need, fdef, fuse, fk, sz, seg; } JItem;
static void e_ea(Buf *B, const JItem *it) {
  const Dc *e = it->e; u64 start = it->rip;
  if (e->riprel) e_i64c(B, start + e->len + (u64)(s64)e->disp);
  else {
    e_i64c(B, (u64)(s64)e->disp);
    if (e->base >= 0) { e_ldreg(B, e->base, 8); OP(I64_ADD); }
    if (e->idx >= 0) { e_ldreg(B, e->idx, 8); if (e->scale) { e_i64c(B, e->scale); OP(I64_SHL); } OP(I64_ADD); }
    if (B->m32) { e_i64c(B, 0xffffffffull); OP(I64_AND); }
  }
  if (it->seg) { e_lget(B, L_C); e_ld(B, 0x29, 3, it->seg == 1 ? CO_FS : CO_GS); OP(I64_ADD); }   /* fs: / gs: (TEB / TLS) */
}
/* EA on the stack → host pointer in L_HOST through the module-local TLB function; 0 = fault → leave the block (c->rip set) */
static void e_host(Buf *B, int size, int write, u64 rip) {
  e_lset(B, L_EA); e_lget(B, L_C); e_lget(B, L_EA); e_i64c(B, size); e_i32c(B, write); e_i64c(B, rip); OP(CALL); bu(B, FN_TLB);
  e_ltee(B, L_HOST); OP(I64_EQZ); OP(BR_IF); bu(B, lbl_fault(B));
}
/* r/m operand helpers (memory forms assume L_HOST is set) */
static void e_rm_load(Buf *B, const Dc *e, int sz) { if (e->mod == 3) e_ldreg(B, e->rm, sz); else { e_lget(B, L_HOST); e_ld(B, ld_u(sz), 0, 0); } }
static void e_rm_load8(Buf *B, const Dc *e) { if (e->mod == 3) e_ldr8(B, e, e->rm); else { e_lget(B, L_HOST); e_ld(B, 0x31, 0, 0); } }
static void e_dst_addr(Buf *B, const Dc *e) { e_lget(B, e->mod == 3 ? L_C : L_HOST); }
static void e_dst_store(Buf *B, const Dc *e, int sz) { if (e->mod == 3) e_streg(B, e->rm, sz); else e_st(B, st_n(sz), 0, 0); }
static void e_dst_store8(Buf *B, const Dc *e) { if (e->mod == 3) e_st(B, 0x3C, 0, r8_off(e, e->rm)); else e_st(B, 0x3C, 0, 0); }
static void e_prep_mem(Buf *B, const JItem *it, int size, int write) { if (it->e->mod != 3) { e_ea(B, it); e_host(B, size, write, it->rip); } }
static s64 imm_at(const u8 *code, int off, int bytes) { if (bytes == 1) return (s8)code[off]; if (bytes == 2) { s16 v; __builtin_memcpy(&v, code + off, 2); return v; } s32 v; __builtin_memcpy(&v, code + off, 4); return v; }
static s32 imm32_at(const u8 *code, int off) { s32 v; __builtin_memcpy(&v, code + off, 4); return v; }
/* condition code → i32 0/1 on the stack */
static void e_cond(Buf *B, int cc) {
  e_lget(B, L_C); e_ld(B, 0x29, 3, CO_RFLAGS); int neg = cc & 1;
  switch (cc >> 1) {
    case 0: e_i64c(B, 11); OP(I64_SHR_U); e_i64c(B, 1); OP(I64_AND); break;
    case 1: e_i64c(B, 1); OP(I64_AND); break;
    case 2: e_i64c(B, 6); OP(I64_SHR_U); e_i64c(B, 1); OP(I64_AND); break;
    case 3: e_i64c(B, 0x41); OP(I64_AND); OP(I64_EQZ); neg ^= 1; goto done32;   /* eqz already yields !r */
    case 4: e_i64c(B, 7); OP(I64_SHR_U); e_i64c(B, 1); OP(I64_AND); break;
    case 5: e_i64c(B, 2); OP(I64_SHR_U); e_i64c(B, 1); OP(I64_AND); break;
    case 6: e_ltee(B, L_T3); e_i64c(B, 7); OP(I64_SHR_U); e_lget(B, L_T3); e_i64c(B, 11); OP(I64_SHR_U); OP(I64_XOR); e_i64c(B, 1); OP(I64_AND); break;
    default: e_ltee(B, L_T3); e_i64c(B, 6); OP(I64_SHR_U); e_lget(B, L_T3); e_i64c(B, 7); OP(I64_SHR_U); e_lget(B, L_T3); e_i64c(B, 11); OP(I64_SHR_U); OP(I64_XOR); OP(I64_OR); e_i64c(B, 1); OP(I64_AND); break;
  }
  OP(I32_WRAP);
done32:
  if (neg) { e_i32c(B, 1); OP(I32_XOR); }
}
/* push / pop (psz = 8, or 4 in a 32-bit process) */
static void e_push_begin(Buf *B, int psz, int m32, u64 rip) {   /* rsp -= psz; L_HOST = &[rsp]; caller then pushes the value and calls e_push_end */
  e_lget(B, L_C); e_ldreg(B, RSP, 8); e_i64c(B, psz); OP(I64_SUB); if (m32) { e_i64c(B, 0xffffffffull); OP(I64_AND); } e_ltee(B, L_EA); e_st(B, 0x37, 3, CO_R(RSP));
  e_lget(B, L_EA); e_host(B, psz, 1, rip);
  e_lget(B, L_HOST);
}
static void e_push_end(Buf *B, int psz) { e_st(B, st_n(psz), 0, 0); }
static void e_pop(Buf *B, int psz, int m32, u64 rip) {   /* value → stack; rsp += psz */
  e_ldreg(B, RSP, 8); e_host(B, psz, 0, rip);
  e_lget(B, L_HOST); e_ld(B, ld_u(psz), 0, 0); e_lset(B, L_T3);
  e_lget(B, L_C); e_ldreg(B, RSP, 8); e_i64c(B, psz); OP(I64_ADD); if (m32) { e_i64c(B, 0xffffffffull); OP(I64_AND); } e_st(B, 0x37, 3, CO_R(RSP));
  e_lget(B, L_T3);
}
static const u8 alu_wasm[8] = { I64_ADD, I64_OR, 0, 0, I64_AND, I64_SUB, I64_XOR, I64_SUB };   /* add or - - and sub xor cmp */

/* ---- flags ---- */
#define FL_ALL 0x3f
enum { FB_CF = 1, FB_PF = 2, FB_AF = 4, FB_ZF = 8, FB_SF = 16, FB_OF = 32 };
static u64 fl_bits(int need) { return (need & FB_CF ? F_CF : 0) | (need & FB_PF ? F_PF : 0) | (need & FB_AF ? F_AF : 0) | (need & FB_ZF ? F_ZF : 0) | (need & FB_SF ? F_SF : 0) | (need & FB_OF ? F_OF : 0); }
static int cc_use(int cc) { static const u8 t[8] = { FB_OF, FB_CF, FB_ZF, FB_CF | FB_ZF, FB_SF, FB_PF, FB_SF | FB_OF, FB_ZF | FB_SF | FB_OF }; return t[cc >> 1]; }
/* a in L_T0, b in L_T1, masked result in L_T2 → rflags = (rflags & ~need) | computed.  kind: 0 add, 1 sub/cmp/neg, 2 logic (CF=OF=AF=0) */
static void e_flags(Buf *B, int kind, int sz, int need) {
  if (!need) return;
  int sh = sz * 8 - 1;
  e_lget(B, L_C); e_lget(B, L_C); e_ld(B, 0x29, 3, CO_RFLAGS); e_i64c(B, ~fl_bits(need)); OP(I64_AND);
  if (need & FB_ZF) { e_lget(B, L_T2); OP(I64_EQZ); OP(I64_EXT_U); e_i64c(B, 6); OP(I64_SHL); OP(I64_OR); }
  if (need & FB_SF) { e_lget(B, L_T2); e_i64c(B, sh); OP(I64_SHR_U); e_i64c(B, 7); OP(I64_SHL); OP(I64_OR); }
  if (need & FB_PF) { e_lget(B, L_T2); e_i64c(B, 0xff); OP(I64_AND); OP(I64_POPCNT); e_i64c(B, 1); OP(I64_AND); e_i64c(B, 1); OP(I64_XOR); e_i64c(B, 2); OP(I64_SHL); OP(I64_OR); }
  if (kind != 2) {
    if (need & FB_CF) { if (kind == 0) { e_lget(B, L_T2); e_lget(B, L_T0); } else { e_lget(B, L_T0); e_lget(B, L_T1); } OP(I64_LT_U); OP(I64_EXT_U); OP(I64_OR); }
    if (need & FB_OF) { if (kind == 0) { e_lget(B, L_T0); e_lget(B, L_T2); OP(I64_XOR); e_lget(B, L_T1); e_lget(B, L_T2); OP(I64_XOR); } else { e_lget(B, L_T0); e_lget(B, L_T1); OP(I64_XOR); e_lget(B, L_T0); e_lget(B, L_T2); OP(I64_XOR); }
      OP(I64_AND); e_i64c(B, sh); OP(I64_SHR_U); e_i64c(B, 1); OP(I64_AND); e_i64c(B, 11); OP(I64_SHL); OP(I64_OR); }
    if (need & FB_AF) { e_lget(B, L_T0); e_lget(B, L_T1); OP(I64_XOR); e_lget(B, L_T2); OP(I64_XOR); e_i64c(B, 0x10); OP(I64_AND); OP(I64_OR); }
  }
  e_st(B, 0x37, 3, CO_RFLAGS);
}
static void e_mask(Buf *B, int sz) { if (sz < 8) { e_i64c(B, sz == 4 ? 0xffffffffull : sz == 2 ? 0xffffull : 0xffull); OP(I64_AND); } }
/* ---- flags liveness ---- */
enum { K_INL, K_STEP, K_TERM };
enum { JK_TEST8_IMM = 100, JK_TEST_AL_IMM, JK_TEST_A_IMM, JK_MOV8_R_IMM, JK_CVTSI2SD, JK_CVTSD2SI, JK_SQRT, JK_CVT5A, JK_MOVD_X_RM, JK_MOVD_RM_X, JK_MOVQ_X, JK_MOVQ_RM_X, JK_PLOGIC, JK_MOVDQ, JK_SSE_MINMAX };
/* class / operand size / segment of an instruction: the interpreter's fast class when it has one, otherwise the prefixed
   (66 → 16-bit, fs:/gs:) and SSE forms the translator handles itself */
static void jit_fast(const Dc *e, JItem *it) {
  it->fk = e->fast; it->sz = e->rexw ? 8 : 4; it->seg = 0;
  if (e->fast || e->lock || e->p67) return;
  u8 op = e->op;
  if (op == 0x0F) {
    u8 op2 = e->op2; if (e->seg) return;
    if (e->rep && !e->p66) {
      if (op2 == 0x2A) it->fk = JK_CVTSI2SD; else if (op2 == 0x2C || op2 == 0x2D) it->fk = JK_CVTSD2SI; else if (op2 == 0x51) it->fk = JK_SQRT; else if (op2 == 0x5A) it->fk = JK_CVT5A;
      else if (op2 == 0x5D || op2 == 0x5F) it->fk = JK_SSE_MINMAX; else if (e->rep == 0xF3 && op2 == 0x7E) it->fk = JK_MOVQ_X; else if (e->rep == 0xF3 && (op2 == 0x6F || op2 == 0x7F)) it->fk = JK_MOVDQ;
      return;
    }
    if (e->p66 && !e->rep) {
      if (op2 == 0x6E) it->fk = JK_MOVD_X_RM; else if (op2 == 0x7E) it->fk = JK_MOVD_RM_X; else if (op2 == 0xD6) it->fk = JK_MOVQ_RM_X;
      else if (op2 == 0xDB || op2 == 0xDF || op2 == 0xEB || op2 == 0xEF) it->fk = JK_PLOGIC; else if (op2 == 0x6F || op2 == 0x7F) it->fk = JK_MOVDQ;
      return;
    }
    return;
  }
  if (e->rep) return;
  if (!e->p66 && !e->seg) {
    if (op == 0xF6 && (e->reg & 7) == 0) it->fk = JK_TEST8_IMM; else if (op == 0xA8) it->fk = JK_TEST_AL_IMM; else if (op == 0xA9) it->fk = JK_TEST_A_IMM; else if (op >= 0xB0 && op <= 0xB7) it->fk = JK_MOV8_R_IMM;
    return;
  }
  if (e->p66) { if (e->seg) return; it->sz = 2; } else it->seg = e->mod == 3 ? 0 : e->seg;
  if (e->m32 && (op == 0xFF || op == 0x63 || op == 0xC7 || (op >= 0xB8 && op <= 0xBF))) return;
  switch (op) {
    case 0x88: it->fk = FK_MOV8_RM_R; return; case 0x8A: it->fk = FK_MOV8_R_RM; return; case 0x80: it->fk = FK_ALU8_RM_IMM; return; case 0xC6: it->fk = FK_MOV8_RM_IMM; return;
    case 0x89: it->fk = FK_MOV_RM_R; return; case 0x8B: it->fk = FK_MOV_R_RM; return;
    case 0x83: it->fk = FK_ALU_RM_IMM8; return; case 0x81: it->fk = FK_ALU_RM_IMM32; return;
    case 0x85: it->fk = e->mod == 3 ? FK_TEST_RR : FK_TEST_RM_R; return;
    case 0xC7: it->fk = FK_MOV_RM_IMM32; return;
    case 0xF7: it->fk = (e->reg & 7) < 2 ? FK_F7_TEST : (e->reg & 7) < 4 ? FK_F7_NOTNEG : 0; return;
    case 0xFF: it->fk = (e->p66 && (e->reg & 7) >= 2) ? 0 : FK_FF; return;
    case 0x63: it->fk = (!e->p66 && it->sz == 8) ? FK_MOVSXD : 0; return;
    case 0xC1: it->fk = FK_SHIFT_IMM; return; case 0xD1: it->fk = FK_SHIFT_1; return; case 0xD3: it->fk = FK_SHIFT_CL; return;
    case 0x8D: if (e->mod != 3 && !e->p66) { it->fk = FK_LEA; it->seg = 0; } return;   /* LEA ignores segment overrides */
    default:
      if (op < 0x40) { static const u8 k[8] = { FK_ALU8_RM_R, FK_ALU_RM_R, FK_ALU8_R_RM, FK_ALU_R_RM, FK_ALU8_AL_IMM, FK_ALU_A_IMM, 0, 0 }; it->fk = k[op & 7]; return; }
      if (op >= 0xB8 && op <= 0xBF) { it->fk = FK_MOV_R_IMM; return; }
      return;
  }
}
static void jit_classify(JItem *it, const u8 *code) {
  const Dc *e = it->e; it->kind = K_INL; it->fdef = 0; it->fuse = 0; jit_fast(e, it);
  switch (it->fk) {
    case FK_NONE: it->kind = K_STEP; it->fuse = FL_ALL; return;
    case FK_SSE_CMP: it->fdef = FL_ALL; return;
    case JK_TEST8_IMM: case JK_TEST_AL_IMM: case JK_TEST_A_IMM: it->fdef = FL_ALL; return;
    case FK_JCC8: it->kind = K_TERM; it->fuse = cc_use(e->op & 15); return;
    case FK_JCC32: it->kind = K_TERM; it->fuse = cc_use(e->op2 & 15); return;
    case FK_JMP8: case FK_JMP32: case FK_CALL: case FK_RET: case FK_RET_N: it->kind = K_TERM; return;
    case FK_FF: { int sub = e->reg & 7; if (sub == 2 || sub == 4) it->kind = K_TERM; else if (sub == 0 || sub == 1) it->fdef = FL_ALL & ~FB_CF; else if (sub != 6) { it->kind = K_STEP; it->fuse = FL_ALL; } return; }
    case FK_ALU_RM_IMM8: case FK_ALU_RM_IMM32: case FK_ALU8_RM_IMM: { int aop = e->reg & 7; it->fdef = FL_ALL; if (aop == 2 || aop == 3) it->fuse = FB_CF; return; }
    case FK_ALU_RM_R: case FK_ALU_R_RM: case FK_ALU_A_IMM: case FK_ALU8_RM_R: case FK_ALU8_R_RM: case FK_ALU8_AL_IMM: { int aop = e->op >> 3; it->fdef = FL_ALL; if (aop == 2 || aop == 3) it->fuse = FB_CF; return; }
    case FK_TEST_RR: case FK_TEST_RM_R: case FK_TEST8: case FK_F7_TEST: case FK_IMUL_R_RM: case FK_IMUL_IMM: it->fdef = FL_ALL; return;
    case FK_F7_NOTNEG: if ((e->reg & 7) == 3) it->fdef = FL_ALL; return;
    case FK_SHIFT_IMM: case FK_SHIFT_1: { int cnt = it->fk == FK_SHIFT_1 ? 1 : code[e->mr_end] & (it->sz == 8 ? 63 : 31); if (cnt && (e->reg & 7) >= 4) it->fdef = FL_ALL; else { it->fdef = FL_ALL; it->fuse = FL_ALL; } return; }
    case FK_SHIFT_CL: it->fdef = FL_ALL; it->fuse = FL_ALL; return;
    case FK_SETCC: case FK_CMOV: it->fuse = cc_use(e->op2 & 15); return;
    default: return;   /* mov/lea/push/pop/movzx/movsx/cdq/nop/sse: no flags */
  }
}

/* ---- one instruction → wasm ---- */
static void e_step(Buf *B, u64 rip, u64 next, int k) {   /* interpreter single step; exit unless it fell through to `next` with RUN_OK */
  e_lget(B, L_C); e_i64c(B, rip); OP(CALL); bu(B, FN_STEP); e_ltee(B, L_RET); e_i64c(B, next); OP(I64_NE);
  e_lget(B, L_C); e_ld(B, 0x28, 2, CO_STATUS); OP(I32_EQZ); OP(I32_EQZ); OP(I32_OR);
  e_if(B, 0); e_icount(B, k); e_lget(B, L_RET); OP(RETURN); e_end(B);
}
/* source operand of an ALU form: register, 8-bit register, or immediate (already masked to sz) */
typedef struct { int reg; u64 imm; } Src;
static void e_src(Buf *B, const Dc *e, const Src *s, int sz) { if (s->reg < 0) e_i64c(B, s->imm); else if (sz == 1) e_ldr8(B, e, s->reg); else e_ldreg(B, s->reg, sz); }
static void e_load_any(Buf *B, const Dc *e, int sz) { if (sz == 1) e_rm_load8(B, e); else e_rm_load(B, e, sz); }
/* dst op= src where dst is r/m (dst_rm = 1) or the register e->reg / RAX (dst_rm = 0, then src is the r/m or an immediate) */
static void e_alu(Buf *B, const Dc *e, int aop, int sz, int dst_rm, int dst_reg, const Src *src, int src_is_rm, int need) {
  int is_cmp = aop == 7, kind = (aop == 0) ? 0 : (aop == 5 || aop == 7) ? 1 : 2;
  if (aop == 2 || aop == 3) {   /* adc/sbb: interpreter helper (flags in, flags out) */
    if (dst_rm) e_dst_addr(B, e); else e_lget(B, L_C);
    e_lget(B, L_C); e_i32c(B, aop);
    if (dst_rm) e_load_any(B, e, sz); else if (sz == 1) e_ldr8(B, e, dst_reg); else e_ldreg(B, dst_reg, sz);
    if (src_is_rm) e_load_any(B, e, sz); else e_src(B, e, src, sz);
    e_i32c(B, sz); OP(CALL); bu(B, FN_ALU);
    if (dst_rm) { if (sz == 1) e_dst_store8(B, e); else e_dst_store(B, e, sz); } else if (sz == 1) e_st(B, 0x3C, 0, r8_off(e, dst_reg)); else e_streg(B, dst_reg, sz);
    return;
  }
  if (is_cmp && !need) { if (e->mod != 3 && (dst_rm || src_is_rm)) { e_load_any(B, e, sz); OP(DROP); } return; }   /* dead cmp/test: keep the memory read (fault behaviour) */
  if (!is_cmp) { if (dst_rm) e_dst_addr(B, e); else e_lget(B, L_C); }
  /* a */
  if (dst_rm) e_load_any(B, e, sz); else if (sz == 1) e_ldr8(B, e, dst_reg); else e_ldreg(B, dst_reg, sz);
  if (need) e_ltee(B, L_T0);
  /* b */
  if (src_is_rm) e_load_any(B, e, sz); else e_src(B, e, src, sz);
  if (need) e_ltee(B, L_T1);
  OP(alu_wasm[aop]);
  if (need) { e_mask(B, sz); e_ltee(B, L_T2); if (is_cmp) OP(DROP); e_flags(B, kind, sz, need); }
  if (!is_cmp) { if (dst_rm) { if (sz == 1) e_dst_store8(B, e); else e_dst_store(B, e, sz); } else if (sz == 1) e_st(B, 0x3C, 0, r8_off(e, dst_reg)); else e_streg(B, dst_reg, sz); }
}
static void e_test(Buf *B, const Dc *e, int sz, int rm_and_reg, u64 imm, int need) {   /* test r/m, reg | test r/m, imm */
  if (!need) { if (e->mod != 3) { e_load_any(B, e, sz); OP(DROP); } return; }
  e_load_any(B, e, sz); if (rm_and_reg) { if (sz == 1) e_ldr8(B, e, e->reg); else e_ldreg(B, e->reg, sz); } else e_i64c(B, imm);
  OP(I64_AND); e_mask(B, sz); e_lset(B, L_T2); e_flags(B, 2, sz, need);
}
static void e_shift(Buf *B, const Dc *e, int sz, int cnt_imm, int need, int cl) {   /* cl: count from CL */
  int op = e->reg & 7;
  if (!cl && !(cnt_imm & (sz == 8 ? 63 : 31))) return;   /* count 0: no-op, flags untouched */
  e_dst_addr(B, e);
  if (need || op < 4) {
    e_lget(B, L_C); e_i32c(B, op); e_rm_load(B, e, sz);
    if (cl) { e_lget(B, L_C); e_ld(B, 0x2D, 0, CO_R(RCX)); } else e_i32c(B, cnt_imm);   /* i32.load8_u cl */
    e_i32c(B, sz); OP(CALL); bu(B, FN_SHIFT);
  } else {
    e_rm_load(B, e, sz); if (op == 7 && sz == 4) OP(I64_EXT32_S); else if (op == 7 && sz == 2) OP(0xC3);
    if (cl) { e_ldreg(B, RCX, 8); e_i64c(B, sz == 8 ? 63 : 31); OP(I64_AND); } else e_i64c(B, (u64)(cnt_imm & (sz == 8 ? 63 : 31)));
    OP(op == 7 ? I64_SHR_S : op == 5 ? I64_SHR_U : I64_SHL);
  }
  e_dst_store(B, e, sz);
}
/* xmm source lanes: from register e->rm (CO_X) or memory (L_HOST); bytes 4/8 at byte offset `off` */
static void e_xmm_src(Buf *B, const Dc *e, int off, int bytes) { if (e->mod == 3) { e_lget(B, L_C); e_ld(B, bytes == 4 ? 0x35 : 0x29, bytes == 4 ? 2 : 3, CO_X(e->rm) + off); } else { e_lget(B, L_HOST); e_ld(B, bytes == 4 ? 0x35 : 0x29, 0, off); } }
static void e_xmm_fsrc(Buf *B, const Dc *e, int off, int dbl) { if (e->mod == 3) { e_lget(B, L_C); e_ld(B, dbl ? F64_LOAD : F32_LOAD, dbl ? 3 : 2, CO_X(e->rm) + off); } else { e_lget(B, L_HOST); e_ld(B, dbl ? F64_LOAD : F32_LOAD, 0, off); } }
/* xmm/m destination at byte offset off: push address (register → c, memory → host), later e_xmm_dst_store */
static void e_xmm_dst(Buf *B, const Dc *e, int off) { (void)off; e_lget(B, e->mod == 3 ? L_C : L_HOST); }
static void e_xmm_dst_store(Buf *B, const Dc *e, int off, int bytes) { if (e->mod == 3) e_st(B, bytes == 4 ? 0x3E : 0x37, bytes == 4 ? 2 : 3, CO_X(e->rm) + off); else e_st(B, bytes == 4 ? 0x3E : 0x37, 0, off); }
/* returns 0 if the item could not be emitted inline (caller falls back to a step) */
static int e_insn(Buf *B, const JItem *it, const u8 *code, int m32, int k) {
  const Dc *e = it->e; u64 start = it->rip, next = start + e->len; int sz = it->sz, psz = m32 ? 4 : 8, need = it->need, isz = sz == 2 ? 2 : 4;   /* isz: "imm32" is imm16 with 66 */
  if (m32) next = (u32)next;
  Src src = { -1, 0 };
  switch (it->fk) {
    case FK_NOP: return 1;
    case FK_MOV_RM_R: e_prep_mem(B, it, sz, 1); e_dst_addr(B, e); e_ldreg(B, e->reg, sz); e_dst_store(B, e, sz); return 1;
    case FK_MOV_R_RM: e_prep_mem(B, it, sz, 0); e_lget(B, L_C); e_rm_load(B, e, sz); e_streg(B, e->reg, sz); return 1;
    case FK_LEA: e_lget(B, L_C); e_ea(B, it); e_streg(B, e->reg, sz); return 1;
    case FK_MOV_R_IMM: { int r = (e->op & 7) | (e->rexb << 3); u64 v; if (sz == 8) __builtin_memcpy(&v, code + e->hdr, 8); else v = (u64)imm_at(code, e->hdr, isz) & mask(sz); e_lget(B, L_C); e_i64c(B, v); e_streg(B, r, sz); return 1; }
    case FK_MOV_RM_IMM32: e_prep_mem(B, it, sz, 1); e_dst_addr(B, e); e_i64c(B, (u64)imm_at(code, e->mr_end, isz)); e_dst_store(B, e, sz); return 1;
    case FK_MOV8_RM_R: e_prep_mem(B, it, 1, 1); e_dst_addr(B, e); e_ldr8(B, e, e->reg); e_dst_store8(B, e); return 1;
    case FK_MOV8_R_RM: e_prep_mem(B, it, 1, 0); e_lget(B, L_C); e_rm_load8(B, e); e_st(B, 0x3C, 0, r8_off(e, e->reg)); return 1;
    case FK_MOV8_RM_IMM: e_prep_mem(B, it, 1, 1); e_dst_addr(B, e); e_i64c(B, code[e->mr_end]); e_dst_store8(B, e); return 1;
    case FK_ALU_RM_IMM8: case FK_ALU_RM_IMM32: { int aop = e->reg & 7; src.imm = (u64)imm_at(code, e->mr_end, it->fk == FK_ALU_RM_IMM8 ? 1 : isz) & mask(sz);
      e_prep_mem(B, it, sz, aop != 7); e_alu(B, e, aop, sz, 1, 0, &src, 0, need); return 1; }
    case FK_ALU8_RM_IMM: { int aop = e->reg & 7; src.imm = code[e->mr_end]; e_prep_mem(B, it, 1, aop != 7); e_alu(B, e, aop, 1, 1, 0, &src, 0, need); return 1; }
    case FK_ALU_RM_R: { int aop = e->op >> 3; src.reg = e->reg; e_prep_mem(B, it, sz, aop != 7); e_alu(B, e, aop, sz, 1, 0, &src, 0, need); return 1; }
    case FK_ALU8_RM_R: { int aop = e->op >> 3; src.reg = e->reg; e_prep_mem(B, it, 1, aop != 7); e_alu(B, e, aop, 1, 1, 0, &src, 0, need); return 1; }
    case FK_ALU_R_RM: { int aop = e->op >> 3; e_prep_mem(B, it, sz, 0); e_alu(B, e, aop, sz, 0, e->reg, &src, 1, need); return 1; }
    case FK_ALU8_R_RM: { int aop = e->op >> 3; e_prep_mem(B, it, 1, 0); e_alu(B, e, aop, 1, 0, e->reg, &src, 1, need); return 1; }
    case FK_ALU_A_IMM: { int aop = e->op >> 3; src.imm = (u64)imm_at(code, e->hdr, isz) & mask(sz); e_alu(B, e, aop, sz, 0, RAX, &src, 0, need); return 1; }
    case FK_ALU8_AL_IMM: { int aop = e->op >> 3; src.imm = code[e->hdr]; e_alu(B, e, aop, 1, 0, RAX, &src, 0, need); return 1; }
    case FK_TEST_RR: case FK_TEST_RM_R: case FK_TEST8: { int s = it->fk == FK_TEST8 ? 1 : sz; e_prep_mem(B, it, s, 0); e_test(B, e, s, 1, 0, need); return 1; }
    case JK_TEST8_IMM: e_prep_mem(B, it, 1, 0); e_test(B, e, 1, 0, code[e->mr_end], need); return 1;
    case JK_TEST_AL_IMM: case JK_TEST_A_IMM: { int s = it->fk == JK_TEST_AL_IMM ? 1 : sz; if (!need) return 1;
      if (s == 1) e_ldr8(B, e, RAX); else e_ldreg(B, RAX, s); e_i64c(B, s == 1 ? code[e->hdr] : (u64)imm_at(code, e->hdr, isz) & mask(s)); OP(I64_AND); e_lset(B, L_T2); e_flags(B, 2, s, need); return 1; }
    case JK_MOV8_R_IMM: { int r = (e->op & 7) | (e->rexb << 3); e_lget(B, L_C); e_i64c(B, code[e->hdr]); e_st(B, 0x3C, 0, r8_off(e, r)); return 1; }
    case FK_F7_TEST: e_prep_mem(B, it, sz, 0); e_test(B, e, sz, 0, (u64)imm_at(code, e->mr_end, isz) & mask(sz), need); return 1;
    case FK_F7_NOTNEG: { e_prep_mem(B, it, sz, 1); e_dst_addr(B, e);
      if ((e->reg & 7) == 2) { e_rm_load(B, e, sz); e_i64c(B, ~0ull); OP(I64_XOR); }
      else if (need) { e_i64c(B, 0); e_lset(B, L_T0); e_rm_load(B, e, sz); e_lset(B, L_T1); e_lget(B, L_T0); e_lget(B, L_T1); OP(I64_SUB); e_mask(B, sz); e_ltee(B, L_T2); e_flags(B, 1, sz, need); }
      else { e_i64c(B, 0); e_rm_load(B, e, sz); OP(I64_SUB); }
      e_dst_store(B, e, sz); return 1; }
    case FK_MOVZX8: e_prep_mem(B, it, 1, 0); e_lget(B, L_C); e_rm_load8(B, e); e_streg(B, e->reg, sz); return 1;
    case FK_MOVZX16: e_prep_mem(B, it, 2, 0); e_lget(B, L_C); if (e->mod == 3) e_ldreg(B, e->rm, 2); else { e_lget(B, L_HOST); e_ld(B, 0x33, 0, 0); } e_streg(B, e->reg, sz); return 1;
    case FK_MOVSX8: e_prep_mem(B, it, 1, 0); e_lget(B, L_C); if (e->mod == 3) { e_lget(B, L_C); e_ld(B, 0x30, 0, r8_off(e, e->rm)); } else { e_lget(B, L_HOST); e_ld(B, 0x30, 0, 0); } e_streg(B, e->reg, sz); return 1;
    case FK_MOVSX16: e_prep_mem(B, it, 2, 0); e_lget(B, L_C); if (e->mod == 3) { e_lget(B, L_C); e_ld(B, 0x32, 1, CO_R(e->rm)); } else { e_lget(B, L_HOST); e_ld(B, 0x32, 0, 0); } e_streg(B, e->reg, sz); return 1;
    case FK_MOVSXD: e_prep_mem(B, it, 4, 0); e_lget(B, L_C); if (e->mod == 3) { e_lget(B, L_C); e_ld(B, 0x34, 2, CO_R(e->rm)); } else { e_lget(B, L_HOST); e_ld(B, 0x34, 0, 0); } e_streg(B, e->reg, 8); return 1;
    case FK_CDQE: if (sz == 2) return 0; e_lget(B, L_C); if (sz == 8) { e_lget(B, L_C); e_ld(B, 0x34, 2, CO_R(RAX)); e_streg(B, RAX, 8); } else { e_lget(B, L_C); e_ld(B, 0x32, 1, CO_R(RAX)); e_streg(B, RAX, 4); } return 1;
    case FK_CDQ: if (sz == 2) return 0; e_lget(B, L_C); if (sz == 8) e_ldreg(B, RAX, 8); else { e_lget(B, L_C); e_ld(B, 0x34, 2, CO_R(RAX)); } e_i64c(B, 63); OP(I64_SHR_S); e_streg(B, RDX, sz); return 1;
    case FK_SHIFT_IMM: e_prep_mem(B, it, sz, 1); e_shift(B, e, sz, code[e->mr_end], need, 0); return 1;
    case FK_SHIFT_1: e_prep_mem(B, it, sz, 1); e_shift(B, e, sz, 1, need, 0); return 1;
    case FK_SHIFT_CL: e_prep_mem(B, it, sz, 1); e_shift(B, e, sz, 0, need, 1); return 1;
    case FK_SETCC: e_prep_mem(B, it, 1, 1); e_dst_addr(B, e); e_cond(B, e->op2 & 15); OP(I64_EXT_U); e_dst_store8(B, e); return 1;
    case FK_CMOV: e_prep_mem(B, it, sz, 0); e_lget(B, L_C); e_rm_load(B, e, sz); e_ldreg(B, e->reg, sz); e_cond(B, e->op2 & 15); OP(SELECT); e_streg(B, e->reg, sz); return 1;
    case FK_IMUL_R_RM: case FK_IMUL_IMM: { if (need || sz == 2) return 0;
      e_prep_mem(B, it, sz, 0); e_lget(B, L_C);
      if (it->fk == FK_IMUL_R_RM) { e_ldreg(B, e->reg, sz); e_rm_load(B, e, sz); } else { e_rm_load(B, e, sz); e_i64c(B, e->op == 0x6B ? (u64)(s64)(s8)code[e->mr_end] : (u64)(s64)imm32_at(code, e->mr_end)); }
      OP(I64_MUL); e_streg(B, e->reg, sz); return 1; }
    case FK_PUSH: { int r = (e->op & 7) | (e->rexb << 3); e_push_begin(B, psz, m32, start); e_ldreg(B, r, 8); e_push_end(B, psz); return 1; }
    case FK_POP: { int r = (e->op & 7) | (e->rexb << 3); e_lget(B, L_C); e_pop(B, psz, m32, start); e_streg(B, r, 8); return 1; }
    case FK_FF: { int sub = e->reg & 7;
      if (sub == 0 || sub == 1) { e_prep_mem(B, it, sz, 1); e_dst_addr(B, e);
        e_rm_load(B, e, sz); if (need) e_ltee(B, L_T0); e_i64c(B, 1); if (need) e_ltee(B, L_T1); OP(sub ? I64_SUB : I64_ADD);
        if (need) { e_mask(B, sz); e_ltee(B, L_T2); e_flags(B, sub ? 1 : 0, sz, need); }
        e_dst_store(B, e, sz); return 1; }
      if (sub == 6) { e_prep_mem(B, it, 8, 0); e_rm_load(B, e, 8); e_lset(B, L_RET); e_push_begin(B, psz, m32, start); e_lget(B, L_RET); e_push_end(B, psz); return 1; }
      if (sub == 2) { e_prep_mem(B, it, 8, 0); e_rm_load(B, e, 8); e_lset(B, L_RET); e_push_begin(B, psz, m32, start); e_i64c(B, next); e_push_end(B, psz); e_icount(B, k + 1); e_lget(B, L_RET); OP(RETURN); return 1; }
      if (sub == 4) { e_prep_mem(B, it, 8, 0); e_rm_load(B, e, 8); e_exit_top(B, k + 1); return 1; }
      return 0; }
    case FK_JCC8: case FK_JCC32: { u64 t = it->fk == FK_JCC8 ? next + (u64)(s64)(s8)code[e->hdr] : next + (u64)(s64)imm32_at(code, e->hdr + 1); if (m32) t = (u32)t;
      e_cond(B, it->fk == FK_JCC8 ? (e->op & 15) : (e->op2 & 15)); e_if(B, 0);
      e_goto(B, t, k + 1);
      if (blk_of(B, next) == B->blk + 1) { e_end(B); e_icount(B, k + 1); }   /* not taken: fall into the next region block */
      else { e_else(B); e_goto(B, next, k + 1); e_end(B); }
      return 1; }
    case FK_JMP8: { u64 t = next + (u64)(s64)(s8)code[e->hdr]; if (m32) t = (u32)t; e_goto(B, t, k + 1); return 1; }
    case FK_JMP32: { u64 t = next + (u64)(s64)imm32_at(code, e->hdr); if (m32) t = (u32)t; e_goto(B, t, k + 1); return 1; }
    case FK_CALL: { u64 t = next + (u64)(s64)imm32_at(code, e->hdr); if (m32) t = (u32)t; e_push_begin(B, psz, m32, start); e_i64c(B, next); e_push_end(B, psz); e_exit_const(B, t, k + 1); return 1; }
    case FK_RET: e_pop(B, psz, m32, start); e_exit_top(B, k + 1); return 1;
    case FK_RET_N: { u32 n = code[e->hdr] | (code[e->hdr + 1] << 8); e_pop(B, psz, m32, start); e_lset(B, L_RET);
      e_lget(B, L_C); e_ldreg(B, RSP, 8); e_i64c(B, n); OP(I64_ADD); if (m32) { e_i64c(B, 0xffffffffull); OP(I64_AND); } e_st(B, 0x37, 3, CO_R(RSP));
      e_icount(B, k + 1); e_lget(B, L_RET); OP(RETURN); return 1; }

    /* ---- SSE (scalar/packed moves, arithmetic, logic, compares, conversions) ---- */
    case FK_SSE_MOV: { u8 op2 = e->op2; int bytes = (op2 == 0x28 || op2 == 0x29) ? 16 : e->rep == 0xF3 ? 4 : e->rep == 0xF2 ? 8 : 16; int load = op2 == 0x10 || op2 == 0x28;
      e_prep_mem(B, it, bytes, !load);
      if (load) {
        if (bytes == 16) { e_lget(B, L_C); e_xmm_src(B, e, 0, 8); e_st(B, 0x37, 3, CO_X(e->reg)); e_lget(B, L_C); e_xmm_src(B, e, 8, 8); e_st(B, 0x37, 3, CO_X(e->reg) + 8); return 1; }
        if (e->mod == 3) { e_lget(B, L_C); e_xmm_src(B, e, 0, bytes); e_st(B, bytes == 4 ? 0x3E : 0x37, bytes == 4 ? 2 : 3, CO_X(e->reg)); return 1; }   /* movss/movsd xmm, xmm: low lanes only */
        e_lget(B, L_C); e_xmm_src(B, e, 0, bytes); e_st(B, 0x37, 3, CO_X(e->reg)); e_lget(B, L_C); e_i64c(B, 0); e_st(B, 0x37, 3, CO_X(e->reg) + 8); return 1;   /* from memory: zero the rest */
      }
      if (bytes == 16) { e_xmm_dst(B, e, 0); e_lget(B, L_C); e_ld(B, 0x29, 3, CO_X(e->reg)); e_xmm_dst_store(B, e, 0, 8); e_xmm_dst(B, e, 8); e_lget(B, L_C); e_ld(B, 0x29, 3, CO_X(e->reg) + 8); e_xmm_dst_store(B, e, 8, 8); return 1; }
      e_xmm_dst(B, e, 0); e_lget(B, L_C); e_ld(B, bytes == 4 ? 0x35 : 0x29, bytes == 4 ? 2 : 3, CO_X(e->reg)); e_xmm_dst_store(B, e, 0, bytes); return 1; }
    case JK_MOVDQ: { int load = e->op2 == 0x6F; e_prep_mem(B, it, 16, !load);
      if (load) { e_lget(B, L_C); e_xmm_src(B, e, 0, 8); e_st(B, 0x37, 3, CO_X(e->reg)); e_lget(B, L_C); e_xmm_src(B, e, 8, 8); e_st(B, 0x37, 3, CO_X(e->reg) + 8); }
      else { e_xmm_dst(B, e, 0); e_lget(B, L_C); e_ld(B, 0x29, 3, CO_X(e->reg)); e_xmm_dst_store(B, e, 0, 8); e_xmm_dst(B, e, 8); e_lget(B, L_C); e_ld(B, 0x29, 3, CO_X(e->reg) + 8); e_xmm_dst_store(B, e, 8, 8); }
      return 1; }
    case JK_MOVQ_X: e_prep_mem(B, it, 8, 0); e_lget(B, L_C); e_xmm_src(B, e, 0, 8); e_st(B, 0x37, 3, CO_X(e->reg)); e_lget(B, L_C); e_i64c(B, 0); e_st(B, 0x37, 3, CO_X(e->reg) + 8); return 1;
    case JK_MOVQ_RM_X: e_prep_mem(B, it, 8, 1); e_xmm_dst(B, e, 0); e_lget(B, L_C); e_ld(B, 0x29, 3, CO_X(e->reg)); e_xmm_dst_store(B, e, 0, 8); if (e->mod == 3) { e_lget(B, L_C); e_i64c(B, 0); e_st(B, 0x37, 3, CO_X(e->rm) + 8); } return 1;
    case JK_MOVD_X_RM: e_prep_mem(B, it, sz, 0); e_lget(B, L_C); e_rm_load(B, e, sz); e_st(B, 0x37, 3, CO_X(e->reg)); e_lget(B, L_C); e_i64c(B, 0); e_st(B, 0x37, 3, CO_X(e->reg) + 8); return 1;
    case JK_MOVD_RM_X: e_prep_mem(B, it, sz, 1); e_dst_addr(B, e); e_lget(B, L_C); e_ld(B, sz == 8 ? 0x29 : 0x35, sz == 8 ? 3 : 2, CO_X(e->reg)); e_dst_store(B, e, sz); return 1;
    case FK_SSE_LOGIC: case JK_PLOGIC: { u8 op2 = e->op2; int kind = it->fk == JK_PLOGIC ? (op2 == 0xDB ? 0 : op2 == 0xDF ? 1 : op2 == 0xEB ? 2 : 3) : op2 - 0x54;   /* and andn or xor */
      e_prep_mem(B, it, 16, 0);
      for (int lane = 0; lane < 2; lane++) { e_lget(B, L_C); e_lget(B, L_C); e_ld(B, 0x29, 3, CO_X(e->reg) + lane * 8); if (kind == 1) { e_i64c(B, ~0ull); OP(I64_XOR); } e_xmm_src(B, e, lane * 8, 8); OP(kind == 0 || kind == 1 ? I64_AND : kind == 2 ? I64_OR : I64_XOR); e_st(B, 0x37, 3, CO_X(e->reg) + lane * 8); }
      return 1; }
    case FK_SSE_ARITH: case JK_SSE_MINMAX: { u8 op2 = e->op2; int sc = e->rep == 0xF3 ? 1 : e->rep == 0xF2 ? 2 : e->p66 ? 3 : 0; int dbl = sc >= 2, lanes = sc == 1 || sc == 2 ? 1 : sc == 3 ? 2 : 4, w = dbl ? 8 : 4;
      if (it->fk == JK_SSE_MINMAX && sc == 0) return 0;   /* packed min/max: interpreter */
      if (it->fk == JK_SSE_MINMAX && sc == 3) return 0;
      e_prep_mem(B, it, lanes * w, 0);
      for (int lane = 0; lane < lanes; lane++) {
        e_lget(B, L_C); e_lget(B, L_C); e_ld(B, dbl ? F64_LOAD : F32_LOAD, dbl ? 3 : 2, CO_X(e->reg) + lane * w);
        if (it->fk == JK_SSE_MINMAX) { if (!dbl) OP(F64_PROMOTE); e_lset(B, L_F0); e_xmm_fsrc(B, e, lane * w, dbl); if (!dbl) OP(F64_PROMOTE); e_lset(B, L_F1); e_lget(B, L_F0); e_lget(B, L_F1); e_lget(B, L_F0); e_lget(B, L_F1); OP(op2 == 0x5F ? F64_GT : F64_LT); OP(SELECT); if (!dbl) OP(F32_DEMOTE); }   /* unordered/equal → source operand, like the interpreter */
        else { e_xmm_fsrc(B, e, lane * w, dbl); OP(dbl ? (op2 == 0x58 ? F64_ADD : op2 == 0x59 ? F64_MUL : op2 == 0x5C ? F64_SUB : F64_DIV) : (op2 == 0x58 ? F32_ADD : op2 == 0x59 ? F32_MUL : op2 == 0x5C ? F32_SUB : F32_DIV)); }
        e_st(B, dbl ? F64_STORE : F32_STORE, dbl ? 3 : 2, CO_X(e->reg) + lane * w);
      }
      return 1; }
    case JK_SQRT: { int dbl = e->rep == 0xF2; e_prep_mem(B, it, dbl ? 8 : 4, 0); e_lget(B, L_C); e_xmm_fsrc(B, e, 0, dbl); OP(dbl ? F64_SQRT : F32_SQRT); e_st(B, dbl ? F64_STORE : F32_STORE, dbl ? 3 : 2, CO_X(e->reg)); return 1; }
    case JK_CVT5A: { int to_dbl = e->rep == 0xF3; e_prep_mem(B, it, to_dbl ? 4 : 8, 0); e_lget(B, L_C); e_xmm_fsrc(B, e, 0, !to_dbl); OP(to_dbl ? F64_PROMOTE : F32_DEMOTE); e_st(B, to_dbl ? F64_STORE : F32_STORE, to_dbl ? 3 : 2, CO_X(e->reg)); return 1; }
    case JK_CVTSI2SD: { int dbl = e->rep == 0xF2; e_prep_mem(B, it, sz, 0); e_lget(B, L_C);
      if (e->mod == 3) { e_lget(B, L_C); e_ld(B, sz == 8 ? 0x29 : 0x34, sz == 8 ? 3 : 2, CO_R(e->rm)); } else { e_lget(B, L_HOST); e_ld(B, sz == 8 ? 0x29 : 0x34, 0, 0); }
      OP(dbl ? F64_CVT_I64S : F32_CVT_I64S); e_st(B, dbl ? F64_STORE : F32_STORE, dbl ? 3 : 2, CO_X(e->reg)); return 1; }
    case JK_CVTSD2SI: { int dbl = e->rep == 0xF2, trunc = e->op2 == 0x2C; e_prep_mem(B, it, dbl ? 8 : 4, 0);
      e_xmm_fsrc(B, e, 0, dbl); if (!dbl) OP(F64_PROMOTE); e_lset(B, L_F0);
      e_lget(B, L_C);
      e_lget(B, L_F0); e_lget(B, L_F0); OP(F64_NE); e_lget(B, L_F0); e_f64c(B, sz == 8 ? 9223372036854775808.0 : 2147483648.0); OP(F64_GE); OP(I32_OR); e_lget(B, L_F0); e_f64c(B, sz == 8 ? -9223372036854775808.0 : -2147483648.0); OP(F64_LT); OP(I32_OR);
      e_if(B, 0x7E); e_i64c(B, sz == 8 ? 0x8000000000000000ull : 0x80000000ull); e_else(B); e_lget(B, L_F0); if (!trunc) OP(F64_NEAREST); if (sz == 8) OP(I64_TRUNC_F64S); else { OP(I32_TRUNC_F64S); OP(I64_EXT32_U); } e_end(B);
      e_st(B, 0x37, 3, CO_R(e->reg)); return 1; }
    case FK_SSE_CMP: { int dbl = e->p66; e_prep_mem(B, it, dbl ? 8 : 4, 0);
      e_lget(B, L_C); e_ld(B, dbl ? F64_LOAD : F32_LOAD, dbl ? 3 : 2, CO_X(e->reg)); if (!dbl) OP(F64_PROMOTE); e_lset(B, L_F0);
      e_xmm_fsrc(B, e, 0, dbl); if (!dbl) OP(F64_PROMOTE); e_lset(B, L_F1);
      if (!need) return 1;
      e_lget(B, L_C); e_lget(B, L_C); e_ld(B, 0x29, 3, CO_RFLAGS); e_i64c(B, ~(u64)(F_ZF | F_PF | F_CF | F_OF | F_SF | F_AF)); OP(I64_AND);
      e_lget(B, L_F0); e_lget(B, L_F0); OP(F64_NE); e_lget(B, L_F1); e_lget(B, L_F1); OP(F64_NE); OP(I32_OR); OP(I64_EXT_U); e_i64c(B, F_ZF | F_PF | F_CF); OP(I64_MUL); OP(I64_OR);   /* unordered → ZF|PF|CF */
      e_lget(B, L_F0); e_lget(B, L_F1); OP(F64_EQ); OP(I64_EXT_U); e_i64c(B, 6); OP(I64_SHL); OP(I64_OR);
      e_lget(B, L_F0); e_lget(B, L_F1); OP(F64_LT); OP(I64_EXT_U); OP(I64_OR);
      e_st(B, 0x37, 3, CO_RFLAGS); return 1; }
    default: return 0;
  }
}

/* module-local TLB probe: (c, ea, size, write, rip) -> host pointer or 0 (jit_mem handles the miss / fault) */
static void e_tlb_func(Buf *B) {
  bu(B, 1); bu(B, 2); OP(0x7E);   /* locals 5 = pg, 6 = entry */
  e_lget(B, 1); e_i64c(B, 12); OP(I64_SHR_U); e_lset(B, 5);
  e_lget(B, 0); e_ld(B, 0x29, 3, CO_MEM); e_lget(B, 5); e_i64c(B, 4095); OP(I64_AND); e_i64c(B, 4); OP(I64_SHL); OP(I64_ADD); e_ltee(B, 6);
  e_ld(B, 0x29, 3, MO_TLB); e_lget(B, 5); e_i64c(B, 1); OP(I64_ADD); OP(I64_EQ);
  e_lget(B, 1); e_i64c(B, 4095); OP(I64_AND); e_lget(B, 2); OP(I64_ADD); e_i64c(B, 4096); OP(I64_LE_U); OP(I32_AND);
  e_if(B, 0x7E);
    e_lget(B, 6); e_ld(B, 0x29, 3, MO_TLB + 8); e_lget(B, 1); e_i64c(B, 4095); OP(I64_AND); OP(I64_ADD);
  e_else(B);
    e_lget(B, 0); e_lget(B, 1); e_lget(B, 2); e_lget(B, 3); e_lget(B, 4); OP(CALL); bu(B, FN_MEM);
  e_end(B);
  OP(0x0B);
}

/* ---- region translation ---- */
static u8 g_jit_body[65536], g_jit_tlb[256], g_jit_mod[65536 + 1024];
typedef struct { u64 start, end; int first, n; u8 term; } RBlk;   /* instructions it[first .. first+n), term: ends with a control transfer */
static int dc_valid(Mem *m, u32 gen, int m32, u64 a, const u8 *code, Dc **out) {
  Dc *e = DC_SLOT(a);
  if (!(e->rip == a && e->mem == m && e->gen == gen && e->m32 == m32 && e->len)) return 0;
  u8 tmp[16] = {0}; __builtin_memcpy(tmp, code, e->len); u64 w0, w1; __builtin_memcpy(&w0, tmp, 8); __builtin_memcpy(&w1, tmp + 8, 8);
  if ((w0 & e->m0) != e->b0 || (w1 & e->m1) != e->b1) return 0;
  *out = e; return 1;
}
/* static successors of a terminator (0, 1 or 2); -1 entries = none */
static int term_targets(const Dc *e, u64 start, const u8 *code, int m32, u64 t[2]) {
  u64 next = start + e->len; if (m32) next = (u32)next; int n = 0;
  switch (e->fast) {
    case FK_JCC8: t[n++] = next + (u64)(s64)(s8)code[e->hdr]; t[n++] = next; break;
    case FK_JCC32: t[n++] = next + (u64)(s64)imm32_at(code, e->hdr + 1); t[n++] = next; break;
    case FK_JMP8: t[n++] = next + (u64)(s64)(s8)code[e->hdr]; break;
    case FK_JMP32: t[n++] = next + (u64)(s64)imm32_at(code, e->hdr); break;
    case FK_CALL: t[n++] = next; break;   /* the callee is its own region; the return lands here */
    default: break;
  }
  if (m32) for (int i = 0; i < n; i++) t[i] = (u32)t[i];
  return n;
}
static void sort_u64(u64 *v, int n) { for (int i = 1; i < n; i++) { u64 x = v[i]; int j = i - 1; while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; } v[j + 1] = x; } }
static void jit_drop(Jb *b);
static int jit_translate(Cpu *c, Jb *b0) {
  Mem *m = c->mem; u32 gen = mem_gen(m); int m32 = c->mode32; u64 rip = b0->rip;
  Region *reg = mem_region(m, rip); if (!reg) return 0;
  u64 lo = reg->base, hi = reg->base + reg->size; const u8 *rbase = reg->host;
  /* 1. discover block starts reachable through direct branches (only code that has been executed: it has Dc entries) */
  static u64 starts[JIT_MAX_BLOCKS + 8]; int ns = 0; starts[ns++] = rip;
  static JItem it[JIT_MAX_REGION_INSNS]; int nit = 0;
  static RBlk blk[JIT_MAX_BLOCKS]; int nb = 0;
  for (int pass = 0; pass < 4; pass++) {
    sort_u64(starts, ns); { int w = 1; for (int i = 1; i < ns; i++) if (starts[i] != starts[w - 1]) starts[w++] = starts[i]; ns = w; }
    nit = 0; nb = 0; int added = 0;
    for (int bi = 0; bi < ns && nb < JIT_MAX_BLOCKS; bi++) {
      u64 a = starts[bi], lim = bi + 1 < ns ? starts[bi + 1] : ~0ull; RBlk *B = &blk[nb]; B->start = a; B->first = nit; B->n = 0; B->term = 0;
      while (a < lim && nit < JIT_MAX_REGION_INSNS) {
        if (a < lo || a >= hi) break; Dc *e; if (!dc_valid(m, gen, m32, a, rbase + (a - lo), &e)) break;
        if (a + e->len > hi) break;
        it[nit].e = e; it[nit].rip = a; it[nit].need = 0; jit_classify(&it[nit], rbase + (a - lo)); nit++; B->n++; a += e->len; if (m32) a = (u32)a;
        if (it[nit - 1].kind == K_TERM) { B->term = 1;
          u64 t[2]; int nt = term_targets(e, it[nit - 1].rip, rbase + (it[nit - 1].rip - lo), m32, t);
          for (int q = 0; q < nt; q++) { int known = 0; for (int z = 0; z < ns; z++) if (starts[z] == t[q]) { known = 1; break; }
            if (!known && ns < JIT_MAX_BLOCKS + 8 && t[q] >= lo && t[q] < hi) { Dc *te; if (dc_valid(m, gen, m32, t[q], rbase + (t[q] - lo), &te)) { starts[ns++] = t[q]; added = 1; } } }
          break; }
      }
      B->end = a;
      if (B->n == 0) { if (bi == 0) return 0; continue; }   /* a start with no decodable instruction: not a block (branch target never executed) */
      nb++;
    }
    if (!added) break;
  }
  if (nb == 0) return 0;
  /* a terminator's fall-through to the very next block is only free when the blocks are adjacent — they are, by construction (sorted, cut at the next start) */
  static u64 bstarts[JIT_MAX_BLOCKS]; for (int i = 0; i < nb; i++) bstarts[i] = blk[i].start;
  /* flags liveness per block (backwards; everything live at a block end) */
  for (int i = 0; i < nb; i++) { int live = FL_ALL; for (int q = blk[i].first + blk[i].n - 1; q >= blk[i].first; q--) { it[q].need = (u8)(it[q].fdef & live); live = (live & ~it[q].fdef) | it[q].fuse; } }
  Buf body = { g_jit_body, 0, sizeof g_jit_body, 0, bstarts, nb, 0, m32 }; Buf *B = &body;
  bu(B, 2); bu(B, 7); OP(0x7E); bu(B, 2); OP(0x7C);   /* locals 4..10: i64, 11..12: f64 */
  /* code bytes of every block must still be what we translated (self-modifying / regenerated code → stale) */
  for (int i = 0; i < nb; i++) for (u64 off = blk[i].start; off < blk[i].end; off += 8) {
    u32 nbyt = blk[i].end - off < 8 ? (u32)(blk[i].end - off) : 8; u64 w = 0; __builtin_memcpy(&w, rbase + (off - lo), nbyt); u64 msk = nbyt == 8 ? ~0ull : (1ull << (nbyt * 8)) - 1;
    e_lget(B, L_CODE); e_ld(B, 0x29, 0, off - lo); if (nbyt < 8) { e_i64c(B, msk); OP(I64_AND); } e_i64c(B, w); OP(I64_NE);
    e_if(B, 0); e_i64c(B, 1); OP(RETURN); e_end(B);
  }
  /* layout:  block $fault  loop $dispatch  block $b(nb-1) … block $b0  br_table pc  end $b0 CODE0 end $b1 CODE1 … end $b(nb-1) CODE(nb-1)  end $dispatch  end $fault  <fault exit> */
  OP(0x02); OP(0x40); OP(0x03); OP(0x40); for (int i = 0; i < nb; i++) { OP(0x02); OP(0x40); }
  e_lget(B, L_PC); OP(BR_TABLE); bu(B, nb); for (int i = 0; i < nb; i++) bu(B, i); bu(B, nb - 1);
  int steps = 0; B->depth = 0;
  for (int i = 0; i < nb; i++) {
    OP(0x0B);   /* end $b_i → CODE_i */
    B->blk = i; int k = 0;
    for (int q = blk[i].first; q < blk[i].first + blk[i].n; q++) {
      const Dc *e = it[q].e; u64 next = it[q].rip + e->len; if (m32) next = (u32)next;
      int done = 0;
      if (it[q].kind != K_STEP) { u32 save = B->n; int d = B->depth; done = e_insn(B, &it[q], rbase + (it[q].rip - lo), m32, k); if (!done) { B->n = save; B->depth = d; } }
      if (done) k++;
      else { e_step(B, it[q].rip, next, k); steps++; if (it[q].kind == K_TERM) e_goto(B, next, k); }
    }
    if (!blk[i].term) { if (i + 1 < nb && blk[i + 1].start == blk[i].end) e_icount(B, k); else e_exit_const(B, blk[i].end, k); }
  }
  OP(0x0B);   /* end $dispatch (unreachable) */
  OP(0x0B);   /* end $fault */
  e_lget(B, L_C); e_ld(B, 0x29, 3, CO_RIP); OP(RETURN);
  OP(0x0B);   /* end of function */
  if (B->n > B->cap || B->depth != 0) return 0;
  Buf tlb = { g_jit_tlb, 0, sizeof g_jit_tlb, 0, 0, 0, 0, 0 }; e_tlb_func(&tlb);
  /* module: types, imports (memory + helpers), functions, export, code */
  Buf mod = { g_jit_mod, 0, sizeof g_jit_mod, 0, 0, 0, 0, 0 }; B = &mod;
  bbytes(B, "\0asm\1\0\0\0", 8);
  { Buf s = { g_jit_mod + 30000, 0, 4096, 0, 0, 0, 0, 0 }; Buf *S = &s;   /* scratch for section payloads (overwritten later by the code section) */
    #define SECTION(id) do { OP(id); bu(B, S->n); bbytes(B, S->b, S->n); S->n = 0; } while (0)
    /* types: 0 (i64,i64,i32,i64)->i64 region   1 (i64,i64,i64,i32,i64)->i64 mem/tlb   2 (i64,i32,i64,i64,i32)->i64 alu   3 (i64,i32,i64,i32,i32)->i64 shift   4 (i64,i64)->i64 step */
    bu(S, 5);
    b8(S, 0x60); bu(S, 4); b8(S, 0x7E); b8(S, 0x7E); b8(S, 0x7F); b8(S, 0x7E); bu(S, 1); b8(S, 0x7E);
    b8(S, 0x60); bu(S, 5); b8(S, 0x7E); b8(S, 0x7E); b8(S, 0x7E); b8(S, 0x7F); b8(S, 0x7E); bu(S, 1); b8(S, 0x7E);
    b8(S, 0x60); bu(S, 5); b8(S, 0x7E); b8(S, 0x7F); b8(S, 0x7E); b8(S, 0x7E); b8(S, 0x7F); bu(S, 1); b8(S, 0x7E);
    b8(S, 0x60); bu(S, 5); b8(S, 0x7E); b8(S, 0x7F); b8(S, 0x7E); b8(S, 0x7F); b8(S, 0x7F); bu(S, 1); b8(S, 0x7E);
    b8(S, 0x60); bu(S, 2); b8(S, 0x7E); b8(S, 0x7E); bu(S, 1); b8(S, 0x7E);
    SECTION(1);
    bu(S, 5);
    bu(S, 3); bbytes(S, "env", 3); bu(S, 6); bbytes(S, "memory", 6); b8(S, 0x02); b8(S, 0x04); bu(S, 1);   /* memory64, min 1 page, no max */
    static const char *const names[4] = { "jit_step", "jit_mem", "jit_alu", "jit_shift" }; static const u8 types[4] = { 4, 1, 2, 3 };
    for (int i = 0; i < 4; i++) { bu(S, 3); bbytes(S, "env", 3); u32 L = 0; while (names[i][L]) L++; bu(S, L); bbytes(S, names[i], L); b8(S, 0x00); bu(S, types[i]); }
    SECTION(2);
    bu(S, 2); bu(S, 0); bu(S, 1); SECTION(3);   /* f: type 0, tlb: type 1 */
    bu(S, 1); bu(S, 1); bbytes(S, "f", 1); b8(S, 0x00); bu(S, FN_BLOCK); SECTION(7);
    #undef SECTION
  }
  { u32 lb = body.n < 128 ? 1 : body.n < 16384 ? 2 : 3, lt = tlb.n < 128 ? 1 : 2;
    OP(10); bu(B, 1 + lb + body.n + lt + tlb.n); bu(B, 2); bu(B, body.n); bbytes(B, body.b, body.n); bu(B, tlb.n); bbytes(B, tlb.b, tlb.n); }
  if (B->n > B->cap) return 0;
  u32 reuse = g_jit_nfree ? g_jit_free[--g_jit_nfree] : 0;
  u32 fn = host_jit_compile(mod.b, mod.n, reuse);
  if (!fn || fn >= JIT_MAX_FN) { g_jit_st.fails++; if (reuse) g_jit_free[g_jit_nfree++] = reuse; return 0; }
  /* publish every block start as an entry (the hot one first so b0 is certainly set) */
  for (int i = 0; i < nb; i++) {
    Jb *b = JB_SLOT(blk[i].start);
    if (b != b0) { if (b->rip == blk[i].start && b->mem == m && b->m32 == m32 && b->fn && b->gen == gen) continue; if (b->fn) jit_drop(b); }
    b->rip = blk[i].start; b->mem = m; b->m32 = m32; b->fn = fn; b->pc = (u16)i; b->code = rbase; b->gen = gen; b->count = JIT_HOT; g_fnref[fn]++;
  }
  g_jit_st.blocks++; g_jit_st.insns += nit; g_jit_st.steps += steps; g_jit_st.bytes += mod.n;
  return b0->fn != 0;
}
static void jit_drop(Jb *b) { if (b->fn && g_fnref[b->fn] && --g_fnref[b->fn] == 0 && g_jit_nfree < 4096) g_jit_free[g_jit_nfree++] = b->fn; b->fn = 0; b->rip = 0; b->count = 0; }
typedef u64 (*JitFn)(Cpu *c, const u8 *code, int pc, u64 limit);
/* called by cpu_run at a block start: runs translated blocks back to back while they chain; returns 1 if any block ran */
static inline int jit_enter(Cpu *c, u64 budget) {
  int ran = 0; u64 ic0 = c->icount;
  for (;;) {
    Jb *b = JB_SLOT(c->rip);
    if (b->rip != c->rip || b->mem != c->mem || b->m32 != c->mode32) {
      if (b->fn) jit_drop(b);
      b->rip = c->rip; b->mem = c->mem; b->m32 = c->mode32; b->count = 1; b->fn = 0; return ran;
    }
    if (!b->fn) {
      if (++b->count < JIT_HOT) return ran;
      if (!jit_translate(c, b)) { b->count = -2000000; g_jit_st.nodc++; return ran; }
    } else if (b->gen != mem_gen(c->mem)) { jit_drop(b); return ran; }
    c->hist[c->hist_i++ & 4095] = c->rip;
    u64 nx = ((JitFn)(size_t)b->fn)(c, b->code, b->pc, ic0 + budget);
    if (nx == 1) { g_jit_st.stale++; jit_drop(b); return ran; }
    g_jit_st.exec++; ran = 1;
    c->rip = nx;
    if (c->status != RUN_OK || c->icount - ic0 >= budget || nx - G_SHIM_BASE < G_SHIM_SIZE) return 1;
  }
}
const char *jit_stats_text(void) {
  static char s[640];
  e_snprintf(s, sizeof s, "blocks=%llu exec=%llu insns=%llu steps=%llu bytes=%llu stale=%llu nodc=%llu fails=%llu dsteps=%llu dinterp=%llu", (unsigned long long)g_jit_st.blocks, (unsigned long long)g_jit_st.exec, (unsigned long long)g_jit_st.insns,
             (unsigned long long)g_jit_st.steps, (unsigned long long)g_jit_st.bytes, (unsigned long long)g_jit_st.stale, (unsigned long long)g_jit_st.nodc, (unsigned long long)g_jit_st.fails, (unsigned long long)g_jit_st.dsteps, (unsigned long long)g_jit_st.dinterp);
  /* top step opcodes */
  { int L = 0; while (s[L]) L++; for (int pass = 0; pass < 12 && L < (int)sizeof s - 40; pass++) { int best = -1; u32 bv = 0; for (int k = 0; k < 1024; k++) if (g_step_hist[k] > bv) { bv = g_step_hist[k]; best = k; } if (best < 0 || !bv) break;
      L += e_snprintf(s + L, sizeof s - L, " %s%s%02x:%u", best >= 512 ? "p" : "", (best & 511) >= 256 ? "0f" : "", best & 255, bv); g_step_hist[best] = 0; } }
  return s;
}
