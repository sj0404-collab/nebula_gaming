/* Regression for WriteProcessMemory in both x86 and x64 guest processes. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int count_a, count_w;
static BOOL CALLBACK locale_a(LPSTR name) { if (!strcmp(name, "00000409")) count_a++; return FALSE; }
static BOOL CALLBACK locale_w(LPWSTR name) { if (!wcscmp(name, L"00000409")) count_w++; return FALSE; }

int main(void) {
  char target[16] = "original";
  const char data[] = "changed";
  SIZE_T count = 0, guard = (SIZE_T)0x12345678;
  struct { SIZE_T n; SIZE_T guard; } result = { 0, guard };
  int ok = WriteProcessMemory(GetCurrentProcess(), target, data, sizeof(data), &result.n)
    && !strcmp(target, data) && result.n == sizeof(data) && result.guard == guard;
  char output[16] = {0};
  ok = ok && ReadProcessMemory(GetCurrentProcess(), target, output, sizeof(data), &count)
    && !strcmp(output, data) && count == sizeof(data);
  result.n = 99; result.guard = guard;
  BOOL invalid = WriteProcessMemory((HANDLE)(ULONG_PTR)0x123456, target, data, sizeof(data), &result.n);
  ok = ok && !invalid && result.guard == guard;
  ok = ok && EnumSystemLocalesA(locale_a, 0) && EnumSystemLocalesW(locale_w, 0) && count_a == 1 && count_w == 1;
  typedef LPWSTR (WINAPI *StrCpyWFn)(LPWSTR, LPCWSTR);
  HMODULE shlwapi = LoadLibraryW(L"shlwapi.dll");
  StrCpyWFn copy_w = shlwapi ? (StrCpyWFn)GetProcAddress(shlwapi, "StrCpyW") : NULL;
  WCHAR wbuf[32] = {0};
  ok = ok && copy_w && copy_w(wbuf, L"Clover Ω") == wbuf && !wcscmp(wbuf, L"Clover Ω");
  typedef HRESULT (WINAPI *PathUrlFn)(LPCWSTR, LPWSTR, DWORD *, DWORD);
  PathUrlFn path_url = shlwapi ? (PathUrlFn)GetProcAddress(shlwapi, "PathCreateFromUrlW") : NULL;
  WCHAR url_path[128] = {0}; DWORD url_cap = 128;
  ok = ok && path_url && path_url(L"file:///C:/Program%20Files/Clover", url_path, &url_cap, 0) == S_OK
    && !wcscmp(url_path, L"C:\\Program Files\\Clover") && url_cap == wcslen(url_path);
  url_cap = 1; ok = ok && path_url(L"file:///C:/x", url_path, &url_cap, 0) == E_POINTER && url_cap == 5;
  WCHAR profile_value[32] = {0};
  ok = ok && WritePrivateProfileStringW(L"one", L"first", L"value", L"C:\\test-profile.ini")
    && WritePrivateProfileStringW(L"one", L"second", L"other", L"C:\\test-profile.ini")
    && GetPrivateProfileStringW(L"one", L"first", L"", profile_value, 32, L"C:\\test-profile.ini") == 5
    && !wcscmp(profile_value, L"value");
  BYTE saved[] = {0, 1, 42, 128, 255}, restored[5] = {0};
  ok = ok && WritePrivateProfileStructW(L"binary", L"key", saved, sizeof(saved), L"C:\\test-profile.ini")
    && GetPrivateProfileStructW(L"binary", L"key", restored, sizeof(restored), L"C:\\test-profile.ini")
    && !memcmp(saved, restored, sizeof(saved));
  ok = ok && !GetPrivateProfileStructW(L"binary", L"missing", restored, sizeof(restored), L"C:\\test-profile.ini");
  typedef BOOL (WINAPI *CreateEnvFn)(LPVOID *, HANDLE, BOOL);
  typedef BOOL (WINAPI *DestroyEnvFn)(LPVOID);
  HMODULE userenv = LoadLibraryW(L"userenv.dll");
  CreateEnvFn create_env = userenv ? (CreateEnvFn)GetProcAddress(userenv, "CreateEnvironmentBlock") : NULL;
  DestroyEnvFn destroy_env = userenv ? (DestroyEnvFn)GetProcAddress(userenv, "DestroyEnvironmentBlock") : NULL;
  LPVOID env = NULL; int has_entry = 0;
  if (create_env && destroy_env && create_env(&env, NULL, FALSE) && env) {
    for (WCHAR *p = (WCHAR *)env; *p; p += wcslen(p) + 1) if (wcschr(p, L'=')) has_entry = 1;
    ok = ok && has_entry && destroy_env(env);
  } else ok = 0;
  typedef HANDLE (WINAPI *SetEventHookFn)(DWORD, DWORD, HMODULE, LPVOID, DWORD, DWORD, DWORD);
  HMODULE user32 = LoadLibraryW(L"user32.dll");
  SetEventHookFn hook = user32 ? (SetEventHookFn)GetProcAddress(user32, "SetWinEventHook") : NULL;
  SetLastError(0); ok = ok && hook && !hook(1, 2, NULL, NULL, 0, 0, 0) && GetLastError() == ERROR_NOT_SUPPORTED;
  printf("procmem: %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
