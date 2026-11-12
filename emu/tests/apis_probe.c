#include <windows.h>
#include <stdio.h>
typedef BOOL (WINAPI *EnumEx_t)(LOCALE_ENUMPROCEX, DWORD, LPARAM, LPVOID);
typedef BOOL (WINAPI *IsValid_t)(LPCWSTR);
typedef DWORD (WINAPI *CMReg_t)(void *, void *, void *, void **);
typedef DWORD (WINAPI *CMUnreg_t)(void *);
static int n_loc; static BOOL CALLBACK locproc(LPWSTR name, DWORD flags, LPARAM lp) { n_loc++; wprintf(L"locale %s flags %lx lp %p\n", name, flags, (void *)lp); return TRUE; }
int main(void) {
  HMODULE k = GetModuleHandleA("kernel32.dll");
  EnumEx_t ee = (EnumEx_t)GetProcAddress(k, "EnumSystemLocalesEx"); IsValid_t iv = (IsValid_t)GetProcAddress(k, "IsValidLocaleName");
  printf("EnumSystemLocalesEx=%d IsValidLocaleName=%d\n", ee != 0, iv != 0);
  if (ee) { BOOL r = ee(locproc, 0, 0x1234, 0); printf("EnumSystemLocalesEx -> %d, %d locales\n", r, n_loc); }
  if (iv) printf("IsValidLocaleName: en-US=%d ru-RU=%d ''=%d bad=%d\n", iv(L"en-US"), iv(L"ru-RU"), iv(L""), iv(L"x y!"));
  HMODULE cm = LoadLibraryA("cfgmgr32.dll"); CMReg_t reg = cm ? (CMReg_t)GetProcAddress(cm, "CM_Register_Notification") : 0; CMUnreg_t unreg = cm ? (CMUnreg_t)GetProcAddress(cm, "CM_Unregister_Notification") : 0;
  printf("cfgmgr32=%d CM_Register_Notification=%d\n", cm != 0, reg != 0);
  if (reg) { void *h = 0; char filter[416] = {0}; *(DWORD *)filter = 416; DWORD r = reg(filter, 0, (void *)1, &h); printf("CM_Register_Notification -> %lu h=%p; unregister -> %lu\n", r, h, unreg(h)); }
  HMODULE gl = LoadLibraryA("opengl32.dll");
  printf("opengl32=%d glVertexAttrib1sARB=%d glUniform4uiEXT=%d glGetDebugMessageLog=%d glFooBarARB=%d wglGetProcAddress=%d\n", gl != 0, GetProcAddress(gl, "glVertexAttrib1sARB") != 0, GetProcAddress(gl, "glUniform4uiEXT") != 0, GetProcAddress(gl, "glGetDebugMessageLog") != 0, GetProcAddress(gl, "glFooBarARB") != 0, GetProcAddress(gl, "wglGetProcAddress") != 0);
  return 0;
}
