/* ttf.inc — TrueType font engine: parser (head/maxp/loca/glyf/cmap/hhea/hmtx/OS/2), outline flattener, signed-area
 * coverage rasterizer (font-rs style accumulation buffer) and a glyph cache. Faces are regular files in the guest VFS
 * (C:\Windows\Fonts\*.ttf), loaded on first use. Everything that measures or draws text goes through fnt_*; when a face
 * is missing the old 8x8 bitmap glyphs with a fixed cell width stay as the fallback, so nothing depends on the files. */

typedef struct { const u8 *d; u32 len; int tried, ok; u16 upem, nglyphs, nhm; int long_loca; u32 loca, glyf, glyf_len, hmtx, cmap4, cmap12; int win_asc, win_desc, xavg, advmax;
  char family[64]; int bold, italic; char path[260]; int mem_handle; } TtfFace;   /* family/bold/italic from the name + OS/2 tables (private fonts); path or mem_handle identify an AddFontResource entry */
enum { TF_SANS = 0, TF_SANS_B = 1, TF_MONO = 2, TF_MONO_B = 3, TF_N = 4, TF_PRIV = 32 };   /* 4 bundled faces + up to 32 private ones (AddFontResource / AddFontMemResourceEx) */
static TtfFace g_ttf[TF_N + TF_PRIV]; static int g_npriv;
static const char *const g_ttf_files[TF_N] = { "C:\\Windows\\Fonts\\ptsans.ttf", "C:\\Windows\\Fonts\\ptsansb.ttf", "C:\\Windows\\Fonts\\cousine.ttf", "C:\\Windows\\Fonts\\cousineb.ttf" };

static u32 tt16(const TtfFace *f, u32 o) { return o + 2 <= f->len ? ((u32)f->d[o] << 8) | f->d[o + 1] : 0; }
static int tts16(const TtfFace *f, u32 o) { return (s16)tt16(f, o); }
static u32 tt32(const TtfFace *f, u32 o) { return o + 4 <= f->len ? ((u32)f->d[o] << 24) | ((u32)f->d[o + 1] << 16) | ((u32)f->d[o + 2] << 8) | f->d[o + 3] : 0; }

static int ttf_parse(TtfFace *f) {
  u32 nt = tt16(f, 4); if (nt == 0 || nt > 64) return 0; u32 head = 0, maxp = 0, hhea = 0, os2 = 0, cmap = 0, name = 0;
  for (u32 i = 0; i < nt; i++) { u32 r = 12 + 16 * i; u32 tag = tt32(f, r), off = tt32(f, r + 8), ln = tt32(f, r + 12); if (off >= f->len || ln > f->len - off) continue;
    switch (tag) { case 0x68656164: head = off; break; case 0x6d617870: maxp = off; break; case 0x68686561: hhea = off; break; case 0x4f532f32: os2 = off; break; case 0x636d6170: cmap = off; break;
      case 0x6c6f6361: f->loca = off; break; case 0x676c7966: f->glyf = off; f->glyf_len = ln; break; case 0x686d7478: f->hmtx = off; break; case 0x6e616d65: name = off; break; default: break; } }
  if (name) {   /* family name (nameID 1): Windows/Unicode UTF-16BE preferred, else Macintosh Roman */
    u32 cnt = tt16(f, name + 2), so = tt16(f, name + 4); if (cnt > 512) cnt = 512; int best = -1;
    for (u32 i = 0; i < cnt; i++) { u32 r = name + 6 + 12 * i; u32 pid = tt16(f, r), nid = tt16(f, r + 6); if (nid != 1) continue; int score = pid == 3 ? (tt16(f, r + 4) == 0x409 ? 3 : 2) : pid == 1 ? 1 : 0; if (best < 0 || score > best) { best = score;
        u32 ln = tt16(f, r + 8), off = name + so + tt16(f, r + 10); if (off + ln > f->len) { best = -1; continue; } int k = 0;
        if (pid == 3 || pid == 0) { for (u32 j = 0; j + 1 < ln && k < 63; j += 2) { u32 ch = ((u32)f->d[off + j] << 8) | f->d[off + j + 1]; if (ch < 0x80) f->family[k++] = (char)ch; else if (ch < 0x800 && k < 62) { f->family[k++] = (char)(0xc0 | (ch >> 6)); f->family[k++] = (char)(0x80 | (ch & 0x3f)); } else if (k < 61) { f->family[k++] = (char)(0xe0 | (ch >> 12)); f->family[k++] = (char)(0x80 | ((ch >> 6) & 0x3f)); f->family[k++] = (char)(0x80 | (ch & 0x3f)); } } }
        else for (u32 j = 0; j < ln && k < 63; j++) f->family[k++] = (char)f->d[off + j];
        f->family[k] = 0; } }
  }
  if (head) { u32 mac = tt16(f, head + 44); f->bold = !!(mac & 1); f->italic = !!(mac & 2); }
  if (!head || !maxp || !hhea || !cmap || !f->loca || !f->glyf || !f->hmtx) return 0;
  f->upem = (u16)tt16(f, head + 18); if (f->upem < 16) return 0; f->long_loca = tts16(f, head + 50) != 0; f->nglyphs = (u16)tt16(f, maxp + 4); f->nhm = (u16)tt16(f, hhea + 34); if (!f->nhm) return 0;
  f->advmax = (int)tt16(f, hhea + 10);
  if (os2) { f->xavg = tts16(f, os2 + 2); f->win_asc = (int)tt16(f, os2 + 74); f->win_desc = (int)tt16(f, os2 + 76); u32 sel = tt16(f, os2 + 62); if (sel & 0x20) f->bold = 1; if (sel & 1) f->italic = 1; if (tt16(f, os2 + 4) >= 600) f->bold = 1; }
  if (f->win_asc + f->win_desc < (int)f->upem / 2) { f->win_asc = tts16(f, hhea + 4); f->win_desc = -tts16(f, hhea + 6); }
  if (f->xavg <= 0) f->xavg = (int)f->upem / 2;
  u32 ns = tt16(f, cmap + 2); if (ns > 32) ns = 32;
  for (u32 i = 0; i < ns; i++) { u32 pid = tt16(f, cmap + 4 + 8 * i), so = tt32(f, cmap + 8 + 8 * i); u32 sub = cmap + so; if (sub >= f->len) continue; u32 fmt = tt16(f, sub);
    if (fmt == 4 && (pid == 3 || pid == 0) && !f->cmap4) f->cmap4 = sub; else if (fmt == 12 && (pid == 3 || pid == 0) && !f->cmap12) f->cmap12 = sub; }
  return f->cmap4 || f->cmap12; }

static TtfFace *ttf_face(int idx) {
  if (idx >= TF_N && idx < TF_N + g_npriv) return g_ttf[idx].ok ? &g_ttf[idx] : 0;
  if (idx < 0 || idx >= TF_N) return 0; TtfFace *f = &g_ttf[idx]; if (f->ok) return f; if (f->tried) return 0; f->tried = 1;
  int fd = vfs_open(g_ttf_files[idx], 0x80000000u, 3, 0); if (fd < 0) return 0; s64 n = vfs_size(fd); if (n <= 64 || n > 64 * 1024 * 1024) { vfs_close(fd); return 0; }
  u8 *buf = emu_alloc((size_t)n); if (!buf) { vfs_close(fd); return 0; } s64 got = vfs_read(fd, buf, (u64)n); vfs_close(fd); if (got != n) { emu_free(buf); return 0; }
  f->d = buf; f->len = (u32)n; if (!ttf_parse(f)) { emu_free(buf); f->d = 0; f->len = 0; return 0; } f->ok = 1; return f; }

/* ---- private fonts (AddFontResource / AddFontMemResourceEx): faces the program ships, selectable by family name ---- */
static int ci_eq(const char *a, const char *b) { for (;; a++, b++) { int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : (u8)*a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : (u8)*b; if (x != y) return 0; if (!x) return 1; } }
static int ttf_priv_install(const u8 *d, u32 len, const char *path, int mem_handle) {
  if (!d || len < 64) return 0; u32 tag = ((u32)d[0] << 24) | ((u32)d[1] << 16) | ((u32)d[2] << 8) | d[3];
  if (tag == 0x74746366) {   /* 'ttcf' collection: install every face */
    u32 n = len >= 12 ? ((u32)d[8] << 24) | ((u32)d[9] << 16) | ((u32)d[10] << 8) | d[11] : 0; if (n > 16) n = 16; int added = 0;
    for (u32 i = 0; i < n && 12 + 4 * i + 4 <= len; i++) { u32 off = ((u32)d[12 + 4 * i] << 24) | ((u32)d[13 + 4 * i] << 16) | ((u32)d[14 + 4 * i] << 8) | d[15 + 4 * i]; if (off + 12 > len) continue;
      /* table offsets inside a collection are relative to the file start: present the whole file with the face's offset table copied to the front is not possible, so parse in place */
      if (g_npriv >= TF_PRIV) break; TtfFace *f = &g_ttf[TF_N + g_npriv]; e_memset(f, 0, sizeof *f); f->d = d; f->len = len;
      /* ttf_parse reads the table directory at offset 0: temporarily view the face's directory by copying the file header */
      u8 *copy = emu_alloc(len); if (!copy) break; e_memcpy(copy, d, len); e_memcpy(copy, d + off, 12 + 16 * (((u32)d[off + 4] << 8 | d[off + 5]) > 64 ? 64 : ((u32)d[off + 4] << 8 | d[off + 5]))); f->d = copy;
      if (ttf_parse(f)) { f->ok = 1; if (path) e_snprintf(f->path, sizeof f->path, "%s", path); f->mem_handle = mem_handle; g_npriv++; added++; } else { emu_free(copy); e_memset(f, 0, sizeof *f); } }
    return added; }
  if (tag != 0x00010000 && tag != 0x74727565 && tag != 0x4f54544f) return 0;   /* TrueType outlines only ('OTTO' = CFF: parsed, fails at glyf) */
  if (g_npriv >= TF_PRIV) return 0; TtfFace *f = &g_ttf[TF_N + g_npriv]; e_memset(f, 0, sizeof *f); f->d = d; f->len = len;
  if (!ttf_parse(f)) { e_memset(f, 0, sizeof *f); return 0; }
  f->ok = 1; if (path) e_snprintf(f->path, sizeof f->path, "%s", path); f->mem_handle = mem_handle; g_npriv++; return 1; }
/* AddFontResource(Ex): the file on the guest disk; returns the number of faces added (0 = not a usable TrueType file) */
int gdi_add_font_file(const char *path) {
  if (!path || !*path) return 0;
  for (int i = 0; i < g_npriv; i++) if (g_ttf[TF_N + i].ok && ci_eq(g_ttf[TF_N + i].path, path)) return 1;   /* already installed */
  int fd = vfs_open(path, 0x80000000u, 3, 0); if (fd < 0) return 0; s64 n = vfs_size(fd); if (n <= 64 || n > 64 * 1024 * 1024) { vfs_close(fd); return 0; }
  u8 *buf = emu_alloc((size_t)n); if (!buf) { vfs_close(fd); return 0; } s64 got = vfs_read(fd, buf, (u64)n); vfs_close(fd); if (got != n) { emu_free(buf); return 0; }
  int r = ttf_priv_install(buf, (u32)n, path, 0); if (!r) emu_free(buf);
  if (r) e_log("[font] AddFontResource %s: family \"%s\"%s%s\n", path, g_ttf[TF_N + g_npriv - 1].family, g_ttf[TF_N + g_npriv - 1].bold ? " bold" : "", g_ttf[TF_N + g_npriv - 1].italic ? " italic" : "");
  else e_log("[font] AddFontResource %s: not a TrueType font (CFF/OpenType-PS, .fon and Type 1 are not supported)\n", path);
  return r; }
int gdi_remove_font_file(const char *path) { int n = 0; for (int i = 0; i < g_npriv; i++) if (g_ttf[TF_N + i].ok && ci_eq(g_ttf[TF_N + i].path, path)) { g_ttf[TF_N + i].ok = 0; n++; } return n; }
/* AddFontMemResourceEx: a copy of the font bytes; the returned handle is removed with RemoveFontMemResourceEx */
static int g_font_mem_next = 0x7000;
int gdi_add_font_mem(const u8 *data, u32 len, u32 *nfaces) {
  if (!data || len < 64) { if (nfaces) *nfaces = 0; return 0; } u8 *copy = emu_alloc(len); if (!copy) return 0; e_memcpy(copy, data, len);
  int h = ++g_font_mem_next; int r = ttf_priv_install(copy, len, 0, h); if (!r) { emu_free(copy); if (nfaces) *nfaces = 0; return 0; }
  if (nfaces) *nfaces = (u32)r; e_log("[font] AddFontMemResourceEx: family \"%s\" (%d face%s)\n", g_ttf[TF_N + g_npriv - 1].family, r, r == 1 ? "" : "s"); return h; }
int gdi_remove_font_mem(int handle) { int n = 0; for (int i = 0; i < g_npriv; i++) if (g_ttf[TF_N + i].ok && g_ttf[TF_N + i].mem_handle == handle) { g_ttf[TF_N + i].ok = 0; n++; } return n; }
/* the private face for a LOGFONT family name, best style match (bold/italic), -1 when the program did not install one */
static int fnt_priv_find(const char *name, int bold, int italic) {
  if (!name || !*name) return -1; int best = -1, bs = -1;
  for (int i = 0; i < g_npriv; i++) { TtfFace *f = &g_ttf[TF_N + i]; if (!f->ok || !ci_eq(f->family, name)) continue; int sc = (f->bold == bold ? 2 : 0) + (f->italic == italic ? 1 : 0); if (sc > bs) { bs = sc; best = TF_N + i; } }
  return best; }

static int ttf_gid(const TtfFace *f, u32 cp) {
  if (f->cmap12 && (cp >= 0x10000 || !f->cmap4)) { u32 ng = tt32(f, f->cmap12 + 12); if (ng > 100000) ng = 100000; u32 lo = 0, hi = ng;
    while (lo < hi) { u32 mid = (lo + hi) / 2, g = f->cmap12 + 16 + 12 * mid; u32 sc = tt32(f, g), ec = tt32(f, g + 4); if (cp < sc) hi = mid; else if (cp > ec) lo = mid + 1; else return (int)(tt32(f, g + 8) + (cp - sc)); } return 0; }
  if (!f->cmap4 || cp > 0xffff) return 0; u32 segx2 = tt16(f, f->cmap4 + 6), seg = segx2 / 2; if (!seg) return 0; u32 ends = f->cmap4 + 14, starts = ends + segx2 + 2, deltas = starts + segx2, ros = deltas + segx2;
  u32 lo = 0, hi = seg; while (lo < hi) { u32 mid = (lo + hi) / 2; if (tt16(f, ends + 2 * mid) < cp) lo = mid + 1; else hi = mid; } if (lo >= seg) return 0;
  u32 sc = tt16(f, starts + 2 * lo); if (cp < sc) return 0; u32 ro = tt16(f, ros + 2 * lo), delta = tt16(f, deltas + 2 * lo); if (!ro) return (int)((cp + delta) & 0xffff);
  u32 ga = ros + 2 * lo + ro + 2 * (cp - sc); u32 g = tt16(f, ga); return g ? (int)((g + delta) & 0xffff) : 0; }

static int ttf_adv_units(const TtfFace *f, int gid) { int k = gid < f->nhm ? gid : f->nhm - 1; return (int)tt16(f, f->hmtx + 4 * (u32)k); }
static int ttf_glyph_loc(const TtfFace *f, int gid, u32 *off, u32 *ln) { if (gid < 0 || gid >= f->nglyphs) return 0; u32 a, b;
  if (f->long_loca) { a = tt32(f, f->loca + 4 * (u32)gid); b = tt32(f, f->loca + 4 * (u32)gid + 4); } else { a = 2 * tt16(f, f->loca + 2 * (u32)gid); b = 2 * tt16(f, f->loca + 2 * (u32)gid + 2); }
  if (b <= a || b > f->glyf_len) return 0; *off = f->glyf + a; *ln = b - a; return 1; }

/* ---- rasterizer: signed-area accumulation (exact-area coverage, no supersampling) ---- */
typedef struct { float *a; int w, h; } Ras;
typedef struct { float a, b, c, d, e, f; } Xf;   /* X = a*x + c*y + e, Y = b*x + d*y + f */
static float ffloor(float v) { int i = (int)v; return (float)(v < (float)i ? i - 1 : i); }
static float fceil(float v) { int i = (int)v; return (float)(v > (float)i ? i + 1 : i); }
static void ras_acc(Ras *r, int idx, float v) { if (idx >= 0 && idx < r->w * r->h) r->a[idx] += v; }
static void ras_line(Ras *r, float x0, float y0, float x1, float y1) {
  if ((y0 > y1 ? y0 - y1 : y1 - y0) <= 1e-5f) return; float dir = 1.0f; if (y0 > y1) { float t; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1.0f; }
  float lim = (float)(r->w - 1) - 0.001f; if (x0 < 0) x0 = 0; if (x1 < 0) x1 = 0; if (x0 > lim) x0 = lim; if (x1 > lim) x1 = lim;
  float dxdy = (x1 - x0) / (y1 - y0); float x = x0; int ys = y0 < 0 ? 0 : (int)y0; if (y0 < 0) x -= y0 * dxdy; int ye = (int)fceil(y1); if (ye > r->h) ye = r->h;
  for (int y = ys; y < ye; y++) { int ls = y * r->w; float dy = ((float)(y + 1) < y1 ? (float)(y + 1) : y1) - ((float)y > y0 ? (float)y : y0); float xnext = x + dxdy * dy; float d = dy * dir;
    float xa = x < xnext ? x : xnext, xb = x < xnext ? xnext : x; float x0f = ffloor(xa); int x0i = (int)x0f; float x1c = fceil(xb); int x1i = (int)x1c;
    if (x1i <= x0i + 1) { float xmf = 0.5f * (x + xnext) - x0f; ras_acc(r, ls + x0i, d - d * xmf); ras_acc(r, ls + x0i + 1, d * xmf); }
    else { float s = 1.0f / (xb - xa); float xf0 = xa - x0f; float a0 = 0.5f * s * (1.0f - xf0) * (1.0f - xf0); float xf1 = xb - x1c + 1.0f; float am = 0.5f * s * xf1 * xf1;
      ras_acc(r, ls + x0i, d * a0);
      if (x1i == x0i + 2) ras_acc(r, ls + x0i + 1, d * (1.0f - a0 - am));
      else { float a1 = s * (1.5f - xf0); ras_acc(r, ls + x0i + 1, d * (a1 - a0)); for (int xi = x0i + 2; xi < x1i - 1; xi++) ras_acc(r, ls + xi, d * s); float a2 = a1 + (float)(x1i - x0i - 3) * s; ras_acc(r, ls + x1i - 1, d * (1.0f - a2 - am)); }
      ras_acc(r, ls + x1i, d * am); }
    x = xnext; } }
static void ras_quad(Ras *r, float x0, float y0, float cx, float cy, float x1, float y1) {
  float dx = (x0 > cx ? x0 - cx : cx - x0) + (cx > x1 ? cx - x1 : x1 - cx), dy = (y0 > cy ? y0 - cy : cy - y0) + (cy > y1 ? cy - y1 : y1 - cy); float dd = dx > dy ? dx : dy; int n = (int)(e_sqrt(dd * 2.0f) + 0.5f); if (n < 1) n = 1; if (n > 24) n = 24;
  float px = x0, py = y0; for (int i = 1; i <= n; i++) { float t = (float)i / (float)n, u = 1.0f - t; float qx = u * u * x0 + 2 * u * t * cx + t * t * x1, qy = u * u * y0 + 2 * u * t * cy + t * t * y1; ras_line(r, px, py, qx, qy); px = qx; py = qy; } }

static void xf_apply(const Xf *x, float px, float py, float *ox, float *oy) { *ox = x->a * px + x->c * py + x->e; *oy = x->b * px + x->d * py + x->f; }
static Xf xf_compose(const Xf *o, const Xf *in) { Xf r; r.a = o->a * in->a + o->c * in->b; r.c = o->a * in->c + o->c * in->d; r.e = o->a * in->e + o->c * in->f + o->e; r.b = o->b * in->a + o->d * in->b; r.d = o->b * in->c + o->d * in->d; r.f = o->b * in->e + o->d * in->f + o->f; return r; }

/* emit one glyph's outline (simple or composite) through xf into the raster */
static void ttf_outline(const TtfFace *f, int gid, const Xf *xf, Ras *r, int depth) {
  u32 off, ln; if (depth > 5 || !ttf_glyph_loc(f, gid, &off, &ln) || ln < 10) return; int nc = tts16(f, off);
  if (nc < 0) { /* composite */ u32 p = off + 10; for (int k = 0; k < 64; k++) { u32 flags = tt16(f, p), cg = tt16(f, p + 2); p += 4; float dx, dy;
      if (flags & 1) { if (flags & 2) { dx = (float)tts16(f, p); dy = (float)tts16(f, p + 2); } else dx = dy = 0; p += 4; } else { if (flags & 2) { dx = (float)(s8)f->d[p < f->len ? p : 0]; dy = (float)(s8)f->d[p + 1 < f->len ? p + 1 : 0]; } else dx = dy = 0; p += 2; }
      Xf cx = { 1, 0, 0, 1, dx, dy };
      if (flags & 8) { float s = (float)tts16(f, p) / 16384.0f; p += 2; cx.a = cx.d = s; } else if (flags & 0x40) { cx.a = (float)tts16(f, p) / 16384.0f; cx.d = (float)tts16(f, p + 2) / 16384.0f; p += 4; }
      else if (flags & 0x80) { cx.a = (float)tts16(f, p) / 16384.0f; cx.b = (float)tts16(f, p + 2) / 16384.0f; cx.c = (float)tts16(f, p + 4) / 16384.0f; cx.d = (float)tts16(f, p + 6) / 16384.0f; p += 8; }
      Xf sub = xf_compose(xf, &cx); if ((int)cg != gid) ttf_outline(f, (int)cg, &sub, r, depth + 1); if (!(flags & 0x20) || p >= off + ln) break; } return; }
  if (nc == 0 || nc > 256) return; u32 ep = off + 10; int npts = (int)tt16(f, ep + 2 * (u32)(nc - 1)) + 1; if (npts <= 0 || npts > 4096) return; u32 il = tt16(f, ep + 2 * (u32)nc); u32 fp = ep + 2 * (u32)nc + 2 + il; if (fp > off + ln) return;
  u8 *flags = emu_alloc((size_t)npts); s16 *xs = emu_alloc(sizeof(s16) * (size_t)npts), *ys = emu_alloc(sizeof(s16) * (size_t)npts); if (!flags || !xs || !ys) { emu_free(flags); emu_free(xs); emu_free(ys); return; }
  u32 p = fp; for (int i = 0; i < npts;) { u8 fl = p < f->len ? f->d[p++] : 0; flags[i++] = fl; if (fl & 8) { int rep = p < f->len ? f->d[p++] : 0; while (rep-- > 0 && i < npts) flags[i++] = fl; } }
  int v = 0; for (int i = 0; i < npts; i++) { u8 fl = flags[i]; if (fl & 2) { int d = p < f->len ? f->d[p++] : 0; v += (fl & 16) ? d : -d; } else if (!(fl & 16)) { v += tts16(f, p); p += 2; } xs[i] = (s16)v; }
  v = 0; for (int i = 0; i < npts; i++) { u8 fl = flags[i]; if (fl & 4) { int d = p < f->len ? f->d[p++] : 0; v += (fl & 32) ? d : -d; } else if (!(fl & 32)) { v += tts16(f, p); p += 2; } ys[i] = (s16)v; }
  int s0 = 0; for (int ci = 0; ci < nc; ci++) { int e = (int)tt16(f, ep + 2 * (u32)ci); if (e < s0 || e >= npts) break; int n = e - s0 + 1; if (n < 2) { s0 = e + 1; continue; }
    /* start at an on-curve point (or the midpoint of the first two off-curve points) */ int first_on = -1; for (int k = 0; k < n; k++) if (flags[s0 + k] & 1) { first_on = k; break; }
    float sx, sy; if (first_on >= 0) xf_apply(xf, xs[s0 + first_on], ys[s0 + first_on], &sx, &sy); else { float ax, ay, bx, by; xf_apply(xf, xs[s0], ys[s0], &ax, &ay); xf_apply(xf, xs[s0 + 1 < e + 1 ? s0 + 1 : s0], ys[s0 + 1 < e + 1 ? s0 + 1 : s0], &bx, &by); sx = (ax + bx) * 0.5f; sy = (ay + by) * 0.5f; first_on = 0; }
    float cx = sx, cy = sy; int have_ctl = 0; float qx = 0, qy = 0;
    for (int k = 1; k <= n; k++) { int idx = s0 + (first_on + k) % n; int on = flags[idx] & 1; float px, py; xf_apply(xf, xs[idx], ys[idx], &px, &py); if (k == n && (flags[s0 + first_on] & 1) == 0) { px = sx; py = sy; on = 1; }
      if (on) { if (have_ctl) { ras_quad(r, cx, cy, qx, qy, px, py); have_ctl = 0; } else ras_line(r, cx, cy, px, py); cx = px; cy = py; }
      else { if (have_ctl) { float mx = (qx + px) * 0.5f, my = (qy + py) * 0.5f; ras_quad(r, cx, cy, qx, qy, mx, my); cx = mx; cy = my; } qx = px; qy = py; have_ctl = 1; } }
    if (have_ctl) ras_quad(r, cx, cy, qx, qy, sx, sy); else if (cx != sx || cy != sy) ras_line(r, cx, cy, sx, sy);
    s0 = e + 1; }
  emu_free(flags); emu_free(xs); emu_free(ys); }

/* ---- synthetic symbols: geometric glyphs that no bundled face covers (Windows would fall back to Segoe UI Symbol); 1000-unit em, baseline y = 0, y up.
   path grammar: M x y | L x y | Q cx cy x y | Z | S width x0 y0 x1 y1 ... (stroked polyline, until the next command) | C cx cy r (filled circle) */
typedef struct { u32 cp; u16 adv; const char *path; } SymGlyph;
static const SymGlyph g_syms[] = {
  { 0x21B5, 800, "S 70 680 600 680 240 160 240 S 70 320 400 160 240 320 80" },
  { 0x22EE, 500, "C 250 560 70 C 250 300 70 C 250 40 70" },
  { 0x22EF, 1000, "C 180 300 70 C 500 300 70 C 820 300 70" },
  { 0x2318, 800, "S 60 260 100 260 500 540 500 540 100 260 100 S 60 260 500 200 560 120 560 60 500 60 420 120 360 200 360 260 360 S 60 540 500 600 560 680 560 740 500 740 420 680 360 600 360 540 360 S 60 260 100 200 40 120 40 60 100 60 180 120 240 200 240 260 240 S 60 540 100 600 40 680 40 740 100 740 180 680 240 600 240 540 240" },
  { 0x23CE, 800, "S 70 680 600 680 240 160 240 S 70 320 400 160 240 320 80" },
  { 0x23F4, 800, "M 550 50 L 550 550 L 200 300 Z" },
  { 0x23F5, 800, "M 250 50 L 600 300 L 250 550 Z" },
  { 0x23F6, 800, "M 150 150 L 400 550 L 650 150 Z" },
  { 0x23F7, 800, "M 150 500 L 650 500 L 400 100 Z" },
  { 0x25B4, 600, "M 150 150 L 300 400 L 450 150 Z" },
  { 0x25B6, 800, "M 100 0 L 700 300 L 100 600 Z" },
  { 0x25B8, 600, "M 150 100 L 450 275 L 150 450 Z" },
  { 0x25BE, 600, "M 150 400 L 450 400 L 300 150 Z" },
  { 0x25C0, 800, "M 700 0 L 700 600 L 100 300 Z" },
  { 0x25C2, 600, "M 450 100 L 450 450 L 150 275 Z" },
  { 0x25C6, 800, "M 400 650 L 720 320 L 400 -10 L 80 320 Z" },
  { 0x25C7, 800, "S 70 400 650 720 320 400 -10 80 320 400 650 720 320" },
  { 0x2605, 800, "M 400 680 L 312 441 L 58 431 L 257 274 L 188 29 L 400 170 L 612 29 L 543 274 L 742 431 L 488 441 Z" },
  { 0x2606, 800, "S 50 400 680 312 441 58 431 257 274 188 29 400 170 612 29 543 274 742 431 488 441 400 680 312 441" },
  { 0x2610, 860, "S 55 100 -40 100 700 760 700 760 -40 100 -40 100 700" },
  { 0x2611, 860, "S 55 100 -40 100 700 760 700 760 -40 100 -40 100 700 S 90 230 330 390 170 720 560" },
  { 0x2612, 860, "S 55 100 -40 100 700 760 700 760 -40 100 -40 100 700 S 80 230 80 630 580 S 80 230 580 630 80" },
  { 0x2630, 800, "M 80 560 L 720 560 L 720 480 L 80 480 Z M 80 340 L 720 340 L 720 260 L 80 260 Z M 80 120 L 720 120 L 720 40 L 80 40 Z" },
  { 0x2713, 800, "S 80 100 330 300 130 720 640" },
  { 0x2714, 800, "S 150 110 330 320 110 720 640" },
  { 0x2715, 800, "S 70 130 30 670 570 S 70 130 570 670 30" },
  { 0x2716, 800, "S 150 130 30 670 570 S 150 130 570 670 30" },
  { 0x2717, 800, "S 80 120 40 680 560 S 80 140 580 660 20" },
  { 0x2718, 800, "S 150 120 40 680 560 S 150 140 580 660 20" },
  { 0x276E, 700, "S 110 520 620 180 300 520 -20" },
  { 0x276F, 700, "S 110 180 620 520 300 180 -20" },
  { 0x2795, 800, "M 320 620 L 480 620 L 480 380 L 720 380 L 720 220 L 480 220 L 480 -20 L 320 -20 L 320 220 L 80 220 L 80 380 L 320 380 Z" },
  { 0x2796, 800, "M 80 380 L 720 380 L 720 220 L 80 220 Z" },
  { 0x27A1, 820, "M 60 410 L 440 410 L 440 620 L 760 300 L 440 -20 L 440 190 L 60 190 Z" },
  { 0x27A4, 820, "M 60 560 L 760 300 L 60 40 L 200 300 Z" },
  { 0x2B05, 820, "M 760 410 L 380 410 L 380 620 L 60 300 L 380 -20 L 380 190 L 760 190 Z" },
  { 0x2B06, 800, "M 290 -20 L 290 360 L 80 360 L 400 680 L 720 360 L 510 360 L 510 -20 Z" },
  { 0x2B07, 800, "M 290 680 L 290 300 L 80 300 L 400 -20 L 720 300 L 510 300 L 510 680 Z" },
  { 0x2B95, 820, "M 60 410 L 440 410 L 440 620 L 760 300 L 440 -20 L 440 190 L 60 190 Z" },
  { 0x2BC5, 800, "M 150 150 L 400 550 L 650 150 Z" },
  { 0x2BC6, 800, "M 150 500 L 650 500 L 400 100 Z" },
  { 0x2BC7, 800, "M 550 50 L 550 550 L 200 300 Z" },
  { 0x2BC8, 800, "M 250 50 L 600 300 L 250 550 Z" },
  { 0xFF0B, 1000, "M 440 600 L 560 600 L 560 360 L 800 360 L 800 240 L 560 240 L 560 0 L 440 0 L 440 240 L 200 240 L 200 360 L 440 360 Z" },
};

static const SymGlyph *sym_find(u32 cp) { int n = (int)(sizeof g_syms / sizeof g_syms[0]), lo = 0, hi = n; while (lo < hi) { int mid = (lo + hi) / 2; if (g_syms[mid].cp < cp) lo = mid + 1; else hi = mid; } return lo < n && g_syms[lo].cp == cp ? &g_syms[lo] : 0; }
typedef struct { const Xf *xf; Ras *r; float bb[4]; int n; } SymSink;   /* r == 0: measure only (bb = xmin ymin xmax ymax in glyph units) */
static void sym_pt(SymSink *S, float x, float y) { if (!S->n || x < S->bb[0]) S->bb[0] = x; if (!S->n || y < S->bb[1]) S->bb[1] = y; if (!S->n || x > S->bb[2]) S->bb[2] = x; if (!S->n || y > S->bb[3]) S->bb[3] = y; S->n++; }
static void sym_line(SymSink *S, float x0, float y0, float x1, float y1) { if (!S->r) { sym_pt(S, x0, y0); sym_pt(S, x1, y1); return; } float ax, ay, bx, by; xf_apply(S->xf, x0, y0, &ax, &ay); xf_apply(S->xf, x1, y1, &bx, &by); ras_line(S->r, ax, ay, bx, by); }
static void sym_quad(SymSink *S, float x0, float y0, float qx, float qy, float x1, float y1) { if (!S->r) { sym_pt(S, x0, y0); sym_pt(S, qx, qy); sym_pt(S, x1, y1); return; } float ax, ay, cx, cy, bx, by; xf_apply(S->xf, x0, y0, &ax, &ay); xf_apply(S->xf, qx, qy, &cx, &cy); xf_apply(S->xf, x1, y1, &bx, &by); ras_quad(S->r, ax, ay, cx, cy, bx, by); }
static int sym_num(const char **p, float *v) { const char *q = *p; while (*q == ' ') q++; int neg = 0; if (*q == '-') { neg = 1; q++; } if (*q < '0' || *q > '9') return 0; int n = 0; while (*q >= '0' && *q <= '9') n = n * 10 + (*q++ - '0'); *v = neg ? -(float)n : (float)n; *p = q; return 1; }
static void sym_square(SymSink *S, float x, float y, float h) { sym_line(S, x - h, y + h, x + h, y + h); sym_line(S, x + h, y + h, x + h, y - h); sym_line(S, x + h, y - h, x - h, y - h); sym_line(S, x - h, y - h, x - h, y + h); }
static void sym_emit(const SymGlyph *g, SymSink *S, float wmul) {
  const char *p = g->path; float cx = 0, cy = 0, sx = 0, sy = 0; int open = 0;
  while (*p) { while (*p == ' ') p++; char c = *p; if (!c) break; p++; float x, y;
    if (c == 'M') { if (open && (cx != sx || cy != sy)) sym_line(S, cx, cy, sx, sy); if (!sym_num(&p, &x) || !sym_num(&p, &y)) break; sx = cx = x; sy = cy = y; open = 1; }
    else if (c == 'L') { if (!sym_num(&p, &x) || !sym_num(&p, &y)) break; sym_line(S, cx, cy, x, y); cx = x; cy = y; }
    else if (c == 'Q') { float qx, qy; if (!sym_num(&p, &qx) || !sym_num(&p, &qy) || !sym_num(&p, &x) || !sym_num(&p, &y)) break; sym_quad(S, cx, cy, qx, qy, x, y); cx = x; cy = y; }
    else if (c == 'Z') { if (open && (cx != sx || cy != sy)) sym_line(S, cx, cy, sx, sy); cx = sx; cy = sy; open = 0; }
    else if (c == 'C') { float ox, oy, rr; if (!sym_num(&p, &ox) || !sym_num(&p, &oy) || !sym_num(&p, &rr)) break; const float k = 1.0823922f;   /* 1 / cos(22.5 deg): control point radius of an 8-segment quadratic circle */
      static const float cs[9] = { 1, 0.70710678f, 0, -0.70710678f, -1, -0.70710678f, 0, 0.70710678f, 1 }, sn[9] = { 0, 0.70710678f, 1, 0.70710678f, 0, -0.70710678f, -1, -0.70710678f, 0 };
      static const float mc[8] = { 0.92387953f, 0.38268343f, -0.38268343f, -0.92387953f, -0.92387953f, -0.38268343f, 0.38268343f, 0.92387953f }, ms[8] = { 0.38268343f, 0.92387953f, 0.92387953f, 0.38268343f, -0.38268343f, -0.92387953f, -0.92387953f, -0.38268343f };
      for (int i = 0; i < 8; i++) sym_quad(S, ox + rr * cs[i], oy + rr * sn[i], ox + rr * k * mc[i], oy + rr * k * ms[i], ox + rr * cs[i + 1], oy + rr * sn[i + 1]); }
    else if (c == 'S') { float w; if (!sym_num(&p, &w)) break; float h = w * wmul * 0.5f, px = 0, py = 0; int have = 0;
      for (;;) { const char *q = p; if (!sym_num(&q, &x) || !sym_num(&q, &y)) break; p = q;
        if (have) { float dx = x - px, dy = y - py; float len = dx * dx + dy * dy; if (len > 0) { len = 1.0f / (float)__builtin_sqrtf(len); float nx = -dy * len * h, ny = dx * len * h;
            sym_line(S, px + nx, py + ny, x + nx, y + ny); sym_line(S, x + nx, y + ny, x - nx, y - ny); sym_line(S, x - nx, y - ny, px - nx, py - ny); sym_line(S, px - nx, py - ny, px + nx, py + ny); } sym_square(S, x, y, h); }
        else sym_square(S, x, y, h);
        px = x; py = y; have = 1; } }
    else break; }
  if (open && (cx != sx || cy != sy)) sym_line(S, cx, cy, sx, sy); }

/* ---- glyph cache ---- */
typedef struct { u32 cp; s16 face, px; s16 italic; s16 w, h, left, top, adv; u8 *cov; int used; } Glyph;
#define GLYPH_CACHE 4096
static Glyph g_glyphs[GLYPH_CACHE]; int g_glyph_log;   /* X64_FONTLOG=1: log code points no bundled face covers */
static u8 g_cov_lut[256]; static int g_cov_lut_ok;
static u32 glyph_hash(int face, int px, int italic, u32 cp) { u32 h = cp * 2654435761u; h ^= (u32)(face * 97 + px * 131 + italic * 7919); h ^= h >> 15; return h % GLYPH_CACHE; }

/* rasterizes (or fetches) one glyph; returns 0 when the face is missing or has no glyph for cp */
static Glyph *ttf_glyph(int face, int px, int italic, u32 cp) {
  TtfFace *f = ttf_face(face); if (!f || px < 1 || px > 512) return 0; Glyph *g = &g_glyphs[glyph_hash(face, px, italic, cp)];
  if (g->used && g->cp == cp && g->face == face && g->px == px && g->italic == italic) return g->adv < 0 ? 0 : g;
  int gid = ttf_gid(f, cp); if (!gid && cp != 0) { if (g_glyph_log && cp > 0x7f && !sym_find(cp)) e_log("[font] no glyph for U+%04x in face %d\n", cp, face); if (g->used) { emu_free(g->cov); g->cov = 0; } g->used = 1; g->cp = cp; g->face = (s16)face; g->px = (s16)px; g->italic = (s16)italic; g->adv = -1; g->w = g->h = 0; return 0; }
  float s = (float)px / (float)f->upem; float shear = (italic & 1) ? 0.21f : 0.0f; int xpct = italic >> 1; float xs = xpct ? (float)xpct / 100.0f : 1.0f; int adv = (int)(ttf_adv_units(f, gid) * s * xs + 0.5f);
  u32 off, ln; int w = 0, h = 0, left = 0, top = 0; u8 *cov = 0;
  if (ttf_glyph_loc(f, gid, &off, &ln) && ln >= 10) { float xmin = tts16(f, off + 2) * s * xs, ymin = tts16(f, off + 4) * s, xmax = tts16(f, off + 6) * s * xs, ymax = tts16(f, off + 8) * s;
    float x0 = xmin + (shear > 0 && ymin < 0 ? shear * ymin : 0), x1 = xmax + (shear > 0 && ymax > 0 ? shear * ymax : 0); int ix0 = (int)ffloor(x0) - 1, ix1 = (int)fceil(x1) + 1, iy0 = (int)ffloor(-ymax) - 1, iy1 = (int)fceil(-ymin) + 1;
    w = ix1 - ix0 + 1; h = iy1 - iy0; left = ix0; top = iy0; if (w > 0 && h > 0 && w < 2048 && h < 2048) { Ras r; r.w = w; r.h = h; r.a = emu_alloc(sizeof(float) * (size_t)(w * h)); cov = emu_alloc((size_t)(w * h));
      if (r.a && cov) { for (int i = 0; i < w * h; i++) r.a[i] = 0; Xf xf = { s * xs, 0, shear * s, -s, -(float)ix0, -(float)iy0 }; ttf_outline(f, gid, &xf, &r, 0);
        if (!g_cov_lut_ok) { for (int i = 0; i < 256; i++) { int v = i + i * (255 - i) * 2 / 255 / 3; g_cov_lut[i] = (u8)(v > 255 ? 255 : v); } g_cov_lut_ok = 1; }   /* mild stem darkening: unhinted small text would look too light */
        float acc = 0; for (int i = 0; i < w * h; i++) { acc += r.a[i]; float v = acc < 0 ? -acc : acc; if (v > 1) v = 1; cov[i] = g_cov_lut[(int)(v * 255.0f + 0.5f)]; } }
      else { emu_free(cov); cov = 0; w = h = 0; } emu_free(r.a); } else { w = h = 0; } }
  if (g->used) { emu_free(g->cov); g->cov = 0; } g->used = 1; g->cp = cp; g->face = (s16)face; g->px = (s16)px; g->italic = (s16)italic; g->adv = (s16)adv; g->w = (s16)w; g->h = (s16)h; g->left = (s16)left; g->top = (s16)top; g->cov = cov; return g; }

/* rasterizes (or fetches) a synthetic symbol glyph for a code point none of the bundled faces covers; px scale uses the 1000-unit symbol em */
static Glyph *sym_glyph(int face, int px, int italic, u32 cp) {
  const SymGlyph *sg = sym_find(cp); if (!sg || px < 1 || px > 512) return 0; int key = face + 8; Glyph *g = &g_glyphs[glyph_hash(key, px, italic, cp)];
  if (g->used && g->cp == cp && g->face == key && g->px == px && g->italic == italic) return g;
  float s = (float)px / 1000.0f; float shear = (italic & 1) ? 0.21f : 0.0f; int xpct = italic >> 1; float xs = xpct ? (float)xpct / 100.0f : 1.0f; float wmul = (face & 1) ? 1.35f : 1.0f;
  int adv = (int)(sg->adv * s * xs + 0.5f); SymSink M = { 0, 0, { 0, 0, 0, 0 }, 0 }; sym_emit(sg, &M, wmul); int w = 0, h = 0, left = 0, top = 0; u8 *cov = 0;
  if (M.n) { float xmin = M.bb[0] * s * xs, ymin = M.bb[1] * s, xmax = M.bb[2] * s * xs, ymax = M.bb[3] * s;
    float x0 = xmin + (shear > 0 && ymin < 0 ? shear * ymin : 0), x1 = xmax + (shear > 0 && ymax > 0 ? shear * ymax : 0); int ix0 = (int)ffloor(x0) - 1, ix1 = (int)fceil(x1) + 1, iy0 = (int)ffloor(-ymax) - 1, iy1 = (int)fceil(-ymin) + 1;
    w = ix1 - ix0 + 1; h = iy1 - iy0; left = ix0; top = iy0; if (w > 0 && h > 0 && w < 2048 && h < 2048) { Ras r; r.w = w; r.h = h; r.a = emu_alloc(sizeof(float) * (size_t)(w * h)); cov = emu_alloc((size_t)(w * h));
      if (r.a && cov) { for (int i = 0; i < w * h; i++) r.a[i] = 0; Xf xf = { s * xs, 0, shear * s, -s, -(float)ix0, -(float)iy0 }; SymSink S = { &xf, &r, { 0, 0, 0, 0 }, 0 }; sym_emit(sg, &S, wmul);
        float acc = 0; for (int i = 0; i < w * h; i++) { acc += r.a[i]; float v = acc < 0 ? -acc : acc; if (v > 1) v = 1; cov[i] = (u8)(v * 255.0f + 0.5f); } }
      else { emu_free(cov); cov = 0; w = h = 0; } emu_free(r.a); } else { w = h = 0; } }
  if (g->used) { emu_free(g->cov); g->cov = 0; } g->used = 1; g->cp = cp; g->face = (s16)key; g->px = (s16)px; g->italic = (s16)italic; g->adv = (s16)adv; g->w = (s16)w; g->h = (s16)h; g->left = (s16)left; g->top = (s16)top; g->cov = cov; return g; }

/* ---- font objects ---- */
static int fnt_is_mono_name(const char *n) { static const char *const mono[] = { "courier", "consolas", "mono", "lucida console", "fixedsys", "terminal", "cascadia", "fira code", "source code", "menlo", "monaco", "ubuntu mono", "dejavu sans mono", "liberation mono", "cousine", "ocr", "simplified arabic fixed", "ms gothic", "ms mincho", "nsimsun", "simsun", 0 }; char l[64]; int i = 0; for (; n[i] && i < 63; i++) l[i] = n[i] >= 'A' && n[i] <= 'Z' ? n[i] + 32 : n[i]; l[i] = 0;
  for (int k = 0; mono[k]; k++) { const char *m = mono[k]; int ml = (int)e_strlen(m); for (int j = 0; j + ml <= i; j++) if (!e_memcmp(l + j, m, (size_t)ml)) return 1; } return 0; }
static int fnt_is_narrow_name(const char *n) { static const char *const nar[] = { "ms shell dlg", "microsoft sans serif", "ms sans serif", "tahoma", "arial", "helvetica", 0 }; char l[64]; int i = 0; for (; n[i] && i < 63; i++) l[i] = n[i] >= 'A' && n[i] <= 'Z' ? n[i] + 32 : n[i]; l[i] = 0;
  for (int k = 0; nar[k]; k++) { const char *m = nar[k]; int ml = (int)e_strlen(m); if (i >= ml && !e_memcmp(l, m, (size_t)ml)) return 1; } return 0; }
/* lfWidth > 0: average character width in pixels → horizontal scale relative to the face's natural average */
static void fnt_set_width(GdiObj *f, int lfw) { if (lfw <= 0 || f->font_face < 0) return; TtfFace *tf = ttf_face(f->font_face); if (!tf) return; int nat = (f->font_px * tf->xavg + tf->upem / 2) / tf->upem; if (nat < 1) nat = 1; int pct = lfw * 100 / nat; if (pct < 40) pct = 40; if (pct > 250) pct = 250; if (pct == 100) pct = 0; f->font_flags = (f->font_flags & 0xff) | pct << 8; }
/* fills in a font object from LOGFONT fields: lfHeight < 0 = em height in pixels, > 0 = cell height, 0 = default (12 px em) */
static void fnt_setup(GdiObj *f, int lfh, int weight, int italic, int ul, int so, int pitch, const char *name) {
  int bold = weight >= 600; int mono = (pitch & 3) == 1 || (name && fnt_is_mono_name(name)); f->font_bold = bold; f->font_flags = (italic ? 1 : 0) | (ul ? 2 : 0) | (so ? 4 : 0);
  if (!mono && name && fnt_is_narrow_name(name)) f->font_flags |= 88 << 8;   /* MS Shell Dlg / Microsoft Sans Serif / Tahoma / Arial are ~12% narrower than the bundled sans: condense so dialogs laid out for them fit */ if (name) { int i = 0; for (; name[i] && i < 31; i++) f->font_name[i] = name[i]; f->font_name[i] = 0; } else f->font_name[0] = 0;
  int face = fnt_priv_find(name, bold, italic); if (face < 0) { face = (mono ? TF_MONO : TF_SANS) + (bold ? 1 : 0); if (!g_ttf[face].ok) g_ttf[face].tried = 0; } TtfFace *tf = ttf_face(face);   /* the program's own fonts first; bundled fonts mounted after the first attempt (stock objects) get another look */
  if (tf && face >= TF_N && tf->italic && italic) f->font_flags &= ~1;   /* a real italic face is not slanted again */
  if (!tf) { f->font_face = -1; int h = lfh, em = 0; if (h < 0) { em = -h; h = (em * 6 + 2) / 5; } if (!h) h = 16; f->font_h = h; f->font_em = em; f->font_px = em ? em : (h * 5 + 3) / 6; return; }
  int cellu = tf->win_asc + tf->win_desc; int px; if (lfh < 0) px = -lfh; else if (lfh > 0) px = (int)((long long)lfh * tf->upem + cellu / 2) / cellu; else px = 12; if (px < 1) px = 1; if (px > 400) px = 400;
  f->font_face = face; f->font_px = px; f->font_em = px; int asc = (px * tf->win_asc + tf->upem / 2) / tf->upem, desc = (px * tf->win_desc + tf->upem / 2) / tf->upem; f->font_h = asc + desc; }
static int fnt_style(GdiObj *f) { return f && f->kind == K_FONT ? (f->font_flags & 1) | ((f->font_flags >> 8) & 0xff) << 1 : 0; }   /* glyph style key: italic + horizontal scale percent (0 = 100) */
static int fnt_face_px(GdiObj *f, int *px) { if (!f || f->kind != K_FONT) { if (px) *px = 12; return -1; } if (px) *px = f->font_px ? f->font_px : (f->font_em ? f->font_em : f->font_h); return f->font_face; }
/* cell metrics: height = ascent + descent; avg = average character width (OS/2 xAvgCharWidth); maxw = widest advance */
static int fnt_metrics(GdiObj *f, int *asc, int *desc, int *avg, int *maxw) {
  int px; int face = fnt_face_px(f, &px); TtfFace *tf = face >= 0 ? ttf_face(face) : 0; int fh = f && f->kind == K_FONT ? f->font_h : 16; if (fh < 4) fh = 4;
  if (!tf) { int cw = font_cw(f, fh); if (asc) *asc = fh - 3; if (desc) *desc = 3; if (avg) *avg = cw; if (maxw) *maxw = cw; return fh; }
  int a = (px * tf->win_asc + tf->upem / 2) / tf->upem, d = (px * tf->win_desc + tf->upem / 2) / tf->upem; if (asc) *asc = a; if (desc) *desc = d; if (avg) *avg = (px * tf->xavg + tf->upem / 2) / tf->upem; if (maxw) *maxw = (px * tf->advmax + tf->upem / 2) / tf->upem; return a + d; }
/* average width the way dialog base units are derived on Windows: the 52-letter alphabet, (width / 26 + 1) / 2 */
static int fnt_dlg_avg(GdiObj *f) { int px; int face = fnt_face_px(f, &px); if (face < 0) return font_cw(f, f && f->kind == K_FONT ? f->font_h : 16); int tot = 0; for (int k = 0; k < 52; k++) { Glyph *g = ttf_glyph(face, px, fnt_style(f) & ~1, (u32)(k < 26 ? 'A' + k : 'a' + k - 26)); tot += g ? g->adv : px / 2; } return (tot / 26 + 1) / 2; }
/* glyph for a code point with family fallback: the matching face first, then the other family, else 0 (bitmap fallback) */
static Glyph *fnt_glyph(int face, int px, int italic, u32 cp) { Glyph *g = ttf_glyph(face, px, italic, cp); if (g) return g; if (face >= TF_N) { face = TF_SANS + (ttf_face(face) && g_ttf[face].bold ? 1 : 0); g = ttf_glyph(face, px, italic, cp); if (g) return g; } int alt = face ^ 2; g = ttf_glyph(alt, px, italic, cp); if (g) return g; if (face & 1) { g = ttf_glyph(face & ~1, px, italic, cp); if (g) return g; } return sym_glyph(face, px, italic, cp); }
static int fnt_cp_adv(GdiObj *f, u32 cp) { int px; int face = fnt_face_px(f, &px); if (cp == '\r' || cp == '\n') return 0; int fh = f && f->kind == K_FONT ? f->font_h : 16; if (face < 0) return font_cw(f, fh);
  if (cp == '\t') { Glyph *sp = fnt_glyph(face, px, 0, ' '); return sp ? sp->adv * 8 : px * 4; } Glyph *g = fnt_glyph(face, px, fnt_style(f), cp); if (g) return g->adv; return (px * 3 + 2) / 5; }
static int fnt_width(GdiObj *f, const char *s, int n) { int i = 0, w = 0; while (i < n) { u32 cp = utf8_next(s, n, &i); w += fnt_cp_adv(f, cp); } return w; }
/* bytes of s that fit into maxw pixels (whole characters); *wfit = their width */
static int fnt_fit(GdiObj *f, const char *s, int n, int maxw, int *wfit) { int i = 0, w = 0, last = 0; while (i < n) { int j = i; u32 cp = utf8_next(s, n, &j); int a = fnt_cp_adv(f, cp); if (w + a > maxw) break; w += a; i = j; last = i; } if (wfit) *wfit = w; return last; }
/* byte offset of the character boundary nearest to pixel x (hit testing in edit controls) */
static int fnt_hit(GdiObj *f, const char *s, int n, int x) { int i = 0, w = 0; if (x <= 0) return 0; while (i < n) { int j = i; u32 cp = utf8_next(s, n, &j); int a = fnt_cp_adv(f, cp); if (x < w + a / 2 + (a & 1)) return i; w += a; i = j; } return n; }

static void blend_px(u32 *p, u32 fg, int cov) { if (cov >= 255) { *p = fg; return; } if (cov <= 0) return; u32 d = *p; int ic = 255 - cov;
  u32 r = ((d & 0xff) * ic + (fg & 0xff) * cov + 127) / 255, g = (((d >> 8) & 0xff) * ic + ((fg >> 8) & 0xff) * cov + 127) / 255, b = (((d >> 16) & 0xff) * ic + ((fg >> 16) & 0xff) * cov + 127) / 255, a = (((d >> 24) & 0xff) * ic + ((fg >> 24) & 0xff) * cov + 127) / 255;
  *p = r | (g << 8) | (b << 16) | (a << 24); }
static void fnt_blit(Surf *s, const Glyph *g, int x, int y, u32 fg) { if (!g->cov) return; for (int gy = 0; gy < g->h; gy++) { int yy = y + gy; if (yy < s->y0 || yy >= s->h) continue; u32 *row = srow(s, yy); const u8 *cr = g->cov + gy * g->w;
    int xa = x < s->x0 ? s->x0 : x, xb = x + g->w > s->w ? s->w : x + g->w; for (int xx = xa; xx < xb; xx++) blend_px(&row[xx], fg, cr[xx - x]); } }
/* one 8x8 bitmap glyph scaled into a cw x gh box (fallback for code points no face covers) */
static void bitmap_glyph(Surf *s, u32 cp, int x, int y, int cw, int gh, int bold, u32 fg) { const unsigned char *g = glyph_of(cp);
  for (int py = 0; py < gh; py++) { int gy = py * 8 / gh; int yy = y + py; if (yy < s->y0 || yy >= s->h) continue; u32 *row = srow(s, yy);
    for (int px = 0; px < cw; px++) { int gx = px * 8 / cw; int on = (g[gy] >> gx) & 1; if (!on && bold && gx > 0) on = (g[gy] >> (gx - 1)) & 1; if (on) { int xx = x + px; if (xx >= s->x0 && xx < s->w) row[xx] = fg; } } } }
/* draws UTF-8 text with the DC's font; (x, y) = top-left of the text cell; honours OPAQUE background, underline, strikeout */
static void fnt_draw(Surf *s, GdiObj *dc, Win32 *w, int x, int y, const char *str, int len) {
  GdiObj *f = gdi_get(w, dc->font); int px; int face = fnt_face_px(f, &px); int fh = f && f->kind == K_FONT ? f->font_h : 16; if (fh < 4) fh = 4; int bold = f && f->kind == K_FONT ? f->font_bold : 0;
  u32 fg = cr2px(dc->text_color), bg = cr2px(dc->bk_color); int opaque = dc->bk_mode != 1;
  if (face < 0) { int cw = font_cw(f, fh); int gh = f && f->kind == K_FONT && f->font_em && f->font_em < fh ? f->font_em : fh; if (opaque) fill_rect_px(s, x, y, x + cw * utf8_cols(str, len), y + fh, bg);
    for (int i = 0, col = 0; i < len; col++) { u32 cp = utf8_next(str, len, &i); bitmap_glyph(s, cp, x + col * cw, y + (fh - gh), cw, gh, bold, fg); } return; }
  int asc; fnt_metrics(f, &asc, 0, 0, 0); int tw = fnt_width(f, str, len); if (opaque) fill_rect_px(s, x, y, x + tw, y + fh, bg); int base = y + asc; int italic = fnt_style(f); int cx = x;
  for (int i = 0; i < len;) { u32 cp = utf8_next(str, len, &i); if (cp == '\r' || cp == '\n') continue; int adv = fnt_cp_adv(f, cp); if (cp == '\t') { cx += adv; continue; }
    Glyph *g = fnt_glyph(face, px, italic, cp); if (g) fnt_blit(s, g, cx + g->left, base + g->top, fg); else { int bw = adv, bh = (px * 4 + 2) / 5; bitmap_glyph(s, cp, cx, base - bh, bw, bh, bold, fg); } cx += adv; }
  if (f->font_flags & 2) { int th = px / 14 + 1, uy = base + (px + 8) / 10; for (int k = 0; k < th; k++) hline(s, x, x + tw - 1, uy + k, fg); }
  if (f->font_flags & 4) { int th = px / 14 + 1, sy = base - (asc * 3) / 8; for (int k = 0; k < th; k++) hline(s, x, x + tw - 1, sy + k, fg); } }
/* the face name GetTextFace / GetObject report: the requested LOGFONT name, else the family's Windows name */
static const char *fnt_face_name(GdiObj *f) { if (f && f->kind == K_FONT && f->font_name[0]) return f->font_name; return f && f->kind == K_FONT && f->font_face >= TF_MONO ? "Courier New" : "Segoe UI"; }
