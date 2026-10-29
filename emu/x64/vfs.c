/* Nebula x64 — in-memory virtual file system + stdin queue.
 * Files live in host memory (emu_alloc, grown by doubling); paths are Windows-style, case-insensitive,
 * rooted at C:\ with the process cwd C:\nebula.  Also provides CRT-style fds (0,1,2 = console, 3.. = files). */
#include "emu.h"

#define VFS_MAX_NODES 1024
#define VFS_MAX_OPEN  128
#define VFS_MAX_FIND  32
#define VFS_PATH      512

typedef struct { char path[VFS_PATH]; u8 *data; u64 size, cap; int is_dir; int written; int used; } VNode;
typedef struct { int node; u64 pos; int used; int append; int eof; int err; int unget; int alias; } VFile;
typedef struct { int used; char dir[VFS_PATH]; char pat[128]; int cursor; int dots; } VFind;

static VNode g_nodes[VFS_MAX_NODES];
static VFile g_open[VFS_MAX_OPEN];
static VFind g_find[VFS_MAX_FIND];
static char  g_cwd[VFS_PATH] = "C:\\nebula";
static int   g_redir[3] = { -1, -1, -1 };   /* std fds: -1 = console, otherwise an open-table fd */
/* stdin */
static u8 *g_in; static u64 g_in_len, g_in_cap, g_in_pos; static int g_in_eof, g_in_wait;

static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int path_eq(const char *a, const char *b) { while (*a && lc(*a) == lc(*b)) { a++; b++; } return lc(*a) == lc(*b); }

const char *vfs_cwd(void) { return g_cwd; }

/* normalise: slashes, \\?\ prefix, relative → cwd, . and .., trailing slash */
void vfs_normalize(const char *in, char *out, size_t cap) {
  char tmp[VFS_PATH * 2]; size_t n = 0;
  if (!e_strncmp(in, "\\\\?\\", 4)) in += 4;
  int abs_drive = ((in[0] >= 'a' && in[0] <= 'z') || (in[0] >= 'A' && in[0] <= 'Z')) && in[1] == ':';
  if (abs_drive) { tmp[n++] = (char)(in[0] >= 'a' ? in[0] - 32 : in[0]); tmp[n++] = ':'; in += 2; if (*in != '\\' && *in != '/') { tmp[n++] = '\\'; } }
  else if (in[0] == '\\' || in[0] == '/') { tmp[n++] = 'C'; tmp[n++] = ':'; }
  else { size_t l = e_strlen(g_cwd); e_memcpy(tmp, g_cwd, l); n = l; tmp[n++] = '\\'; }
  for (; *in && n + 1 < sizeof tmp; in++) tmp[n++] = (*in == '/') ? '\\' : *in;
  tmp[n] = 0;
  /* resolve components */
  char *comps[128]; int nc = 0; char buf[VFS_PATH * 2]; e_memcpy(buf, tmp, n + 1);
  char *p = buf + 2; /* after "C:" */
  while (*p) {
    while (*p == '\\') p++; if (!*p) break;
    char *s = p; while (*p && *p != '\\') p++; if (*p) *p++ = 0;
    if (!e_strcmp(s, ".")) continue;
    if (!e_strcmp(s, "..")) { if (nc) nc--; continue; }
    if (nc < 128) comps[nc++] = s;
  }
  size_t o = 0; out[o++] = buf[0]; out[o++] = ':';
  for (int i = 0; i < nc; i++) { size_t l = e_strlen(comps[i]); if (o + l + 2 >= cap) break; out[o++] = '\\'; e_memcpy(out + o, comps[i], l); o += l; }
  if (nc == 0 && o + 1 < cap) out[o++] = '\\';
  out[o] = 0;
}

int vfs_find_node(const char *path) {
  char n[VFS_PATH]; vfs_normalize(path, n, sizeof n);
  for (int i = 0; i < VFS_MAX_NODES; i++) if (g_nodes[i].used && path_eq(g_nodes[i].path, n)) return i;
  if (!e_stricmp(n, "C:\\") || path_eq(n, g_cwd)) return -2;   /* implicit directories */
  return -1;
}
static int node_alloc(const char *norm) {
  for (int i = 0; i < VFS_MAX_NODES; i++) if (!g_nodes[i].used) { e_memset(&g_nodes[i], 0, sizeof g_nodes[i]); g_nodes[i].used = 1; e_snprintf(g_nodes[i].path, VFS_PATH, "%s", norm); return i; }
  return -1;
}
static void ensure_parents(const char *norm) {
  char d[VFS_PATH]; e_snprintf(d, sizeof d, "%s", norm);
  for (;;) {
    size_t l = e_strlen(d); while (l > 0 && d[l - 1] != '\\') l--; if (l <= 3) return; d[l - 1] = 0;
    if (vfs_find_node(d) >= -2 && vfs_find_node(d) != -1) return;
    int i = node_alloc(d); if (i < 0) return; g_nodes[i].is_dir = 1;
  }
}
static int grow(VNode *v, u64 need) {
  if (need <= v->cap) return 1;
  u64 nc = v->cap ? v->cap : 4096; while (nc < need) nc *= 2;
  u8 *nd = emu_alloc((size_t)nc); if (!nd) return 0;
  if (v->size) e_memcpy(nd, v->data, (size_t)v->size);
  v->data = nd; v->cap = nc; return 1;
}
/* add/replace a file (host-provided bytes are copied) */
int vfs_add(const char *path, const u8 *data, u64 n) {
  char norm[VFS_PATH]; vfs_normalize(path, norm, sizeof norm);
  int i = vfs_find_node(norm); if (i == -2) return -1;
  if (i < 0) { i = node_alloc(norm); if (i < 0) return -1; ensure_parents(norm); }
  VNode *v = &g_nodes[i]; v->is_dir = 0; v->size = 0; if (!grow(v, n)) return -1; if (n) e_memcpy(v->data, data, (size_t)n); v->size = n; v->written = 0; return i;
}
int vfs_mkdir(const char *path) {
  char norm[VFS_PATH]; vfs_normalize(path, norm, sizeof norm);
  if (vfs_find_node(norm) != -1) return 0;
  int i = node_alloc(norm); if (i < 0) return 0; g_nodes[i].is_dir = 1; ensure_parents(norm); return 1;
}
int vfs_remove(const char *path) { int i = vfs_find_node(path); if (i < 0) return 0; for (int k = 0; k < VFS_MAX_OPEN; k++) if (g_open[k].used && g_open[k].node == i) return 0; g_nodes[i].used = 0; return 1; }
int vfs_rename(const char *from, const char *to) { int i = vfs_find_node(from); if (i < 0) return 0; char norm[VFS_PATH]; vfs_normalize(to, norm, sizeof norm); int j = vfs_find_node(norm); if (j >= 0) g_nodes[j].used = 0; e_snprintf(g_nodes[i].path, VFS_PATH, "%s", norm); ensure_parents(norm); return 1; }
int vfs_chdir(const char *path) { char norm[VFS_PATH]; vfs_normalize(path, norm, sizeof norm); int i = vfs_find_node(norm); if (i == -1 || (i >= 0 && !g_nodes[i].is_dir)) return 0; e_snprintf(g_cwd, sizeof g_cwd, "%s", norm); return 1; }
/* stat: returns 0 missing, 1 file, 2 dir */
int vfs_stat(const char *path, u64 *size) { int i = vfs_find_node(path); if (i == -1) return 0; if (i == -2) { if (size) *size = 0; return 2; } if (size) *size = g_nodes[i].size; return g_nodes[i].is_dir ? 2 : 1; }
const VNodeInfo *vfs_node_info(int i, VNodeInfo *out) { if (i < 0 || i >= VFS_MAX_NODES || !g_nodes[i].used) return 0; out->path = g_nodes[i].path; out->data = g_nodes[i].data; out->size = g_nodes[i].size; out->is_dir = g_nodes[i].is_dir; out->written = g_nodes[i].written; return out; }
int vfs_node_count(void) { return VFS_MAX_NODES; }

/* ---------- open files (fd ≥ 3) ---------- */
/* access: 1 read, 2 write; disp: 1 CREATE_NEW, 2 CREATE_ALWAYS, 3 OPEN_EXISTING, 4 OPEN_ALWAYS, 5 TRUNCATE_EXISTING; returns fd or -errno (-2 ENOENT, -17 EEXIST, -13 EACCES, -24 EMFILE, -21 EISDIR) */
int vfs_open(const char *path, int access, int disp, int append) {
  char norm[VFS_PATH]; vfs_normalize(path, norm, sizeof norm);
  int i = vfs_find_node(norm);
  if (i == -2 || (i >= 0 && g_nodes[i].is_dir)) { if (access & 2) return -21; /* allow opening a dir read-only (handle used for info) */ }
  if (i == -1) {
    if (disp == 3 || disp == 5) return -2;
    i = node_alloc(norm); if (i < 0) return -24; ensure_parents(norm); g_nodes[i].written = 1;
  } else if (i >= 0) {
    if (disp == 1) return -17;
    if (disp == 2 || disp == 5) { g_nodes[i].size = 0; g_nodes[i].written = 1; }
  }
  for (int k = 0; k < VFS_MAX_OPEN; k++) if (!g_open[k].used) { e_memset(&g_open[k], 0, sizeof g_open[k]); g_open[k].used = 1; g_open[k].node = i; g_open[k].append = append; g_open[k].unget = -1; return k + 3; }
  return -24;
}
static VFile *vf(int fd) { fd -= 3; if (fd < 0 || fd >= VFS_MAX_OPEN || !g_open[fd].used) return 0; return &g_open[fd]; }
int vfs_close(int fd) { if (fd >= 0 && fd < 3) { if (g_redir[fd] != -1) { vfs_close(g_redir[fd]); g_redir[fd] = -1; } return 1; } VFile *f = vf(fd); if (!f) return 0; f->used = 0; return 1; }
int vfs_is_open(int fd) { return vf(fd) != 0; }
int vfs_fd_node(int fd) { VFile *f = vf(fd); return f ? f->node : -1; }
s64 vfs_read(int fd, u8 *buf, u64 n) {
  VFile *f = vf(fd); if (!f) return -1; if (f->node < 0) return 0; VNode *v = &g_nodes[f->node]; if (v->is_dir) return -1;
  u64 o = 0;
  if (f->unget >= 0 && n) { buf[o++] = (u8)f->unget; f->unget = -1; }
  u64 avail = f->pos < v->size ? v->size - f->pos : 0; u64 c = n - o < avail ? n - o : avail;
  if (c) e_memcpy(buf + o, v->data + f->pos, (size_t)c); f->pos += c; o += c; if (o == 0 && n) f->eof = 1; return (s64)o;
}
s64 vfs_write(int fd, const u8 *buf, u64 n) {
  VFile *f = vf(fd); if (!f || f->node < 0) return -1; VNode *v = &g_nodes[f->node]; if (v->is_dir) return -1;
  if (f->append) f->pos = v->size;
  if (!grow(v, f->pos + n)) return -1;
  if (f->pos > v->size) e_memset(v->data + v->size, 0, (size_t)(f->pos - v->size));
  e_memcpy(v->data + f->pos, buf, (size_t)n); f->pos += n; if (f->pos > v->size) v->size = f->pos; v->written = 1; return (s64)n;
}
s64 vfs_seek(int fd, s64 off, int whence) {
  VFile *f = vf(fd); if (!f || f->node < 0) return -1; VNode *v = &g_nodes[f->node];
  s64 base = whence == 0 ? 0 : whence == 1 ? (s64)f->pos : (s64)v->size; s64 np = base + off; if (np < 0) return -1; f->pos = (u64)np; f->eof = 0; f->unget = -1; return np;
}
s64 vfs_tell(int fd) { VFile *f = vf(fd); return f ? (s64)f->pos : -1; }
s64 vfs_size(int fd) { VFile *f = vf(fd); if (!f || f->node < 0) return -1; return (s64)g_nodes[f->node].size; }
int vfs_eof(int fd) { VFile *f = vf(fd); return f ? f->eof : 1; }
void vfs_clearerr(int fd) { VFile *f = vf(fd); if (f) { f->eof = 0; f->err = 0; } }
int vfs_ungetc(int fd, int ch) { VFile *f = vf(fd); if (!f) return -1; f->unget = ch & 0xff; f->eof = 0; return ch & 0xff; }
int vfs_truncate(int fd) { VFile *f = vf(fd); if (!f || f->node < 0) return 0; g_nodes[f->node].size = f->pos; g_nodes[f->node].written = 1; return 1; }
int vfs_fd_isdir(int fd) { VFile *f = vf(fd); if (!f) return 0; return f->node == -2 || (f->node >= 0 && g_nodes[f->node].is_dir); }
const char *vfs_fd_path(int fd) { VFile *f = vf(fd); if (!f || f->node < 0) return g_cwd; return g_nodes[f->node].path; }
/* resolve any fd to: -1 console stdin, 1/2 console out, >=3 file, 0 invalid */
int vfs_resolve(int fd) {
  if (fd >= 0 && fd < 3) { if (g_redir[fd] == -1) return fd == 0 ? -1 : fd; fd = g_redir[fd]; }
  VFile *f = vf(fd); if (!f) return 0; if (f->alias) return f->alias; return fd;
}
static int slot_alloc(void) { for (int k = 0; k < VFS_MAX_OPEN; k++) if (!g_open[k].used) return k; return -1; }
int vfs_dup(int fd) {
  int k = slot_alloc(); if (k < 0) return -1;
  if (fd >= 0 && fd < 3) { if (g_redir[fd] == -1) { e_memset(&g_open[k], 0, sizeof g_open[k]); g_open[k].used = 1; g_open[k].node = -1; g_open[k].unget = -1; g_open[k].alias = fd == 0 ? -1 : fd; return k + 3; } fd = g_redir[fd]; }
  VFile *f = vf(fd); if (!f) return -1; g_open[k] = *f; return k + 3;
}
int vfs_dup2(int src, int dst) {
  if (src == dst) return dst;
  if (dst >= 0 && dst < 3) {
    int r = vfs_resolve(src); if (!r) return -1;
    if (g_redir[dst] != -1) vfs_close(g_redir[dst]);
    if (r < 3 && r != 0) { g_redir[dst] = -1; return dst; }
    int d = vfs_dup(src); if (d < 0) return -1; g_redir[dst] = d; return dst;
  }
  VFile *f = vf(dst); if (f) f->used = 0;
  if (src >= 0 && src < 3) { int d = vfs_dup(src); if (d < 0) return -1; if (d != dst) { g_open[dst - 3] = g_open[d - 3]; g_open[d - 3].used = 0; } return dst; }
  VFile *sf = vf(src); if (!sf || dst < 3 || dst - 3 >= VFS_MAX_OPEN) return -1; g_open[dst - 3] = *sf; return dst;
}
void vfs_close_all(void) { for (int k = 0; k < VFS_MAX_OPEN; k++) g_open[k].used = 0; g_redir[0] = g_redir[1] = g_redir[2] = -1; for (int k = 0; k < VFS_MAX_FIND; k++) g_find[k].used = 0; g_in_pos = 0; g_in_len = 0; g_in_eof = 0; g_in_wait = 0; e_snprintf(g_cwd, sizeof g_cwd, "C:\\nebula"); }

/* ---------- directory enumeration (FindFirstFile semantics) ---------- */
static int glob_match(const char *pat, const char *s) {
  if (!*pat) return !*s;
  if (*pat == '*') { for (;; s++) { if (glob_match(pat + 1, s)) return 1; if (!*s) return 0; } }
  if (!*s) return 0;
  if (*pat == '?' || lc(*pat) == lc(*s)) return glob_match(pat + 1, s + 1);
  return 0;
}
static const char *basename_of(const char *p) { const char *b = p; for (const char *q = p; *q; q++) if (*q == '\\') b = q + 1; return b; }
static int dir_of(const char *norm, char *dir, size_t cap) { const char *b = basename_of(norm); size_t l = (size_t)(b - norm); if (l > 0 && norm[l - 1] == '\\' && l > 3) l--; if (l >= cap) l = cap - 1; e_memcpy(dir, norm, l); dir[l] = 0; return 1; }
static int in_dir(const char *path, const char *dir) {   /* path's parent == dir ? */
  size_t dl = e_strlen(dir); if (e_strncmp(path, dir, 0) != 0) return 0;
  for (size_t i = 0; i < dl; i++) if (lc(path[i]) != lc(dir[i])) return 0;
  const char *rest = path + dl; if (dl == 3) rest = path + 3; else { if (*rest != '\\') return 0; rest++; }
  if (!*rest) return 0; for (const char *q = rest; *q; q++) if (*q == '\\') return 0; return 1;
}
/* returns find handle index ≥ 0, -1 none, fills first match via vfs_find_next */
int vfs_find_first(const char *pattern) {
  char norm[VFS_PATH]; vfs_normalize(pattern, norm, sizeof norm);
  int h = -1; for (int k = 0; k < VFS_MAX_FIND; k++) if (!g_find[k].used) { h = k; break; } if (h < 0) return -1;
  VFind *f = &g_find[h]; e_memset(f, 0, sizeof *f); f->used = 1;
  const char *pat = basename_of(norm); dir_of(norm, f->dir, sizeof f->dir);
  /* pattern without wildcard that names a directory → enumerate that single entry */
  e_snprintf(f->pat, sizeof f->pat, "%s", pat);
  if (!e_strchr(pat, '*') && !e_strchr(pat, '?')) { int i = vfs_find_node(norm); if (i == -1) { f->used = 0; return -1; } }
  else if (vfs_find_node(f->dir) == -1) { f->used = 0; return -1; }
  if (!e_strcmp(pat, "*") || !e_strcmp(pat, "*.*")) f->dots = 2;
  return h;
}
/* next entry: returns 1 and fills name/size/is_dir, or 0 when done */
int vfs_find_next(int h, char *name, size_t cap, u64 *size, int *is_dir) {
  if (h < 0 || h >= VFS_MAX_FIND || !g_find[h].used) return 0; VFind *f = &g_find[h];
  if (f->dots > 0) { e_snprintf(name, cap, f->dots == 2 ? "." : ".."); *size = 0; *is_dir = 1; f->dots--; return 1; }
  for (; f->cursor < VFS_MAX_NODES; f->cursor++) {
    VNode *v = &g_nodes[f->cursor]; if (!v->used || !in_dir(v->path, f->dir)) continue;
    const char *b = basename_of(v->path); if (!glob_match(f->pat, b)) continue;
    e_snprintf(name, cap, "%s", b); *size = v->size; *is_dir = v->is_dir; f->cursor++; return 1;
  }
  return 0;
}
void vfs_find_close(int h) { if (h >= 0 && h < VFS_MAX_FIND) g_find[h].used = 0; }

/* ---------- stdin ---------- */
void vfs_stdin_push(const u8 *d, u64 n) {
  if (g_in_pos && g_in_pos == g_in_len) { g_in_pos = 0; g_in_len = 0; }
  if (g_in_len + n > g_in_cap) { u64 nc = g_in_cap ? g_in_cap : 4096; while (nc < g_in_len + n) nc *= 2; u8 *nb = emu_alloc((size_t)nc); if (g_in_len) e_memcpy(nb, g_in, (size_t)g_in_len); g_in = nb; g_in_cap = nc; }
  e_memcpy(g_in + g_in_len, d, (size_t)n); g_in_len += n; g_in_wait = 0;
}
void vfs_stdin_eof(void) { g_in_eof = 1; g_in_wait = 0; }
int  vfs_stdin_waiting(void) { return g_in_wait; }
/* read up to n bytes (stops after a newline so line-oriented programs behave); returns -2 when it would block */
s64 vfs_stdin_read(u8 *buf, u64 n) {
  if (g_in_pos >= g_in_len) { if (g_in_eof) return 0; g_in_wait = 1; return -2; }
  u64 o = 0; while (o < n && g_in_pos < g_in_len) { u8 ch = g_in[g_in_pos++]; buf[o++] = ch; if (ch == '\n') break; }
  return (s64)o;
}
int vfs_stdin_available(void) { return g_in_pos < g_in_len || g_in_eof; }
