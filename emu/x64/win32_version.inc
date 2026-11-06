/* win32_version.inc — version.dll: GetFileVersionInfoSize / GetFileVersionInfo / VerQueryValue (A and W) over the real
   RT_VERSION resource of a PE file on the virtual disk (Inno Setup's [Files] version checks, Delphi/VCL "About" boxes,
   installers that compare their own version with the installed one). Included by win32.c. */
static u32 vr32(const u8 *d, u64 len, u64 o) { return o + 4 <= len ? (u32)d[o] | (u32)d[o + 1] << 8 | (u32)d[o + 2] << 16 | (u32)d[o + 3] << 24 : 0; }
static u32 vr16(const u8 *d, u64 len, u64 o) { return o + 2 <= len ? (u32)d[o] | (u32)d[o + 1] << 8 : 0; }
/* RVA → file offset through the section table */
static u64 ver_rva_off(const u8 *d, u64 len, u64 pe, u32 rva) {
  u32 nsec = vr16(d, len, pe + 6), optsz = vr16(d, len, pe + 20); u64 sec = pe + 24 + optsz;
  for (u32 i = 0; i < nsec; i++) { u64 s = sec + 40 * i; u32 va = vr32(d, len, s + 12), vsz = vr32(d, len, s + 8), raw = vr32(d, len, s + 20), rsz = vr32(d, len, s + 16); u32 span = vsz > rsz ? vsz : rsz; if (rva >= va && rva < va + span) return raw + (rva - va); }
  return 0; }
/* first data entry under resource type 16 (RT_VERSION): returns the file offset and size of the VS_VERSIONINFO block */
static int ver_find_block(const u8 *d, u64 len, u64 *off, u32 *size) {
  if (len < 0x40 || d[0] != 'M' || d[1] != 'Z') return 0; u64 pe = vr32(d, len, 0x3c); if (pe + 24 > len || d[pe] != 'P' || d[pe + 1] != 'E') return 0;
  u32 magic = vr16(d, len, pe + 24); u64 dd = pe + 24 + (magic == 0x20b ? 112 : 96); u32 rsrc_rva = vr32(d, len, dd + 2 * 8); if (!rsrc_rva) return 0;
  u64 root = ver_rva_off(d, len, pe, rsrc_rva); if (!root) return 0;
  u32 nn = vr16(d, len, root + 12), ni = vr16(d, len, root + 14); u64 dir = 0;
  for (u32 i = 0; i < nn + ni; i++) { u64 e = root + 16 + 8 * i; u32 id = vr32(d, len, e), sub = vr32(d, len, e + 4); if (i >= nn && id == 16 && (sub & 0x80000000u)) { dir = root + (sub & 0x7fffffff); break; } }
  if (!dir) return 0;
  for (int level = 0; level < 2; level++) { u32 cnt = vr16(d, len, dir + 12) + vr16(d, len, dir + 14); if (!cnt) return 0; u32 sub = vr32(d, len, dir + 16 + 4); if (!(sub & 0x80000000u)) { dir = root + sub; goto leaf; } dir = root + (sub & 0x7fffffff); }
  { u32 cnt = vr16(d, len, dir + 12) + vr16(d, len, dir + 14); if (!cnt) return 0; u32 sub = vr32(d, len, dir + 16 + 4); if (sub & 0x80000000u) return 0; dir = root + sub; }
leaf:
  { u32 rva = vr32(d, len, dir), sz = vr32(d, len, dir + 4); u64 o = ver_rva_off(d, len, pe, rva); if (!o || !sz || o + sz > len) return 0; *off = o; *size = sz; return 1; } }
static int ver_load(Cpu *c, const char *path, u8 **out, u32 *size) {
  int fd = vfs_open(path, 0x80000000u, 3, 0); if (fd < 0) { W->last_error = 2; return 0; } s64 n = vfs_size(fd); if (n <= 0 || n > 512 * 1024 * 1024) { vfs_close(fd); W->last_error = 1813; return 0; }
  u8 *buf = emu_alloc((size_t)n); if (!buf) { vfs_close(fd); W->last_error = 8; return 0; } s64 got = vfs_read(fd, buf, (u64)n); vfs_close(fd);
  u64 off; u32 sz; if (got != n || !ver_find_block(buf, (u64)n, &off, &sz)) { emu_free(buf); W->last_error = 1813; return 0; }   /* ERROR_RESOURCE_TYPE_NOT_FOUND */
  u8 *blk = emu_alloc(sz); if (!blk) { emu_free(buf); W->last_error = 8; return 0; } e_memcpy(blk, buf + off, sz); emu_free(buf); *out = blk; *size = sz; return 1; }
static void ver_info_size(Cpu *c, const char *path, u64 handle_out) { u8 *blk; u32 sz; if (handle_out) wr32(c, handle_out, 0); if (!ver_load(c, path, &blk, &sz)) { RET(0); return; } emu_free(blk); RET(sz * 2 + 64); }   /* room for the ANSI copies VerQueryValueA hands out */
static void ver_info(Cpu *c, const char *path, u64 dst, u32 cap) { u8 *blk; u32 sz; if (!ver_load(c, path, &blk, &sz)) { RET(0); return; } if (cap < sz) { emu_free(blk); W->last_error = 122; RET(0); return; }
  for (u32 i = 0; i < sz; i++) wr8(c, dst + i, blk[i]); for (u32 i = sz; i < cap && i < sz + 64; i++) wr8(c, dst + i, 0); emu_free(blk); RET(1); }
static void s_GetFileVersionInfoSizeW(Cpu *c) { ver_info_size(c, name_w(c, ARG(0)), ARG(1)); }
static void s_GetFileVersionInfoSizeA(Cpu *c) { ver_info_size(c, name_a(c, ARG(0)), ARG(1)); }
static void s_GetFileVersionInfoW(Cpu *c) { ver_info(c, name_w(c, ARG(0)), ARG(3), (u32)ARG(2)); }
static void s_GetFileVersionInfoA(Cpu *c) { ver_info(c, name_a(c, ARG(0)), ARG(3), (u32)ARG(2)); }
static void s_GetFileVersionInfoSizeExW2(Cpu *c) { ver_info_size(c, name_w(c, ARG(1)), ARG(2)); }
static void s_GetFileVersionInfoExW(Cpu *c) { ver_info(c, name_w(c, ARG(1)), ARG(4), (u32)ARG(3)); }
/* VS_VERSIONINFO tree walk in guest memory: { u16 len, u16 valueLen, u16 type, wchar key[], pad, value, pad, children } */
static int ver_key_eq(Cpu *c, u64 a, const char *key) { for (int i = 0;; i++) { u32 ch = rd16(c, a + 2 * i); u32 k = (u8)key[i]; if (ch >= 'a' && ch <= 'z') ch -= 32; if (k >= 'a' && k <= 'z') k -= 32; if (ch != k) return 0; if (!ch) return 1; } }
static u64 ver_node_value(Cpu *c, u64 n, u32 *vlen, u32 *type, u64 *children, u64 *end) {
  u32 len = rd16(c, n), vl = rd16(c, n + 2), ty = rd16(c, n + 4); u64 k = n + 6; while (rd16(c, k)) k += 2; k += 2; k = (k + 3) & ~3ull;
  u32 vbytes = ty == 1 ? vl * 2 : vl; u64 ch = (k + vbytes + 3) & ~3ull; *vlen = vl; *type = ty; *children = ch; *end = n + len; return k; }
static u64 ver_find_child(Cpu *c, u64 from, u64 end, const char *key) { for (u64 n = from; n + 6 <= end;) { u32 len = rd16(c, n); if (!len) break; if (ver_key_eq(c, n + 6, key)) return n; n = (n + len + 3) & ~3ull; } return 0; }
static void ver_query(Cpu *c, int wide) {
  u64 blk = ARG(0); char sub[260]; if (wide) wstr_to_utf8(c, ARG(1), sub, sizeof sub, -1); else { const char *s = rd_str(c, ARG(1), 259); e_snprintf(sub, sizeof sub, "%s", s ? s : ""); }
  u64 out = ARG(2), outlen = ARG(3); u32 vl, ty; u64 ch, end; u64 val = ver_node_value(c, blk, &vl, &ty, &ch, &end);
  if (!sub[0] || (sub[0] == '\\' && !sub[1])) { wrp(c, out, val); wr32(c, outlen, vl); RET(1); return; }   /* "\" → VS_FIXEDFILEINFO */
  /* walk the path: \StringFileInfo\040904E4\FileVersion or \VarFileInfo\Translation */
  u64 node = 0; u64 from = ch, lim = end; char *p = sub; while (*p == '\\') p++;
  while (*p) { char part[128]; int i = 0; while (*p && *p != '\\' && i < 127) part[i++] = *p++; part[i] = 0; while (*p == '\\') p++; node = ver_find_child(c, from, lim, part); if (!node) { RET(0); return; } val = ver_node_value(c, node, &vl, &ty, &ch, &end); from = ch; lim = end; }
  if (!node) { RET(0); return; }
  if (ty == 1 && !wide) {   /* string value for an ANSI caller: an 8-bit copy in the slack after the block (GetFileVersionInfoSize reserved it) */
    u32 blen = rd16(c, blk); u64 scratch = blk + ((blen + 3) & ~3ull); u32 used = rd32(c, scratch); u64 dst = scratch + 4 + used; u32 n = 0; while (n < vl && rd16(c, val + 2 * n)) n++;
    for (u32 i = 0; i < n; i++) { u32 w = rd16(c, val + 2 * i); wr8(c, dst + i, (u8)(w < 256 ? w : '?')); } wr8(c, dst + n, 0); wr32(c, scratch, used + n + 1); wrp(c, out, dst); wr32(c, outlen, n + 1); RET(1); return; }
  wrp(c, out, val); wr32(c, outlen, ty == 1 ? vl : vl); RET(1); }
static void s_VerQueryValueW(Cpu *c) { ver_query(c, 1); }
static void s_VerQueryValueA(Cpu *c) { ver_query(c, 0); }
static void s_VerLanguageNameA(Cpu *c) { const char *s = "English (United States)"; u32 cap = (u32)ARG(2); u32 n = (u32)e_strlen(s); if (cap) { u32 m = n < cap - 1 ? n : cap - 1; for (u32 i = 0; i < m; i++) wr8(c, ARG(1) + i, (u8)s[i]); wr8(c, ARG(1) + m, 0); } RET(n); }
static void s_VerLanguageNameW(Cpu *c) { const char *s = "English (United States)"; u32 cap = (u32)ARG(2); u32 n = (u32)e_strlen(s); if (cap) put_w8(c, ARG(1), s, cap); RET(n); }
#define VERSION_SHIMS S(GetFileVersionInfoSizeW), S(GetFileVersionInfoSizeA), S(GetFileVersionInfoW), S(GetFileVersionInfoA), SA("GetFileVersionInfoSizeExW", GetFileVersionInfoSizeExW2), S(GetFileVersionInfoExW), \
  S(VerQueryValueW), S(VerQueryValueA), S(VerLanguageNameA), S(VerLanguageNameW)
