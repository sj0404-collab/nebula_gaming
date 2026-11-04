/* ctxmenu.inc — the shell context menu of files and folders on the virtual disk: IContextMenu / IContextMenu2 / IContextMenu3
   returned by IShellFolder::GetUIObjectOf(IID_IContextMenu) to hosts (file managers build their own popup from it:
   QueryContextMenu merges the verbs into the host's HMENU, InvokeCommand runs them, GetCommandString names them), and the
   same object drives the right-click menu of the default folder view (defview.inc): NM_RCLICK → popup through the
   TrackPopupMenu modal → WM_COMMAND → verb.  Verbs: open / explore, cut, copy, paste, delete, properties on items;
   refresh, paste, new folder, properties on the folder background.  Cut/copy fill a file-list clipboard that is also
   visible to programs as CF_HDROP.  Lives in gdi.c after defview.inc. */
#define CM_MAX 16
#define CM_ITEMS 64
enum { CMV_OPEN = 0, CMV_EXPLORE, CMV_CUT, CMV_COPY, CMV_PASTE, CMV_DELETE, CMV_PROPERTIES, CMV_REFRESH, CMV_NEWFOLDER, CMV_COUNT };
static const char *const cm_verb_name[CMV_COUNT] = { "open", "explore", "cut", "copy", "paste", "delete", "properties", "refresh", "NewFolder" };
static const char *const cm_verb_text[CMV_COUNT] = { "&Open", "E&xplore", "Cu&t", "&Copy", "&Paste", "&Delete", "P&roperties", "R&efresh", "New &folder" };
static const char *const cm_verb_help[CMV_COUNT] = { "Opens the selected items.", "Opens the selected folder in a new window.", "Removes the selected items and copies them onto the Clipboard.", "Copies the selected items to the Clipboard.", "Inserts the items you have copied or cut into the selected location.", "Deletes the selected items.", "Displays the properties of the selected items.", "Refreshes the contents of the window.", "Creates a new, empty folder." };
typedef struct { int used; Win32 *w; u64 addr; int refs; char folder[1040]; int n; char (*paths)[520]; int all_dirs; int bg; u32 first; int map[CMV_COUNT]; int nmap; } CtxMenu;
static CtxMenu g_cm[CM_MAX];
static const char *const vt_IContextMenu3[] = { "QueryInterface", "AddRef", "Release", "QueryContextMenu", "InvokeCommand", "GetCommandString", "HandleMenuMsg", "HandleMenuMsg2" };
static u64 g_cm_vt; static Win32 *g_cm_vt_owner;
static u64 cm_vtable(Cpu *c) { Win32 *w = W; if (g_cm_vt_owner != w) { g_cm_vt = 0; g_cm_vt_owner = w; } if (g_cm_vt) return g_cm_vt; u64 P = PSZ_OF(c); int n = (int)(sizeof vt_IContextMenu3 / sizeof vt_IContextMenu3[0]); u64 vt = w32_heap_alloc(w, (u64)n * P, 1); if (!vt) return 0;
  for (int i = 0; i < n; i++) { char nm[64]; e_snprintf(nm, sizeof nm, "IContextMenu::%s", vt_IContextMenu3[i]); wrp(c, vt + (u64)i * P, w32_shim_slot(w, "shell32", nm)); } g_cm_vt = vt; return vt; }
static CtxMenu *cm_of(Cpu *c, u64 a) { for (int i = 0; i < CM_MAX; i++) if (g_cm[i].used && g_cm[i].w == W && g_cm[i].addr == a) return &g_cm[i]; return 0; }
static void cm_free(CtxMenu *m) { if (m->paths) emu_free(m->paths); m->paths = 0; m->used = 0; }
static void cm_join(const char *folder, const char *name, char *out, size_t cap) { size_t fl = e_strlen(folder); if (!fl) e_snprintf(out, cap, "%s", name); else e_snprintf(out, cap, "%s%s%s", folder, folder[fl - 1] == '\\' ? "" : "\\", name); }
static const char *cm_base(const char *path) { const char *b = path; for (const char *p = path; *p; p++) if (*p == '\\' && p[1]) b = p + 1; return b; }
/* the object: [vt IContextMenu3][refs u32][idx u32]; paths = full paths of the items (n == 0: the folder itself / background) */
static CtxMenu *cm_create(Cpu *c, const char *folder, int bg) { Win32 *w = W; int k; for (k = 0; k < CM_MAX && g_cm[k].used; k++) { } if (k == CM_MAX) return 0; u64 P = PSZ_OF(c); u64 a = w32_heap_alloc(w, 2 * P, 1); if (!a) return 0;
  CtxMenu *m = &g_cm[k]; e_memset(m, 0, sizeof *m); m->used = 1; m->w = w; m->addr = a; m->refs = 1; m->bg = bg; m->all_dirs = 1; e_snprintf(m->folder, sizeof m->folder, "%s", folder); m->paths = emu_alloc(sizeof(char[520]) * CM_ITEMS); if (!m->paths) { m->used = 0; return 0; }
  wrp(c, a, cm_vtable(c)); wr32(c, a + P, 1); wr32(c, a + P + 4, (u32)k); return m; }
static void cm_add_path(CtxMenu *m, const char *path) { if (m->n >= CM_ITEMS) return; e_snprintf(m->paths[m->n], 520, "%s", path); u64 sz; if (vfs_stat(path, &sz) != 2) m->all_dirs = 0; m->n++; }
/* merge the verbs into guest menu m at position pos with ids first + k; returns the number of ids used */
static u32 cm_fill(Cpu *c, CtxMenu *cm, int mi, int pos, u32 first, u32 flags) { Menu *mm = &g_menu[mi]; int order[16]; int no = 0; int defonly = (flags & 1) != 0; cm->first = first; cm->nmap = 0; (void)c;
  int drives = cm->n > 0; for (int k = 0; k < cm->n; k++) { size_t l = e_strlen(cm->paths[k]); if (!(l <= 3 && cm->paths[k][1] == 0x3a)) drives = 0; }   /* drive roots: no Cut / Delete, like the Windows shell */
  if (cm->bg || cm->n == 0) { if (!defonly) { order[no++] = CMV_REFRESH; order[no++] = -1; if (g_clipf[0]) { order[no++] = CMV_PASTE; order[no++] = -1; } order[no++] = CMV_NEWFOLDER; order[no++] = -1; } order[no++] = CMV_PROPERTIES; }
  else { order[no++] = CMV_OPEN; if (!defonly) { if (cm->all_dirs) order[no++] = CMV_EXPLORE; order[no++] = -1; if (!drives) order[no++] = CMV_CUT; order[no++] = CMV_COPY; if (cm->all_dirs && cm->n == 1 && g_clipf[0]) order[no++] = CMV_PASTE; order[no++] = -1; if (!drives) { order[no++] = CMV_DELETE; order[no++] = -1; } order[no++] = CMV_PROPERTIES; } }
  u32 used = 0; for (int q = 0; q < no; q++) { int p = menu_insert_at(mi, pos < 0 ? -1 : pos + q); if (p < 0) break; MItem *it = &mm->it[p]; e_memset(it, 0, sizeof *it);
    if (order[q] < 0) { it->ftype = 0x800; continue; } int v = order[q]; it->id = first + (u32)cm->nmap; it->ftype = 0; it->state = (q == 0 && !cm->bg) ? 0x1000 /* MFS_DEFAULT */ : 0; e_snprintf(it->text, sizeof it->text, "%s", cm_verb_text[v]); cm->map[cm->nmap++] = v; used = (u32)cm->nmap; }
  menu_changed(W, mi); return used; }
static int cm_verb_of_id(CtxMenu *cm, u32 id) { if (id < cm->first || id >= cm->first + (u32)cm->nmap) return -1; return cm->map[id - cm->first]; }
static int cm_verb_of_name(const char *s) { for (int v = 0; v < CMV_COUNT; v++) if (!e_stricmp(s, cm_verb_name[v])) return v; if (!e_stricmp(s, "cut")) return CMV_CUT; return -1; }
static void cm_refresh_folder(Cpu *c, const char *folder) { for (int i = 0; i < SV_MAX; i++) if (g_sv[i].used && g_sv[i].w == W && g_sv[i].lv >= 0 && wnd_alive(g_sv[i].lv) && !e_stricmp(g_sv[i].path, folder)) sv_fill(c, &g_sv[i]); }
static void cm_copy_tree(const char *src, const char *dst, int depth) { u64 sz; int st = vfs_stat(src, &sz); if (st == 2) { vfs_mkdir(dst); if (depth > 16) return; char pat[1060]; cm_join(src, "*", pat, sizeof pat); int h = vfs_find_first(pat); char nm[260]; u64 s2; int isd;
    while (h >= 0 && vfs_find_next(h, nm, sizeof nm, &s2, &isd)) { if (!e_strcmp(nm, ".") || !e_strcmp(nm, "..")) continue; char a[1300], b[1300]; cm_join(src, nm, a, sizeof a); cm_join(dst, nm, b, sizeof b); cm_copy_tree(a, b, depth + 1); } if (h >= 0) vfs_find_close(h); return; }
  if (st == 1 || st == 3) { int node = vfs_find_node(src); const u8 *d = node >= 0 ? vfs_node_data(node) : 0; int fd = vfs_open(dst, 2, 2 /* CREATE_ALWAYS: a written node, synced back to a mounted host folder */, 0); if (fd >= 0) { if (d && sz) vfs_write(fd, d, sz); vfs_close(fd); } } }
static void cm_unique_dest(const char *folder, const char *name, char *out, size_t cap) { cm_join(folder, name, out, cap); u64 sz; if (!vfs_stat(out, &sz)) return; const char *dot = 0; for (const char *p = name; *p; p++) if (*p == '.') dot = p; char stem[260], ext[64]; if (dot && dot != name) { e_snprintf(stem, sizeof stem, "%.*s", (int)(dot - name), name); e_snprintf(ext, sizeof ext, "%s", dot); } else { e_snprintf(stem, sizeof stem, "%s", name); ext[0] = 0; }
  for (int k = 2; k < 1000; k++) { char nm[300]; if (k == 2) e_snprintf(nm, sizeof nm, "%s - Copy%s", stem, ext); else e_snprintf(nm, sizeof nm, "%s - Copy (%d)%s", stem, k, ext); cm_join(folder, nm, out, cap); if (!vfs_stat(out, &sz)) return; } }
static void cm_props_text(const char *path, int is_folder_bg, char *out, size_t cap) { u64 sz = 0; int st = vfs_stat(path, &sz); const char *nm = cm_base(path); char loc[1040]; e_snprintf(loc, sizeof loc, "%s", path); size_t ll = e_strlen(loc); while (ll > 0 && loc[ll - 1] != '\\') loc[--ll] = 0; if (ll > 3 && loc[ll - 1] == '\\') loc[ll - 1] = 0;
  if (st == 2 || is_folder_bg) { int files = 0, dirs = 0; u64 total = 0; char pat[1060]; cm_join(path, "*", pat, sizeof pat); int h = vfs_find_first(pat); char en[260]; u64 es; int isd; while (h >= 0 && vfs_find_next(h, en, sizeof en, &es, &isd)) { if (!e_strcmp(en, ".") || !e_strcmp(en, "..")) continue; if (isd) dirs++; else { files++; total += es; } } if (h >= 0) vfs_find_close(h);
    e_snprintf(out, cap, "%s\n\nType:\tFile folder\nLocation:\t%s\nSize:\t%llu KB (%llu bytes)\nContains:\t%d Files, %d Folders", path[0] ? nm : "Desktop", loc[0] ? loc : "Desktop", (unsigned long long)((total + 1023) / 1024), (unsigned long long)total, files, dirs); return; }
  char tb[64]; e_snprintf(out, cap, "%s\n\nType of file:\t%s\nLocation:\t%s\nSize:\t%llu KB (%llu bytes)", nm, shell_type_name(path, 0, 0, tb, sizeof tb), loc, (unsigned long long)((sz + 1023) / 1024), (unsigned long long)sz); }
/* run a verb; owner = window for message boxes; returns 1 when the call RETs itself (modal / ShellExecute), else 0 */
static int cm_invoke(Cpu *c, CtxMenu *cm, int verb, u64 owner_h) {
  switch (verb) {
    case CMV_OPEN: case CMV_EXPLORE: { if (!cm->n) { cm_refresh_folder(c, cm->folder); return 0; } const char *p = cm->paths[0]; u64 sz; if (vfs_stat(p, &sz) == 2) { /* a folder: navigate the view that shows its parent (what Explorer does in-place) */ for (int i = 0; i < SV_MAX; i++) if (g_sv[i].used && g_sv[i].w == W && g_sv[i].lv >= 0 && wnd_alive(g_sv[i].lv) && !e_stricmp(g_sv[i].path, cm->folder)) { sv_navigate(c, &g_sv[i], p, 0x1000); return 1; } w32_report(W, "Open folder %s: no folder view to navigate", p); return 0; }
      w32_shell_open(c, p); return 1; }
    case CMV_CUT: case CMV_COPY: { g_clipf[0] = 0; g_clip[0] = 0; size_t o = 0; for (int k = 0; k < cm->n; k++) { size_t l = e_strlen(cm->paths[k]); if (o + l + 2 >= sizeof g_clipf) break; if (o) g_clipf[o++] = '\n'; e_memcpy(g_clipf + o, cm->paths[k], l); o += l; g_clipf[o] = 0; } e_snprintf(g_clip, sizeof g_clip, "%s", g_clipf); for (char *q = g_clip; *q; q++) if (*q == '\n') *q = 0x20; g_clipf_cut = verb == CMV_CUT; return 0; }
    case CMV_PASTE: { const char *dst_folder = cm->n && cm->all_dirs ? cm->paths[0] : cm->folder; if (!dst_folder[0]) return 0; char list[8192]; e_snprintf(list, sizeof list, "%s", g_clipf); char *s = list; while (*s) { char *e = s; while (*e && *e != '\n') e++; char save = *e; *e = 0; if (*s) { char dst[1300]; const char *nm = cm_base(s); char srcdir[1040]; e_snprintf(srcdir, sizeof srcdir, "%.*s", (int)(nm - s), s); size_t sl = e_strlen(srcdir); if (sl > 3 && srcdir[sl - 1] == '\\') srcdir[sl - 1] = 0;
          int same = !e_stricmp(srcdir, dst_folder); if (g_clipf_cut && same) { } else { if (same) cm_unique_dest(dst_folder, nm, dst, sizeof dst); else cm_join(dst_folder, nm, dst, sizeof dst); cm_copy_tree(s, dst, 0); if (g_clipf_cut) vfs_remove(s); } }
        if (!save) break; s = e + 1; }
      if (g_clipf_cut) { g_clipf[0] = 0; g_clipf_cut = 0; } cm_refresh_folder(c, dst_folder); cm_refresh_folder(c, cm->folder); return 0; }
    case CMV_DELETE: { for (int k = 0; k < cm->n; k++) { vfs_remove(cm->paths[k]); w32_report(W, "Deleted %s", cm->paths[k]); } cm_refresh_folder(c, cm->folder); return 0; }
    case CMV_REFRESH: cm_refresh_folder(c, cm->folder); return 0;
    case CMV_NEWFOLDER: { char dst[1300]; cm_join(cm->folder, "New folder", dst, sizeof dst); u64 sz; for (int k = 2; vfs_stat(dst, &sz) && k < 1000; k++) { char nm[64]; e_snprintf(nm, sizeof nm, "New folder (%d)", k); cm_join(cm->folder, nm, dst, sizeof dst); } vfs_mkdir(dst); cm_refresh_folder(c, cm->folder); return 0; }
    case CMV_PROPERTIES: { char text[2048], cap[300]; const char *p = cm->n ? cm->paths[0] : cm->folder; cm_props_text(p, cm->n == 0, text, sizeof text); if (cm->n > 1) { char more[64]; e_snprintf(more, sizeof more, "\n\n(%d items selected)", cm->n); size_t tl = e_strlen(text); e_snprintf(text + tl, sizeof text - tl, "%s", more); } e_snprintf(cap, sizeof cap, "%s Properties", p[0] ? cm_base(p) : "Desktop"); message_box(c, owner_h, text, cap, 0x40); return 1; }
    default: return 0; } }
/* ---- COM ---- */
static void s_IContextMenu_QueryInterface(Cpu *c) { CtxMenu *m = cm_of(c, ARG(0)); u64 out = ARG(2); if (!m || !out) { if (out) wrp(c, out, 0); RET(0x80070057u); return; } const u8 *g = mem_ptr(c->mem, ARG(1), 16, 0);
  static const u8 iid_unk[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46 }, iid_cm[16] = { 0xE4, 0x14, 2, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46 }, iid_cm2[16] = { 0xF4, 0x14, 2, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46 }, iid_cm3[16] = { 0xA0, 0xE0, 0xFC, 0xBC, 0x17, 0xEC, 0xD0, 0x11, 0x8D, 0x10, 0x00, 0xA0, 0xC9, 0x0F, 0x27, 0x19 };
  if (g && (!e_memcmp(g, iid_unk, 16) || !e_memcmp(g, iid_cm, 16) || !e_memcmp(g, iid_cm2, 16) || !e_memcmp(g, iid_cm3, 16))) { m->refs++; wrp(c, out, m->addr); RET(0); return; }
  wrp(c, out, 0); RET(0x80004002u); }
static void s_IContextMenu_AddRef(Cpu *c) { CtxMenu *m = cm_of(c, ARG(0)); if (!m) { RET(1); return; } m->refs++; RET((u32)m->refs); }
static void s_IContextMenu_Release(Cpu *c) { CtxMenu *m = cm_of(c, ARG(0)); if (!m) { RET(0); return; } if (--m->refs > 0) { RET((u32)m->refs); return; } cm_free(m); RET(0); }
static void s_IContextMenu_QueryContextMenu(Cpu *c) { /* (hmenu, indexMenu, idCmdFirst, idCmdLast, uFlags) */ CtxMenu *m = cm_of(c, ARG(0)); int mi = menu_of(ARG(1)); if (!m || mi < 0) { RET(0x80070057u); return; } u32 used = cm_fill(c, m, mi, (int)(u32)ARG(2), (u32)ARG(3), (u32)ARG(5)); RET(used & 0xFFFF); }
static void s_IContextMenu_InvokeCommand(Cpu *c) { /* CMINVOKECOMMANDINFO(EX): cbSize fMask hwnd lpVerb lpParameters lpDirectory nShow dwHotKey hIcon [lpTitle lpVerbW ...] */ if (modal_reentry(c)) { modal_loop(c, g_modal[g_nmodal - 1].wnd); return; }
  CtxMenu *m = cm_of(c, ARG(0)); u64 p = ARG(1); if (!m || !p) { RET(0x80070057u); return; } u64 P = PSZ_OF(c); u32 cb = rd32(c, p), mask = rd32(c, p + 4); u64 hwnd = rdp(c, p + 8), verb_a = rdp(c, p + 8 + P); int verb = -1;   /* cbSize fMask hwnd lpVerb: hwnd at 8 on both bitnesses */
  if (verb_a < 0x10000) verb = cm_verb_of_id(m, m->first + (u32)verb_a); else { char vs[64]; if ((mask & 0x4000 /* CMIC_MASK_UNICODE */) && cb >= (u32)(c->mode32 ? 44 : 72)) { u64 vw = rdp(c, p + (c->mode32 ? 40 : 64)); if (vw >= 0x10000) text_in(c, vw, 1, vs, sizeof vs); else text_in(c, verb_a, 0, vs, sizeof vs); } else text_in(c, verb_a, 0, vs, sizeof vs); verb = cm_verb_of_name(vs); }
  if (verb < 0) { RET(0x80070057u); return; } if (cm_invoke(c, m, verb, hwnd)) { /* modal / ShellExecute RET their own value: InvokeCommand must answer S_OK — a message box result or ShellExecute's 42 is not an error */ if (!modal_reentry(c) && g_nmodal == 0) RET(0); return; } RET(0); }
static void s_IContextMenu_GetCommandString(Cpu *c) { /* (idCmd, uType, pReserved, pszName, cchMax) */ CtxMenu *m = cm_of(c, ARG(0)); if (!m) { RET(0x80070057u); return; } u32 type = (u32)ARG(2); int verb = cm_verb_of_id(m, m->first + (u32)ARG(1)); if (verb < 0 && ARG(1) < CMV_COUNT) verb = m->nmap > (int)ARG(1) ? m->map[ARG(1)] : -1; if (verb < 0) { RET(0x80070057u); return; }
  u64 out = ARG(4); u32 cch = (u32)ARG(5); int wide = (type & 4) != 0; switch (type & 3) { case 0: text_out_cap(c, out, cm_verb_name[verb], cch, wide); RET(0); return; case 1: text_out_cap(c, out, cm_verb_help[verb], cch, wide); RET(0); return; case 2: RET(0); return; default: RET(0x80004001u); return; } }
static void s_IContextMenu_HandleMenuMsg(Cpu *c) { RET(0); }
static void s_IContextMenu_HandleMenuMsg2(Cpu *c) { if (ARG(3)) wrp(c, ARG(3), 0); RET(0); }
/* IShellFolder::GetUIObjectOf(IID_IContextMenu*) — called from win32_shellns.inc with the folder path and the item paths */
u64 gdi_shell_ctxmenu_create(Cpu *c, const char *folder, const char (*paths)[520], int n) { CtxMenu *m = cm_create(c, folder, n == 0); if (!m) return 0; for (int k = 0; k < n; k++) cm_add_path(m, paths[k]); return m->addr; }
/* ---- the default folder view's own right-click menu ---- */
static void sv_context_menu(Cpu *c, ShellView *v, int item) { Win32 *w = W; ListView *lv = v->lv >= 0 ? lv_of(v->lv) : 0; if (!lv) { RET(0); return; }
  if (v->ctx_cm >= 0 && v->ctx_cm < CM_MAX && g_cm[v->ctx_cm].used) cm_free(&g_cm[v->ctx_cm]); v->ctx_cm = -1; if (v->ctx_menu > 0 && g_menu[v->ctx_menu].used) menu_destroy(v->ctx_menu, 0); v->ctx_menu = 0;
  CtxMenu *cm = cm_create(c, v->path, item < 0); if (!cm) { RET(0); return; }
  if (item >= 0) { for (int k = 0; k < lv->n; k++) if ((lv->it[k].state & 2) || k == item) { char p[1300]; sv_item_path(v, k, p, sizeof p); if (p[0]) cm_add_path(cm, p); } if (!cm->n) { cm_free(cm); RET(0); return; } }
  int mi = menu_new(w); if (mi < 0) { cm_free(cm); RET(0); return; } cm_fill(c, cm, mi, -1, 0x7000, 0); v->ctx_cm = (int)(cm - g_cm); v->ctx_menu = mi;
  track_popup(c, menu_handle(mi), 0x2 /* TPM_RIGHTBUTTON */, g_mouse_x + SCREEN_X0, g_mouse_y + SCREEN_Y0, HWND_OF(v->wnd)); }
static int sv_context_command(Cpu *c, ShellView *v, u32 id) { if (v->ctx_cm < 0 || v->ctx_cm >= CM_MAX || !g_cm[v->ctx_cm].used) return 0; CtxMenu *cm = &g_cm[v->ctx_cm]; int verb = cm_verb_of_id(cm, id); if (verb < 0) return 0;
  if (v->ctx_menu > 0 && g_menu[v->ctx_menu].used) menu_destroy(v->ctx_menu, 0); v->ctx_menu = 0; int r = cm_invoke(c, cm, verb, HWND_OF(v->wnd)); if (!r) { cm_free(cm); v->ctx_cm = -1; RET(0); } return 1; }
#define CTXMENU_SHIMS WM(IContextMenu, QueryInterface), WM(IContextMenu, AddRef), WM(IContextMenu, Release), WM(IContextMenu, QueryContextMenu), WM(IContextMenu, InvokeCommand), WM(IContextMenu, GetCommandString), WM(IContextMenu, HandleMenuMsg), WM(IContextMenu, HandleMenuMsg2),
