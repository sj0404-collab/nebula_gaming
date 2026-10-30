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
