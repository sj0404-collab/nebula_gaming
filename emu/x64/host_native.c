/* Native host for tests: console on stdout, framebuffer dumped to PPM on demand. */
#include "emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static int g_dump_frames; static int g_frame_count; static const char *g_dump_prefix = "frame";
void host_write(int fd, const char *p, size_t n) { if (fd == 3) { if (getenv("X64_LOG")) fwrite(p, 1, n, stderr); return; } fwrite(p, 1, n, fd == 2 ? stderr : stdout); fflush(fd == 2 ? stderr : stdout); }
double host_wall_ms(void) { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }
double host_now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }
void host_present(const u32 *bgra, int w, int h) {
  g_frame_count++;
  if (!g_dump_frames || g_frame_count % g_dump_frames) return;
  char name[256]; snprintf(name, sizeof name, "%s_%04d.ppm", g_dump_prefix, g_frame_count);
  FILE *f = fopen(name, "wb"); if (!f) return; fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; i++) { u8 px[3] = { (u8)(bgra[i] >> 16), (u8)(bgra[i] >> 8), (u8)bgra[i] }; fwrite(px, 1, 3, f); }
  fclose(f);
}
int host_key(int vk) { (void)vk; return 0; }
void host_exit(int code) { exit(code); }
double host_math(int op, double a, double b) {
  switch (op) { case 0: return sin(a); case 1: return cos(a); case 2: return tan(a); case 3: return exp(a); case 4: return log(a); case 5: return pow(a, b); case 6: return atan2(a, b); case 7: return asin(a); case 8: return acos(a); case 9: return atan(a); case 10: return sinh(a); case 11: return cosh(a); case 12: return tanh(a); case 13: return log10(a); case 14: return log2(a); case 15: return cbrt(a); default: return 0; }
}
void host_set_dump(int every, const char *prefix) { g_dump_frames = every; g_dump_prefix = prefix; }
int host_frames(void) { return g_frame_count; }
