/* x64run: native test runner.  x64run [--inspect] [--frames N] [--max-ms T] [--key VK@ms ...] file.exe [args...] */
#include "emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int x64_load(const u8 *bytes, size_t len, const char *name, const char *cmdline);
int x64_run(u64 budget); const char *x64_inspect(const u8 *b, size_t n); const char *x64_error(void); u64 x64_error_rip(void); const char *x64_report(void); u64 x64_icount(void); int x64_exit_code(void); const char *x64_error_bytes(void); const char *x64_regs(void); void x64_key(int vk, int down); double x64_sleep_ms(void); const char *x64_api_stats(void); int x64_has_window(void);
void host_set_dump(int every, const char *prefix); int host_frames(void); double host_now_ms(void);

int main(int argc, char **argv) {
  int inspect = 0, dump_every = 0; double max_ms = 60000; const char *prefix = "frame"; int quiet = 0;
  struct { int vk; double at; int down; } keys[64]; int nkeys = 0; int ai = 1; int stats = 0;
  for (; ai < argc && argv[ai][0] == '-'; ai++) {
    if (!strcmp(argv[ai], "--inspect")) inspect = 1;
    else if (!strcmp(argv[ai], "--frames") && ai + 1 < argc) dump_every = atoi(argv[++ai]);
    else if (!strcmp(argv[ai], "--prefix") && ai + 1 < argc) prefix = argv[++ai];
    else if (!strcmp(argv[ai], "--max-ms") && ai + 1 < argc) max_ms = atof(argv[++ai]);
    else if (!strcmp(argv[ai], "--quiet")) quiet = 1;
    else if (!strcmp(argv[ai], "--stats")) stats = 1;
    else if (!strcmp(argv[ai], "--key") && ai + 1 < argc) { int vk; double at; int dur = 100; if (sscanf(argv[++ai], "%i@%lf:%d", &vk, &at, &dur) >= 2 && nkeys < 62) { keys[nkeys].vk = vk; keys[nkeys].at = at; keys[nkeys].down = 1; nkeys++; keys[nkeys].vk = vk; keys[nkeys].at = at + dur; keys[nkeys].down = 0; nkeys++; } }
    else { fprintf(stderr, "unknown option %s\n", argv[ai]); return 2; }
  }
  if (ai >= argc) { fprintf(stderr, "usage: x64run [--inspect] [--frames N] [--max-ms T] [--key VK@ms[:dur]] file.exe [args]\n"); return 2; }
  const char *path = argv[ai];
  FILE *f = fopen(path, "rb"); if (!f) { perror(path); return 2; }
  fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET); u8 *bytes = malloc(len); if (fread(bytes, 1, len, f) != (size_t)len) { fclose(f); return 2; } fclose(f);
  if (inspect) { puts(x64_inspect(bytes, len)); return 0; }
  char cmdline[4096]; const char *base = strrchr(path, '/'); base = base ? base + 1 : path; snprintf(cmdline, sizeof cmdline, "%s", base);
  for (int i = ai + 1; i < argc; i++) { int q = strchr(argv[i], ' ') != 0 || !argv[i][0]; strncat(cmdline, q ? " \"" : " ", sizeof cmdline - strlen(cmdline) - 1); strncat(cmdline, argv[i], sizeof cmdline - strlen(cmdline) - 1); if (q) strncat(cmdline, "\"", sizeof cmdline - strlen(cmdline) - 1); }
  host_set_dump(dump_every, prefix);
  int rc = x64_load(bytes, len, base, cmdline);
  if (rc) { fprintf(stderr, "load failed (%d): %s", rc, x64_report()); return 3; }
  double t0 = host_now_ms(); int r; int ki = 0; u64 yields = 0;
  for (;;) {
    double el = host_now_ms() - t0;
    if (el > max_ms) { if (!quiet) fprintf(stderr, "[x64run] time limit reached\n"); r = -1; break; }
    while (ki < nkeys && keys[ki].at <= el) { x64_key(keys[ki].vk, keys[ki].down); ki++; }
    r = x64_run(2000000);
    if (r == RUN_YIELD) { yields++; double s = x64_sleep_ms(); if (s > 0) { if (s > 16) s = 16; usleep((useconds_t)(s * 1000)); } else if (x64_has_window()) usleep(1000); continue; }
    if (r != RUN_OK) break;
  }
  double dt = (host_now_ms() - t0) / 1000.0; u64 ic = x64_icount();
  if (!quiet) {
    fflush(stdout);
    if (r == RUN_EXIT) fprintf(stderr, "[x64run] exit code %d\n", x64_exit_code());
    else if (r == RUN_FAULT) fprintf(stderr, "[x64run] FAULT at rip=0x%llx: %s  bytes: %s\n", (unsigned long long)x64_error_rip(), x64_error(), x64_error_bytes()), fprintf(stderr, "[x64run] regs: %s\n", x64_regs());
    else if (r == RUN_BREAK) fprintf(stderr, "[x64run] breakpoint at rip=0x%llx\n", (unsigned long long)x64_error_rip());
    fprintf(stderr, "[x64run] %llu instructions in %.3fs = %.1f MIPS, %d frames, %llu yields\n", (unsigned long long)ic, dt, dt > 0 ? ic / dt / 1e6 : 0, host_frames(), (unsigned long long)yields);
    if (x64_report()[0]) fprintf(stderr, "[x64run] report:\n%s", x64_report());
    if (stats) fprintf(stderr, "[x64run] api calls: %s\n", x64_api_stats());
  }
  return r == RUN_EXIT ? x64_exit_code() : r == -1 ? 0 : 100 + r;
}
