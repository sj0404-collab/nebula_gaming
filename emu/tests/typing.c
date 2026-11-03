/* typing.exe: host text input test. An EDIT control gets focus; the host types text through x64_char (--type "…@ms") and the
   program reports what arrived: the edit text, the number of WM_KEYDOWN seen by the edit (subclass) and the WM_CHAR codes. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
static int g_keydown, g_char, g_wait = 2500; static WNDPROC g_old; static wchar_t g_chars[64]; static int g_nchars;
static LRESULT CALLBACK edit_sub(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  if (m == WM_KEYDOWN) g_keydown++;
  if (m == WM_CHAR) { g_char++; if (g_nchars < 63) g_chars[g_nchars++] = (wchar_t)wp; }
  return CallWindowProcW(g_old, h, m, wp, lp);
}
static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  static HWND edit;
  switch (m) {
    case WM_CREATE: edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 10, 10, 300, 24, h, (HMENU)1, NULL, NULL); g_old = (WNDPROC)SetWindowLongPtrW(edit, GWLP_WNDPROC, (LONG_PTR)edit_sub); SetFocus(edit); SetTimer(h, 1, g_wait, NULL); return 0;
    case WM_TIMER: { wchar_t buf[128]; GetWindowTextW(edit, buf, 128); char out[512]; WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof out, NULL, NULL);
      printf("text=[%s] len=%d keydown=%d char=%d\n", out, (int)wcslen(buf), g_keydown, g_char); printf("chars:"); for (int i = 0; i < g_nchars; i++) printf(" %04x", g_chars[i]); printf("\n"); fflush(stdout);
      printf("%s\n", (wcscmp(buf, L"Hello, Nebula! 123 \x041f\x0440\x0438\x0432\x0435\x0442 {x}") == 0 && g_keydown >= 19 && g_char >= 25) ? "TYPING OK" : "TYPING FAIL"); fflush(stdout); PostQuitMessage(0); return 0; }
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(h, m, wp, lp);
}
int main(int argc, char **argv) {
  if (argc > 1 && atoi(argv[1]) > 0) g_wait = atoi(argv[1]);   /* how long to wait for the host to type (ms) */
  WNDCLASSW wc; memset(&wc, 0, sizeof wc); wc.lpfnWndProc = wnd_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"NebulaTyping"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc);
  HWND h = CreateWindowExW(0, L"NebulaTyping", L"typing", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 340, 100, NULL, NULL, wc.hInstance, NULL); (void)h;
  MSG msg; while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  return 0;
}
