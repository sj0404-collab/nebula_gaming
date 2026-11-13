/* private fonts (AddFontResourceW / AddFontMemResourceEx / RemoveFontResourceW), rpcrt4 UUIDs, CRT _wfindfirst64i32 layout and tmpnam */
#include <windows.h>
#include <rpc.h>
#include <stdio.h>
#include <string.h>
#include <io.h>
typedef struct { unsigned attrib; __int64 tc, ta, tw; unsigned size; wchar_t name[260]; } wfd64i32;   /* _wfinddata64i32_t: name at 36 */
int main(void) {
  int added = AddFontResourceW(L"C:\\Windows\\Fonts\\ptsansb.ttf");
  HFONT bold = CreateFontW(-24, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"PT Sans");
  HFONT missing = CreateFontW(-24, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"No Such Family");
  HDC dc = GetDC(0); SIZE s1 = {0}, s2 = {0}; TEXTMETRICW tm = {0};
  SelectObject(dc, bold); GetTextExtentPoint32W(dc, L"Hamburgefonstiv", 15, &s1); GetTextMetricsW(dc, &tm);
  SelectObject(dc, missing); GetTextExtentPoint32W(dc, L"Hamburgefonstiv", 15, &s2);
  printf("addfont=%d private_width=%ld fallback_width=%ld tmHeight_ok=%d weight=%ld\n", added, (long)s1.cx, (long)s2.cx, tm.tmHeight >= 24 && tm.tmHeight <= 36, (long)tm.tmWeight);
  /* memory font: the regular PT Sans from a file image */
  HANDLE f = CreateFileW(L"C:\\Windows\\Fonts\\ptsans.ttf", GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0); DWORD n = 0, got = 0; HANDLE mh = 0; DWORD faces = 0;
  if (f != INVALID_HANDLE_VALUE) { n = GetFileSize(f, 0); void *buf = malloc(n); ReadFile(f, buf, n, &got, 0); CloseHandle(f); mh = AddFontMemResourceEx(buf, got, 0, &faces); free(buf); }
  printf("memfont=%d faces=%lu removed=%d memremoved=%d\n", mh != 0, (unsigned long)faces, RemoveFontResourceW(L"C:\\Windows\\Fonts\\ptsansb.ttf"), RemoveFontMemResourceEx(mh));
  /* UUIDs */
  UUID u; RPC_CSTR str = 0; UUID back; int ok = UuidCreate(&u) == RPC_S_OK && UuidToStringA(&u, &str) == RPC_S_OK && str && strlen((char *)str) == 36 && str[8] == '-' && str[23] == '-';
  ok = ok && UuidFromStringA(str, &back) == RPC_S_OK && !memcmp(&u, &back, 16) && (u.Data3 >> 12) == 4; int nil = 0; UuidIsNil(&back, (RPC_STATUS *)&nil);
  printf("uuid_ok=%d roundtrip_nil=%d free=%d\n", ok, nil, RpcStringFreeA(&str) == RPC_S_OK && str == 0);
  /* _wfindfirst64i32 layout */
  CreateDirectoryW(L"C:\\tmp\\ff", 0); HANDLE t = CreateFileW(L"C:\\tmp\\ff\\abc.txt", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0); WriteFile(t, "12345", 5, &got, 0); CloseHandle(t);
  HMODULE crt = GetModuleHandleA("msvcrt.dll"); intptr_t (__cdecl *ff)(const wchar_t *, wfd64i32 *) = (void *)GetProcAddress(crt, "_wfindfirst64i32"); int (__cdecl *fc)(intptr_t) = (void *)GetProcAddress(crt, "_findclose");
  wfd64i32 d; memset(&d, 0, sizeof d); intptr_t h = ff ? ff(L"C:\\tmp\\ff\\*.txt", &d) : -1;
  printf("wfindfirst64i32=%d name=%ls size=%u\n", h != -1, d.name, d.size); if (h != -1) fc(h);
  char *tn = tmpnam(0); printf("tmpnam=%s\n", tn && tn[0] == '\\' ? "root-relative" : "bad");
  return 0;
}
