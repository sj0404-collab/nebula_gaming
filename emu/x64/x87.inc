/* x87.inc — x87 FPU subset, included by cpu.c.
   Registers are kept as IEEE doubles (64-bit); 80-bit loads/stores are converted. That is enough for what 64-bit
   Windows code still uses the FPU for: mingw's own printf/strtod (long double formatting), `long double` arithmetic
   in CRT helpers, and legacy codecs. Precision beyond 53 bits is NOT emulated (documented limit). */

#define FTOP (c->ftop & 7)
#define ST(n) c->fp[(c->ftop + (n)) & 7]
#define F_C0 0x0100
#define F_C1 0x0200
#define F_C2 0x0400
#define F_C3 0x4000
static inline void fpush(Cpu *c, double v) { c->ftop = (c->ftop - 1) & 7; c->fp[c->ftop] = v; c->ftag |= 1u << c->ftop; c->fxk[c->ftop] = 0; }
static inline double fpop(Cpu *c) { double v = c->fp[c->ftop]; c->ftag &= ~(1u << c->ftop); c->fxk[c->ftop] = 0; c->ftop = (c->ftop + 1) & 7; return v; }
/* exact-copy shadow: fild m64 → fistp m64 and fld m80 → fstp m80 must reproduce every bit (Delphi's Move copies 8-byte blocks through
   fild/fistp; a 53-bit double would round the low bits of ASCII text into neighbouring letters). Any arithmetic drops the shadow. */
#define FXI(n) ((c->ftop + (n)) & 7)
static inline void fx_clear_all(Cpu *c) { for (int k = 0; k < 8; k++) c->fxk[k] = 0; }
static inline void fx_copy(Cpu *c, int dst, int src) { c->fxk[dst] = c->fxk[src]; c->fxm[dst] = c->fxm[src]; c->fxse[dst] = c->fxse[src]; }
static inline void fx_swap(Cpu *c, int a, int b) { u8 k = c->fxk[a]; u64 m = c->fxm[a]; u16 s = c->fxse[a]; fx_copy(c, a, b); c->fxk[b] = k; c->fxm[b] = m; c->fxse[b] = s; }
static void i64_to_f80(s64 v, u64 *mant, u16 *se) { if (!v) { *mant = 0; *se = 0; return; } int sign = v < 0; u64 m = sign ? (u64)(-(v + 1)) + 1 : (u64)v; int e = 63; while (!(m >> 63)) { m <<= 1; e--; } *mant = m; *se = (u16)((sign << 15) | (e + 16383)); }
static int f80_is_i64(u64 mant, u16 se, s64 *out) {   /* an 80-bit value that is exactly a 64-bit integer */
  int e = (se & 0x7fff) - 16383; if (!(mant >> 63) || e < 0 || e > 63) return (mant == 0 && (se & 0x7fff) == 0) ? (*out = 0, 1) : 0;
  int sh = 63 - e; if (sh && (mant << (64 - sh))) return 0; u64 v = mant >> sh; if (se & 0x8000) { if (v > 0x8000000000000000ull) return 0; *out = (s64)(0 - v); return 1; } if (v >> 63) return 0; *out = (s64)v; return 1; }
static double pow2d(int e) { double r = 1.0; while (e > 1000) { r *= 1.0715086071862673e301; e -= 1000; } while (e < -1000) { r *= 9.332636185032189e-302; e += 1000; } u64 bits = (u64)(e + 1023) << 52; double s; e_memcpy(&s, &bits, 8); return r * s; }
static double f80_to_double(u64 mant, u16 se) {
  int sign = se >> 15, e = se & 0x7fff;
  if (e == 0 && mant == 0) return sign ? -0.0 : 0.0;
  if (e == 0x7fff) { if (mant << 1) return 0.0 / 0.0; return sign ? -1.0 / 0.0 : 1.0 / 0.0; }
  double r = (double)mant * pow2d(e - 16383 - 63);   /* u64→double rounds to 53 bits */
  return sign ? -r : r;
}
static void double_to_f80(double v, u64 *mant, u16 *se) {
  u64 bits; e_memcpy(&bits, &v, 8); int sign = (int)(bits >> 63); int e = (int)((bits >> 52) & 0x7ff); u64 frac = bits & 0xfffffffffffffull;
  if (e == 0 && frac == 0) { *mant = 0; *se = (u16)(sign << 15); return; }
  if (e == 0x7ff) { *se = (u16)((sign << 15) | 0x7fff); *mant = frac ? (0xc000000000000000ull | (frac << 11)) : 0x8000000000000000ull; return; }
  if (e == 0) { /* subnormal */ int sh = 0; while (!(frac & (1ull << 52))) { frac <<= 1; sh++; } e = 1 - sh; }
  *mant = (frac << 11) | 0x8000000000000000ull; *se = (u16)((sign << 15) | (e - 1023 + 16383));
}
static double x87_ld(Cpu *c, u64 a, int kind) {   /* 0 f32, 1 f64, 2 i16, 3 i32, 4 i64, 5 f80 */
  switch (kind) {
    case 0: { u32 b = rd32(c, a); float f; e_memcpy(&f, &b, 4); return f; }
    case 1: { u64 b = rd64(c, a); double d; e_memcpy(&d, &b, 8); return d; }
    case 2: return (double)(s16)rd16(c, a);
    case 3: return (double)(s32)rd32(c, a);
    case 4: return (double)(s64)rd64(c, a);
    default: return f80_to_double(rd64(c, a), rd16(c, a + 8));
  }
}
static double x87_round(Cpu *c, double v) {
  int rc = (c->fpu_cw >> 10) & 3;   /* 0 nearest-even, 1 down, 2 up, 3 truncate */
  double f = e_floor(v);
  switch (rc) { case 1: return f; case 2: return e_ceil(v); case 3: return v < 0 ? e_ceil(v) : f;
    default: { double d = v - f; if (d > 0.5) return f + 1; if (d < 0.5) return f; return e_fmod(f, 2.0) == 0 ? f : f + 1; } }
}
static void x87_st(Cpu *c, u64 a, int kind, double v) {
  switch (kind) {
    case 0: { float f = (float)v; u32 b; e_memcpy(&b, &f, 4); wr32(c, a, b); break; }
    case 1: { u64 b; e_memcpy(&b, &v, 8); wr64(c, a, b); break; }
    case 2: { double r = x87_round(c, v); wr16(c, a, (r != r || r > 32767 || r < -32768) ? 0x8000 : (u16)(s16)r); break; }
    case 3: { double r = x87_round(c, v); wr32(c, a, (r != r || r > 2147483647.0 || r < -2147483648.0) ? 0x80000000u : (u32)(s32)r); break; }
    case 4: { double r = x87_round(c, v); wr64(c, a, (r != r || r >= 9223372036854775808.0 || r < -9223372036854775808.0) ? 0x8000000000000000ull : (u64)(s64)r); break; }
    case 6: { wr16(c, a, (u16)(s16)__builtin_trunc(v)); break; }   /* fisttp variants: truncate */
    case 7: { wr32(c, a, (u32)(s32)__builtin_trunc(v)); break; }
    case 8: { wr64(c, a, (u64)(s64)__builtin_trunc(v)); break; }
    default: { u64 m; u16 se; double_to_f80(v, &m, &se); wr64(c, a, m); wr16(c, a + 8, se); }
  }
}
static void x87_compare(Cpu *c, double a, double b, int to_eflags) {
  int unordered = (a != a) || (b != b);
  if (to_eflags) { c->rflags &= ~(u64)(F_ZF | F_PF | F_CF); if (unordered) c->rflags |= F_ZF | F_PF | F_CF; else if (a < b) c->rflags |= F_CF; else if (a == b) c->rflags |= F_ZF; }
  else { c->fsw &= ~(F_C0 | F_C2 | F_C3); if (unordered) c->fsw |= F_C0 | F_C2 | F_C3; else if (a < b) c->fsw |= F_C0; else if (a == b) c->fsw |= F_C3; }
}
static double x87_arith(int sub, double a, double b) {   /* sub = modrm.reg: 0 add 1 mul 4 sub 5 subr 6 div 7 divr */
  switch (sub) { case 0: return a + b; case 1: return a * b; case 4: return a - b; case 5: return b - a; case 6: return a / b; default: return b / a; }
}
static int x87_cmov_ok(Cpu *c, int cc) {   /* DA: b, e, be, u ; DB: nb, ne, nbe, nu */
  int cf = !!(c->rflags & F_CF), zf = !!(c->rflags & F_ZF), pf = !!(c->rflags & F_PF);
  switch (cc) { case 0: return cf; case 1: return zf; case 2: return cf || zf; case 3: return pf; case 4: return !cf; case 5: return !zf; case 6: return !cf && !zf; default: return !pf; }
}
static int x87_reg(Cpu *c, u8 op, int sub, int sti);
/* instructions that preserve the exact-copy shadow (everything else drops it): loads/stores/moves that do no arithmetic */
static inline int x87_keep(u8 op, int sub, int isreg) {
  return !isreg ? ((op == 0xDF && (sub == 5 || sub == 7)) || (op == 0xDB && (sub == 5 || sub == 7)) || (op == 0xDD && (sub == 0 || sub == 2 || sub == 3)) || (op == 0xD9 && (sub == 0 || sub == 2 || sub == 3 || sub == 5 || sub == 7)))
                : ((op == 0xD9 && sub < 2) || (op == 0xDD && (sub == 2 || sub == 3)) || (op == 0xDF && sub == 4));   /* fld st / fxch / fst(p) st / fnstsw ax */
}
static void exec_x87(Ins *i, Cpu *c, u8 op) {
  decode_modrm(i); int sub = i->reg & 7; int sti = i->rm & 7;
  if (!x87_keep(op, sub, i->mod == 3)) fx_clear_all(c);
  if (i->mod != 3) {
    u64 a = EA(i);
    switch (op) {
      case 0xD8: { double m = x87_ld(c, a, 0); if (sub == 2 || sub == 3) { x87_compare(c, ST(0), m, 0); if (sub == 3) fpop(c); } else ST(0) = x87_arith(sub, ST(0), m); return; }
      case 0xDC: { double m = x87_ld(c, a, 1); if (sub == 2 || sub == 3) { x87_compare(c, ST(0), m, 0); if (sub == 3) fpop(c); } else ST(0) = x87_arith(sub, ST(0), m); return; }
      case 0xDA: { double m = x87_ld(c, a, 3); if (sub == 2 || sub == 3) { x87_compare(c, ST(0), m, 0); if (sub == 3) fpop(c); } else ST(0) = x87_arith(sub, ST(0), m); return; }
      case 0xDE: { double m = x87_ld(c, a, 2); if (sub == 2 || sub == 3) { x87_compare(c, ST(0), m, 0); if (sub == 3) fpop(c); } else ST(0) = x87_arith(sub, ST(0), m); return; }
      case 0xD9: switch (sub) {
        case 0: fpush(c, x87_ld(c, a, 0)); return;                 /* fld m32 */
        case 2: x87_st(c, a, 0, ST(0)); return;                    /* fst m32 */
        case 3: x87_st(c, a, 0, fpop(c)); return;                  /* fstp m32 */
        case 4: { c->fpu_cw = rd16(c, a); c->fsw = rd16(c, a + 4); return; }   /* fldenv (28 bytes; we keep cw/sw) */
        case 5: c->fpu_cw = rd16(c, a); return;                    /* fldcw */
        case 6: { for (int k = 0; k < 28; k += 4) wr32(c, a + k, 0); wr16(c, a, c->fpu_cw); wr16(c, a + 4, (u16)(c->fsw | (FTOP << 11))); wr16(c, a + 8, 0xffff); return; }   /* fnstenv */
        case 7: wr16(c, a, c->fpu_cw); return;                     /* fnstcw */
        default: break; } break;
      case 0xDB: switch (sub) {
        case 0: fpush(c, x87_ld(c, a, 3)); return;                 /* fild m32 */
        case 1: x87_st(c, a, 7, fpop(c)); return;                  /* fisttp m32 */
        case 2: x87_st(c, a, 3, ST(0)); return;                    /* fist m32 */
        case 3: x87_st(c, a, 3, fpop(c)); return;                  /* fistp m32 */
        case 5: { u64 m = rd64(c, a); u16 se = rd16(c, a + 8); fpush(c, f80_to_double(m, se)); c->fxk[FXI(0)] = 2; c->fxm[FXI(0)] = m; c->fxse[FXI(0)] = se; return; }   /* fld m80 (exact image kept) */
        case 7: { int k = c->fxk[FXI(0)]; u64 m = c->fxm[FXI(0)]; u16 se = c->fxse[FXI(0)]; double v = fpop(c); if (k == 1) i64_to_f80((s64)m, &m, &se); if (k) { wr64(c, a, m); wr16(c, a + 8, se); return; } x87_st(c, a, 5, v); return; }   /* fstp m80 */
        default: break; } break;
      case 0xDD: switch (sub) {
        case 0: fpush(c, x87_ld(c, a, 1)); return;                 /* fld m64 */
        case 1: x87_st(c, a, 8, fpop(c)); return;                  /* fisttp m64 */
        case 2: x87_st(c, a, 1, ST(0)); return;                    /* fst m64 */
        case 3: x87_st(c, a, 1, fpop(c)); return;                  /* fstp m64 */
        case 4: { c->fpu_cw = rd16(c, a); c->fsw = rd16(c, a + 4); c->ftop = (c->fsw >> 11) & 7; for (int k = 0; k < 8; k++) c->fp[(c->ftop + k) & 7] = x87_ld(c, a + 28 + k * 10, 5); c->ftag = 0xff; return; }   /* frstor */
        case 6: { for (int k = 0; k < 108; k += 4) wr32(c, a + k, 0); wr16(c, a, c->fpu_cw); wr16(c, a + 4, (u16)(c->fsw | (FTOP << 11))); for (int k = 0; k < 8; k++) x87_st(c, a + 28 + k * 10, 5, ST(k)); c->ftop = 0; c->ftag = 0; c->fsw = 0; c->fpu_cw = 0x37f; return; }   /* fnsave */
        case 7: wr16(c, a, (u16)(c->fsw | (FTOP << 11))); return;  /* fnstsw m16 */
        default: break; } break;
      case 0xDF: switch (sub) {
        case 0: fpush(c, x87_ld(c, a, 2)); return;                 /* fild m16 */
        case 1: x87_st(c, a, 6, fpop(c)); return;                  /* fisttp m16 */
        case 2: x87_st(c, a, 2, ST(0)); return;                    /* fist m16 */
        case 3: x87_st(c, a, 2, fpop(c)); return;                  /* fistp m16 */
        case 4: { /* fbld m80bcd */ u64 v = 0, m = 1; for (int k = 0; k < 9; k++) { u8 b = rd8(c, a + k); v += (u64)(b & 15) * m; m *= 10; v += (u64)(b >> 4) * m; m *= 10; } double d = (double)v; if (rd8(c, a + 9) & 0x80) d = -d; fpush(c, d); return; }
        case 6: { /* fbstp m80bcd: round to integer (current rounding mode), 18 packed digits + sign */ double d = fpop(c); double r = x87_round(c, d);
          int neg = r < 0; if (neg) r = -r; u64 v = r >= 1e18 ? 999999999999999999ull : (u64)r; for (int k = 0; k < 9; k++) { u8 lo = (u8)(v % 10); v /= 10; u8 hi = (u8)(v % 10); v /= 10; wr8(c, a + k, (u8)(lo | hi << 4)); } wr8(c, a + 9, neg ? 0x80 : 0); return; }
        case 5: { u64 m = rd64(c, a); fpush(c, (double)(s64)m); c->fxk[FXI(0)] = 1; c->fxm[FXI(0)] = m; return; }   /* fild m64 (exact i64 kept) */
        case 7: { int k = c->fxk[FXI(0)]; u64 m = c->fxm[FXI(0)]; u16 se = c->fxse[FXI(0)]; double v = fpop(c); s64 iv;
          if (k == 1) { wr64(c, a, m); return; } if (k == 2 && f80_is_i64(m, se, &iv)) { wr64(c, a, (u64)iv); return; } x87_st(c, a, 4, v); return; }   /* fistp m64 */
        default: break; } break;
    }
    UNSUP("x87 instruction %02X /%d (memory form)", op, sub);
  }
  if (x87_reg(c, op, sub, sti)) return;
  UNSUP("x87 instruction %02X /%d st(%d)", op, sub, sti);
}
/* register forms: 1 = executed, 0 = not an instruction (caller faults).  Shared by the interpreter and the JIT helper (jit_x87). */
static int x87_reg(Cpu *c, u8 op, int sub, int sti) {
  switch (op) {
    case 0xD8: if (sub == 2 || sub == 3) { x87_compare(c, ST(0), ST(sti), 0); if (sub == 3) fpop(c); } else ST(0) = x87_arith(sub, ST(0), ST(sti)); return 1;
    case 0xDC: if (sub == 2 || sub == 3) { x87_compare(c, ST(0), ST(sti), 0); if (sub == 3) fpop(c); } else { int s2 = sub == 4 ? 5 : sub == 5 ? 4 : sub == 6 ? 7 : sub == 7 ? 6 : sub; ST(sti) = x87_arith(s2, ST(sti), ST(0)); } return 1;   /* fsub st(i),st = st(i) - st */
    case 0xDE: if (sub == 3) { if (sti == 1) { x87_compare(c, ST(0), ST(1), 0); fpop(c); fpop(c); return 1; } break; }   /* fcompp */
      { int s2 = sub == 4 ? 5 : sub == 5 ? 4 : sub == 6 ? 7 : sub == 7 ? 6 : sub; ST(sti) = x87_arith(s2, ST(sti), ST(0)); fpop(c); return 1; }   /* faddp/fmulp/fsubrp/fsubp/fdivrp/fdivp */
    case 0xD9: switch (sub) {
      case 0: { int src = FXI(sti); fpush(c, ST(sti)); fx_copy(c, FXI(0), src); return 1; }   /* fld st(i) */
      case 1: { double t = ST(0); ST(0) = ST(sti); ST(sti) = t; fx_swap(c, FXI(0), FXI(sti)); return 1; }   /* fxch */
      case 2: if (sti == 0) return 1; break;                         /* fnop */
      case 3: ST(sti) = ST(0); fpop(c); return 1;                    /* fstp st(i) (undocumented alias) */
      case 4: switch (sti) {
        case 0: ST(0) = -ST(0); return 1;                            /* fchs */
        case 1: ST(0) = e_fabs(ST(0)); return 1;                     /* fabs */
        case 4: x87_compare(c, ST(0), 0.0, 0); return 1;             /* ftst */
        case 5: { double v = ST(0); c->fsw &= ~(F_C0 | F_C1 | F_C2 | F_C3); u64 b; e_memcpy(&b, &v, 8); if (b >> 63) c->fsw |= F_C1;   /* fxam */
          if (!(c->ftag & (1u << FTOP))) c->fsw |= F_C0 | F_C3; else if (v != v) c->fsw |= F_C0; else if (v == 0) c->fsw |= F_C3; else if (v * 0 != 0) c->fsw |= F_C0 | F_C2; else if (e_fabs(v) < 2.2250738585072014e-308) c->fsw |= F_C2 | F_C3; else c->fsw |= F_C2; return 1; }
        default: break; } break;
      case 5: { static const double k[8] = { 1.0, 3.321928094887362, 1.4426950408889634, 3.141592653589793, 0.3010299956639812, 0.6931471805599453, 0.0, 0.0 }; if (sti < 7) { fpush(c, k[sti]); return 1; } break; }
      case 6: switch (sti) {
        case 0: ST(0) = host_math(5, 2.0, ST(0)) - 1.0; return 1;    /* f2xm1 */
        case 1: ST(1) = ST(1) * host_math(14, ST(0), 0); fpop(c); return 1;   /* fyl2x */
        case 2: { double t = host_math(2, ST(0), 0); ST(0) = t; fpush(c, 1.0); c->fsw &= ~F_C2; return 1; }   /* fptan */
        case 3: ST(1) = host_math(6, ST(1), ST(0)); fpop(c); return 1;   /* fpatan */
        case 4: { double v = ST(0); if (v == 0) { ST(0) = -1.0 / 0.0; fpush(c, v); return 1; } int e = 0; double m = e_fabs(v); while (m >= 2) { m /= 2; e++; } while (m < 1) { m *= 2; e--; } ST(0) = e; fpush(c, v < 0 ? -m : m); return 1; }   /* fxtract */
        case 5: { double a = ST(0), b = ST(1); double q = a / b; double n = (q < 0 ? -e_floor(-q + 0.5) : e_floor(q + 0.5)); ST(0) = a - n * b; c->fsw &= ~(F_C2 | F_C0 | F_C1 | F_C3); s64 qi = (s64)n; if (qi & 1) c->fsw |= F_C1; if (qi & 2) c->fsw |= F_C3; if (qi & 4) c->fsw |= F_C0; return 1; }   /* fprem1 */
        case 6: c->ftop = (c->ftop - 1) & 7; return 1;                /* fdecstp */
        case 7: c->ftop = (c->ftop + 1) & 7; return 1;                /* fincstp */
        default: break; } break;
      case 7: switch (sti) {
        case 0: { double a = ST(0), b = ST(1); double q = __builtin_trunc(a / b); ST(0) = a - q * b; c->fsw &= ~(F_C2 | F_C0 | F_C1 | F_C3); s64 qi = (s64)q; if (qi & 1) c->fsw |= F_C1; if (qi & 2) c->fsw |= F_C3; if (qi & 4) c->fsw |= F_C0; return 1; }   /* fprem */
        case 1: ST(1) = ST(1) * host_math(14, ST(0) + 1.0, 0); fpop(c); return 1;   /* fyl2xp1 */
        case 2: ST(0) = e_sqrt(ST(0)); return 1;                      /* fsqrt */
        case 3: { double v = ST(0); ST(0) = host_math(0, v, 0); fpush(c, host_math(1, v, 0)); c->fsw &= ~F_C2; return 1; }   /* fsincos */
        case 4: ST(0) = x87_round(c, ST(0)); return 1;                /* frndint */
        case 5: { double e = __builtin_trunc(ST(1)); ST(0) = ST(0) * host_math(5, 2.0, e); return 1; }   /* fscale */
        case 6: ST(0) = host_math(0, ST(0), 0); c->fsw &= ~F_C2; return 1;   /* fsin */
        case 7: ST(0) = host_math(1, ST(0), 0); c->fsw &= ~F_C2; return 1;   /* fcos */
        default: break; } break;
      default: break; } break;
    case 0xDA: if (sub < 4) { if (x87_cmov_ok(c, sub)) ST(0) = ST(sti); return 1; } if (sub == 5 && sti == 1) { x87_compare(c, ST(0), ST(1), 0); fpop(c); fpop(c); return 1; } break;   /* fcmovcc / fucompp */
    case 0xDB: if (sub < 4) { if (x87_cmov_ok(c, 4 + sub)) ST(0) = ST(sti); return 1; }
      if (sub == 4) { if (sti == 2) { c->fsw &= 0xff00 & ~0x8000; c->fsw &= ~0x80ff; return 1; } if (sti == 3) { c->fpu_cw = 0x37f; c->fsw = 0; c->ftop = 0; c->ftag = 0; return 1; } if (sti == 0 || sti == 1 || sti == 4) return 1; break; }   /* fnclex / fninit / fneni,fndisi,fsetpm */
      if (sub == 5 || sub == 6) { x87_compare(c, ST(0), ST(sti), 1); return 1; }   /* fucomi / fcomi */
      break;
    case 0xDD: switch (sub) {
      case 0: c->ftag &= ~(1u << ((c->ftop + sti) & 7)); return 1;   /* ffree */
      case 2: ST(sti) = ST(0); fx_copy(c, FXI(sti), FXI(0)); return 1;   /* fst st(i) */
      case 3: ST(sti) = ST(0); fx_copy(c, FXI(sti), FXI(0)); fpop(c); return 1;   /* fstp st(i) */
      case 4: x87_compare(c, ST(0), ST(sti), 0); return 1;           /* fucom */
      case 5: x87_compare(c, ST(0), ST(sti), 0); fpop(c); return 1;  /* fucomp */
      default: break; } break;
    case 0xDF: if (sub == 4 && sti == 0) { c->r[RAX] = (c->r[RAX] & ~0xffffull) | (c->fsw | (FTOP << 11)); return 1; }   /* fnstsw ax */
      if (sub == 5 || sub == 6) { x87_compare(c, ST(0), ST(sti), 1); fpop(c); return 1; }   /* fucomip / fcomip */
      break;
  }
  return 0;
}

/* ---- JIT helper (jit_x87): the same instructions with the memory operand already resolved to a host pointer ----
   x87_mem_size: bytes of the memory operand of op /sub when the JIT may translate that form (0 = leave it to the interpreter:
   fldenv/fnstenv/fnsave/frstor/fbld/fbstp and undefined encodings); x87_mem_write: the form stores. */
static int x87_mem_size(u8 op, int sub) {
  switch (op) {
    case 0xD8: case 0xDA: return 4; case 0xDC: return 8; case 0xDE: return 2;
    case 0xD9: return (sub == 0 || sub == 2 || sub == 3) ? 4 : (sub == 5 || sub == 7) ? 2 : 0;
    case 0xDB: return sub < 4 ? 4 : (sub == 5 || sub == 7) ? 10 : 0;
    case 0xDD: return sub < 4 ? 8 : sub == 7 ? 2 : 0;
    case 0xDF: return sub < 4 ? 2 : (sub == 5 || sub == 7) ? 8 : 0;
    default: return 0;
  }
}
static int x87_mem_write(u8 op, int sub) { return (op == 0xD9 || op == 0xDB || op == 0xDD || op == 0xDF) && (sub == 1 || sub == 2 || sub == 3 || sub == 7) && !(op == 0xD9 && sub == 1); }
static int x87_reg_ok(u8 op, int sub, int sti) {   /* register forms x87_reg() executes (mirrors its switch) */
  switch (op) {
    case 0xD8: case 0xDC: return 1;
    case 0xDE: return sub != 3 || sti == 1;
    case 0xD9: return sub < 2 || (sub == 2 && sti == 0) || sub == 3 || (sub == 4 && (sti == 0 || sti == 1 || sti == 4 || sti == 5)) || (sub == 5 && sti < 7) || sub == 6 || sub == 7;
    case 0xDA: return sub < 4 || (sub == 5 && sti == 1);
    case 0xDB: return sub < 4 || (sub == 4 && (sti <= 4)) || sub == 5 || sub == 6;
    case 0xDD: return sub == 0 || (sub >= 2 && sub <= 5);
    case 0xDF: return (sub == 4 && sti == 0) || sub == 5 || sub == 6;
    default: return 0;
  }
}
static inline u64 ldp64(const u8 *p) { u64 v; e_memcpy(&v, p, 8); return v; }
static inline u32 ldp32(const u8 *p) { u32 v; e_memcpy(&v, p, 4); return v; }
static inline u16 ldp16(const u8 *p) { u16 v; e_memcpy(&v, p, 2); return v; }
static double x87_ldp(const u8 *p, int kind) {   /* as x87_ld, from a host pointer */
  switch (kind) {
    case 0: { float f; e_memcpy(&f, p, 4); return f; }
    case 1: { double d; e_memcpy(&d, p, 8); return d; }
    case 2: return (double)(s16)ldp16(p);
    case 3: return (double)(s32)ldp32(p);
    case 4: return (double)(s64)ldp64(p);
    default: return f80_to_double(ldp64(p), ldp16(p + 8));
  }
}
static void x87_stp(Cpu *c, u8 *p, int kind, double v) {   /* as x87_st, to a host pointer */
  switch (kind) {
    case 0: { float f = (float)v; e_memcpy(p, &f, 4); break; }
    case 1: e_memcpy(p, &v, 8); break;
    case 2: { double r = x87_round(c, v); u16 w = (r != r || r > 32767 || r < -32768) ? 0x8000 : (u16)(s16)r; e_memcpy(p, &w, 2); break; }
    case 3: { double r = x87_round(c, v); u32 w = (r != r || r > 2147483647.0 || r < -2147483648.0) ? 0x80000000u : (u32)(s32)r; e_memcpy(p, &w, 4); break; }
    case 4: { double r = x87_round(c, v); u64 w = (r != r || r >= 9223372036854775808.0 || r < -9223372036854775808.0) ? 0x8000000000000000ull : (u64)(s64)r; e_memcpy(p, &w, 8); break; }
    case 6: { u16 w = (u16)(s16)__builtin_trunc(v); e_memcpy(p, &w, 2); break; }
    case 7: { u32 w = (u32)(s32)__builtin_trunc(v); e_memcpy(p, &w, 4); break; }
    case 8: { u64 w = (u64)(s64)__builtin_trunc(v); e_memcpy(p, &w, 8); break; }
    default: { u64 m; u16 se; double_to_f80(v, &m, &se); e_memcpy(p, &m, 8); e_memcpy(p + 8, &se, 2); }
  }
}
static void x87_memp(Cpu *c, u8 op, int sub, u8 *p) {   /* memory forms for which x87_mem_size() != 0; semantics identical to exec_x87 */
  switch (op) {
    case 0xD8: case 0xDC: case 0xDA: case 0xDE: { double m = x87_ldp(p, op == 0xD8 ? 0 : op == 0xDC ? 1 : op == 0xDA ? 3 : 2);
      if (sub == 2 || sub == 3) { x87_compare(c, ST(0), m, 0); if (sub == 3) fpop(c); } else ST(0) = x87_arith(sub, ST(0), m); return; }
    case 0xD9: switch (sub) {
      case 0: fpush(c, x87_ldp(p, 0)); return; case 2: x87_stp(c, p, 0, ST(0)); return; case 3: x87_stp(c, p, 0, fpop(c)); return;
      case 5: c->fpu_cw = ldp16(p); return; default: { u16 w = c->fpu_cw; e_memcpy(p, &w, 2); return; } }
    case 0xDB: switch (sub) {
      case 0: fpush(c, x87_ldp(p, 3)); return; case 1: x87_stp(c, p, 7, fpop(c)); return; case 2: x87_stp(c, p, 3, ST(0)); return; case 3: x87_stp(c, p, 3, fpop(c)); return;
      case 5: { u64 m = ldp64(p); u16 se = ldp16(p + 8); fpush(c, f80_to_double(m, se)); c->fxk[FXI(0)] = 2; c->fxm[FXI(0)] = m; c->fxse[FXI(0)] = se; return; }
      default: { int k = c->fxk[FXI(0)]; u64 m = c->fxm[FXI(0)]; u16 se = c->fxse[FXI(0)]; double v = fpop(c); if (k == 1) i64_to_f80((s64)m, &m, &se); if (k) { e_memcpy(p, &m, 8); e_memcpy(p + 8, &se, 2); return; } x87_stp(c, p, 5, v); return; } }
    case 0xDD: switch (sub) {
      case 0: fpush(c, x87_ldp(p, 1)); return; case 1: x87_stp(c, p, 8, fpop(c)); return; case 2: x87_stp(c, p, 1, ST(0)); return; case 3: x87_stp(c, p, 1, fpop(c)); return;
      default: { u16 w = (u16)(c->fsw | (FTOP << 11)); e_memcpy(p, &w, 2); return; } }
    default: switch (sub) {   /* DF */
      case 0: fpush(c, x87_ldp(p, 2)); return; case 1: x87_stp(c, p, 6, fpop(c)); return; case 2: x87_stp(c, p, 2, ST(0)); return; case 3: x87_stp(c, p, 2, fpop(c)); return;
      case 5: { u64 m = ldp64(p); fpush(c, (double)(s64)m); c->fxk[FXI(0)] = 1; c->fxm[FXI(0)] = m; return; }
      default: { int k = c->fxk[FXI(0)]; u64 m = c->fxm[FXI(0)]; u16 se = c->fxse[FXI(0)]; double v = fpop(c); s64 iv;
        if (k == 1) { e_memcpy(p, &m, 8); return; } if (k == 2 && f80_is_i64(m, se, &iv)) { u64 w = (u64)iv; e_memcpy(p, &w, 8); return; } x87_stp(c, p, 4, v); return; } }
  }
}
/* key = op | sub << 8 | sti << 11 | (register form) << 14; returns the fnstsw value for DF /4 (the translator stores it into AX), else 0 */
u64 x87_jit(Cpu *c, u32 key, u8 *p) {
  u8 op = (u8)key; int sub = (key >> 8) & 7, sti = (key >> 11) & 7, isreg = (key >> 14) & 1;
  if (!x87_keep(op, sub, isreg)) fx_clear_all(c);
  if (!isreg) { x87_memp(c, op, sub, p); return 0; }
  if (op == 0xDF && sub == 4) return (u64)(u16)(c->fsw | (FTOP << 11));
  x87_reg(c, op, sub, sti); return 0;
}
