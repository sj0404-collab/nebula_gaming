/* Exercise the real UpdateLayeredWindow bitmap path, not just its return code. */
#include <windows.h>
#include <stdio.h>
static int filtered;
static LRESULT CALLBACK filterproc(int code, WPARAM wp, LPARAM lp) { MSG *msg = (MSG *)lp; if (code == 7 && wp == 0 && msg && msg->message == WM_USER + 3) filtered++; return 1; }
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b) { return DefWindowProcW(w,m,a,b); }
int main(void) {
  RECT rect = {100, 200, 400, 300}; POINT inside = {290, 232}, outside = {290, 50};
  if (!PtInRect(&rect, inside) || PtInRect(&rect, outside)) { puts("layered: POINT FAIL"); return 6; }
  HINSTANCE inst = GetModuleHandleW(NULL);
  WNDCLASSW wc = {0}; wc.lpfnWndProc = proc; wc.hInstance = inst; wc.lpszClassName = L"layer-test";
  if (!RegisterClassW(&wc)) return 1;
  HWND w = CreateWindowExW(WS_EX_LAYERED, wc.lpszClassName, L"layer", WS_POPUP, 20, 30, 64, 64, 0, 0, inst, 0);
  if (!w) return 2;
  typedef BOOL (WINAPI *ShellHookFn)(HWND);
  HMODULE user32 = LoadLibraryW(L"user32.dll");
  ShellHookFn reg = (ShellHookFn)GetProcAddress(user32, "RegisterShellHookWindow");
  ShellHookFn unreg = (ShellHookFn)GetProcAddress(user32, "DeregisterShellHookWindow");
  typedef BOOL (WINAPI *FilterFn)(LPMSG, int);
  FilterFn filter = (FilterFn)GetProcAddress(user32, "CallMsgFilterW");
  MSG filtered_msg = {0}; filtered_msg.message = WM_USER + 3;
  if (!filter || filter(&filtered_msg, 7)) { puts("layered: FILTER EMPTY FAIL"); return 9; }
  HHOOK hh = SetWindowsHookExW(WH_MSGFILTER, filterproc, NULL, GetCurrentThreadId());
  if (!hh || !filter(&filtered_msg, 7) || filtered != 1 || !UnhookWindowsHookEx(hh)) { puts("layered: FILTER HOOK FAIL"); return 10; }
  UINT shell_msg = RegisterWindowMessageW(L"SHELLHOOK");
  if (!reg || !unreg || !shell_msg || !reg(w)) { puts("layered: SHELL REGISTER FAIL"); return 7; }
  HDC dc = CreateCompatibleDC(NULL); BITMAPINFO bi = {0};
  if (!dc || GetGraphicsMode(dc) != GM_COMPATIBLE || SetGraphicsMode(dc, GM_ADVANCED) != GM_COMPATIBLE || GetGraphicsMode(dc) != GM_ADVANCED) { puts("layered: GRAPHICS MODE FAIL"); return 11; }
  if (GetDCBrushColor(dc) != RGB(255,255,255) || SetDCBrushColor(dc, RGB(10,20,30)) != RGB(255,255,255) || GetDCBrushColor(dc) != RGB(10,20,30) || SetDCPenColor(dc, RGB(30,20,10)) != RGB(0,0,0) || GetDCPenColor(dc) != RGB(30,20,10) || !GetStockObject(DC_PEN)) { puts("layered: DC COLORS FAIL"); return 14; }
  if (GetArcDirection(dc) != AD_COUNTERCLOCKWISE || SetArcDirection(dc, AD_CLOCKWISE) != AD_COUNTERCLOCKWISE || GetArcDirection(dc) != AD_CLOCKWISE || SetArcDirection(dc, 99) != 0) { puts("layered: ARC DIRECTION FAIL"); return 13; }
  if (!SaveDC(dc) || SetGraphicsMode(dc, GM_COMPATIBLE) != GM_ADVANCED || !RestoreDC(dc, -1) || GetGraphicsMode(dc) != GM_ADVANCED || SetGraphicsMode(dc, 99) != 0) { puts("layered: GRAPHICS SAVE FAIL"); return 12; }
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = 64; bi.bmiHeader.biHeight = -64;
  bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
  DWORD *bits = NULL; HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
  if (!bm || !bits || !SelectObject(dc, bm)) return 3;
  for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) bits[y*64+x] = x < 32 ? 0xffff0000u : 0xff00ff00u;
  bits[65] = 0; /* transparent pixel must not capture pointer input */
  POINT dst = {20, 30}, src = {0, 0}; SIZE size = {64,64}; BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
  if (!UpdateLayeredWindow(w, NULL, &dst, &size, dc, &src, 0, &blend, ULW_ALPHA)) { printf("layered: FAIL %lu\n", GetLastError()); return 4; }
  /* bitmap must survive destroying the source DC */
  DeleteDC(dc); DeleteObject(bm); ShowWindow(w, SW_SHOW);
  /* DefWindowProc's WM_PAINT must not erase the shared screen underneath the layer. */
  POINT transparent = {21, 31}, opaque = {30, 40};
  if (WindowFromPoint(transparent) == w || WindowFromPoint(opaque) != w) { puts("layered: HIT FAIL"); return 5; }
  InvalidateRect(w, NULL, TRUE);
  MSG msg; int notified = 0; while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { printf("shell: expected=%u msg=%u wp=%llu lp=%p wnd=%p\n", shell_msg, msg.message, (unsigned long long)msg.wParam, (void *)msg.lParam, (void *)w); if (msg.message == shell_msg && msg.wParam == 1 && (HWND)msg.lParam == w) notified = 1; TranslateMessage(&msg); DispatchMessageW(&msg); }
  int unregistered = unreg(w); if (!notified || !unregistered) { printf("layered: SHELL NOTIFY FAIL notified=%d unregistered=%d\n", notified, unregistered); return 8; }
  printf("layered: PASS\n"); Sleep(1000); return 0;
}
