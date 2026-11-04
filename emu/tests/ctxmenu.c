/* ctxmenu.c — the shell context menu: IShellFolder::GetUIObjectOf(IID_IContextMenu) → QueryContextMenu / GetCommandString /
   InvokeCommand (copy → CF_HDROP, paste, delete, NewFolder on the background object), and the default folder view's own
   right-click menu (NM_RCLICK → popup → WM_COMMAND → Delete).
   zig cc -target x86_64-windows-gnu -O1 -o ctxmenu.exe ctxmenu.c -luser32 -lgdi32 -lole32 -lshell32 -luuid
   run: x64run --no-stdin --max-ms 9000 --mount-rw "<dir>=C:\ctx" --rclick 60,60@2500 --click 95,92@4000 ctxmenu.exe
   (the view fills the 400x300 client at screen 64,64; the first row of the report list is at y≈46 in the view;
    the popup opens at the click point and its first items are Open, Explore?, sep, Cut, Copy, [Paste], sep, Delete) */
#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
static int g_ok = 0, g_fail = 0;
#define CHECK(cond, ...) do { if (cond) g_ok++; else { g_fail++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
static void writefile(const char *p, const char *t) { HANDLE h = CreateFileA(p, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0); DWORD w; if (h != INVALID_HANDLE_VALUE) { WriteFile(h, t, (DWORD)strlen(t), &w, 0); CloseHandle(h); } }
static int exists(const char *p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }
/* ---- minimal IShellBrowser host for the DefView ---- */
typedef struct { IShellBrowserVtbl *vt; LONG refs; HWND hwnd; } Browser;
static HRESULT STDMETHODCALLTYPE br_QI(IShellBrowser *t, REFIID riid, void **pv) { if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IOleWindow) || IsEqualIID(riid, &IID_IShellBrowser)) { *pv = t; return S_OK; } *pv = 0; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE br_AddRef(IShellBrowser *t) { return ++((Browser *)t)->refs; }
static ULONG STDMETHODCALLTYPE br_Release(IShellBrowser *t) { return --((Browser *)t)->refs; }
static HRESULT STDMETHODCALLTYPE br_GetWindow(IShellBrowser *t, HWND *ph) { *ph = ((Browser *)t)->hwnd; return S_OK; }
static HRESULT STDMETHODCALLTYPE br_CSH(IShellBrowser *t, BOOL f) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE br_InsertMenusSB(IShellBrowser *t, HMENU h, LPOLEMENUGROUPWIDTHS w) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE br_SetMenuSB(IShellBrowser *t, HMENU h, HOLEMENU o, HWND a) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE br_RemoveMenusSB(IShellBrowser *t, HMENU h) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE br_SetStatusTextSB(IShellBrowser *t, LPCOLESTR s) { return S_OK; }
static HRESULT STDMETHODCALLTYPE br_EnableModelessSB(IShellBrowser *t, BOOL f) { return S_OK; }
static HRESULT STDMETHODCALLTYPE br_TranslateAcceleratorSB(IShellBrowser *t, MSG *m, WORD id) { return S_FALSE; }
static HRESULT STDMETHODCALLTYPE br_BrowseObject(IShellBrowser *t, PCUIDLIST_RELATIVE p, UINT f) { printf("BrowseObject flags=%x\n", f); return S_OK; }
static HRESULT STDMETHODCALLTYPE br_GetViewStateStream(IShellBrowser *t, DWORD m, IStream **s) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE br_GetControlWindow(IShellBrowser *t, UINT id, HWND *h) { *h = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE br_SendControlMsg(IShellBrowser *t, UINT id, UINT m, WPARAM w, LPARAM l, LRESULT *r) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE br_QueryActiveShellView(IShellBrowser *t, IShellView **v) { *v = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE br_OnViewWindowActive(IShellBrowser *t, IShellView *v) { return S_OK; }
static HRESULT STDMETHODCALLTYPE br_SetToolbarItems(IShellBrowser *t, LPTBBUTTONSB b, UINT n, UINT f) { return E_NOTIMPL; }
static IShellBrowserVtbl g_brvt = { br_QI, br_AddRef, br_Release, br_GetWindow, br_CSH, br_InsertMenusSB, br_SetMenuSB, br_RemoveMenusSB, br_SetStatusTextSB, br_EnableModelessSB, br_TranslateAcceleratorSB, br_BrowseObject, br_GetViewStateStream, br_GetControlWindow, br_SendControlMsg, br_QueryActiveShellView, br_OnViewWindowActive, br_SetToolbarItems };
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { if (m == WM_DESTROY) { PostQuitMessage(0); return 0; } return DefWindowProcW(h, m, w, l); }
static IShellFolder *bind_folder(const wchar_t *path) { IShellFolder *desk = 0, *f = 0; if (FAILED(SHGetDesktopFolder(&desk))) return 0; LPITEMIDLIST pidl = 0; ULONG eaten = 0; if (SUCCEEDED(IShellFolder_ParseDisplayName(desk, 0, 0, (LPWSTR)path, &eaten, &pidl, 0)) && pidl) { IShellFolder_BindToObject(desk, pidl, 0, &IID_IShellFolder, (void **)&f); CoTaskMemFree(pidl); } IShellFolder_Release(desk); return f; }
static int menu_has(HMENU m, const char *text) { int n = GetMenuItemCount(m); for (int i = 0; i < n; i++) { char b[128]; if (GetMenuStringA(m, (UINT)i, b, sizeof b, MF_BYPOSITION) && !strcmp(b, text)) return 1; } return 0; }
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
  AllocConsole(); freopen("CONOUT$", "w", stdout); CoInitialize(0);
  CreateDirectoryA("C:\\ctx\\sub", 0); writefile("C:\\ctx\\alpha.txt", "alpha"); writefile("C:\\ctx\\beta.txt", "beta-beta"); writefile("C:\\ctx\\gamma.txt", "gamma");
  IShellFolder *f = bind_folder(L"C:\\ctx"); CHECK(f != 0, "bind C:\\ctx"); if (!f) { printf("CTXMENU FAIL (no folder)\n"); return 1; }
  /* ---- item context menu over alpha.txt + beta.txt ---- */
  LPITEMIDLIST ch[2] = { 0, 0 }; ULONG eaten; IShellFolder_ParseDisplayName(f, 0, 0, (LPWSTR)L"alpha.txt", &eaten, &ch[0], 0); IShellFolder_ParseDisplayName(f, 0, 0, (LPWSTR)L"beta.txt", &eaten, &ch[1], 0); CHECK(ch[0] && ch[1], "child pidls");
  IContextMenu *cm = 0; HRESULT hr = IShellFolder_GetUIObjectOf(f, 0, 2, (PCUITEMID_CHILD_ARRAY)ch, &IID_IContextMenu, 0, (void **)&cm); CHECK(hr == S_OK && cm, "GetUIObjectOf(IContextMenu) hr=%lx", hr);
  if (cm) {
    IContextMenu2 *cm2 = 0; IContextMenu3 *cm3 = 0; CHECK(IContextMenu_QueryInterface(cm, &IID_IContextMenu2, (void **)&cm2) == S_OK && cm2, "QI IContextMenu2"); CHECK(IContextMenu_QueryInterface(cm, &IID_IContextMenu3, (void **)&cm3) == S_OK && cm3, "QI IContextMenu3"); if (cm2) IContextMenu2_Release(cm2); if (cm3) IContextMenu3_Release(cm3);
    HMENU pm = CreatePopupMenu(); AppendMenuA(pm, MF_STRING, 900, "Host item"); hr = IContextMenu_QueryContextMenu(cm, pm, 0, 1, 0x7FFF, CMF_NORMAL); int used = (int)HRESULT_CODE(hr);
    CHECK(SUCCEEDED(hr) && used >= 5, "QueryContextMenu hr=%lx used=%d", hr, used); int n = GetMenuItemCount(pm); CHECK(n >= used + 1 && menu_has(pm, "&Open") && menu_has(pm, "&Copy") && menu_has(pm, "Cu&t") && menu_has(pm, "&Delete") && menu_has(pm, "P&roperties"), "menu items n=%d", n);
    MENUITEMINFOA mii = { sizeof mii, MIIM_STATE | MIIM_ID }; GetMenuItemInfoA(pm, 0, TRUE, &mii); CHECK((mii.fState & MFS_DEFAULT) && mii.wID == 1, "Open is the default item id=%u state=%x", mii.wID, mii.fState);
    char verb[64] = ""; wchar_t verbw[64] = L""; char help[256] = ""; CHECK(IContextMenu_GetCommandString(cm, 0, GCS_VERBA, 0, verb, sizeof verb) == S_OK && !strcmp(verb, "open"), "GCS_VERBA(0)=%s", verb);
    CHECK(IContextMenu_GetCommandString(cm, mii.wID - 1, GCS_VERBW, 0, (char *)verbw, 64) == S_OK && !wcscmp(verbw, L"open"), "GCS_VERBW"); CHECK(IContextMenu_GetCommandString(cm, 0, GCS_HELPTEXTA, 0, help, sizeof help) == S_OK && strstr(help, "Opens"), "GCS_HELPTEXTA=%s", help);
    /* find "Copy" and invoke it by id offset, then by verb */ int copy_id = -1, del_id = -1; for (int i = 0; i < n; i++) { char b[64]; if (GetMenuStringA(pm, (UINT)i, b, sizeof b, MF_BYPOSITION)) { if (!strcmp(b, "&Copy")) copy_id = (int)GetMenuItemID(pm, (UINT)i); if (!strcmp(b, "&Delete")) del_id = (int)GetMenuItemID(pm, (UINT)i); } }
    CHECK(copy_id > 0 && del_id > 0, "copy/delete ids %d %d", copy_id, del_id);
    CMINVOKECOMMANDINFO ici = { sizeof ici }; ici.lpVerb = MAKEINTRESOURCEA(copy_id - 1); ici.nShow = SW_SHOWNORMAL; hr = IContextMenu_InvokeCommand(cm, &ici); CHECK(hr == S_OK, "InvokeCommand(copy by id) hr=%lx", hr);
    CHECK(IsClipboardFormatAvailable(CF_HDROP), "CF_HDROP available after copy");
    if (OpenClipboard(0)) { HDROP hd = (HDROP)GetClipboardData(CF_HDROP); UINT cnt = hd ? DragQueryFileW(hd, 0xFFFFFFFF, 0, 0) : 0; wchar_t nm[260] = L""; char nma[260] = ""; if (hd) { UINT ln = DragQueryFileW(hd, 0, nm, 260); WideCharToMultiByte(CP_ACP, 0, nm, -1, nma, sizeof nma, 0, 0); printf("DragQueryFileW len=%u nm0=%x nm1=%x wcslen=%u\n", ln, nm[0], nm[1], (unsigned)wcslen(nm)); int wr = WideCharToMultiByte(CP_ACP, 0, nm, -1, nma, sizeof nma, 0, 0); printf("W2MB=%d err=%lu [%s]\n", wr, GetLastError(), nma); } CHECK(cnt == 2 && !_stricmp(nma, "C:\\ctx\\alpha.txt"), "DROPFILES count=%u first=%s", cnt, nma); CloseClipboard(); }
    /* paste into the sub folder through the background object of C:\ctx\sub */ IShellFolder *sub = bind_folder(L"C:\\ctx\\sub"); IContextMenu *bg = 0; CHECK(sub && IShellFolder_CreateViewObject(sub, 0, &IID_IContextMenu, (void **)&bg) == S_OK && bg, "CreateViewObject(IContextMenu) on sub");
    if (bg) { CMINVOKECOMMANDINFO pi = { sizeof pi }; pi.lpVerb = "paste"; hr = IContextMenu_InvokeCommand(bg, &pi); CHECK(hr == S_OK && exists("C:\\ctx\\sub\\alpha.txt") && exists("C:\\ctx\\sub\\beta.txt") && exists("C:\\ctx\\alpha.txt"), "paste by verb hr=%lx copied=%d%d src=%d", hr, exists("C:\\ctx\\sub\\alpha.txt"), exists("C:\\ctx\\sub\\beta.txt"), exists("C:\\ctx\\alpha.txt"));
      HMENU bm = CreatePopupMenu(); hr = IContextMenu_QueryContextMenu(bg, bm, 0, 1, 0x7FFF, CMF_NORMAL); CHECK(SUCCEEDED(hr) && menu_has(bm, "R&efresh") && menu_has(bm, "New &folder") && menu_has(bm, "&Paste"), "background menu");
      CMINVOKECOMMANDINFOEX nf = { sizeof nf }; nf.fMask = CMIC_MASK_UNICODE; nf.lpVerb = "NewFolder"; nf.lpVerbW = L"NewFolder"; hr = IContextMenu_InvokeCommand(bg, (CMINVOKECOMMANDINFO *)&nf); CHECK(hr == S_OK && (GetFileAttributesA("C:\\ctx\\sub\\New folder") & FILE_ATTRIBUTE_DIRECTORY), "NewFolder (unicode verb) hr=%lx", hr);
      DestroyMenu(bm); IContextMenu_Release(bg); }
    if (sub) IShellFolder_Release(sub);
    /* delete beta.txt only: a fresh object over one item */ IContextMenu *one = 0; IShellFolder_GetUIObjectOf(f, 0, 1, (PCUITEMID_CHILD_ARRAY)&ch[1], &IID_IContextMenu, 0, (void **)&one); if (one) { CMINVOKECOMMANDINFO di = { sizeof di }; di.lpVerb = "delete"; hr = IContextMenu_InvokeCommand(one, &di); CHECK(hr == S_OK && !exists("C:\\ctx\\beta.txt") && exists("C:\\ctx\\alpha.txt"), "delete by verb hr=%lx", hr); IContextMenu_Release(one); }
    CMINVOKECOMMANDINFO bad = { sizeof bad }; bad.lpVerb = "nosuchverb"; CHECK(FAILED(IContextMenu_InvokeCommand(cm, &bad)), "unknown verb fails");
    DestroyMenu(pm); CHECK(IContextMenu_Release(cm) == 0, "Release → 0"); }
  CoTaskMemFree(ch[0]); CoTaskMemFree(ch[1]);
  /* ---- the folder view's own menu: right-click alpha.txt → Delete ---- */
  WNDCLASSW wc = { 0 }; wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = L"CtxHost"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassW(&wc);
  HWND host = CreateWindowExW(0, L"CtxHost", L"ctxmenu", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 400, 300, 0, 0, hi, 0);
  Browser br = { &g_brvt, 1, host }; IShellView *sv = 0; hr = IShellFolder_CreateViewObject(f, host, &IID_IShellView, (void **)&sv); CHECK(hr == S_OK && sv, "CreateViewObject(IShellView) hr=%lx", hr);
  HWND hview = 0; if (sv) { FOLDERSETTINGS fs = { FVM_DETAILS, 0 }; RECT rc; GetClientRect(host, &rc); hr = IShellView_CreateViewWindow(sv, 0, &fs, (IShellBrowser *)&br, &rc, &hview); CHECK(hr == S_OK && hview, "CreateViewWindow hr=%lx", hr); IShellView_UIActivate(sv, SVUIA_ACTIVATE_FOCUS); }
  int had_alpha = exists("C:\\ctx\\alpha.txt"); DWORD t0 = GetTickCount(); MSG msg;
  while (GetTickCount() - t0 < 6500) { while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) break; TranslateMessage(&msg); DispatchMessageW(&msg); } if (!exists("C:\\ctx\\alpha.txt")) break; Sleep(10); }
  CHECK(had_alpha && !exists("C:\\ctx\\alpha.txt") && exists("C:\\ctx\\gamma.txt"), "view menu Delete removed alpha.txt (alpha=%d gamma=%d)", exists("C:\\ctx\\alpha.txt"), exists("C:\\ctx\\gamma.txt"));
  if (sv) { IShellView_DestroyViewWindow(sv); IShellView_Release(sv); } IShellFolder_Release(f); DestroyWindow(host);
  printf("%s %d/%d\n", g_fail ? "CTXMENU FAIL" : "CTXMENU OK", g_ok, g_ok + g_fail); fflush(stdout); return g_fail ? 1 : 0; }
