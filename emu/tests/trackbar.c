/* trackbar.c — msctls_trackbar32 and msctls_updown32: range/position/tick/selection messages, thumb and channel
   rectangles, keyboard and mouse (channel click pages, thumb drag tracks) with WM_HSCROLL notifications, up-down
   auto-buddy alignment, UDS_SETBUDDYINT text (thousands, hex), UDM_GETPOS32 from the buddy text, UDN_DELTAPOS +
   WM_VSCROLL on an arrow click, UDS_ARROWKEYS from the buddy edit, acceleration table round trip.
   Driver: --click 262,25@1500 --click 103,65@3000 --key 40@4200
   zig cc -target x86_64-windows-gnu -O1 -o trackbar.exe trackbar.c -luser32 -lgdi32 -lcomctl32 */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
static int g_ok = 0, g_fail = 0; static int g_hs[64], g_nhs = 0, g_hspos[64]; static int g_vs[64], g_nvs = 0, g_vspos[64]; static int g_delta = 0, g_delta_pos = -1, g_delta_delta = 0; static int g_released = 0;
#define CHECK(cond, ...) do { if (cond) g_ok++; else { g_fail++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_HSCROLL) { if (g_nhs < 64) { g_hs[g_nhs] = LOWORD(w); g_hspos[g_nhs] = HIWORD(w); g_nhs++; } return 0; }
  if (m == WM_VSCROLL) { if (g_nvs < 64) { g_vs[g_nvs] = LOWORD(w); g_vspos[g_nvs] = HIWORD(w); g_nvs++; } return 0; }
  if (m == WM_NOTIFY) { NMHDR *nm = (NMHDR *)l; if (nm->code == UDN_DELTAPOS) { NMUPDOWN *ud = (NMUPDOWN *)l; g_delta++; g_delta_pos = ud->iPos; g_delta_delta = ud->iDelta; return 0; } if (nm->code == NM_RELEASEDCAPTURE) g_released++; }
  if (m == WM_DESTROY) { PostQuitMessage(0); return 0; } return DefWindowProcW(h, m, w, l); }
static void pump(int ms) { DWORD t0 = GetTickCount(); MSG msg; do { while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } Sleep(5); } while (GetTickCount() - t0 < (DWORD)ms); }
static int has_code(int *a, int n, int code) { for (int k = 0; k < n; k++) if (a[k] == code) return 1; return 0; }
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
  AllocConsole(); freopen("CONOUT$", "w", stdout); INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_BAR_CLASSES | ICC_UPDOWN_CLASS }; InitCommonControlsEx(&icc);
  WNDCLASSW wc = { 0 }; wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = L"TrkHost"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc);
  HWND host = CreateWindowExW(0, L"TrkHost", L"trackbar", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 500, 360, 0, 0, hi, 0);
  HWND tb = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS | TBS_ENABLESELRANGE, 10, 10, 300, 30, host, (HMENU)100, hi, 0);
  HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 10, 60, 100, 22, host, (HMENU)200, hi, 0);
  HWND ud = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_AUTOBUDDY | UDS_ARROWKEYS, 0, 0, 16, 22, host, (HMENU)201, hi, 0);
  HWND ed2 = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", L"", WS_CHILD | WS_VISIBLE, 10, 100, 100, 22, host, (HMENU)202, hi, 0);
  HWND ud2 = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_NOTHOUSANDS | UDS_WRAP, 0, 0, 16, 22, host, (HMENU)203, hi, 0);
  SendMessageW(ud2, UDM_SETBUDDY, (WPARAM)ed2, 0);
  CHECK(tb && ed && ud && ed2 && ud2, "controls created");
  wchar_t cls[40] = L""; GetClassNameW(tb, cls, 40); CHECK(!lstrcmpiW(cls, L"msctls_trackbar32"), "trackbar class name (got %ls)", cls);
  /* ---- trackbar messages ---- */
  SendMessageW(tb, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100)); SendMessageW(tb, TBM_SETPOS, TRUE, 50); SendMessageW(tb, TBM_SETPAGESIZE, 0, 20); SendMessageW(tb, TBM_SETLINESIZE, 0, 1); SendMessageW(tb, TBM_SETTICFREQ, 10, 0);
  CHECK(SendMessageW(tb, TBM_GETPOS, 0, 0) == 50 && SendMessageW(tb, TBM_GETRANGEMIN, 0, 0) == 0 && SendMessageW(tb, TBM_GETRANGEMAX, 0, 0) == 100, "range 0..100, pos 50");
  CHECK(SendMessageW(tb, TBM_GETPAGESIZE, 0, 0) == 20 && SendMessageW(tb, TBM_GETLINESIZE, 0, 0) == 1, "page 20, line 1");
  RECT th = { 0 }, chn = { 0 }; SendMessageW(tb, TBM_GETTHUMBRECT, 0, (LPARAM)&th); SendMessageW(tb, TBM_GETCHANNELRECT, 0, (LPARAM)&chn);
  CHECK(th.right > th.left && th.bottom > th.top && th.left > 130 && th.right < 170, "thumb rect near the middle for pos 50 (%ld,%ld-%ld,%ld)", th.left, th.top, th.right, th.bottom);
  CHECK(chn.right - chn.left > 250 && chn.bottom - chn.top >= 3 && chn.top >= th.top && chn.bottom <= th.bottom, "channel spans the control and sits inside the thumb's height (%ld,%ld-%ld,%ld)", chn.left, chn.top, chn.right, chn.bottom);
  CHECK(SendMessageW(tb, TBM_GETNUMTICS, 0, 0) == 11, "11 auto ticks at frequency 10 (got %d)", (int)SendMessageW(tb, TBM_GETNUMTICS, 0, 0));
  SendMessageW(tb, TBM_SETTIC, 0, 42); CHECK(SendMessageW(tb, TBM_GETTIC, 0, 0) == 42 && (int)SendMessageW(tb, TBM_GETTICPOS, 0, 0) > chn.left + 50, "TBM_SETTIC/GETTIC/GETTICPOS (pos %d)", (int)SendMessageW(tb, TBM_GETTICPOS, 0, 0));
  SendMessageW(tb, TBM_SETSEL, TRUE, MAKELPARAM(20, 60)); CHECK(SendMessageW(tb, TBM_GETSELSTART, 0, 0) == 20 && SendMessageW(tb, TBM_GETSELEND, 0, 0) == 60, "selection range 20..60");
  SendMessageW(tb, TBM_SETTHUMBLENGTH, 24, 0); CHECK(SendMessageW(tb, TBM_GETTHUMBLENGTH, 0, 0) == 24, "thumb length 24");
  SendMessageW(tb, TBM_SETPOS, TRUE, 250); CHECK(SendMessageW(tb, TBM_GETPOS, 0, 0) == 100, "TBM_SETPOS clamps to the range");
  /* ---- keyboard ---- */
  SetFocus(tb); pump(50); g_nhs = 0; SendMessageW(tb, WM_KEYDOWN, VK_HOME, 0); SendMessageW(tb, WM_KEYDOWN, VK_RIGHT, 0); pump(50);
  CHECK(SendMessageW(tb, TBM_GETPOS, 0, 0) == 1, "Home then Right -> pos 1 (got %d)", (int)SendMessageW(tb, TBM_GETPOS, 0, 0));
  CHECK(g_nhs >= 3 && g_hs[0] == TB_TOP && g_hs[1] == TB_ENDTRACK && g_hs[2] == TB_LINEDOWN, "WM_HSCROLL TB_TOP, TB_ENDTRACK, TB_LINEDOWN (%d: %d %d %d)", g_nhs, g_hs[0], g_hs[1], g_hs[2]);
  SendMessageW(tb, WM_KEYDOWN, VK_NEXT, 0); pump(20); CHECK(SendMessageW(tb, TBM_GETPOS, 0, 0) == 21 && has_code(g_hs, g_nhs, TB_PAGEDOWN), "PgDn -> pos 21 with TB_PAGEDOWN");
  SendMessageW(tb, WM_KEYDOWN, VK_END, 0); pump(20); CHECK(SendMessageW(tb, TBM_GETPOS, 0, 0) == 100 && has_code(g_hs, g_nhs, TB_BOTTOM), "End -> pos 100 with TB_BOTTOM");
  SendMessageW(tb, WM_KEYDOWN, VK_HOME, 0); pump(20); g_nhs = 0;
  /* ---- mouse: the driver clicks the channel at x=262 (right of the thumb) at 1500 ms -> one page towards the pointer ---- */
  pump(1800);
  CHECK(SendMessageW(tb, TBM_GETPOS, 0, 0) == 20, "channel click pages down: pos 20 (got %d)", (int)SendMessageW(tb, TBM_GETPOS, 0, 0));
  CHECK(has_code(g_hs, g_nhs, TB_PAGEDOWN) && has_code(g_hs, g_nhs, TB_ENDTRACK), "WM_HSCROLL TB_PAGEDOWN + TB_ENDTRACK for the click (%d codes)", g_nhs);
  /* ---- drag through messages: down on the thumb, move 55 px right, up ---- */
  SendMessageW(tb, TBM_GETTHUMBRECT, 0, (LPARAM)&th); int tx = (th.left + th.right) / 2, ty = (th.top + th.bottom) / 2; g_nhs = 0; g_released = 0;
  SendMessageW(tb, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(tx, ty)); CHECK(GetCapture() == tb, "thumb press captures the mouse");
  SendMessageW(tb, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(tx + 28, ty)); SendMessageW(tb, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(tx + 55, ty));
  int tracked = SendMessageW(tb, TBM_GETPOS, 0, 0); CHECK(tracked > 20 && tracked < 60 && g_nhs >= 2 && g_hs[0] == TB_THUMBTRACK && g_hs[g_nhs - 1] == TB_THUMBTRACK && g_hspos[g_nhs - 1] == tracked, "thumb drag tracks (pos %d, %d notifications, last code %d pos %d)", tracked, g_nhs, g_nhs ? g_hs[g_nhs - 1] : -1, g_nhs ? g_hspos[g_nhs - 1] : -1);
  SendMessageW(tb, WM_LBUTTONUP, 0, MAKELPARAM(tx + 55, ty)); pump(50);
  CHECK(GetCapture() == 0 && g_hs[g_nhs - 2] == TB_THUMBPOSITION && g_hs[g_nhs - 1] == TB_ENDTRACK && g_hspos[g_nhs - 2] == tracked && g_released == 1, "release: TB_THUMBPOSITION + TB_ENDTRACK, NM_RELEASEDCAPTURE, capture freed (codes %d %d rel %d)", g_hs[g_nhs - 2], g_hs[g_nhs - 1], g_released);
  /* ---- up-down: auto buddy, alignment ---- */
  CHECK((HWND)SendMessageW(ud, UDM_GETBUDDY, 0, 0) == ed, "UDS_AUTOBUDDY picks the previous sibling (the edit)");
  RECT er, ur; GetWindowRect(ed, &er); GetWindowRect(ud, &ur);
  CHECK(er.right <= ur.left + 1 && ur.right <= er.right + 18 && ur.top == er.top && ur.bottom == er.bottom, "UDS_ALIGNRIGHT: the up-down sits at the buddy's right edge (edit %ld-%ld, ud %ld-%ld)", er.left, er.right, ur.left, ur.right);
  SendMessageW(ud, UDM_SETRANGE32, 0, 20); int lo = -1, hh = -1; SendMessageW(ud, UDM_GETRANGE32, (WPARAM)&lo, (LPARAM)&hh); CHECK(lo == 0 && hh == 20, "UDM_SETRANGE32/GETRANGE32 (%d..%d)", lo, hh);
  SendMessageW(ud, UDM_SETPOS32, 0, 7); wchar_t t[64] = L""; GetWindowTextW(ed, t, 64); CHECK(!lstrcmpW(t, L"7") && SendMessageW(ud, UDM_GETPOS32, 0, 0) == 7, "UDM_SETPOS32 7 writes the buddy text (got %ls)", t);
  SetWindowTextW(ed, L"15"); BOOL err = 2; CHECK(SendMessageW(ud, UDM_GETPOS32, 0, (LPARAM)&err) == 15 && err == 0, "UDM_GETPOS32 reads the buddy text 15");
  SetWindowTextW(ed, L"abc"); err = 0; SendMessageW(ud, UDM_GETPOS32, 0, (LPARAM)&err); CHECK(err != 0, "UDM_GETPOS32 reports an error for non-numeric text"); SetWindowTextW(ed, L"15");
  CHECK(SendMessageW(ud, UDM_SETPOS32, 0, 99) == 15 && SendMessageW(ud, UDM_GETPOS32, 0, 0) == 20, "UDM_SETPOS32 clamps to 20 and returns the old position");
  SendMessageW(ud, UDM_SETPOS32, 0, 15);
  UDACCEL acc[3] = { { 0, 1 }, { 1, 4 }, { 3, 16 } }; SendMessageW(ud, UDM_SETACCEL, 3, (LPARAM)acc); UDACCEL got[3] = { 0 }; int na = (int)SendMessageW(ud, UDM_GETACCEL, 3, (LPARAM)got); CHECK(na == 3 && got[1].nSec == 1 && got[1].nInc == 4 && got[2].nInc == 16, "UDM_SETACCEL/GETACCEL round trip");
  /* the driver clicks the upper arrow of the up-down at 3000 ms */
  g_nvs = 0; g_delta = 0; pump(1700);
  GetWindowTextW(ed, t, 64);
  CHECK(g_delta == 1 && g_delta_pos == 15 && g_delta_delta == 1, "UDN_DELTAPOS once with iPos=15 iDelta=1 (n=%d pos=%d delta=%d)", g_delta, g_delta_pos, g_delta_delta);
  CHECK(!lstrcmpW(t, L"16") && SendMessageW(ud, UDM_GETPOS32, 0, 0) == 16, "arrow click -> 16 in the buddy (got %ls)", t);
  CHECK(g_nvs >= 2 && g_vs[0] == SB_THUMBPOSITION && g_vspos[0] == 16 && g_vs[g_nvs - 1] == SB_ENDSCROLL, "WM_VSCROLL SB_THUMBPOSITION(16) then SB_ENDSCROLL (%d: %d/%d %d)", g_nvs, g_vs[0], g_vspos[0], g_vs[g_nvs - 1]);
  /* UDS_ARROWKEYS: focus in the edit, the driver presses Down at 4200 ms */
  SetFocus(ed); g_nvs = 0; pump(1300); GetWindowTextW(ed, t, 64);
  CHECK(!lstrcmpW(t, L"15") && g_nvs >= 1 && g_vs[0] == SB_THUMBPOSITION && g_vspos[0] == 15, "Down arrow in the buddy edit -> 15 (got %ls, %d scrolls)", t, g_nvs);
  SendMessageW(ud, UDM_SETBASE, 16, 0); GetWindowTextW(ed, t, 64); CHECK(!lstrcmpW(t, L"0xF") && SendMessageW(ud, UDM_GETBASE, 0, 0) == 16, "base 16 shows 0xF (got %ls)", t); SendMessageW(ud, UDM_SETBASE, 10, 0);
  /* thousands separators and UDS_NOTHOUSANDS, UDS_WRAP */
  SendMessageW(ud, UDM_SETRANGE32, 0, 100000); SendMessageW(ud, UDM_SETPOS32, 0, 12345); GetWindowTextW(ed, t, 64); CHECK(!lstrcmpW(t, L"12,345"), "thousands separator (got %ls)", t);
  SendMessageW(ud2, UDM_SETRANGE32, 0, 100000); SendMessageW(ud2, UDM_SETPOS32, 0, 12345); GetWindowTextW(ed2, t, 64); CHECK(!lstrcmpW(t, L"12345"), "UDS_NOTHOUSANDS (got %ls)", t);
  SendMessageW(ud2, UDM_SETRANGE32, 0, 3); SendMessageW(ud2, UDM_SETPOS32, 0, 3); SendMessageW(ud2, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 4)); SendMessageW(ud2, WM_LBUTTONUP, 0, MAKELPARAM(8, 4)); pump(30); GetWindowTextW(ed2, t, 64);
  CHECK(!lstrcmpW(t, L"0") && SendMessageW(ud2, UDM_GETPOS32, 0, 0) == 0, "UDS_WRAP: up from 3 wraps to 0 (got %ls)", t);
  pump(200);
  printf("%s %d/%d\n", g_fail ? "TRACKBAR FAIL" : "TRACKBAR OK", g_ok, g_ok + g_fail); fflush(stdout); DestroyWindow(host); return g_fail ? 1 : 0; }
