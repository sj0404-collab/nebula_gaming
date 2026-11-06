/* ansisub.c — an ANSI application (Delphi VCL / MFC style) subclasses an EDIT and forwards string messages with CallWindowProcA:
   the built-in control must read/write ANSI (UTF-8 here) through that path, never UTF-16, and never past the caller's buffer. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
static WNDPROC g_old; static int g_seen;
static LRESULT CALLBACK Sub(HWND h, UINT m, WPARAM wp, LPARAM lp) { if (m == WM_GETTEXT || m == WM_SETTEXT) g_seen++; return CallWindowProcA(g_old, h, m, wp, lp); }
int main(void) {
  HWND top = CreateWindowExA(0, "STATIC", "host", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200, 0, 0, GetModuleHandleA(0), 0);
  HWND ed = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 10, 10, 200, 24, top, (HMENU)1, GetModuleHandleA(0), 0);
  g_old = (WNDPROC)SetWindowLongPtrA(ed, GWLP_WNDPROC, (LONG_PTR)Sub);
  const char *txt = "Папка (1) test";   /* UTF-8 source: Cyrillic + ASCII */
  int fails = 0;
  SendMessageA(ed, WM_SETTEXT, 0, (LPARAM)txt);                               /* app proc → CallWindowProcA(built-in) */
  char buf[64]; memset(buf, 0xAA, sizeof buf); int n = (int)SendMessageA(ed, WM_GETTEXT, 32, (LPARAM)buf);
  if (n != (int)strlen(txt) || strcmp(buf, txt)) { printf("FAIL WM_GETTEXT via subclass: n=%d '%s'\n", n, buf); fails++; }
  for (int i = n + 1; i < 64; i++) if ((unsigned char)buf[i] != 0xAA) { printf("FAIL overflow at %d\n", i); fails++; break; }
  int len = (int)SendMessageA(ed, WM_GETTEXTLENGTH, 0, 0); if (len != (int)strlen(txt)) { printf("FAIL WM_GETTEXTLENGTH %d\n", len); fails++; }
  char small[6]; memset(small, 0xBB, sizeof small); n = (int)SendMessageA(ed, WM_GETTEXT, 5, (LPARAM)small);
  if (n > 4 || small[5] != (char)0xBB) { printf("FAIL truncation n=%d\n", n); fails++; }
  char direct[64]; GetWindowTextA(ed, direct, sizeof direct); if (strcmp(direct, txt)) { printf("FAIL GetWindowTextA '%s'\n", direct); fails++; }
  WCHAR wide[64]; GetWindowTextW(ed, wide, 64); if (wide[0] != 0x41f || wide[5] != L' ') { printf("FAIL GetWindowTextW %x\n", wide[0]); fails++; }
  if (g_seen < 2) { printf("FAIL subclass not reached (%d)\n", g_seen); fails++; }
  printf(fails ? "ANSISUB FAILED %d\n" : "ANSISUB OK\n", fails); return fails ? 1 : 0;
}
