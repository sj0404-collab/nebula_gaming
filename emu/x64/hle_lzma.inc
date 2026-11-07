/* High-level emulation of the LZMA SDK decoder inside installers (Inno Setup's Setup engine links LzmaDec.c / Lzma2Dec.c, built with
   bcc32 in Borland's register calling convention).  LzmaDec_DecodeToDic is where an installer spends nearly all of its time while it
   extracts files; interpreting it costs ~150 guest instructions per output byte — minutes for a game.  The function is found by its
   machine-code signature when the exe is mapped and its entry is replaced by a jump into a shim that runs the same algorithm natively
   on the guest's own CLzmaDec state (SDK 4.65 … 9.20 layout: props 0, probs 16, dic 20, buf 24, range 28, code 32, dicPos 36,
   dicBufSize 40, processedPos 44, checkDicSize 48, state 52, reps 56, remainLen 72, needFlush 76, needInitState 80, numProbs 84,
   tempBufSize 88, tempBuf 92).  Output is bit-identical by construction (same algorithm, same state), and Inno verifies every
   extracted file's CRC-32 anyway.  Register convention: p in eax, dicLimit in edx, src in ecx; srcLen, finishMode, status pushed
   left to right (status on top of the stack), callee pops 12 bytes. */

#define LZ_kTopValue (1u << 24)
#define LZ_kNumBitModelTotalBits 11
#define LZ_kBitModelTotal (1u << 11)
#define LZ_kNumMoveBits 5
#define LZ_RC_INIT_SIZE 5
#define LZ_REQUIRED_INPUT_MAX 20
#define LZ_kNumPosBitsMax 4
#define LZ_kNumPosStatesMax 16
#define LZ_kLenNumLowBits 3
#define LZ_kLenNumLowSymbols 8
#define LZ_kLenNumMidBits 3
#define LZ_kLenNumMidSymbols 8
#define LZ_kLenNumHighBits 8
#define LZ_kLenNumHighSymbols 256
#define LZ_LenChoice 0
#define LZ_LenChoice2 1
#define LZ_LenLow 2
#define LZ_LenMid (LZ_LenLow + (LZ_kNumPosStatesMax << LZ_kLenNumLowBits))
#define LZ_LenHigh (LZ_LenMid + (LZ_kNumPosStatesMax << LZ_kLenNumMidBits))
#define LZ_kNumLenProbs (LZ_LenHigh + LZ_kLenNumHighSymbols)
#define LZ_kNumStates 12
#define LZ_kNumLitStates 7
#define LZ_kStartPosModelIndex 4
#define LZ_kEndPosModelIndex 14
#define LZ_kNumFullDistances (1 << (LZ_kEndPosModelIndex >> 1))
#define LZ_kNumPosSlotBits 6
#define LZ_kNumLenToPosStates 4
#define LZ_kNumAlignBits 4
#define LZ_kAlignTableSize (1 << LZ_kNumAlignBits)
#define LZ_kMatchMinLen 2
#define LZ_kMatchSpecLenStart (LZ_kMatchMinLen + LZ_kLenNumLowSymbols + LZ_kLenNumMidSymbols + LZ_kLenNumHighSymbols)
#define LZ_IsMatch 0
#define LZ_IsRep (LZ_IsMatch + (LZ_kNumStates << LZ_kNumPosBitsMax))
#define LZ_IsRepG0 (LZ_IsRep + LZ_kNumStates)
#define LZ_IsRepG1 (LZ_IsRepG0 + LZ_kNumStates)
#define LZ_IsRepG2 (LZ_IsRepG1 + LZ_kNumStates)
#define LZ_IsRep0Long (LZ_IsRepG2 + LZ_kNumStates)
#define LZ_PosSlot (LZ_IsRep0Long + (LZ_kNumStates << LZ_kNumPosBitsMax))
#define LZ_SpecPos (LZ_PosSlot + (LZ_kNumLenToPosStates << LZ_kNumPosSlotBits))
#define LZ_Align (LZ_SpecPos + LZ_kNumFullDistances - LZ_kEndPosModelIndex)
#define LZ_LenCoder (LZ_Align + LZ_kAlignTableSize)
#define LZ_RepLenCoder (LZ_LenCoder + LZ_kNumLenProbs)
#define LZ_Literal (LZ_RepLenCoder + LZ_kNumLenProbs)
#define LZ_LIT_SIZE 768

static u32 lz_le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void lz_put32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
typedef u16 LzProb;
typedef struct {   /* the guest CLzmaDec, loaded into host form for the duration of one call */
  u32 lc, lp, pb, dicSize; LzProb *probs; u8 *dic; const u8 *buf; u32 range, code; u32 dicPos, dicBufSize, processedPos, checkDicSize, state, reps[4], remainLen; int needFlush, needInitState; u32 numProbs, tempBufSize; u8 tempBuf[LZ_REQUIRED_INPUT_MAX];
} LzDec;

#define LZ_NORMALIZE if (range < LZ_kTopValue) { range <<= 8; code = (code << 8) | (*buf++); }
#define LZ_IF_BIT_0(p) ttt = *(p); LZ_NORMALIZE; bound = (range >> LZ_kNumBitModelTotalBits) * ttt; if (code < bound)
#define LZ_UPDATE_0(p) range = bound; *(p) = (LzProb)(ttt + ((LZ_kBitModelTotal - ttt) >> LZ_kNumMoveBits));
#define LZ_UPDATE_1(p) range -= bound; code -= bound; *(p) = (LzProb)(ttt - (ttt >> LZ_kNumMoveBits));
#define LZ_GET_BIT2(p, i, A0, A1) LZ_IF_BIT_0(p) { LZ_UPDATE_0(p); i = (i + i); A0; } else { LZ_UPDATE_1(p); i = (i + i) + 1; A1; }
#define LZ_GET_BIT(p, i) LZ_GET_BIT2(p, i, ; , ;)
#define LZ_TREE_DECODE(probs, limit, i) { i = 1; do { LZ_GET_BIT((probs) + i, i); } while (i < (limit)); i -= (limit); }

static int lz_decode_real(LzDec *p, u32 limit, const u8 *bufLimit) {
  LzProb *probs = p->probs; unsigned state = p->state; u32 rep0 = p->reps[0], rep1 = p->reps[1], rep2 = p->reps[2], rep3 = p->reps[3];
  unsigned pbMask = (1u << p->pb) - 1, lpMask = (1u << p->lp) - 1, lc = p->lc; u8 *dic = p->dic; u32 dicBufSize = p->dicBufSize, dicPos = p->dicPos;
  u32 processedPos = p->processedPos, checkDicSize = p->checkDicSize; unsigned len = 0; const u8 *buf = p->buf; u32 range = p->range, code = p->code;
  do {
    LzProb *prob; u32 bound; unsigned ttt; unsigned posState = processedPos & pbMask;
    prob = probs + LZ_IsMatch + (state << LZ_kNumPosBitsMax) + posState;
    LZ_IF_BIT_0(prob) {
      unsigned symbol; LZ_UPDATE_0(prob); prob = probs + LZ_Literal;
      if (checkDicSize != 0 || processedPos != 0) prob += (LZ_LIT_SIZE * (((processedPos & lpMask) << lc) + (dic[(dicPos == 0 ? dicBufSize : dicPos) - 1] >> (8 - lc))));
      if (state < LZ_kNumLitStates) { state -= (state < 4) ? state : 3; symbol = 1; do { LZ_GET_BIT(prob + symbol, symbol) } while (symbol < 0x100); }
      else { unsigned matchByte = dic[(dicPos - rep0) + ((dicPos < rep0) ? dicBufSize : 0)]; unsigned offs = 0x100; state -= (state < 10) ? 3 : 6; symbol = 1;
        do { unsigned bit; LzProb *probLit; matchByte <<= 1; bit = (matchByte & offs); probLit = prob + offs + bit + symbol; LZ_GET_BIT2(probLit, symbol, offs &= ~bit, offs &= bit) } while (symbol < 0x100); }
      dic[dicPos++] = (u8)symbol; processedPos++; continue;
    } else {
      LZ_UPDATE_1(prob); prob = probs + LZ_IsRep + state;
      LZ_IF_BIT_0(prob) { LZ_UPDATE_0(prob); state += LZ_kNumStates; prob = probs + LZ_LenCoder; }
      else {
        LZ_UPDATE_1(prob); if (checkDicSize == 0 && processedPos == 0) return 1;
        prob = probs + LZ_IsRepG0 + state;
        LZ_IF_BIT_0(prob) {
          LZ_UPDATE_0(prob); prob = probs + LZ_IsRep0Long + (state << LZ_kNumPosBitsMax) + posState;
          LZ_IF_BIT_0(prob) { LZ_UPDATE_0(prob); dic[dicPos] = dic[(dicPos - rep0) + ((dicPos < rep0) ? dicBufSize : 0)]; dicPos++; processedPos++; state = state < LZ_kNumLitStates ? 9 : 11; continue; }
          LZ_UPDATE_1(prob);
        } else {
          u32 distance; LZ_UPDATE_1(prob); prob = probs + LZ_IsRepG1 + state;
          LZ_IF_BIT_0(prob) { LZ_UPDATE_0(prob); distance = rep1; }
          else { LZ_UPDATE_1(prob); prob = probs + LZ_IsRepG2 + state;
            LZ_IF_BIT_0(prob) { LZ_UPDATE_0(prob); distance = rep2; } else { LZ_UPDATE_1(prob); distance = rep3; rep3 = rep2; }
            rep2 = rep1; }
          rep1 = rep0; rep0 = distance;
        }
        state = state < LZ_kNumLitStates ? 8 : 11; prob = probs + LZ_RepLenCoder;
      }
      { unsigned lim, offset; LzProb *probLen = prob + LZ_LenChoice;
        LZ_IF_BIT_0(probLen) { LZ_UPDATE_0(probLen); probLen = prob + LZ_LenLow + (posState << LZ_kLenNumLowBits); offset = 0; lim = (1 << LZ_kLenNumLowBits); }
        else { LZ_UPDATE_1(probLen); probLen = prob + LZ_LenChoice2;
          LZ_IF_BIT_0(probLen) { LZ_UPDATE_0(probLen); probLen = prob + LZ_LenMid + (posState << LZ_kLenNumMidBits); offset = LZ_kLenNumLowSymbols; lim = (1 << LZ_kLenNumMidBits); }
          else { LZ_UPDATE_1(probLen); probLen = prob + LZ_LenHigh; offset = LZ_kLenNumLowSymbols + LZ_kLenNumMidSymbols; lim = (1 << LZ_kLenNumHighBits); } }
        LZ_TREE_DECODE(probLen, lim, len); len += offset; }
      if (state >= LZ_kNumStates) {
        u32 distance; prob = probs + LZ_PosSlot + ((len < LZ_kNumLenToPosStates ? len : LZ_kNumLenToPosStates - 1) << LZ_kNumPosSlotBits);
        LZ_TREE_DECODE(prob, 1 << LZ_kNumPosSlotBits, distance);
        if (distance >= LZ_kStartPosModelIndex) {
          unsigned posSlot = (unsigned)distance; int numDirectBits = (int)((distance >> 1) - 1); distance = (2 | (distance & 1));
          if (posSlot < LZ_kEndPosModelIndex) { distance <<= numDirectBits; prob = probs + LZ_SpecPos + distance - posSlot - 1;
            { u32 mask = 1; unsigned i = 1; do { LZ_GET_BIT2(prob + i, i, ; , distance |= mask); mask <<= 1; } while (--numDirectBits != 0); } }
          else { numDirectBits -= LZ_kNumAlignBits;
            do { LZ_NORMALIZE range >>= 1; { u32 t; code -= range; t = (0 - ((u32)code >> 31)); distance = (distance << 1) + (t + 1); code += range & t; } } while (--numDirectBits != 0);
            prob = probs + LZ_Align; distance <<= LZ_kNumAlignBits;
            { unsigned i = 1; LZ_GET_BIT2(prob + i, i, ; , distance |= 1); LZ_GET_BIT2(prob + i, i, ; , distance |= 2); LZ_GET_BIT2(prob + i, i, ; , distance |= 4); LZ_GET_BIT2(prob + i, i, ; , distance |= 8); }
            if (distance == 0xFFFFFFFFu) { len += LZ_kMatchSpecLenStart; state -= LZ_kNumStates; break; } }
        }
        rep3 = rep2; rep2 = rep1; rep1 = rep0; rep0 = distance + 1;
        if (checkDicSize == 0) { if (distance >= processedPos) return 1; } else if (distance >= checkDicSize) return 1;
        state = (state < LZ_kNumStates + LZ_kNumLitStates) ? LZ_kNumLitStates : LZ_kNumLitStates + 3;
      }
      len += LZ_kMatchMinLen;
      if (limit == dicPos) return 1;
      { u32 rem = limit - dicPos; unsigned curLen = ((rem < len) ? (unsigned)rem : len); u32 pos = (dicPos - rep0) + ((dicPos < rep0) ? dicBufSize : 0);
        processedPos += curLen; len -= curLen;
        if (pos + curLen <= dicBufSize) { u8 *dest = dic + dicPos; ptrdiff_t src = (ptrdiff_t)pos - (ptrdiff_t)dicPos; const u8 *lim = dest + curLen; dicPos += curLen; do *(dest) = (u8)*(dest + src); while (++dest != lim); }
        else { do { dic[dicPos++] = dic[pos]; if (++pos == dicBufSize) pos = 0; } while (--curLen != 0); } }
    }
  } while (dicPos < limit && buf < bufLimit);
  LZ_NORMALIZE;
  p->buf = buf; p->range = range; p->code = code; p->remainLen = len; p->dicPos = dicPos; p->processedPos = processedPos;
  p->reps[0] = rep0; p->reps[1] = rep1; p->reps[2] = rep2; p->reps[3] = rep3; p->state = state;
  return 0;
}

static void lz_write_rem(LzDec *p, u32 limit) {
  if (p->remainLen != 0 && p->remainLen < LZ_kMatchSpecLenStart) {
    u8 *dic = p->dic; u32 dicPos = p->dicPos, dicBufSize = p->dicBufSize; unsigned len = p->remainLen; u32 rep0 = p->reps[0];
    if (limit - dicPos < len) len = (unsigned)(limit - dicPos);
    if (p->checkDicSize == 0 && p->dicSize - p->processedPos <= len) p->checkDicSize = p->dicSize;
    p->processedPos += len; p->remainLen -= len;
    while (len != 0) { len--; dic[dicPos] = dic[(dicPos - rep0) + ((dicPos < rep0) ? dicBufSize : 0)]; dicPos++; }
    p->dicPos = dicPos;
  }
}

static int lz_decode_real2(LzDec *p, u32 limit, const u8 *bufLimit) {
  do {
    u32 limit2 = limit;
    if (p->checkDicSize == 0) { u32 rem = p->dicSize - p->processedPos; if (limit - p->dicPos > rem) limit2 = p->dicPos + rem; }
    if (lz_decode_real(p, limit2, bufLimit)) return 1;
    if (p->processedPos >= p->dicSize) p->checkDicSize = p->dicSize;
    lz_write_rem(p, limit);
  } while (p->dicPos < limit && p->buf < bufLimit && p->remainLen < LZ_kMatchSpecLenStart);
  if (p->remainLen > LZ_kMatchSpecLenStart) p->remainLen = LZ_kMatchSpecLenStart;
  return 0;
}

enum { LZ_DUMMY_ERROR, LZ_DUMMY_LIT, LZ_DUMMY_MATCH, LZ_DUMMY_REP };
#define LZ_NORMALIZE_CHECK if (range < LZ_kTopValue) { if (buf >= bufLimit) return LZ_DUMMY_ERROR; range <<= 8; code = (code << 8) | (*buf++); }
#define LZ_IF_BIT_0_CHECK(p) ttt = *(p); LZ_NORMALIZE_CHECK; bound = (range >> LZ_kNumBitModelTotalBits) * ttt; if (code < bound)
#define LZ_UPDATE_0_CHECK range = bound;
#define LZ_UPDATE_1_CHECK range -= bound; code -= bound;
#define LZ_GET_BIT2_CHECK(p, i, A0, A1) LZ_IF_BIT_0_CHECK(p) { LZ_UPDATE_0_CHECK; i = (i + i); A0; } else { LZ_UPDATE_1_CHECK; i = (i + i) + 1; A1; }
#define LZ_GET_BIT_CHECK(p, i) LZ_GET_BIT2_CHECK(p, i, ; , ;)
#define LZ_TREE_DECODE_CHECK(probs, limit, i) { i = 1; do { LZ_GET_BIT_CHECK((probs) + i, i) } while (i < (limit)); i -= (limit); }

static int lz_try_dummy(const LzDec *p, const u8 *buf, u32 inSize) {
  u32 range = p->range, code = p->code; const u8 *bufLimit = buf + inSize; const LzProb *probs = p->probs; unsigned state = p->state; int res;
  { const LzProb *prob; u32 bound; unsigned ttt; unsigned posState = (p->processedPos) & ((1u << p->pb) - 1);
    prob = probs + LZ_IsMatch + (state << LZ_kNumPosBitsMax) + posState;
    LZ_IF_BIT_0_CHECK(prob) {
      LZ_UPDATE_0_CHECK prob = probs + LZ_Literal;
      if (p->checkDicSize != 0 || p->processedPos != 0) prob += (LZ_LIT_SIZE * ((((p->processedPos) & ((1u << (p->lp)) - 1)) << p->lc) + (p->dic[(p->dicPos == 0 ? p->dicBufSize : p->dicPos) - 1] >> (8 - p->lc))));
      if (state < LZ_kNumLitStates) { unsigned symbol = 1; do { LZ_GET_BIT_CHECK(prob + symbol, symbol) } while (symbol < 0x100); }
      else { unsigned matchByte = p->dic[p->dicPos - p->reps[0] + ((p->dicPos < p->reps[0]) ? p->dicBufSize : 0)]; unsigned offs = 0x100; unsigned symbol = 1;
        do { unsigned bit; const LzProb *probLit; matchByte <<= 1; bit = (matchByte & offs); probLit = prob + offs + bit + symbol; LZ_GET_BIT2_CHECK(probLit, symbol, offs &= ~bit, offs &= bit) } while (symbol < 0x100); }
      res = LZ_DUMMY_LIT;
    } else {
      unsigned len; LZ_UPDATE_1_CHECK; prob = probs + LZ_IsRep + state;
      LZ_IF_BIT_0_CHECK(prob) { LZ_UPDATE_0_CHECK; state = 0; prob = probs + LZ_LenCoder; res = LZ_DUMMY_MATCH; }
      else { LZ_UPDATE_1_CHECK; res = LZ_DUMMY_REP; prob = probs + LZ_IsRepG0 + state;
        LZ_IF_BIT_0_CHECK(prob) { LZ_UPDATE_0_CHECK; prob = probs + LZ_IsRep0Long + (state << LZ_kNumPosBitsMax) + posState;
          LZ_IF_BIT_0_CHECK(prob) { LZ_UPDATE_0_CHECK; LZ_NORMALIZE_CHECK; return LZ_DUMMY_REP; } else { LZ_UPDATE_1_CHECK; } }
        else { LZ_UPDATE_1_CHECK; prob = probs + LZ_IsRepG1 + state;
          LZ_IF_BIT_0_CHECK(prob) { LZ_UPDATE_0_CHECK; } else { LZ_UPDATE_1_CHECK; prob = probs + LZ_IsRepG2 + state; LZ_IF_BIT_0_CHECK(prob) { LZ_UPDATE_0_CHECK; } else { LZ_UPDATE_1_CHECK; } } }
        state = LZ_kNumStates; prob = probs + LZ_RepLenCoder; }
      { unsigned lim, offset; const LzProb *probLen = prob + LZ_LenChoice;
        LZ_IF_BIT_0_CHECK(probLen) { LZ_UPDATE_0_CHECK; probLen = prob + LZ_LenLow + (posState << LZ_kLenNumLowBits); offset = 0; lim = 1 << LZ_kLenNumLowBits; }
        else { LZ_UPDATE_1_CHECK; probLen = prob + LZ_LenChoice2;
          LZ_IF_BIT_0_CHECK(probLen) { LZ_UPDATE_0_CHECK; probLen = prob + LZ_LenMid + (posState << LZ_kLenNumMidBits); offset = LZ_kLenNumLowSymbols; lim = 1 << LZ_kLenNumMidBits; }
          else { LZ_UPDATE_1_CHECK; probLen = prob + LZ_LenHigh; offset = LZ_kLenNumLowSymbols + LZ_kLenNumMidSymbols; lim = 1 << LZ_kLenNumHighBits; } }
        LZ_TREE_DECODE_CHECK(probLen, lim, len); len += offset; }
      if (state < 4) {
        unsigned posSlot; prob = probs + LZ_PosSlot + ((len < LZ_kNumLenToPosStates ? len : LZ_kNumLenToPosStates - 1) << LZ_kNumPosSlotBits);
        LZ_TREE_DECODE_CHECK(prob, 1 << LZ_kNumPosSlotBits, posSlot);
        if (posSlot >= LZ_kStartPosModelIndex) {
          int numDirectBits = ((posSlot >> 1) - 1);
          if (posSlot < LZ_kEndPosModelIndex) prob = probs + LZ_SpecPos + ((2 | (posSlot & 1)) << numDirectBits) - posSlot - 1;
          else { numDirectBits -= LZ_kNumAlignBits; do { LZ_NORMALIZE_CHECK range >>= 1; code -= range & (((code - range) >> 31) - 1); } while (--numDirectBits != 0); prob = probs + LZ_Align; numDirectBits = LZ_kNumAlignBits; }
          { unsigned i = 1; do { LZ_GET_BIT_CHECK(prob + i, i); } while (--numDirectBits != 0); }
        }
      }
    }
  }
  LZ_NORMALIZE_CHECK;
  return res;
}

/* SRes LzmaDec_DecodeToDic(CLzmaDec *p, SizeT dicLimit, const Byte *src, SizeT *srcLen, ELzmaFinishMode finishMode, ELzmaStatus *status) — guest struct at `gp` */
static int lz_decode_to_dic(Cpu *c, u32 gp, u32 dicLimit, u32 gsrc, u32 srcLenPtr, u32 finishMode, u32 statusPtr) {
  u8 *S = mem_ptr(c->mem, gp, 112, 1); if (!S) { cpu_fault(c, "LzmaDec: bad state pointer 0x%x", gp); return 1; }
  LzDec d; d.lc = lz_le32(S); d.lp = lz_le32(S + 4); d.pb = lz_le32(S + 8); d.dicSize = lz_le32(S + 12); u32 gprobs = lz_le32(S + 16), gdic = lz_le32(S + 20), gbuf = lz_le32(S + 24);
  d.range = lz_le32(S + 28); d.code = lz_le32(S + 32); d.dicPos = lz_le32(S + 36); d.dicBufSize = lz_le32(S + 40); d.processedPos = lz_le32(S + 44); d.checkDicSize = lz_le32(S + 48); d.state = lz_le32(S + 52);
  for (int i = 0; i < 4; i++) d.reps[i] = lz_le32(S + 56 + 4 * i); d.remainLen = lz_le32(S + 72); d.needFlush = (int)lz_le32(S + 76); d.needInitState = (int)lz_le32(S + 80); d.numProbs = lz_le32(S + 84); d.tempBufSize = lz_le32(S + 88);
  e_memcpy(d.tempBuf, S + 92, LZ_REQUIRED_INPUT_MAX); (void)gbuf;
  u32 nprobs = LZ_Literal + ((u32)LZ_LIT_SIZE << (d.lc + d.lp)); if (d.lc > 8 || d.lp > 4 || d.pb > 4 || d.numProbs < nprobs) { cpu_fault(c, "LzmaDec: bad props lc=%u lp=%u pb=%u numProbs=%u", d.lc, d.lp, d.pb, d.numProbs); return 1; }
  d.probs = (LzProb *)mem_ptr(c->mem, gprobs, (u64)nprobs * 2, 1); d.dic = mem_ptr(c->mem, gdic, d.dicBufSize ? d.dicBufSize : 1, 1);
  u32 inSize = rd32(c, srcLenPtr); const u8 *src = inSize ? mem_ptr(c->mem, gsrc, inSize, 0) : (const u8 *)d.tempBuf;
  if (!d.probs || !d.dic || !src) { cpu_fault(c, "LzmaDec: bad buffers probs=0x%x dic=0x%x(%u) src=0x%x(%u)", gprobs, gdic, d.dicBufSize, gsrc, inSize); return 1; }
  const u8 *src0 = src; u32 srcLen = 0; int rc = 0; u32 status = 0; d.buf = src;
  lz_write_rem(&d, dicLimit);
  while (d.remainLen != LZ_kMatchSpecLenStart) {
    int checkEndMarkNow;
    if (d.needFlush != 0) {
      for (; inSize > 0 && d.tempBufSize < LZ_RC_INIT_SIZE; srcLen++, inSize--) d.tempBuf[d.tempBufSize++] = *src++;
      if (d.tempBufSize < LZ_RC_INIT_SIZE) { status = 3; goto done; }
      if (d.tempBuf[0] != 0) { rc = 1; goto done; }
      d.code = ((u32)d.tempBuf[1] << 24) | ((u32)d.tempBuf[2] << 16) | ((u32)d.tempBuf[3] << 8) | ((u32)d.tempBuf[4]); d.range = 0xFFFFFFFFu; d.needFlush = 0; d.tempBufSize = 0;
    }
    checkEndMarkNow = 0;
    if (d.dicPos >= dicLimit) {
      if (d.remainLen == 0 && d.code == 0) { status = 4; goto done; }
      if (finishMode == 0) { status = 2; goto done; }
      if (d.remainLen != 0) { status = 2; rc = 1; goto done; }
      checkEndMarkNow = 1;
    }
    if (d.needInitState) { for (u32 i = 0; i < nprobs; i++) d.probs[i] = LZ_kBitModelTotal >> 1; d.reps[0] = d.reps[1] = d.reps[2] = d.reps[3] = 1; d.state = 0; d.needInitState = 0; }
    if (d.tempBufSize == 0) {
      u32 processed; const u8 *bufLimit;
      if (inSize < LZ_REQUIRED_INPUT_MAX || checkEndMarkNow) {
        int dummyRes = lz_try_dummy(&d, src, inSize);
        if (dummyRes == LZ_DUMMY_ERROR) { e_memcpy(d.tempBuf, src, inSize); d.tempBufSize = inSize; srcLen += inSize; status = 3; goto done; }
        if (checkEndMarkNow && dummyRes != LZ_DUMMY_MATCH) { status = 2; rc = 1; goto done; }
        bufLimit = src;
      } else bufLimit = src + inSize - LZ_REQUIRED_INPUT_MAX;
      d.buf = src;
      if (lz_decode_real2(&d, dicLimit, bufLimit) != 0) { rc = 1; goto done; }
      processed = (u32)(d.buf - src); srcLen += processed; src += processed; inSize -= processed;
    } else {
      unsigned rem = d.tempBufSize, lookAhead = 0;
      while (rem < LZ_REQUIRED_INPUT_MAX && lookAhead < inSize) d.tempBuf[rem++] = src[lookAhead++];
      d.tempBufSize = rem;
      if (rem < LZ_REQUIRED_INPUT_MAX || checkEndMarkNow) {
        int dummyRes = lz_try_dummy(&d, d.tempBuf, rem);
        if (dummyRes == LZ_DUMMY_ERROR) { srcLen += lookAhead; status = 3; goto done; }
        if (checkEndMarkNow && dummyRes != LZ_DUMMY_MATCH) { status = 2; rc = 1; goto done; }
      }
      d.buf = d.tempBuf;
      if (lz_decode_real2(&d, dicLimit, d.buf) != 0) { rc = 1; goto done; }
      lookAhead -= (rem - (unsigned)(d.buf - d.tempBuf)); srcLen += lookAhead; src += lookAhead; inSize -= lookAhead; d.tempBufSize = 0;
    }
  }
  if (d.code == 0) status = 1; rc = (d.code == 0) ? 0 : 1;
done:
  /* back into the guest struct (p->buf as a guest address: inside src or inside tempBuf) */
  { u32 gb = d.buf >= d.tempBuf && d.buf <= d.tempBuf + LZ_REQUIRED_INPUT_MAX ? gp + 92 + (u32)(d.buf - d.tempBuf) : gsrc + (u32)(d.buf - src0);
    lz_put32(S + 24, gb); lz_put32(S + 28, d.range); lz_put32(S + 32, d.code); lz_put32(S + 36, d.dicPos); lz_put32(S + 44, d.processedPos); lz_put32(S + 48, d.checkDicSize); lz_put32(S + 52, d.state);
    for (int i = 0; i < 4; i++) lz_put32(S + 56 + 4 * i, d.reps[i]); lz_put32(S + 72, d.remainLen); lz_put32(S + 76, (u32)d.needFlush); lz_put32(S + 80, (u32)d.needInitState); lz_put32(S + 88, d.tempBufSize); e_memcpy(S + 92, d.tempBuf, LZ_REQUIRED_INPUT_MAX); }
  wr32(c, srcLenPtr, srcLen); wr32(c, statusPtr, status);
  return rc;
}

static u64 g_hle_lzma_calls, g_hle_lzma_bytes; int g_hle_off = 0;   /* x64_set_hle(0) / X64_NOHLE=1: keep the guest's own decoder (comparison, fallback) */
static void s_hle_LzmaDec_DecodeToDic(Cpu *c) {
  u32 gp = (u32)g_entry_eax /* the dispatcher zeroes eax before calling a shim; Borland register convention passes the first argument there */, dicLimit = (u32)c->r[RDX], gsrc = (u32)c->r[RCX]; u32 statusPtr = (u32)ARG(0), finishMode = (u32)ARG(1), srcLenPtr = (u32)ARG(2);
  u32 pos0 = rd32(c, gp + 36); int rc = lz_decode_to_dic(c, gp, dicLimit, gsrc, srcLenPtr, finishMode, statusPtr); if (c->status != RUN_OK) return;
  g_hle_lzma_calls++; g_hle_lzma_bytes += rd32(c, gp + 36) - pos0; RET((u32)rc);
}

/* signature of bcc32-built LzmaDec_DecodeToDic (LZMA SDK 4.65–9.20 as shipped in Inno Setup 5.x/6.x): prologue + "inSize = *srcLen; *srcLen = 0;" */
static const u8 LZ_SIG[] = { 0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xEC, 0x53, 0x56, 0x57, 0x89, 0x4D, 0xF8, 0x89, 0x55, 0xFC, 0x8B, 0xD8, 0x8B, 0x45, 0x10, 0x33, 0xD2, 0x8B, 0x30, 0x8B, 0x45, 0x10, 0x89, 0x10 };
/* entry patch: jmp rel32 into the shim slot (the slot region lies below every 32-bit image, so rel32 always reaches) */

/* ---------- Inno Setup SHA1Update (SHA1.pas, Delphi register convention: eax = context, edx = buffer, ecx = length; no stack arguments) ----------
   Every file Inno Setup extracts is hashed (SHA-1 in 5.x/6.0-6.3): on the phone this was ~half of the installation time.
   TSHA1Context: state[5] @0, count lo/hi (bytes) @0x14/0x18, buffer[64] @0x1c, W[80] @0x5c (scratch of SHA1Transform). */
static u32 hle_sha1_rol(u32 x, int n) { return (x << n) | (x >> (32 - n)); }
static void hle_sha1_block(u32 st[5], const u8 *p) {
  u32 w[80]; for (int i = 0; i < 16; i++) w[i] = (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16 | (u32)p[4 * i + 2] << 8 | p[4 * i + 3];
  for (int i = 16; i < 80; i++) w[i] = hle_sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  u32 a = st[0], b = st[1], c = st[2], d = st[3], e = st[4];
  for (int i = 0; i < 80; i++) { u32 f, k;
    if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; } else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; } else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; } else { f = b ^ c ^ d; k = 0xCA62C1D6; }
    u32 t = hle_sha1_rol(a, 5) + f + e + k + w[i]; e = d; d = c; c = hle_sha1_rol(b, 30); b = a; a = t; }
  st[0] += a; st[1] += b; st[2] += c; st[3] += d; st[4] += e;
}
static u64 g_hle_sha1_bytes;
static void s_hle_SHA1Update(Cpu *c) {
  u32 ctx = (u32)g_entry_eax, buf = (u32)c->r[RDX], len = (u32)c->r[RCX];
  u8 *C = mem_ptr(c->mem, ctx, 0x5c, 1); if (!C) { cpu_fault(c, "SHA1Update: bad context 0x%x", ctx); return; }
  const u8 *B = len ? mem_ptr(c->mem, buf, len, 0) : 0; if (len && !B) { cpu_fault(c, "SHA1Update: bad buffer 0x%x+%u", buf, len); return; }
  u32 st[5]; for (int i = 0; i < 5; i++) st[i] = lz_le32(C + 4 * i);
  u32 old = lz_le32(C + 0x14), cnt = old + len; lz_put32(C + 0x14, cnt); if (old > cnt) lz_put32(C + 0x18, lz_le32(C + 0x18) + 1);
  u32 j = old & 63, room = 64 - j; u8 *bufr = C + 0x1c;
  if (len < room) { if (len) e_memcpy(bufr + j, B, len); g_hle_sha1_bytes += len; return; }
  e_memcpy(bufr + j, B, room); hle_sha1_block(st, bufr); B += room; len -= room;
  while (len >= 64) { hle_sha1_block(st, B); B += 64; len -= 64; }
  e_memcpy(bufr, B, 64); /* the guest copies the last whole block through its buffer: keep the buffer bytes identical */
  if (len) e_memcpy(bufr, B, len);
  for (int i = 0; i < 5; i++) lz_put32(C + 4 * i, st[i]); g_hle_sha1_bytes += len;
}
/* Inno's SHA1Update prologue: push ebx/esi/edi/ebp; esi=len edi=buf ebx=ctx; ebp=count; count+=len; carry -> inc hi; ebp&=63; eax=64-ebp */
static const u8 SHA1_SIG[] = { 0x53, 0x56, 0x57, 0x55, 0x8B, 0xF1, 0x8B, 0xD8, 0x8B, 0xFA, 0x8B, 0x6B, 0x14, 0x01, 0x73, 0x14, 0x3B, 0x6B, 0x14, 0x76, 0x03, 0xFF, 0x43, 0x18, 0x83, 0xE5, 0x3F, 0xB8, 0x40, 0x00, 0x00, 0x00, 0x2B, 0xC5 };

/* ---------- zlib inflate_fast (inffast.c of zlib 1.2.5-1.2.8, bcc32 register convention: eax = strm, edx = start) ----------
   GOG's uninstall.dll (shipped in every GOG Inno installer) inflates its payload through a statically linked zlib: the other half of the
   installation time.  z_stream: next_in @0, avail_in @4, total_in @8, next_out @0xc, avail_out @0x10, total_out @0x14, msg @0x18, state @0x1c.
   inflate_state (1.2.5-1.2.8 layout, verified against the DLL's code): mode @0, dmax @0x14, wsize @0x28, whave @0x2c, wnext @0x30, window @0x34,
   hold @0x38, bits @0x3c, lencode @0x4c, distcode @0x50, lenbits @0x54, distbits @0x58, sane @0x1c3c (not used: -DINFLATE_STRICT off). */
#define ZF_BAD 27   /* inflate_mode BAD in the zlib this signature matches (1.2.5: no COPY_/LEN_ states; the DLL stores 0x1b there, TYPE = 0xb) */
static u64 g_hle_inflate_bytes;
static void s_hle_inflate_fast(Cpu *c) {
  u32 strm = (u32)g_entry_eax, start = (u32)c->r[RDX];
  u8 *S = mem_ptr(c->mem, strm, 0x20, 1); if (!S) { cpu_fault(c, "inflate_fast: bad z_stream 0x%x", strm); return; }
  u32 st = lz_le32(S + 0x1c); u8 *ST = mem_ptr(c->mem, st, 0x60, 1); if (!ST) { cpu_fault(c, "inflate_fast: bad inflate_state 0x%x", st); return; }
  u32 gin = lz_le32(S), avail_in = lz_le32(S + 4), gout = lz_le32(S + 0xc), avail_out = lz_le32(S + 0x10);
  u32 wsize = lz_le32(ST + 0x28), whave = lz_le32(ST + 0x2c), wnext = lz_le32(ST + 0x30), gwindow = lz_le32(ST + 0x34);
  u32 hold = lz_le32(ST + 0x38), bits = lz_le32(ST + 0x3c), glen = lz_le32(ST + 0x4c), gdist = lz_le32(ST + 0x50), lmask = (1u << lz_le32(ST + 0x54)) - 1, dmask = (1u << lz_le32(ST + 0x58)) - 1;
  u32 dmax = lz_le32(ST + 0x14);
  /* the C code reads up to 5 bytes behind avail_in and writes up to 257 bytes behind avail_out: inflate() guarantees avail_in >= 6 and avail_out >= 258 on entry */
  if (avail_in < 6 || avail_out < 258) { return; }
  u8 *in = mem_ptr(c->mem, gin, avail_in, 0), *out = mem_ptr(c->mem, gout, avail_out, 1), *window = wsize ? mem_ptr(c->mem, gwindow, wsize, 0) : 0;
  const u8 *lcode = mem_ptr(c->mem, glen, (lmask + 1) * 4, 0), *dcode = mem_ptr(c->mem, gdist, (dmask + 1) * 4, 0);
  if (!in || !out || (wsize && !window) || !lcode || !dcode) { cpu_fault(c, "inflate_fast: bad buffers"); return; }
  const u8 *last = in + (avail_in - 5); u8 *beg = out - (start - avail_out), *end = out + (avail_out - 257);
  const u8 *in0 = in; u8 *out0 = out; int bad = 0;
  do {
    if (bits < 15) { hold += (u32)*in++ << bits; bits += 8; hold += (u32)*in++ << bits; bits += 8; }
    const u8 *here = lcode + (hold & lmask) * 4; u32 op, len, dist;
  dolen:
    op = here[1]; hold >>= op; bits -= op; op = here[0];
    if (op == 0) { *out++ = here[2]; }
    else if (op & 16) {
      len = (u32)here[2] | (u32)here[3] << 8; op &= 15;
      if (op) { if (bits < op) { hold += (u32)*in++ << bits; bits += 8; } len += hold & ((1u << op) - 1); hold >>= op; bits -= op; }
      if (bits < 15) { hold += (u32)*in++ << bits; bits += 8; hold += (u32)*in++ << bits; bits += 8; }
      here = dcode + (hold & dmask) * 4;
    dodist:
      op = here[1]; hold >>= op; bits -= op; op = here[0];
      if (op & 16) {
        dist = (u32)here[2] | (u32)here[3] << 8; op &= 15;
        if (bits < op) { hold += (u32)*in++ << bits; bits += 8; if (bits < op) { hold += (u32)*in++ << bits; bits += 8; } }
        dist += hold & ((1u << op) - 1); (void)dmax; hold >>= op; bits -= op;
        op = (u32)(out - beg);
        if (dist > op) {   /* copy from window */
          op = dist - op;
          if (op > whave) { bad = 1; break; }   /* "invalid distance too far back" (sane mode) */
          const u8 *from = window; u8 *from_w;
          if (wnext == 0) { from += wsize - op; if (op < len) { len -= op; do { *out++ = *from++; } while (--op); from_w = out - dist; from = from_w; } }
          else if (wnext < op) { from += wsize + wnext - op; op -= wnext; if (op < len) { len -= op; do { *out++ = *from++; } while (--op); from = window; if (wnext < len) { op = wnext; len -= op; do { *out++ = *from++; } while (--op); from = out - dist; } } }
          else { from += wnext - op; if (op < len) { len -= op; do { *out++ = *from++; } while (--op); from = out - dist; } }
          while (len > 2) { *out++ = *from++; *out++ = *from++; *out++ = *from++; len -= 3; }
          if (len) { *out++ = *from++; if (len > 1) *out++ = *from++; }
        } else {
          const u8 *from = out - dist;
          do { *out++ = *from++; *out++ = *from++; *out++ = *from++; len -= 3; } while (len > 2);
          if (len) { *out++ = *from++; if (len > 1) *out++ = *from++; }
        }
      }
      else if ((op & 64) == 0) { here = dcode + (((u32)here[2] | (u32)here[3] << 8) + (hold & ((1u << op) - 1))) * 4; goto dodist; }
      else { bad = 1; break; }   /* invalid distance code */
    }
    else if ((op & 64) == 0) { here = lcode + (((u32)here[2] | (u32)here[3] << 8) + (hold & ((1u << op) - 1))) * 4; goto dolen; }
    else if (op & 32) { lz_put32(ST, 11 /* TYPE */); break; }   /* end of block */
    else { bad = 1; break; }   /* invalid literal/length code */
  } while (in < last && out < end);
  if (bad) { lz_put32(ST, ZF_BAD); lz_put32(S + 0x18, 0); }
  /* return unused bytes (on entry, bits < 8, so in won't go too far back) */
  u32 len = bits >> 3; in -= len; bits -= len << 3; hold &= (1u << bits) - 1;
  lz_put32(S, gin + (u32)(in - in0)); lz_put32(S + 0xc, gout + (u32)(out - out0));
  lz_put32(S + 4, (u32)(in < last ? 5 + (last - in) : 5 - (in - last))); lz_put32(S + 0x10, (u32)(out < end ? 257 + (end - out) : 257 - (out - end)));
  lz_put32(ST + 0x38, hold); lz_put32(ST + 0x3c, bits); g_hle_inflate_bytes += (u64)(out - out0);
}
/* bcc32-built inflate_fast prologue: push ebx/esi/edi; sub esp,0x44; [esp]=strm; state=strm->state; in=next_in-1; last=in+(avail_in-5) */
static const u8 INFL_SIG[] = { 0x53, 0x56, 0x57, 0x83, 0xC4, 0xBC, 0x89, 0x04, 0x24, 0x8B, 0x04, 0x24, 0x8B, 0x48, 0x1C, 0x89, 0x4C, 0x24, 0x04, 0x8B, 0x04, 0x24, 0x8B, 0x08, 0x49, 0x89, 0x4C, 0x24, 0x08, 0x8B, 0x04, 0x24, 0x8B, 0x48, 0x04, 0x83, 0xE9, 0x05 };

/* ---------- zlib adler32 (same DLL; bcc32 register convention: eax = adler, edx = buf, ecx = len; returns eax) ----------
   inflate() checksums every inflated byte with it — half of what remained after inflate_fast went native. */
static u64 g_hle_adler_bytes;
static void s_hle_adler32(Cpu *c) {
  u32 adler = (u32)g_entry_eax, buf = (u32)c->r[RDX], len = (u32)c->r[RCX];
  if (!buf) { RET(1); return; }
  const u8 *B = len ? mem_ptr(c->mem, buf, len, 0) : 0; if (len && !B) { cpu_fault(c, "adler32: bad buffer 0x%x+%u", buf, len); return; }
  u32 a = adler & 0xffff, b = adler >> 16; g_hle_adler_bytes += len;
  while (len) { u32 n = len > 5552 ? 5552 : len; len -= n; while (n--) { a += *B++; b += a; } a %= 65521; b %= 65521; }
  RET(b << 16 | a);
}
static const u8 ADLER_SIG[] = { 0x53, 0x56, 0x8B, 0xD8, 0x57, 0x8B, 0xFB, 0x55, 0xC1, 0xEF, 0x10, 0x8B, 0xE9, 0x81, 0xE7, 0xFF, 0xFF, 0x00, 0x00, 0x81, 0xE3, 0xFF, 0xFF, 0x00, 0x00, 0x83, 0xFD, 0x01, 0x8B, 0xF2, 0x75, 0x30 };

static int hle_patch(Win32 *w, u8 *img, u64 base, u64 o, const char *name, ShimFn fn, int clean32, const char *what) {
  u64 slot = w32_shim_slot(w, "nebula_hle", name); if (!slot) return 0;
  { int idx = (int)((slot - G_SHIM_BASE) / SHIM_SLOT_SIZE); w->slots[idx].clean32 = (u16)clean32; w->slots[idx].warned32 = 1; w->slots[idx].fn = fn; }
  u64 at = base + o; s32 rel = (s32)((s64)slot - (s64)(at + 5)); img[o] = 0xE9; lz_put32(img + o + 1, (u32)rel);
  w32_report(w, "[hle] %s at 0x%llx -> %s", name, (unsigned long long)at, what); return 1;
}
/* scan a freshly mapped 32-bit image (exe or DLL) for hot library routines we can run natively; each is patched once per image */
void w32_hle_scan(Win32 *w, u8 *img, u64 base, u64 size) {
  if (!w->mode32 || size < 64 || g_hle_off) return;
  int got_lz = 0, got_sha = 0, got_infl = 0, got_adler = 0;
  for (u64 o = 0; o + 48 <= size; o++) {
    u8 b = img[o];
    if (b == 0x55 && !got_lz && o + sizeof LZ_SIG + 8 <= size && !e_memcmp(img + o, LZ_SIG, sizeof LZ_SIG)) {
      /* the body must also contain "cmp dword [ebx+0x58], 5" (tempBufSize < RC_INIT_SIZE) within the next 160 bytes */
      static const u8 chk[] = { 0x83, 0x7B, 0x58, 0x05 }; int ok = 0; for (u64 k = o; k + 4 <= size && k < o + 160; k++) if (!e_memcmp(img + k, chk, 4)) { ok = 1; break; }
      if (ok) got_lz = hle_patch(w, img, base, o, "LzmaDec_DecodeToDic", s_hle_LzmaDec_DecodeToDic, 12, "native LZMA decoder");
    } else if (b == 0x53 && img[o + 1] == 0x56 && img[o + 2] == 0x8B) {
      if (!got_adler && !e_memcmp(img + o, ADLER_SIG, sizeof ADLER_SIG)) got_adler = hle_patch(w, img, base, o, "adler32", s_hle_adler32, 0, "native zlib adler32");
    } else if (b == 0x53 && img[o + 1] == 0x56 && img[o + 2] == 0x57) {
      if (!got_sha && !e_memcmp(img + o, SHA1_SIG, sizeof SHA1_SIG)) got_sha = hle_patch(w, img, base, o, "SHA1Update", s_hle_SHA1Update, 0, "native SHA-1 (Inno Setup file verification)");
      else if (!got_infl && !e_memcmp(img + o, INFL_SIG, sizeof INFL_SIG)) got_infl = hle_patch(w, img, base, o, "inflate_fast", s_hle_inflate_fast, 0, "native zlib inflate_fast (GOG uninstall.dll)");
    }
    if (got_lz && got_sha && got_infl && got_adler) break;
  }
}
