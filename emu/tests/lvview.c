/* lvview.c — SysListView32 view modes: LVS_ICON (grid, large icons, wrapped labels), LVS_SMALLICON (grid), LVS_LIST (columns,
   horizontal scrolling) and LVS_REPORT; LVM_SETVIEW, GETITEMPOSITION, GETITEMRECT, HITTEST, GETCOUNTPERPAGE, GETTOPINDEX,
   ENSUREVISIBLE, SETICONSPACING / GETITEMSPACING, GETORIGIN, arrow keys per mode.
   zig cc -target x86_64-windows-gnu -O1 -o lvview.exe lvview.c -luser32 -lgdi32 -lcomctl32 */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
static int g_ok = 0, g_fail = 0;
#define CHECK(cond, ...) do { if (cond) g_ok++; else { g_fail++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
static HIMAGELIST make_iml(int cx, int cy, int n) { HIMAGELIST il = ImageList_Create(cx, cy, ILC_COLOR24, n, 0); HDC sdc = GetDC(0); for (int k = 0; k < n; k++) { HDC dc = CreateCompatibleDC(sdc); HBITMAP bm = CreateCompatibleBitmap(sdc, cx, cy); HGDIOBJ o = SelectObject(dc, bm); HBRUSH b = CreateSolidBrush(RGB(40 + k * 50, 120, 220 - k * 40)); RECT r = { 0, 0, cx, cy }; FillRect(dc, &r, b); DeleteObject(b); SelectObject(dc, o); DeleteDC(dc); ImageList_Add(il, bm, 0); DeleteObject(bm); } ReleaseDC(0, sdc); return il; }
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { if (m == WM_DESTROY) { PostQuitMessage(0); return 0; } return DefWindowProcW(h, m, w, l); }
static void pump(int ms) { DWORD t0 = GetTickCount(); MSG msg; do { while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } Sleep(5); } while (GetTickCount() - t0 < (DWORD)ms); }
static int hit(HWND lv, int x, int y, UINT *fl) { LVHITTESTINFO ht = { { x, y } }; int r = (int)SendMessageW(lv, LVM_HITTEST, 0, (LPARAM)&ht); if (fl) *fl = ht.flags; return r; }
static POINT pos(HWND lv, int k) { POINT p = { -1, -1 }; SendMessageW(lv, LVM_GETITEMPOSITION, k, (LPARAM)&p); return p; }
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
  AllocConsole(); freopen("CONOUT$", "w", stdout); INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_LISTVIEW_CLASSES }; InitCommonControlsEx(&icc);
  WNDCLASSW wc = { 0 }; wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = L"LvHost"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassW(&wc);
  HWND host = CreateWindowExW(0, L"LvHost", L"lvview", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 660, 460, 0, 0, hi, 0);
  HWND lv = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_ICON | LVS_SHOWSELALWAYS, 10, 10, 330, 180, host, (HMENU)100, hi, 0);
  HWND lv2 = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_LIST, 350, 10, 200, 180, host, (HMENU)101, hi, 0);
  HWND lv3 = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_SMALLICON, 10, 200, 330, 110, host, (HMENU)102, hi, 0);
  HWND lv4 = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT, 350, 200, 290, 110, host, (HMENU)103, hi, 0);
  HIMAGELIST big = make_iml(32, 32, 4), small = make_iml(16, 16, 4); HWND all[4] = { lv, lv2, lv3, lv4 };
  for (int q = 0; q < 4; q++) { SendMessageW(all[q], LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)big); SendMessageW(all[q], LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)small); LVCOLUMNW col = { LVCF_TEXT | LVCF_WIDTH, 0, 150, (LPWSTR)L"Name" }; SendMessageW(all[q], LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
    for (int k = 0; k < 30; k++) { wchar_t t[64]; if (k == 2 && q == 0) wsprintfW(t, L"A rather long file name number %d.txt", k); else wsprintfW(t, L"Item %02d", k); LVITEMW it = { LVIF_TEXT | LVIF_IMAGE, k, 0, 0, 0, t, 0, k % 4 }; SendMessageW(all[q], LVM_INSERTITEMW, 0, (LPARAM)&it); } }
  pump(300);
  /* ---- icon view geometry ---- */
  CHECK(SendMessageW(lv, LVM_GETVIEW, 0, 0) == LV_VIEW_ICON, "GETVIEW icon");
  DWORD sp = (DWORD)SendMessageW(lv, LVM_GETITEMSPACING, FALSE, 0); int cellw = LOWORD(sp), cellh = HIWORD(sp); CHECK(cellw >= 60 && cellw <= 120 && cellh >= 50 && cellh <= 120, "icon spacing %dx%d", cellw, cellh);
  POINT p0 = pos(lv, 0), p1 = pos(lv, 1); CHECK(p1.y == p0.y && p1.x - p0.x == cellw, "icon items flow left to right: (%ld,%ld) (%ld,%ld)", p0.x, p0.y, p1.x, p1.y);
  int cols = (330 - 4) / cellw; if (cols < 1) cols = 1; int perpage = (int)SendMessageW(lv, LVM_GETCOUNTPERPAGE, 0, 0); POINT pc = pos(lv, cols); CHECK(pc.x == p0.x && pc.y - p0.y == cellh, "icon row wrap after %d columns: (%ld,%ld)", cols, pc.x, pc.y);
  CHECK(perpage % cols == 0 && perpage >= cols, "GETCOUNTPERPAGE icon=%d (cols=%d)", perpage, cols);
  UINT fl = 0; int h0 = hit(lv, (int)p0.x + cellw / 2, (int)p0.y + 10, &fl); CHECK(h0 == 0 && fl == LVHT_ONITEMICON, "hit icon of item 0 → %d flags=%x", h0, fl);
  RECT rl = { LVIR_LABEL }; SendMessageW(lv, LVM_GETITEMRECT, 1, (LPARAM)&rl); int h1 = hit(lv, (rl.left + rl.right) / 2, (rl.top + rl.bottom) / 2, &fl); CHECK(h1 == 1 && fl == LVHT_ONITEMLABEL, "hit label of item 1 → %d flags=%x (rect %ld,%ld-%ld,%ld)", h1, fl, rl.left, rl.top, rl.right, rl.bottom);
  RECT rb = { LVIR_BOUNDS }; SendMessageW(lv, LVM_GETITEMRECT, 5, (LPARAM)&rb); CHECK(rb.right - rb.left == cellw && rb.bottom - rb.top == cellh, "LVIR_BOUNDS = cell (%ld x %ld)", rb.right - rb.left, rb.bottom - rb.top);
  CHECK(hit(lv, 2 + cols * cellw + 5, 10, &fl) == -1, "hit beyond the last column → nowhere");
  /* ensure visible scrolls by rows */ SendMessageW(lv, LVM_ENSUREVISIBLE, 29, FALSE); int top = (int)SendMessageW(lv, LVM_GETTOPINDEX, 0, 0); POINT o; SendMessageW(lv, LVM_GETORIGIN, 0, (LPARAM)&o); CHECK(top > 0 && top % cols == 0 && o.y < 0 && (-o.y) % cellh == 0, "icon ENSUREVISIBLE(29): top=%d origin.y=%ld", top, o.y);
  SendMessageW(lv, LVM_ENSUREVISIBLE, 0, FALSE); CHECK(SendMessageW(lv, LVM_GETTOPINDEX, 0, 0) == 0, "back to top");
  /* arrows: right = +1, down = +cols */ LVITEMW st = { 0 }; st.stateMask = LVIS_SELECTED | LVIS_FOCUSED; st.state = LVIS_SELECTED | LVIS_FOCUSED; SendMessageW(lv, LVM_SETITEMSTATE, 0, (LPARAM)&st); SetFocus(lv); pump(50);
  SendMessageW(lv, WM_KEYDOWN, VK_RIGHT, 0); pump(50); CHECK(SendMessageW(lv, LVM_GETNEXTITEM, -1, LVNI_FOCUSED) == 1, "icon VK_RIGHT → 1 (got %d)", (int)SendMessageW(lv, LVM_GETNEXTITEM, -1, LVNI_FOCUSED));
  SendMessageW(lv, WM_KEYDOWN, VK_DOWN, 0); pump(50); CHECK(SendMessageW(lv, LVM_GETNEXTITEM, -1, LVNI_FOCUSED) == 1 + cols, "icon VK_DOWN → %d (got %d)", 1 + cols, (int)SendMessageW(lv, LVM_GETNEXTITEM, -1, LVNI_FOCUSED));
  /* icon spacing */ DWORD old = (DWORD)SendMessageW(lv, LVM_SETICONSPACING, 0, MAKELONG(100, 90)); DWORD now = (DWORD)SendMessageW(lv, LVM_GETITEMSPACING, FALSE, 0); CHECK(LOWORD(old) == cellw && LOWORD(now) == 100 && HIWORD(now) == 90, "SETICONSPACING old=%ux%u now=%ux%u", LOWORD(old), HIWORD(old), LOWORD(now), HIWORD(now)); SendMessageW(lv, LVM_SETICONSPACING, 0, MAKELONG(-1, -1));
  /* ---- list view: columns top to bottom ---- */
  POINT q0 = pos(lv2, 0), q1 = pos(lv2, 1); int ih = (int)(q1.y - q0.y); CHECK(q1.x == q0.x && ih > 10 && ih < 40, "list items flow top to bottom: (%ld,%ld) (%ld,%ld)", q0.x, q0.y, q1.x, q1.y);
  int rows = (180 - 2 - 4) / (ih ? ih : 1); POINT qr = pos(lv2, rows); int colw = (int)SendMessageW(lv2, LVM_GETCOLUMNWIDTH, 0, 0); CHECK(qr.y == q0.y && qr.x - q0.x == colw && colw > 60, "list column wrap after %d rows: (%ld,%ld) colw=%d", rows, qr.x, qr.y, colw);
  CHECK(hit(lv2, (int)qr.x + 30, (int)qr.y + ih / 2, &fl) == rows, "list hit test column 2 row 0 → %d", hit(lv2, (int)qr.x + 30, (int)qr.y + ih / 2, &fl));
  SendMessageW(lv2, LVM_ENSUREVISIBLE, 29, FALSE); SendMessageW(lv2, LVM_GETORIGIN, 0, (LPARAM)&o); CHECK(o.x < 0 && o.y == 0 && (-o.x) % colw == 0, "list scrolls horizontally: origin=(%ld,%ld) top=%d perpage=%d colw=%d", o.x, o.y, (int)SendMessageW(lv2, LVM_GETTOPINDEX, 0, 0), (int)SendMessageW(lv2, LVM_GETCOUNTPERPAGE, 0, 0), colw); SendMessageW(lv2, LVM_ENSUREVISIBLE, 0, FALSE);
  st.state = LVIS_SELECTED | LVIS_FOCUSED; SendMessageW(lv2, LVM_SETITEMSTATE, 0, (LPARAM)&st); SetFocus(lv2); pump(50); SendMessageW(lv2, WM_KEYDOWN, VK_RIGHT, 0); pump(50); CHECK(SendMessageW(lv2, LVM_GETNEXTITEM, -1, LVNI_FOCUSED) == rows, "list VK_RIGHT → %d (got %d)", rows, (int)SendMessageW(lv2, LVM_GETNEXTITEM, -1, LVNI_FOCUSED));
  SendMessageW(lv2, WM_KEYDOWN, VK_DOWN, 0); pump(50); CHECK(SendMessageW(lv2, LVM_GETNEXTITEM, -1, LVNI_FOCUSED) == rows + 1, "list VK_DOWN → %d", rows + 1);
  SendMessageW(lv2, LVM_SETCOLUMNWIDTH, 0, 120); CHECK(SendMessageW(lv2, LVM_GETCOLUMNWIDTH, 0, 0) == 120 && pos(lv2, rows).x - pos(lv2, 0).x == 120, "list SETCOLUMNWIDTH 120");
  /* ---- small icon grid + report, SETVIEW switches ---- */
  POINT s0 = pos(lv3, 0), s1 = pos(lv3, 1); CHECK(s1.y == s0.y && s1.x > s0.x, "small icon grid flows left to right");
  POINT r0 = pos(lv4, 0), r1 = pos(lv4, 1); CHECK(r1.x == r0.x && r1.y > r0.y && r0.y > 10, "report rows under the header");
  SendMessageW(lv4, LVM_SETVIEW, LV_VIEW_ICON, 0); pump(50); POINT v1 = pos(lv4, 1); CHECK(SendMessageW(lv4, LVM_GETVIEW, 0, 0) == LV_VIEW_ICON && v1.y == pos(lv4, 0).y && v1.x > pos(lv4, 0).x, "SETVIEW icon re-lays out");
  SendMessageW(lv4, LVM_SETVIEW, LV_VIEW_DETAILS, 0); pump(50); CHECK(pos(lv4, 1).x == pos(lv4, 0).x, "SETVIEW details back to rows");
  pump(1200);
  printf("%s %d/%d\n", g_fail ? "LVVIEW FAIL" : "LVVIEW OK", g_ok, g_ok + g_fail); fflush(stdout); DestroyWindow(host); return g_fail ? 1 : 0; }
