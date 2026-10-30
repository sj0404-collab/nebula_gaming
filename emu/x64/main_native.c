/* x64run: native test runner.  x64run [--inspect] [--frames N] [--max-ms T] [--key VK@ms ...] file.exe [args...] */
#include "emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int x64_load(const u8 *bytes, size_t len, const char *name, const char *cmdline);
static int g_apitrace;
int x64_run(u64 budget); void x64_trace(int on); const char *x64_inspect(const u8 *b, size_t n); const char *x64_error(void); u64 x64_error_rip(void); const char *x64_report(void); u64 x64_icount(void); int x64_exit_code(void); const char *x64_error_bytes(void); const char *x64_regs(void); const char *x64_history(void); int x64_mem_copy(u64 addr, unsigned char *dst, unsigned n); void x64_key(int vk, int down); double x64_sleep_ms(void); const char *x64_api_stats(void); int x64_has_window(void);
int x64_vfs_add(const char *path, const u8 *data, size_t n); int x64_vfs_mkdir(const char *path); const char *x64_vfs_list(void); void x64_stdin_write(const u8 *data, size_t n); void x64_stdin_eof(void); int x64_wants_stdin(void);
void host_set_dump(int every, const char *prefix); int host_frames(void); double host_now_ms(void);


#include <dirent.h>
#include <sys/stat.h>
static int mount_dir(const char *host, const char *guest, int depth) {   /* copies a host directory tree into the VFS */
  DIR *d = opendir(host); if (!d) return 0; struct dirent *e; int n = 0; x64_vfs_mkdir(guest);
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    char hp[2048], gp[2048]; snprintf(hp, sizeof hp, "%s/%s", host, e->d_name); snprintf(gp, sizeof gp, "%s\\%s", guest, e->d_name);
    struct stat st; if (stat(hp, &st)) continue;
    if (S_ISDIR(st.st_mode)) { if (depth < 40) n += mount_dir(hp, gp, depth + 1); continue; }
    FILE *f = fopen(hp, "rb"); if (!f) continue; u8 *buf = malloc((size_t)st.st_size + 1); size_t got = fread(buf, 1, (size_t)st.st_size, f); fclose(f);
    x64_vfs_add(gp, buf, got); free(buf); n++;
  }
  closedir(d); return n;
}
int main(int argc, char **argv) {
  int inspect = 0, dump_every = 0; double max_ms = 60000; const char *prefix = "frame"; int quiet = 0;
  const char *mounts[16][2]; int nmounts = 0; int no_stdin = 0;
  struct { int vk; double at; int down; } keys[64]; int nkeys = 0; int ai = 1; int stats = 0;
  for (; ai < argc && argv[ai][0] == '-'; ai++) {
    if (!strcmp(argv[ai], "--inspect")) inspect = 1;
    else if (!strcmp(argv[ai], "--frames") && ai + 1 < argc) dump_every = atoi(argv[++ai]);
    else if (!strcmp(argv[ai], "--prefix") && ai + 1 < argc) prefix = argv[++ai];
    else if (!strcmp(argv[ai], "--max-ms") && ai + 1 < argc) max_ms = atof(argv[++ai]);
    else if (!strcmp(argv[ai], "--quiet")) quiet = 1;
    else if (!strcmp(argv[ai], "--trace")) x64_trace(1); else if (!strcmp(argv[ai], "--trace2")) x64_trace(2); else if (!strcmp(argv[ai], "--apitrace")) { extern void w32_api_trace(int); g_apitrace = 1; }
    else if (!strcmp(argv[ai], "--mount") && ai + 1 < argc) { const char *spec = argv[++ai]; char hostd[1024]; const char *eq = strchr(spec, '='); const char *guest = "C:\\nebula"; if (eq) { snprintf(hostd, sizeof hostd, "%.*s", (int)(eq - spec), spec); guest = eq + 1; } else snprintf(hostd, sizeof hostd, "%s", spec); nmounts++; mounts[nmounts - 1][0] = strdup(hostd); mounts[nmounts - 1][1] = guest; }
    else if (!strcmp(argv[ai], "--no-stdin")) no_stdin = 1;
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
  for (int i = ai + 1; i < argc; i++) {   /* MSVC command-line quoting rules */
    const char *s = argv[i]; size_t o = strlen(cmdline); if (o + 1 < sizeof cmdline) cmdline[o++] = ' ';
    int q = !s[0] || strpbrk(s, " \t\"") != 0; if (q && o + 1 < sizeof cmdline) cmdline[o++] = '"';
    for (const char *p = s; *p && o + 3 < sizeof cmdline; p++) { if (q && *p == '"') cmdline[o++] = '\\'; cmdline[o++] = *p; }
    if (q && o + 1 < sizeof cmdline) cmdline[o++] = '"'; cmdline[o] = 0;
  }
  host_set_dump(dump_every, prefix);
  for (int mi = 0; mi < nmounts; mi++) { int n = mount_dir(mounts[mi][0], mounts[mi][1], 0); if (!quiet) fprintf(stderr, "[x64run] mounted %s -> %s (%d files)\n", mounts[mi][0], mounts[mi][1], n); }   /* before load: DLLs next to the exe */
  int rc = x64_load(bytes, len, base, cmdline);
  if (g_apitrace) { extern void w32_api_trace(int); w32_api_trace(1); }
  if (rc) { fprintf(stderr, "load failed (%d): %s", rc, x64_report()); return 3; }
  double t0 = host_now_ms(); int r; int ki = 0; u64 yields = 0;
  for (;;) {
    double el = host_now_ms() - t0;
    if (el > max_ms) { if (!quiet) { fprintf(stderr, "[x64run] time limit reached\n"); if (getenv("X64_HIST")) { extern u64 x64_rip(void); fprintf(stderr, "[x64run] rip=%llx regs: %s\n[x64run] last blocks: %s\n", (unsigned long long)x64_rip(), x64_regs(), x64_history()); unsigned char b[512]; u64 a = (x64_rip() & ~0xfull) - 256; if (x64_mem_copy(a, b, 512)) { FILE *f = fopen("/tmp/rip_tl.bin", "wb"); if (f) { fwrite(b, 1, 512, f); fclose(f); fprintf(stderr, "[x64run] dumped 512 bytes from 0x%llx to /tmp/rip_tl.bin\n", (unsigned long long)a); } } } } r = -1; break; }
    while (ki < nkeys && keys[ki].at <= el) { x64_key(keys[ki].vk, keys[ki].down); ki++; }
    r = x64_run(2000000);
    if (r == RUN_YIELD && x64_wants_stdin()) { char line[4096]; if (!no_stdin && fgets(line, sizeof line, stdin)) x64_stdin_write((const u8 *)line, strlen(line)); else x64_stdin_eof(); continue; }
    if (r == RUN_YIELD) { yields++; double s = x64_sleep_ms(); if (s > 0) { if (s > 16) s = 16; usleep((useconds_t)(s * 1000)); } else if (x64_has_window()) usleep(1000); continue; }
    if (r != RUN_OK) break;
  }
  double dt = (host_now_ms() - t0) / 1000.0; u64 ic = x64_icount();
  if (!quiet) {
    fflush(stdout);
    if (r == RUN_EXIT) fprintf(stderr, "[x64run] exit code %d\n", x64_exit_code());
    else if (r == RUN_FAULT) fprintf(stderr, "[x64run] FAULT at rip=0x%llx: %s  bytes: %s\n", (unsigned long long)x64_error_rip(), x64_error(), x64_error_bytes()), fprintf(stderr, "[x64run] regs: %s\n", x64_regs()), fprintf(stderr, "[x64run] last blocks: %s\n", x64_history());
    if (getenv("X64_DUMP")) { unsigned char b[512]; u64 a = x64_error_rip() & ~0xfull; if (a >= 256) a -= 256; if (x64_mem_copy(a, b, 512)) { FILE *f = fopen(getenv("X64_DUMP"), "wb"); if (f) { fwrite(b, 1, 512, f); fclose(f); fprintf(stderr, "[x64run] dumped 512 bytes from 0x%llx to %s\n", (unsigned long long)a, getenv("X64_DUMP")); } } }
    else if (r == RUN_BREAK) fprintf(stderr, "[x64run] breakpoint at rip=0x%llx\n", (unsigned long long)x64_error_rip());
    fprintf(stderr, "[x64run] %llu instructions in %.3fs = %.1f MIPS, %d frames, %llu yields\n", (unsigned long long)ic, dt, dt > 0 ? ic / dt / 1e6 : 0, host_frames(), (unsigned long long)yields);
    if (x64_report()[0]) fprintf(stderr, "[x64run] report:\n%s", x64_report());
    if (stats) fprintf(stderr, "[x64run] api calls: %s\n", x64_api_stats());
  }
  return r == RUN_EXIT ? x64_exit_code() : r == -1 ? 0 : 100 + r;
}
