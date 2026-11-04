/* ttf.inc — TrueType font engine: parser (head/maxp/loca/glyf/cmap/hhea/hmtx/OS/2), outline flattener, signed-area
 * coverage rasterizer (font-rs style accumulation buffer) and a glyph cache. Faces are regular files in the guest VFS
 * (C:\Windows\Fonts\*.ttf), loaded on first use. Everything that measures or draws text goes through fnt_*; when a face
 * is missing the old 8x8 bitmap glyphs with a fixed cell width stay as the fallback, so nothing depends on the files. */

typedef struct { const u8 *d; u32 len; int tried, ok; u16 upem, nglyphs, nhm; int long_loca; u32 loca, glyf, glyf_len, hmtx, cmap4, cmap12; int win_asc, win_desc, xavg, advmax; } TtfFace;
enum { TF_SANS = 0, TF_SANS_B = 1, TF_MONO = 2, TF_MONO_B = 3, TF_N = 4 };
static TtfFace g_ttf[TF_N];
static const char *const g_ttf_files[TF_N] = { "C:\\Windows\\Fonts\\ptsans.ttf", "C:\\Windows\\Fonts\\ptsansb.ttf", "C:\\Windows\\Fonts\\cousine.ttf", "C:\\Windows\\Fonts\\cousineb.ttf" };

static u32 tt16(const TtfFace *f, u32 o) { return o + 2 <= f->len ? ((u32)f->d[o] << 8) | f->d[o + 1] : 0; }
static int tts16(const TtfFace *f, u32 o) { return (s16)tt16(f, o); }
static u32 tt32(const TtfFace *f, u32 o) { return o + 4 <= f->len ? ((u32)f->d[o] << 24) | ((u32)f->d[o + 1] << 16) | ((u32)f->d[o + 2] << 8) | f->d[o + 3] : 0; }

static int ttf_parse(TtfFace *f) {
  u32 nt = tt16(f, 4); if (nt == 0 || nt > 64) return 0; u32 head = 0, maxp = 0, hhea = 0, os2 = 0, cmap = 0;
  for (u32 i = 0; i < nt; i++) { u32 r = 12 + 16 * i; u32 tag = tt32(f, r), off = tt32(f, r + 8), ln = tt32(f, r + 12); if (off >= f->len || ln > f->len - off) continue;
    switch (tag) { case 0x68656164: head = off; break; case 0x6d617870: maxp = off; break; case 0x68686561: hhea = off; break; case 0x4f532f32: os2 = off; break; case 0x636d6170: cmap = off; break;
      case 0x6c6f6361: f->loca = off; break; case 0x676c7966: f->glyf = off; f->glyf_len = ln; break; case 0x686d7478: f->hmtx = off; break; default: break; } }
  if (!head || !maxp || !hhea || !cmap || !f->loca || !f->glyf || !f->hmtx) return 0;
  f->upem = (u16)tt16(f, head + 18); if (f->upem < 16) return 0; f->long_loca = tts16(f, head + 50) != 0; f->nglyphs = (u16)tt16(f, maxp + 4); f->nhm = (u16)tt16(f, hhea + 34); if (!f->nhm) return 0;
  f->advmax = (int)tt16(f, hhea + 10);
  if (os2) { f->xavg = tts16(f, os2 + 2); f->win_asc = (int)tt16(f, os2 + 74); f->win_desc = (int)tt16(f, os2 + 76); }
  if (f->win_asc + f->win_desc < (int)f->upem / 2) { f->win_asc = tts16(f, hhea + 4); f->win_desc = -tts16(f, hhea + 6); }
  if (f->xavg <= 0) f->xavg = (int)f->upem / 2;
  u32 ns = tt16(f, cmap + 2); if (ns > 32) ns = 32;
  for (u32 i = 0; i < ns; i++) { u32 pid = tt16(f, cmap + 4 + 8 * i), so = tt32(f, cmap + 8 + 8 * i); u32 sub = cmap + so; if (sub >= f->len) continue; u32 fmt = tt16(f, sub);
    if (fmt == 4 && (pid == 3 || pid == 0) && !f->cmap4) f->cmap4 = sub; else if (fmt == 12 && (pid == 3 || pid == 0) && !f->cmap12) f->cmap12 = sub; }
  return f->cmap4 || f->cmap12; }

static TtfFace *ttf_face(int idx) {
  if (idx < 0 || idx >= TF_N) return 0; TtfFace *f = &g_ttf[idx]; if (f->ok) return f; if (f->tried) return 0; f->tried = 1;
  int fd = vfs_open(g_ttf_files[idx], 0x80000000u, 3, 0); if (fd < 0) return 0; s64 n = vfs_size(fd); if (n <= 64 || n > 64 * 1024 * 1024) { vfs_close(fd); return 0; }
  u8 *buf = emu_alloc((size_t)n); if (!buf) { vfs_close(fd); return 0; } s64 got = vfs_read(fd, buf, (u64)n); vfs_close(fd); if (got != n) { emu_free(buf); return 0; }
  f->d = buf; f->len = (u32)n; if (!ttf_parse(f)) { emu_free(buf); f->d = 0; f->len = 0; return 0; } f->ok = 1; return f; }

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

/* ---- glyph cache ---- */
typedef struct { u32 cp; s16 face, px; s16 italic; s16 w, h, left, top, adv; u8 *cov; int used; } Glyph;
#define GLYPH_CACHE 4096
static Glyph g_glyphs[GLYPH_CACHE];
static u8 g_cov_lut[256]; static int g_cov_lut_ok;
static u32 glyph_hash(int face, int px, int italic, u32 cp) { u32 h = cp * 2654435761u; h ^= (u32)(face * 97 + px * 131 + italic * 7919); h ^= h >> 15; return h % GLYPH_CACHE; }

/* rasterizes (or fetches) one glyph; returns 0 when the face is missing or has no glyph for cp */
static Glyph *ttf_glyph(int face, int px, int italic, u32 cp) {
  TtfFace *f = ttf_face(face); if (!f || px < 1 || px > 512) return 0; Glyph *g = &g_glyphs[glyph_hash(face, px, italic, cp)];
  if (g->used && g->cp == cp && g->face == face && g->px == px && g->italic == italic) return g->adv < 0 ? 0 : g;
  int gid = ttf_gid(f, cp); if (!gid && cp != 0) { if (g->used) { emu_free(g->cov); g->cov = 0; } g->used = 1; g->cp = cp; g->face = (s16)face; g->px = (s16)px; g->italic = (s16)italic; g->adv = -1; g->w = g->h = 0; return 0; }
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
  int face = (mono ? TF_MONO : TF_SANS) + (bold ? 1 : 0); if (!g_ttf[face].ok) g_ttf[face].tried = 0; TtfFace *tf = ttf_face(face);   /* fonts mounted after the first attempt (stock objects) get another look */
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
static Glyph *fnt_glyph(int face, int px, int italic, u32 cp) { Glyph *g = ttf_glyph(face, px, italic, cp); if (g) return g; int alt = face ^ 2; g = ttf_glyph(alt, px, italic, cp); if (g) return g; if (face & 1) { g = ttf_glyph(face & ~1, px, italic, cp); if (g) return g; } return 0; }
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
