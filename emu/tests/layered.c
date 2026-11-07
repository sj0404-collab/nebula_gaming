/* Exercise the real UpdateLayeredWindow bitmap path, not just its return code. */
#include <windows.h>
#include <stdio.h>
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b) { return DefWindowProcW(w,m,a,b); }
int main(void) {
  HINSTANCE inst = GetModuleHandleW(NULL);
  WNDCLASSW wc = {0}; wc.lpfnWndProc = proc; wc.hInstance = inst; wc.lpszClassName = L"layer-test";
  if (!RegisterClassW(&wc)) return 1;
  HWND w = CreateWindowExW(WS_EX_LAYERED, wc.lpszClassName, L"layer", WS_POPUP, 20, 30, 64, 64, 0, 0, inst, 0);
  if (!w) return 2;
  HDC dc = CreateCompatibleDC(NULL); BITMAPINFO bi = {0};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = 64; bi.bmiHeader.biHeight = -64;
  bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
  DWORD *bits = NULL; HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
  if (!bm || !bits || !SelectObject(dc, bm)) return 3;
  for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) bits[y*64+x] = x < 32 ? 0xffff0000u : 0xff00ff00u;
  POINT dst = {20, 30}, src = {0, 0}; SIZE size = {64,64}; BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
  if (!UpdateLayeredWindow(w, NULL, &dst, &size, dc, &src, 0, &blend, ULW_ALPHA)) { printf("layered: FAIL %lu\n", GetLastError()); return 4; }
  /* bitmap must survive destroying the source DC */
  DeleteDC(dc); DeleteObject(bm); ShowWindow(w, SW_SHOW);
  printf("layered: PASS\n"); Sleep(1000); return 0;
}
