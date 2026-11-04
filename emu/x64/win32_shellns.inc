/* Shell namespace over the virtual disk: SHGetDesktopFolder, IShellFolder / IShellFolder2 / IPersistFolder2, IEnumIDList,
   SHBindToParent / SHBindToObject and the ITEMIDLIST helpers file managers use (ILCombine, ILGetNext, ILRemoveLastID …).
   PIDLs keep the single-item { u16 cb; char path[]; u16 0 } format of win32_shell.inc: an item IS its full path, so a
   "relative" child pidl is just the child's path and ILCombine of an absolute child is a clone of it.
   Included from win32_shell.inc (win32.c translation unit). Guest layouts:
     folder [vt IShellFolder2][vt IPersistFolder2][refs u32][pad u32][path 1040 UTF-8; "" = Desktop]
     enum   [vt IEnumIDList][refs u32][idx u32][n u32][pad u32][u64 pidl[n]] */
static u64 ns_vt(Cpu *c, const char *iface, const char **names, int n) { u64 P = PSZ_OF(c), vt = w32_heap_alloc(W, (u64)n * P, 1); char full[96]; for (int i = 0; i < n; i++) { e_snprintf(full, sizeof full, "%s::%s", iface, names[i]); wrp(c, vt + (u64)i * P, w32_shim_slot(W, "shell32", full)); } return vt; }
static const char *g_sf_names[20] = { "QueryInterface", "AddRef", "Release", "ParseDisplayName", "EnumObjects", "BindToObject", "BindToStorage", "CompareIDs", "CreateViewObject", "GetAttributesOf", "GetUIObjectOf", "GetDisplayNameOf", "SetNameOf",
  "GetDefaultSearchGUID", "EnumSearches", "GetDefaultColumn", "GetDefaultColumnState", "GetDetailsEx", "GetDetailsOf", "MapColumnToSCID" };
static const char *g_pf_names[6] = { "QueryInterface", "AddRef", "Release", "GetClassID", "Initialize", "GetCurFolder" };
static const char *g_eil_names[7] = { "QueryInterface", "AddRef", "Release", "Next", "Skip", "Reset", "Clone" };
#define NS_PATH(c, o) ((o) + 2 * PSZ_OF(c) + 8)
static const char *ns_path(Cpu *c, u64 o) { const char *p = rd_str(c, NS_PATH(c, o), 1039); return p ? p : ""; }
static u64 ns_folder_create(Cpu *c, const char *path) { u64 P = PSZ_OF(c), o = w32_heap_alloc(W, 2 * P + 8 + 1040, 1); if (!o) return 0;
  wrp(c, o, ns_vt(c, "IShellFolder", g_sf_names, 20)); wrp(c, o + P, ns_vt(c, "IPersistFolder", g_pf_names, 6)); wr32(c, o + 2 * P, 1); wr32(c, o + 2 * P + 4, 0);
  u8 *d = mem_ptr(W->mem, NS_PATH(c, o), 1040, 1); if (d) { size_t l = e_strlen(path); if (l > 1039) l = 1039; e_memcpy(d, path, l); d[l] = 0; if (l > 3 && d[l - 1] == '\\') d[l - 1] = 0; } return o; }
/* an interface pointer for riid on folder object o (IUnknown / IShellFolder / IShellFolder2 → sub 0; IPersist / IPersistFolder / IPersistFolder2 → sub 1) */
static int ns_qi(Cpu *c, u64 o, u64 riid, u64 ppv) { u32 d1 = rd32(c, riid); u64 P = PSZ_OF(c); if (!ppv) return 0;
  if (d1 == 0 /* IUnknown */ || d1 == 0x000214E6u /* IShellFolder */ || d1 == 0x93F2F68Cu /* IShellFolder2 */) wrp(c, ppv, o);
  else if (d1 == 0x0000010Cu /* IPersist */ || d1 == 0x000214EAu /* IPersistFolder */ || d1 == 0x1AC3D9F0u /* IPersistFolder2 */) wrp(c, ppv, o + P);
  else { wrp(c, ppv, 0); return 0; }
  wr32(c, o + 2 * P, rd32(c, o + 2 * P) + 1); return 1; }
static void ns_release(Cpu *c, u64 o) { u64 a = o + 2 * PSZ_OF(c); u32 n = rd32(c, a); if (n) n--; wr32(c, a, n); if (!n) w32_heap_free(W, o); RET(n); }
static void s_IShellFolder_QueryInterface(Cpu *c) { RET(ns_qi(c, ARG(0), ARG(1), ARG(2)) ? 0 : 0x80004002u); }
static void s_IPersistFolder_QueryInterface(Cpu *c) { RET(ns_qi(c, ARG(0) - PSZ_OF(c), ARG(1), ARG(2)) ? 0 : 0x80004002u); }
static void s_IShellFolder_AddRef(Cpu *c) { u64 r = ARG(0) + 2 * PSZ_OF(c); wr32(c, r, rd32(c, r) + 1); RET(rd32(c, r)); }
static void s_IPersistFolder_AddRef(Cpu *c) { u64 r = ARG(0) + PSZ_OF(c); wr32(c, r, rd32(c, r) + 1); RET(rd32(c, r)); }
static void s_IShellFolder_Release(Cpu *c) { ns_release(c, ARG(0)); }
static void s_IPersistFolder_Release(Cpu *c) { ns_release(c, ARG(0) - PSZ_OF(c)); }
/* path helpers */
static void ns_join(const char *dir, const char *name, char *out, size_t cap) { if (!dir[0] || (name[1] == ':' && name[0]) || name[0] == '\\') e_snprintf(out, cap, "%s", name); else e_snprintf(out, cap, "%s%s%s", dir, dir[e_strlen(dir) - 1] == '\\' ? "" : "\\", name); size_t l = e_strlen(out); if (l > 3 && out[l - 1] == '\\') out[l - 1] = 0; }
static const char *ns_base(const char *p) { const char *b = p; for (const char *q = p; *q; q++) if (*q == '\\' && q[1]) b = q + 1; return b; }
static int ns_is_root(const char *p) { return p[0] && p[1] == ':' && (p[2] == 0 || (p[2] == '\\' && p[3] == 0)); }
/* STRRET: { u32 uType; union { WCHAR *pOleStr; u32 uOffset; char cStr[260]; } } — always STRRET_WSTR with a CoTaskMem string */
static void ns_strret(Cpu *c, u64 sr, const char *s) { if (!sr) return; wr32(c, sr, 0); u64 a = w32_heap_alloc(W, (e_strlen(s) + 1) * 2, 0); if (a) put_w8(c, a, s, 0); wrp(c, sr + PSZ_OF(c), a); }
static void ns_display_name(const char *path, u32 flags, char *out, size_t cap) {
  if (!path[0]) { e_snprintf(out, cap, "%s", (flags & 0x8000) ? "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}" : "Desktop"); return; }   /* SHGDN_FORPARSING on the desktop: the "This PC" CLSID */
  if (flags & 0x8000 /* SHGDN_FORPARSING */) { if ((flags & 0x1) /* INFOLDER */) e_snprintf(out, cap, "%s", ns_is_root(path) ? path : ns_base(path)); else e_snprintf(out, cap, "%s", path); return; }
  if (ns_is_root(path)) { e_snprintf(out, cap, "Local Disk (%c:)", path[0]); return; }
  e_snprintf(out, cap, "%s", ns_base(path)); }
static u32 ns_attrs(const char *path) { if (!path[0]) return 0x20000000u | 0x80000000u | 0x10000000u | 0x00000100u;   /* desktop: FOLDER | HASSUBFOLDER | FILESYSANCESTOR | DROPTARGET */
  u64 sz; int st = vfs_stat(path, &sz); if (!st) return 0;
  u32 a = 0x40000000u /* FILESYSTEM */ | 0x00000002u /* CANMOVE */ | 0x00000001u /* CANCOPY */ | 0x00000020u /* CANDELETE */ | 0x00000010u /* CANRENAME */ | 0x00000004u /* CANLINK */ | 0x00000040u /* HASPROPSHEET */;
  if (st == 2) { a |= 0x20000000u /* FOLDER */ | 0x10000000u /* FILESYSANCESTOR */ | 0x00000100u /* DROPTARGET */ | 0x00000200u /* STORAGE */ ; if (vfs_dir_nonempty(path)) a |= 0x80000000u /* HASSUBFOLDER */; if (ns_is_root(path)) a &= ~(0x20u | 0x10u | 0x2u); } else a |= 0x00400000u /* STREAM */;
  return a; }
/* IShellFolder */
static void s_IShellFolder_ParseDisplayName(Cpu *c) { /* (hwnd, bc, name, *eaten, **pidl, *attrs) */ if (!ARG(2) || !ARG(4)) { RET(0x80070057u); return; }
  char name[1040], full[1040]; wstr_to_utf8(c, ARG(2), name, sizeof name, -1); if (!e_strncmp(name, "::{20D04FE0", 11)) name[0] = 0; ns_join(ns_path(c, ARG(0)), name, full, sizeof full);
  u64 sz; if (full[0] && !vfs_stat(full, &sz)) { wrp(c, ARG(4), 0); W->last_error = 2; RET(0x80070002u); return; }
  if (ARG(3)) wr32(c, ARG(3), (u32)e_strlen(name)); wrp(c, ARG(4), pidl_make(W, full)); if (ARG(5)) wr32(c, ARG(5), rd32(c, ARG(5)) & ns_attrs(full)); RET(0); }
static u64 ns_enum_create(Cpu *c, const char *dir, u32 flags) { /* SHCONTF_FOLDERS 0x20, NONFOLDERS 0x40, INCLUDEHIDDEN 0x80 */
  u64 items[2048]; int n = 0;
  if (!dir[0]) { if (flags & 0x20) items[n++] = pidl_make(W, "C:\\"); }
  else { char pat[1060]; e_snprintf(pat, sizeof pat, "%s%s*", dir, dir[e_strlen(dir) - 1] == '\\' ? "" : "\\"); int h = vfs_find_first(pat); char nm[260]; u64 sz; int isd;
    while (h >= 0 && n < 2048 && vfs_find_next(h, nm, sizeof nm, &sz, &isd)) { if (!e_strcmp(nm, ".") || !e_strcmp(nm, "..")) continue; if (isd ? !(flags & 0x20) : !(flags & 0x40)) continue; char full[1040]; ns_join(dir, nm, full, sizeof full); items[n++] = pidl_make(W, full); }
    if (h >= 0) vfs_find_close(h); }
  u64 P = PSZ_OF(c), o = w32_heap_alloc(W, P + 16 + 8 * (u64)(n ? n : 1), 1); if (!o) return 0; wrp(c, o, ns_vt(c, "IEnumIDList", g_eil_names, 7)); wr32(c, o + P, 1); wr32(c, o + P + 4, 0); wr32(c, o + P + 8, (u32)n); wr32(c, o + P + 12, 0);
  for (int i = 0; i < n; i++) wr64(c, o + P + 16 + 8 * (u64)i, items[i]); return o; }
static void s_IShellFolder_EnumObjects(Cpu *c) { /* (hwnd, flags, **enum) */ if (!ARG(3)) { RET(0x80070057u); return; } u64 o = ns_enum_create(c, ns_path(c, ARG(0)), (u32)ARG(2)); wrp(c, ARG(3), o); RET(o ? 0 : 0x8007000Eu); }
static void s_IShellFolder_BindToObject(Cpu *c) { /* (pidl, bc, riid, **ppv) */ if (!ARG(4)) { RET(0x80070057u); return; } char full[1040]; ns_join(ns_path(c, ARG(0)), pidl_path(c, ARG(1)), full, sizeof full);
  u64 sz; if (full[0] && vfs_stat(full, &sz) != 2) { wrp(c, ARG(4), 0); RET(0x80070003u); return; }
  u64 o = ns_folder_create(c, full); if (!o) { wrp(c, ARG(4), 0); RET(0x8007000Eu); return; } if (!ns_qi(c, o, ARG(3), ARG(4))) { w32_heap_free(W, o); RET(0x80004002u); return; } wr32(c, o + 2 * PSZ_OF(c), 1); RET(0); }
static void s_IShellFolder_BindToStorage(Cpu *c) { if (ARG(4)) wrp(c, ARG(4), 0); RET(0x80004002u); }
static void s_IShellFolder_CompareIDs(Cpu *c) { /* (lParam, pidl1, pidl2): folders first, then case-insensitive by path; HRESULT with the result in the low word */
  const char *a = pidl_path(c, ARG(2)), *b = pidl_path(c, ARG(3)); u64 sz; int da = a[0] ? vfs_stat(a, &sz) == 2 : 1, db = b[0] ? vfs_stat(b, &sz) == 2 : 1; int r = da != db ? (da ? -1 : 1) : e_stricmp(a, b); r = r < 0 ? -1 : r > 0 ? 1 : 0; RET((u32)(u16)(s16)r); }
static void s_IShellFolder_CreateViewObject(Cpu *c) { if (ARG(3)) wrp(c, ARG(3), 0); RET(0x80004002u); }
static void s_IShellFolder_GetAttributesOf(Cpu *c) { /* (cidl, apidl, *inout) */ u32 n = (u32)ARG(1); u64 P = PSZ_OF(c); if (!ARG(3)) { RET(0x80070057u); return; } u32 mask = rd32(c, ARG(3)), acc = 0xFFFFFFFFu;
  if (!n) acc = ns_attrs(ns_path(c, ARG(0))); else for (u32 i = 0; i < n; i++) { char full[1040]; ns_join(ns_path(c, ARG(0)), pidl_path(c, rdp(c, ARG(2) + (u64)i * P)), full, sizeof full); acc &= ns_attrs(full); }
  wr32(c, ARG(3), acc & mask); RET(0); }
static void s_IShellFolder_GetUIObjectOf(Cpu *c) { /* (hwnd, cidl, apidl, riid, rsv, **ppv): no icon extractors / context menus here — SHGetFileInfo covers icons */ if (ARG(5)) wrp(c, ARG(5), 0); RET(0x80004002u); }
static void s_IShellFolder_GetDisplayNameOf(Cpu *c) { /* (pidl, flags, STRRET*) */ char full[1040], out[1040]; if (!ARG(3)) { RET(0x80070057u); return; } const char *rel = pidl_path(c, ARG(1)); if (!rel[0]) e_snprintf(full, sizeof full, "%s", ns_path(c, ARG(0))); else ns_join(ns_path(c, ARG(0)), rel, full, sizeof full);
  ns_display_name(full, (u32)ARG(2), out, sizeof out); ns_strret(c, ARG(3), out); RET(0); }
static void s_IShellFolder_SetNameOf(Cpu *c) { /* (hwnd, pidl, newname, flags, **pidlout): a real rename on the disk */ char full[1040], nn[520], to[1040]; ns_join(ns_path(c, ARG(0)), pidl_path(c, ARG(2)), full, sizeof full); wstr_to_utf8(c, ARG(3), nn, sizeof nn, -1);
  if (!full[0] || !nn[0] || ns_is_root(full)) { RET(0x80070005u); return; } char dir[1040]; e_snprintf(dir, sizeof dir, "%s", full); char *b = dir + (ns_base(dir) - dir); if (b > dir) b[-1] = 0; ns_join(dir, nn, to, sizeof to);
  if (!vfs_rename(full, to)) { RET(0x80070005u); return; } if (ARG(5)) wrp(c, ARG(5), pidl_make(W, to)); RET(0); }
/* IShellFolder2 */
static void s_IShellFolder_GetDefaultSearchGUID(Cpu *c) { RET(0x80004001u); }
static void s_IShellFolder_EnumSearches(Cpu *c) { if (ARG(1)) wrp(c, ARG(1), 0); RET(0x80004001u); }
static void s_IShellFolder_GetDefaultColumn(Cpu *c) { if (ARG(2)) wr32(c, ARG(2), 0); if (ARG(3)) wr32(c, ARG(3), 0); RET(0); }
static void s_IShellFolder_GetDefaultColumnState(Cpu *c) { u32 col = (u32)ARG(1); if (col > 3 || !ARG(2)) { RET(0x80070057u); return; } wr32(c, ARG(2), (col == 1 ? 0x2u /* TYPE_INT */ : col == 3 ? 0x3u /* TYPE_DATE */ : 0x1u /* TYPE_STR */) | 0x10u /* ONBYDEFAULT */); RET(0); }
static void s_IShellFolder_GetDetailsEx(Cpu *c) { RET(0x80004001u); }
static void s_IShellFolder_GetDetailsOf(Cpu *c) { /* (pidl, col, SHELLDETAILS* { int fmt; int cxChar; STRRET str }) : Name / Size / Type / Date modified */ u32 col = (u32)ARG(2); u64 sd = ARG(3); if (col > 3 || !sd) { RET(0x80070057u); return; }
  static const char *hdr[4] = { "Name", "Size", "Type", "Date modified" }; static const int cx[4] = { 30, 12, 20, 20 }; wr32(c, sd, col == 1 ? 1 /* LVCFMT_RIGHT */ : 0); wr32(c, sd + 4, (u32)cx[col]);
  if (!ARG(1)) { ns_strret(c, sd + 8, hdr[col]); RET(0); return; }
  char full[1040], out[1040]; ns_join(ns_path(c, ARG(0)), pidl_path(c, ARG(1)), full, sizeof full); u64 sz = 0; int st = full[0] ? vfs_stat(full, &sz) : 2;
  if (col == 0) ns_display_name(full, 0, out, sizeof out); else if (col == 1) { if (st == 2) out[0] = 0; else e_snprintf(out, sizeof out, "%llu KB", (unsigned long long)((sz + 1023) / 1024)); }
  else if (col == 2) { if (st == 2) e_snprintf(out, sizeof out, "%s", ns_is_root(full) ? "Local Disk" : "File folder"); else { const char *dot = 0; for (const char *q = ns_base(full); *q; q++) if (*q == '.') dot = q; if (dot && dot[1]) { char ext[32]; e_snprintf(ext, sizeof ext, "%s", dot + 1); for (char *q = ext; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32; e_snprintf(out, sizeof out, "%s File", ext); } else e_snprintf(out, sizeof out, "File"); } }
  else out[0] = 0;
  ns_strret(c, sd + 8, out); RET(0); }
static void s_IShellFolder_MapColumnToSCID(Cpu *c) { RET(0x80004001u); }
/* IPersistFolder2 */
static void s_IPersistFolder_GetClassID(Cpu *c) { if (!ARG(1)) { RET(0x80070057u); return; } const char *p = ns_path(c, ARG(0) - PSZ_OF(c)); u8 *d = mem_ptr(W->mem, ARG(1), 16, 1); if (!d) { RET(0x80070057u); return; }
  static const u8 desktop[16] = { 0x00,0x14,0x02,0x00,0x00,0x00,0x00,0x00,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46 };   /* CLSID_ShellDesktop {00021400-0000-0000-C000-000000000046} */
  static const u8 fsfolder[16] = { 0xA0,0x4B,0x36,0xF3,0xB9,0x65,0xCE,0x11,0xA9,0xBA,0x00,0xAA,0x00,0x4A,0xE8,0x37 };  /* CLSID_ShellFSFolder {F3364BA0-65B9-11CE-A9BA-00AA004AE837} */
  e_memcpy(d, p[0] ? fsfolder : desktop, 16); RET(0); }
static void s_IPersistFolder_Initialize(Cpu *c) { u64 o = ARG(0) - PSZ_OF(c); const char *p = pidl_path(c, ARG(1)); u8 *d = mem_ptr(W->mem, NS_PATH(c, o), 1040, 1); if (d) { size_t l = e_strlen(p); if (l > 1039) l = 1039; e_memcpy(d, p, l); d[l] = 0; } RET(0); }
static void s_IPersistFolder_GetCurFolder(Cpu *c) { if (!ARG(1)) { RET(0x80070057u); return; } wrp(c, ARG(1), pidl_make(W, ns_path(c, ARG(0) - PSZ_OF(c)))); RET(0); }
/* IEnumIDList */
static void s_IEnumIDList_QueryInterface(Cpu *c) { u32 d1 = rd32(c, ARG(1)); if (!ARG(2)) { RET(0x80070057u); return; } if (d1 == 0 || d1 == 0x000214F2u) { u64 r = ARG(0) + PSZ_OF(c); wr32(c, r, rd32(c, r) + 1); wrp(c, ARG(2), ARG(0)); RET(0); } else { wrp(c, ARG(2), 0); RET(0x80004002u); } }
static void s_IEnumIDList_AddRef(Cpu *c) { u64 r = ARG(0) + PSZ_OF(c); wr32(c, r, rd32(c, r) + 1); RET(rd32(c, r)); }
static void s_IEnumIDList_Release(Cpu *c) { u64 P = PSZ_OF(c), r = ARG(0) + P; u32 n = rd32(c, r); if (n) n--; wr32(c, r, n); if (!n) { u32 cnt = rd32(c, r + 8); for (u32 i = 0; i < cnt; i++) { u64 p = rd64(c, r + 16 + 8 * (u64)i); if (p) w32_heap_free(W, p); } w32_heap_free(W, ARG(0)); } RET(n); }
static void s_IEnumIDList_Next(Cpu *c) { /* (celt, rgelt, *fetched) */ u64 P = PSZ_OF(c), r = ARG(0) + P; u32 want = (u32)ARG(1), idx = rd32(c, r + 4), cnt = rd32(c, r + 8), got = 0; if (!ARG(2)) { RET(0x80070057u); return; }
  while (got < want && idx < cnt) { wrp(c, ARG(2) + (u64)got * P, pidl_make(W, pidl_path(c, rd64(c, r + 16 + 8 * (u64)idx)))); got++; idx++; }
  wr32(c, r + 4, idx); if (ARG(3)) wr32(c, ARG(3), got); RET(got == want ? 0 : 1); }
static void s_IEnumIDList_Skip(Cpu *c) { u64 r = ARG(0) + PSZ_OF(c); u32 idx = rd32(c, r + 4) + (u32)ARG(1), cnt = rd32(c, r + 8); if (idx > cnt) { wr32(c, r + 4, cnt); RET(1); return; } wr32(c, r + 4, idx); RET(0); }
static void s_IEnumIDList_Reset(Cpu *c) { wr32(c, ARG(0) + PSZ_OF(c) + 4, 0); RET(0); }
static void s_IEnumIDList_Clone(Cpu *c) { if (ARG(1)) wrp(c, ARG(1), 0); RET(0x80004001u); }
/* entry points */
static void s_SHGetSettings(Cpu *c) { /* (SHELLFLAGSTATE*, mask): show all objects + extensions + info tips, classic view off */ if (ARG(0)) wr32(c, ARG(0), (0x1u | 0x2u | 0x800u) & (u32)ARG(1)); }
static void s_SHGetSetSettings(Cpu *c) { /* (SHELLSTATE*, mask, set) */ if (ARG(0) && !ARG(2)) { wr32(c, ARG(0), 0x1u | 0x2u | 0x800u); wr32(c, ARG(0) + 4, 0); } }
static void s_SHGetDesktopFolder(Cpu *c) { if (!ARG(0)) { RET(0x80070057u); return; } u64 o = ns_folder_create(c, ""); wrp(c, ARG(0), o); RET(o ? 0 : 0x8007000Eu); }
static void s_SHBindToParent(Cpu *c) { /* (pidl, riid, **ppv, **pidlLast): the parent folder of the item + the item itself as the "last id" */ if (!ARG(0) || !ARG(2)) { RET(0x80070057u); return; }
  const char *p = pidl_path(c, ARG(0)); char dir[1040]; if (!p[0] || ns_is_root(p)) dir[0] = 0; else { e_snprintf(dir, sizeof dir, "%s", p); char *b = dir + (ns_base(dir) - dir); if (b > dir) b[-1] = 0; if (dir[1] == ':' && !dir[2]) { dir[2] = '\\'; dir[3] = 0; } }
  u64 o = ns_folder_create(c, dir); if (!o) { wrp(c, ARG(2), 0); RET(0x8007000Eu); return; } if (!ns_qi(c, o, ARG(1), ARG(2))) { w32_heap_free(W, o); RET(0x80004002u); return; } wr32(c, o + 2 * PSZ_OF(c), 1); if (ARG(3)) wrp(c, ARG(3), ARG(0)); RET(0); }
static void s_SHBindToObject(Cpu *c) { /* (psf, pidl, bc, riid, **ppv) */ if (!ARG(4)) { RET(0x80070057u); return; } char full[1040]; const char *base = ARG(0) ? ns_path(c, ARG(0)) : ""; ns_join(base, pidl_path(c, ARG(1)), full, sizeof full);
  u64 sz; if (full[0] && vfs_stat(full, &sz) != 2) { wrp(c, ARG(4), 0); RET(0x80070003u); return; } u64 o = ns_folder_create(c, full); if (!o) { RET(0x8007000Eu); return; } if (!ns_qi(c, o, ARG(3), ARG(4))) { w32_heap_free(W, o); RET(0x80004002u); return; } wr32(c, o + 2 * PSZ_OF(c), 1); RET(0); }
static void s_SHBindToFolderIDListParent(Cpu *c) { /* (psfRoot, pidl, riid, **ppv, **pidlLast) */ if (!ARG(3)) { RET(0x80070057u); return; } const char *p = pidl_path(c, ARG(1)); char dir[1040]; if (!p[0] || ns_is_root(p)) dir[0] = 0; else { e_snprintf(dir, sizeof dir, "%s", p); char *b = dir + (ns_base(dir) - dir); if (b > dir) b[-1] = 0; if (dir[1] == ':' && !dir[2]) { dir[2] = '\\'; dir[3] = 0; } }
  u64 o = ns_folder_create(c, dir); if (!o) { RET(0x8007000Eu); return; } if (!ns_qi(c, o, ARG(2), ARG(3))) { w32_heap_free(W, o); RET(0x80004002u); return; } wr32(c, o + 2 * PSZ_OF(c), 1); if (ARG(4)) wrp(c, ARG(4), ARG(1)); RET(0); }
static void s_SHILCreateFromPath(Cpu *c) { char path[1040]; wstr_to_utf8(c, ARG(0), path, sizeof path, -1); u64 sz; if (!vfs_stat(path, &sz)) { if (ARG(1)) wrp(c, ARG(1), 0); RET(0x80070002u); return; } if (ARG(1)) wrp(c, ARG(1), pidl_make(W, path)); if (ARG(2)) wr32(c, ARG(2), rd32(c, ARG(2)) & ns_attrs(path)); RET(0); }
static void s_ILCreateFromPathA(Cpu *c) { const char *p = rd_str(c, ARG(0), 1000); RET(pidl_make(W, p ? p : "")); }
static void s_ILCombine(Cpu *c) { if (!ARG(0) && !ARG(1)) { RET(0); return; } char full[1040]; ns_join(ARG(0) ? pidl_path(c, ARG(0)) : "", ARG(1) ? pidl_path(c, ARG(1)) : "", full, sizeof full); RET(pidl_make(W, full)); }
static void s_ILGetNext(Cpu *c) { if (!ARG(0)) { RET(0); return; } u16 cb = rd16(c, ARG(0)); RET(cb ? ARG(0) + cb : 0); }
static void s_ILGetSize(Cpu *c) { if (!ARG(0)) { RET(0); return; } u16 cb = rd16(c, ARG(0)); RET(cb ? (u32)cb + 2 : 2); }
static void s_ILFindLastID(Cpu *c) { RET(ARG(0)); }
static void s_ILCloneFirst(Cpu *c) { RET(ARG(0) ? pidl_make(W, pidl_path(c, ARG(0))) : 0); }
static void s_ILIsParent(Cpu *c) { /* (parent, child, immediate) */ const char *a = pidl_path(c, ARG(0)), *b = pidl_path(c, ARG(1)); size_t la = e_strlen(a); if (la && a[la - 1] == '\\') la--; if (e_strnicmp(a, b, la) || (la && b[la] != '\\' && b[la] != 0) || !b[la ? la + 1 : 0]) { RET(0); return; }
  if (ARG(2)) { const char *rest = b + (la ? la + 1 : 0); for (const char *q = rest; *q; q++) if (*q == '\\' && q[1]) { RET(0); return; } } RET(1); }
static void s_ILFindChild(Cpu *c) { const char *a = pidl_path(c, ARG(0)), *b = pidl_path(c, ARG(1)); size_t la = e_strlen(a); if (la && a[la - 1] == '\\') la--; if (la && (e_strnicmp(a, b, la) || (b[la] != '\\' && b[la] != 0))) { RET(0); return; } if (!la && !b[0]) { RET(0); return; } RET(ARG(1)); }
static void s_ILRemoveLastID(Cpu *c) { if (!ARG(0)) { RET(0); return; } const char *p = pidl_path(c, ARG(0)); if (!p[0]) { RET(0); return; } char dir[1040]; if (ns_is_root(p)) dir[0] = 0; else { e_snprintf(dir, sizeof dir, "%s", p); char *b = dir + (ns_base(dir) - dir); if (b > dir) b[-1] = 0; if (dir[1] == ':' && !dir[2]) { dir[2] = '\\'; dir[3] = 0; } }
  size_t l = e_strlen(dir); u8 *d = mem_ptr(W->mem, ARG(0), l + 6, 1); if (!d) { RET(0); return; } d[0] = (u8)(l + 3); d[1] = (u8)((l + 3) >> 8); e_memcpy(d + 2, dir, l + 1); d[l + 3] = 0; d[l + 4] = 0; d[l + 5] = 0; RET(1); }
static void s_ILIsEmpty(Cpu *c) { RET(!ARG(0) || rd16(c, ARG(0)) == 0); }
static void s_ILAppendID(Cpu *c) { /* (pidl, SHITEMID*, append): our items carry full paths, so the result is the joined path */ char full[1040]; ns_join(ARG(0) ? pidl_path(c, ARG(0)) : "", ARG(1) ? pidl_path(c, ARG(1)) : "", full, sizeof full); if (ARG(0)) w32_heap_free(W, ARG(0)); RET(pidl_make(W, full)); }
#define SHELLNS_SHIMS \
  S(SHGetDesktopFolder), S(SHGetSettings), S(SHGetSetSettings), S(SHBindToParent), S(SHBindToObject), S(SHBindToFolderIDListParent), S(SHILCreateFromPath), S(ILCreateFromPathA), S(ILCombine), S(ILGetNext), S(ILGetSize), S(ILFindLastID), S(ILCloneFirst), S(ILIsParent), S(ILFindChild), S(ILRemoveLastID), S(ILIsEmpty), S(ILAppendID), \
  SA("IShellFolder::QueryInterface", IShellFolder_QueryInterface), SA("IShellFolder::AddRef", IShellFolder_AddRef), SA("IShellFolder::Release", IShellFolder_Release), SA("IShellFolder::ParseDisplayName", IShellFolder_ParseDisplayName), SA("IShellFolder::EnumObjects", IShellFolder_EnumObjects), \
  SA("IShellFolder::BindToObject", IShellFolder_BindToObject), SA("IShellFolder::BindToStorage", IShellFolder_BindToStorage), SA("IShellFolder::CompareIDs", IShellFolder_CompareIDs), SA("IShellFolder::CreateViewObject", IShellFolder_CreateViewObject), SA("IShellFolder::GetAttributesOf", IShellFolder_GetAttributesOf), \
  SA("IShellFolder::GetUIObjectOf", IShellFolder_GetUIObjectOf), SA("IShellFolder::GetDisplayNameOf", IShellFolder_GetDisplayNameOf), SA("IShellFolder::SetNameOf", IShellFolder_SetNameOf), SA("IShellFolder::GetDefaultSearchGUID", IShellFolder_GetDefaultSearchGUID), SA("IShellFolder::EnumSearches", IShellFolder_EnumSearches), \
  SA("IShellFolder::GetDefaultColumn", IShellFolder_GetDefaultColumn), SA("IShellFolder::GetDefaultColumnState", IShellFolder_GetDefaultColumnState), SA("IShellFolder::GetDetailsEx", IShellFolder_GetDetailsEx), SA("IShellFolder::GetDetailsOf", IShellFolder_GetDetailsOf), SA("IShellFolder::MapColumnToSCID", IShellFolder_MapColumnToSCID), \
  SA("IPersistFolder::QueryInterface", IPersistFolder_QueryInterface), SA("IPersistFolder::AddRef", IPersistFolder_AddRef), SA("IPersistFolder::Release", IPersistFolder_Release), SA("IPersistFolder::GetClassID", IPersistFolder_GetClassID), SA("IPersistFolder::Initialize", IPersistFolder_Initialize), SA("IPersistFolder::GetCurFolder", IPersistFolder_GetCurFolder), \
  SA("IEnumIDList::QueryInterface", IEnumIDList_QueryInterface), SA("IEnumIDList::AddRef", IEnumIDList_AddRef), SA("IEnumIDList::Release", IEnumIDList_Release), SA("IEnumIDList::Next", IEnumIDList_Next), SA("IEnumIDList::Skip", IEnumIDList_Skip), SA("IEnumIDList::Reset", IEnumIDList_Reset), SA("IEnumIDList::Clone", IEnumIDList_Clone),
