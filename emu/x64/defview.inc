/* defview.inc — the shell's default folder view (SHELLDLL_DefView), i.e. what IShellFolder::CreateViewObject(IID_IShellView) returns and
   what every Explorer-style file manager (Q-Dir, FreeCommander, the common dialogs) embeds: a child window owning a SysListView32 filled
   with the folder's items (icons from the system image lists, Name / Size / Type / Date columns), IShellView + IShellView2 + IFolderView
   on one object, IShellBrowser callbacks for navigation (BrowseObject on folder activation, GetWindow for the parent) and ShellExecute
   for files.  Lives in gdi.c after listview.inc and shellicon.inc. */
#define SV_MAX 32
typedef struct { int used; Win32 *w; u64 addr, folder, browser; char path[1040]; int wnd, lv; u32 viewmode, flags; int refs; u64 scratch; int wait_browse; int ctx_cm, ctx_menu; } ShellView;
static ShellView g_sv[SV_MAX];
static void sv_context_menu(Cpu *c, ShellView *v, int item); static int sv_context_command(Cpu *c, ShellView *v, u32 id);   /* ctxmenu.inc */
static const char *const vt_IShellView2[] = { "QueryInterface", "AddRef", "Release", "GetWindow", "ContextSensitiveHelp", "TranslateAccelerator", "EnableModeless", "UIActivate", "Refresh", "CreateViewWindow", "DestroyViewWindow", "GetCurrentInfo", "AddPropertySheetPages", "SaveViewState", "SelectItem", "GetItemObject", "GetView", "CreateViewWindow2", "HandleRename", "SelectAndPositionItem" };
static const char *const vt_IFolderView[] = { "QueryInterface", "AddRef", "Release", "GetCurrentViewMode", "SetCurrentViewMode", "GetFolder", "Item", "ItemCount", "Items", "GetSelectionMarkedItem", "GetFocusedItem", "GetItemPosition", "GetSpacing", "GetDefaultSpacing", "GetAutoArrange", "SelectItem", "SelectAndPositionItems" };
static u64 g_sv_vt[2]; static Win32 *g_sv_vt_owner;
static u64 sv_vtable(Cpu *c, int which) { Win32 *w = W; if (g_sv_vt_owner != w) { g_sv_vt[0] = g_sv_vt[1] = 0; g_sv_vt_owner = w; } if (g_sv_vt[which]) return g_sv_vt[which]; u64 P = PSZ_OF(c);
  const char *const *names = which ? vt_IFolderView : vt_IShellView2; int n = which ? (int)(sizeof vt_IFolderView / sizeof vt_IFolderView[0]) : (int)(sizeof vt_IShellView2 / sizeof vt_IShellView2[0]); const char *iface = which ? "IFolderView" : "IShellView";
  u64 vt = w32_heap_alloc(w, (u64)n * P, 1); if (!vt) return 0; for (int i = 0; i < n; i++) { char nm[64]; e_snprintf(nm, sizeof nm, "%s::%s", iface, names[i]); wrp(c, vt + (u64)i * P, w32_shim_slot(w, "shell32", nm)); } g_sv_vt[which] = vt; return vt; }
static ShellView *sv_of(Cpu *c, u64 a) { u64 P = PSZ_OF(c); for (int i = 0; i < SV_MAX; i++) if (g_sv[i].used && g_sv[i].w == W && (g_sv[i].addr == a || g_sv[i].addr + P == a)) return &g_sv[i]; return 0; }
static ShellView *sv_of_wnd(int wi) { for (int i = 0; i < SV_MAX; i++) if (g_sv[i].used && g_sv[i].wnd == wi) return &g_sv[i]; return 0; }
/* IShellFolder::CreateViewObject(IID_IShellView): one object = [vt IShellView2][vt IFolderView][refs][id] */
u64 gdi_shell_view_create(Cpu *c, u64 folder_obj, const char *path) { Win32 *w = W; int k; for (k = 0; k < SV_MAX && g_sv[k].used; k++) { } if (k == SV_MAX) return 0; u64 P = PSZ_OF(c); u64 a = w32_heap_alloc(w, 4 * P, 1); if (!a) return 0;
  ShellView *v = &g_sv[k]; e_memset(v, 0, sizeof *v); v->ctx_cm = -1; v->used = 1; v->w = w; v->addr = a; v->folder = folder_obj; e_snprintf(v->path, sizeof v->path, "%s", path); v->wnd = v->lv = -1; v->viewmode = 4; v->refs = 1; v->scratch = w32_heap_alloc(w, 64, 1);
  wrp(c, a, sv_vtable(c, 0)); wrp(c, a + P, sv_vtable(c, 1)); wr32(c, a + 2 * P, 1); wr32(c, a + 2 * P + 4, (u32)k); return a; }
/* ---- filling the list ---- */
typedef struct { char name[260]; u64 size; int is_dir; } SvEntry;
static int sv_cmp(const SvEntry *a, const SvEntry *b) { if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1; return e_stricmp(a->name, b->name); }
static void sv_size_text(u64 sz, char *out, size_t cap) { u64 kb = (sz + 1023) / 1024; if (sz == 0) kb = 0; e_snprintf(out, cap, "%llu KB", (unsigned long long)kb); }
static void sv_fill(Cpu *c, ShellView *v) { if (v->lv < 0 || !wnd_alive(v->lv)) return; ListView *lv = lv_of(v->lv); Wnd *n = &g_wnd[v->lv]; if (!lv) return;
  for (int k = 0; k < lv->n; k++) lv_free_item(&lv->it[k]); lv->n = 0; lv->focus = -1; lv->mark = -1; lv->top = 0;
  if (!lv->ncol) { static const struct { const char *t; int cx, fmt; } cols[4] = { { "Name", 230, 0 }, { "Size", 80, 1 }, { "Type", 130, 0 }, { "Date modified", 130, 0 } }; for (int q = 0; q < 4; q++) { e_snprintf(lv->col[q].text, sizeof lv->col[q].text, "%s", cols[q].t); lv->col[q].cx = cols[q].cx; lv->col[q].fmt = cols[q].fmt; lv->col[q].image = -1; } lv->ncol = 4; }
  SvEntry *ents = emu_alloc(sizeof(SvEntry) * 4096); int ne = 0;
  if (!v->path[0]) { e_snprintf(ents[0].name, sizeof ents[0].name, "C:\\"); ents[0].size = 0; ents[0].is_dir = 1; ne = 1; }
  else { char pat[1060]; e_snprintf(pat, sizeof pat, "%s%s*", v->path, v->path[e_strlen(v->path) - 1] == '\\' ? "" : "\\"); int h = vfs_find_first(pat); char nm[260]; u64 sz; int isd;
    while (h >= 0 && ne < 4096 && vfs_find_next(h, nm, sizeof nm, &sz, &isd)) { if (!e_strcmp(nm, ".") || !e_strcmp(nm, "..")) continue; SvEntry *e = &ents[ne++]; e_snprintf(e->name, sizeof e->name, "%s", nm); e->size = sz; e->is_dir = isd; }
    if (h >= 0) vfs_find_close(h); }
  for (int i = 1; i < ne; i++) { SvEntry t = ents[i]; int j = i - 1; while (j >= 0 && sv_cmp(&ents[j], &t) > 0) { ents[j + 1] = ents[j]; j--; } ents[j + 1] = t; }
  lv_ensure_cap(lv, ne); for (int i = 0; i < ne; i++) { SvEntry *e = &ents[i]; LvItem *t = &lv->it[i]; e_memset(t, 0, sizeof *t); char full[1300]; if (!v->path[0]) e_snprintf(full, sizeof full, "%s", e->name); else e_snprintf(full, sizeof full, "%s%s%s", v->path, v->path[e_strlen(v->path) - 1] == '\\' ? "" : "\\", e->name);
    char disp[300]; e_snprintf(disp, sizeof disp, "%s", e->name); size_t dl = e_strlen(disp); if (!v->path[0] && dl == 3) e_snprintf(disp, sizeof disp, "Local Disk (%c:)", e->name[0]); else if (!e->is_dir) { const char *ext = path_ext(disp); if (ext[0] && ext_in(ext, "exe lnk dll txt ini bat cmd zip png jpg jpeg gif bmp mp3 mp4 pdf doc docx html htm url")) disp[dl - e_strlen(ext) - 1] = 0; }
    lv_set_sub(t, 0, disp); if (!e->is_dir) { char st[32]; sv_size_text(e->size, st, sizeof st); lv_set_sub(t, 1, st); } char tb[64]; lv_set_sub(t, 2, shell_type_name(full, e->is_dir, !v->path[0], tb, sizeof tb)); lv_set_sub(t, 3, "");
    t->image = shell_icon_index(c, full, e->is_dir ? 0x10 : 0x80, 0, 0); t->lparam = (u64)i; }
  lv->n = ne; emu_free(ents); (void)n; lv_invalidate_cache(lv); wnd_invalidate(v->lv, 1); }
static void sv_item_path(ShellView *v, int k, char *out, size_t cap) { ListView *lv = v->lv >= 0 ? lv_of(v->lv) : 0; out[0] = 0; if (!lv || k < 0 || k >= lv->n) return;
  /* the display name may have lost its extension: re-enumerate to find the k-th sorted entry */ const char *disp = lv_sub(lv, k, 0); if (!v->path[0]) { e_snprintf(out, cap, "C:\\"); return; }
  char pat[1060]; e_snprintf(pat, sizeof pat, "%s%s*", v->path, v->path[e_strlen(v->path) - 1] == '\\' ? "" : "\\"); int h = vfs_find_first(pat); char nm[260]; u64 sz; int isd; const char *type = lv_sub(lv, k, 2); int want_dir = !e_strcmp(type, "File folder");
  while (h >= 0 && vfs_find_next(h, nm, sizeof nm, &sz, &isd)) { if (isd != want_dir) continue; size_t dl = e_strlen(disp); int more_dots = 0; for (const char *q = nm + dl + 1; nm[dl] == '.' && *q; q++) if (*q == '.') more_dots = 1; if (!e_strnicmp(nm, disp, dl) && (nm[dl] == 0 || (nm[dl] == '.' && !more_dots))) { e_snprintf(out, cap, "%s%s%s", v->path, v->path[e_strlen(v->path) - 1] == '\\' ? "" : "\\", nm); break; } }
  if (h >= 0) vfs_find_close(h); }
static int sv_lv_style(u32 viewmode) { switch (viewmode) { case 1: case 5: case 6: case 7: case 8: return 0; case 2: return 2; case 3: return 3; default: return 1; } }
/* ---- the view window (class SHELLDLL_DefView, builtin BI_DEFVIEW) ---- */
static void sv_create_list(Cpu *c, ShellView *v) { Wnd *n = &g_wnd[v->wnd]; u32 st = 0x50010000u | 0x04000000u | 0x40 /* LVS_SHAREIMAGELISTS */ | 8 /* SHOWSELALWAYS */ | (u32)sv_lv_style(v->viewmode); int li = wnd_create_obj(c, "SysListView32", "", st, 0, 0, 0, wnd_client_w(v->wnd), wnd_client_h(v->wnd), HWND_OF(v->wnd), 1, n->inst, 0, 1, 0); if (li < 0) return;
  v->lv = li; call_shim(c, s_NebulaListViewProc, HWND_OF(li), 0x01, 0, 0); ListView *lv = lv_of(li); if (lv) { lv->himl_small = sysiml_get(c, 1); lv->himl = sysiml_get(c, 0); lv->exstyle |= 0x20 | 0x10000; } g_wnd[li].font = n->font ? n->font : g_dlg_font; sv_fill(c, v); wnd_show_now(c, li); }
static void sv_navigate(Cpu *c, ShellView *v, const char *rel, u32 flags) { /* IShellBrowser::BrowseObject(child pidl, SBSP_RELATIVE) on item activation / (NULL, SBSP_PARENT) on Backspace, exactly like the Windows DefView; the browser (Q-Dir, a file-open dialog) then ILCombines the pidl with its current folder and creates a view for it */ u64 P = PSZ_OF(c); if (!v->browser) { RET(0); return; } u64 pidl = rel ? pidl_make(W, rel) : 0; u64 fn = rdp(c, rdp(c, v->browser) + 11 * P); if (!fn) { RET(0); return; } w32_push_cont(c, CONT_SVBROWSE, 0); w32_guest_call(c, fn, v->browser, pidl, flags, 0); }
static void svbrowse_resume(Cpu *c, u64 rax) { w32_cont_finish(c, rax); }
static void s_NebulaDefViewProc(Cpu *c) { int i = wnd_of(ARG(0)); if (i < 0) { RET(0); return; } u32 msg = (u32)ARG(1); u64 wp = ARG(2), lp = ARG(3); ShellView *v = sv_of_wnd(i); u64 P = PSZ_OF(c);
  if (modal_reentry(c)) { /* a popup menu / message box started by this window procedure re-executes the call until it ends */ int m = g_nmodal - 1; if (g_modal[m].wnd < 0) track_popup(c, 0, 0, 0, 0, 0); else modal_loop(c, g_modal[m].wnd); return; }
  switch (msg) {
    case 0x111: { if (v && sv_context_command(c, v, (u32)(wp & 0xFFFF))) return; RET(0); return; }
    case 0x01: { if (v) sv_create_list(c, v); RET(0); return; }
    case 0x02: { if (v) { v->wnd = -1; v->lv = -1; } RET(0); return; }
    case 0x05: { if (v && v->lv >= 0 && wnd_alive(v->lv)) { Wnd *l = &g_wnd[v->lv]; l->x = 0; l->y = 0; l->w = wnd_client_w(i); l->h = wnd_client_h(i); wnd_invalidate(v->lv, 1); } RET(0); return; }
    case 0x07: { if (v && v->lv >= 0 && wnd_alive(v->lv)) { int nc0 = W->ncont; set_focus(c, v->lv); if (W->ncont > nc0 || c->status != RUN_OK) return; } RET(0); return; }
    case 0x14: { RET(1); return; }
    case 0x0F: { u64 hdc = ctl_paint_begin(c, i); if (hdc) { Surf s; if (dc_surf(c, hdc, &s)) fill_rect_px(&s, 0, 0, wnd_client_w(i), wnd_client_h(i), 0xffffff); ctl_paint_end(c, i, hdc); } RET(0); return; }
    case 0x4E: { if (!v || !lp) { RET(0); return; } u32 code = rd32(c, lp + 2 * P); int from = wnd_of(rdp(c, lp)); if (from != v->lv) { RET(0); return; }
      if (code == (u32)-114 /* LVN_ITEMACTIVATE (the list raises it for a double click and for Enter; NM_DBLCLK / NM_RETURN alone do not open) */) { ListView *lv = lv_of(v->lv); int k = (int)(s32)rd32(c, lp + 3 * P); if (k < 0 && lv) k = lv->focus; char path[1300]; sv_item_path(v, k, path, sizeof path); if (!path[0]) { RET(0); return; }
        u64 sz; int st = vfs_stat(path, &sz); if (st == 2) { size_t pl = e_strlen(path); while (pl > 3 && path[pl - 1] == 0x5c) path[--pl] = 0; const char *base = path; if (!(pl <= 3 && path[1] == 0x3a)) for (const char *q = path; *q; q++) if (*q == 0x5c) base = q + 1; /* a drive root stays "C:\" (child of the desktop), anything else is its last component */ sv_navigate(c, v, base, 0x1000 /* SBSP_RELATIVE */); return; } w32_shell_open(c, path); RET(0); return; }
      if (code == (u32)-5 /* NM_RCLICK: the shell context menu of the clicked (now selected) item, or of the folder background */) { int k = (int)(s32)rd32(c, lp + 3 * P); sv_context_menu(c, v, k); return; }
      if (code == (u32)-155 /* LVN_KEYDOWN */) { u32 vk = rd16(c, lp + 3 * P); if (vk == 8 /* Backspace: parent folder */ && v->path[0]) { sv_navigate(c, v, 0, 0x2000 /* SBSP_PARENT */); return; } RET(0); return; }
      RET(0); return; }
    default: def_window_proc(c, i, msg, wp, lp); return; } }
/* ---- IShellView ---- */
static void s_IShellView_QueryInterface(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); u64 out = ARG(2); if (!v || !out) { if (out) wrp(c, out, 0); RET(0x80070057u); return; } const u8 *g = mem_ptr(c->mem, ARG(1), 16, 0); u64 P = PSZ_OF(c);
  static const u8 iid_unk[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46 }, iid_olew[16] = { 0x14, 1, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46 }, iid_sv[16] = { 0xE3, 0x14, 2, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46 },
    iid_sv2[16] = { 0x80, 0x9E, 0xE3, 0x88, 0x78, 0x35, 0xCF, 0x11, 0xAE, 0x69, 0x08, 0x00, 0x2B, 0x2E, 0x12, 0x62 }, iid_fv[16] = { 0xB0, 0x25, 0xE7, 0xCD, 0xC9, 0xCC, 0x19, 0x45, 0x91, 0x7E, 0x32, 0x5D, 0x72, 0xFA, 0xB4, 0xCE };
  if (g && (!e_memcmp(g, iid_unk, 16) || !e_memcmp(g, iid_olew, 16) || !e_memcmp(g, iid_sv, 16) || !e_memcmp(g, iid_sv2, 16))) { v->refs++; wrp(c, out, v->addr); RET(0); return; }
  if (g && !e_memcmp(g, iid_fv, 16)) { v->refs++; wrp(c, out, v->addr + P); RET(0); return; }
  if (g) e_log("[shell] IShellView::QueryInterface({%08x-%04x-%04x-…}) not supported\n", (u32)(g[0] | g[1] << 8 | g[2] << 16 | g[3] << 24), (u32)(g[4] | g[5] << 8), (u32)(g[6] | g[7] << 8)); wrp(c, out, 0); RET(0x80004002u); }
static void s_IShellView_AddRef(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v) { RET(1); return; } v->refs++; RET((u32)v->refs); }
static void s_IShellView_Release(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v) { RET(0); return; } if (--v->refs > 0) { RET((u32)v->refs); return; } v->used = 0; if (v->wnd >= 0 && wnd_alive(v->wnd)) { int wi = v->wnd; v->wnd = -1; v->lv = -1; int nc0 = W->ncont; destroy_window(c, wi); if (W->ncont > nc0 || c->status != RUN_OK) return; } RET(0); }
static void s_IShellView_GetWindow(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v || !ARG(1)) { RET(0x80070057u); return; } wrp(c, ARG(1), v->wnd >= 0 && wnd_alive(v->wnd) ? HWND_OF(v->wnd) : 0); RET(v->wnd >= 0 ? 0 : 0x80004005u); }
static void s_IShellView_ContextSensitiveHelp(Cpu *c) { RET(0x80004001u); }
static void s_IShellView_TranslateAccelerator(Cpu *c) { RET(1); }
static void s_IShellView_EnableModeless(Cpu *c) { RET(0); }
static void s_IShellView_UIActivate(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); u32 state = (u32)ARG(1); if (!v) { RET(0x80070057u); return; } if (state == 2 /* SVUIA_ACTIVATE_FOCUS */ && v->lv >= 0 && wnd_alive(v->lv)) { int nc0 = W->ncont; set_focus(c, v->lv); if (W->ncont > nc0 || c->status != RUN_OK) return; } RET(0); }
static void s_IShellView_Refresh(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v) { RET(0x80070057u); return; } sv_fill(c, v); RET(0); }
/* CreateViewWindow(prev, FOLDERSETTINGS*, IShellBrowser*, RECT*, HWND*): the parent is IShellBrowser::GetWindow (a guest call) */
static void sv_create_view(Cpu *c, ShellView *v, u64 fs, u64 browser, u64 rc, u64 out) { u64 P = PSZ_OF(c); if (fs) { v->viewmode = rd32(c, fs); v->flags = rd32(c, fs + 4); } v->browser = browser; if (out) wrp(c, out, 0);
  if (!browser) { RET(0x80070057u); return; } u64 fn = rdp(c, rdp(c, browser) + 3 * P); if (!fn) { RET(0x80004005u); return; } wrp(c, v->scratch, 0); wr64(c, v->scratch + 8, rc); wr64(c, v->scratch + 16, out); w32_push_cont(c, CONT_SVGETWND, (u64)(v - g_sv)); w32_guest_call(c, fn, browser, v->scratch, 0, 0); }
static void svgetwnd_resume(Cpu *c, u64 rax) { Win32 *w = W; int k = (int)w->cont[w->ncont - 1].data[0]; ShellView *v = &g_sv[k]; u64 parent = rdp(c, v->scratch); u64 rc = rd64(c, v->scratch + 8), out = rd64(c, v->scratch + 16); int pi = wnd_of(parent); if ((u32)rax || pi < 0) { w32_cont_finish(c, 0x80004005u); return; }
  int x = 0, y = 0, cx = wnd_client_w(pi), cy = wnd_client_h(pi); if (rc) { x = (s32)rd32(c, rc); y = (s32)rd32(c, rc + 4); cx = (s32)rd32(c, rc + 8) - x; cy = (s32)rd32(c, rc + 12) - y; } if (cx < 0) cx = 0; if (cy < 0) cy = 0;
    int wi = wnd_create_obj(c, "SHELLDLL_DefView", "FolderView", 0x50000000u | 0x02000000u | 0x04000000u | 0x10000, 0, x, y, cx, cy, parent, 0, g_wnd[pi].inst, 0, 1, 0);
  /* the view record must be known before WM_CREATE creates the list: wnd_create_obj sends WM_CREATE synchronously, so bind by name afterwards and fill now */
  if (wi < 0) { w32_cont_finish(c, 0x8007000Eu); return; } v->wnd = wi; call_shim(c, s_NebulaDefViewProc, HWND_OF(wi), 0x01, 0, 0); if (v->lv < 0) sv_create_list(c, v); wnd_show_now(c, wi); if (out) wrp(c, out, HWND_OF(wi)); w32_cont_finish(c, 0); }
static void s_IShellView_CreateViewWindow(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v) { RET(0x80070057u); return; } sv_create_view(c, v, ARG(2), ARG(3), ARG(4), ARG(5)); }
static void s_IShellView_DestroyViewWindow(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v) { RET(0x80070057u); return; } if (v->wnd >= 0 && wnd_alive(v->wnd)) { int wi = v->wnd; v->wnd = -1; v->lv = -1; int nc0 = W->ncont; destroy_window(c, wi); if (W->ncont > nc0 || c->status != RUN_OK) return; } RET(0); }
static void s_IShellView_GetCurrentInfo(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v || !ARG(1)) { RET(0x80070057u); return; } wr32(c, ARG(1), v->viewmode); wr32(c, ARG(1) + 4, v->flags); RET(0); }
static void s_IShellView_AddPropertySheetPages(Cpu *c) { RET(0); }
static void s_IShellView_SaveViewState(Cpu *c) { RET(0); }
static int sv_find_item(Cpu *c, ShellView *v, const char *path) { ListView *lv = v->lv >= 0 ? lv_of(v->lv) : 0; if (!lv) return -1; const char *b = path; for (const char *p = path; *p; p++) if (*p == '\\') b = p + 1; for (int k = 0; k < lv->n; k++) { const char *d = lv_sub(lv, k, 0); size_t dl = e_strlen(d); if (!e_strnicmp(d, b, dl) && (b[dl] == 0 || b[dl] == '.')) return k; } return -1; }
static void sv_select(Cpu *c, ShellView *v, int k, u32 flags) { ListView *lv = v->lv >= 0 ? lv_of(v->lv) : 0; Wnd *n = v->lv >= 0 ? &g_wnd[v->lv] : 0; if (!lv || !n) return; if (flags == 0 /* SVSI_DESELECT */) { if (k >= 0 && k < lv->n) lv->it[k].state &= ~2u; } else { if (flags & 4 /* DESELECTOTHERS */) lv_select_only(n, lv, k); if (k >= 0 && k < lv->n) { lv->it[k].state |= 2; if (flags & 0x10 /* FOCUSED */ || flags & 1) lv->focus = k; } } wnd_invalidate(v->lv, 1); }
static void s_IShellView_SelectItem(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v) { RET(0x80070057u); return; } u32 flags = (u32)ARG(2); if (!ARG(1)) { ListView *lv = v->lv >= 0 ? lv_of(v->lv) : 0; if (lv) { for (int k = 0; k < lv->n; k++) lv->it[k].state &= ~2u; wnd_invalidate(v->lv, 1); } RET(0); return; } int k = sv_find_item(c, v, pidl_path(c, ARG(1))); if (k < 0) { RET(0x80004005u); return; } sv_select(c, v, k, flags); RET(0); }
static void s_IShellView_GetItemObject(Cpu *c) { if (ARG(3)) wrp(c, ARG(3), 0); RET(0x80004002u); }
static void s_IShellView_GetView(Cpu *c) { RET(0x80004001u); }
static void s_IShellView_CreateViewWindow2(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); u64 p = ARG(1), P = PSZ_OF(c); if (!v || !p) { RET(0x80070057u); return; } sv_create_view(c, v, rdp(c, p + 2 * P), rdp(c, p + 3 * P), rdp(c, p + 4 * P), p + 6 * P); }
static void s_IShellView_HandleRename(Cpu *c) { RET(0x80004001u); }
static void s_IShellView_SelectAndPositionItem(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v) { RET(0x80070057u); return; } if (ARG(1)) { int k = sv_find_item(c, v, pidl_path(c, ARG(1))); if (k >= 0) sv_select(c, v, k, (u32)ARG(2)); } RET(0); }
/* ---- IFolderView (this == object + P) ---- */
static void s_IFolderView_QueryInterface(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v) { if (ARG(2)) wrp(c, ARG(2), 0); RET(0x80070057u); return; } call_shim(c, s_IShellView_QueryInterface, v->addr, ARG(1), ARG(2), 0); }
static void s_IFolderView_AddRef(Cpu *c) { s_IShellView_AddRef(c); }
static void s_IFolderView_Release(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v) { RET(0); return; } call_shim(c, s_IShellView_Release, v->addr, 0, 0, 0); }
static void s_IFolderView_GetCurrentViewMode(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v || !ARG(1)) { RET(0x80070057u); return; } wr32(c, ARG(1), v->viewmode); RET(0); }
static void s_IFolderView_SetCurrentViewMode(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v) { RET(0x80070057u); return; } v->viewmode = (u32)ARG(1); if (v->lv >= 0 && wnd_alive(v->lv)) { Wnd *l = &g_wnd[v->lv]; l->style = (l->style & ~3u) | (u32)sv_lv_style(v->viewmode); wnd_invalidate(v->lv, 1); } RET(0); }
static void s_IFolderView_GetFolder(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v || !ARG(2)) { RET(0x80070057u); return; } if (!v->folder) { wrp(c, ARG(2), 0); RET(0x80004002u); return; } /* IShellFolder::QueryInterface(riid) on the folder object */ u64 P = PSZ_OF(c); u64 fn = rdp(c, rdp(c, v->folder) + 0 * P); if (!fn) { RET(0x80004005u); return; } w32_push_cont(c, CONT_SVBROWSE, 0); w32_guest_call(c, fn, v->folder, ARG(1), ARG(2), 0); }
static void s_IFolderView_Item(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v || !ARG(2)) { RET(0x80070057u); return; } char path[1300]; sv_item_path(v, (int)(s32)ARG(1), path, sizeof path); if (!path[0]) { wrp(c, ARG(2), 0); RET(0x80070057u); return; } wrp(c, ARG(2), pidl_make(W, path)); RET(0); }
static void s_IFolderView_ItemCount(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v || !ARG(2)) { RET(0x80070057u); return; } u32 flags = (u32)ARG(1); ListView *lv = v->lv >= 0 ? lv_of(v->lv) : 0; int n = 0; if (lv) { if (flags & 1 /* SVGIO_SELECTION */) { for (int k = 0; k < lv->n; k++) if (lv->it[k].state & 2) n++; } else n = lv->n; } wr32(c, ARG(2), (u32)n); RET(0); }
static void s_IFolderView_Items(Cpu *c) { if (ARG(3)) wrp(c, ARG(3), 0); RET(0x80004002u); }
static void s_IFolderView_GetSelectionMarkedItem(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v || !ARG(1)) { RET(0x80070057u); return; } ListView *lv = v->lv >= 0 ? lv_of(v->lv) : 0; wr32(c, ARG(1), (u32)(lv ? lv->mark : -1)); RET(0); }
static void s_IFolderView_GetFocusedItem(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v || !ARG(1)) { RET(0x80070057u); return; } ListView *lv = v->lv >= 0 ? lv_of(v->lv) : 0; wr32(c, ARG(1), (u32)(lv ? lv->focus : -1)); RET(0); }
static void s_IFolderView_GetItemPosition(Cpu *c) { RET(0x80004001u); }
static void s_IFolderView_GetSpacing(Cpu *c) { if (ARG(1)) { wr32(c, ARG(1), 75); wr32(c, ARG(1) + 4, 75); } RET(0); }
static void s_IFolderView_GetDefaultSpacing(Cpu *c) { if (ARG(1)) { wr32(c, ARG(1), 75); wr32(c, ARG(1) + 4, 75); } RET(0); }
static void s_IFolderView_GetAutoArrange(Cpu *c) { RET(0); }
static void s_IFolderView_SelectItem(Cpu *c) { ShellView *v = sv_of(c, ARG(0)); if (!v) { RET(0x80070057u); return; } sv_select(c, v, (int)(s32)ARG(1), (u32)ARG(2)); RET(0); }
static void s_IFolderView_SelectAndPositionItems(Cpu *c) { RET(0x80004001u); }
#define DEFVIEW_SHIMS CTXMENU_SHIMS S(NebulaDefViewProc), WM(IShellView, QueryInterface), WM(IShellView, AddRef), WM(IShellView, Release), WM(IShellView, GetWindow), WM(IShellView, ContextSensitiveHelp), WM(IShellView, TranslateAccelerator), WM(IShellView, EnableModeless), WM(IShellView, UIActivate), WM(IShellView, Refresh), WM(IShellView, CreateViewWindow), WM(IShellView, DestroyViewWindow), WM(IShellView, GetCurrentInfo), WM(IShellView, AddPropertySheetPages), WM(IShellView, SaveViewState), WM(IShellView, SelectItem), WM(IShellView, GetItemObject), WM(IShellView, GetView), WM(IShellView, CreateViewWindow2), WM(IShellView, HandleRename), WM(IShellView, SelectAndPositionItem), \
  WM(IFolderView, QueryInterface), WM(IFolderView, AddRef), WM(IFolderView, Release), WM(IFolderView, GetCurrentViewMode), WM(IFolderView, SetCurrentViewMode), WM(IFolderView, GetFolder), WM(IFolderView, Item), WM(IFolderView, ItemCount), WM(IFolderView, Items), WM(IFolderView, GetSelectionMarkedItem), WM(IFolderView, GetFocusedItem), WM(IFolderView, GetItemPosition), WM(IFolderView, GetSpacing), WM(IFolderView, GetDefaultSpacing), WM(IFolderView, GetAutoArrange), WM(IFolderView, SelectItem), WM(IFolderView, SelectAndPositionItems),
