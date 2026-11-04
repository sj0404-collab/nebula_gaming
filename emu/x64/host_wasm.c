/* wasm64 host glue: imports provided by the JS side (src/lib/x64/runtime.ts) */
#include "emu.h"
__attribute__((import_module("env"), import_name("host_write"))) void js_write(int fd, const char *p, size_t n);
__attribute__((import_module("env"), import_name("host_now_ms"))) double js_now_ms(void);
__attribute__((import_module("env"), import_name("host_present"))) void js_present(const u32 *bgra, int w, int h);
__attribute__((import_module("env"), import_name("host_math"))) double js_math(int op, double a, double b);
void host_write(int fd, const char *p, size_t n) { js_write(fd, p, n); }
double host_now_ms(void) { return js_now_ms(); }
__attribute__((import_module("env"), import_name("host_wall_ms"))) double js_wall_ms(void);
double host_wall_ms(void) { return js_wall_ms(); }
void host_present(const u32 *bgra, int w, int h) { js_present(bgra, w, h); }
int host_key(int vk) { (void)vk; return 0; }
void host_exit(int code) { (void)code; }
void host_mem_discard(void *p, size_t n) { e_memset(p, 0, n); }   /* linear memory cannot shrink; keep the zero-on-recommit semantics */
double host_math(int op, double a, double b) { return js_math(op, a, b); }
/* persistent virtual disk: the page keeps C:\ in IndexedDB (src/lib/x64/disk.ts). op 1 = a written file was closed → hand the bytes to JS,
   3 = mkdir, 4 = delete (size = is_dir), 5 = rename. op 2 (exists / load back) returns 0: the bytes stay in wasm memory, nothing is evicted. */
__attribute__((import_module("env"), import_name("host_fs_write"))) int js_fs_write(const char *path, const u8 *data, u64 n);
__attribute__((import_module("env"), import_name("host_fs_mkdir"))) int js_fs_mkdir(const char *path);
__attribute__((import_module("env"), import_name("host_fs_remove"))) int js_fs_remove(const char *path, int is_dir);
__attribute__((import_module("env"), import_name("host_fs_rename"))) int js_fs_rename(const char *from, const char *to);
__attribute__((import_module("env"), import_name("host_fs_size"))) s64 js_fs_size(const char *path);          /* -1: the page cannot hand the bytes back synchronously */
__attribute__((import_module("env"), import_name("host_fs_read"))) int js_fs_read(const char *path, u8 *dst, u64 n);
/* file-data blocks: the wasm arena never frees, so blocks a file no longer needs (closed and persisted → evicted; grown → old
   buffer) are kept in a pool and handed out again. Sizes are rounded up to 64 KB classes so a re-opened or re-grown file finds
   its block again instead of taking fresh arena pages. */
#define POOL_MAX 1024
static struct { u8 *p; size_t cap; } g_pool[POOL_MAX]; static int g_pool_n = 0;
static size_t pool_round(size_t n) { return n <= 4096 ? 4096 : (n + 65535) & ~(size_t)65535; }
void *vfs_mem_alloc(size_t n) {
  size_t want = pool_round(n); int best = -1;
  for (int i = 0; i < g_pool_n; i++) if (g_pool[i].cap >= want && (best < 0 || g_pool[i].cap < g_pool[best].cap)) { best = i; if (g_pool[i].cap == want) break; }
  if (best >= 0 && g_pool[best].cap <= want * 2) { u8 *p = g_pool[best].p; size_t cap = g_pool[best].cap; g_pool[best] = g_pool[--g_pool_n]; e_memset(p, 0, cap); return p; }
  return emu_alloc(want);
}
void vfs_mem_free(void *p, size_t cap) {
  if (!p) return; size_t c = pool_round(cap);
  if (g_pool_n < POOL_MAX) { g_pool[g_pool_n].p = p; g_pool[g_pool_n].cap = c; g_pool_n++; }
}
int host_fs_op(int op, const char *a, const char *b, u8 **data, u64 *size) {
  if (op == 1) return a && data && size ? js_fs_write(a, *data, *size) : 0;
  if (op == 2) {   /* exists on the page's disk? / load the bytes into a pooled block (node_load); the page answers from its in-memory copy */
    if (!a) return 0; s64 n = js_fs_size(a); if (n < 0) return 0; if (!data || !size) return 1;
    u8 *buf = n ? vfs_mem_alloc((size_t)n) : 0; if (n && !buf) return 0;
    if (n && !js_fs_read(a, buf, (u64)n)) { vfs_mem_free(buf, (size_t)n); return 0; }
    *data = buf; *size = (u64)n; return 1;
  }
  if (op == 3) return a ? js_fs_mkdir(a) : 0;
  if (op == 4) return a ? js_fs_remove(a, size && *size ? 1 : 0) : 0;
  if (op == 5) return a && b ? js_fs_rename(a, b) : 0;
  if (op == 6) { if (data && *data && size) vfs_mem_free(*data, (size_t)*size); return 1; }
  return 0;
}
/* networking: no raw TCP from a browser page — every call fails cleanly (WSAENETDOWN); a WebSocket relay can plug in here later */
int host_net_resolve(const char *name, u8 *ip4s, int max) { (void)name; (void)ip4s; (void)max; return -1; }
int host_net_connect(const u8 *ip4, int port, int *wsa_err) { (void)ip4; (void)port; if (wsa_err) *wsa_err = 10050; return -1; }
int host_net_listen(const u8 *ip4, int port, int backlog, int *wsa_err) { (void)ip4; (void)port; (void)backlog; if (wsa_err) *wsa_err = 10050; return -1; }
int host_net_accept(int s, u8 *peer_ip4, int *peer_port) { (void)s; (void)peer_ip4; (void)peer_port; return -3; }
int host_net_status(int s) { (void)s; return -10050; }
s64 host_net_read(int s, u8 *buf, u64 n) { (void)s; (void)buf; (void)n; return -3; }
s64 host_net_write(int s, const u8 *buf, u64 n) { (void)s; (void)buf; (void)n; return -3; }
int host_net_readable(int s) { (void)s; return 1; }
int host_net_avail(int s) { (void)s; return 0; }
void host_net_shutdown(int s, int how) { (void)s; (void)how; }
void host_net_close(int s) { (void)s; }
int host_net_name(int s, int peer, u8 *ip4, int *port) { (void)s; (void)peer; (void)ip4; (void)port; return 0; }
/* GPU back end (WebGL2 in src/lib/x64/gfx.ts) */
__attribute__((import_module("env"), import_name("host_gfx_init"))) int js_gfx_init(void);
__attribute__((import_module("env"), import_name("host_gfx_shader"))) int js_gfx_shader(int kind, const char *glsl);
__attribute__((import_module("env"), import_name("host_gfx_buffer"))) void js_gfx_buffer(int id, const u8 *data, u32 len, u32 bind);
__attribute__((import_module("env"), import_name("host_gfx_texture"))) void js_gfx_texture(int id, int w, int h, int fmt, const u8 *px, u32 pitch);
__attribute__((import_module("env"), import_name("host_gfx_clear"))) void js_gfx_clear(int tex_id, const float *rgba);
__attribute__((import_module("env"), import_name("host_gfx_clear_depth"))) void js_gfx_clear_depth(int tex_id, float depth);
__attribute__((import_module("env"), import_name("host_gfx_draw"))) void js_gfx_draw(const u32 *cmd);
__attribute__((import_module("env"), import_name("host_gfx_readback"))) void js_gfx_readback(int tex_id, u32 *bgra);
__attribute__((import_module("env"), import_name("host_gfx_free"))) void js_gfx_free(int id);
int host_gfx_init(void) { return js_gfx_init(); }
int host_gfx_shader(int kind, const char *glsl) { return js_gfx_shader(kind, glsl); }
void host_gfx_buffer(int id, const u8 *data, u32 len, u32 bind) { js_gfx_buffer(id, data, len, bind); }
void host_gfx_texture(int id, int w, int h, int fmt, const u8 *px, u32 pitch) { js_gfx_texture(id, w, h, fmt, px, pitch); }
void host_gfx_clear(int tex_id, const float *rgba) { js_gfx_clear(tex_id, rgba); }
void host_gfx_clear_depth(int tex_id, float depth) { js_gfx_clear_depth(tex_id, depth); }
void host_gfx_draw(const u32 *cmd) { js_gfx_draw(cmd); }
void host_gfx_readback(int tex_id, u32 *bgra) { js_gfx_readback(tex_id, bgra); }
void host_gfx_free(int id) { js_gfx_free(id); }
__attribute__((import_module("env"), import_name("host_gl_init"))) int js_gl_init(int w, int h);
__attribute__((import_module("env"), import_name("host_gl_probe"))) int js_gl_probe(int id);
__attribute__((import_module("env"), import_name("host_gl_call"))) u64 js_gl_call(int id, const u64 *args, int n);
__attribute__((import_module("env"), import_name("host_gl_present"))) void js_gl_present(u32 *bgra, int w, int h);
int host_gl_init(int w, int h) { return js_gl_init(w, h); }
int host_gl_probe(int id) { return js_gl_probe(id); }
u64 host_gl_call(int id, const u64 *args, int n) { return js_gl_call(id, args, n); }
void host_gl_present(u32 *bgra, int w, int h) { js_gl_present(bgra, w, h); }

__attribute__((import_module("env"), import_name("host_audio_open"))) int js_audio_open(int rate);
__attribute__((import_module("env"), import_name("host_audio_write"))) void js_audio_write(const float *lr, int frames);
__attribute__((import_module("env"), import_name("host_audio_queued"))) int js_audio_queued(void);
int host_audio_open(int rate) { return js_audio_open(rate); }
void host_audio_write(const float *lr, int frames) { js_audio_write(lr, frames); }
int host_audio_queued(void) { return js_audio_queued(); }
