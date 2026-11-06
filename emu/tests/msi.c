/* msi.c — msi.dll the way installer scripts use it: LoadLibrary + GetProcAddress (Inno Setup [Code] 'external' imports),
   product/feature queries on a machine with no MSI products, MsiSetInternalUI, MsiGetFileVersion/MsiGetFileHash over a real file. Built by run.sh with zig cc. */
#include <windows.h>
#include <stdio.h>
typedef int (WINAPI *QPS_W)(const WCHAR *); typedef UINT (WINAPI *ERP_W)(const WCHAR *, DWORD, DWORD, WCHAR *); typedef UINT (WINAPI *GPI_W)(const WCHAR *, const WCHAR *, WCHAR *, DWORD *);
typedef UINT (WINAPI *SIU)(UINT, HWND *); typedef UINT (WINAPI *GFV_W)(const WCHAR *, WCHAR *, DWORD *, WCHAR *, DWORD *); typedef UINT (WINAPI *GFV_A)(const char *, char *, DWORD *, char *, DWORD *);
typedef struct { ULONG size; ULONG data[4]; } FHI; typedef UINT (WINAPI *GFH_A)(const char *, DWORD, FHI *);
int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : argv[0]; HMODULE m = LoadLibraryA("msi.dll"); if (!m) { printf("msi: LoadLibrary failed err %lu\n", GetLastError()); return 1; }
  QPS_W qps = (QPS_W)GetProcAddress(m, "MsiQueryProductStateW"); ERP_W erp = (ERP_W)GetProcAddress(m, "MsiEnumRelatedProductsW"); GPI_W gpi = (GPI_W)GetProcAddress(m, "MsiGetProductInfoW");
  SIU siu = (SIU)GetProcAddress(m, "MsiSetInternalUI"); GFV_W gfvw = (GFV_W)GetProcAddress(m, "MsiGetFileVersionW"); GFV_A gfva = (GFV_A)GetProcAddress(m, "MsiGetFileVersionA"); GFH_A gfh = (GFH_A)GetProcAddress(m, "MsiGetFileHashA");
  if (!qps || !erp || !gpi || !siu || !gfvw || !gfva || !gfh) { printf("msi: import missing (%p %p %p %p %p %p %p)\n", (void *)qps, (void *)erp, (void *)gpi, (void *)siu, (void *)gfvw, (void *)gfva, (void *)gfh); return 1; }
  printf("state=%d\n", qps(L"{1D8E6291-B0D5-35EC-8441-6616F567A0F7}"));
  WCHAR code[40]; printf("related=%u\n", erp(L"{AA783A14-A7A3-3D33-95F0-9A351D530011}", 0, 0, code));
  WCHAR buf[64]; DWORD n = 64; printf("info=%u\n", gpi(L"{1D8E6291-B0D5-35EC-8441-6616F567A0F7}", L"VersionString", buf, &n));
  HWND prev = (HWND)1; UINT lvl = siu(2, &prev); printf("ui=%u->%u hwnd=%p\n", lvl, siu(5, 0), (void *)prev);
  WCHAR wpath[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH); WCHAR ver[64], lang[16]; DWORD vn = 64, ln = 16; UINT rc = gfvw(wpath, ver, &vn, lang, &ln);
  char vern[64], langn[16]; WideCharToMultiByte(CP_ACP, 0, ver, -1, vern, 64, 0, 0); WideCharToMultiByte(CP_ACP, 0, lang, -1, langn, 16, 0, 0); printf("fileversionW rc=%u ver=%s (%lu) lang=%s (%lu)\n", rc, vern, vn, langn, ln);
  char vera[64]; DWORD va = 64; rc = gfva(path, vera, &va, 0, 0); printf("fileversionA rc=%u ver=%s\n", rc, vera);
  char small[4]; DWORD sn = 4; printf("small rc=%u need=%lu\n", gfva(path, small, &sn, 0, 0), sn);
  printf("missing rc=%u\n", gfva("C:\\no\\such.exe", vera, &va, 0, 0));
  FHI h = { 20 }; rc = gfh(path, 0, &h); printf("hash rc=%u %08lx%08lx%08lx%08lx\n", rc, h.data[0], h.data[1], h.data[2], h.data[3]);
  printf("MSI OK\n"); return 0; }
