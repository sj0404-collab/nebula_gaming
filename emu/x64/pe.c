/* PE32+ loader and static inspector */
#include "emu.h"

static u16 le16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u64 le64(const u8 *p) { return (u64)le32(p) | (u64)le32(p + 4) << 32; }

typedef struct {
  const u8 *d; size_t len;
  u32 pe; u16 machine, nsec, optsz, chars, magic, subsystem, dllchars;
  u64 image_base; u32 entry, salign, falign, size_image, size_headers;
  u64 stack_reserve; u32 ndd; u32 dd[16][2];
  u32 sec_off;
} PeHdr;

static int pe_parse(PeHdr *h, const u8 *d, size_t len) {
  e_memset(h, 0, sizeof *h); h->d = d; h->len = len;
  if (len < 0x40 || d[0] != 'M' || d[1] != 'Z') return -1;
  h->pe = le32(d + 0x3c);
  if (h->pe + 24 > len || le32(d + h->pe) != 0x00004550) return -2;
  const u8 *c = d + h->pe + 4;
  h->machine = le16(c); h->nsec = le16(c + 2); h->optsz = le16(c + 16); h->chars = le16(c + 18);
  const u8 *o = d + h->pe + 24;
  if ((size_t)(h->pe + 24 + h->optsz) > len) return -3;
  h->magic = le16(o);
  if (h->magic == 0x20b) {
    h->entry = le32(o + 16); h->image_base = le64(o + 24); h->salign = le32(o + 32); h->falign = le32(o + 36);
    h->size_image = le32(o + 56); h->size_headers = le32(o + 60); h->subsystem = le16(o + 68); h->dllchars = le16(o + 70);
    h->stack_reserve = le64(o + 72); h->ndd = le32(o + 108);
    for (u32 i = 0; i < h->ndd && i < 16; i++) { h->dd[i][0] = le32(o + 112 + i * 8); h->dd[i][1] = le32(o + 116 + i * 8); }
  } else if (h->magic == 0x10b) {
    h->entry = le32(o + 16); h->image_base = le32(o + 28); h->salign = le32(o + 32); h->falign = le32(o + 36);
    h->size_image = le32(o + 56); h->size_headers = le32(o + 60); h->subsystem = le16(o + 68); h->dllchars = le16(o + 70);
    h->ndd = le32(o + 92);
    for (u32 i = 0; i < h->ndd && i < 16; i++) { h->dd[i][0] = le32(o + 96 + i * 8); h->dd[i][1] = le32(o + 100 + i * 8); }
  } else return -4;
  h->sec_off = h->pe + 24 + h->optsz;
  if ((size_t)h->sec_off + 40u * h->nsec > len) return -5;
  return 0;
}
/* RVA → file offset (for static inspection) */
static const u8 *pe_rva(PeHdr *h, u32 rva, u32 need) {
  if (rva < h->size_headers && (size_t)rva + need <= h->len) return h->d + rva;
  for (int i = 0; i < h->nsec; i++) {
    const u8 *s = h->d + h->sec_off + 40 * i; u32 vs = le32(s + 8), va = le32(s + 12), rs = le32(s + 16), rp = le32(s + 20);
    u32 span = vs > rs ? vs : rs;
    if (rva >= va && rva + need <= va + span) { if (rva - va + need > rs) return 0; if ((size_t)rp + (rva - va) + need > h->len) return 0; return h->d + rp + (rva - va); }
  }
  return 0;
}

/* ---------- import helper shared by loader and inspector ---------- */
typedef void (*ImportCb)(void *ctx, const char *dll, const char *name, u32 ordinal, u32 iat_rva);
static int pe_walk_imports(PeHdr *h, ImportCb cb, void *ctx) {
  u32 rva = h->dd[1][0]; if (!rva) return 0;
  int total = 0;
  for (u32 off = 0;; off += 20) {
    const u8 *e = pe_rva(h, rva + off, 20); if (!e) break;
    u32 ilt = le32(e), name = le32(e + 12), iat = le32(e + 16);
    if (!name && !iat) break;
    const u8 *dn = pe_rva(h, name, 1); const char *dll = dn ? (const char *)dn : "?";
    u32 thunk = ilt ? ilt : iat; int esz = h->magic == 0x20b ? 8 : 4;
    for (u32 k = 0;; k++) {
      const u8 *t = pe_rva(h, thunk + k * esz, esz); if (!t) break;
      u64 v = esz == 8 ? le64(t) : le32(t); if (!v) break;
      int by_ord = esz == 8 ? (v >> 63) : (v >> 31);
      if (by_ord) cb(ctx, dll, 0, (u32)(v & 0xffff), iat + k * esz);
      else { const u8 *hn = pe_rva(h, (u32)(v & 0x7fffffff) + 2, 1); cb(ctx, dll, hn ? (const char *)hn : "?", 0, iat + k * esz); }
      total++;
      if (k > 100000) break;
    }
  }
  return total;
}

/* ---------- loader ---------- */
typedef struct { Win32 *w; u8 *img; u64 base; int unknown; } LoadCtx;
static void load_import(void *ctx, const char *dll, const char *name, u32 ordinal, u32 iat_rva) {
  LoadCtx *lc = ctx; char tmp[32]; const char *n = name;
  if (!n) { e_snprintf(tmp, sizeof tmp, "#%u", ordinal); n = tmp; }
  u64 slot = w32_shim_slot(lc->w, dll, n);
  u8 *p = lc->img + iat_rva; p[0] = slot; p[1] = slot >> 8; p[2] = slot >> 16; p[3] = slot >> 24; p[4] = slot >> 32; p[5] = slot >> 40; p[6] = slot >> 48; p[7] = slot >> 56;
}

int pe_load(Win32 *w, const u8 *bytes, size_t len, const char *name) {
  PeHdr h; int rc = pe_parse(&h, bytes, len);
  if (rc) { w32_report(w, "not a PE file (code %d)", rc); return -1; }
  if (h.machine != 0x8664) { w32_report(w, "unsupported machine 0x%x (only x86-64 / PE32+ is emulated)", h.machine); return -2; }
  if (h.magic != 0x20b) { w32_report(w, "not PE32+"); return -2; }
  if (h.chars & 0x2000) { w32_report(w, "file is a DLL, not an executable"); return -3; }
  if (h.dd[14][1]) { w32_report(w, ".NET / CLR executable — needs the .NET runtime, not supported"); return -4; }
  u64 base = h.image_base; u64 size = (h.size_image + 0xffff) & ~0xffffull;
  /* collide with our fixed regions? relocate */
  int collide = (base < G_HEAP_BASE + 0x40000000ull && base + size > G_SHIM_BASE) ? 1 : 0;
  if (base == 0 || collide) {
    if (!h.dd[5][1] && !(h.dllchars & 0x40)) { w32_report(w, "image base 0x%llx collides and the file has no relocations", (unsigned long long)base); return -5; }
    base = 0x0000000140000000ull; if (base < G_HEAP_BASE + 0x40000000ull) base = 0x0000000180000000ull;
  }
  u8 *img = mem_map(w->mem, base, size, "image");
  if (!img) { w32_report(w, "cannot map image (%llu bytes)", (unsigned long long)size); return -6; }
  e_memcpy(img, bytes, h.size_headers < len ? h.size_headers : len);
  for (int i = 0; i < h.nsec; i++) {
    const u8 *s = bytes + h.sec_off + 40 * i; u32 vs = le32(s + 8), va = le32(s + 12), rs = le32(s + 16), rp = le32(s + 20);
    if (va >= size) continue;
    u32 n = rs; if (vs && vs < n) n = vs; if ((u64)va + n > size) n = (u32)(size - va); if ((size_t)rp + n > len) n = rp < len ? (u32)(len - rp) : 0;
    e_memcpy(img + va, bytes + rp, n);
  }
  /* relocations */
  s64 delta = (s64)(base - h.image_base);
  if (delta && h.dd[5][1]) {
    u32 r = h.dd[5][0], end = r + h.dd[5][1];
    while (r + 8 <= end && r + 8 <= size) {
      u32 page = le32(img + r), bsz = le32(img + r + 4); if (bsz < 8) break;
      for (u32 k = 8; k + 2 <= bsz; k += 2) {
        u16 e = le16(img + r + k); u32 type = e >> 12, off = page + (e & 0xfff);
        if (type == 10 && off + 8 <= size) { u64 v = le64(img + off) + (u64)delta; e_memcpy(img + off, &v, 8); }
        else if (type == 3 && off + 4 <= size) { u32 v = le32(img + off) + (u32)delta; e_memcpy(img + off, &v, 4); }
      }
      r += bsz;
    }
  }
  /* imports → shim slots */
  LoadCtx lc = { w, img, base, 0 };
  pe_walk_imports(&h, load_import, &lc);
  w->image_base = base; w->image_size = size; w->entry = base + h.entry; w->subsystem = h.subsystem;
  e_snprintf(w->exe_name, sizeof w->exe_name, "%s", name);
  /* TLS: write index 0, build a thread block */
  Cpu *c = w->cpu;
  if (h.dd[9][0] && h.dd[9][0] + 40 <= size) {
    const u8 *t = img + h.dd[9][0];
    u64 raw_start = le64(t), raw_end = le64(t + 8), idx_addr = le64(t + 16), cb_addr = le64(t + 24); u32 zero = le32(t + 32);
    u64 blk_sz = (raw_end > raw_start ? raw_end - raw_start : 0) + zero + 16;
    u64 blk = w32_heap_alloc(w, blk_sz, 1);
    if (raw_end > raw_start) { u8 *src = mem_ptr(w->mem, raw_start, raw_end - raw_start, 0); u8 *dst = mem_ptr(w->mem, blk, raw_end - raw_start, 1); if (src && dst) e_memcpy(dst, src, (size_t)(raw_end - raw_start)); }
    u64 arr = w32_heap_alloc(w, 64, 1); wr64(c, arr, blk);
    wr64(c, w->teb + 0x58, arr);
    if (idx_addr) wr32(c, idx_addr, 0);
    /* callbacks: stored for the start-up sequence */
    if (cb_addr) { for (int k = 0; k < 16; k++) { u64 fn = rd64(c, cb_addr + k * 8); if (!fn) break; w->tls_callbacks[w->n_tls_cb++] = fn; } }
  }
  wr64(c, w->peb + 0x10, base);
  return 0;
}

/* ---------- static inspection ---------- */
typedef struct { char *out; size_t sz, pos; int n, known, unknown; char dlls[64][48]; int ndll; int unknown_listed; } InsCtx;
static void ins_put(InsCtx *x, const char *fmt, ...) { if (x->pos >= x->sz) return; __builtin_va_list ap; __builtin_va_start(ap, fmt); int n = e_vsnprintf(x->out + x->pos, x->sz - x->pos, fmt, ap); __builtin_va_end(ap); x->pos += n; if (x->pos > x->sz - 1) x->pos = x->sz - 1; }
int w32_have_shim(const char *dll, const char *name);
static void ins_import(void *ctx, const char *dll, const char *name, u32 ordinal, u32 iat_rva) {
  (void)iat_rva; InsCtx *x = ctx; x->n++;
  char tmp[32]; if (!name) { e_snprintf(tmp, sizeof tmp, "#%u", ordinal); name = tmp; }
  int found = 0; for (int i = 0; i < x->ndll; i++) if (!e_stricmp(x->dlls[i], dll)) { found = 1; break; }
  if (!found && x->ndll < 64) { e_snprintf(x->dlls[x->ndll++], 48, "%s", dll); }
  if (w32_have_shim(dll, name)) x->known++;
  else { x->unknown++; if (x->unknown_listed < 60) { ins_put(x, "%s%s!%s", x->unknown_listed ? ", " : "", dll, name); x->unknown_listed++; } else if (x->unknown_listed == 60) { ins_put(x, ", ..."); x->unknown_listed++; } }
}
int pe_inspect(const u8 *bytes, size_t len, char *out, size_t out_sz) {
  PeHdr h; int rc = pe_parse(&h, bytes, len);
  InsCtx x; e_memset(&x, 0, sizeof x); x.out = out; x.sz = out_sz;
  if (rc) { ins_put(&x, "{\"ok\":false,\"error\":\"not a PE executable (code %d)\"}", rc); return -1; }
  const char *arch = h.machine == 0x8664 ? "x86-64" : h.machine == 0x14c ? "x86 (32-bit)" : h.machine == 0xaa64 ? "ARM64" : h.machine == 0x1c0 ? "ARM" : "unknown";
  const char *sub = h.subsystem == 2 ? "GUI" : h.subsystem == 3 ? "console" : "other";
  int is_dll = !!(h.chars & 0x2000), is_clr = !!h.dd[14][1];
  ins_put(&x, "{\"ok\":true,\"arch\":\"%s\",\"machine\":%u,\"pe32plus\":%s,\"subsystem\":\"%s\",\"dll\":%s,\"clr\":%s,\"imageBase\":\"0x%llx\",\"sizeOfImage\":%u,\"entry\":\"0x%x\",\"sections\":%u,\"relocs\":%s,\"tls\":%s,\"exceptions\":%s,\"resources\":%u,",
    arch, h.machine, h.magic == 0x20b ? "true" : "false", sub, is_dll ? "true" : "false", is_clr ? "true" : "false", (unsigned long long)h.image_base, h.size_image, h.entry, h.nsec,
    h.dd[5][1] ? "true" : "false", h.dd[9][1] ? "true" : "false", h.dd[3][1] ? "true" : "false", h.dd[2][1]);
  /* section names */
  ins_put(&x, "\"sectionNames\":[");
  for (int i = 0; i < h.nsec && i < 32; i++) { const u8 *s = bytes + h.sec_off + 40 * i; char nm[9]; e_memcpy(nm, s, 8); nm[8] = 0; for (int k = 0; k < 8; k++) if (nm[k] == '"' || nm[k] == '\\') nm[k] = '_'; ins_put(&x, "%s\"%s\"", i ? "," : "", nm); }
  ins_put(&x, "],\"unknownImports\":\"");
  size_t mark = x.pos;
  pe_walk_imports(&h, ins_import, &x);
  (void)mark;
  ins_put(&x, "\",\"imports\":%d,\"knownImports\":%d,\"unknownImportsCount\":%d,\"dlls\":[", x.n, x.known, x.unknown);
  for (int i = 0; i < x.ndll; i++) ins_put(&x, "%s\"%s\"", i ? "," : "", x.dlls[i]);
  /* verdict */
  const char *verdict; const char *why;
  if (h.machine != 0x8664) { verdict = "unsupported"; why = "only x86-64 (PE32+) code is emulated; 32-bit/ARM binaries are rejected"; }
  else if (is_dll) { verdict = "unsupported"; why = "DLL, not an executable"; }
  else if (is_clr) { verdict = "unsupported"; why = ".NET executable — needs the CLR"; }
  else {
    int heavy = 0; for (int i = 0; i < x.ndll; i++) { const char *d = x.dlls[i];
      if (!e_strncmp(d, "d3d", 3) || !e_strncmp(d, "D3D", 3) || !e_stricmp(d, "opengl32.dll") || !e_stricmp(d, "dxgi.dll") || !e_stricmp(d, "vulkan-1.dll") || !e_stricmp(d, "xinput1_4.dll") || !e_stricmp(d, "dsound.dll") || !e_stricmp(d, "xaudio2_9.dll") || !e_stricmp(d, "ws2_32.dll") || !e_stricmp(d, "mono-2.0-bdwgc.dll") || !e_stricmp(d, "UnityPlayer.dll") || !e_stricmp(d, "GameAssembly.dll")) heavy = 1; }
    if (heavy) { verdict = "unsupported"; why = "needs GPU/audio/network subsystems (Direct3D, OpenGL, Vulkan, XAudio, Winsock, Unity/Mono) that have no wasm64 implementation here"; }
    else if (x.unknown == 0) { verdict = "likely"; why = "all imports have shims"; }
    else if (x.unknown * 4 <= x.n) { verdict = "partial"; why = "most imports have shims; the program will stop at the first unimplemented call"; }
    else { verdict = "unlikely"; why = "most imports are not implemented"; }
  }
  ins_put(&x, "],\"verdict\":\"%s\",\"why\":\"%s\"}", verdict, why);
  return 0;
}
