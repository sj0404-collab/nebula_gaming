/* Shell icon services (shell32): the system image lists (SHIL_LARGE / SMALL / EXTRALARGE / SYSSMALL / JUMBO) seeded with the classic
   shell32 icon set (index i == shell32 resource id i+1, as in every Windows since 95), per-file icons (folders, drives, document types,
   the icon embedded in an .exe/.dll/.ico read straight from the PE resources on the virtual disk), SHGetFileInfo, ExtractIcon /
   ExtractIconEx / ExtractAssociatedIcon / PrivateExtractIcons / SHDefExtractIcon, SHGetStockIconInfo, SHGetImageList + IImageList,
   Shell_GetImageLists.  Lives in the gdi.c TU because it builds on icon.inc and imagelist.inc. */

/* ---- resources straight out of a PE file image (not loaded as a module): RT_GROUP_ICON (14) / RT_ICON (3) ---- */
static u32 pe_rd32(const u8 *d, size_t n, size_t o) { return o + 4 <= n ? (u32)d[o] | (u32)d[o + 1] << 8 | (u32)d[o + 2] << 16 | (u32)d[o + 3] << 24 : 0; }
static u32 pe_rd16(const u8 *d, size_t n, size_t o) { return o + 2 <= n ? (u32)d[o] | (u32)d[o + 1] << 8 : 0; }
typedef struct { const u8 *d; size_t n; u32 nsec; size_t sec; u32 rsrc_rva, rsrc_size; size_t rsrc_off; } PeFile;
static size_t pe_rva_off(const PeFile *p, u32 rva) { for (u32 i = 0; i < p->nsec; i++) { size_t s = p->sec + (size_t)i * 40; u32 va = pe_rd32(p->d, p->n, s + 12), vs = pe_rd32(p->d, p->n, s + 8), ro = pe_rd32(p->d, p->n, s + 20), rs = pe_rd32(p->d, p->n, s + 16); if (vs < rs) vs = rs; if (rva >= va && rva < va + vs) { size_t off = (size_t)ro + (rva - va); return off < p->n ? off : 0; } } return 0; }
static int pe_file_open(PeFile *p, const u8 *d, size_t n) {
  e_memset(p, 0, sizeof *p); if (!d || n < 0x100 || d[0] != 'M' || d[1] != 'Z') return 0; u32 pe = pe_rd32(d, n, 0x3c); if (pe + 0x100 > n || pe_rd32(d, n, pe) != 0x4550) return 0;
  u32 optsz = pe_rd16(d, n, pe + 20); u32 magic = pe_rd16(d, n, pe + 24); size_t opt = pe + 24; size_t dirs = opt + (magic == 0x20b ? 112 : 96); u32 ndir = pe_rd32(d, n, opt + (magic == 0x20b ? 108 : 92)); if (ndir < 3) return 0;
  p->d = d; p->n = n; p->nsec = pe_rd16(d, n, pe + 6); p->sec = opt + optsz; p->rsrc_rva = pe_rd32(d, n, dirs + 16); p->rsrc_size = pe_rd32(d, n, dirs + 20); if (!p->rsrc_rva) return 0; p->rsrc_off = pe_rva_off(p, p->rsrc_rva); return p->rsrc_off != 0; }
/* entry `want` (>= 0: id; < 0: the (-want-1)-th entry in directory order) of resource type `type`; returns the data pointer or 0 */
static const u8 *pe_file_rsrc(const PeFile *p, u32 type, s64 want, u32 *out_len, u32 *out_id) {
  size_t root = p->rsrc_off; u32 nn = pe_rd16(p->d, p->n, root + 12), ni = pe_rd16(p->d, p->n, root + 14); size_t tdir = 0;
  for (u32 i = 0; i < nn + ni; i++) { size_t e = root + 16 + (size_t)i * 8; u32 nm = pe_rd32(p->d, p->n, e), off = pe_rd32(p->d, p->n, e + 4); if (!(nm & 0x80000000u) && nm == type && (off & 0x80000000u)) { tdir = root + (off & 0x7fffffffu); break; } }
  if (!tdir) return 0; nn = pe_rd16(p->d, p->n, tdir + 12); ni = pe_rd16(p->d, p->n, tdir + 14); size_t ldir = 0; u32 id = 0;
  for (u32 i = 0; i < nn + ni; i++) { size_t e = tdir + 16 + (size_t)i * 8; u32 nm = pe_rd32(p->d, p->n, e), off = pe_rd32(p->d, p->n, e + 4); if (nm & 0x80000000u) continue; if ((want >= 0 && nm == (u32)want) || (want < 0 && (s64)i - (s64)nn == -want - 1)) { if (off & 0x80000000u) ldir = tdir + (off & 0x7fffffffu); id = nm; break; } }
  if (!ldir) return 0; u32 ln = pe_rd16(p->d, p->n, ldir + 12) + pe_rd16(p->d, p->n, ldir + 14); if (!ln) return 0; u32 doff = pe_rd32(p->d, p->n, ldir + 16 + 4); if (doff & 0x80000000u) return 0;
  size_t de = root + doff; u32 rva = pe_rd32(p->d, p->n, de), sz = pe_rd32(p->d, p->n, de + 4); size_t off = pe_rva_off(p, rva); if (!off || off + sz > p->n) return 0; if (out_len) *out_len = sz; if (out_id) *out_id = id; return p->d + off; }
static int pe_file_count(const PeFile *p, u32 type) { size_t root = p->rsrc_off; u32 nn = pe_rd16(p->d, p->n, root + 12), ni = pe_rd16(p->d, p->n, root + 14);
  for (u32 i = 0; i < nn + ni; i++) { size_t e = root + 16 + (size_t)i * 8; u32 nm = pe_rd32(p->d, p->n, e), off = pe_rd32(p->d, p->n, e + 4); if (!(nm & 0x80000000u) && nm == type && (off & 0x80000000u)) { size_t t = root + (off & 0x7fffffffu); return (int)(pe_rd16(p->d, p->n, t + 12) + pe_rd16(p->d, p->n, t + 14)); } } return 0; }
/* best entry of a GRPICONDIR (14-byte entries) or an ICONDIR (.ico, 16-byte entries) for the wanted size: exact size first, then the closest larger one, highest bpp */
static int icon_dir_best(const u8 *g, u32 glen, int ico_file, int want) { if (glen < 6) return -1; int n = pe_rd16(g, glen, 4), es = ico_file ? 16 : 14; if (n <= 0) return -1; if ((u32)(6 + n * es) > glen) n = (int)((glen - 6) / es); int best = -1, bs = 1 << 30;
  for (int i = 0; i < n; i++) { const u8 *e = g + 6 + i * es; int w = e[0] ? e[0] : 256, bpp = pe_rd16(e, es, 6); int score = (w == want ? 0 : (w > want ? (w - want) * 4 : (want - w) * 64)) * 64 + (32 - (bpp > 32 ? 32 : bpp)); if (score < bs) { bs = score; best = i; } } return best; }
static u64 icon_from_ico_bytes(Cpu *c, const u8 *d, size_t n, int cx, int cy) { if (n < 22 || pe_rd16(d, n, 2) != 1) return 0; int i = icon_dir_best(d, (u32)n, 1, cx > 0 ? cx : 32); if (i < 0) return 0; const u8 *e = d + 6 + i * 16; u32 sz = pe_rd32(e, 16, 8), off = pe_rd32(e, 16, 12); if (!sz || (size_t)off + sz > n) return 0; return icon_from_bits(c, d + off, sz, cx, cy, 0); }
/* icon `idx` (>= 0: group index, < 0: group resource id -idx) of a PE file image at the wanted size */
static u64 icon_from_pe_bytes(Cpu *c, const u8 *d, size_t n, int idx, int cx, int cy) { PeFile p; if (!pe_file_open(&p, d, n)) return 0; u32 glen = 0; const u8 *g = pe_file_rsrc(&p, 14, idx >= 0 ? -(s64)idx - 1 : (s64)(-idx), &glen, 0); if (!g) return 0;
  int bi = icon_dir_best(g, glen, 0, cx > 0 ? cx : 32); if (bi < 0) return 0; u32 id = pe_rd16(g, glen, 6 + bi * 14 + 12), ilen = 0; const u8 *ic = pe_file_rsrc(&p, 3, (s64)id, &ilen, 0); if (!ic) return 0; return icon_from_bits(c, ic, ilen, cx, cy, 0); }
static int pe_bytes_icon_count(const u8 *d, size_t n) { PeFile p; if (!pe_file_open(&p, d, n)) return 0; return pe_file_count(&p, 14); }
/* the file's own icon: .ico files, PE images (resource group `idx`); 0 when the file has none */
static u64 icon_from_file(Cpu *c, const char *path, int idx, int cx, int cy, int *count) { if (count) *count = 0; int node = vfs_find_node(path); if (node < 0) return 0; const u8 *d = vfs_node_data(node); u64 sz = 0; if (!d || vfs_stat(path, &sz) != 1 || sz < 22) return 0;
  if (sz > 4 && d[0] == 0 && d[1] == 0 && d[2] == 1 && d[3] == 0) { if (count) *count = pe_rd16(d, (size_t)sz, 4); return idx > 0 ? 0 : icon_from_ico_bytes(c, d, (size_t)sz, cx, cy); }
  if (count) *count = pe_bytes_icon_count(d, (size_t)sz); return icon_from_pe_bytes(c, d, (size_t)sz, idx, cx, cy); }

/* ---- the system image lists ---- */
#define SYSIML_BASE_ICONS 60     /* shell32 ids 1..60 at indices 0..59 */
enum { SI_ARCHIVE = SYSIML_BASE_ICONS, SI_PICTURE, SI_AUDIO, SI_VIDEO, SI_TEXT, SI_PDF, SI_SETUP, SI_FONT, SI_LNK, SI_DYNAMIC };
static u32 icon_shell_special(const char *ext);
static Win32 *g_sysiml_owner; static u64 g_sysiml[5]; static const int g_sysiml_px[5] = { 32, 16, 48, 16, 256 };
#define SYSIML_CACHE 512
static struct { u64 hash; int index; } g_sysiml_cache[SYSIML_CACHE]; static int g_sysiml_ncache;
static void sysiml_reset(Win32 *w) { if (g_sysiml_owner == w) return; g_sysiml_owner = w; e_memset(g_sysiml, 0, sizeof g_sysiml); g_sysiml_ncache = 0; }
static void sysiml_put(Cpu *c, ImgList *il, int i, u64 hicon) { iml_reserve(il, i + 1); if (il->n < i + 1) { e_memset(iml_img(il, il->n), 0, (size_t)il->cx * il->cy * 4 * (i + 1 - il->n)); il->n = i + 1; } Icon *ic = icon_of(hicon); u32 *d = iml_img(il, i); if (!ic) { iml_generic_icon(il, d); return; } if (ic->cx == il->cx && ic->cy == il->cy) e_memcpy(d, ic->px, (size_t)il->cx * il->cy * 4); else { u32 *t = icon_scaled(ic->px, ic->cx, ic->cy, il->cx, il->cy); e_memcpy(d, t, (size_t)il->cx * il->cy * 4); emu_free(t); } }
/* image drawn for the special document kinds (the extension decides) */
static u64 icon_special(Cpu *c, int kind, int cx, int cy) { static const char *const ext[] = { "zip", "png", "mp3", "mp4", "txt", "pdf", "msi", "ttf", "lnk" }; return icon_shell(c, "shell32", icon_shell_special(ext[kind - SYSIML_BASE_ICONS]), cx, cy); }
static u64 sysiml_get(Cpu *c, int which) { Win32 *w = W; sysiml_reset(w); if (which < 0 || which > 4) which = 0; if (g_sysiml[which]) return g_sysiml[which];
  int px = g_sysiml_px[which]; ImgList *il = iml_new(w, px, px, 0x20 /* ILC_COLOR32 */ | 1, SI_DYNAMIC); if (!il) return 0; u64 h = iml_handle(il); g_sysiml[which] = h;
  for (int i = 0; i < SYSIML_BASE_ICONS; i++) { u64 ic = icon_shell(c, "shell32", (u32)i + 1, px, px); sysiml_put(c, il, i, ic); Icon *o = icon_of(ic); if (o) icon_free(o); }
  for (int k = SYSIML_BASE_ICONS; k < SI_DYNAMIC; k++) { u64 ic = icon_special(c, k, px, px); sysiml_put(c, il, k, ic); Icon *o = icon_of(ic); if (o) icon_free(o); }
  /* a dynamic icon added to one list is added to all of them at the same index: keep the other lists' counts in step */
  for (int j = 0; j < 5; j++) if (j != which && g_sysiml[j]) { ImgList *o = iml_of(g_sysiml[j]); if (o && o->n > il->n) { for (int e = il->n; e < o->n; e++) { u32 *op = iml_img(o, e); u32 *t = icon_scaled(op, o->cx, o->cy, il->cx, il->cy); iml_reserve(il, e + 1); e_memcpy(iml_img(il, e), t, (size_t)il->cx * il->cy * 4); emu_free(t); il->n = e + 1; } } }
  return h; }
static u64 str_hash_ci(const char *s) { u64 h = 1469598103934665603ull; for (; *s; s++) { u8 ch = (u8)*s; if (ch >= 'A' && ch <= 'Z') ch += 32; if (ch == '/') ch = '\\'; h = (h ^ ch) * 1099511628211ull; } return h; }
/* append the file's own icon to every system image list (cached per path); -1 when the file has none */
static int sysiml_add_file_icon(Cpu *c, const char *path) { u64 hh = str_hash_ci(path); for (int i = 0; i < g_sysiml_ncache; i++) if (g_sysiml_cache[i].hash == hh) return g_sysiml_cache[i].index;
  int index = -1; sysiml_get(c, 0); sysiml_get(c, 1); int n = 0; u64 probe = icon_from_file(c, path, 0, 32, 32, &n); if (probe) { Icon *o = icon_of(probe); if (o) icon_free(o);
    ImgList *l0 = iml_of(g_sysiml[0]); index = l0 ? l0->n : -1;
    if (index >= 0) for (int j = 0; j < 5; j++) if (g_sysiml[j]) { ImgList *il = iml_of(g_sysiml[j]); if (!il) continue; u64 ic = icon_from_file(c, path, 0, il->cx, il->cy, 0); sysiml_put(c, il, index, ic); Icon *o2 = icon_of(ic); if (o2) icon_free(o2); } }
  if (g_sysiml_ncache < SYSIML_CACHE) { g_sysiml_cache[g_sysiml_ncache].hash = hh; g_sysiml_cache[g_sysiml_ncache].index = index; g_sysiml_ncache++; } return index; }
static const char *path_ext(const char *path) { const char *e = 0; for (const char *p = path; *p; p++) { if (*p == '.') e = p + 1; else if (*p == '\\' || *p == '/') e = 0; } return e ? e : ""; }
static int ext_in(const char *ext, const char *list) { size_t l = e_strlen(ext); if (!l) return 0; for (const char *p = list; *p; ) { const char *q = p; while (*q && *q != ' ') q++; if ((size_t)(q - p) == l && !e_strnicmp(p, ext, l)) return 1; p = *q ? q + 1 : q; } return 0; }
/* shell32 resource id of the special document kinds (ids above the classic range are our own; icon_shell draws them) */
static u32 icon_shell_special(const char *ext) {
  if (ext_in(ext, "zip rar 7z gz tgz bz2 xz cab arj lzh z tar iso")) return 1001; if (ext_in(ext, "png jpg jpeg gif bmp tif tiff webp ico svg psd heic")) return 1002; if (ext_in(ext, "mp3 wav ogg flac wma m4a aac opus mid")) return 1003;
  if (ext_in(ext, "mp4 avi mkv mov wmv webm mpg mpeg m4v flv")) return 1004; if (ext_in(ext, "txt log ini inf cfg conf md nfo csv json xml yml yaml c h cpp cs js ts py bat cmd ps1 reg html htm css")) return 1005; if (ext_in(ext, "pdf doc docx rtf odt xls xlsx ppt pptx")) return 1006;
  if (ext_in(ext, "msi msp")) return 1007; if (ext_in(ext, "ttf otf fon ttc")) return 1008; if (ext_in(ext, "lnk url")) return 1009; return 1; }
/* system image list index for a path (or for the attributes alone when the path need not exist) */
static int shell_icon_index(Cpu *c, const char *path, u32 attrs, int use_attrs, int open_icon) {
  int is_dir = (attrs & 0x10) != 0; size_t pl = e_strlen(path); int is_root = pl >= 2 && path[1] == ':' && (pl == 2 || (pl == 3 && (path[2] == '\\' || path[2] == '/')));
  if (!use_attrs) { int n = vfs_find_node(path); if (n >= 0) { VNodeInfo vi; if (vfs_node_info(n, &vi)) is_dir = vi.is_dir; } else if (!is_root) is_dir = (attrs & 0x10) != 0; else is_dir = 1; }
  if (is_root) return 8; if (is_dir) return open_icon ? 4 : 3; const char *ext = path_ext(path);
  if (ext_in(ext, "exe scr cpl dll ocx ico icl")) { if (!use_attrs) { int i = sysiml_add_file_icon(c, path); if (i >= 0) return i; } return ext_in(ext, "exe scr") ? 2 : (ext_in(ext, "ico") ? SI_PICTURE : 0); }
  if (ext_in(ext, "com bat cmd pif")) return 2; u32 sp = icon_shell_special(ext); if (sp >= 1001) return SYSIML_BASE_ICONS + (int)(sp - 1001); return ext[0] ? 1 : 0; }
static const char *shell_type_name(const char *path, int is_dir, int is_root, char *buf, size_t cap) {
  if (is_root) return "Local Disk"; if (is_dir) return "File folder"; const char *ext = path_ext(path); if (!ext[0]) return "File";
  if (ext_in(ext, "exe")) return "Application"; if (ext_in(ext, "dll")) return "Application extension"; if (ext_in(ext, "txt")) return "Text Document"; if (ext_in(ext, "lnk")) return "Shortcut"; if (ext_in(ext, "zip")) return "Compressed (zipped) Folder"; if (ext_in(ext, "ini")) return "Configuration settings"; if (ext_in(ext, "bat cmd")) return "Windows Batch File"; if (ext_in(ext, "msi")) return "Windows Installer Package"; if (ext_in(ext, "ttf")) return "TrueType font file"; if (ext_in(ext, "pdf")) return "PDF File"; if (ext_in(ext, "html htm")) return "HTML Document";
  size_t o = 0; for (; ext[o] && o + 6 < cap; o++) buf[o] = (ext[o] >= 'a' && ext[o] <= 'z') ? (char)(ext[o] - 32) : ext[o]; e_memcpy(buf + o, " File", 6); return buf; }
/* ---- SHGetFileInfoW ---- */
static void s_SHGetFileInfoW(Cpu *c) { u64 fi = ARG(2); u32 attrs = (u32)ARG(1), flags = (u32)ARG(4); char path[1040]; path[0] = 0; int use_attrs = (flags & 0x10) != 0;
  if (ARG(0) >= 0x10000 && !(flags & 8)) wstr_to_utf8(c, ARG(0), path, sizeof path, -1); else if (flags & 8) e_snprintf(path, sizeof path, "%s", pidl_path(c, ARG(0)));
  u64 P = PSZ_OF(c), attr_off = P == 4 ? 8 : 12, disp_off = P == 4 ? 12 : 16, type_off = disp_off + 520; size_t pl = e_strlen(path); int is_root = pl >= 2 && path[1] == ':' && (pl == 2 || (pl == 3 && (path[2] == '\\' || path[2] == '/')));
  int is_dir = (attrs & 0x10) != 0; if (!use_attrs) { int n = vfs_find_node(path); if (n >= 0) { VNodeInfo vi; if (vfs_node_info(n, &vi)) is_dir = vi.is_dir; } else if (is_root) is_dir = 1; else if (!path[0]) is_dir = 1; else if (!(flags & 0x4000) && !(flags & 0x100) && !(flags & 0x400) && !(flags & 0x200) && !(flags & 0x800)) { RET(0); return; } }
  if (!fi) { if (flags & 0x2000) { const char *ext = path_ext(path); RET(ext_in(ext, "exe scr") ? 0x00004550 | (2 << 16) : 0); return; } RET(0); return; }
  int small = (flags & 1) != 0; int which = small ? 1 : 0; if ((flags & 4) && !small) which = 0; int open_icon = (flags & 2) != 0; int index = shell_icon_index(c, path, is_dir ? 0x10 : (attrs ? attrs : 0x80), use_attrs, open_icon);
  wrp(c, fi, 0); wr32(c, fi + P, (u32)index);
  u32 sfgao = is_dir ? 0xF0400177u : 0x40400177u; if (is_root) sfgao |= 0x20000000u | 0x80000000u; if ((flags & 0x20000) && ARG(3)) { } wr32(c, fi + attr_off, (flags & 0x800) ? sfgao : 0);
  if (flags & 0x200) { const char *bn = path; for (const char *p = path; *p; p++) if (*p == '\\' || *p == '/') bn = p + 1; if (is_root) { char nm[32]; e_snprintf(nm, sizeof nm, "Local Disk (%c:)", path[0]); put_w8(c, fi + disp_off, nm, 260); } else { char nm[520]; e_snprintf(nm, sizeof nm, "%s", bn); if (!is_dir && (flags & 0x200) && !(flags & 0x10000)) { size_t l = e_strlen(nm); const char *ext = path_ext(nm); if (ext[0] && ext_in(ext, "exe lnk dll txt ini bat cmd zip png jpg jpeg gif bmp mp3 mp4 pdf doc docx html htm url")) nm[l - e_strlen(ext) - 1] = 0; } put_w8(c, fi + disp_off, nm, 260); } }
  if (flags & 0x400) { char tb[64]; put_w8(c, fi + type_off, shell_type_name(path, is_dir, is_root, tb, sizeof tb), 80); }
  if (flags & 0x1000) { put_w8(c, fi + disp_off, "C:\\Windows\\System32\\shell32.dll", 260); wr32(c, fi + P, index < SYSIML_BASE_ICONS ? (u32)index : 0); }
  if (flags & 0x100) { u64 himl = sysiml_get(c, which); ImgList *il = iml_of(himl); if (il && index >= 0 && index < il->n) { u32 *px = emu_alloc((size_t)il->cx * il->cy * 4); e_memcpy(px, iml_img(il, index), (size_t)il->cx * il->cy * 4); if (flags & 0x8000) { u64 ov = icon_shell(c, "shell32", 30, il->cx, il->cy); Icon *oi = icon_of(ov); if (oi) { for (int k = 0; k < il->cx * il->cy; k++) if (oi->px[k] >> 24) px[k] = oi->px[k]; icon_free(oi); } } if (flags & 0x10000) for (int k = 0; k < il->cx * il->cy; k++) if (px[k] >> 24) px[k] = (px[k] & 0xff000000u) | ((px[k] >> 1) & 0x7f7f7f) | 0x000040; wrp(c, fi, icon_new(W, il->cx, il->cy, px, 0, 0, 0)); } }
  if (flags & 0x4000) { RET(sysiml_get(c, which)); return; } RET(1); }
static void s_SHGetFileInfoA(Cpu *c) { /* narrow in, narrow out: run the W form on a temporary buffer */ char path[1040]; path[0] = 0; u32 flags = (u32)ARG(4); if (ARG(0) >= 0x10000 && !(flags & 8)) { const char *s = rd_str(c, ARG(0), 1024); if (s) e_snprintf(path, sizeof path, "%s", s); }
  u64 tmp = w32_heap_alloc(W, 1200 + 0x300, 1); if (!tmp) { RET(0); return; } u64 wp = tmp + 0x300; put_w8(c, wp, path, 600); u64 fi = ARG(2); u64 P = PSZ_OF(c);
  call_shim(c, s_SHGetFileInfoW, (flags & 8) ? ARG(0) : wp, ARG(1), fi ? tmp : 0, 0x300); u64 r = c->r[RAX];
  if (fi) { u64 attr_off = P == 4 ? 8 : 12, disp_off = P == 4 ? 12 : 16, type_off = disp_off + 520; wrp(c, fi, rdp(c, tmp)); wr32(c, fi + P, rd32(c, tmp + P)); wr32(c, fi + attr_off, rd32(c, tmp + attr_off)); char s[600]; wstr_to_utf8(c, tmp + disp_off, s, sizeof s, 260); u8 *d = mem_ptr(c->mem, fi + disp_off, 260, 1); if (d) { e_memset(d, 0, 260); e_snprintf((char *)d, 260, "%s", s); } wstr_to_utf8(c, tmp + type_off, s, sizeof s, 80); d = mem_ptr(c->mem, fi + type_off, 80, 1); if (d) { e_memset(d, 0, 80); e_snprintf((char *)d, 80, "%s", s); } }
  RET(r); }
/* ---- SHGetStockIconInfo: SIID n == shell32 icon n+1 ---- */
static void s_SHGetStockIconInfo(Cpu *c) { u32 siid = (u32)ARG(0), flags = (u32)ARG(1); u64 p = ARG(2); if (!p || rd32(c, p) < 20) { RET(0x80070057u); return; } u64 P = PSZ_OF(c); u32 id = siid + 1; if (siid > 180) { RET(0x80070057u); return; }
  wrp(c, p + P, 0); wr32(c, p + 2 * P, siid < SYSIML_BASE_ICONS ? siid : 0); wr32(c, p + 2 * P + 4, (u32)-(s32)id); put_w8(c, p + 2 * P + 8, "C:\\Windows\\System32\\shell32.dll", 260);
  if (flags & 0x100) { int px = (flags & 1) ? 16 : ((flags & 4) ? 16 : 32); wrp(c, p + P, icon_shell(c, "shell32", id, px, px)); }
  if (flags & 0x4000) { sysiml_get(c, (flags & 1) ? 1 : 0); } RET(0); }
/* ---- ExtractIcon family ---- */
static const char *icon_source_dll(const char *path) { const char *b = path; for (const char *p = path; *p; p++) if (*p == '\\' || *p == '/') b = p + 1; static char k[40]; size_t o = 0; for (; b[o] && o + 1 < sizeof k; o++) k[o] = (b[o] >= 'A' && b[o] <= 'Z') ? (char)(b[o] + 32) : b[o]; k[o] = 0; if (o > 4 && !e_stricmp(k + o - 4, ".dll")) k[o - 4] = 0;
  return (!e_strcmp(k, "shell32") || !e_strcmp(k, "imageres") || !e_strcmp(k, "user32") || !e_strcmp(k, "comctl32") || !e_strcmp(k, "ieframe") || !e_strcmp(k, "ddores") || !e_strcmp(k, "setupapi") || !e_strcmp(k, "wmploc") || !e_strcmp(k, "netshell") || !e_strcmp(k, "explorer")) ? k : 0; }
/* one icon of a file by index (negative = resource id); loaded modules are served from their mapped resources, system DLLs from the built-in set, anything else from the file on the virtual disk */
static u64 extract_icon(Cpu *c, const char *path, int idx, int cx, int cy, int *count) { if (count) *count = 0; const char *sd = icon_source_dll(path);
  if (sd) { if (count) *count = !e_strcmp(sd, "imageres") ? 340 : 330; u32 id = idx < 0 ? (u32)-idx : (u32)idx + 1; return icon_shell(c, sd, id, cx, cy); }
  u64 hm = w32_module_handle_of(W, path); if (hm) { int cnt = 0; s64 id = w32_rsrc_nth(c, hm, 14, idx < 0 ? 0 : idx, &cnt); if (count) *count = cnt; u64 h = idx < 0 ? icon_load_res(c, hm, (u64)-idx, 0, cx, cy, 1) : (id >= 0 ? icon_load_res(c, hm, (u64)id, 0, cx, cy, 1) : 0); if (h) return h; }
  return icon_from_file(c, path, idx, cx, cy, count); }
static void s_ExtractIconW(Cpu *c) { char path[1040]; text_in(c, ARG(1), 1, path, sizeof path); int idx = (int)(s32)ARG(2), n = 0; if (idx == -1) { extract_icon(c, path, 0, 32, 32, &n); RET((u32)n); return; } u64 h = extract_icon(c, path, idx, 32, 32, 0); RET(h ? h : 0); }
static void s_ExtractIconA(Cpu *c) { const char *s = rd_str(c, ARG(1), 1024); char path[1040]; e_snprintf(path, sizeof path, "%s", s ? s : ""); int idx = (int)(s32)ARG(2), n = 0; if (idx == -1) { extract_icon(c, path, 0, 32, 32, &n); RET((u32)n); return; } u64 h = extract_icon(c, path, idx, 32, 32, 0); RET(h ? h : 0); }
static void extract_icon_ex(Cpu *c, const char *path, int idx, u64 large, u64 small, u32 n) { u64 P = PSZ_OF(c); if (idx == -1) { int cnt = 0; extract_icon(c, path, 0, 32, 32, &cnt); RET((u32)cnt); return; } u32 got = 0;
  for (u32 i = 0; i < n && i < 256; i++) { int k = idx < 0 ? idx : idx + (int)i; u64 hl = large ? extract_icon(c, path, k, 32, 32, 0) : 0, hs = small ? extract_icon(c, path, k, 16, 16, 0) : 0; if (large) wrp(c, large + (u64)i * P, hl); if (small) wrp(c, small + (u64)i * P, hs); if (hl || hs) got++; else break; if (idx < 0) break; }
  RET(got); }
static void s_ExtractIconExW(Cpu *c) { char path[1040]; text_in(c, ARG(0), 1, path, sizeof path); extract_icon_ex(c, path, (int)(s32)ARG(1), ARG(2), ARG(3), (u32)ARG(4)); }
static void s_ExtractIconExA(Cpu *c) { const char *s = rd_str(c, ARG(0), 1024); char path[1040]; e_snprintf(path, sizeof path, "%s", s ? s : ""); extract_icon_ex(c, path, (int)(s32)ARG(1), ARG(2), ARG(3), (u32)ARG(4)); }
static void s_ExtractAssociatedIconW(Cpu *c) { u64 pp = ARG(1), pidx = ARG(2); char path[1040]; text_in(c, pp, 1, path, sizeof path); int idx = pidx ? (int)(s16)rd16(c, pidx) : 0; u64 h = extract_icon(c, path, idx, 32, 32, 0);
  if (!h) { int index = shell_icon_index(c, path, 0x80, 0, 0); u64 himl = sysiml_get(c, 0); ImgList *il = iml_of(himl); if (il && index < il->n) { u32 *px = emu_alloc((size_t)il->cx * il->cy * 4); e_memcpy(px, iml_img(il, index), (size_t)il->cx * il->cy * 4); h = icon_new(W, il->cx, il->cy, px, 0, 0, 0); } if (pp) put_w8(c, pp, "C:\\Windows\\System32\\shell32.dll", 260); if (pidx) wr16(c, pidx, (u16)(index < SYSIML_BASE_ICONS ? index : 0)); }
  RET(h); }
static void s_ExtractAssociatedIconA(Cpu *c) { u64 pp = ARG(1), pidx = ARG(2); const char *s = rd_str(c, pp, 1024); char path[1040]; e_snprintf(path, sizeof path, "%s", s ? s : ""); int idx = pidx ? (int)(s16)rd16(c, pidx) : 0; u64 h = extract_icon(c, path, idx, 32, 32, 0);
  if (!h) { int index = shell_icon_index(c, path, 0x80, 0, 0); u64 himl = sysiml_get(c, 0); ImgList *il = iml_of(himl); if (il && index < il->n) { u32 *px = emu_alloc((size_t)il->cx * il->cy * 4); e_memcpy(px, iml_img(il, index), (size_t)il->cx * il->cy * 4); h = icon_new(W, il->cx, il->cy, px, 0, 0, 0); } if (pidx) wr16(c, pidx, (u16)(index < SYSIML_BASE_ICONS ? index : 0)); }
  RET(h); }
/* PrivateExtractIconsW(path, idx, cx, cy, HICON *out, UINT *ids, n, flags) — cx/cy may pack two sizes (LOWORD/HIWORD) */
static void private_extract_icons(Cpu *c, const char *path, int idx, u32 cx, u32 cy, u64 out, u64 ids, u32 n) { u64 P = PSZ_OF(c);
  if (!out) { int cnt = 0; extract_icon(c, path, 0, 32, 32, &cnt); RET((u32)cnt); return; } u32 got = 0; int two = (cx >> 16) != 0;
  for (u32 i = 0; i < n && i < 256; i++) { int sz_x = (int)((two && (i & 1)) ? cx >> 16 : cx & 0xffff), sz_y = (int)((two && (i & 1)) ? cy >> 16 : cy & 0xffff); int k = idx < 0 ? idx : idx + (int)(two ? i / 2 : i); u64 h = extract_icon(c, path, k, sz_x ? sz_x : 32, sz_y ? sz_y : 32, 0); wrp(c, out + (u64)i * P, h); if (ids) wr32(c, ids + (u64)i * 4, (u32)k); if (!h) break; got++; }
  RET(got); }
static void s_PrivateExtractIconsW(Cpu *c) { char path[1040]; text_in(c, ARG(0), 1, path, sizeof path); private_extract_icons(c, path, (int)(s32)ARG(1), (u32)ARG(2), (u32)ARG(3), ARG(4), ARG(5), (u32)ARG(6)); }
static void s_PrivateExtractIconsA(Cpu *c) { const char *s = rd_str(c, ARG(0), 1024); char path[1040]; e_snprintf(path, sizeof path, "%s", s ? s : ""); private_extract_icons(c, path, (int)(s32)ARG(1), (u32)ARG(2), (u32)ARG(3), ARG(4), ARG(5), (u32)ARG(6)); }
/* SHDefExtractIconW(path, idx, flags, HICON *large, HICON *small, size(LOWORD large, HIWORD small)) */
static void shdef_extract(Cpu *c, const char *path, int idx, u64 lg, u64 sm, u32 sz) { int cl = (int)(sz & 0xffff), cs = (int)(sz >> 16); if (!cl) cl = 32; if (!cs) cs = 16;
  u64 hl = lg ? extract_icon(c, path, idx, cl, cl, 0) : 0, hs = sm ? extract_icon(c, path, idx, cs, cs, 0) : 0; if (lg) wrp(c, lg, hl); if (sm) wrp(c, sm, hs); RET((hl || hs) ? 0 : 1 /* S_FALSE: no icon */); }
static void s_SHDefExtractIconW(Cpu *c) { char path[1040]; text_in(c, ARG(0), 1, path, sizeof path); shdef_extract(c, path, (int)(s32)ARG(1), ARG(3), ARG(4), (u32)ARG(5)); }
static void s_SHDefExtractIconA(Cpu *c) { const char *s = rd_str(c, ARG(0), 1024); char path[1040]; e_snprintf(path, sizeof path, "%s", s ? s : ""); shdef_extract(c, path, (int)(s32)ARG(1), ARG(3), ARG(4), (u32)ARG(5)); }
/* ---- IImageList over the system image lists (SHGetImageList); the COM object is [vtable][himl] in guest memory ---- */
static const char *const vt_IImageList[] = { "QueryInterface", "AddRef", "Release", "Add", "ReplaceIcon", "SetOverlayImage", "Replace", "AddMasked", "Draw", "Remove", "GetIcon", "GetImageInfo", "Copy", "Merge", "Clone", "GetImageRect", "GetIconSize", "SetIconSize", "GetImageCount", "SetImageCount", "SetBkColor", "GetBkColor", "BeginDrag", "EndDrag", "DragEnter", "DragLeave", "DragMove", "SetDragCursorImage", "DragShowNolock", "GetDragImage", "GetItemFlags", "GetOverlayImage",
  /* IImageList2 */ "Resize", "GetOriginalSize", "SetOriginalSize", "SetCallback", "GetCallback", "ForceImagePresent", "DiscardImages", "PreloadImages", "GetStatistics", "Initialize", "Replace2", "ReplaceFromImageList" };
#define SHIML_MAX 64
static struct { u64 addr, himl; Win32 *w; } g_shiml[SHIML_MAX]; static int g_nshiml; static u64 g_shiml_vt; static Win32 *g_shiml_vt_owner;
static u64 shiml_vtable(Cpu *c) { Win32 *w = W; if (g_shiml_vt_owner == w && g_shiml_vt) return g_shiml_vt; u64 P = PSZ_OF(c); int n = (int)(sizeof vt_IImageList / sizeof vt_IImageList[0]); u64 vt = w32_heap_alloc(w, (u64)n * P, 1); if (!vt) return 0;
  for (int i = 0; i < n; i++) { char nm[64]; e_snprintf(nm, sizeof nm, "IImageList::%s", vt_IImageList[i]); wrp(c, vt + (u64)i * P, w32_shim_slot(w, "comctl32", nm)); } g_shiml_vt = vt; g_shiml_vt_owner = w; return vt; }
static u64 shiml_object(Cpu *c, u64 himl) { Win32 *w = W; for (int i = 0; i < g_nshiml; i++) if (g_shiml[i].w == w && g_shiml[i].himl == himl) return g_shiml[i].addr; u64 P = PSZ_OF(c); u64 vt = shiml_vtable(c); if (!vt) return 0; u64 a = w32_heap_alloc(w, 4 * P, 1); if (!a) return 0; wrp(c, a, vt); wrp(c, a + P, himl); wr32(c, a + 2 * P, 1);
  if (g_nshiml < SHIML_MAX) { g_shiml[g_nshiml].addr = a; g_shiml[g_nshiml].himl = himl; g_shiml[g_nshiml].w = w; g_nshiml++; } return a; }
static u64 shiml_resolve(u64 h) { for (int i = 0; i < g_nshiml; i++) if (g_shiml[i].addr == h) return g_shiml[i].himl; return 0; }
static u64 shiml_this(Cpu *c) { u64 a = ARG(0); for (int i = 0; i < g_nshiml; i++) if (g_shiml[i].addr == a) return g_shiml[i].himl; return iml_of(a) ? a : 0; }
static void s_IImageList_QueryInterface(Cpu *c) { u64 out = ARG(2); if (!out) { RET(0x80070057u); return; } const u8 *g = mem_ptr(c->mem, ARG(1), 16, 0); static const u8 iid_unk[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46 }, iid_il[16] = { 0x26, 0x59, 0xEB, 0x46, 0x2E, 0x58, 0x17, 0x40, 0x9F, 0xDF, 0xE8, 0x99, 0x8D, 0xAA, 0x09, 0x50 }, iid_il2[16] = { 0x83, 0x9D, 0x2B, 0x19, 0xFC, 0x50, 0x7B, 0x45, 0x90, 0xA0, 0x2B, 0x82, 0xA8, 0xB5, 0xDA, 0xE1 };
  if (g && (!e_memcmp(g, iid_unk, 16) || !e_memcmp(g, iid_il, 16) || !e_memcmp(g, iid_il2, 16))) { wrp(c, out, ARG(0)); RET(0); return; } wrp(c, out, 0); RET(0x80004002u); }
static void s_IImageList_AddRef(Cpu *c) { RET(2); }
static void s_IImageList_Release(Cpu *c) { RET(1); }   /* the system image lists live as long as the process */
#define SHIML_FWD(name, fn, nargs) static void s_IImageList_##name(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } call_shim(c, fn, h, ARG(1), ARG(2), ARG(3)); (void)nargs; }
static void s_IImageList_Add(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } call_shim(c, s_ImageList_Add, h, ARG(1), ARG(2), 0); int i = (int)(s32)c->r[RAX]; if (ARG(3)) wr32(c, ARG(3), (u32)i); RET(i >= 0 ? 0 : 0x80004005u); }
static void s_IImageList_ReplaceIcon(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } call_shim(c, s_ImageList_ReplaceIcon, h, ARG(1), ARG(2), 0); int i = (int)(s32)c->r[RAX]; if (ARG(3)) wr32(c, ARG(3), (u32)i); RET(i >= 0 ? 0 : 0x80004005u); }
static void s_IImageList_SetOverlayImage(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } call_shim(c, s_ImageList_SetOverlayImage, h, ARG(1), ARG(2), 0); RET(c->r[RAX] ? 0 : 0x80004005u); }
static void s_IImageList_Replace(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } call_shim(c, s_ImageList_Replace, h, ARG(1), ARG(2), ARG(3)); RET(c->r[RAX] ? 0 : 0x80004005u); }
static void s_IImageList_AddMasked(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } call_shim(c, s_ImageList_AddMasked, h, ARG(1), ARG(2), 0); int i = (int)(s32)c->r[RAX]; if (ARG(3)) wr32(c, ARG(3), (u32)i); RET(i >= 0 ? 0 : 0x80004005u); }
static void s_IImageList_Draw(Cpu *c) { u64 h = shiml_this(c), p = ARG(1); if (!h || !p) { RET(0x80070057u); return; } u64 P = PSZ_OF(c); u64 sv = rdp(c, p + P); wrp(c, p + P, h); call_shim(c, s_ImageList_DrawIndirect, p, 0, 0, 0); wrp(c, p + P, sv); RET(c->r[RAX] ? 0 : 0x80004005u); }
static void s_IImageList_Remove(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } call_shim(c, s_ImageList_Remove, h, ARG(1), 0, 0); RET(c->r[RAX] ? 0 : 0x80004005u); }
static void s_IImageList_GetIcon(Cpu *c) { u64 h = shiml_this(c); if (!h || !ARG(3)) { RET(0x80070057u); return; } call_shim(c, s_ImageList_GetIcon, h, ARG(1), ARG(2), 0); wrp(c, ARG(3), c->r[RAX]); RET(c->r[RAX] ? 0 : 0x80004005u); }
static void s_IImageList_GetImageInfo(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } call_shim(c, s_ImageList_GetImageInfo, h, ARG(1), ARG(2), 0); RET(c->r[RAX] ? 0 : 0x80004005u); }
static void s_IImageList_Copy(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } u64 src = ARG(2); u64 sh = shiml_resolve(src); if (!sh) sh = src; call_shim(c, s_ImageList_Copy, h, ARG(1), sh, ARG(3)); RET(c->r[RAX] ? 0 : 0x80004005u); }   /* flags (5th arg) default to ILCF_MOVE */
static void s_IImageList_Merge(Cpu *c) { RET(0x80004001u); }
static void s_IImageList_Clone(Cpu *c) { u64 h = shiml_this(c); u64 out = ARG(2); if (!h || !out) { RET(0x80070057u); return; } call_shim(c, s_ImageList_Duplicate, h, 0, 0, 0); u64 nh = c->r[RAX]; if (!nh) { RET(0x8007000Eu); return; } wrp(c, out, shiml_object(c, nh)); RET(0); }
static void s_IImageList_GetImageRect(Cpu *c) { u64 h = shiml_this(c); ImgList *il = iml_of(h); u64 rc = ARG(2); int i = (int)ARG(1); if (!il || !rc || i < 0 || i >= il->n) { RET(0x80070057u); return; } wr32(c, rc, 0); wr32(c, rc + 4, 0); wr32(c, rc + 8, (u32)il->cx); wr32(c, rc + 12, (u32)il->cy); RET(0); }
static void s_IImageList_GetIconSize(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } call_shim(c, s_ImageList_GetIconSize, h, ARG(1), ARG(2), 0); RET(0); }
static void s_IImageList_SetIconSize(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } call_shim(c, s_ImageList_SetIconSize, h, ARG(1), ARG(2), 0); RET(c->r[RAX] ? 0 : 0x80004005u); }
static void s_IImageList_GetImageCount(Cpu *c) { u64 h = shiml_this(c); ImgList *il = iml_of(h); if (!il || !ARG(1)) { RET(0x80070057u); return; } wr32(c, ARG(1), (u32)il->n); RET(0); }
static void s_IImageList_SetImageCount(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } call_shim(c, s_ImageList_SetImageCount, h, ARG(1), 0, 0); RET(c->r[RAX] ? 0 : 0x80004005u); }
static void s_IImageList_SetBkColor(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } call_shim(c, s_ImageList_SetBkColor, h, ARG(1), 0, 0); if (ARG(2)) wr32(c, ARG(2), (u32)c->r[RAX]); RET(0); }
static void s_IImageList_GetBkColor(Cpu *c) { u64 h = shiml_this(c); ImgList *il = iml_of(h); if (!il || !ARG(1)) { RET(0x80070057u); return; } wr32(c, ARG(1), il->bk); RET(0); }
static void s_IImageList_BeginDrag(Cpu *c) { RET(0x80004005u); }
static void s_IImageList_EndDrag(Cpu *c) { RET(0); }
static void s_IImageList_DragEnter(Cpu *c) { RET(0); }
static void s_IImageList_DragLeave(Cpu *c) { RET(0); }
static void s_IImageList_DragMove(Cpu *c) { RET(0); }
static void s_IImageList_SetDragCursorImage(Cpu *c) { RET(0); }
static void s_IImageList_DragShowNolock(Cpu *c) { RET(0); }
static void s_IImageList_GetDragImage(Cpu *c) { RET(0x80004005u); }
static void s_IImageList_GetItemFlags(Cpu *c) { if (ARG(2)) wr32(c, ARG(2), 0); RET(0); }
static void s_IImageList_GetOverlayImage(Cpu *c) { if (ARG(2)) wr32(c, ARG(2), (u32)-1); RET(0x80004005u); }
static void s_IImageList_Resize(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } call_shim(c, s_ImageList_SetIconSize, h, ARG(1), ARG(2), 0); RET(0); }
static void s_IImageList_GetOriginalSize(Cpu *c) { u64 h = shiml_this(c); ImgList *il = iml_of(h); if (!il) { RET(0x80070057u); return; } if (ARG(3)) wr32(c, ARG(3), (u32)il->cx); if (ARG(4)) wr32(c, ARG(4), (u32)il->cy); RET(0); }
static void s_IImageList_SetOriginalSize(Cpu *c) { RET(0); }
static void s_IImageList_SetCallback(Cpu *c) { RET(0); }
static void s_IImageList_GetCallback(Cpu *c) { if (ARG(2)) wrp(c, ARG(2), 0); RET(0x80004002u); }
static void s_IImageList_ForceImagePresent(Cpu *c) { RET(0); }
static void s_IImageList_DiscardImages(Cpu *c) { RET(0); }
static void s_IImageList_PreloadImages(Cpu *c) { RET(0); }
static void s_IImageList_GetStatistics(Cpu *c) { RET(0x80004001u); }
static void s_IImageList_Initialize(Cpu *c) { RET(0x80004001u); }
static void s_IImageList_Replace2(Cpu *c) { u64 h = shiml_this(c); if (!h) { RET(0x80070057u); return; } call_shim(c, s_ImageList_Replace, h, ARG(1), ARG(2), ARG(3)); RET(c->r[RAX] ? 0 : 0x80004005u); }
static void s_IImageList_ReplaceFromImageList(Cpu *c) { RET(0x80004001u); }
/* SHGetImageList(iImageList, riid, void **ppv): SHIL_LARGE 0, SHIL_SMALL 1, SHIL_EXTRALARGE 2, SHIL_SYSSMALL 3, SHIL_JUMBO 4 (SHIL_LAST = 4) */
static void s_SHGetImageList(Cpu *c) { int which = (int)(s32)ARG(0); u64 out = ARG(2); if (!out || which < 0 || which > 4) { if (out) wrp(c, out, 0); RET(0x80070057u); return; } u64 himl = sysiml_get(c, which); if (!himl) { wrp(c, out, 0); RET(0x8007000Eu); return; } u64 obj = shiml_object(c, himl); wrp(c, out, obj); RET(obj ? 0 : 0x8007000Eu); }
static void s_Shell_GetImageLists(Cpu *c) { if (ARG(0)) wrp(c, ARG(0), sysiml_get(c, 0)); if (ARG(1)) wrp(c, ARG(1), sysiml_get(c, 1)); RET(1); }
static void s_Shell_GetCachedImageIndexW(Cpu *c) { char path[1040]; text_in(c, ARG(0), 1, path, sizeof path); int idx = (int)(s32)ARG(1); const char *sd = icon_source_dll(path); if (sd && !e_strcmp(sd, "shell32") && idx >= 0 && idx < SYSIML_BASE_ICONS) { RET((u32)idx); return; } sysiml_get(c, 0); sysiml_get(c, 1); u64 hh = str_hash_ci(path) ^ (u64)(u32)idx * 0x9E3779B97F4A7C15ull; for (int i = 0; i < g_sysiml_ncache; i++) if (g_sysiml_cache[i].hash == hh) { RET((u32)g_sysiml_cache[i].index); return; }
  ImgList *l0 = iml_of(g_sysiml[0]); int index = l0 ? l0->n : -1; if (index >= 0) { int any = 0; for (int j = 0; j < 5; j++) if (g_sysiml[j]) { ImgList *il = iml_of(g_sysiml[j]); if (!il) continue; u64 ic = extract_icon(c, path, idx, il->cx, il->cy, 0); if (ic) any = 1; sysiml_put(c, il, index, ic); Icon *o = icon_of(ic); if (o) icon_free(o); } if (!any) index = 0; }
  if (g_sysiml_ncache < SYSIML_CACHE) { g_sysiml_cache[g_sysiml_ncache].hash = hh; g_sysiml_cache[g_sysiml_ncache].index = index; g_sysiml_ncache++; } RET((u32)index); }
static void s_Shell_GetCachedImageIndex(Cpu *c) { s_Shell_GetCachedImageIndexW(c); }
static void s_SHMapPIDLToSystemImageListIndex(Cpu *c) { char path[1040]; path[0] = 0; e_snprintf(path, sizeof path, "%s", pidl_path(c, ARG(1))); int index = shell_icon_index(c, path, 0x10, 0, 0); if (ARG(2)) wr32(c, ARG(2), (u32)index); sysiml_get(c, 0); sysiml_get(c, 1); RET((u32)index); }
#define SHELLICON_SHIMS S(SHGetFileInfoW), S(SHGetFileInfoA), S(SHGetStockIconInfo), S(ExtractIconW), S(ExtractIconA), S(ExtractIconExW), S(ExtractIconExA), S(ExtractAssociatedIconW), S(ExtractAssociatedIconA), S(PrivateExtractIconsW), S(PrivateExtractIconsA), S(SHDefExtractIconW), S(SHDefExtractIconA), \
  S(SHGetImageList), S(Shell_GetImageLists), S(Shell_GetCachedImageIndexW), S(Shell_GetCachedImageIndex), S(SHMapPIDLToSystemImageListIndex), \
  WM(IImageList, QueryInterface), WM(IImageList, AddRef), WM(IImageList, Release), WM(IImageList, Add), WM(IImageList, ReplaceIcon), WM(IImageList, SetOverlayImage), WM(IImageList, Replace), WM(IImageList, AddMasked), WM(IImageList, Draw), WM(IImageList, Remove), WM(IImageList, GetIcon), WM(IImageList, GetImageInfo), WM(IImageList, Copy), WM(IImageList, Merge), WM(IImageList, Clone), WM(IImageList, GetImageRect), WM(IImageList, GetIconSize), WM(IImageList, SetIconSize), WM(IImageList, GetImageCount), WM(IImageList, SetImageCount), WM(IImageList, SetBkColor), WM(IImageList, GetBkColor), WM(IImageList, BeginDrag), WM(IImageList, EndDrag), WM(IImageList, DragEnter), WM(IImageList, DragLeave), WM(IImageList, DragMove), WM(IImageList, SetDragCursorImage), WM(IImageList, DragShowNolock), WM(IImageList, GetDragImage), WM(IImageList, GetItemFlags), WM(IImageList, GetOverlayImage), \
  WM(IImageList, Resize), WM(IImageList, GetOriginalSize), WM(IImageList, SetOriginalSize), WM(IImageList, SetCallback), WM(IImageList, GetCallback), WM(IImageList, ForceImagePresent), WM(IImageList, DiscardImages), WM(IImageList, PreloadImages), WM(IImageList, GetStatistics), WM(IImageList, Initialize), WM(IImageList, Replace2), WM(IImageList, ReplaceFromImageList),
