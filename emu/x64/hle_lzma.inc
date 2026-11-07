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
void w32_hle_scan(Win32 *w, u8 *img, u64 base, u64 size) {
  if (!w->mode32 || size < sizeof LZ_SIG + 64 || g_hle_off) return;
  for (u64 o = 0; o + sizeof LZ_SIG + 8 <= size; o++) {
    if (img[o] != 0x55 || img[o + 1] != 0x8B || e_memcmp(img + o, LZ_SIG, sizeof LZ_SIG)) continue;
    /* the body must also contain "cmp dword [ebx+0x58], 5" (tempBufSize < RC_INIT_SIZE) within the next 160 bytes */
    static const u8 chk[] = { 0x83, 0x7B, 0x58, 0x05 }; int ok = 0; for (u64 k = o; k + 4 <= size && k < o + 160; k++) if (!e_memcmp(img + k, chk, 4)) { ok = 1; break; }
    if (!ok) continue;
    u64 slot = w32_shim_slot(w, "nebula_hle", "LzmaDec_DecodeToDic"); if (!slot) return;
    { int idx = (int)((slot - G_SHIM_BASE) / SHIM_SLOT_SIZE); w->slots[idx].clean32 = 12; w->slots[idx].warned32 = 1; w->slots[idx].fn = s_hle_LzmaDec_DecodeToDic; }
    u64 at = base + o; s32 rel = (s32)((s64)slot - (s64)(at + 5)); img[o] = 0xE9; lz_put32(img + o + 1, (u32)rel);
    w32_report(w, "[hle] LzmaDec_DecodeToDic at 0x%llx -> native LZMA decoder (installer extraction runs at host speed)", (unsigned long long)at);
    return;
  }
}
