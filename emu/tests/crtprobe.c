/* crtprobe: exercise the REAL Microsoft Visual C++ runtimes of every redistributable pack (2005 … 2013) the way programs
 * built with those compilers do - LoadLibrary of msvcrXX.dll / msvcpXX.dll from the system directory and real calls into
 * them (heap, printf family, strtod, qsort, stdio FILE*, _beginthreadex, time). Builds for x86 (SysWOW64) and x64
 * (System32) with zig cc; the pack DLLs are mounted by run.sh.
 *   argv[1..]  version numbers to probe (default: 80 90 100 110 120) */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef int (__cdecl *snprintf_t)(char *, size_t, const char *, ...);
typedef void *(__cdecl *malloc_t)(size_t);
typedef void (__cdecl *free_t)(void *);
typedef double (__cdecl *strtod_t)(const char *, char **);
typedef void (__cdecl *qsort_t)(void *, size_t, size_t, int (__cdecl *)(const void *, const void *));
typedef void *(__cdecl *fopen_t)(const char *, const char *);
typedef int (__cdecl *fprintf_t)(void *, const char *, ...);
typedef size_t (__cdecl *fread_t)(void *, size_t, size_t, void *);
typedef int (__cdecl *fclose_t)(void *);
typedef unsigned long (__cdecl *beginthreadex_t)(void *, unsigned, unsigned (__stdcall *)(void *), void *, unsigned, unsigned *);
typedef long long (__cdecl *time64_t)(long long *);
typedef char *(__cdecl *strerror_t)(int);
typedef int (__cdecl *errno_t_)(int *);
typedef char *(__cdecl *setlocale_t)(int, const char *);
typedef int (__cdecl *atexit_t)(void (__cdecl *)(void));

static int __cdecl cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static volatile long thread_ran;
static unsigned __stdcall thread_main(void *arg) { InterlockedExchange(&thread_ran, (long)(size_t)arg); return 7; }

static int probe(int ver) {
  char crt[32], cpp[32]; sprintf(crt, "msvcr%d.dll", ver); sprintf(cpp, "msvcp%d.dll", ver);
  HMODULE c = LoadLibraryA(crt);
  if (!c) { printf("crtprobe %d: LoadLibrary(%s) failed: %lu\n", ver, crt, GetLastError()); return 1; }
  char path[MAX_PATH] = ""; GetModuleFileNameA(c, path, sizeof path);
  printf("crtprobe %d: %s loaded at %p from %s\n", ver, crt, (void *)c, path);
  snprintf_t p_snprintf = (snprintf_t)GetProcAddress(c, "_snprintf");
  malloc_t p_malloc = (malloc_t)GetProcAddress(c, "malloc"); free_t p_free = (free_t)GetProcAddress(c, "free");
  strtod_t p_strtod = (strtod_t)GetProcAddress(c, "strtod"); qsort_t p_qsort = (qsort_t)GetProcAddress(c, "qsort");
  fopen_t p_fopen = (fopen_t)GetProcAddress(c, "fopen"); fprintf_t p_fprintf = (fprintf_t)GetProcAddress(c, "fprintf");
  fread_t p_fread = (fread_t)GetProcAddress(c, "fread"); fclose_t p_fclose = (fclose_t)GetProcAddress(c, "fclose");
  beginthreadex_t p_beginthreadex = (beginthreadex_t)GetProcAddress(c, "_beginthreadex");
  time64_t p_time64 = (time64_t)GetProcAddress(c, "_time64"); strerror_t p_strerror = (strerror_t)GetProcAddress(c, "strerror");
  setlocale_t p_setlocale = (setlocale_t)GetProcAddress(c, "setlocale");
  if (!p_snprintf || !p_malloc || !p_free || !p_strtod || !p_qsort || !p_fopen || !p_fprintf || !p_fread || !p_fclose || !p_beginthreadex || !p_time64) {
    printf("crtprobe %d: exports missing (_snprintf=%p malloc=%p strtod=%p qsort=%p fopen=%p _beginthreadex=%p _time64=%p)\n", ver, (void *)p_snprintf, (void *)p_malloc, (void *)p_strtod, (void *)p_qsort, (void *)p_fopen, (void *)p_beginthreadex, (void *)p_time64);
    return 2;
  }
  int fails = 0;
  char buf[128]; int n = p_snprintf(buf, sizeof buf, "%d|%s|%.3f|%x", 42, "real crt", 3.14159, 0xBEEF);
  if (n != 22 || strcmp(buf, "42|real crt|3.142|beef")) { printf("crtprobe %d: _snprintf -> %d '%s'\n", ver, n, buf); fails++; }
  char *m = p_malloc(100000); if (!m) { printf("crtprobe %d: malloc failed\n", ver); fails++; } else { memset(m, 0x5a, 100000); if (m[99999] != 0x5a) fails++; p_free(m); }
  char *end = NULL; double d = p_strtod("  -12.5e2xyz", &end);
  if (d != -1250.0 || !end || strcmp(end, "xyz")) { printf("crtprobe %d: strtod -> %g '%s'\n", ver, d, end ? end : "(null)"); fails++; }
  int arr[8] = { 9, 3, 7, 1, 8, 2, 6, 4 }; p_qsort(arr, 8, sizeof(int), cmp_int);
  for (int i = 1; i < 8; i++) if (arr[i - 1] > arr[i]) { printf("crtprobe %d: qsort wrong order\n", ver); fails++; break; }
  char tmp[MAX_PATH]; GetTempPathA(sizeof tmp, tmp); strcat(tmp, "crtprobe.txt");
  void *f = p_fopen(tmp, "wb");
  if (!f) { printf("crtprobe %d: fopen(w) failed\n", ver); fails++; }
  else { p_fprintf(f, "hello from msvcr%d %s", ver, "stdio"); p_fclose(f);
    f = p_fopen(tmp, "rb"); char rb[64] = {0}; size_t got = f ? p_fread(rb, 1, sizeof rb - 1, f) : 0; if (f) p_fclose(f);
    char want[64]; sprintf(want, "hello from msvcr%d stdio", ver);
    if (got != strlen(want) || strcmp(rb, want)) { printf("crtprobe %d: stdio round trip -> %u '%s'\n", ver, (unsigned)got, rb); fails++; } }
  thread_ran = 0; unsigned tid = 0; unsigned long th = p_beginthreadex(NULL, 0, thread_main, (void *)(size_t)(ver + 1), 0, &tid);
  if (!th) { printf("crtprobe %d: _beginthreadex failed\n", ver); fails++; }
  else { DWORD w = WaitForSingleObject((HANDLE)(size_t)th, 5000); DWORD code = 0; GetExitCodeThread((HANDLE)(size_t)th, &code); CloseHandle((HANDLE)(size_t)th);
    if (w != WAIT_OBJECT_0 || thread_ran != ver + 1 || code != 7) { printf("crtprobe %d: thread wait=%lu ran=%ld code=%lu\n", ver, w, thread_ran, code); fails++; } }
  long long t = p_time64(NULL); if (t < 1600000000LL) { printf("crtprobe %d: _time64 -> %lld\n", ver, t); fails++; }
  if (p_strerror) { const char *e = p_strerror(2); if (!e || !strstr(e, "No such file")) { printf("crtprobe %d: strerror(2) -> '%s'\n", ver, e ? e : "(null)"); fails++; } }
  if (p_setlocale) { const char *l = p_setlocale(0 /*LC_ALL*/, NULL); printf("crtprobe %d: locale '%s'\n", ver, l ? l : "(null)"); }
  HMODULE p = LoadLibraryA(cpp);
  if (!p) { printf("crtprobe %d: LoadLibrary(%s) failed: %lu\n", ver, cpp, GetLastError()); fails++; }
  else {
    void *cout = GetProcAddress(p, "?cout@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A");
    void *xlen = GetProcAddress(p, "?_Xlength_error@std@@YAXPBD@Z"); if (!xlen) xlen = GetProcAddress(p, "?_Xlength_error@std@@YAXPEBD@Z");
    printf("crtprobe %d: %s loaded, std::cout=%p _Xlength_error=%p\n", ver, cpp, cout, xlen);
    if (!cout) fails++;
  }
  char mfc[32]; sprintf(mfc, "mfc%du.dll", ver); HMODULE mf = LoadLibraryA(mfc);
  printf("crtprobe %d: %s %s\n", ver, mfc, mf ? "loaded (DllMain ran)" : "not loaded");   /* informational: MFC is only in some packs */
  printf("crtprobe %d: %s\n", ver, fails ? "FAIL" : "OK");
  return fails ? 3 : 0;
}

int main(int argc, char **argv) {
  static const int def[] = { 80, 90, 100, 110, 120 };
  int bad = 0, total = 0;
  if (argc > 1) { for (int i = 1; i < argc; i++) { total++; if (probe(atoi(argv[i]))) bad++; } }
  else for (int i = 0; i < 5; i++) { total++; if (probe(def[i])) bad++; }
  printf("crtprobe %u-bit: %d/%d runtimes OK\n", (unsigned)(sizeof(void *) * 8), total - bad, total);
  if (!bad) puts("CRTPROBE PASS");
  return bad ? 1 : 0;
}
