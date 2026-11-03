/* fdlg.exe — exercises the file dialogs: GetOpenFileNameW (typed name + Enter), GetSaveFileNameA (default extension),
   IFileOpenDialog / IFileSaveDialog via COM (what Notepad++ uses). Driven from the host with --type / --key; prints what a real app sees. */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commdlg.h>
#define COBJMACROS
#include <shobjidl.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  HANDLE h = CreateFileW(L"C:\\nebula\\fd_test.txt", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0); DWORD wr; WriteFile(h, "hello", 5, &wr, 0); CloseHandle(h);
  wchar_t file[MAX_PATH] = L"", title[MAX_PATH] = L"";
  OPENFILENAMEW o; memset(&o, 0, sizeof o); o.lStructSize = sizeof o; o.lpstrFilter = L"Text files (*.txt)\0*.txt;*.log\0All files (*.*)\0*.*\0"; o.nFilterIndex = 1;
  o.lpstrFile = file; o.nMaxFile = MAX_PATH; o.lpstrFileTitle = title; o.nMaxFileTitle = MAX_PATH; o.lpstrInitialDir = L"C:\\nebula"; o.lpstrTitle = L"Open a text file"; o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_HIDEREADONLY;
  BOOL r = GetOpenFileNameW(&o);
  printf("open=%d file=%ls off=%u ext=%u title=%ls filter=%u\n", (int)r, file, (unsigned)o.nFileOffset, (unsigned)o.nFileExtension, title, (unsigned)o.nFilterIndex);
  char afile[MAX_PATH] = "report"; OPENFILENAMEA a; memset(&a, 0, sizeof a); a.lStructSize = sizeof a; a.lpstrFilter = "Log (*.log)\0*.log\0"; a.lpstrFile = afile; a.nMaxFile = MAX_PATH; a.lpstrInitialDir = "C:\\nebula"; a.lpstrDefExt = "log"; a.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER;
  r = GetSaveFileNameA(&a);
  printf("save=%d file=%s off=%u ext=%u\n", (int)r, afile, (unsigned)a.nFileOffset, (unsigned)a.nFileExtension);
  /* modern COM dialogs (what Notepad++ & co use) */
  wchar_t com_open[MAX_PATH] = L"", com_save[MAX_PATH] = L""; UINT ftype = 0;
  CoInitializeEx(0, COINIT_APARTMENTTHREADED);
  IFileOpenDialog *od = 0; HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, 0, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&od);
  if (SUCCEEDED(hr)) { COMDLG_FILTERSPEC fs[2] = { { L"Text", L"*.txt" }, { L"All", L"*.*" } }; IShellItem *folder = 0; DWORD opts = 0;
    IFileOpenDialog_SetFileTypes(od, 2, fs); IFileOpenDialog_SetFileTypeIndex(od, 1); IFileOpenDialog_GetOptions(od, &opts); IFileOpenDialog_SetOptions(od, opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST); IFileOpenDialog_SetTitle(od, L"COM open");
    if (SUCCEEDED(SHCreateItemFromParsingName(L"C:\\nebula", 0, &IID_IShellItem, (void **)&folder))) { IFileOpenDialog_SetFolder(od, folder); IShellItem_Release(folder); }
    hr = IFileOpenDialog_Show(od, 0); printf("com open show=%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) { IShellItemArray *arr = 0; if (SUCCEEDED(IFileOpenDialog_GetResults(od, &arr))) { DWORD cnt = 0; IShellItemArray_GetCount(arr, &cnt); IShellItem *it = 0; if (cnt && SUCCEEDED(IShellItemArray_GetItemAt(arr, 0, &it))) { PWSTR nm = 0; if (SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &nm))) { wcsncpy(com_open, nm, MAX_PATH - 1); CoTaskMemFree(nm); } IShellItem_Release(it); } IShellItemArray_Release(arr); } IFileOpenDialog_GetFileTypeIndex(od, &ftype); }
    IFileOpenDialog_Release(od); }
  else printf("CoCreateInstance(FileOpenDialog) failed %08lx\n", (unsigned long)hr);
  IFileSaveDialog *sd = 0; hr = CoCreateInstance(&CLSID_FileSaveDialog, 0, CLSCTX_INPROC_SERVER, &IID_IFileSaveDialog, (void **)&sd);
  if (SUCCEEDED(hr)) { IFileDialogCustomize *cz = 0; IShellItem *folder = 0;
    if (SUCCEEDED(IFileSaveDialog_QueryInterface(sd, &IID_IFileDialogCustomize, (void **)&cz))) { IFileDialogCustomize_AddCheckButton(cz, 1000, L"Append extension", TRUE); IFileDialogCustomize_Release(cz); }
    if (SUCCEEDED(SHCreateItemFromParsingName(L"C:\\nebula", 0, &IID_IShellItem, (void **)&folder))) { IFileSaveDialog_SetFolder(sd, folder); IShellItem_Release(folder); }
    IFileSaveDialog_SetFileName(sd, L"notes"); IFileSaveDialog_SetDefaultExtension(sd, L"txt");
    hr = IFileSaveDialog_Show(sd, 0); printf("com save show=%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) { IShellItem *it = 0; if (SUCCEEDED(IFileSaveDialog_GetResult(sd, &it))) { PWSTR nm = 0; if (SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &nm))) { wcsncpy(com_save, nm, MAX_PATH - 1); CoTaskMemFree(nm); } IShellItem_Release(it); } }
    IFileSaveDialog_Release(sd); }
  printf("com open=%ls ftype=%u save=%ls\n", com_open, ftype, com_save);
  int ok = r && !strcmp(afile, "C:\\nebula\\report.log") && a.nFileOffset == 10 && a.nFileExtension == 17 && !wcscmp(file, L"C:\\nebula\\fd_test.txt") && o.nFileOffset == 10 && o.nFileExtension == 18 && !wcscmp(title, L"fd_test.txt") && o.nFilterIndex == 1
           && !wcscmp(com_open, L"C:\\nebula\\fd_test.txt") && ftype == 1 && !wcscmp(com_save, L"C:\\nebula\\notes.txt");
  printf(ok ? "FDLG OK\n" : "FDLG FAIL\n"); fflush(stdout); return ok ? 0 : 1;
}
