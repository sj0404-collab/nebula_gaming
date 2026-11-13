/* mmx.inc — MMX (mm0–mm7) on the no-prefix 0F opcode space, included from cpu.c.
 *
 * MMX registers are kept in Cpu.mm[8] (the low 64 bits of the x87 registers on real hardware; the x87 stack here is
 * doubles, so the aliasing is reduced to what programs observe: any MMX instruction sets TOP = 0 and marks all eight
 * x87 registers valid, emms marks them empty). The lane arithmetic is the SSE2 code (pint_op) run at 8 bytes, so
 * both widths stay bit-identical; the MMX-only forms (movd/movq, pshufw, pinsrw/pextrw, pmovmskb, maskmovq, the
 * immediate shifts, cvtpi2ps/cvtps2pi) are here. Prefixed forms (66 = xmm, F2/F3 = scalar) never come through this
 * file except 66 0F 2A/2C/2D (cvtpi2pd / cvtpd2pi), whose source or destination is an mm register. */
static const u8 g_mmx_ops[256] = {
  [0x60] = 1, [0x61] = 1, [0x62] = 1, [0x63] = 1, [0x64] = 1, [0x65] = 1, [0x66] = 1, [0x67] = 1, [0x68] = 1, [0x69] = 1, [0x6A] = 1, [0x6B] = 1, [0x6E] = 1, [0x6F] = 1,
  [0x70] = 1, [0x71] = 1, [0x72] = 1, [0x73] = 1, [0x74] = 1, [0x75] = 1, [0x76] = 1, [0x77] = 1, [0x7E] = 1, [0x7F] = 1, [0xC4] = 1, [0xC5] = 1,
  [0xD1] = 1, [0xD2] = 1, [0xD3] = 1, [0xD4] = 1, [0xD5] = 1, [0xD7] = 1, [0xD8] = 1, [0xD9] = 1, [0xDA] = 1, [0xDB] = 1, [0xDC] = 1, [0xDD] = 1, [0xDE] = 1, [0xDF] = 1,
  [0xE0] = 1, [0xE1] = 1, [0xE2] = 1, [0xE3] = 1, [0xE4] = 1, [0xE5] = 1, [0xE7] = 1, [0xE8] = 1, [0xE9] = 1, [0xEA] = 1, [0xEB] = 1, [0xEC] = 1, [0xED] = 1, [0xEE] = 1, [0xEF] = 1,
  [0xF1] = 1, [0xF2] = 1, [0xF3] = 1, [0xF4] = 1, [0xF5] = 1, [0xF6] = 1, [0xF7] = 1, [0xF8] = 1, [0xF9] = 1, [0xFA] = 1, [0xFB] = 1, [0xFC] = 1, [0xFD] = 1, [0xFE] = 1,
};
static inline int mmx_is(u8 op, const Ins *i) { if (i->rep) return 0; if (op == 0x2A || op == 0x2C || op == 0x2D) return 1; return !i->p66 && g_mmx_ops[op]; }
static inline void mmx_touch(Cpu *c) { c->ftop = 0; c->ftag = 0xff; }   /* every MMX instruction: TOS = 0, all x87 registers tagged valid */
static inline u64 mmx_src64(Ins *i) { return i->mod == 3 ? i->c->mm[i->rm & 7] : rd64(i->c, EA(i)); }
static void mmx_exec(Ins *i, u8 op) {
  Cpu *c = i->c;
  if (op == 0x77) { c->ftop = 0; c->ftag = 0; return; }   /* emms */
  decode_modrm(i); mmx_touch(c);
  int reg = i->reg & 7, rm = i->rm & 7;
  switch (op) {
    case 0x2A: {   /* cvtpi2ps xmm, mm/m64 (two floats) / 66: cvtpi2pd xmm, mm/m64 (two doubles) */
      u64 v = mmx_src64(i); s32 lo = (s32)(u32)v, hi = (s32)(u32)(v >> 32); Xmm *d = &c->x[i->reg];
      if (i->p66) { d->f64[0] = (double)lo; d->f64[1] = (double)hi; } else { d->f32[0] = (float)lo; d->f32[1] = (float)hi; } return; }
    case 0x2C: case 0x2D: {   /* cvt(t)ps2pi mm, xmm/m64 / 66: cvt(t)pd2pi mm, xmm/m128 */
      Xmm s; xmm_load(i, &s, i->p66 ? 16 : 8); double a = i->p66 ? s.f64[0] : (double)s.f32[0], b = i->p66 ? s.f64[1] : (double)s.f32[1];
      c->mm[reg] = (u64)(u32)f2i32(a, op == 0x2C) | ((u64)(u32)f2i32(b, op == 0x2C) << 32); return; }
    case 0x6E: c->mm[reg] = i->rexw ? rm_get(i, 8) : (u32)rm_get(i, 4); return;                                   /* movd/movq mm, r/m */
    case 0x7E: rm_set(i, i->rexw ? 8 : 4, i->rexw ? c->mm[reg] : (u32)c->mm[reg]); return;                       /* movd/movq r/m, mm */
    case 0x6F: c->mm[reg] = mmx_src64(i); return;                                                                 /* movq mm, mm/m64 */
    case 0x7F: case 0xE7: if (i->mod == 3) c->mm[rm] = c->mm[reg]; else wr64(c, EA(i), c->mm[reg]); return;       /* movq mm/m64, mm; movntq */
    case 0xD7: { u64 v = c->mm[rm]; u32 r = 0; for (int k = 0; k < 8; k++) r |= (u32)((v >> (8 * k + 7)) & 1) << k; c->r[i->reg] = r; return; }   /* pmovmskb r32, mm */
    case 0xC4: { fix_rip_rel(i, 1); u64 v = rm_get(i, 2); int im = fetch8(i) & 3; c->mm[reg] = (c->mm[reg] & ~(0xffffull << (16 * im))) | ((v & 0xffff) << (16 * im)); return; }   /* pinsrw */
    case 0xC5: { int im = fetch8(i) & 3; c->r[i->reg] = (c->mm[rm] >> (16 * im)) & 0xffff; return; }                                                                            /* pextrw */
    case 0x70: { fix_rip_rel(i, 1); Xmm s, r; s.q[0] = mmx_src64(i); u8 im = fetch8(i); for (int k = 0; k < 4; k++) r.w[k] = s.w[(im >> (2 * k)) & 3]; c->mm[reg] = r.q[0]; return; }   /* pshufw */
    case 0x71: case 0x72: case 0x73: { u8 im = fetch8(i); int sub = i->reg & 7; Xmm d; d.q[0] = c->mm[rm]; d.q[1] = 0;
      if (op == 0x71) { for (int k = 0; k < 4; k++) d.w[k] = sub == 2 ? (im > 15 ? 0 : d.w[k] >> im) : sub == 4 ? (u16)((s16)d.w[k] >> (im > 15 ? 15 : im)) : sub == 6 ? (im > 15 ? 0 : (u16)(d.w[k] << im)) : d.w[k]; }
      else if (op == 0x72) { for (int k = 0; k < 2; k++) d.d[k] = sub == 2 ? (im > 31 ? 0 : d.d[k] >> im) : sub == 4 ? (u32)((s32)d.d[k] >> (im > 31 ? 31 : im)) : sub == 6 ? (im > 31 ? 0 : d.d[k] << im) : d.d[k]; }
      else { if (sub == 2) d.q[0] = im > 63 ? 0 : d.q[0] >> im; else if (sub == 6) d.q[0] = im > 63 ? 0 : d.q[0] << im; else { UNSUP("0F 73 /%d (mmx)", sub); } }
      c->mm[rm] = d.q[0]; return; }
    case 0xF7: {   /* maskmovq [rdi], mm, mm: bytes whose mask high bit is set */
      if (i->mod != 3) UNSUP("maskmovq with memory operand"); u64 data = c->mm[reg], msk = c->mm[rm], di = c->r[RDI]; if (c->mode32 || i->p67) di &= 0xffffffffull; if (i->seg == 1) di += c->fs_base; else if (i->seg == 2) di += c->gs_base;
      for (int k = 0; k < 8; k++) if ((msk >> (8 * k + 7)) & 1) wr8(c, di + k, (u8)(data >> (8 * k))); return; }
    default: { Xmm d, s; d.q[0] = c->mm[reg]; d.q[1] = 0; s.q[0] = mmx_src64(i); s.q[1] = 0; pint_op(op, &d, &s, 8); c->mm[reg] = d.q[0]; return; }
  }
}
