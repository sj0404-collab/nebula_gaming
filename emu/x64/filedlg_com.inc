/* shell32 COM: IFileOpenDialog / IFileSaveDialog (+IFileDialogCustomize), IShellItem, IShellItemArray, SHCreateItemFromParsingName.
   Modern apps (Notepad++ …) open files through these instead of GetOpenFileName; both end in the same modal dialog (user.inc fd_show).
   Included from user.inc (gdi.c translation unit). Guest layouts:
     dialog [vt IFileDialog][vt IFileDialogCustomize][refs u32][spec u32]   item [vt IShellItem][refs u32][pad u32][path 1040 UTF-8]   array [vt IShellItemArray][refs u32][count u32][item ptr] */
static u64 com_vt(Cpu *c, const char *iface, const char **names, int n) { u64 P = PSZ_OF(c), vt = w32_heap_alloc(W, (u64)n * P, 1); char full[96]; for (int i = 0; i < n; i++) { e_snprintf(full, sizeof full, "%s::%s", iface, names[i]); wrp(c, vt + (u64)i * P, w32_shim_slot(W, "shell32", full)); } return vt; }
static const char *g_fd_names[32] = { "QueryInterface", "AddRef", "Release", "Show", "SetFileTypes", "SetFileTypeIndex", "GetFileTypeIndex", "Advise", "Unadvise", "SetOptions", "GetOptions", "SetDefaultFolder", "SetFolder", "GetFolder", "GetCurrentSelection", "SetFileName", "GetFileName", "SetTitle", "SetOkButtonLabel", "SetFileNameLabel", "GetResult", "AddPlace", "SetDefaultExtension", "Close", "SetClientGuid", "ClearClientData", "SetFilter", "M27", "M28", "M29", "M30", "M31" };
static const char *g_fdc_names[30] = { "QueryInterface", "AddRef", "Release", "EnableOpenDropDown", "AddMenu", "AddPushButton", "AddComboBox", "AddRadioButtonList", "AddCheckButton", "AddEditBox", "AddSeparator", "AddText", "SetControlLabel", "GetControlState", "SetControlState", "GetEditBoxText", "SetEditBoxText", "GetCheckButtonState", "SetCheckButtonState", "AddControlItem", "RemoveControlItem", "RemoveAllControlItems", "GetControlItemState", "SetControlItemState", "GetSelectedControlItem", "SetSelectedControlItem", "StartVisualGroup", "EndVisualGroup", "MakeProminent", "SetControlItemText" };
static const char *g_si_names[8] = { "QueryInterface", "AddRef", "Release", "BindToHandler", "GetParent", "GetDisplayName", "GetAttributes", "Compare" };
static const char *g_sia_names[10] = { "QueryInterface", "AddRef", "Release", "BindToHandler", "GetPropertyStore", "GetPropertyDescriptionList", "GetAttributes", "GetCount", "GetItemAt", "EnumItems" };
static u64 si_create(Cpu *c, const char *path) { u64 P = PSZ_OF(c), o = w32_heap_alloc(W, P + 8 + 1040, 1); if (!o) return 0; wrp(c, o, com_vt(c, "IShellItem", g_si_names, 8)); wr32(c, o + P, 1); wr32(c, o + P + 4, 0); u8 *d = mem_ptr(c->mem, o + P + 8, 1040, 1); if (d) { e_memset(d, 0, 1040); e_snprintf((char *)d, 1040, "%s", path); } return o; }
static const char *si_path(Cpu *c, u64 o) { const char *p = rd_str(c, o + PSZ_OF(c) + 8, 1039); return p ? p : ""; }
static u64 com_alloc_wstr(Cpu *c, const char *s) { u64 a = w32_heap_alloc(W, (e_strlen(s) + 1) * 2, 0); if (a) put_w8(c, a, s, 0); return a; }
static void com_addref_at(Cpu *c, u64 r) { wr32(c, r, rd32(c, r) + 1); RET(rd32(c, r)); }
/* IShellItem */
static void s_IShellItem_QueryInterface(Cpu *c) { u32 d1 = rd32(c, ARG(1)); if (!ARG(2)) { RET(0x80070057u); return; } if (d1 == 0 || d1 == 0x43826d1eu) { u64 r = ARG(0) + PSZ_OF(c); wr32(c, r, rd32(c, r) + 1); wrp(c, ARG(2), ARG(0)); RET(0); return; } wrp(c, ARG(2), 0); RET(0x80004002u); }
static void s_IShellItem_AddRef(Cpu *c) { com_addref_at(c, ARG(0) + PSZ_OF(c)); }
static void s_IShellItem_Release(Cpu *c) { u64 r = ARG(0) + PSZ_OF(c); u32 n = rd32(c, r); if (n) n--; wr32(c, r, n); if (!n) w32_heap_free(W, ARG(0)); RET(n); }
static void s_IShellItem_BindToHandler(Cpu *c) { if (ARG(4)) wrp(c, ARG(4), 0); RET(0x80004001u); }
static void s_IShellItem_GetParent(Cpu *c) { char p[1040]; e_snprintf(p, sizeof p, "%s", si_path(c, ARG(0))); if (e_strlen(p) <= 3) { if (ARG(1)) wrp(c, ARG(1), 0); RET(0x80070490u); return; } fd_up(p); u64 o = si_create(c, p); if (ARG(1)) wrp(c, ARG(1), o); RET(o ? 0 : 0x8007000Eu); }
static void s_IShellItem_GetDisplayName(Cpu *c) { u32 sigdn = (u32)ARG(1); const char *p = si_path(c, ARG(0)); const char *b = p; for (const char *q = p; *q; q++) if (*q == '\\') b = q + 1;
  int rel = sigdn == 0 /* NORMALDISPLAY */ || sigdn == 0x80018001u /* PARENTRELATIVEPARSING */ || sigdn == 0x80031001u /* PARENTRELATIVEEDITING */ || sigdn == 0x8007c001u /* PARENTRELATIVEFORADDRESSBAR */ || sigdn == 0x80080001u /* PARENTRELATIVE */ || sigdn == 0x80094001u /* PARENTRELATIVEFORUI */;
  if (!ARG(2)) { RET(0x80070057u); return; } u64 a = com_alloc_wstr(c, rel ? b : p); wrp(c, ARG(2), a); RET(a ? 0 : 0x8007000Eu); }
static void s_IShellItem_GetAttributes(Cpu *c) { u64 sz; int st = vfs_stat(si_path(c, ARG(0)), &sz); u32 attr = 0x40000000u /* SFGAO_FILESYSTEM */ | (st == 2 ? 0x20000000u /* FOLDER */ | 0x80000000u /* HASSUBFOLDER */ : 0x00400000u /* STREAM */); if (ARG(2)) wr32(c, ARG(2), attr & (u32)ARG(1)); RET((attr & (u32)ARG(1)) == (u32)ARG(1) ? 0 : 1); }
static void s_IShellItem_Compare(Cpu *c) { int r = ARG(1) ? e_stricmp(si_path(c, ARG(0)), si_path(c, ARG(1))) : 1; if (ARG(3)) wr32(c, ARG(3), (u32)r); RET(r ? 1 : 0); }
/* IShellItemArray (one item: the dialog is single-select) */
static u64 sia_create(Cpu *c, u64 item) { u64 P = PSZ_OF(c), o = w32_heap_alloc(W, 2 * P + 8, 1); if (!o) return 0; wrp(c, o, com_vt(c, "IShellItemArray", g_sia_names, 10)); wr32(c, o + P, 1); wr32(c, o + P + 4, item ? 1 : 0); wrp(c, o + P + 8, item); return o; }
static void s_IShellItemArray_QueryInterface(Cpu *c) { u32 d1 = rd32(c, ARG(1)); if (!ARG(2)) { RET(0x80070057u); return; } if (d1 == 0 || d1 == 0xb63ea76du) { u64 r = ARG(0) + PSZ_OF(c); wr32(c, r, rd32(c, r) + 1); wrp(c, ARG(2), ARG(0)); RET(0); return; } wrp(c, ARG(2), 0); RET(0x80004002u); }
static void s_IShellItemArray_AddRef(Cpu *c) { com_addref_at(c, ARG(0) + PSZ_OF(c)); }
static void s_IShellItemArray_Release(Cpu *c) { u64 r = ARG(0) + PSZ_OF(c); u32 n = rd32(c, r); if (n) n--; wr32(c, r, n); if (!n) { u64 it = rdp(c, r + 8); if (it) { u64 ir = it + PSZ_OF(c); u32 k = rd32(c, ir); if (k) k--; wr32(c, ir, k); if (!k) w32_heap_free(W, it); } w32_heap_free(W, ARG(0)); } RET(n); }
static void s_IShellItemArray_BindToHandler(Cpu *c) { if (ARG(4)) wrp(c, ARG(4), 0); RET(0x80004001u); }
static void s_IShellItemArray_GetPropertyStore(Cpu *c) { if (ARG(3)) wrp(c, ARG(3), 0); RET(0x80004001u); }
static void s_IShellItemArray_GetPropertyDescriptionList(Cpu *c) { if (ARG(3)) wrp(c, ARG(3), 0); RET(0x80004001u); }
static void s_IShellItemArray_GetAttributes(Cpu *c) { if (ARG(3)) wr32(c, ARG(3), 0x40000000u & (u32)ARG(2)); RET(0); }
static void s_IShellItemArray_GetCount(Cpu *c) { if (ARG(1)) wr32(c, ARG(1), rd32(c, ARG(0) + PSZ_OF(c) + 4)); RET(0); }
static void s_IShellItemArray_GetItemAt(Cpu *c) { u64 it = rdp(c, ARG(0) + PSZ_OF(c) + 8); if ((u32)ARG(1) >= rd32(c, ARG(0) + PSZ_OF(c) + 4) || !it) { if (ARG(2)) wrp(c, ARG(2), 0); RET(0x80070057u); return; } u64 ir = it + PSZ_OF(c); wr32(c, ir, rd32(c, ir) + 1); wrp(c, ARG(2), it); RET(0); }
static void s_IShellItemArray_EnumItems(Cpu *c) { if (ARG(1)) wrp(c, ARG(1), 0); RET(0x80004001u); }
static void s_SHCreateItemFromParsingName(Cpu *c) { if (!ARG(0) || !ARG(3)) { RET(0x80070057u); return; } char p[1040]; text_in(c, ARG(0), 1, p, sizeof p); u64 sz; if (!vfs_stat(p, &sz)) { wrp(c, ARG(3), 0); RET(0x80070002u); return; } u64 o = si_create(c, p); wrp(c, ARG(3), o); RET(o ? 0 : 0x8007000Eu); }
static void s_SHCreateItemFromIDList(Cpu *c) { if (!ARG(0) || !ARG(2)) { RET(0x80070057u); return; } u64 o = si_create(c, pidl_path(c, ARG(0))); wrp(c, ARG(2), o); RET(o ? 0 : 0x8007000Eu); }
static void s_SHGetIDListFromObject(Cpu *c) { if (!ARG(0) || !ARG(1)) { RET(0x80070057u); return; } wrp(c, ARG(1), pidl_make(W, si_path(c, ARG(0)))); RET(0); }
/* IFileDialog */
static FdSpec *fd_spec_of(Cpu *c, u64 this_) { u32 k = rd32(c, this_ + 2 * PSZ_OF(c) + 4); return k < 8 && g_fdspec[k].used ? &g_fdspec[k] : 0; }
static u64 fd_this(Cpu *c, int sub) { return ARG(0) - (u64)sub * PSZ_OF(c); }
static int fd_qi(Cpu *c, u64 obj, u64 riid, u64 ppv) { u32 d1 = rd32(c, riid); u64 P = PSZ_OF(c); if (!ppv) return 0; FdSpec *sp = fd_spec_of(c, obj);
  if (d1 == 0 || d1 == 0xb4db1657u /* IModalWindow */ || d1 == 0x42f85136u /* IFileDialog */ || (d1 == 0xd57c7288u /* IFileOpenDialog */ && sp && !sp->save) || (d1 == 0x84bccd23u /* IFileSaveDialog */ && sp && sp->save)) wrp(c, ppv, obj);
  else if (d1 == 0xe6fdd21au /* IFileDialogCustomize */) wrp(c, ppv, obj + P); else { wrp(c, ppv, 0); return 0; }
  wr32(c, obj + 2 * P, rd32(c, obj + 2 * P) + 1); return 1; }
static void fd_release(Cpu *c, u64 obj) { u64 a = obj + 2 * PSZ_OF(c); u32 n = rd32(c, a); if (n) n--; wr32(c, a, n); if (!n) { FdSpec *sp = fd_spec_of(c, obj); if (sp) sp->used = 0; w32_heap_free(W, obj); } RET(n); }
static void s_IFileDialog_QueryInterface(Cpu *c) { RET(fd_qi(c, fd_this(c, 0), ARG(1), ARG(2)) ? 0 : 0x80004002u); }
static void s_IFileDialogCustomize_QueryInterface(Cpu *c) { RET(fd_qi(c, fd_this(c, 1), ARG(1), ARG(2)) ? 0 : 0x80004002u); }
static void s_IFileDialog_AddRef(Cpu *c) { com_addref_at(c, fd_this(c, 0) + 2 * PSZ_OF(c)); }
static void s_IFileDialogCustomize_AddRef(Cpu *c) { com_addref_at(c, fd_this(c, 1) + 2 * PSZ_OF(c)); }
static void s_IFileDialog_Release(Cpu *c) { fd_release(c, fd_this(c, 0)); }
static void s_IFileDialogCustomize_Release(Cpu *c) { fd_release(c, fd_this(c, 1)); }
static void s_IFileDialog_Show(Cpu *c) { Win32 *w = W; if (modal_reentry(c)) { modal_loop(c, g_modal[g_nmodal - 1].wnd); return; }
  FdSpec *sp = fd_spec_of(c, ARG(0)); if (!sp) { RET(0x80004005u); return; }
  if (g_msgbox_auto) { w32_report(w, "IFileDialog::Show: headless run — cancelled"); RET(0x800704C7u); return; }
  FileDlg *fd = fd_alloc(); if (!fd) { RET(0x80004005u); return; } fd->spec = (int)(sp - g_fdspec); fd->save = sp->save; fd->wide = 1; fd->pickfolders = (sp->options & 0x20) != 0; if (sp->options & 0x1000 /* FOS_FILEMUSTEXIST */) fd->flags |= 0x1000;
  fd->nflt = sp->nflt; e_memcpy(fd->fname, sp->fname, sizeof fd->fname); e_memcpy(fd->fpat, sp->fpat, sizeof fd->fpat); e_snprintf(fd->defext, sizeof fd->defext, "%s", sp->defext); sp->result[0] = 0;
  char start[1024]; u64 szd; e_snprintf(start, sizeof start, "%s", sp->folder); if (!start[0] || vfs_stat(start, &szd) != 2) e_snprintf(start, sizeof start, "%s", vfs_cwd()[0] ? vfs_cwd() : "C:\\");
  fd_show(c, fd, ARG(1), sp->title[0] ? sp->title : (fd->pickfolders ? "Select Folder" : sp->save ? "Save As" : "Open"), start); }
static void s_IFileDialog_SetFileTypes(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (!sp) { RET(0x80004005u); return; } u32 n = (u32)ARG(1); u64 a = ARG(2), P = PSZ_OF(c); sp->nflt = 0; for (u32 k = 0; k < n && k < FD_MAX_FILTERS && a; k++) { u64 nm = rdp(c, a + (u64)k * 2 * P), pt = rdp(c, a + (u64)k * 2 * P + P); if (nm) text_in(c, nm, 1, sp->fname[k], 64); else sp->fname[k][0] = 0; if (pt) text_in(c, pt, 1, sp->fpat[k], 128); else e_snprintf(sp->fpat[k], 128, "*"); sp->nflt = (int)k + 1; } RET(0); }
static void s_IFileDialog_SetFileTypeIndex(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (sp) sp->ftype = (int)(u32)ARG(1); RET(0); }
static void s_IFileDialog_GetFileTypeIndex(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (ARG(1)) wr32(c, ARG(1), sp ? (u32)(sp->ftype ? sp->ftype : 1) : 1); RET(0); }
static void s_IFileDialog_Advise(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (sp) sp->sink = ARG(1); if (ARG(2)) wr32(c, ARG(2), 1); RET(0); }   /* the sink is accepted; the dialog does not call back into it */
static void s_IFileDialog_Unadvise(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (sp) sp->sink = 0; RET(0); }
static void s_IFileDialog_SetOptions(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (sp) sp->options = (u32)ARG(1); RET(0); }
static void s_IFileDialog_GetOptions(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (ARG(1)) wr32(c, ARG(1), sp ? sp->options : 0); RET(0); }
static void s_IFileDialog_SetDefaultFolder(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (sp && ARG(1) && !sp->folder[0]) e_snprintf(sp->folder, sizeof sp->folder, "%s", si_path(c, ARG(1))); RET(0); }
static void s_IFileDialog_SetFolder(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (sp && ARG(1)) e_snprintf(sp->folder, sizeof sp->folder, "%s", si_path(c, ARG(1))); RET(0); }
static void s_IFileDialog_GetFolder(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (!ARG(1)) { RET(0x80070057u); return; } const char *f = sp && sp->folder[0] ? sp->folder : vfs_cwd(); u64 o = si_create(c, f); wrp(c, ARG(1), o); RET(o ? 0 : 0x8007000Eu); }
static void s_IFileDialog_GetCurrentSelection(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (!ARG(1)) { RET(0x80070057u); return; } if (!sp || !sp->result[0]) { wrp(c, ARG(1), 0); RET(0x80004005u); return; } u64 o = si_create(c, sp->result); wrp(c, ARG(1), o); RET(o ? 0 : 0x8007000Eu); }
static void s_IFileDialog_SetFileName(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (sp) { if (ARG(1)) text_in(c, ARG(1), 1, sp->filename, sizeof sp->filename); else sp->filename[0] = 0; } RET(0); }
static void s_IFileDialog_GetFileName(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (!ARG(1)) { RET(0x80070057u); return; } u64 a = com_alloc_wstr(c, sp ? sp->filename : ""); wrp(c, ARG(1), a); RET(a ? 0 : 0x8007000Eu); }
static void s_IFileDialog_SetTitle(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (sp && ARG(1)) text_in(c, ARG(1), 1, sp->title, sizeof sp->title); RET(0); }
static void s_IFileDialog_SetOkButtonLabel(Cpu *c) { RET(0); }
static void s_IFileDialog_SetFileNameLabel(Cpu *c) { RET(0); }
static void s_IFileDialog_GetResult(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (!ARG(1)) { RET(0x80070057u); return; } if (!sp || !sp->result[0]) { wrp(c, ARG(1), 0); RET(0x80004005u); return; } u64 o = si_create(c, sp->result); wrp(c, ARG(1), o); RET(o ? 0 : 0x8007000Eu); }
static void s_IFileDialog_AddPlace(Cpu *c) { RET(0); }
static void s_IFileDialog_SetDefaultExtension(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (sp) { if (ARG(1)) text_in(c, ARG(1), 1, sp->defext, sizeof sp->defext); else sp->defext[0] = 0; } RET(0); }
static void s_IFileDialog_Close(Cpu *c) { RET(0); }
static void s_IFileDialog_SetClientGuid(Cpu *c) { RET(0); }
static void s_IFileDialog_ClearClientData(Cpu *c) { RET(0); }
static void s_IFileDialog_SetFilter(Cpu *c) { RET(0); }
/* slots 27–31 differ: IFileOpenDialog (GetResults, GetSelectedItems) vs IFileSaveDialog (SetSaveAsItem, SetProperties, SetCollectedProperties, GetProperties, ApplyProperties) */
static void s_IFileDialog_M27(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (!sp) { RET(0x80004005u); return; }
  if (sp->save) { if (ARG(1)) { const char *p = si_path(c, ARG(1)); const char *b = p; for (const char *q = p; *q; q++) if (*q == '\\') b = q + 1; e_snprintf(sp->filename, sizeof sp->filename, "%s", b); int dl = (int)(b - p); if (dl > 3) dl--; if (dl > 0) e_snprintf(sp->folder, sizeof sp->folder, "%.*s", dl, p); } RET(0); return; }
  if (!ARG(1)) { RET(0x80070057u); return; } if (!sp->result[0]) { wrp(c, ARG(1), 0); RET(0x80004005u); return; } u64 it = si_create(c, sp->result), arr = it ? sia_create(c, it) : 0; wrp(c, ARG(1), arr); RET(arr ? 0 : 0x8007000Eu); }
static void s_IFileDialog_M28(Cpu *c) { FdSpec *sp = fd_spec_of(c, ARG(0)); if (!sp) { RET(0x80004005u); return; } if (sp->save) { RET(0); return; } s_IFileDialog_M27(c); }
static void s_IFileDialog_M29(Cpu *c) { RET(0); }
static void s_IFileDialog_M30(Cpu *c) { if (ARG(1)) wrp(c, ARG(1), 0); RET(0x80004001u); }
static void s_IFileDialog_M31(Cpu *c) { RET(0); }
/* IFileDialogCustomize: controls are accepted and remembered (check buttons keep their state) but not drawn */
static void s_IFileDialogCustomize_EnableOpenDropDown(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_AddMenu(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_AddPushButton(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_AddComboBox(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_AddRadioButtonList(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_AddCheckButton(Cpu *c) { FdSpec *sp = fd_spec_of(c, fd_this(c, 1)); if (sp && sp->nchk < 8) { sp->chk_id[sp->nchk] = (int)(u32)ARG(1); sp->chk_val[sp->nchk] = (u32)ARG(3) != 0; sp->nchk++; } RET(0); }
static void s_IFileDialogCustomize_AddEditBox(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_AddSeparator(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_AddText(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_SetControlLabel(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_GetControlState(Cpu *c) { if (ARG(2)) wr32(c, ARG(2), 3 /* ENABLED|VISIBLE */); RET(0); }
static void s_IFileDialogCustomize_SetControlState(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_GetEditBoxText(Cpu *c) { if (ARG(2)) wrp(c, ARG(2), com_alloc_wstr(c, "")); RET(0); }
static void s_IFileDialogCustomize_SetEditBoxText(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_GetCheckButtonState(Cpu *c) { FdSpec *sp = fd_spec_of(c, fd_this(c, 1)); u32 v = 0; if (sp) for (int k = 0; k < sp->nchk; k++) if (sp->chk_id[k] == (int)(u32)ARG(1)) v = (u32)sp->chk_val[k]; if (ARG(2)) wr32(c, ARG(2), v); RET(0); }
static void s_IFileDialogCustomize_SetCheckButtonState(Cpu *c) { FdSpec *sp = fd_spec_of(c, fd_this(c, 1)); if (sp) for (int k = 0; k < sp->nchk; k++) if (sp->chk_id[k] == (int)(u32)ARG(1)) sp->chk_val[k] = (u32)ARG(2) != 0; RET(0); }
static void s_IFileDialogCustomize_AddControlItem(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_RemoveControlItem(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_RemoveAllControlItems(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_GetControlItemState(Cpu *c) { if (ARG(3)) wr32(c, ARG(3), 3); RET(0); }
static void s_IFileDialogCustomize_SetControlItemState(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_GetSelectedControlItem(Cpu *c) { if (ARG(2)) wr32(c, ARG(2), 0); RET(0); }
static void s_IFileDialogCustomize_SetSelectedControlItem(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_StartVisualGroup(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_EndVisualGroup(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_MakeProminent(Cpu *c) { RET(0); }
static void s_IFileDialogCustomize_SetControlItemText(Cpu *c) { RET(0); }
/* CoCreateInstance hook (called from win32.c): CLSID_FileOpenDialog {DC1C5A9C-…} / CLSID_FileSaveDialog {C0B4E2F3-…} */
int gdi_filedlg_cocreate(Cpu *c, u32 clsid_d1, u64 riid, u64 ppv) {
  if (clsid_d1 != 0xDC1C5A9Cu && clsid_d1 != 0xC0B4E2F3u) return 0; int k = -1; for (int q = 0; q < 8; q++) if (!g_fdspec[q].used) { k = q; break; }
  if (k < 0) { RET(0x8007000Eu); return 1; } u64 P = PSZ_OF(c), obj = w32_heap_alloc(W, 2 * P + 8, 1); if (!obj) { RET(0x8007000Eu); return 1; }
  FdSpec *sp = &g_fdspec[k]; e_memset(sp, 0, sizeof *sp); sp->used = 1; sp->save = clsid_d1 == 0xC0B4E2F3u; sp->options = 0x40 /* FOS_FORCEFILESYSTEM */ | 0x8 /* NOCHANGEDIR */ | (sp->save ? 0x2 /* OVERWRITEPROMPT */ : 0x1000 /* FILEMUSTEXIST */) | 0x800 /* PATHMUSTEXIST */; sp->ftype = 1;
  wrp(c, obj, com_vt(c, "IFileDialog", g_fd_names, 32)); wrp(c, obj + P, com_vt(c, "IFileDialogCustomize", g_fdc_names, 30)); wr32(c, obj + 2 * P, 0); wr32(c, obj + 2 * P + 4, (u32)k);
  if (!fd_qi(c, obj, riid, ppv)) { sp->used = 0; w32_heap_free(W, obj); RET(0x80004002u); return 1; } RET(0); return 1; }
#define FILEDLG_COM_SHIMS \
  SA("IShellItem::QueryInterface", IShellItem_QueryInterface), SA("IShellItem::AddRef", IShellItem_AddRef), SA("IShellItem::Release", IShellItem_Release), SA("IShellItem::BindToHandler", IShellItem_BindToHandler), SA("IShellItem::GetParent", IShellItem_GetParent), SA("IShellItem::GetDisplayName", IShellItem_GetDisplayName), SA("IShellItem::GetAttributes", IShellItem_GetAttributes), SA("IShellItem::Compare", IShellItem_Compare), \
  SA("IShellItemArray::QueryInterface", IShellItemArray_QueryInterface), SA("IShellItemArray::AddRef", IShellItemArray_AddRef), SA("IShellItemArray::Release", IShellItemArray_Release), SA("IShellItemArray::BindToHandler", IShellItemArray_BindToHandler), SA("IShellItemArray::GetPropertyStore", IShellItemArray_GetPropertyStore), SA("IShellItemArray::GetPropertyDescriptionList", IShellItemArray_GetPropertyDescriptionList), SA("IShellItemArray::GetAttributes", IShellItemArray_GetAttributes), SA("IShellItemArray::GetCount", IShellItemArray_GetCount), SA("IShellItemArray::GetItemAt", IShellItemArray_GetItemAt), SA("IShellItemArray::EnumItems", IShellItemArray_EnumItems), \
  S(SHCreateItemFromParsingName), S(SHCreateItemFromIDList), S(SHGetIDListFromObject), \
  SA("IFileDialog::QueryInterface", IFileDialog_QueryInterface), SA("IFileDialog::AddRef", IFileDialog_AddRef), SA("IFileDialog::Release", IFileDialog_Release), SA("IFileDialog::Show", IFileDialog_Show), SA("IFileDialog::SetFileTypes", IFileDialog_SetFileTypes), SA("IFileDialog::SetFileTypeIndex", IFileDialog_SetFileTypeIndex), SA("IFileDialog::GetFileTypeIndex", IFileDialog_GetFileTypeIndex), SA("IFileDialog::Advise", IFileDialog_Advise), SA("IFileDialog::Unadvise", IFileDialog_Unadvise), \
  SA("IFileDialog::SetOptions", IFileDialog_SetOptions), SA("IFileDialog::GetOptions", IFileDialog_GetOptions), SA("IFileDialog::SetDefaultFolder", IFileDialog_SetDefaultFolder), SA("IFileDialog::SetFolder", IFileDialog_SetFolder), SA("IFileDialog::GetFolder", IFileDialog_GetFolder), SA("IFileDialog::GetCurrentSelection", IFileDialog_GetCurrentSelection), SA("IFileDialog::SetFileName", IFileDialog_SetFileName), SA("IFileDialog::GetFileName", IFileDialog_GetFileName), SA("IFileDialog::SetTitle", IFileDialog_SetTitle), \
  SA("IFileDialog::SetOkButtonLabel", IFileDialog_SetOkButtonLabel), SA("IFileDialog::SetFileNameLabel", IFileDialog_SetFileNameLabel), SA("IFileDialog::GetResult", IFileDialog_GetResult), SA("IFileDialog::AddPlace", IFileDialog_AddPlace), SA("IFileDialog::SetDefaultExtension", IFileDialog_SetDefaultExtension), SA("IFileDialog::Close", IFileDialog_Close), SA("IFileDialog::SetClientGuid", IFileDialog_SetClientGuid), SA("IFileDialog::ClearClientData", IFileDialog_ClearClientData), SA("IFileDialog::SetFilter", IFileDialog_SetFilter), \
  SA("IFileDialog::M27", IFileDialog_M27), SA("IFileDialog::M28", IFileDialog_M28), SA("IFileDialog::M29", IFileDialog_M29), SA("IFileDialog::M30", IFileDialog_M30), SA("IFileDialog::M31", IFileDialog_M31), \
  SA("IFileDialogCustomize::QueryInterface", IFileDialogCustomize_QueryInterface), SA("IFileDialogCustomize::AddRef", IFileDialogCustomize_AddRef), SA("IFileDialogCustomize::Release", IFileDialogCustomize_Release), SA("IFileDialogCustomize::EnableOpenDropDown", IFileDialogCustomize_EnableOpenDropDown), SA("IFileDialogCustomize::AddMenu", IFileDialogCustomize_AddMenu), SA("IFileDialogCustomize::AddPushButton", IFileDialogCustomize_AddPushButton), SA("IFileDialogCustomize::AddComboBox", IFileDialogCustomize_AddComboBox), SA("IFileDialogCustomize::AddRadioButtonList", IFileDialogCustomize_AddRadioButtonList), \
  SA("IFileDialogCustomize::AddCheckButton", IFileDialogCustomize_AddCheckButton), SA("IFileDialogCustomize::AddEditBox", IFileDialogCustomize_AddEditBox), SA("IFileDialogCustomize::AddSeparator", IFileDialogCustomize_AddSeparator), SA("IFileDialogCustomize::AddText", IFileDialogCustomize_AddText), SA("IFileDialogCustomize::SetControlLabel", IFileDialogCustomize_SetControlLabel), SA("IFileDialogCustomize::GetControlState", IFileDialogCustomize_GetControlState), SA("IFileDialogCustomize::SetControlState", IFileDialogCustomize_SetControlState), \
  SA("IFileDialogCustomize::GetEditBoxText", IFileDialogCustomize_GetEditBoxText), SA("IFileDialogCustomize::SetEditBoxText", IFileDialogCustomize_SetEditBoxText), SA("IFileDialogCustomize::GetCheckButtonState", IFileDialogCustomize_GetCheckButtonState), SA("IFileDialogCustomize::SetCheckButtonState", IFileDialogCustomize_SetCheckButtonState), SA("IFileDialogCustomize::AddControlItem", IFileDialogCustomize_AddControlItem), SA("IFileDialogCustomize::RemoveControlItem", IFileDialogCustomize_RemoveControlItem), SA("IFileDialogCustomize::RemoveAllControlItems", IFileDialogCustomize_RemoveAllControlItems), \
  SA("IFileDialogCustomize::GetControlItemState", IFileDialogCustomize_GetControlItemState), SA("IFileDialogCustomize::SetControlItemState", IFileDialogCustomize_SetControlItemState), SA("IFileDialogCustomize::GetSelectedControlItem", IFileDialogCustomize_GetSelectedControlItem), SA("IFileDialogCustomize::SetSelectedControlItem", IFileDialogCustomize_SetSelectedControlItem), SA("IFileDialogCustomize::StartVisualGroup", IFileDialogCustomize_StartVisualGroup), SA("IFileDialogCustomize::EndVisualGroup", IFileDialogCustomize_EndVisualGroup), SA("IFileDialogCustomize::MakeProminent", IFileDialogCustomize_MakeProminent), SA("IFileDialogCustomize::SetControlItemText", IFileDialogCustomize_SetControlItemText)
