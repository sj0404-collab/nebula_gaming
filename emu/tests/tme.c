/* TrackMouseEvent: WM_MOUSEHOVER / WM_MOUSELEAVE delivery, TME_QUERY, TME_CANCEL, immediate leave when the pointer is elsewhere.
   Driven by: --move 150,100@800 (into the child) --move 400,300@2400 (out, onto the parent) --move 150,100@3400 --move 50,34@4600 (onto the button) --move 400,300@5000 (off it) */
#include <windows.h>
#include <stdio.h>
static int ok_n = 0, fail_n = 0;
static void check(const char *what, int cond) { printf("%s %s\n", cond ? "ok  " : "FAIL", what); if (cond) ok_n++; else fail_n++; fflush(stdout); }
static int child_moves, child_hover, child_leave, parent_moves, parent_leave, btn_leave_seen;
static HWND g_child, g_btn, g_main;
static WNDPROC g_btn_orig;
static LRESULT CALLBACK BtnProc(HWND h, UINT m, WPARAM wp, LPARAM lp) { if (m == WM_MOUSELEAVE) btn_leave_seen++; if (m == WM_MOUSEMOVE) { TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent(&t); } return CallWindowProc(g_btn_orig, h, m, wp, lp); }
static LRESULT CALLBACK ChildProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  switch (m) {
    case WM_MOUSEMOVE: { child_moves++; TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE | TME_HOVER, h, 300 }; TrackMouseEvent(&t); return 0; }
    case WM_MOUSEHOVER: child_hover++; return 0;
    case WM_MOUSELEAVE: child_leave++; return 0;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT r; GetClientRect(h, &r); FillRect(dc, &r, (HBRUSH)GetStockObject(LTGRAY_BRUSH)); EndPaint(h, &ps); return 0; }
  }
  return DefWindowProc(h, m, wp, lp);
}
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  switch (m) {
    case WM_MOUSEMOVE: parent_moves++; return 0;
    case WM_MOUSELEAVE: parent_leave++; return 0;
    case WM_TIMER:
      if (wp == 1) { /* 1.6 s: pointer rested in the child */ KillTimer(h, 1);
        check("WM_MOUSEMOVE reached the child", child_moves >= 1);
        check("WM_MOUSEHOVER after dwHoverTime (300 ms) at rest", child_hover == 1);
        check("no WM_MOUSELEAVE while the pointer stays", child_leave == 0);
        TRACKMOUSEEVENT q = { sizeof q, TME_QUERY, 0, 0 }; q.hwndTrack = g_child; BOOL r = TrackMouseEvent(&q);
        check("TME_QUERY succeeds", r); check("TME_QUERY: hover consumed, leave still tracked", (q.dwFlags & (TME_HOVER | TME_LEAVE)) == TME_LEAVE); check("TME_QUERY: dwHoverTime reported", q.dwHoverTime == 300);
        return 0; }
      if (wp == 2) { /* 3.0 s: pointer moved onto the parent */ KillTimer(h, 2);
        check("WM_MOUSELEAVE when the pointer left the child", child_leave == 1);
        check("parent gets WM_MOUSEMOVE", parent_moves >= 1);
        TRACKMOUSEEVENT q = { sizeof q, TME_QUERY, g_child, 0 }; TrackMouseEvent(&q); check("TME_QUERY: nothing tracked after leave", (q.dwFlags & (TME_HOVER | TME_LEAVE)) == 0);
        /* TME_LEAVE on a window the pointer is not over: WM_MOUSELEAVE arrives at once */
        TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, g_child, 0 }; TrackMouseEvent(&t); SetTimer(h, 5, 150, 0);
        return 0; }
      if (wp == 5) { KillTimer(h, 5); check("TME_LEAVE while the pointer is elsewhere: immediate WM_MOUSELEAVE", child_leave == 2); return 0; }
      if (wp == 3) { /* 4.0 s: back in the child, then cancel */ KillTimer(h, 3);
        check("re-entering the child restarts tracking (hover again)", child_hover == 2);
        TRACKMOUSEEVENT t = { sizeof t, TME_CANCEL | TME_LEAVE, g_child, 0 }; TrackMouseEvent(&t);
        TRACKMOUSEEVENT q = { sizeof q, TME_QUERY, g_child, 0 }; TrackMouseEvent(&q); check("TME_CANCEL clears TME_LEAVE", (q.dwFlags & TME_LEAVE) == 0);
        return 0; }
      if (wp == 4) { /* 5.4 s: pointer moved onto the button; leave was cancelled */ KillTimer(h, 4);
        check("cancelled tracking: no WM_MOUSELEAVE on the next exit", child_leave == 2);
        check("subclassed common control (Button): WM_MOUSELEAVE through the subclass", btn_leave_seen == 1);
        printf("TME %s %d/%d\n", fail_n ? "FAIL" : "OK", ok_n, ok_n + fail_n); fflush(stdout); PostQuitMessage(fail_n ? 1 : 0); return 0; }
      return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProc(h, m, wp, lp);
}
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
  WNDCLASSA wc = {0}; wc.lpfnWndProc = MainProc; wc.hInstance = hi; wc.lpszClassName = "TmeMain"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassA(&wc);
  WNDCLASSA cc = {0}; cc.lpfnWndProc = ChildProc; cc.hInstance = hi; cc.lpszClassName = "TmeChild"; RegisterClassA(&cc);
  g_main = CreateWindowExA(0, "TmeMain", "TrackMouseEvent", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 640, 480, 0, 0, hi, 0);
  g_child = CreateWindowExA(0, "TmeChild", "", WS_CHILD | WS_VISIBLE, 100, 60, 150, 120, g_main, (HMENU)1, hi, 0);
  g_btn = CreateWindowExA(0, "Button", "Hot", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 20, 60, 28, g_main, (HMENU)2, hi, 0);
  g_btn_orig = (WNDPROC)SetWindowLongPtr(g_btn, GWLP_WNDPROC, (LONG_PTR)BtnProc);
  SetTimer(g_main, 1, 1600, 0); SetTimer(g_main, 2, 3000, 0); SetTimer(g_main, 3, 4000, 0); SetTimer(g_main, 4, 5400, 0);
  MSG msg; while (GetMessage(&msg, 0, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessage(&msg); }
  return (int)msg.wParam;
}
