/* dlg.c — the Win32 dialog manager end to end, as installers use it (same source for x64 and x86):
   resources (string table, icon, DIALOGEX templates), DialogBoxParam / CreateDialogParam, dialog units, standard controls
   (edit, button, checkbox, combo box, list box, progress bar), nested modal dialogs, MessageBox, IsDialogMessage keyboard
   navigation (Tab / Escape), shell folders and IShellLink/IPersistFile shortcuts.
   Self-driving: a timer in the dialog performs the steps, so no host input is needed. Prints "ok"/"FAIL" lines and "ALL OK".
   build: zig rc dlg.rc; zig cc -target x86_64-windows-gnu -O2 -o dlg.exe dlg.c dlg.res -luser32 -lgdi32 -lshell32 -lole32
          zig cc -target x86-windows-gnu    -O2 -o dlg32.exe dlg.c dlg.res -luser32 -lgdi32 -lshell32 -lole32 */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>
#include "dlg.h"

static int g_ok, g_fail;
static void chk(int cond, const char *what) { if (cond) { g_ok++; printf("ok   %s\n", what); } else { g_fail++; printf("FAIL %s\n", what); } fflush(stdout); }
static HINSTANCE g_inst;
static HWND g_main;
static int g_step;

static INT_PTR CALLBACK about_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  (void)lp;
  if (m == WM_INITDIALOG) { PostMessageW(h, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0); return TRUE; }
  if (m == WM_COMMAND && LOWORD(wp) == IDOK) { EndDialog(h, 7); return TRUE; }
  return FALSE;
}

static void shortcut_test(void) {
  WCHAR dir[MAX_PATH], lnk[MAX_PATH], back[MAX_PATH];
  HRESULT hr = SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, dir);
  chk(hr == S_OK && dir[0] != 0, "SHGetFolderPathW(CSIDL_PROGRAMS)");
  CoInitialize(NULL);
  IShellLinkW *sl = NULL; IPersistFile *pf = NULL;
  hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl);
  chk(hr == S_OK && sl, "CoCreateInstance(CLSID_ShellLink)");
  if (hr != S_OK) return;
  IShellLinkW_SetPath(sl, L"C:\\Program Files\\Nebula\\app.exe");
  IShellLinkW_SetArguments(sl, L"--fast");
  IShellLinkW_SetWorkingDirectory(sl, L"C:\\Program Files\\Nebula");
  IShellLinkW_SetDescription(sl, L"Nebula test app");
  IShellLinkW_SetIconLocation(sl, L"C:\\Program Files\\Nebula\\app.exe", 2);
  hr = IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf);
  chk(hr == S_OK && pf, "IShellLink::QueryInterface(IPersistFile)");
  GetTempPathW(MAX_PATH, lnk); lstrcatW(lnk, L"nebula-test.lnk");
  hr = IPersistFile_Save(pf, lnk, TRUE);
  chk(hr == S_OK && GetFileAttributesW(lnk) != INVALID_FILE_ATTRIBUTES, "IPersistFile::Save wrote the .lnk");
  IPersistFile_Release(pf); IShellLinkW_Release(sl);
  /* read it back through a fresh object */
  sl = NULL; pf = NULL;
  hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IPersistFile, (void **)&pf);
  chk(hr == S_OK && pf, "CoCreateInstance(CLSID_ShellLink, IID_IPersistFile)");
  if (hr != S_OK) return;
  hr = IPersistFile_Load(pf, lnk, 0);
  chk(hr == S_OK, "IPersistFile::Load");
  hr = IPersistFile_QueryInterface(pf, &IID_IShellLinkW, (void **)&sl);
  chk(hr == S_OK && sl, "IPersistFile::QueryInterface(IShellLinkW)");
  back[0] = 0; IShellLinkW_GetPath(sl, back, MAX_PATH, NULL, 0);
  chk(lstrcmpW(back, L"C:\\Program Files\\Nebula\\app.exe") == 0, "GetPath after Load");
  back[0] = 0; IShellLinkW_GetArguments(sl, back, MAX_PATH);
  chk(lstrcmpW(back, L"--fast") == 0, "GetArguments after Load");
  int icon = -1; back[0] = 0; IShellLinkW_GetIconLocation(sl, back, MAX_PATH, &icon);
  chk(icon == 2, "GetIconLocation index after Load");
  IShellLinkW_Release(sl); IPersistFile_Release(pf);
  DeleteFileW(lnk);
  CoUninitialize();
}

static INT_PTR CALLBACK main_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  switch (m) {
    case WM_INITDIALOG: {
      g_main = h;
      chk(lp == 0x1234, "WM_INITDIALOG lParam");
      chk(GetDlgItem(h, IDC_EDIT) && GetDlgItem(h, IDC_COMBO) && GetDlgItem(h, IDC_LIST) && GetDlgItem(h, IDC_PROGRESS) && GetDlgItem(h, IDOK), "GetDlgItem for every control");
      WCHAR title[128]; int n = LoadStringW(g_inst, IDS_TITLE, title, 128);
      chk(n == 18 && lstrcmpW(title, L"Nebula dialog test") == 0, "LoadStringW from the string table");
      SetWindowTextW(h, title);
      SetDlgItemTextW(h, IDC_EDIT, L"C:\\Program Files\\Nebula");
      CheckDlgButton(h, IDC_CHECK, BST_CHECKED);
      SendDlgItemMessageW(h, IDC_COMBO, CB_ADDSTRING, 0, (LPARAM)L"English");
      SendDlgItemMessageW(h, IDC_COMBO, CB_ADDSTRING, 0, (LPARAM)L"Русский");
      SendDlgItemMessageW(h, IDC_COMBO, CB_ADDSTRING, 0, (LPARAM)L"Deutsch");
      SendDlgItemMessageW(h, IDC_COMBO, CB_SETCURSEL, 1, 0);
      SendDlgItemMessageW(h, IDC_LIST, LB_ADDSTRING, 0, (LPARAM)L"one");
      SendDlgItemMessageW(h, IDC_LIST, LB_ADDSTRING, 0, (LPARAM)L"two");
      SendDlgItemMessageW(h, IDC_LIST, LB_ADDSTRING, 0, (LPARAM)L"three");
      SendDlgItemMessageW(h, IDC_LIST, LB_SETCURSEL, 2, 0);
      SendDlgItemMessageW(h, IDC_PROGRESS, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
      SendDlgItemMessageW(h, IDC_PROGRESS, PBM_SETPOS, 40, 0);
      HICON ic = LoadIconW(g_inst, MAKEINTRESOURCEW(IDI_APP));
      chk(ic != NULL, "LoadIconW(IDI_APP)");
      SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)ic);
      SetTimer(h, 1, 30, NULL);
      return TRUE; }
    case WM_TIMER: {
      KillTimer(h, 1);
      switch (g_step++) {
        case 0: {
          WCHAR buf[260]; GetDlgItemTextW(h, IDC_EDIT, buf, 260);
          chk(lstrcmpW(buf, L"C:\\Program Files\\Nebula") == 0, "GetDlgItemTextW returns what SetDlgItemTextW stored");
          chk(IsDlgButtonChecked(h, IDC_CHECK) == BST_CHECKED, "IsDlgButtonChecked");
          chk(SendDlgItemMessageW(h, IDC_COMBO, CB_GETCURSEL, 0, 0) == 1, "CB_GETCURSEL");
          buf[0] = 0; SendDlgItemMessageW(h, IDC_COMBO, CB_GETLBTEXT, 1, (LPARAM)buf);
          chk(lstrcmpW(buf, L"Русский") == 0, "CB_GETLBTEXT (Unicode)");
          chk(SendDlgItemMessageW(h, IDC_LIST, LB_GETCOUNT, 0, 0) == 3 && SendDlgItemMessageW(h, IDC_LIST, LB_GETCURSEL, 0, 0) == 2, "LB_GETCOUNT / LB_GETCURSEL");
          chk(SendDlgItemMessageW(h, IDC_PROGRESS, PBM_GETPOS, 0, 0) == 40, "PBM_GETPOS");
          HWND e = GetDlgItem(h, IDC_EDIT);
          chk(GetDlgCtrlID(e) == IDC_EDIT && GetParent(e) == h, "GetDlgCtrlID / GetParent");
          RECT r = { 0, 0, 4, 8 }; MapDialogRect(h, &r);
          chk(r.right >= 5 && r.right <= 8 && r.bottom >= 11 && r.bottom <= 16, "MapDialogRect base units (8 pt font)");
          RECT wr, er; GetWindowRect(h, &wr); GetWindowRect(e, &er);
          chk(er.left >= wr.left && er.top >= wr.top && er.right <= wr.right && er.bottom <= wr.bottom, "control rect inside the dialog rect");
          chk(IsWindowVisible(e) && IsWindowEnabled(e), "IsWindowVisible / IsWindowEnabled");
          EnableWindow(GetDlgItem(h, IDCANCEL), FALSE);
          chk(!IsWindowEnabled(GetDlgItem(h, IDCANCEL)), "EnableWindow(FALSE)");
          WCHAR cls[32]; GetClassNameW(e, cls, 32);
          chk(lstrcmpiW(cls, L"Edit") == 0, "GetClassNameW of a dialog control");
          chk(GetWindowLongPtrW(h, DWLP_USER) == 0, "DWLP_USER initially 0");
          SetWindowLongPtrW(h, DWLP_USER, 77); chk(GetWindowLongPtrW(h, DWLP_USER) == 77, "DWLP_USER round trip");
          SetTimer(h, 1, 30, NULL); return TRUE; }
        case 1: {
          INT_PTR r = DialogBoxParamW(g_inst, MAKEINTRESOURCEW(IDD_ABOUT), h, about_proc, 0);
          chk(r == 7, "nested modal DialogBoxParamW returned EndDialog's value");
          chk(IsWindowEnabled(h), "owner re-enabled after the nested dialog");
          int mb = MessageBoxW(h, L"Continue the installation?", L"Question", MB_OKCANCEL | MB_ICONQUESTION);
          chk(mb == IDOK || mb == IDCANCEL, "MessageBoxW returned a button id");
          SetTimer(h, 1, 30, NULL); return TRUE; }
        case 2: {
          shortcut_test();
          /* keyboard navigation through IsDialogMessage is tested in the modeless dialog; here: click Install by posting the command like a real button does */
          PostMessageW(h, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), (LPARAM)GetDlgItem(h, IDOK));
          return TRUE; }
      }
      return TRUE; }
    case WM_COMMAND:
      if (LOWORD(wp) == IDOK) {
        SendDlgItemMessageW(h, IDC_PROGRESS, PBM_SETPOS, 100, 0);
        SetDlgItemTextW(h, IDC_STATUS, L"Nebula test is installed");
        EndDialog(h, 42); return TRUE; }
      if (LOWORD(wp) == IDCANCEL) { EndDialog(h, 2); return TRUE; }
      return FALSE;
  }
  return FALSE;
}

/* modeless dialog: Tab moves the focus through IsDialogMessage, Escape sends IDCANCEL */
static int g_modeless_step;
static INT_PTR CALLBACK modeless_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  (void)lp;
  if (m == WM_INITDIALOG) { SetFocus(GetDlgItem(h, IDC_EDIT)); SetTimer(h, 2, 30, NULL); return FALSE; }
  if (m == WM_TIMER) {
    switch (g_modeless_step++) {
      case 0: chk(GetFocus() == GetDlgItem(h, IDC_EDIT), "modeless: SetFocus on the edit control"); PostMessageW(GetDlgItem(h, IDC_EDIT), WM_KEYDOWN, VK_TAB, 0); PostMessageW(GetDlgItem(h, IDC_EDIT), WM_KEYUP, VK_TAB, 0); break;
      case 1: chk(GetFocus() == GetDlgItem(h, IDC_BROWSE), "modeless: Tab moved the focus to the next tab stop"); PostMessageW(GetFocus(), WM_KEYDOWN, VK_TAB, 0); PostMessageW(GetFocus(), WM_KEYUP, VK_TAB, 0); break;
      case 2: chk(GetFocus() == GetDlgItem(h, IDC_CHECK), "modeless: second Tab reached the checkbox"); PostMessageW(GetFocus(), WM_KEYDOWN, VK_ESCAPE, 0); PostMessageW(GetFocus(), WM_KEYUP, VK_ESCAPE, 0); break;
      default: KillTimer(h, 2); return TRUE;
    }
    return TRUE; }
  if (m == WM_COMMAND && LOWORD(wp) == IDCANCEL) { chk(1, "modeless: Escape delivered IDCANCEL"); DestroyWindow(h); return TRUE; }
  if (m == WM_DESTROY) { PostQuitMessage(5); return TRUE; }
  return FALSE;
}

int main(void) {
  g_inst = GetModuleHandleW(NULL);
  InitCommonControls();
  HRSRC rs = FindResourceW(g_inst, MAKEINTRESOURCEW(IDD_MAIN), (LPCWSTR)RT_DIALOG);
  chk(rs != NULL && SizeofResource(g_inst, rs) > 100, "FindResourceW(RT_DIALOG) / SizeofResource");
  HGLOBAL hg = LoadResource(g_inst, rs); const WORD *t = (const WORD *)LockResource(hg);
  chk(t && t[0] == 1 && t[1] == 0xFFFF, "LockResource: DIALOGEX signature");
  WCHAR greet[128]; LoadStringW(g_inst, IDS_GREET, greet, 128);
  chk(wcsstr(greet, L"\x043f\x0440\x0438\x0432\x0435\x0442") != NULL, "LoadStringW keeps Cyrillic text");
  INT_PTR r = DialogBoxParamW(g_inst, MAKEINTRESOURCEW(IDD_MAIN), NULL, main_proc, 0x1234);
  chk(r == 42, "DialogBoxParamW returned EndDialog's value (42)");
  chk(!IsWindow(g_main), "the modal dialog window was destroyed");
  /* modeless */
  HWND md = CreateDialogParamW(g_inst, MAKEINTRESOURCEW(IDD_MAIN), NULL, modeless_proc, 0);
  chk(md != NULL, "CreateDialogParamW");
  ShowWindow(md, SW_SHOW);
  MSG msg; int quit_code = -1; DWORD t0 = GetTickCount();
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    if (!IsDialogMessageW(md, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (GetTickCount() - t0 > 10000) { printf("FAIL modeless: timeout\n"); g_fail++; break; }
  }
  if (msg.message == WM_QUIT) quit_code = (int)msg.wParam;
  chk(quit_code == 5, "modeless: PostQuitMessage code reached GetMessage");
  printf("%s %d/%d\n", g_fail ? "FAILED" : "ALL OK", g_ok, g_ok + g_fail);
  return g_fail ? 1 : 0;
}
