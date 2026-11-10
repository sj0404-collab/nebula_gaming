#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
typedef char *(__cdecl *narrow_fn)(void);
typedef wchar_t *(__cdecl *wide_fn)(void);
int main(int argc, char **argv) {
  HMODULE m = LoadLibraryA("api-ms-win-crt-runtime-l1-1-0.dll");
  narrow_fn n = m ? (narrow_fn)GetProcAddress(m, "_get_narrow_winmain_command_line") : NULL;
  wide_fn w = m ? (wide_fn)GetProcAddress(m, "_get_wide_winmain_command_line") : NULL;
  if (!n || !w) { puts("winmain-command: missing FAIL"); return 1; }
  const char *expected = argc > 1 ? "alpha beta" : "";
  const wchar_t *expectedw = argc > 1 ? L"alpha beta" : L"";
  if (!n() || !w() || strcmp(n(), expected) || wcscmp(w(), expectedw)) { puts("winmain-command: value FAIL"); return 2; }
  puts("winmain-command: PASS"); return 0;
}
