/* jpegtest.c — host-side check of emu/x64/jpeg.inc against libjpeg-decoded references (tests/jpeg/ .jpg + .ppm written by Pillow).
   Usage: jpegtest file.jpg file.ppm …  → prints mean / max channel error per file; exits 1 when the mean error exceeds 3 or the max exceeds 64. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
static void *emu_alloc(size_t n) { return calloc(1, n ? n : 1); } static int emu_free(void *p) { free(p); return 1; }
static void *e_memset(void *d, int v, size_t n) { return memset(d, v, n); } static void *e_memcpy(void *d, const void *s, size_t n) { return memcpy(d, s, n); } static int e_memcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }
static void e_log(const char *f, ...) { va_list a; va_start(a, f); vfprintf(stderr, f, a); va_end(a); }
#include "../x64/jpeg.inc"
int main(int argc, char **argv) { int bad = 0;
  for (int i = 1; i + 1 < argc; i += 2) { FILE *f = fopen(argv[i], "rb"); if (!f) { printf("%s: cannot open\n", argv[i]); return 1; } fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); u8 *d = malloc(n); if (fread(d, 1, n, f) != (size_t)n) return 1; fclose(f);
    int w, h, a; u32 *px = jpg_decode(d, n, &w, &h, &a); if (!px) { printf("%s: decode failed\n", argv[i]); bad = 1; continue; }
    FILE *r = fopen(argv[i + 1], "rb"); int rw, rh, mx; if (!r || fscanf(r, "P6 %d %d %d", &rw, &rh, &mx) != 3 || rw != w || rh != h) { printf("%s: reference mismatch (%dx%d)\n", argv[i], w, h); bad = 1; continue; } fgetc(r);
    double sum = 0; int maxd = 0; for (int k = 0; k < w * h; k++) { int rr = fgetc(r), gg = fgetc(r), bb = fgetc(r); int dr = abs((int)((px[k] >> 16) & 255) - rr), dg = abs((int)((px[k] >> 8) & 255) - gg), db = abs((int)(px[k] & 255) - bb); sum += dr + dg + db; if (dr > maxd) maxd = dr; if (dg > maxd) maxd = dg; if (db > maxd) maxd = db; }
    double mean = sum / (3.0 * w * h); printf("%s: %dx%d mean=%.2f max=%d %s\n", argv[i], w, h, mean, maxd, mean <= 3 && maxd <= 64 ? "ok" : "BAD"); if (!(mean <= 3 && maxd <= 64)) bad = 1; fclose(r); }
  printf(bad ? "JPEG FAIL\n" : "JPEG OK\n"); return bad; }
