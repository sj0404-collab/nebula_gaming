/* jpeg.inc — JPEG decoder for GDI+ / WIC (gdi.c TU): baseline and progressive Huffman JPEG (JFIF, Exif, Adobe), 8-bit,
   1 / 3 / 4 components with any sampling factors, restart markers, 8- and 16-bit quantisation tables. Output is the same
   0xAARRGGBB host buffer the PNG and BMP decoders produce. Chroma is upsampled by replication (no "fancy" filtering);
   arithmetic-coded, lossless and 12-bit JPEGs are rejected (as GDI+ does). Structure follows the classic stb_image flow. */
static const float jpg_cos[8][8] = {
  { 0.353553391f, 0.490392640f, 0.461939766f, 0.415734806f, 0.353553391f, 0.277785117f, 0.191341716f, 0.097545161f },
  { 0.353553391f, 0.415734806f, 0.191341716f, -0.097545161f, -0.353553391f, -0.490392640f, -0.461939766f, -0.277785117f },
  { 0.353553391f, 0.277785117f, -0.191341716f, -0.490392640f, -0.353553391f, 0.097545161f, 0.461939766f, 0.415734806f },
  { 0.353553391f, 0.097545161f, -0.461939766f, -0.277785117f, 0.353553391f, 0.415734806f, -0.191341716f, -0.490392640f },
  { 0.353553391f, -0.097545161f, -0.461939766f, 0.277785117f, 0.353553391f, -0.415734806f, -0.191341716f, 0.490392640f },
  { 0.353553391f, -0.277785117f, -0.191341716f, 0.490392640f, -0.353553391f, -0.097545161f, 0.461939766f, -0.415734806f },
  { 0.353553391f, -0.415734806f, 0.191341716f, 0.097545161f, -0.353553391f, 0.490392640f, -0.461939766f, 0.277785117f },
  { 0.353553391f, -0.490392640f, 0.461939766f, -0.415734806f, 0.353553391f, -0.277785117f, 0.191341716f, -0.097545161f } };
static const u8 jpg_zz[64 + 15] = { 0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63,
  63,63,63,63,63,63,63,63,63,63,63,63,63,63,63 };   /* extra entries guard a corrupt run past 63 */
typedef struct { u8 fast[512]; u16 code[256]; u8 values[256]; u8 size[257]; u32 maxcode[18]; int delta[17]; } JHuff;
typedef struct { int id, h, v, tq, hd, ha, dc_pred; int x, y, w2, h2; u8 *plane; short *coeff; int coeff_w, coeff_h; } JComp;
typedef struct { const u8 *p, *end; u32 code_buffer; int code_bits, marker, nomore; int eobrun; int w, h, ncomp, progressive, restart_interval, todo;
  int spec_start, spec_end, succ_high, succ_low, scan_n, order[4], hmax, vmax, mcu_x, mcu_y, mcu_w, mcu_h, jfif, adobe, adobe_transform, ok;
  u16 dq[4][64]; JHuff hdc[4], hac[4]; JComp comp[4]; } Jpg;
static int jpg_is(const u8 *p, size_t n) { return n > 3 && p[0] == 0xff && p[1] == 0xd8 && p[2] == 0xff; }
static int jh_build(JHuff *h, const u8 *count) { int k = 0, code = 0; for (int i = 0; i < 16; i++) for (int j = 0; j < count[i]; j++) { if (k >= 256) return 0; h->size[k++] = (u8)(i + 1); } h->size[k] = 0;
  k = 0; for (int j = 1; j <= 16; j++) { h->delta[j] = k - code; if (h->size[k] == j) { while (h->size[k] == j) h->code[k++] = (u16)code++; if (code - 1 >= (1 << j)) return 0; } h->maxcode[j] = (u32)code << (16 - j); code <<= 1; }
  h->maxcode[17] = 0xffffffffu; e_memset(h->fast, 255, 512); for (int i = 0; i < k; i++) { int s = h->size[i]; if (s <= 9) { int c = h->code[i] << (9 - s), m = 1 << (9 - s); for (int j = 0; j < m; j++) h->fast[c + j] = (u8)i; } } return 1; }
static int jpg_get8(Jpg *j) { return j->p < j->end ? *j->p++ : 0; }
static void jpg_grow(Jpg *j) { do { int b = j->nomore ? 0 : jpg_get8(j); if (b == 0xff) { int c = jpg_get8(j); while (c == 0xff) c = jpg_get8(j); if (c != 0) { j->marker = c; j->nomore = 1; return; } } j->code_buffer |= (u32)b << (24 - j->code_bits); j->code_bits += 8; } while (j->code_bits <= 24); }
static const u32 jpg_bmask[17] = { 0,1,3,7,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767,65535 };
static int jpg_huff(Jpg *j, JHuff *h) { if (j->code_bits < 16) jpg_grow(j); int c = (int)((j->code_buffer >> 23) & 511), k = h->fast[c];
  if (k < 255) { int s = h->size[k]; if (s > j->code_bits) return -1; j->code_buffer <<= s; j->code_bits -= s; return h->values[k]; }
  u32 temp = j->code_buffer >> 16; for (k = 10; ; k++) if (temp < h->maxcode[k]) break; if (k == 17) { j->code_bits -= 16; return -1; } if (k > j->code_bits) return -1;
  c = (int)(((j->code_buffer >> (32 - k)) & jpg_bmask[k])) + h->delta[k]; if (c < 0 || c >= 256) return -1; j->code_bits -= k; j->code_buffer <<= k; return h->values[c]; }
static int jpg_extend(Jpg *j, int n) { if (j->code_bits < n) jpg_grow(j); if (j->code_bits < n) return 0; int k = (int)(j->code_buffer >> (32 - n)); j->code_buffer <<= n; j->code_bits -= n; if (k < (1 << (n - 1))) k += (-1 << n) + 1; return k; }
static int jpg_bits(Jpg *j, int n) { if (j->code_bits < n) jpg_grow(j); if (j->code_bits < n) return 0; int k = (int)(j->code_buffer >> (32 - n)); j->code_buffer <<= n; j->code_bits -= n; return k; }
static int jpg_bit(Jpg *j) { if (j->code_bits < 1) jpg_grow(j); if (j->code_bits < 1) return 0; u32 k = j->code_buffer; j->code_buffer <<= 1; j->code_bits--; return (int)(k >> 31); }
static u8 jpg_clamp(float v) { int i = (int)(v + 128.5f); return i < 0 ? 0 : i > 255 ? 255 : (u8)i; }
static void jpg_idct(const short *in, u8 *out, int stride) { float tmp[64]; for (int v = 0; v < 8; v++) { for (int x = 0; x < 8; x++) { float s = 0; for (int u = 0; u < 8; u++) s += jpg_cos[x][u] * in[v * 8 + u]; tmp[v * 8 + x] = s; } }
  for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { float s = 0; for (int v = 0; v < 8; v++) s += jpg_cos[y][v] * tmp[v * 8 + x]; out[y * stride + x] = jpg_clamp(s); } }
static int jpg_block(Jpg *j, short *data, JHuff *hdc, JHuff *hac, const u16 *dq, int *dc_pred) { int t = jpg_huff(j, hdc); if (t < 0 || t > 15) return 0; e_memset(data, 0, 128);
  int diff = t ? jpg_extend(j, t) : 0; int dc = *dc_pred + diff; *dc_pred = dc; data[0] = (short)(dc * dq[0]); int k = 1;
  do { int rs = jpg_huff(j, hac); if (rs < 0) return 0; int s = rs & 15, r = rs >> 4; if (s == 0) { if (rs != 0xf0) break; k += 16; } else { k += r; int zig = jpg_zz[k++]; data[zig] = (short)(jpg_extend(j, s) * dq[zig]); } } while (k < 64); return 1; }
static int jpg_block_prog_dc(Jpg *j, short *data, JHuff *hdc, int *dc_pred) { if (j->spec_end != 0) return 0; if (j->code_bits < 16) jpg_grow(j);
  if (j->succ_high == 0) { e_memset(data, 0, 128); int t = jpg_huff(j, hdc); if (t < 0 || t > 15) return 0; int diff = t ? jpg_extend(j, t) : 0; int dc = *dc_pred + diff; *dc_pred = dc; data[0] = (short)(dc * (1 << j->succ_low)); }
  else { if (jpg_bit(j)) data[0] = (short)(data[0] + (1 << j->succ_low)); } return 1; }
static int jpg_block_prog_ac(Jpg *j, short *data, JHuff *hac) { if (j->spec_start == 0) return 0; int k;
  if (j->succ_high == 0) { int shift = j->succ_low; if (j->eobrun) { j->eobrun--; return 1; } k = j->spec_start;
    do { int rs = jpg_huff(j, hac); if (rs < 0) return 0; int s = rs & 15, r = rs >> 4; if (s == 0) { if (r < 15) { j->eobrun = 1 << r; if (r) j->eobrun += jpg_bits(j, r); j->eobrun--; break; } k += 16; } else { k += r; int zig = jpg_zz[k++]; data[zig] = (short)(jpg_extend(j, s) * (1 << shift)); } } while (k <= j->spec_end); }
  else { short bit = (short)(1 << j->succ_low);
    if (j->eobrun) { j->eobrun--; for (k = j->spec_start; k <= j->spec_end; k++) { short *p = &data[jpg_zz[k]]; if (*p != 0 && jpg_bit(j) && (*p & bit) == 0) { if (*p > 0) *p = (short)(*p + bit); else *p = (short)(*p - bit); } } }
    else { k = j->spec_start; do { int rs = jpg_huff(j, hac); if (rs < 0) return 0; int s = rs & 15, r = rs >> 4;
        if (s == 0) { if (r < 15) { j->eobrun = (1 << r) - 1; if (r) j->eobrun += jpg_bits(j, r); r = 64; } }
        else { if (s != 1) return 0; s = jpg_bit(j) ? bit : -bit; }
        while (k <= j->spec_end) { short *p = &data[jpg_zz[k++]]; if (*p != 0) { if (jpg_bit(j) && (*p & bit) == 0) { if (*p > 0) *p = (short)(*p + bit); else *p = (short)(*p - bit); } } else { if (r == 0) { *p = (short)s; break; } r--; } } } while (k <= j->spec_end); } } return 1; }
static void jpg_reset(Jpg *j) { j->code_bits = 0; j->code_buffer = 0; j->nomore = 0; j->marker = 0xff; j->eobrun = 0; for (int i = 0; i < 4; i++) j->comp[i].dc_pred = 0; j->todo = j->restart_interval ? j->restart_interval : 0x7fffffff; }
static int jpg_restart_ok(Jpg *j) { if (j->code_bits < 24) jpg_grow(j); if (j->marker >= 0xd0 && j->marker <= 0xd7) { jpg_reset(j); return 1; } return 0; }
static int jpg_scan(Jpg *j) { short data[64]; jpg_reset(j);
  if (j->scan_n == 1) { JComp *c = &j->comp[j->order[0]]; int bw = (c->x + 7) >> 3, bh = (c->y + 7) >> 3;
    for (int by = 0; by < bh; by++) for (int bx = 0; bx < bw; bx++) {
      if (!j->progressive) { if (!jpg_block(j, data, &j->hdc[c->hd], &j->hac[c->ha], j->dq[c->tq], &c->dc_pred)) return 0; jpg_idct(data, c->plane + by * 8 * c->w2 + bx * 8, c->w2); }
      else { short *d = c->coeff + 64 * (by * c->coeff_w + bx); if (j->spec_start == 0) { if (!jpg_block_prog_dc(j, d, &j->hdc[c->hd], &c->dc_pred)) return 0; } else if (!jpg_block_prog_ac(j, d, &j->hac[c->ha])) return 0; }
      if (--j->todo <= 0) { if (!jpg_restart_ok(j)) return 1; } } return 1; }
  for (int my = 0; my < j->mcu_y; my++) for (int mx = 0; mx < j->mcu_x; mx++) { for (int k = 0; k < j->scan_n; k++) { JComp *c = &j->comp[j->order[k]];
      for (int y = 0; y < c->v; y++) for (int x = 0; x < c->h; x++) { int bx = mx * c->h + x, by = my * c->v + y;
        if (!j->progressive) { if (!jpg_block(j, data, &j->hdc[c->hd], &j->hac[c->ha], j->dq[c->tq], &c->dc_pred)) return 0; jpg_idct(data, c->plane + by * 8 * c->w2 + bx * 8, c->w2); }
        else { if (!jpg_block_prog_dc(j, c->coeff + 64 * (by * c->coeff_w + bx), &j->hdc[c->hd], &c->dc_pred)) return 0; } } }
    if (--j->todo <= 0) { if (!jpg_restart_ok(j)) return 1; } } return 1; }
static int jpg_marker(Jpg *j) { if (j->marker != 0xff) { int m = j->marker; j->marker = 0xff; return m; } int x = jpg_get8(j); if (x != 0xff) return 0xff; while (x == 0xff) x = jpg_get8(j); return x; }
static int jpg_segment(Jpg *j, int m) { int len = (jpg_get8(j) << 8); len |= jpg_get8(j); if (len < 2 || j->p + len - 2 > j->end) return 0; const u8 *seg = j->p, *segend = j->p + len - 2; j->p = segend;
  switch (m) {
  case 0xdb: while (seg < segend) { int q = *seg++, pq = q >> 4, tq = q & 15; if (pq > 1 || tq > 3) return 0; for (int i = 0; i < 64; i++) { if (pq) { j->dq[tq][jpg_zz[i]] = (u16)(seg[0] << 8 | seg[1]); seg += 2; } else j->dq[tq][jpg_zz[i]] = *seg++; } } return 1;
  case 0xc4: while (seg + 17 <= segend) { int q = *seg++, tc = q >> 4, th = q & 15; if (tc > 1 || th > 3) return 0; u8 count[16]; int n = 0; for (int i = 0; i < 16; i++) { count[i] = seg[i]; n += seg[i]; } seg += 16; if (n > 256 || seg + n > segend) return 0;
      JHuff *h = tc == 0 ? &j->hdc[th] : &j->hac[th]; if (!jh_build(h, count)) return 0; for (int i = 0; i < n; i++) h->values[i] = seg[i]; seg += n; } return 1;
  case 0xdd: if (len != 4) return 0; j->restart_interval = seg[0] << 8 | seg[1]; return 1;
  case 0xe0: if (len >= 7 && !e_memcmp(seg, "JFIF", 5)) j->jfif = 1; return 1;
  case 0xee: if (len >= 14 && !e_memcmp(seg, "Adobe", 6)) { j->adobe = 1; j->adobe_transform = seg[11]; } return 1;
  case 0xc0: case 0xc1: case 0xc2: { if (len < 8) return 0; if (seg[0] != 8) return 0; j->h = seg[1] << 8 | seg[2]; j->w = seg[3] << 8 | seg[4]; j->ncomp = seg[5]; if (!j->w || !j->h || (j->ncomp != 1 && j->ncomp != 3 && j->ncomp != 4) || len != 8 + 3 * j->ncomp) return 0;
      if ((u64)j->w * j->h > 80u * 1024 * 1024) return 0;
      j->progressive = m == 0xc2; seg += 6; int hmax = 1, vmax = 1;
      for (int i = 0; i < j->ncomp; i++) { JComp *c = &j->comp[i]; c->id = seg[0]; c->h = seg[1] >> 4; c->v = seg[1] & 15; c->tq = seg[2]; seg += 3; if (!c->h || c->h > 4 || !c->v || c->v > 4 || c->tq > 3) return 0; if (c->h > hmax) hmax = c->h; if (c->v > vmax) vmax = c->v; }
      for (int i = 0; i < j->ncomp; i++) if (hmax % j->comp[i].h || vmax % j->comp[i].v) return 0;
      j->hmax = hmax; j->vmax = vmax; j->mcu_w = hmax * 8; j->mcu_h = vmax * 8; j->mcu_x = (j->w + j->mcu_w - 1) / j->mcu_w; j->mcu_y = (j->h + j->mcu_h - 1) / j->mcu_h;
      for (int i = 0; i < j->ncomp; i++) { JComp *c = &j->comp[i]; c->x = (j->w * c->h + hmax - 1) / hmax; c->y = (j->h * c->v + vmax - 1) / vmax; c->w2 = j->mcu_x * c->h * 8; c->h2 = j->mcu_y * c->v * 8; c->plane = emu_alloc((size_t)c->w2 * c->h2); if (!c->plane) return 0;
        if (j->progressive) { c->coeff_w = c->w2 / 8; c->coeff_h = c->h2 / 8; c->coeff = emu_alloc((size_t)c->coeff_w * c->coeff_h * 64 * sizeof(short)); if (!c->coeff) return 0; } } return 1; }
  case 0xda: { if (!j->ncomp) return 0; int ns = seg[0]; if (ns < 1 || ns > 4 || len != 6 + 2 * ns) return 0; j->scan_n = ns; seg++;
      for (int i = 0; i < ns; i++) { int id = seg[0], q = seg[1]; seg += 2; int which; for (which = 0; which < j->ncomp; which++) if (j->comp[which].id == id) break; if (which == j->ncomp) return 0; j->comp[which].hd = q >> 4; j->comp[which].ha = q & 15; if (j->comp[which].hd > 3 || j->comp[which].ha > 3) return 0; j->order[i] = which; }
      j->spec_start = seg[0]; j->spec_end = seg[1]; j->succ_high = seg[2] >> 4; j->succ_low = seg[2] & 15;
      if (j->progressive) { if (j->spec_start > 63 || j->spec_end > 63 || j->spec_start > j->spec_end || j->succ_high > 13 || j->succ_low > 13) return 0; } else { if (j->spec_start != 0 || j->succ_high != 0 || j->succ_low != 0) return 0; j->spec_end = 63; }
      return 2; }
  default: return 1; } }   /* APPn, COM, DNL and friends are skipped */
static void jpg_finish_prog(Jpg *j) { short data[64]; for (int i = 0; i < j->ncomp; i++) { JComp *c = &j->comp[i]; for (int by = 0; by < c->coeff_h; by++) for (int bx = 0; bx < c->coeff_w; bx++) { short *d = c->coeff + 64 * (by * c->coeff_w + bx); for (int k = 0; k < 64; k++) data[k] = (short)(d[k] * j->dq[c->tq][k]); jpg_idct(data, c->plane + by * 8 * c->w2 + bx * 8, c->w2); } } }
static u8 jpg_u8(int v) { return v < 0 ? 0 : v > 255 ? 255 : (u8)v; }
/* bring a component plane to full image size: factor-2 steps use libjpeg's "fancy" triangle filter (3/4 nearer + 1/4 farther sample), other factors replicate */
static u8 *jpg_upsample(JComp *c, int hmax, int vmax, int w, int h) { int hs = hmax / c->h, vs = vmax / c->v; int sw = c->x, sh = c->y; if (sw > c->w2) sw = c->w2; if (sh > c->h2) sh = c->h2;
  u8 *tmp = emu_alloc((size_t)sw * h); if (!tmp) return 0;
  for (int y = 0; y < h; y++) { const u8 *near, *far; int rnd = 0; if (vs == 2) { int sy = y >> 1; if (sy > sh - 1) sy = sh - 1; int fy = (y & 1) ? sy + 1 : sy - 1; if (fy < 0) fy = 0; if (fy > sh - 1) fy = sh - 1; near = c->plane + (size_t)sy * c->w2; far = c->plane + (size_t)fy * c->w2; rnd = (y & 1) ? 2 : 1; }
    else { int sy = y * c->v / vmax; if (sy > sh - 1) sy = sh - 1; near = far = c->plane + (size_t)sy * c->w2; }
    u8 *o = tmp + (size_t)y * sw; if (near == far) e_memcpy(o, near, sw); else for (int x = 0; x < sw; x++) o[x] = (u8)((3 * near[x] + far[x] + rnd) >> 2); }
  u8 *out = emu_alloc((size_t)w * h); if (!out) { emu_free(tmp); return 0; }
  for (int y = 0; y < h; y++) { const u8 *i = tmp + (size_t)y * sw; u8 *o = out + (size_t)y * w;
    if (hs == 1) e_memcpy(o, i, w < sw ? w : sw);
    else if (hs == 2) for (int x = 0; x < w; x++) { int sx = x >> 1; if (sx > sw - 1) sx = sw - 1; int fx = (x & 1) ? sx + 1 : sx - 1; if (fx < 0) fx = 0; if (fx > sw - 1) fx = sw - 1; o[x] = (u8)((3 * i[sx] + i[fx] + ((x & 1) ? 2 : 1)) >> 2); }
    else for (int x = 0; x < w; x++) { int sx = x * c->h / hmax; if (sx > sw - 1) sx = sw - 1; o[x] = i[sx]; } }
  emu_free(tmp); return out; }
static u32 *jpg_decode(const u8 *p, size_t n, int *out_w, int *out_h, int *has_alpha) { if (!jpg_is(p, n)) return 0; Jpg *j = emu_alloc(sizeof(Jpg)); if (!j) return 0; j->p = p + 2; j->end = p + n; j->marker = 0xff; u32 *out = 0;
  for (;;) { int m = jpg_marker(j); if (m == 0xd9) break; if (m == 0xff || m == 0) { if (j->p >= j->end) break; continue; } if (m >= 0xd0 && m <= 0xd7) continue;
    if ((m >= 0xc3 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc)) { e_log("[jpeg] unsupported SOF%d (arithmetic / lossless / hierarchical)\n", m - 0xc0); goto done; }
    int r = jpg_segment(j, m); if (!r) { e_log("[jpeg] bad segment 0x%02x\n", m); goto done; }
    if (r == 2) { if (!jpg_scan(j)) { e_log("[jpeg] corrupt scan data\n"); if (!j->progressive) goto done; break; } if (j->marker == 0xff) { while (j->p < j->end && *j->p != 0xff) j->p++; } } }
  if (!j->ncomp || !j->comp[0].plane) goto done;
  if (j->progressive) jpg_finish_prog(j);
  out = emu_alloc((size_t)j->w * j->h * 4); if (!out) goto done;
  { int rgb = j->ncomp == 3 && (j->adobe ? j->adobe_transform == 0 : (!j->jfif && j->comp[0].id == 'R' && j->comp[1].id == 'G' && j->comp[2].id == 'B'));
    u8 *full[4] = { 0, 0, 0, 0 }; for (int i = 0; i < j->ncomp; i++) { full[i] = jpg_upsample(&j->comp[i], j->hmax, j->vmax, j->w, j->h); if (!full[i]) { out = 0; goto done; } }
    for (int y = 0; y < j->h; y++) for (int x = 0; x < j->w; x++) { int s[4]; for (int i = 0; i < j->ncomp; i++) s[i] = full[i][(size_t)y * j->w + x];
      int R, G, B; if (j->ncomp == 1) R = G = B = s[0];
      else if (rgb) { R = s[0]; G = s[1]; B = s[2]; }
      else { int Y = s[0], cb = s[1] - 128, cr = s[2] - 128; if (j->ncomp == 4 && j->adobe_transform == 0) { R = s[0]; G = s[1]; B = s[2]; } else { R = Y + ((91881 * cr + 32768) >> 16); G = Y - ((22554 * cb + 46802 * cr - 32768) >> 16); B = Y + ((116130 * cb + 32768) >> 16); }
        if (j->ncomp == 4) { int k = s[3]; R = jpg_u8(R) * k / 255; G = jpg_u8(G) * k / 255; B = jpg_u8(B) * k / 255; } }   /* Adobe CMYK/YCCK are stored inverted */
      out[(size_t)y * j->w + x] = 0xff000000u | (u32)jpg_u8(R) << 16 | (u32)jpg_u8(G) << 8 | jpg_u8(B); }
    for (int i = 0; i < 4; i++) if (full[i]) emu_free(full[i]); }
  *out_w = j->w; *out_h = j->h; *has_alpha = 0;
done: for (int i = 0; i < 4; i++) { if (j->comp[i].plane) emu_free(j->comp[i].plane); if (j->comp[i].coeff) emu_free(j->comp[i].coeff); } emu_free(j); return out; }
