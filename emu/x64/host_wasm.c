/* wasm64 host glue: imports provided by the JS side (src/lib/x64/runtime.ts) */
#include "emu.h"
__attribute__((import_module("env"), import_name("host_write"))) void js_write(int fd, const char *p, size_t n);
__attribute__((import_module("env"), import_name("host_now_ms"))) double js_now_ms(void);
__attribute__((import_module("env"), import_name("host_present"))) void js_present(const u32 *bgra, int w, int h);
__attribute__((import_module("env"), import_name("host_math"))) double js_math(int op, double a, double b);
void host_write(int fd, const char *p, size_t n) { js_write(fd, p, n); }
double host_now_ms(void) { return js_now_ms(); }
void host_present(const u32 *bgra, int w, int h) { js_present(bgra, w, h); }
int host_key(int vk) { (void)vk; return 0; }
void host_exit(int code) { (void)code; }
double host_math(int op, double a, double b) { return js_math(op, a, b); }
