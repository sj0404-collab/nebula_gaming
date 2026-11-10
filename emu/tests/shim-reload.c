/* Built-in control procedure must be registered in each new guest process. */
#include <windows.h>
#include <stdio.h>
int main(void) {
  WNDCLASSEXA wc = {0}; wc.cbSize = sizeof(wc);
  if (!GetClassInfoExA(NULL, "BUTTON", &wc) || !wc.lpfnWndProc) { puts("shim-reload: class FAIL"); return 1; }
  HWND h = CreateWindowExA(0, "BUTTON", "test", WS_OVERLAPPED | BS_CHECKBOX, 0, 0, 80, 40, NULL, NULL, GetModuleHandleA(NULL), NULL);
  if (!h) { puts("shim-reload: create FAIL"); return 2; }
  CallWindowProcA(wc.lpfnWndProc, h, BM_SETCHECK, BST_CHECKED, 0);
  if (CallWindowProcA(wc.lpfnWndProc, h, BM_GETCHECK, 0, 0) != BST_CHECKED) { puts("shim-reload: control FAIL"); return 3; }
  DestroyWindow(h);
  puts("shim-reload: PASS"); return 0;
}
