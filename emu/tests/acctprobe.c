/* LookupAccountSid/Name round trip, SHDeleteKey, CoSetProxyBlanket, SHAutoComplete, GetDCOrgEx */
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
typedef LSTATUS (WINAPI *PFN_SHDeleteKeyW)(HKEY, LPCWSTR);
typedef HRESULT (WINAPI *PFN_SHAutoComplete)(HWND, DWORD);
typedef HRESULT (WINAPI *PFN_CoSetProxyBlanket)(IUnknown *, DWORD, DWORD, OLECHAR *, DWORD, DWORD, void *, DWORD);
int main(void) {
  int ok = 1; HANDLE tok; BYTE buf[256]; DWORD n = 0; WCHAR name[64], dom[64]; DWORD cn, cd; SID_NAME_USE use;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok) || !GetTokenInformation(tok, TokenUser, buf, sizeof buf, &n)) { printf("token failed %lu\n", GetLastError()); return 1; }
  PSID sid = ((TOKEN_USER *)buf)->User.Sid; cn = cd = 64;
  BOOL r = LookupAccountSidW(NULL, sid, name, &cn, dom, &cd, &use); printf("LookupAccountSidW -> %d '%ls' @ '%ls' use %d (err %lu)\n", r, name, dom, (int)use, GetLastError()); ok &= r && use == SidTypeUser && cn == 4 && name[0];
  cn = 2; cd = 64; r = LookupAccountSidW(NULL, sid, name, &cn, dom, &cd, &use); printf("small buffer -> %d err %lu need %lu\n", r, GetLastError(), cn); ok &= !r && GetLastError() == ERROR_INSUFFICIENT_BUFFER && cn == 5;
  SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY; PSID admins = NULL; AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins);
  cn = cd = 64; r = LookupAccountSidW(NULL, admins, name, &cn, dom, &cd, &use); printf("Administrators -> %d '%ls' @ '%ls' use %d\n", r, name, dom, (int)use); ok &= r && use == SidTypeAlias && !wcscmp(dom, L"BUILTIN");
  BYTE sid2[68]; DWORD cb = sizeof sid2; cd = 64; r = LookupAccountNameW(NULL, L"BUILTIN\\Administrators", sid2, &cb, dom, &cd, &use); printf("LookupAccountNameW -> %d cb %lu equal %d dom '%ls' use %d\n", r, cb, r ? EqualSid(sid2, admins) : 0, dom, (int)use); ok &= r && EqualSid(sid2, admins) && use == SidTypeAlias;
  cb = sizeof sid2; cd = 64; r = LookupAccountNameW(NULL, L"nobody-here", sid2, &cb, dom, &cd, &use); printf("unknown name -> %d err %lu\n", r, GetLastError()); ok &= !r && GetLastError() == ERROR_NONE_MAPPED;
  char an[64], ad[64]; cn = cd = 64; r = LookupAccountSidA(NULL, admins, an, &cn, ad, &cd, &use); printf("LookupAccountSidA -> %d '%s' @ '%s'\n", r, an, ad); ok &= r && !strcmp(an, "Administrators");
  HKEY k; RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\NebulaProbe\\Sub\\Deep", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL); RegSetValueExW(k, L"v", 0, REG_DWORD, (BYTE *)&n, 4); RegCloseKey(k);
  HMODULE shl = LoadLibraryA("shlwapi.dll"); PFN_SHDeleteKeyW del = (PFN_SHDeleteKeyW)GetProcAddress(shl, "SHDeleteKeyW"); PFN_SHAutoComplete ac = (PFN_SHAutoComplete)GetProcAddress(shl, "SHAutoComplete");
  LSTATUS ds = del ? del(HKEY_CURRENT_USER, L"Software\\NebulaProbe") : -1; LSTATUS op = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\NebulaProbe\\Sub\\Deep", 0, KEY_READ, &k);
  printf("SHDeleteKeyW -> %ld, reopen deep -> %ld (want 2)\n", (long)ds, (long)op); ok &= ds == 0 && op == ERROR_FILE_NOT_FOUND;
  HWND ed = CreateWindowExA(0, "EDIT", "", WS_POPUP, 0, 0, 100, 20, NULL, NULL, GetModuleHandleA(NULL), NULL); HRESULT hac = ac ? ac(ed, SHACF_FILESYSTEM | SHACF_AUTOSUGGEST_FORCE_ON) : -1; printf("SHAutoComplete -> %08lx\n", hac); ok &= hac == S_OK;
  HMODULE ole = LoadLibraryA("ole32.dll"); PFN_CoSetProxyBlanket pb = (PFN_CoSetProxyBlanket)GetProcAddress(ole, "CoSetProxyBlanket"); HRESULT hpb = pb ? pb((IUnknown *)ed, 10, 0, NULL, 3, 3, NULL, 0) : -1; printf("CoSetProxyBlanket -> %08lx\n", hpb); ok &= hpb == S_OK;
  HWND w = CreateWindowExA(0, "STATIC", "o", WS_POPUP | WS_VISIBLE, 37, 53, 90, 60, NULL, NULL, GetModuleHandleA(NULL), NULL); POINT cl = { 0, 0 }; ClientToScreen(w, &cl); HDC dc = GetDC(w); POINT pt = { -1, -1 }; BOOL go = GetDCOrgEx(dc, &pt); HDC mem = CreateCompatibleDC(dc); POINT pm = { -1, -1 }; BOOL gm = GetDCOrgEx(mem, &pm);
  printf("GetDCOrgEx window -> %d (%ld,%ld) client origin (%ld,%ld); memory DC -> %d (%ld,%ld)\n", go, pt.x, pt.y, cl.x, cl.y, gm, pm.x, pm.y); ok &= go && pt.x == cl.x && pt.y == cl.y && gm && pm.x == 0 && pm.y == 0;
  printf(ok ? "ACCTPROBE OK\n" : "ACCTPROBE MISMATCH\n"); return ok ? 0 : 1;
}
