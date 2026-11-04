/* lvedit.c — SysListView32 in-place label editing: LVM_EDITLABEL / F2 (LVS_EDITLABELS) create an Edit child over the label
   (LVM_GETEDITCONTROL), LVN_BEGINLABELEDIT can veto, typing + Enter commits through LVN_ENDLABELEDIT (parent returns TRUE),
   Escape cancels (pszText == NULL), the item text changes and the edit box is gone afterwards.
   Driver: --type 'renamed@1500' --key 13@2200 --key 27@3600
   zig cc -target x86_64-windows-gnu -O1 -o lvedit.exe lvedit.c -luser32 -lgdi32 -lcomctl32 */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
static int g_ok = 0, g_fail = 0, g_begin = 0, g_end = 0, g_end_null = 0, g_veto = 0; static wchar_t g_end_text[260];
#define CHECK(cond, ...) do { if (cond) g_ok++; else { g_fail++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NOTIFY) { NMHDR *nm = (NMHDR *)l; if (nm->code == LVN_BEGINLABELEDITW) { NMLVDISPINFOW *di = (NMLVDISPINFOW *)l; g_begin++; return (di->item.iItem == 3 && g_veto) ? TRUE : FALSE; }
    if (nm->code == LVN_ENDLABELEDITW) { NMLVDISPINFOW *di = (NMLVDISPINFOW *)l; g_end++; if (!di->item.pszText) { g_end_null++; return FALSE; } lstrcpynW(g_end_text, di->item.pszText, 260); return TRUE; } }
  if (m == WM_DESTROY) { PostQuitMessage(0); return 0; } return DefWindowProcW(h, m, w, l); }
static void pump(int ms) { DWORD t0 = GetTickCount(); MSG msg; do { while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } Sleep(5); } while (GetTickCount() - t0 < (DWORD)ms); }
static void item_text(HWND lv, int k, wchar_t *out, int cap) { LVITEMW it = { LVIF_TEXT, k, 0, 0, 0, out, cap }; out[0] = 0; SendMessageW(lv, LVM_GETITEMTEXTW, k, (LPARAM)&it); }
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
  AllocConsole(); freopen("CONOUT$", "w", stdout); INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_LISTVIEW_CLASSES }; InitCommonControlsEx(&icc);
  WNDCLASSW wc = { 0 }; wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = L"LvEditHost"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassW(&wc);
  HWND host = CreateWindowExW(0, L"LvEditHost", L"lvedit", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 500, 360, 0, 0, hi, 0);
  HWND lv = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_EDITLABELS | LVS_SHOWSELALWAYS, 10, 10, 460, 280, host, (HMENU)100, hi, 0);
  LVCOLUMNW col = { LVCF_TEXT | LVCF_WIDTH, 0, 200, (LPWSTR)L"Name" }; SendMessageW(lv, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
  for (int k = 0; k < 6; k++) { wchar_t t[64]; wsprintfW(t, L"File %02d.txt", k); LVITEMW it = { LVIF_TEXT, k, 0, 0, 0, t, 0 }; SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it); }
  SetFocus(lv); pump(300);
  CHECK(SendMessageW(lv, LVM_GETEDITCONTROL, 0, 0) == 0, "no edit control before editing");
  /* ---- LVM_EDITLABEL on item 1: the edit box appears with the item text, selected, focused ---- */
  HWND ed = (HWND)SendMessageW(lv, LVM_EDITLABELW, 1, 0); pump(100);
  CHECK(ed != 0 && IsWindow(ed), "LVM_EDITLABEL returns an edit window"); CHECK(g_begin == 1, "LVN_BEGINLABELEDIT sent once (got %d)", g_begin);
  CHECK((HWND)SendMessageW(lv, LVM_GETEDITCONTROL, 0, 0) == ed, "LVM_GETEDITCONTROL is the same window");
  wchar_t cls[32] = L""; if (ed) GetClassNameW(ed, cls, 32); CHECK(!lstrcmpiW(cls, L"Edit"), "edit class is Edit (got %ls)", cls);
  wchar_t et[260] = L""; if (ed) GetWindowTextW(ed, et, 260); CHECK(!lstrcmpW(et, L"File 01.txt"), "edit holds the label text (got %ls)", et);
  CHECK(GetFocus() == ed, "the edit box has the focus"); CHECK(ed && GetParent(ed) == lv, "edit is a child of the list view");
  RECT er, lr; if (ed) { GetWindowRect(ed, &er); } SendMessageW(lv, LVM_GETITEMRECT, 1, (LPARAM)&lr); lr.left = LVIR_LABEL; SendMessageW(lv, LVM_GETITEMRECT, 1, (LPARAM)&lr); POINT lt = { lr.left, lr.top }; ClientToScreen(lv, &lt);
  CHECK(ed && er.top <= lt.y + 2 && er.bottom >= lt.y + 10 && er.left <= lt.x + 2, "edit box covers the label (edit %ld,%ld item label %ld,%ld)", er.left, er.top, lt.x, lt.y);
  /* the driver types 'renamed' (replacing the selected text) and presses Enter at 2200 ms */
  pump(2500);
  wchar_t t1[260]; item_text(lv, 1, t1, 260); CHECK(!lstrcmpW(t1, L"renamed"), "Enter commits the new text (item 1 = %ls)", t1);
  CHECK(g_end == 1 && !lstrcmpW(g_end_text, L"renamed"), "LVN_ENDLABELEDIT once with pszText=renamed (end=%d text=%ls)", g_end, g_end_text);
  CHECK(SendMessageW(lv, LVM_GETEDITCONTROL, 0, 0) == 0 && !IsWindow(ed), "edit box destroyed after commit");
  CHECK(GetFocus() == lv, "focus back on the list view");
  /* ---- F2 on the focused item, Escape cancels (driver: VK_ESCAPE at 3600 ms) ---- */
  LVITEMW st = { 0 }; st.stateMask = LVIS_FOCUSED | LVIS_SELECTED; st.state = LVIS_FOCUSED | LVIS_SELECTED; SendMessageW(lv, LVM_SETITEMSTATE, 2, (LPARAM)&st); SetFocus(lv); pump(50);
  SendMessageW(lv, WM_KEYDOWN, VK_F2, 0); pump(100); HWND ed2 = (HWND)SendMessageW(lv, LVM_GETEDITCONTROL, 0, 0); CHECK(ed2 != 0 && g_begin == 2, "F2 starts editing the focused item (edit=%p begin=%d)", (void *)ed2, g_begin);
  if (ed2) SetWindowTextW(ed2, L"should not stick");
  pump(1500);
  wchar_t t2[260]; item_text(lv, 2, t2, 260); CHECK(!lstrcmpW(t2, L"File 02.txt"), "Escape keeps the old text (item 2 = %ls)", t2);
  CHECK(g_end == 2 && g_end_null == 1, "LVN_ENDLABELEDIT with pszText=NULL on cancel (end=%d null=%d)", g_end, g_end_null);
  CHECK(SendMessageW(lv, LVM_GETEDITCONTROL, 0, 0) == 0, "edit box gone after cancel");
  /* ---- veto: the parent returns TRUE from LVN_BEGINLABELEDIT for item 3 ---- */
  g_veto = 1; HWND ed3 = (HWND)SendMessageW(lv, LVM_EDITLABELW, 3, 0); pump(100); CHECK(ed3 == 0 && g_begin == 3 && SendMessageW(lv, LVM_GETEDITCONTROL, 0, 0) == 0, "vetoed edit creates no box (edit=%p begin=%d)", (void *)ed3, g_begin); g_veto = 0;
  /* ---- focus loss commits ---- */
  HWND ed4 = (HWND)SendMessageW(lv, LVM_EDITLABELW, 4, 0); pump(100); if (ed4) SetWindowTextW(ed4, L"by focus"); SetFocus(host); pump(200);
  wchar_t t4[260]; item_text(lv, 4, t4, 260); CHECK(!lstrcmpW(t4, L"by focus") && SendMessageW(lv, LVM_GETEDITCONTROL, 0, 0) == 0, "losing focus commits (item 4 = %ls)", t4);
  CHECK(SendMessageW(lv, LVM_EDITLABELW, 99, 0) == 0, "LVM_EDITLABEL on a bad index returns NULL");
  pump(300);
  printf("%s %d/%d\n", g_fail ? "LVEDIT FAIL" : "LVEDIT OK", g_ok, g_ok + g_fail); fflush(stdout); DestroyWindow(host); return g_fail ? 1 : 0; }
