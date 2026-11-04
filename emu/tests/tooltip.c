/* tooltip.c — tooltips_class32 smoke test (x86 and x64): a button tool (TTF_IDISHWND|TTF_SUBCLASS, literal text), a
   rectangle tool answered through TTN_GETDISPINFOW, and a TBSTYLE_TOOLTIPS toolbar whose button tips come through the
   parent's WM_NOTIFY.  Driven by x64run: --move 150,75@1000 --move 400,80@2600 --move 80,12@4200 --move 500,300@5800
   Build: zig cc -target x86_64-windows-gnu -O1 -o tooltip.exe tooltip.c -luser32 -lgdi32 -lcomctl32 (same with x86-windows-gnu) */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
static int g_ok, g_fail, g_phase, g_cb_rect, g_cb_tb, g_cb_tb_id; static HWND g_tip, g_btn, g_tb, g_tbtip;
static void chk(int cond, const char *what) { printf("%s %s\n", cond ? "ok  " : "FAIL", what); if (cond) g_ok++; else g_fail++; fflush(stdout); }
static int tip_visible(HWND t, RECT *r) { if (!t || !IsWindowVisible(t)) return 0; GetWindowRect(t, r); return r->right > r->left && r->bottom > r->top; }
static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  switch (m) {
    case WM_NOTIFY: { NMHDR *hdr = (NMHDR *)lp;
      if (hdr->code == TTN_GETDISPINFOW) { NMTTDISPINFOW *di = (NMTTDISPINFOW *)lp;
        if (hdr->hwndFrom == g_tip && hdr->idFrom == 7) { g_cb_rect++; lstrcpyW(di->szText, L"Callback text for tool 7"); di->lpszText = di->szText; return 0; }
        if (hdr->hwndFrom == g_tbtip) { g_cb_tb++; g_cb_tb_id = (int)hdr->idFrom; return 0; /* leave it empty: the toolbar falls back to the button text */ } }
      if (hdr->code == TTN_GETDISPINFOA) { NMTTDISPINFOA *di = (NMTTDISPINFOA *)lp; if (hdr->hwndFrom == g_tip && hdr->idFrom == 7) { g_cb_rect++; lstrcpyA(di->szText, "Callback text for tool 7"); di->lpszText = di->szText; } if (hdr->hwndFrom == g_tbtip) { g_cb_tb++; g_cb_tb_id = (int)hdr->idFrom; } return 0; }
      return 0; }
    case WM_TIMER: { g_phase++; RECT r; POINT pt; GetCursorPos(&pt);
      if (g_phase == 1) { /* t ≈ 2.0 s: pointer rests on the button since 1.0 s */ int vis = tip_visible(g_tip, &r); chk(vis, "button tool: tooltip window visible after the initial delay");
        chk(vis && r.top >= pt.y && r.top <= pt.y + 40 && r.left <= pt.x && r.right > pt.x, "button tool: bubble sits just below the cursor");
        TOOLINFOW ti; ZeroMemory(&ti, sizeof ti); ti.cbSize = sizeof ti; LRESULT cur = SendMessageW(g_tip, TTM_GETCURRENTTOOL, 0, (LPARAM)&ti); chk(cur && ti.uId == (UINT_PTR)g_btn && (ti.uFlags & TTF_IDISHWND), "TTM_GETCURRENTTOOL reports the button (TTF_IDISHWND)");
        wchar_t buf[128] = L""; ti.lpszText = buf; SendMessageW(g_tip, TTM_GETTEXTW, 128, (LPARAM)&ti); chk(lstrcmpW(buf, L"Press me to continue") == 0, "TTM_GETTEXTW returns the literal tool text");
        chk(vis && r.right - r.left > 60 && r.bottom - r.top >= 14 && r.bottom - r.top < 40, "bubble size fits one line of text"); }
      else if (g_phase == 2) { /* t ≈ 3.6 s: pointer on the rectangle tool */ int vis = tip_visible(g_tip, &r); chk(g_cb_rect >= 1, "rectangle tool: TTN_GETDISPINFO asked the owner for the text"); chk(vis, "rectangle tool: tooltip visible");
        TOOLINFOW ti; ZeroMemory(&ti, sizeof ti); ti.cbSize = sizeof ti; LRESULT cur = SendMessageW(g_tip, TTM_GETCURRENTTOOL, 0, (LPARAM)&ti); chk(cur && ti.uId == 7 && !(ti.uFlags & TTF_IDISHWND), "TTM_GETCURRENTTOOL reports tool 7 (rectangle)");
        chk(vis && r.right - r.left > 120, "callback text (24 chars) widened the bubble"); }
      else if (g_phase == 3) { /* t ≈ 5.2 s: pointer on toolbar button 2 */ int vis = tip_visible(g_tbtip, &r); chk(g_tbtip != NULL && IsWindow(g_tbtip), "TB_GETTOOLTIPS returned a tooltip window");
        chk(g_cb_tb >= 1 && g_cb_tb_id == 102, "toolbar tip: TTN_GETDISPINFO forwarded to the parent with the button id (102)"); chk(vis, "toolbar tip visible (text taken from the button label)");
        chk(!tip_visible(g_tip, &r), "the first tooltip window hid when the pointer left its tools"); }
      else if (g_phase == 4) { /* t ≈ 6.8 s: pointer far away */ chk(!tip_visible(g_tbtip, &r) && !tip_visible(g_tip, &r), "no tooltip visible once the pointer rests outside every tool");
        chk(SendMessageW(g_tip, TTM_GETTOOLCOUNT, 0, 0) == 2, "TTM_GETTOOLCOUNT == 2");
        TOOLINFOW ti; ZeroMemory(&ti, sizeof ti); ti.cbSize = sizeof ti; ti.hwnd = h; ti.uId = 7; SendMessageW(g_tip, TTM_DELTOOLW, 0, (LPARAM)&ti); chk(SendMessageW(g_tip, TTM_GETTOOLCOUNT, 0, 0) == 1, "TTM_DELTOOL removed tool 7");
        chk(SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, 300) == -1 && SendMessageW(g_tip, TTM_GETMAXTIPWIDTH, 0, 0) == 300, "TTM_SETMAXTIPWIDTH / GETMAXTIPWIDTH");
        printf("%s %d/%d\n", g_fail ? "FAILED" : "ALL OK", g_ok, g_ok + g_fail); fflush(stdout); KillTimer(h, 1); DestroyWindow(h); }
      return 0; }
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(h, m, wp, lp);
}
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
  INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_BAR_CLASSES }; InitCommonControlsEx(&icc);
  WNDCLASSW wc; ZeroMemory(&wc, sizeof wc); wc.lpfnWndProc = wnd_proc; wc.hInstance = hi; wc.lpszClassName = L"TipTest"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.hCursor = LoadCursor(NULL, IDC_ARROW); RegisterClassW(&wc);
  HWND h = CreateWindowExW(0, L"TipTest", L"Tooltips", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 640, 400, NULL, NULL, hi, NULL);
  g_btn = CreateWindowExW(0, L"BUTTON", L"Hover", WS_CHILD | WS_VISIBLE, 40, 60, 160, 30, h, (HMENU)1, hi, NULL);
  g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, h, NULL, hi, NULL);
  TOOLINFOW ti; ZeroMemory(&ti, sizeof ti); ti.cbSize = sizeof ti; ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS; ti.hwnd = h; ti.uId = (UINT_PTR)g_btn; ti.lpszText = (LPWSTR)L"Press me to continue"; SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
  ZeroMemory(&ti, sizeof ti); ti.cbSize = sizeof ti; ti.uFlags = TTF_SUBCLASS; ti.hwnd = h; ti.uId = 7; ti.rect.left = 300; ti.rect.top = 50; ti.rect.right = 500; ti.rect.bottom = 110; ti.lpszText = LPSTR_TEXTCALLBACKW; SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
  g_tb = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL, WS_CHILD | WS_VISIBLE | TBSTYLE_TOOLTIPS | TBSTYLE_FLAT | TBSTYLE_LIST, 0, 0, 0, 0, h, (HMENU)2, hi, NULL);
  SendMessageW(g_tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0); SendMessageW(g_tb, TB_SETBITMAPSIZE, 0, MAKELPARAM(16, 16));
  TBBUTTON tbb[2]; ZeroMemory(tbb, sizeof tbb); tbb[0].iBitmap = I_IMAGENONE; tbb[0].idCommand = 101; tbb[0].fsState = TBSTATE_ENABLED; tbb[0].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT; tbb[0].iString = (INT_PTR)L"Open"; tbb[1] = tbb[0]; tbb[1].idCommand = 102; tbb[1].iString = (INT_PTR)L"Save all";
  SendMessageW(g_tb, TB_ADDBUTTONSW, 2, (LPARAM)tbb); SendMessageW(g_tb, TB_AUTOSIZE, 0, 0); g_tbtip = (HWND)SendMessageW(g_tb, TB_GETTOOLTIPS, 0, 0);
  Sleep(400); SetTimer(h, 1, 1600, NULL); /* checks fall at 2.0 / 3.6 / 5.2 / 6.8 s */
  MSG msg; while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  return g_fail ? 1 : 0;
}
