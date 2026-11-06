/* verinfo.c — version.dll over a real file: GetFileVersionInfoSize/GetFileVersionInfo/VerQueryValue (A and W). Built by run.sh with zig cc. */
#include <windows.h>
#include <stdio.h>
int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : argv[0]; DWORD h = 0; DWORD sz = GetFileVersionInfoSizeA(path, &h);
  if (!sz) { printf("verinfo: no version resource (err %lu)\n", GetLastError()); return 1; }
  void *blk = malloc(sz); if (!GetFileVersionInfoA(path, 0, sz, blk)) { printf("verinfo: GetFileVersionInfo failed (err %lu)\n", GetLastError()); return 1; }
  VS_FIXEDFILEINFO *ffi = 0; UINT n = 0;
  if (VerQueryValueA(blk, "\\", (void **)&ffi, &n) && ffi) printf("fixed=%u.%u.%u.%u sig=%08lx\n", (unsigned)HIWORD(ffi->dwFileVersionMS), (unsigned)LOWORD(ffi->dwFileVersionMS), (unsigned)HIWORD(ffi->dwFileVersionLS), (unsigned)LOWORD(ffi->dwFileVersionLS), ffi->dwSignature);
  WORD *tr = 0; UINT tn = 0; char sub[128]; const char *fv = 0; WCHAR *fvw = 0;
  if (VerQueryValueA(blk, "\\VarFileInfo\\Translation", (void **)&tr, &tn) && tn >= 4) { printf("translation=%04x%04x\n", tr[0], tr[1]); sprintf(sub, "\\StringFileInfo\\%04x%04x\\FileVersion", tr[0], tr[1]); }
  else strcpy(sub, "\\StringFileInfo\\040904E4\\FileVersion");
  if (VerQueryValueA(blk, sub, (void **)&fv, &n)) printf("FileVersion=%s\n", fv);
  sprintf(sub, "\\StringFileInfo\\%04x%04x\\ProductName", tr ? tr[0] : 0x409, tr ? tr[1] : 0x4e4); if (VerQueryValueA(blk, sub, (void **)&fv, &n)) printf("ProductName=%s\n", fv);
  WCHAR wpath[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH); DWORD szw = GetFileVersionInfoSizeW(wpath, &h); void *blkw = malloc(szw);
  if (szw && GetFileVersionInfoW(wpath, 0, szw, blkw)) { WCHAR subw[128]; wsprintfW(subw, L"\\StringFileInfo\\%04x%04x\\FileVersion", tr ? tr[0] : 0x409, tr ? tr[1] : 0x4e4); if (VerQueryValueW(blkw, subw, (void **)&fvw, &n)) printf("FileVersionW=%ls\n", fvw); }
  printf("verinfo done\n"); return 0;
}
