/* fdlg.exe — exercises the file dialogs: GetOpenFileNameW (typed name + Enter), GetSaveFileNameA (default extension),
   IFileOpenDialog / IFileSaveDialog via COM (what Notepad++ uses). Driven from the host with --type / --key; prints what a real app sees. */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commdlg.h>
#define COBJMACROS
#include <shobjidl.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>

/* ---- stage 5: what Notepad++'s CustomFileDialog does — IFileDialogEvents sink, IOleWindow to get the dialog HWND, EnumChildWindows to find the
   file-name Edit (inside a ComboBox) and the OK button, a WH_KEYBOARD hook that rewrites the name on Enter and eats the 'z' key, a WH_CALLWNDPROC hook
   that sees the OK button's WM_COMMAND, and a second keyboard hook reached only through CallNextHookEx */
static IFileDialog *g_dlg; static HWND g_dlghwnd, g_edit, g_button; static int ev_type, ev_folderchanging, ev_folderchange, ev_sel, ev_fileok, kbd_calls, kbd_chain, cwp_ok, focus_ok, kbd_enter;
static HHOOK g_kbd1, g_kbd2, g_cwp;
static void find_controls(void);
static HRESULT STDMETHODCALLTYPE ev_QI(IFileDialogEvents *t, REFIID riid, void **ppv) { if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IFileDialogEvents)) { *ppv = t; return S_OK; } *ppv = 0; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE ev_AddRef(IFileDialogEvents *t) { (void)t; return 2; }
static ULONG STDMETHODCALLTYPE ev_Release(IFileDialogEvents *t) { (void)t; return 1; }
static HRESULT STDMETHODCALLTYPE ev_OnFileOk(IFileDialogEvents *t, IFileDialog *d) { (void)t; (void)d; ev_fileok++; return S_OK; }
static HRESULT STDMETHODCALLTYPE ev_OnFolderChanging(IFileDialogEvents *t, IFileDialog *d, IShellItem *psi) { (void)t; (void)d; (void)psi; ev_folderchanging++; return S_OK; }
static HRESULT STDMETHODCALLTYPE ev_OnFolderChange(IFileDialogEvents *t, IFileDialog *d) { (void)t; (void)d; ev_folderchange++; find_controls(); return S_OK; }
static HRESULT STDMETHODCALLTYPE ev_OnSelectionChange(IFileDialogEvents *t, IFileDialog *d) { (void)t; (void)d; ev_sel++; if (!g_edit) find_controls(); return S_OK; }
static HRESULT STDMETHODCALLTYPE ev_OnShareViolation(IFileDialogEvents *t, IFileDialog *d, IShellItem *p, FDE_SHAREVIOLATION_RESPONSE *r) { (void)t; (void)d; (void)p; (void)r; return S_OK; }
static HRESULT STDMETHODCALLTYPE ev_OnTypeChange(IFileDialogEvents *t, IFileDialog *d) { (void)t; UINT idx = 0; if (SUCCEEDED(IFileDialog_GetFileTypeIndex(d, &idx)) && idx) ev_type++; return S_OK; }
static HRESULT STDMETHODCALLTYPE ev_OnOverwrite(IFileDialogEvents *t, IFileDialog *d, IShellItem *p, FDE_OVERWRITE_RESPONSE *r) { (void)t; (void)d; (void)p; (void)r; return S_OK; }
static IFileDialogEventsVtbl g_evvt = { ev_QI, ev_AddRef, ev_Release, ev_OnFileOk, ev_OnFolderChanging, ev_OnFolderChange, ev_OnSelectionChange, ev_OnShareViolation, ev_OnTypeChange, ev_OnOverwrite };
static IFileDialogEvents g_ev = { &g_evvt };
static BOOL CALLBACK enum_child(HWND h, LPARAM lp) { (void)lp; wchar_t cls[64]; if (!IsWindowEnabled(h) || !GetClassNameW(h, cls, 64)) return TRUE;
  if (!lstrcmpiW(cls, L"ComboBox")) { HWND e = FindWindowExW(h, 0, L"Edit", L""); if (e) g_edit = e; }
  else if (!lstrcmpiW(cls, L"Button")) { LONG st = GetWindowLongW(h, GWL_STYLE); if ((st & (WS_CHILD | WS_GROUP)) && ((st & 0xF) == BS_PUSHBUTTON || (st & 0xF) == BS_DEFPUSHBUTTON) && !(st & 0xF0)) { RECT r, b; GetWindowRect(h, &r); if (!g_button || (GetWindowRect(g_button, &b), r.left < b.left)) g_button = h; } }
  return TRUE; }
static void find_controls(void) { IOleWindow *ow = 0; if (!g_dlg || FAILED(IFileDialog_QueryInterface(g_dlg, &IID_IOleWindow, (void **)&ow))) return; HWND h = 0; ow->lpVtbl->GetWindow(ow, &h); ow->lpVtbl->Release(ow); if (!h || !IsWindow(h)) return; g_dlghwnd = h; EnumChildWindows(h, enum_child, 0); }
static LRESULT CALLBACK kbd_hook1(int code, WPARAM wp, LPARAM lp) { if (code == HC_ACTION) kbd_chain++; return CallNextHookEx(0, code, wp, lp); }
static LRESULT CALLBACK kbd_hook2(int code, WPARAM wp, LPARAM lp) { if (code == HC_ACTION) { kbd_calls++; if (wp == 'Z') return 1;   /* eaten: never reaches the edit */
    if (wp == VK_RETURN && !(lp & 0x80000000u) && g_dlg) { kbd_enter++; HWND f = GetFocus(); if (f == g_edit) focus_ok = 1; PWSTR nm = 0; if (SUCCEEDED(IFileDialog_GetFileName(g_dlg, &nm)) && nm) { if (!wcschr(nm, L'.')) { wchar_t nn[MAX_PATH]; wcscpy(nn, nm); wcscat(nn, L".md"); IFileDialog_SetFileName(g_dlg, L""); IFileDialog_SetFileName(g_dlg, nn); } CoTaskMemFree(nm); } } }
  return CallNextHookEx(0, code, wp, lp); }
static LRESULT CALLBACK cwp_hook(int code, WPARAM wp, LPARAM lp) { if (code == HC_ACTION) { CWPSTRUCT *m = (CWPSTRUCT *)lp; if (m && m->message == WM_COMMAND && HIWORD(m->wParam) == BN_CLICKED && GetDlgItem(m->hwnd, LOWORD(m->wParam)) == g_button && g_button) cwp_ok++; } return CallNextHookEx(0, code, wp, lp); }
int main(void) {
  HANDLE h = CreateFileW(L"C:\\nebula\\fd_test.txt", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0); DWORD wr; WriteFile(h, "hello", 5, &wr, 0); CloseHandle(h);
  wchar_t file[MAX_PATH] = L"", title[MAX_PATH] = L"";
  OPENFILENAMEW o; memset(&o, 0, sizeof o); o.lStructSize = sizeof o; o.lpstrFilter = L"Text files (*.txt)\0*.txt;*.log\0All files (*.*)\0*.*\0"; o.nFilterIndex = 1;
  o.lpstrFile = file; o.nMaxFile = MAX_PATH; o.lpstrFileTitle = title; o.nMaxFileTitle = MAX_PATH; o.lpstrInitialDir = L"C:\\nebula"; o.lpstrTitle = L"Open a text file"; o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_HIDEREADONLY;
  BOOL r = GetOpenFileNameW(&o);
  printf("open=%d file=%ls off=%u ext=%u title=%ls filter=%u\n", (int)r, file, (unsigned)o.nFileOffset, (unsigned)o.nFileExtension, title, (unsigned)o.nFilterIndex);
  char afile[MAX_PATH] = "report"; OPENFILENAMEA a; memset(&a, 0, sizeof a); a.lStructSize = sizeof a; a.lpstrFilter = "Log (*.log)\0*.log\0"; a.lpstrFile = afile; a.nMaxFile = MAX_PATH; a.lpstrInitialDir = "C:\\nebula"; a.lpstrDefExt = "log"; a.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER;
  r = GetSaveFileNameA(&a);
  printf("save=%d file=%s off=%u ext=%u\n", (int)r, afile, (unsigned)a.nFileOffset, (unsigned)a.nFileExtension);
  /* modern COM dialogs (what Notepad++ & co use) */
  wchar_t com_open[MAX_PATH] = L"", com_save[MAX_PATH] = L""; UINT ftype = 0;
  CoInitializeEx(0, COINIT_APARTMENTTHREADED);
  IFileOpenDialog *od = 0; HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, 0, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&od);
  if (SUCCEEDED(hr)) { COMDLG_FILTERSPEC fs[2] = { { L"Text", L"*.txt" }, { L"All", L"*.*" } }; IShellItem *folder = 0; DWORD opts = 0;
    IFileOpenDialog_SetFileTypes(od, 2, fs); IFileOpenDialog_SetFileTypeIndex(od, 1); IFileOpenDialog_GetOptions(od, &opts); IFileOpenDialog_SetOptions(od, opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST); IFileOpenDialog_SetTitle(od, L"COM open");
    if (SUCCEEDED(SHCreateItemFromParsingName(L"C:\\nebula", 0, &IID_IShellItem, (void **)&folder))) { IFileOpenDialog_SetFolder(od, folder); IShellItem_Release(folder); }
    hr = IFileOpenDialog_Show(od, 0); printf("com open show=%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) { IShellItemArray *arr = 0; if (SUCCEEDED(IFileOpenDialog_GetResults(od, &arr))) { DWORD cnt = 0; IShellItemArray_GetCount(arr, &cnt); IShellItem *it = 0; if (cnt && SUCCEEDED(IShellItemArray_GetItemAt(arr, 0, &it))) { PWSTR nm = 0; if (SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &nm))) { wcsncpy(com_open, nm, MAX_PATH - 1); CoTaskMemFree(nm); } IShellItem_Release(it); } IShellItemArray_Release(arr); } IFileOpenDialog_GetFileTypeIndex(od, &ftype); }
    IFileOpenDialog_Release(od); }
  else printf("CoCreateInstance(FileOpenDialog) failed %08lx\n", (unsigned long)hr);
  IFileSaveDialog *sd = 0; hr = CoCreateInstance(&CLSID_FileSaveDialog, 0, CLSCTX_INPROC_SERVER, &IID_IFileSaveDialog, (void **)&sd);
  if (SUCCEEDED(hr)) { IFileDialogCustomize *cz = 0; IShellItem *folder = 0;
    if (SUCCEEDED(IFileSaveDialog_QueryInterface(sd, &IID_IFileDialogCustomize, (void **)&cz))) { IFileDialogCustomize_AddCheckButton(cz, 1000, L"Append extension", TRUE); IFileDialogCustomize_Release(cz); }
    if (SUCCEEDED(SHCreateItemFromParsingName(L"C:\\nebula", 0, &IID_IShellItem, (void **)&folder))) { IFileSaveDialog_SetFolder(sd, folder); IShellItem_Release(folder); }
    IFileSaveDialog_SetFileName(sd, L"notes"); IFileSaveDialog_SetDefaultExtension(sd, L"txt");
    hr = IFileSaveDialog_Show(sd, 0); printf("com save show=%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) { IShellItem *it = 0; if (SUCCEEDED(IFileSaveDialog_GetResult(sd, &it))) { PWSTR nm = 0; if (SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &nm))) { wcsncpy(com_save, nm, MAX_PATH - 1); CoTaskMemFree(nm); } IShellItem_Release(it); } }
    IFileSaveDialog_Release(sd); }
  wchar_t ev_save[MAX_PATH] = L""; HWND ev_dlghwnd = 0; int ev_dlg_alive_after = 1;
  hr = CoCreateInstance(&CLSID_FileSaveDialog, 0, CLSCTX_INPROC_SERVER, &IID_IFileDialog, (void **)&g_dlg);
  if (SUCCEEDED(hr)) { DWORD cookie = 0; IShellItem *folder = 0; COMDLG_FILTERSPEC fs[2] = { { L"Markdown", L"*.md" }, { L"All", L"*.*" } };
    IFileDialog_SetFileTypes(g_dlg, 2, fs); IFileDialog_SetFileTypeIndex(g_dlg, 1); IFileDialog_SetTitle(g_dlg, L"events + hooks");
    if (SUCCEEDED(SHCreateItemFromParsingName(L"C:\\nebula", 0, &IID_IShellItem, (void **)&folder))) { IFileDialog_SetFolder(g_dlg, folder); IShellItem_Release(folder); }
    g_kbd1 = SetWindowsHookExW(WH_KEYBOARD, kbd_hook1, 0, GetCurrentThreadId()); g_kbd2 = SetWindowsHookExW(WH_KEYBOARD, kbd_hook2, 0, GetCurrentThreadId()); g_cwp = SetWindowsHookExW(WH_CALLWNDPROC, cwp_hook, 0, GetCurrentThreadId());
    hr = IFileDialog_Advise(g_dlg, &g_ev, &cookie); printf("advise=%08lx cookie=%lu hooks=%p/%p/%p\n", (unsigned long)hr, (unsigned long)cookie, (void *)g_kbd1, (void *)g_kbd2, (void *)g_cwp);
    hr = IFileDialog_Show(g_dlg, 0); printf("events save show=%08lx\n", (unsigned long)hr); ev_dlghwnd = g_dlghwnd; ev_dlg_alive_after = IsWindow(g_dlghwnd);
    if (SUCCEEDED(hr)) { IShellItem *it = 0; if (SUCCEEDED(IFileDialog_GetResult(g_dlg, &it))) { PWSTR nm = 0; if (SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &nm))) { wcsncpy(ev_save, nm, MAX_PATH - 1); CoTaskMemFree(nm); } IShellItem_Release(it); } }
    IFileDialog_Unadvise(g_dlg, cookie); UnhookWindowsHookEx(g_kbd1); UnhookWindowsHookEx(g_kbd2); UnhookWindowsHookEx(g_cwp); IFileDialog *d = g_dlg; g_dlg = 0; IFileDialog_Release(d); }
  else printf("CoCreateInstance(FileSaveDialog as IFileDialog) failed %08lx\n", (unsigned long)hr);
  wchar_t ecls[32] = L"", bcls[32] = L""; (void)ecls; (void)bcls;
  printf("events: type=%d folderchanging=%d folderchange=%d sel=%d fileok=%d dlghwnd=%p edit=%p button=%p kbd=%d chain=%d enter=%d focus=%d cwp=%d alive_after=%d save=%ls\n", ev_type, ev_folderchanging, ev_folderchange, ev_sel, ev_fileok, (void *)ev_dlghwnd, (void *)g_edit, (void *)g_button, kbd_calls, kbd_chain, kbd_enter, focus_ok, cwp_ok, ev_dlg_alive_after, ev_save);
  int ev_ok = ev_type >= 1 && ev_folderchanging >= 1 && ev_folderchange >= 1 && ev_sel >= 1 && ev_fileok == 1 && ev_dlghwnd && g_edit && g_button && kbd_calls > 0 && kbd_chain > 0 && kbd_enter == 1 && focus_ok && cwp_ok >= 1 && !ev_dlg_alive_after && !wcscmp(ev_save, L"C:\\nebula\\draft.md");
  printf("com open=%ls ftype=%u save=%ls\n", com_open, ftype, com_save);
  int ok = r && !strcmp(afile, "C:\\nebula\\report.log") && a.nFileOffset == 10 && a.nFileExtension == 17 && !wcscmp(file, L"C:\\nebula\\fd_test.txt") && o.nFileOffset == 10 && o.nFileExtension == 18 && !wcscmp(title, L"fd_test.txt") && o.nFilterIndex == 1
           && !wcscmp(com_open, L"C:\\nebula\\fd_test.txt") && ftype == 1 && !wcscmp(com_save, L"C:\\nebula\\notes.txt") && ev_ok;
  printf(ok ? "FDLG OK\n" : "FDLG FAIL\n"); fflush(stdout); return ok ? 0 : 1;
}
