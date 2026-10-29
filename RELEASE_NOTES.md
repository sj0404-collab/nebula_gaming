# v1.4.0 — Nebula x64: virtual disk, stdin, environment

* **Virtual file system** (`emu/x64/vfs.c`): an in‑memory `C:\` drive shared by every program run in the session (cwd `C:\nebula`, `C:\tmp`).
  Win32: `CreateFileA/W, ReadFile, WriteFile, CloseHandle, GetFileSize(Ex), SetFilePointer(Ex), SetEndOfFile, GetFileType, GetFileAttributes(Ex)A/W,
  GetFileInformationByHandle, FindFirstFile(Ex)A/W / FindNextFile / FindClose (wildcards), DeleteFile, CreateDirectory, RemoveDirectory, MoveFile(Ex), CopyFile,
  Get/SetCurrentDirectory, GetFullPathName, GetTempPath, GetTempFileName, CreateFileMapping/MapViewOfFile (copy semantics)`.
  CRT: `fopen/_wfopen/fopen_s/freopen/fclose/fread/fwrite/fgets/fgetc/getc/getchar/ungetc/fseek/_fseeki64/ftell/rewind/feof/clearerr/_fileno/_fdopen,
  _filbuf/_flsbuf (MinGW's inline getc/putc), _open/_wopen/_sopen_s/_close/_read/_write/_lseek(i64)/_tell/_eof/_chsize/_dup/_dup2, _stat64*/_fstat64*,
  _access/_unlink/remove/rename/_mkdir/_rmdir/_getcwd/_chdir/_fullpath, _findfirst64/_findnext64`.
  std handles can be redirected (`dup2` onto 0/1/2), so busybox `sh` redirections `>`, `>>`, `<`, `1>&2` work.
* **stdin**: blocking reads (`ReadFile`, `ReadConsoleA/W`, `ReadConsoleInputW`, `_read`, `fgets`, `getchar`, `_getch`, `gets_s`) suspend the guest until the
  console input line (Enter) or **EOF** button delivers data — no busy loop; the native `x64run` reads the real stdin (`--no-stdin` to force EOF).
* **Environment block** is dynamic now: `getenv/_wgetenv/getenv_s/_dupenv_s/_putenv/_putenv_s/GetEnvironmentVariable/SetEnvironmentVariable/
  ExpandEnvironmentStrings/GetEnvironmentStrings/_environ` all see the same table (`busybox env` shows what the program exported).
* **Fixed silent stubs** that returned 0: `strspn/strcspn/strpbrk/strtok/strtok_s/_strlwr/_strupr/_strrev/_itoa/_ltoa/_ultoa/_i64toa/strerror/strcpy_s/strcat_s/
  strncpy_s/memcpy_s/_mktemp`, `bsearch` with a guest comparator (was a hard fault), `setbuf/setvbuf`, Toolhelp `Process32First/Next`.
* **x64 Runner UI**: “Open folder…” mounts a whole directory under `C:\nebula\` (and lists the .exe files it contains), **Files** tab shows the virtual disk
  with *written* badges, download and delete, drag‑and‑drop of data files; console gets an input line + EOF; new status `WAITING-STDIN`.
* `x64run --mount <dir>[=C:\path]` copies a host folder into the VFS for headless tests; `emu/tests/run.sh` grew a VFS/stdin section (26 checks).
* Measured on busybox‑w32: `ls -l cat wc sort head tail grep sed awk cut find diff cmp cp tee xxd base64 md5sum sha256sum stat du mkdir sort -r (stdin) sh -c "for…; read x; [ -f ]; > <"` work.
* Still missing (honest): child processes / pipes (`sh` pipelines, `$(…)`, non‑builtin applets inside `sh` say “unable to spawn shell”), threads, sockets,
  Direct3D/OpenGL/audio, dialog resources — planned next: in‑emulator process table + anonymous pipes.
* e2e: 25 scenarios (6 for the emulator; the new one mounts a file, sorts it, feeds stdin to `sort -r`, writes `out.txt` with `tee`, reads it back, downloads it).

