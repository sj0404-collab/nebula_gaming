/* Entry points shared by the wasm64 build (exported to JS) and the native test runner. */
#include "emu.h"

static Mem g_mem; static Cpu g_cpu; static Win32 g_w32; static int g_loaded;
static char g_inspect[16384];

#ifdef __wasm__
#define EXPORT(name) __attribute__((export_name(#name)))
#else
#define EXPORT(name)
#endif

EXPORT(x64_alloc) void *x64_alloc(size_t n) { return emu_alloc(n); }
EXPORT(x64_inspect) const char *x64_inspect(const u8 *bytes, size_t len) { pe_inspect(bytes, len, g_inspect, sizeof g_inspect); return g_inspect; }
EXPORT(x64_load) int x64_load(const u8 *bytes, size_t len, const char *name, const char *cmdline) {
  e_memset(&g_mem, 0, sizeof g_mem); cpu_reset(&g_cpu);
  w32_init(&g_w32, &g_cpu, &g_mem);
  int rc = pe_load(&g_w32, bytes, len, name ? name : "app.exe");
  if (rc) return rc;
  w32_start(&g_w32, cmdline ? cmdline : name ? name : "app.exe");
  g_loaded = 1; return 0;
}
EXPORT(x64_run) int x64_run(u64 budget) { if (!g_loaded) return RUN_FAULT; int r = cpu_run(&g_cpu, budget); if (g_w32.dirty && g_w32.screen) { host_present(g_w32.screen, g_w32.screen_w, g_w32.screen_h); g_w32.dirty = 0; g_w32.frames_presented++; } return r; }
EXPORT(x64_key) void x64_key(int vk, int down) { w32_key(&g_w32, vk, down); }
EXPORT(x64_mouse) void x64_mouse(int x, int y, int ev) { w32_mouse(&g_w32, x, y, ev); }
EXPORT(x64_icount) u64 x64_icount(void) { return g_cpu.icount; }
EXPORT(x64_exit_code) int x64_exit_code(void) { return g_cpu.exit_code; }
EXPORT(x64_error) const char *x64_error(void) { return g_cpu.err; }
EXPORT(x64_error_rip) u64 x64_error_rip(void) { return g_cpu.err_rip; }
static char g_regs[512];
EXPORT(x64_regs) const char *x64_regs(void) { static const char *nm[16] = {"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15"}; int o = 0; for (int i = 0; i < 16; i++) o += e_snprintf(g_regs + o, sizeof g_regs - o, "%s=%llx ", nm[i], (unsigned long long)g_cpu.r[i]); u64 sp = g_cpu.r[4]; const u8 *st = mem_ptr(&g_mem, sp, 32, 0); if (st) for (int k = 0; k < 4; k++) { u64 v = 0; for (int b = 7; b >= 0; b--) v = (v << 8) | st[k * 8 + b]; o += e_snprintf(g_regs + o, sizeof g_regs - o, "[rsp+%d]=%llx ", k * 8, (unsigned long long)v); } return g_regs; }
EXPORT(x64_rip) u64 x64_rip(void) { return g_cpu.rip; }
EXPORT(x64_report) const char *x64_report(void) { return g_w32.report; }
EXPORT(x64_console) const char *x64_console(void) { return g_w32.con; }
EXPORT(x64_screen_w) int x64_screen_w(void) { return g_w32.screen ? g_w32.screen_w : 0; }
EXPORT(x64_screen_h) int x64_screen_h(void) { return g_w32.screen ? g_w32.screen_h : 0; }
EXPORT(x64_screen) const u32 *x64_screen(void) { return g_w32.screen; }
EXPORT(x64_title) const char *x64_title(void) { return g_w32.win.title; }
EXPORT(x64_subsystem) int x64_subsystem(void) { return g_w32.subsystem; }
EXPORT(x64_sleep_ms) double x64_sleep_ms(void) { return w32_sleep_remaining_ms(&g_w32); }
EXPORT(x64_mem_used) u64 x64_mem_used(void) { return emu_alloc_used(); }
EXPORT(x64_has_window) int x64_has_window(void) { return g_w32.win.created; }
EXPORT(x64_api_stats) const char *x64_api_stats(void) {
  static char buf[8192]; int p = 0; buf[0] = 0;
  for (int i = 2; i < g_w32.nslots && p < (int)sizeof buf - 128; i++) if (g_w32.slots[i].calls) p += e_snprintf(buf + p, sizeof buf - p, "%s%s!%s:%d", p ? "," : "", g_w32.slots[i].dll, g_w32.slots[i].name, g_w32.slots[i].calls);
  return buf;
}
/* bytes of the error instruction for the report */
EXPORT(x64_error_bytes) const char *x64_error_bytes(void) {
  static char buf[64]; int p = 0; for (int i = 0; i < 12; i++) { u8 *b = mem_ptr(&g_mem, g_cpu.err_rip + i, 1, 0); if (!b) break; p += e_snprintf(buf + p, sizeof buf - p, "%02X ", *b); } return buf;
}
