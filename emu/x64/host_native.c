/* Native host for tests: console on stdout, framebuffer dumped to PPM on demand. */
#define _GNU_SOURCE
#include "emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

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
#include <sys/mman.h>
void host_mem_discard(void *p, size_t n) {   /* drop whole pages inside [p, p+n): RSS shrinks, re-touched pages read as zero (= Windows re-commit semantics) */
  uintptr_t a = ((uintptr_t)p + 4095) & ~(uintptr_t)4095, e = ((uintptr_t)p + n) & ~(uintptr_t)4095;
  if (e > a) madvise((void *)a, e - a, MADV_DONTNEED);
  if (a > (uintptr_t)p) memset(p, 0, (size_t)(a - (uintptr_t)p < n ? a - (uintptr_t)p : n));
  if (e > a && (uintptr_t)p + n > e) memset((void *)e, 0, (size_t)((uintptr_t)p + n - e)); }
double host_math(int op, double a, double b) {
  switch (op) { case 0: return sin(a); case 1: return cos(a); case 2: return tan(a); case 3: return exp(a); case 4: return log(a); case 5: return pow(a, b); case 6: return atan2(a, b); case 7: return asin(a); case 8: return acos(a); case 9: return atan(a); case 10: return sinh(a); case 11: return cosh(a); case 12: return tanh(a); case 13: return log10(a); case 14: return log2(a); case 15: return cbrt(a); default: return 0; }
}
void host_set_dump(int every, const char *prefix) { g_dump_frames = every; g_dump_prefix = prefix; }
int host_frames(void) { return g_frame_count; }

/* ---- TCP networking on the host (IPv4) ---- */
static int wsa_of_errno(int e) {
  switch (e) { case ECONNREFUSED: return 10061; case ETIMEDOUT: return 10060; case EHOSTUNREACH: return 10065; case ENETUNREACH: return 10051; case ECONNRESET: return 10054;
    case ECONNABORTED: case EPIPE: return 10053; case EADDRINUSE: return 10048; case EADDRNOTAVAIL: return 10049; case EACCES: return 10013; case ENETDOWN: return 10050; default: return 10022; } }
int host_net_resolve(const char *name, u8 *ip4s, int max) {
  struct addrinfo hints, *res = 0, *p; memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  int r = getaddrinfo(name, 0, &hints, &res); if (r) return r == EAI_AGAIN || r == EAI_SYSTEM ? -1 : 0;
  int n = 0; for (p = res; p && n < max; p = p->ai_next) if (p->ai_family == AF_INET) { memcpy(ip4s + n * 4, &((struct sockaddr_in *)p->ai_addr)->sin_addr, 4); n++; }
  freeaddrinfo(res); return n; }
int host_net_connect(const u8 *ip4, int port, int *wsa_err) {
  int s = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0); if (s < 0) { if (wsa_err) *wsa_err = wsa_of_errno(errno); return -1; }
  int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons((u16)port); memcpy(&a.sin_addr, ip4, 4);
  if (connect(s, (struct sockaddr *)&a, sizeof a) < 0 && errno != EINPROGRESS) { if (wsa_err) *wsa_err = wsa_of_errno(errno); close(s); return -1; }
  return s; }
int host_net_listen(const u8 *ip4, int port, int backlog, int *wsa_err) {
  int s = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0); if (s < 0) { if (wsa_err) *wsa_err = wsa_of_errno(errno); return -1; }
  int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons((u16)port); memcpy(&a.sin_addr, ip4, 4);
  if (bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, backlog > 0 ? backlog : 16) < 0) { if (wsa_err) *wsa_err = wsa_of_errno(errno); close(s); return -1; }
  return s; }
int host_net_accept(int s, u8 *peer_ip4, int *peer_port) {
  struct sockaddr_in a; socklen_t al = sizeof a; int c = accept4(s, (struct sockaddr *)&a, &al, SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (c < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? -2 : -3;
  int one = 1; setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  if (peer_ip4) memcpy(peer_ip4, &a.sin_addr, 4); if (peer_port) *peer_port = ntohs(a.sin_port); return c; }
int host_net_status(int s) {
  struct pollfd p = { s, POLLOUT, 0 }; int r = poll(&p, 1, 0); if (r <= 0) return 0;
  int err = 0; socklen_t l = sizeof err; getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &l); if (err) return -wsa_of_errno(err); return 1; }
s64 host_net_read(int s, u8 *buf, u64 n) {
  ssize_t r = recv(s, buf, (size_t)n, MSG_DONTWAIT); if (r >= 0) return (s64)r;
  return errno == EAGAIN || errno == EWOULDBLOCK ? -2 : -3; }
s64 host_net_write(int s, const u8 *buf, u64 n) {
  u64 done = 0; int waits = 0;
  while (done < n) { ssize_t r = send(s, buf + done, (size_t)(n - done), MSG_NOSIGNAL | MSG_DONTWAIT);
    if (r > 0) { done += (u64)r; continue; }
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { if (++waits > 200) break; struct pollfd p = { s, POLLOUT, 0 }; poll(&p, 1, 50); continue; }   /* up to ~10 s for room */
    return done ? (s64)done : -3; }
  return (s64)done; }
int host_net_readable(int s) { struct pollfd p = { s, POLLIN, 0 }; return poll(&p, 1, 0) > 0; }
int host_net_avail(int s) { int n = 0; if (ioctl(s, FIONREAD, &n) < 0) return 0; return n; }
void host_net_shutdown(int s, int how) { shutdown(s, how == 0 ? SHUT_RD : how == 1 ? SHUT_WR : SHUT_RDWR); }
void host_net_close(int s) { close(s); }
int host_net_name(int s, int peer, u8 *ip4, int *port) {
  struct sockaddr_in a; socklen_t al = sizeof a; int r = peer ? getpeername(s, (struct sockaddr *)&a, &al) : getsockname(s, (struct sockaddr *)&a, &al);
  if (r < 0) return 0; if (ip4) memcpy(ip4, &a.sin_addr, 4); if (port) *port = ntohs(a.sin_port); return 1; }
