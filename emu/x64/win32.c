/* Win32 personality: import trampolines, process environment, kernel32 + UCRT/msvcrt shims, guest callbacks. */
#include "emu.h"
#include <stdarg.h>

#define ARG(n) w32_arg(c, n)
#define RET(v) (c->r[RAX] = (u64)(v))
#define W (c->w)

enum { CONT_STARTUP = 1, CONT_INITTERM, CONT_ATEXIT, CONT_RET_RAX, CONT_RET_VAL, CONT_CREATEWND, CONT_QSORT };
enum { SLOT_STARTUP = 0, SLOT_CONT = 1, SLOT_FIRST = 2 };
#define SLOT_ADDR(i) (G_SHIM_BASE + (u64)(i) * SHIM_SLOT_SIZE)
#define TEB_OFF   0x10000
#define PEB_OFF   0x14000
#define PARAMS_OFF 0x15000
#define STR_OFF   0x16000

u64 w32_arg(Cpu *c, int n) {
  switch (n) { case 0: return c->r[RCX]; case 1: return c->r[RDX]; case 2: return c->r[R8]; case 3: return c->r[R9]; default: return rd64(c, c->r[RSP] + 0x20 + (u64)(n - 4) * 8); }
}
static u64 g_str_cur;   /* bump pointer inside shim region for strings/structs */
static u64 shim_bump(Win32 *w, u64 n) { (void)w; n = (n + 15) & ~15ull; u64 a = g_str_cur; if (a + n > G_SHIM_BASE + G_SHIM_SIZE) return 0; g_str_cur += n; return a; }
u64 w32_guest_strdup(Win32 *w, const char *s) { size_t n = e_strlen(s) + 1; u64 a = w32_heap_alloc(w, n, 0); u8 *p = mem_ptr(w->mem, a, n, 1); if (p) e_memcpy(p, s, n); return a; }
static u64 guest_wstrdup(Win32 *w, const char *s) { size_t n = e_strlen(s) + 1; u64 a = w32_heap_alloc(w, n * 2, 1); for (size_t i = 0; i < n; i++) wr16(w->cpu, a + i * 2, (u8)s[i]); return a; }

void w32_report(Win32 *w, const char *fmt, ...) {
  if (w->report_len >= (int)sizeof w->report - 2) return;
  va_list ap; va_start(ap, fmt); int n = e_vsnprintf(w->report + w->report_len, sizeof w->report - w->report_len - 1, fmt, ap); va_end(ap);
  w->report_len += n; if (w->report_len > (int)sizeof w->report - 2) w->report_len = sizeof w->report - 2;
  w->report[w->report_len++] = '\n'; w->report[w->report_len] = 0;
}

/* ---------- heap ---------- */
static int heap_grow(Win32 *w, u64 need) {
  u64 sz = need > (16ull << 20) ? ((need + 0xffff) & ~0xffffull) : (16ull << 20);
  if (!mem_map(w->mem, w->heap_end, sz, "heap")) return 0;
  w->heap_end += sz; return 1;
}
u64 w32_heap_alloc(Win32 *w, u64 size, int zero) {
  if (!size) size = 1; size = (size + 15) & ~15ull;
  /* first fit among freed blocks */
  for (int i = 0; i < w->nblocks; i++) if (w->blocks[i].free && w->blocks[i].size >= size) {
    HeapBlock *b = &w->blocks[i];
    if (b->size >= size + 64 && w->nblocks < 65536) { HeapBlock nb = { b->base + size, b->size - size, 1 }; b->size = size; w->blocks[w->nblocks++] = nb; }
    b->free = 0; if (zero) { u8 *p = mem_ptr(w->mem, b->base, b->size, 1); if (p) e_memset(p, 0, (size_t)b->size); } return b->base;
  }
  if (w->heap_cur + size > w->heap_end) { if (!heap_grow(w, w->heap_cur + size - w->heap_end)) return 0; }
  u64 a = w->heap_cur; w->heap_cur += size;
  if (w->nblocks < 65536) { HeapBlock nb = { a, size, 0 }; w->blocks[w->nblocks++] = nb; }
  return a;
}
static HeapBlock *heap_find(Win32 *w, u64 addr) { for (int i = w->nblocks - 1; i >= 0; i--) if (w->blocks[i].base == addr) return &w->blocks[i]; return 0; }
int w32_heap_free(Win32 *w, u64 addr) { HeapBlock *b = heap_find(w, addr); if (!b || b->free) return 0; b->free = 1; return 1; }
u64 w32_heap_size(Win32 *w, u64 addr) { HeapBlock *b = heap_find(w, addr); return b ? b->size : 0; }
static u64 heap_realloc(Win32 *w, u64 addr, u64 size, int zero) {
  if (!addr) return w32_heap_alloc(w, size, zero);
  HeapBlock *b = heap_find(w, addr); if (!b) return 0;
  if (b->size >= size) return addr;
  u64 n = w32_heap_alloc(w, size, zero); if (!n) return 0;
  u8 *s = mem_ptr(w->mem, addr, b->size, 0), *d = mem_ptr(w->mem, n, b->size, 1); if (s && d) e_memcpy(d, s, (size_t)b->size);
  b->free = 1; return n;
}

/* ---------- slots ---------- */
int w32_have_shim(const char *dll, const char *name);
static ShimFn find_shim(const char *dll, const char *name);
u64 w32_shim_slot(Win32 *w, const char *dll, const char *name) {
  for (int i = SLOT_FIRST; i < w->nslots; i++) if (!e_strcmp(w->slots[i].name, name) && !e_stricmp(w->slots[i].dll, dll)) return SLOT_ADDR(i);
  if (w->nslots >= MAX_SHIMS) return 0;
  int i = w->nslots++;
  size_t ln = e_strlen(name) + 1, ld = e_strlen(dll) + 1; char *n = emu_alloc(ln + ld); e_memcpy(n, name, ln); e_memcpy(n + ln, dll, ld);
  w->slots[i].name = n; w->slots[i].dll = n + ln; w->slots[i].fn = find_shim(dll, name); w->slots[i].calls = 0;
  return SLOT_ADDR(i);
}

/* ---------- continuations ---------- */
static void guest_call(Cpu *c, u64 fn, u64 a0, u64 a1, u64 a2, u64 a3) {
  c->r[RCX] = a0; c->r[RDX] = a1; c->r[R8] = a2; c->r[R9] = a3;
  c->r[RSP] = (c->r[RSP] & ~15ull) - 32;
  cpu_push(c, SLOT_ADDR(SLOT_CONT));
  c->rip = fn;
}
static int cont_push(Cpu *c, int kind) {
  Win32 *w = W; if (w->ncont >= 64) { cpu_fault(c, "guest callback nesting too deep"); return -1; }
  int i = w->ncont++; w->cont[i].kind = kind; w->cont[i].ret_addr = c->rip; w->cont[i].rsp = c->r[RSP]; w->cont[i].idx = 0;
  w->cont[i].data[0] = w->cont[i].data[1] = w->cont[i].data[2] = w->cont[i].data[3] = 0; return i;
}
static void cont_finish(Cpu *c, u64 rax) {
  Win32 *w = W; if (!w->ncont) { cpu_fault(c, "continuation underflow"); return; }
  int i = --w->ncont; c->r[RSP] = w->cont[i].rsp; c->rip = w->cont[i].ret_addr; c->r[RAX] = rax;
}
static void do_exit(Cpu *c, int code) { W->exited = 1; c->exit_code = code; c->status = RUN_EXIT; }
static void retry_and_yield(Cpu *c, u64 slot) { cpu_push(c, c->rip); c->rip = slot; c->status = RUN_YIELD; }
static u64 g_cur_slot;

void w32_cont_resume(Cpu *c) {
  Win32 *w = W; if (!w->ncont) { cpu_fault(c, "stray continuation"); return; }
  int i = w->ncont - 1; u64 rax = c->r[RAX];
  switch (w->cont[i].kind) {
    case CONT_STARTUP:
      if (w->cont[i].idx < w->n_tls_cb) { u64 fn = w->tls_callbacks[w->cont[i].idx++]; guest_call(c, fn, w->image_base, 1, 0, 0); return; }
      if (w->cont[i].idx == w->n_tls_cb) { w->cont[i].idx++; guest_call(c, w->entry, 0, 0, 0, 0); return; }
      do_exit(c, (int)rax); return;
    case CONT_INITTERM: {
      if (w->cont[i].data[2] && (int)rax != 0) { cont_finish(c, rax); return; }
      while (w->cont[i].data[0] < w->cont[i].data[1]) { u64 fn = rd64(c, w->cont[i].data[0]); w->cont[i].data[0] += 8; if (fn) { guest_call(c, fn, 0, 0, 0, 0); return; } }
      cont_finish(c, 0); return; }
    case CONT_ATEXIT:
      if (w->n_at_exit > 0) { u64 fn = w->at_exit[--w->n_at_exit]; guest_call(c, fn, 0, 0, 0, 0); return; }
      if (w->cont[i].data[0]) { do_exit(c, (int)w->cont[i].data[1]); return; }
      cont_finish(c, 0); return;
    case CONT_RET_RAX: cont_finish(c, rax); return;
    case CONT_RET_VAL: cont_finish(c, w->cont[i].data[0]); return;
    case CONT_CREATEWND: {
      int step = w->cont[i].idx++;
      if (step == 0 && (int)rax == -1) { w->win.created = 0; cont_finish(c, 0); return; }
      if (step == 0) { guest_call(c, w->win.wndproc, w->win.hwnd, 0x0005 /* WM_SIZE */, 0, ((u64)(w->win.h & 0xffff) << 16) | (w->win.w & 0xffff)); return; }
      if (step == 1) { guest_call(c, w->win.wndproc, w->win.hwnd, 0x0003 /* WM_MOVE */, 0, (64 << 16) | 64); return; }
      if (step == 2 && (w->win.style & 0x10000000)) { w->win.shown = 1; guest_call(c, w->win.wndproc, w->win.hwnd, 0x0018 /* WM_SHOWWINDOW */, 1, 0); return; }
      cont_finish(c, w->win.hwnd); return; }
    case CONT_QSORT: {
      /* insertion sort driven by the guest comparator: data0=base data1=n data2=size data3=cmp data4=ii data5=jj */
      u64 base = w->cont[i].data[0], n = w->cont[i].data[1], sz = w->cont[i].data[2], cmp = w->cont[i].data[3];
      u64 ii = w->cont[i].data[4], jj = w->cont[i].data[5];
      if ((int)rax > 0) { u8 *a = mem_ptr(w->mem, base + (jj - 1) * sz, sz, 1), *b = mem_ptr(w->mem, base + jj * sz, sz, 1); if (a && b) for (u64 k = 0; k < sz; k++) { u8 t = a[k]; a[k] = b[k]; b[k] = t; } jj--; }
      else jj = 0;
      if (jj == 0) { ii++; jj = ii; }
      if (ii >= n) { cont_finish(c, 0); return; }
      w->cont[i].data[4] = ii; w->cont[i].data[5] = jj;
      guest_call(c, cmp, base + (jj - 1) * sz, base + jj * sz, 0, 0);
      return; }
  }
  extern void gdi_cont_resume(Cpu *c, int kind, u64 rax);
  gdi_cont_resume(c, w->cont[i].kind, rax);
}
/* exported helpers for gdi.c */
void w32_retry_yield(Cpu *c) { retry_and_yield(c, g_cur_slot); }
void w32_push_cont(Cpu *c, int kind, u64 val) { int i = cont_push(c, kind); if (i >= 0) W->cont[i].data[0] = val; }
void w32_guest_call(Cpu *c, u64 fn, u64 a0, u64 a1, u64 a2, u64 a3) { guest_call(c, fn, a0, a1, a2, a3); }
void w32_cont_finish(Cpu *c, u64 rax) { cont_finish(c, rax); }

int w32_dispatch(Cpu *c, u64 addr) {
  Win32 *w = W; u64 off = addr - G_SHIM_BASE;
  if (off % SHIM_SLOT_SIZE || off / SHIM_SLOT_SIZE >= (u64)w->nslots) { cpu_fault(c, "jump into shim region at 0x%llx", (unsigned long long)addr); return RUN_FAULT; }
  int idx = (int)(off / SHIM_SLOT_SIZE);
  if (idx == SLOT_CONT) { w32_cont_resume(c); return c->status; }
  u64 ret = cpu_pop(c); c->rip = ret; g_cur_slot = addr;
  ShimSlot *s = &w->slots[idx]; s->calls++;
  if (idx == SLOT_STARTUP) { cont_push(c, CONT_STARTUP); w32_cont_resume(c); return c->status; }
  if (!s->fn) {
    w->unknown_import_calls++;
    cpu_fault(c, "unimplemented API %s!%s (called from 0x%llx)", s->dll, s->name, (unsigned long long)ret);
    w32_report(w, "STOP: unimplemented API %s!%s", s->dll, s->name);
    return RUN_FAULT;
  }
  c->r[RAX] = 0;
  s->fn(c);
  return c->status;
}

/* ---------- guest printf ---------- */
typedef struct { Cpu *c; int kind; int idx; u64 va; } ArgSrc;   /* kind 0: register/stack args starting at idx; kind 1: va_list pointer */
static u64 arg_next(ArgSrc *s) { if (s->kind == 1) { u64 v = rd64(s->c, s->va); s->va += 8; return v; } return w32_arg(s->c, s->idx++); }
static int wstr_to_utf8(Cpu *c, u64 a, char *out, size_t n, int maxchars) {
  size_t p = 0; for (int i = 0; maxchars < 0 || i < maxchars; i++) { u16 ch = rd16(c, a + (u64)i * 2); if (!ch || c->status) break;
    if (ch < 0x80) { if (p + 1 < n) out[p++] = (char)ch; } else if (ch < 0x800) { if (p + 2 < n) { out[p++] = (char)(0xC0 | ch >> 6); out[p++] = (char)(0x80 | (ch & 0x3f)); } }
    else { if (p + 3 < n) { out[p++] = (char)(0xE0 | ch >> 12); out[p++] = (char)(0x80 | ((ch >> 6) & 0x3f)); out[p++] = (char)(0x80 | (ch & 0x3f)); } } }
  out[p] = 0; return (int)p;
}
static int g_format(Cpu *c, char *out, size_t n, u64 fmt_addr, int fmt_wide, ArgSrc *src) {
  char fmtbuf[4096]; const char *fmt;
  if (fmt_wide) { wstr_to_utf8(c, fmt_addr, fmtbuf, sizeof fmtbuf, -1); fmt = fmtbuf; } else { fmt = rd_str(c, fmt_addr, 1 << 20); if (!fmt) { cpu_fault(c, "printf: bad format pointer 0x%llx", (unsigned long long)fmt_addr); return 0; } }
  size_t pos = 0;
#define OUTC(ch) do { if (pos + 1 < n) out[pos] = (ch); pos++; } while (0)
  for (const char *f = fmt; *f; f++) {
    if (*f != '%') { OUTC(*f); continue; }
    const char *spec_start = f; f++;
    char spec[32]; int sp = 0; spec[sp++] = '%';
    while (*f == '-' || *f == '+' || *f == ' ' || *f == '#' || *f == '0') { if (sp < 8) spec[sp++] = *f; f++; }
    int width = -1, prec = -1;
    if (*f == '*') { width = (int)arg_next(src); f++; } else if (*f >= '0' && *f <= '9') { width = 0; while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0'); }
    if (*f == '.') { f++; prec = 0; if (*f == '*') { prec = (int)arg_next(src); f++; } else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0'); }
    int size = 4, wide = fmt_wide; /* default int */
    for (;;) {
      if (*f == 'l') { if (f[1] == 'l') { size = 8; f += 2; } else { size = 4; wide = 1; f++; } }
      else if (*f == 'h') { if (f[1] == 'h') { size = 1; f += 2; } else { size = 2; wide = 0; f++; } }
      else if (*f == 'I') { if (f[1] == '6' && f[2] == '4') { size = 8; f += 3; } else if (f[1] == '3' && f[2] == '2') { size = 4; f += 3; } else { size = 8; f++; } }
      else if (*f == 'z' || *f == 'j' || *f == 't' || *f == 'q' || *f == 'L') { size = 8; f++; }
      else if (*f == 'w') { wide = 1; f++; }
      else break;
    }
    char conv = *f; if (!conv) { f--; break; }
    if (width >= 0) { spec[sp++] = '*'; } if (prec >= 0) { spec[sp++] = '.'; spec[sp++] = '*'; }
    char tmp[1024]; int tl = 0;
    switch (conv) {
      case '%': OUTC('%'); break;
      case 'c': case 'C': { u64 v = arg_next(src); spec[sp++] = 'c'; spec[sp] = 0; if (width >= 0 && prec >= 0) tl = e_snprintf(tmp, sizeof tmp, spec, width, prec, (int)(v & 0xff)); else if (width >= 0) tl = e_snprintf(tmp, sizeof tmp, spec, width, (int)(v & 0xff)); else if (prec >= 0) tl = e_snprintf(tmp, sizeof tmp, spec, prec, (int)(v & 0xff)); else tl = e_snprintf(tmp, sizeof tmp, spec, (int)(v & 0xff)); break; }
      case 's': case 'S': { u64 v = arg_next(src); const char *s; char wbuf[2048]; int is_wide = conv == 'S' ? !fmt_wide : wide;
        if (!v) s = "(null)"; else if (is_wide) { wstr_to_utf8(c, v, wbuf, sizeof wbuf, prec >= 0 ? prec : -1); s = wbuf; } else { s = rd_str(c, v, 1 << 20); if (!s) { s = "(bad ptr)"; } }
        spec[sp++] = 's'; spec[sp] = 0;
        if (width >= 0 && prec >= 0) tl = e_snprintf(tmp, sizeof tmp, spec, width, prec, s); else if (width >= 0) tl = e_snprintf(tmp, sizeof tmp, spec, width, s); else if (prec >= 0) tl = e_snprintf(tmp, sizeof tmp, spec, prec, s); else tl = e_snprintf(tmp, sizeof tmp, spec, s);
        if (tl > (int)sizeof tmp - 1) tl = sizeof tmp - 1; break; }
      case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': {
        u64 v = arg_next(src); if (conv == 'p') { size = 8; conv = 'x'; if (prec < 0) { prec = 16; spec[sp++] = '.'; spec[sp++] = '*'; } }
        if (size == 1) v = (conv == 'd' || conv == 'i') ? (u64)(s64)(s8)v : (v & 0xff); else if (size == 2) v = (conv == 'd' || conv == 'i') ? (u64)(s64)(s16)v : (v & 0xffff); else if (size == 4) v = (conv == 'd' || conv == 'i') ? (u64)(s64)(s32)v : (v & 0xffffffff);
        spec[sp++] = 'l'; spec[sp++] = 'l'; spec[sp++] = conv; spec[sp] = 0;
        if (width >= 0 && prec >= 0) tl = e_snprintf(tmp, sizeof tmp, spec, width, prec, v); else if (width >= 0) tl = e_snprintf(tmp, sizeof tmp, spec, width, v); else if (prec >= 0) tl = e_snprintf(tmp, sizeof tmp, spec, prec, v); else tl = e_snprintf(tmp, sizeof tmp, spec, v); break; }
      case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
        u64 bits = arg_next(src); double v; e_memcpy(&v, &bits, 8); if (conv == 'a' || conv == 'A') conv = 'e';
        spec[sp++] = conv; spec[sp] = 0;
        if (width >= 0 && prec >= 0) tl = e_snprintf(tmp, sizeof tmp, spec, width, prec, v); else if (width >= 0) tl = e_snprintf(tmp, sizeof tmp, spec, width, v); else if (prec >= 0) tl = e_snprintf(tmp, sizeof tmp, spec, prec, v); else tl = e_snprintf(tmp, sizeof tmp, spec, v); break; }
      case 'n': { u64 p = arg_next(src); if (p) wr32(c, p, (u32)pos); break; }
      default: { for (const char *q = spec_start; q <= f; q++) OUTC(*q); break; }
    }
    if (tl > (int)sizeof tmp - 1) tl = sizeof tmp - 1;
    for (int k = 0; k < tl; k++) OUTC(tmp[k]);
  }
  if (n) out[pos < n ? pos : n - 1] = 0;
  return (int)pos;
#undef OUTC
}
static char g_fmt_out[1 << 16];
int w32_format_args(Cpu *c, char *out, size_t n, u64 fmt, int wide, int first_arg) { ArgSrc src = { c, 0, first_arg, 0 }; return g_format(c, out, n, fmt, wide, &src); }
static void con_out(Win32 *w, int fd, const char *s, size_t n) {
  host_write(fd, s, n);
  size_t room = sizeof w->con - 1 - w->con_len; if (n > room) { /* keep tail */ size_t drop = n - room; if (drop > (size_t)w->con_len) { w->con_len = 0; } else { e_memmove(w->con, w->con + drop, w->con_len - drop); w->con_len -= (int)drop; } room = sizeof w->con - 1 - w->con_len; }
  if (n > room) { s += n - room; n = room; }
  e_memcpy(w->con + w->con_len, s, n); w->con_len += (int)n; w->con[w->con_len] = 0;
}
static int fd_of_file(Win32 *w, u64 file) {   /* guest FILE* → 1/2, or 0 */
  u64 iob = w->ubuf; if (file == iob + 0x60) return 1; if (file == iob + 0xC0) return 2; if (file == iob) return -1; return 0;
}
static int fd_of_handle(u64 h) { if (h == 0x11 || h == 0x12) return (int)(h - 0x10); if (h == 0x10) return -1; return 0; }

/* ---------- misc helpers ---------- */
static u64 now_100ns(void) { return (u64)(host_now_ms() * 10000.0) + 116444736000000000ull + 0x1d0000000000000ull / 0x1d; }
static u64 unix_time(void) { return 1793000000ull + (u64)(host_now_ms() / 1000.0); }   /* ~2026-10-26 + run time (host clock has no calendar in freestanding wasm) */

/* ================= kernel32 ================= */
static void s_GetLastError(Cpu *c) { RET(W->last_error); }
static void s_SetLastError(Cpu *c) { W->last_error = (u32)ARG(0); }
static void s_GetCurrentProcess(Cpu *c) { RET(~0ull); }
static void s_GetCurrentThread(Cpu *c) { RET(~1ull); }
static void s_GetCurrentProcessId(Cpu *c) { RET(4242); }
static void s_GetCurrentThreadId(Cpu *c) { RET(4243); }
static void s_ExitProcess(Cpu *c) { do_exit(c, (int)ARG(0)); }
static void s_TerminateProcess(Cpu *c) { do_exit(c, (int)ARG(1)); }
static void s_GetTickCount(Cpu *c) { RET((u32)(host_now_ms() - W->start_ms + 100000.0)); }
static void s_GetTickCount64(Cpu *c) { RET((u64)(host_now_ms() - W->start_ms + 100000.0)); }
static void s_QueryPerformanceCounter(Cpu *c) { wr64(c, ARG(0), (u64)(host_now_ms() * 10000.0)); RET(1); }
static void s_QueryPerformanceFrequency(Cpu *c) { wr64(c, ARG(0), 10000000); RET(1); }
static void s_GetSystemTimeAsFileTime(Cpu *c) { wr64(c, ARG(0), now_100ns()); }
static void s_GetSystemTimePreciseAsFileTime(Cpu *c) { s_GetSystemTimeAsFileTime(c); }
static void fill_systemtime(Cpu *c, u64 a) { u64 t = unix_time(); u64 days = t / 86400, rem = t % 86400;
  /* civil from days (Howard Hinnant) */ s64 z = (s64)days + 719468; s64 era = z / 146097; s64 doe = z - era * 146097; s64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; s64 y = yoe + era * 400; s64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100); s64 mp = (5 * doy + 2) / 153; s64 d = doy - (153 * mp + 2) / 5 + 1; s64 m = mp < 10 ? mp + 3 : mp - 9; if (m <= 2) y++;
  wr16(c, a, (u16)y); wr16(c, a + 2, (u16)m); wr16(c, a + 4, (u16)((days + 4) % 7)); wr16(c, a + 6, (u16)d); wr16(c, a + 8, (u16)(rem / 3600)); wr16(c, a + 10, (u16)(rem / 60 % 60)); wr16(c, a + 12, (u16)(rem % 60)); wr16(c, a + 14, 0); }
static void s_GetSystemTime(Cpu *c) { fill_systemtime(c, ARG(0)); }
static void s_GetLocalTime(Cpu *c) { fill_systemtime(c, ARG(0)); }
static void s_Sleep(Cpu *c) {
  Win32 *w = W; double now = host_now_ms();
  if (!w->sleeping) { w->sleeping = 1; w->sleep_until_ms = now + (double)(u32)ARG(0); retry_and_yield(c, g_cur_slot); return; }
  if (now >= w->sleep_until_ms) { w->sleeping = 0; return; }
  retry_and_yield(c, g_cur_slot);
}
double w32_sleep_remaining_ms(Win32 *w) { if (!w->sleeping) return 0; double r = w->sleep_until_ms - host_now_ms(); return r > 0 ? r : 0; }
static void s_SleepEx(Cpu *c) { s_Sleep(c); }
static void s_GetStdHandle(Cpu *c) { u32 n = (u32)ARG(0); RET(n == 0xfffffff6u ? 0x10 : n == 0xfffffff5u ? 0x11 : n == 0xfffffff4u ? 0x12 : ~0ull); }
static void s_WriteFile(Cpu *c) { int fd = fd_of_handle(ARG(0)); u32 n = (u32)ARG(2); if (fd <= 0) { W->last_error = 6; RET(0); return; } const u8 *p = mem_ptr(c->mem, ARG(1), n, 0); if (!p) { RET(0); return; } con_out(W, fd, (const char *)p, n); if (ARG(3)) wr32(c, ARG(3), n); RET(1); }
static void s_WriteConsoleA(Cpu *c) { s_WriteFile(c); }
static void s_WriteConsoleW(Cpu *c) { int fd = fd_of_handle(ARG(0)); u32 n = (u32)ARG(2); if (fd <= 0) { RET(0); return; } char buf[4096]; int m = wstr_to_utf8(c, ARG(1), buf, sizeof buf, (int)n); con_out(W, fd, buf, m); if (ARG(3)) wr32(c, ARG(3), n); RET(1); }
static void s_ReadFile(Cpu *c) { if (ARG(3)) wr32(c, ARG(3), 0); RET(fd_of_handle(ARG(0)) == -1 ? 1 : 0); }
static void s_ReadConsoleA(Cpu *c) { if (ARG(3)) wr32(c, ARG(3), 0); RET(1); }
static void s_GetConsoleMode(Cpu *c) { if (fd_of_handle(ARG(0))) { wr32(c, ARG(1), 3); RET(1); } else RET(0); }
static void s_SetConsoleMode(Cpu *c) { RET(1); }
static void s_GetConsoleOutputCP(Cpu *c) { RET(65001); }
static void s_GetConsoleCP(Cpu *c) { RET(65001); }
static void s_SetConsoleOutputCP(Cpu *c) { RET(1); }
static void s_SetConsoleCP(Cpu *c) { RET(1); }
static void s_GetConsoleScreenBufferInfo(Cpu *c) { u64 a = ARG(1); wr16(c, a, 120); wr16(c, a + 2, 40); wr16(c, a + 4, 0); wr16(c, a + 6, 0); wr16(c, a + 8, 7); wr16(c, a + 10, 0); wr16(c, a + 12, 0); wr16(c, a + 14, 119); wr16(c, a + 16, 39); wr16(c, a + 18, 120); wr16(c, a + 20, 40); RET(1); }
static void s_SetConsoleTextAttribute(Cpu *c) { RET(1); }
static void s_SetConsoleTitleA(Cpu *c) { RET(1); }
static void s_GetConsoleWindow(Cpu *c) { RET(0); }
static void s_AllocConsole(Cpu *c) { RET(1); }
static void s_SetConsoleCtrlHandler(Cpu *c) { RET(1); }
static void s_FlushFileBuffers(Cpu *c) { RET(1); }
static void s_GetFileType(Cpu *c) { RET(fd_of_handle(ARG(0)) ? 2 : 0); }
static void s_CloseHandle(Cpu *c) { RET(1); }
static void s_CreateFileA(Cpu *c) { const char *n = rd_str(c, ARG(0), 1024); w32_report(W, "CreateFileA(\"%s\") → no file system in the sandbox", n ? n : "?"); W->last_error = 2; RET(~0ull); }
static void s_CreateFileW(Cpu *c) { char n[512]; wstr_to_utf8(c, ARG(0), n, sizeof n, -1); w32_report(W, "CreateFileW(\"%s\") → no file system in the sandbox", n); W->last_error = 2; RET(~0ull); }
static void s_GetFileAttributesA(Cpu *c) { W->last_error = 2; RET(0xffffffffu); }
static void s_GetFileAttributesW(Cpu *c) { W->last_error = 2; RET(0xffffffffu); }
static void s_GetFileAttributesExW(Cpu *c) { W->last_error = 2; RET(0); }
static void s_FindFirstFileA(Cpu *c) { W->last_error = 2; RET(~0ull); }
static void s_FindFirstFileW(Cpu *c) { W->last_error = 2; RET(~0ull); }
static void s_FindClose(Cpu *c) { RET(1); }
static void s_DeleteFileA(Cpu *c) { RET(0); }
static void s_GetFullPathNameA(Cpu *c) { const char *n = rd_str(c, ARG(0), 1024); if (!n) { RET(0); return; } char out[600]; e_snprintf(out, sizeof out, "C:\\%s", n); size_t l = e_strlen(out); if ((u32)ARG(1) > l) { u8 *p = mem_ptr(c->mem, ARG(2), l + 1, 1); if (p) e_memcpy(p, out, l + 1); } RET(l); }
static void s_GetCurrentDirectoryA(Cpu *c) { const char *d = "C:\\nebula"; size_t l = e_strlen(d); if ((u32)ARG(0) > l) { u8 *p = mem_ptr(c->mem, ARG(1), l + 1, 1); if (p) e_memcpy(p, d, l + 1); } RET(l); }
static void s_GetCurrentDirectoryW(Cpu *c) { const char *d = "C:\\nebula"; size_t l = e_strlen(d); if ((u32)ARG(0) > l) for (size_t i = 0; i <= l; i++) wr16(c, ARG(1) + i * 2, (u8)d[i]); RET(l); }
static void s_GetTempPathA(Cpu *c) { const char *d = "C:\\tmp\\"; size_t l = e_strlen(d); if ((u32)ARG(0) > l) { u8 *p = mem_ptr(c->mem, ARG(1), l + 1, 1); if (p) e_memcpy(p, d, l + 1); } RET(l); }
static void s_SetFilePointer(Cpu *c) { RET(0xffffffffu); }
static void s_GetFileSize(Cpu *c) { RET(0xffffffffu); }

static void s_GetCommandLineA(Cpu *c) { RET(W->cmdline_a); }
static void s_GetCommandLineW(Cpu *c) { RET(W->cmdline_w); }
static void s_GetEnvironmentStrings(Cpu *c) { RET(W->env_a); }
static void s_GetEnvironmentStringsW(Cpu *c) { RET(W->env_w); }
static void s_FreeEnvironmentStringsW(Cpu *c) { RET(1); }
static void s_FreeEnvironmentStringsA(Cpu *c) { RET(1); }
static void s_GetEnvironmentVariableA(Cpu *c) { W->last_error = 203; RET(0); }
static void s_GetEnvironmentVariableW(Cpu *c) { W->last_error = 203; RET(0); }
static void s_SetEnvironmentVariableA(Cpu *c) { RET(1); }
static void s_ExpandEnvironmentStringsA(Cpu *c) { RET(0); }
static void s_GetStartupInfoA(Cpu *c) { u64 a = ARG(0); u8 *p = mem_ptr(c->mem, a, 104, 1); if (p) { e_memset(p, 0, 104); wr32(c, a, 104); wr64(c, a + 80, 0x10); wr64(c, a + 88, 0x11); wr64(c, a + 96, 0x12); } }
static void s_GetStartupInfoW(Cpu *c) { s_GetStartupInfoA(c); }
static void s_GetModuleHandleA(Cpu *c) { const char *n = rd_str(c, ARG(0), 260); if (!n) { RET(W->image_base); return; } RET(0x7ff00000000ull + ((u64)(u8)n[0] << 12)); }
static void s_GetModuleHandleW(Cpu *c) { if (!ARG(0)) { RET(W->image_base); return; } RET(0x7ff00000000ull + ((u64)(rd16(c, ARG(0)) & 0xff) << 12)); }
static void s_GetModuleHandleExW(Cpu *c) { if (ARG(2)) wr64(c, ARG(2), W->image_base); RET(1); }
static void s_GetModuleHandleExA(Cpu *c) { s_GetModuleHandleExW(c); }
static void s_LoadLibraryA(Cpu *c) { const char *n = rd_str(c, ARG(0), 260); RET(0x7ff00000000ull + ((u64)(n ? (u8)n[0] : 1) << 12)); }
static void s_LoadLibraryW(Cpu *c) { RET(0x7ff00000000ull + ((u64)(rd16(c, ARG(0)) & 0xff) << 12)); }
static void s_LoadLibraryExA(Cpu *c) { s_LoadLibraryA(c); }
static void s_LoadLibraryExW(Cpu *c) { s_LoadLibraryW(c); }
static void s_FreeLibrary(Cpu *c) { RET(1); }
static void s_GetProcAddress(Cpu *c) { const char *n = rd_str(c, ARG(1), 256); if (!n || ARG(1) < 0x10000) { RET(0); return; } if (!w32_have_shim("", n)) { W->last_error = 127; RET(0); return; } RET(w32_shim_slot(W, "dynamic", n)); }
static void s_GetModuleFileNameA(Cpu *c) { char p[300]; e_snprintf(p, sizeof p, "C:\\nebula\\%s", W->exe_name); size_t l = e_strlen(p); u32 n = (u32)ARG(2); if (l + 1 > n) l = n ? n - 1 : 0; u8 *d = mem_ptr(c->mem, ARG(1), l + 1, 1); if (d) { e_memcpy(d, p, l); d[l] = 0; } RET(l); }
static void s_GetModuleFileNameW(Cpu *c) { char p[300]; e_snprintf(p, sizeof p, "C:\\nebula\\%s", W->exe_name); size_t l = e_strlen(p); u32 n = (u32)ARG(2); if (l + 1 > n) l = n ? n - 1 : 0; for (size_t i = 0; i < l; i++) wr16(c, ARG(1) + i * 2, (u8)p[i]); wr16(c, ARG(1) + l * 2, 0); RET(l); }
static void s_GetSystemDirectoryA(Cpu *c) { const char *d = "C:\\Windows\\System32"; size_t l = e_strlen(d); if ((u32)ARG(1) > l) { u8 *p = mem_ptr(c->mem, ARG(0), l + 1, 1); if (p) e_memcpy(p, d, l + 1); } RET(l); }
static void s_GetWindowsDirectoryA(Cpu *c) { const char *d = "C:\\Windows"; size_t l = e_strlen(d); if ((u32)ARG(1) > l) { u8 *p = mem_ptr(c->mem, ARG(0), l + 1, 1); if (p) e_memcpy(p, d, l + 1); } RET(l); }
static void s_GetVersion(Cpu *c) { RET(0x23f00206); }
static void s_GetVersionExA(Cpu *c) { u64 a = ARG(0); u32 sz = rd32(c, a); wr32(c, a + 4, 10); wr32(c, a + 8, 0); wr32(c, a + 12, 19045); wr32(c, a + 16, 2); if (sz >= 20) { u8 *p = mem_ptr(c->mem, a + 20, 128, 1); if (p) e_memset(p, 0, 128); } RET(1); }
static void s_GetVersionExW(Cpu *c) { s_GetVersionExA(c); }
static void s_IsProcessorFeaturePresent(Cpu *c) { u32 f = (u32)ARG(0); RET(f == 6 || f == 10 || f == 0 || f == 3 || f == 1 || f == 2 || f == 4 || f == 5 ? 1 : 0); /* sse, sse2, cmpxchg, rdtsc... ; no sse3/avx/xsave(17) */ }
static void s_IsDebuggerPresent(Cpu *c) { RET(0); }
static void s_IsDBCSLeadByte(Cpu *c) { RET(0); }
static void s_IsDBCSLeadByteEx(Cpu *c) { RET(0); }
static void s_GetACP(Cpu *c) { RET(65001); }
static void s_GetOEMCP(Cpu *c) { RET(65001); }
static void s_GetCPInfo(Cpu *c) { u64 a = ARG(1); wr32(c, a, 1); wr8(c, a + 4, '?'); wr8(c, a + 5, 0); for (int i = 0; i < 12; i++) wr8(c, a + 6 + i, 0); RET(1); }
static void s_GetSystemInfo(Cpu *c) { u64 a = ARG(0); u8 *p = mem_ptr(c->mem, a, 48, 1); if (!p) return; e_memset(p, 0, 48); wr16(c, a, 9); wr32(c, a + 4, 4096); wr64(c, a + 8, 0x10000); wr64(c, a + 16, 0x7ffffffeffff); wr64(c, a + 24, 1); wr32(c, a + 32, 1); wr32(c, a + 36, 586); wr32(c, a + 40, 0x10000); }
static void s_GetNativeSystemInfo(Cpu *c) { s_GetSystemInfo(c); }
static void s_GlobalMemoryStatusEx(Cpu *c) { u64 a = ARG(0); wr32(c, a + 4, 30); wr64(c, a + 8, 4ull << 30); wr64(c, a + 16, 3ull << 30); wr64(c, a + 24, 4ull << 30); wr64(c, a + 32, 3ull << 30); wr64(c, a + 40, 8ull << 40); wr64(c, a + 48, 8ull << 40); RET(1); }
static void s_SetUnhandledExceptionFilter(Cpu *c) { RET(0); }
static void s_UnhandledExceptionFilter(Cpu *c) { RET(0); }
static void s_AddVectoredExceptionHandler(Cpu *c) { RET(0x1234); }
static void s_RemoveVectoredExceptionHandler(Cpu *c) { RET(1); }
static void s_RtlAddFunctionTable(Cpu *c) { RET(1); }
static void s_RtlDeleteFunctionTable(Cpu *c) { RET(1); }
static void s_RtlLookupFunctionEntry(Cpu *c) { RET(0); }
static void s_RtlCaptureContext(Cpu *c) { u64 a = ARG(0); u8 *p = mem_ptr(c->mem, a, 1232, 1); if (p) e_memset(p, 0, 1232); wr64(c, a + 0x98, c->r[RSP]); wr64(c, a + 0xf8, c->rip); }
static void s_RtlVirtualUnwind(Cpu *c) { RET(0); }
static void s_RtlPcToFileHeader(Cpu *c) { wr64(c, ARG(1), W->image_base); RET(W->image_base); }
static void s_RtlUnwindEx(Cpu *c) { cpu_fault(c, "SEH unwind (RtlUnwindEx) is not supported"); }
static void s_RaiseException(Cpu *c) { cpu_fault(c, "RaiseException(0x%08x) — exceptions are not supported", (u32)ARG(0)); }
static void s_EncodePointer(Cpu *c) { RET(ARG(0) ^ 0xA5A5A5A5ull); }
static void s_DecodePointer(Cpu *c) { RET(ARG(0) ^ 0xA5A5A5A5ull); }
static void s_OutputDebugStringA(Cpu *c) { const char *s = rd_str(c, ARG(0), 4096); if (s) e_log("[dbg] %s", s); }
static void s_OutputDebugStringW(Cpu *c) { char b[1024]; wstr_to_utf8(c, ARG(0), b, sizeof b, -1); e_log("[dbg] %s", b); }
static void s_FormatMessageA(Cpu *c) { RET(0); }
static void s_FormatMessageW(Cpu *c) { RET(0); }
static void s_SetErrorMode(Cpu *c) { RET(0); }
static void s_GetErrorMode(Cpu *c) { RET(0); }
static void s_SetThreadErrorMode(Cpu *c) { RET(1); }
static void s_GetLogicalProcessorInformation(Cpu *c) { W->last_error = 122; RET(0); }
static void s_GetProcessAffinityMask(Cpu *c) { wr64(c, ARG(1), 1); wr64(c, ARG(2), 1); RET(1); }
static void s_SetThreadAffinityMask(Cpu *c) { RET(1); }
static void s_GetThreadPriority(Cpu *c) { RET(0); }
static void s_SetThreadPriority(Cpu *c) { RET(1); }
static void s_SetPriorityClass(Cpu *c) { RET(1); }
static void s_GetPriorityClass(Cpu *c) { RET(0x20); }
static void s_GetProcessTimes(Cpu *c) { u64 t = now_100ns(); wr64(c, ARG(1), t); wr64(c, ARG(2), 0); wr64(c, ARG(3), 0); wr64(c, ARG(4), (u64)((host_now_ms() - W->start_ms) * 10000)); RET(1); }
static void s_GetThreadTimes(Cpu *c) { s_GetProcessTimes(c); }
static void s_GetProcessHeap(Cpu *c) { RET(0x9000); }
static void s_HeapCreate(Cpu *c) { RET(0x9000 + ++W->next_handle); }
static void s_HeapDestroy(Cpu *c) { RET(1); }
static void s_HeapAlloc(Cpu *c) { RET(w32_heap_alloc(W, ARG(2), !!(ARG(1) & 8))); }
static void s_HeapFree(Cpu *c) { if (ARG(2)) w32_heap_free(W, ARG(2)); RET(1); }
static void s_HeapReAlloc(Cpu *c) { RET(heap_realloc(W, ARG(2), ARG(3), !!(ARG(1) & 8))); }
static void s_HeapSize(Cpu *c) { RET(w32_heap_size(W, ARG(2))); }
static void s_HeapValidate(Cpu *c) { RET(1); }
static void s_HeapSetInformation(Cpu *c) { RET(1); }
static void s_HeapQueryInformation(Cpu *c) { RET(0); }
static void s_HeapCompact(Cpu *c) { RET(0); }
static void s_HeapLock(Cpu *c) { RET(1); }
static void s_HeapUnlock(Cpu *c) { RET(1); }
static void s_HeapWalk(Cpu *c) { RET(0); }
static void s_GlobalAlloc(Cpu *c) { RET(w32_heap_alloc(W, ARG(1), !!(ARG(0) & 0x40))); }
static void s_GlobalFree(Cpu *c) { w32_heap_free(W, ARG(0)); RET(0); }
static void s_GlobalLock(Cpu *c) { RET(ARG(0)); }
static void s_GlobalUnlock(Cpu *c) { RET(1); }
static void s_LocalAlloc(Cpu *c) { RET(w32_heap_alloc(W, ARG(1), !!(ARG(0) & 0x40))); }
static void s_LocalFree(Cpu *c) { w32_heap_free(W, ARG(0)); RET(0); }
static void s_VirtualAlloc(Cpu *c) { u64 addr = ARG(0), size = ARG(1);
  if (addr && mem_ptr(c->mem, addr, 1, 0)) { RET(addr); return; }
  size = (size + 0xffff) & ~0xffffull; u64 a = w32_heap_alloc(W, size + 0x10000, 1); if (!a) { W->last_error = 8; RET(0); return; } RET((a + 0xffff) & ~0xffffull); }
static void s_VirtualAllocEx(Cpu *c) { c->r[RCX] = ARG(1); c->r[RDX] = ARG(2); s_VirtualAlloc(c); }
static void s_VirtualFree(Cpu *c) { RET(1); }
static void s_VirtualProtect(Cpu *c) { if (ARG(3)) wr32(c, ARG(3), 0x40); RET(1); }
static void s_VirtualLock(Cpu *c) { RET(1); }
static void s_VirtualUnlock(Cpu *c) { RET(1); }
static void s_VirtualQuery(Cpu *c) { u64 addr = ARG(0), mbi = ARG(1); Region *r = mem_region(c->mem, addr);
  u8 *p = mem_ptr(c->mem, mbi, 48, 1); if (!p) { RET(0); return; } e_memset(p, 0, 48);
  if (!r) { wr64(c, mbi, addr & ~0xfffull); wr64(c, mbi + 24, 0x1000); wr32(c, mbi + 32, 0x10000); wr32(c, mbi + 36, 1); RET(48); return; }
  wr64(c, mbi, addr & ~0xfffull); wr64(c, mbi + 8, r->base); wr32(c, mbi + 16, 0x40); wr64(c, mbi + 24, r->base + r->size - (addr & ~0xfffull)); wr32(c, mbi + 32, 0x1000); wr32(c, mbi + 36, 0x40); wr32(c, mbi + 40, r->base == W->image_base ? 0x1000000 : 0x20000); RET(48); }
static void s_FlushInstructionCache(Cpu *c) { RET(1); }
static void s_TlsAlloc(Cpu *c) { Win32 *w = W; for (u32 i = 1; i < 64; i++) if (!w->tls_slots[i]) { w->tls_slots[i] = 1; w->tls_values[i] = 0; RET(i); return; } RET(0xffffffffu); }
static void s_TlsFree(Cpu *c) { u32 i = (u32)ARG(0); if (i < 64) W->tls_slots[i] = 0; RET(1); }
static void s_TlsGetValue(Cpu *c) { u32 i = (u32)ARG(0); W->last_error = 0; RET(i < 64 ? rd64(c, W->teb + 0x1480 + i * 8) : 0); }
static void s_TlsSetValue(Cpu *c) { u32 i = (u32)ARG(0); if (i < 64) { W->tls_values[i] = ARG(1); wr64(c, W->teb + 0x1480 + i * 8, ARG(1)); } RET(1); }
static void s_FlsAlloc(Cpu *c) { s_TlsAlloc(c); }
static void s_FlsFree(Cpu *c) { s_TlsFree(c); }
static void s_FlsGetValue(Cpu *c) { s_TlsGetValue(c); }
static void s_FlsSetValue(Cpu *c) { s_TlsSetValue(c); }
static void s_InitializeCriticalSection(Cpu *c) { u8 *p = mem_ptr(c->mem, ARG(0), 40, 1); if (p) { e_memset(p, 0, 40); wr32(c, ARG(0) + 4, (u32)-1); } }
static void s_InitializeCriticalSectionAndSpinCount(Cpu *c) { s_InitializeCriticalSection(c); RET(1); }
static void s_InitializeCriticalSectionEx(Cpu *c) { s_InitializeCriticalSection(c); RET(1); }
static void s_DeleteCriticalSection(Cpu *c) { }
static void s_EnterCriticalSection(Cpu *c) { u64 a = ARG(0); wr32(c, a + 4, rd32(c, a + 4) + 1); wr64(c, a + 16, 4243); }
static void s_LeaveCriticalSection(Cpu *c) { u64 a = ARG(0); wr32(c, a + 4, rd32(c, a + 4) - 1); }
static void s_TryEnterCriticalSection(Cpu *c) { s_EnterCriticalSection(c); RET(1); }
static void s_InitializeSRWLock(Cpu *c) { wr64(c, ARG(0), 0); }
static void s_AcquireSRWLockExclusive(Cpu *c) { }
static void s_ReleaseSRWLockExclusive(Cpu *c) { }
static void s_AcquireSRWLockShared(Cpu *c) { }
static void s_ReleaseSRWLockShared(Cpu *c) { }
static void s_TryAcquireSRWLockExclusive(Cpu *c) { RET(1); }
static void s_InitializeConditionVariable(Cpu *c) { wr64(c, ARG(0), 0); }
static void s_WakeAllConditionVariable(Cpu *c) { }
static void s_WakeConditionVariable(Cpu *c) { }
static void s_SleepConditionVariableSRW(Cpu *c) { RET(1); }
static void s_SleepConditionVariableCS(Cpu *c) { RET(1); }
static void s_InitOnceExecuteOnce(Cpu *c) { u64 once = ARG(0); if (rd64(c, once) == 0) { wr64(c, once, 1); cont_push(c, CONT_RET_VAL); W->cont[W->ncont - 1].data[0] = 1; guest_call(c, ARG(1), once, ARG(2), ARG(3), 0); return; } RET(1); }
static void s_InitOnceBeginInitialize(Cpu *c) { u64 once = ARG(0); if (rd64(c, once) == 0) { wr32(c, ARG(2), 1); } else wr32(c, ARG(2), 0); RET(1); }
static void s_InitOnceComplete(Cpu *c) { wr64(c, ARG(0), 1); RET(1); }
static void s_CreateEventA(Cpu *c) { RET(0xE000 + ++W->next_handle); }
static void s_CreateEventW(Cpu *c) { s_CreateEventA(c); }
static void s_CreateEventExW(Cpu *c) { s_CreateEventA(c); }
static void s_SetEvent(Cpu *c) { RET(1); }
static void s_ResetEvent(Cpu *c) { RET(1); }
static void s_CreateMutexA(Cpu *c) { s_CreateEventA(c); }
static void s_CreateMutexW(Cpu *c) { s_CreateEventA(c); }
static void s_ReleaseMutex(Cpu *c) { RET(1); }
static void s_CreateSemaphoreA(Cpu *c) { s_CreateEventA(c); }
static void s_CreateSemaphoreW(Cpu *c) { s_CreateEventA(c); }
static void s_ReleaseSemaphore(Cpu *c) { RET(1); }
static void s_WaitForSingleObject(Cpu *c) { RET(0); }
static void s_WaitForSingleObjectEx(Cpu *c) { RET(0); }
static void s_WaitForMultipleObjects(Cpu *c) { RET(0); }
static void s_SignalObjectAndWait(Cpu *c) { RET(0); }
static void s_CreateThread(Cpu *c) { w32_report(W, "CreateThread: threads are not supported (single-threaded emulator)"); W->last_error = 8; RET(0); }
static void s_CreateTimerQueue(Cpu *c) { RET(0); }
static void s_GetThreadLocale(Cpu *c) { RET(0x409); }
static void s_GetUserDefaultLCID(Cpu *c) { RET(0x409); }
static void s_GetUserDefaultLangID(Cpu *c) { RET(0x409); }
static void s_GetSystemDefaultLangID(Cpu *c) { RET(0x409); }
static void s_GetUserDefaultUILanguage(Cpu *c) { RET(0x409); }
static void s_GetLocaleInfoA(Cpu *c) { RET(0); }
static void s_GetLocaleInfoW(Cpu *c) { RET(0); }
static void s_GetLocaleInfoEx(Cpu *c) { RET(0); }
static void s_IsValidLocale(Cpu *c) { RET(1); }
static void s_IsValidCodePage(Cpu *c) { RET(1); }
static void s_EnumSystemLocalesW(Cpu *c) { RET(1); }
static void s_GetUserDefaultLocaleName(Cpu *c) { const char *n = "en-US"; for (int i = 0; i <= 5; i++) wr16(c, ARG(0) + i * 2, (u8)n[i]); RET(6); }
static void s_LCMapStringW(Cpu *c) { u32 flags = (u32)ARG(1); u64 src = ARG(2); int n = (int)(s32)ARG(3); u64 dst = ARG(4); int dn = (int)ARG(5);
  int len = n; if (n < 0) { len = 0; while (rd16(c, src + len * 2)) len++; len++; }
  if (dn == 0) { RET(len); return; } if (dn < len) { RET(0); return; }
  for (int i = 0; i < len; i++) { u16 ch = rd16(c, src + i * 2); if ((flags & 0x100) && ch >= 'a' && ch <= 'z') ch -= 32; if ((flags & 0x200) && ch >= 'A' && ch <= 'Z') ch += 32; wr16(c, dst + i * 2, ch); } RET(len); }
static void s_LCMapStringEx(Cpu *c) { s_LCMapStringW(c); }
static void s_LCMapStringA(Cpu *c) { u32 flags = (u32)ARG(1); u64 src = ARG(2); int n = (int)(s32)ARG(3); u64 dst = ARG(4); int dn = (int)ARG(5);
  int len = n; if (n < 0) { len = 0; while (rd8(c, src + len)) len++; len++; }
  if (dn == 0) { RET(len); return; } if (dn < len) { RET(0); return; }
  for (int i = 0; i < len; i++) { u8 ch = rd8(c, src + i); if ((flags & 0x100) && ch >= 'a' && ch <= 'z') ch -= 32; if ((flags & 0x200) && ch >= 'A' && ch <= 'Z') ch += 32; wr8(c, dst + i, ch); } RET(len); }
static void s_CompareStringW(Cpu *c) { u64 a = ARG(2); int an = (int)(s32)ARG(3); u64 b = ARG(4); int bn = (int)(s32)ARG(5); int i = 0;
  for (;; i++) { u16 x = (an < 0 || i < an) ? rd16(c, a + i * 2) : 0, y = (bn < 0 || i < bn) ? rd16(c, b + i * 2) : 0; if (ARG(1) & 1) { if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; } if (x != y) { RET(x < y ? 1 : 3); return; } if (!x) break; } RET(2); }
static void s_CompareStringA(Cpu *c) { u64 a = ARG(2); int an = (int)(s32)ARG(3); u64 b = ARG(4); int bn = (int)(s32)ARG(5); int i = 0;
  for (;; i++) { u8 x = (an < 0 || i < an) ? rd8(c, a + i) : 0, y = (bn < 0 || i < bn) ? rd8(c, b + i) : 0; if (ARG(1) & 1) { if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; } if (x != y) { RET(x < y ? 1 : 3); return; } if (!x) break; } RET(2); }
static void s_CompareStringEx(Cpu *c) { s_CompareStringW(c); }
static void s_GetStringTypeW(Cpu *c) { u32 type = (u32)ARG(0); u64 src = ARG(1); int n = (int)(s32)ARG(2); u64 dst = ARG(3);
  if (n < 0) { n = 0; while (rd16(c, src + n * 2)) n++; n++; }
  for (int i = 0; i < n; i++) { u16 ch = rd16(c, src + i * 2); u16 t = 0;
    if (type == 1) { if (ch >= 'A' && ch <= 'Z') t |= 0x1 | 0x100; if (ch >= 'a' && ch <= 'z') t |= 0x2 | 0x100; if (ch >= '0' && ch <= '9') t |= 0x4 | 0x200; if (ch == ' ' || (ch >= 9 && ch <= 13)) t |= 0x8 | 0x40; if (ch < 32 || ch == 127) t |= 0x20; if ((ch >= 33 && ch <= 47) || (ch >= 58 && ch <= 64) || (ch >= 91 && ch <= 96) || (ch >= 123 && ch <= 126)) t |= 0x10; if (ch >= 128) t |= 0x100 | 0x1; if (ch == ' ') t |= 0x48; }
    wr16(c, dst + i * 2, t); } RET(1); }
static void s_GetStringTypeA(Cpu *c) { u32 type = (u32)ARG(1); u64 src = ARG(2); int n = (int)(s32)ARG(3); u64 dst = ARG(4);
  if (n < 0) { n = 0; while (rd8(c, src + n)) n++; n++; }
  for (int i = 0; i < n; i++) { u8 ch = rd8(c, src + i); u16 t = 0; if (type == 1) { if (ch >= 'A' && ch <= 'Z') t |= 0x101; if (ch >= 'a' && ch <= 'z') t |= 0x102; if (ch >= '0' && ch <= '9') t |= 0x204; if (ch == ' ' || (ch >= 9 && ch <= 13)) t |= 0x48; if (ch < 32 || ch == 127) t |= 0x20; } wr16(c, dst + i * 2, t); } RET(1); }
static void s_MultiByteToWideChar(Cpu *c) { u64 src = ARG(2); int n = (int)(s32)ARG(3); u64 dst = ARG(4); int dn = (int)ARG(5);
  int si = 0, out = 0; for (;;) { if (n >= 0 && si >= n) break; u32 b = rd8(c, src + si); if (n < 0 && !b) { si++; if (dn) { if (out < dn) wr16(c, dst + out * 2, 0); else { W->last_error = 122; RET(0); return; } } out++; break; }
    u32 cp = b; int extra = 0; if (b >= 0xF0) { cp = b & 7; extra = 3; } else if (b >= 0xE0) { cp = b & 15; extra = 2; } else if (b >= 0xC0) { cp = b & 31; extra = 1; } si++;
    for (int k = 0; k < extra; k++) { if (n >= 0 && si >= n) break; u8 nb = rd8(c, src + si); if ((nb & 0xC0) != 0x80) break; cp = (cp << 6) | (nb & 0x3f); si++; }
    if (cp >= 0x10000) { cp -= 0x10000; if (dn) { if (out + 1 < dn) { wr16(c, dst + out * 2, 0xD800 | (cp >> 10)); wr16(c, dst + out * 2 + 2, 0xDC00 | (cp & 0x3ff)); } else { W->last_error = 122; RET(0); return; } } out += 2; }
    else { if (dn) { if (out < dn) wr16(c, dst + out * 2, (u16)cp); else { W->last_error = 122; RET(0); return; } } out++; }
    if (c->status) return; }
  RET(out); }
static void s_WideCharToMultiByte(Cpu *c) { u64 src = ARG(2); int n = (int)(s32)ARG(3); u64 dst = ARG(4); int dn = (int)ARG(5); u64 used = ARG(7);
  int si = 0, out = 0; char tmp[4];
  for (;;) { if (n >= 0 && si >= n) break; u32 ch = rd16(c, src + si * 2); si++; if (n < 0 && !ch) { if (dn) { if (out < dn) wr8(c, dst + out, 0); else { W->last_error = 122; RET(0); return; } } out++; break; }
    if (ch >= 0xD800 && ch < 0xDC00 && (n < 0 || si < n)) { u32 lo = rd16(c, src + si * 2); if (lo >= 0xDC00 && lo < 0xE000) { ch = 0x10000 + ((ch - 0xD800) << 10) + (lo - 0xDC00); si++; } }
    int l; if (ch < 0x80) { tmp[0] = (char)ch; l = 1; } else if (ch < 0x800) { tmp[0] = (char)(0xC0 | ch >> 6); tmp[1] = (char)(0x80 | (ch & 0x3f)); l = 2; } else if (ch < 0x10000) { tmp[0] = (char)(0xE0 | ch >> 12); tmp[1] = (char)(0x80 | ((ch >> 6) & 0x3f)); tmp[2] = (char)(0x80 | (ch & 0x3f)); l = 3; } else { tmp[0] = (char)(0xF0 | ch >> 18); tmp[1] = (char)(0x80 | ((ch >> 12) & 0x3f)); tmp[2] = (char)(0x80 | ((ch >> 6) & 0x3f)); tmp[3] = (char)(0x80 | (ch & 0x3f)); l = 4; }
    if ((u32)ARG(0) != 65001 && ch >= 0x80) { tmp[0] = ch < 0x100 ? (char)ch : '?'; l = 1; }
    if (dn) { if (out + l <= dn) for (int k = 0; k < l; k++) wr8(c, dst + out + k, (u8)tmp[k]); else { W->last_error = 122; RET(0); return; } } out += l; if (c->status) return; }
  if (used) wr32(c, used, 0); RET(out); }
static void s_CharUpperA(Cpu *c) { u64 a = ARG(0); if (a < 0x10000) { u8 ch = (u8)a; RET(ch >= 'a' && ch <= 'z' ? ch - 32 : ch); return; } for (u64 i = 0;; i++) { u8 ch = rd8(c, a + i); if (!ch) break; if (ch >= 'a' && ch <= 'z') wr8(c, a + i, ch - 32); } RET(a); }
static void s_lstrlenA(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 20); RET(s ? e_strlen(s) : 0); }
static void s_lstrlenW(Cpu *c) { int n = 0; while (rd16(c, ARG(0) + n * 2)) n++; RET(n); }
static void s_lstrcpyA(Cpu *c) { const char *s = rd_str(c, ARG(1), 1 << 20); if (s) { size_t l = e_strlen(s) + 1; u8 *d = mem_ptr(c->mem, ARG(0), l, 1); if (d) e_memmove(d, s, l); } RET(ARG(0)); }
static void s_lstrcmpiA(Cpu *c) { const char *a = rd_str(c, ARG(0), 1 << 20), *b = rd_str(c, ARG(1), 1 << 20); RET(a && b ? e_stricmp(a, b) : 0); }
static void s_lstrcmpA(Cpu *c) { const char *a = rd_str(c, ARG(0), 1 << 20), *b = rd_str(c, ARG(1), 1 << 20); RET(a && b ? e_strcmp(a, b) : 0); }
static void s_InterlockedCompareExchange(Cpu *c) { u32 old = rd32(c, ARG(0)); if (old == (u32)ARG(2)) wr32(c, ARG(0), (u32)ARG(1)); RET(old); }
static void s_InterlockedExchange(Cpu *c) { u32 old = rd32(c, ARG(0)); wr32(c, ARG(0), (u32)ARG(1)); RET(old); }
static void s_InterlockedIncrement(Cpu *c) { u32 v = rd32(c, ARG(0)) + 1; wr32(c, ARG(0), v); RET(v); }
static void s_InterlockedDecrement(Cpu *c) { u32 v = rd32(c, ARG(0)) - 1; wr32(c, ARG(0), v); RET(v); }
static void s_InterlockedExchangeAdd(Cpu *c) { u32 v = rd32(c, ARG(0)); wr32(c, ARG(0), v + (u32)ARG(1)); RET(v); }
static void s_QueueUserAPC(Cpu *c) { RET(0); }
static void s_GetTimeZoneInformation(Cpu *c) { u8 *p = mem_ptr(c->mem, ARG(0), 172, 1); if (p) e_memset(p, 0, 172); RET(0); }
static void s_FileTimeToSystemTime(Cpu *c) { fill_systemtime(c, ARG(1)); RET(1); }
static void s_SystemTimeToFileTime(Cpu *c) { wr64(c, ARG(1), now_100ns()); RET(1); }
static void s_FileTimeToLocalFileTime(Cpu *c) { wr64(c, ARG(1), rd64(c, ARG(0))); RET(1); }
static void s_GetDateFormatA(Cpu *c) { RET(0); }
static void s_GetTimeFormatA(Cpu *c) { RET(0); }
static void s_SystemFunction036(Cpu *c) { Win32 *w = W; u64 a = ARG(0); u32 n = (u32)ARG(1); for (u32 i = 0; i < n; i++) { w->rand_state = w->rand_state * 1103515245u + 12345u; wr8(c, a + i, (u8)(w->rand_state >> 16)); } RET(1); }
static void s_GetComputerNameA(Cpu *c) { const char *n = "NEBULA"; u32 sz = rd32(c, ARG(1)); if (sz > 6) { u8 *p = mem_ptr(c->mem, ARG(0), 7, 1); if (p) e_memcpy(p, n, 7); wr32(c, ARG(1), 6); RET(1); } else RET(0); }
static void s_GetUserNameA(Cpu *c) { const char *n = "user"; u32 sz = rd32(c, ARG(1)); if (sz > 4) { u8 *p = mem_ptr(c->mem, ARG(0), 5, 1); if (p) e_memcpy(p, n, 5); wr32(c, ARG(1), 5); RET(1); } else RET(0); }
static void s_GetDriveTypeA(Cpu *c) { RET(3); }
static void s_GetLogicalDrives(Cpu *c) { RET(4); }
static void s_GetDiskFreeSpaceExA(Cpu *c) { RET(0); }
static void s_SetHandleCount(Cpu *c) { RET(ARG(0)); }
static void s_GetHandleInformation(Cpu *c) { RET(0); }
static void s_SetHandleInformation(Cpu *c) { RET(1); }
static void s_DuplicateHandle(Cpu *c) { wr64(c, ARG(3), ARG(1)); RET(1); }
static void s_CreatePipe(Cpu *c) { RET(0); }
static void s_PeekNamedPipe(Cpu *c) { RET(0); }
static void s_GetOverlappedResult(Cpu *c) { RET(0); }
static void s_CreateIoCompletionPort(Cpu *c) { RET(0); }
static void s_Beep(Cpu *c) { RET(1); }
static void s_DebugBreak(Cpu *c) { c->status = RUN_BREAK; }
static void s_GetProcessId(Cpu *c) { RET(4242); }
static void s_K32GetProcessMemoryInfo(Cpu *c) { RET(0); }

/* ================= UCRT / msvcrt ================= */
static void s_malloc(Cpu *c) { RET(w32_heap_alloc(W, ARG(0), 0)); }
static void s_calloc(Cpu *c) { RET(w32_heap_alloc(W, ARG(0) * ARG(1), 1)); }
static void s_realloc(Cpu *c) { if (!ARG(1)) { w32_heap_free(W, ARG(0)); RET(0); return; } RET(heap_realloc(W, ARG(0), ARG(1), 0)); }
static void s_free(Cpu *c) { if (ARG(0)) w32_heap_free(W, ARG(0)); }
static void s_msize(Cpu *c) { RET(w32_heap_size(W, ARG(0))); }
static void s_aligned_malloc(Cpu *c) { u64 al = ARG(1); if (al < 16) al = 16; u64 a = w32_heap_alloc(W, ARG(0) + al, 0); RET((a + al - 1) & ~(al - 1)); }
static void s_aligned_free(Cpu *c) { }
static void s_set_new_mode(Cpu *c) { RET(0); }
static void s_ret0(Cpu *c) { RET(0); }
static void s_ret1(Cpu *c) { RET(1); }
static void s_retm1(Cpu *c) { RET(~0ull); }
static void s_initterm(Cpu *c) { cont_push(c, CONT_INITTERM); Win32 *w = W; w->cont[w->ncont - 1].data[0] = ARG(0); w->cont[w->ncont - 1].data[1] = ARG(1); w->cont[w->ncont - 1].data[2] = 0; c->r[RAX] = 0; w32_cont_resume(c); }
static void s_initterm_e(Cpu *c) { cont_push(c, CONT_INITTERM); Win32 *w = W; w->cont[w->ncont - 1].data[0] = ARG(0); w->cont[w->ncont - 1].data[1] = ARG(1); w->cont[w->ncont - 1].data[2] = 1; c->r[RAX] = 0; w32_cont_resume(c); }
static void s_atexit(Cpu *c) { Win32 *w = W; if (w->n_at_exit < 64) w->at_exit[w->n_at_exit++] = ARG(0); RET(0); }
static void s_onexit(Cpu *c) { Win32 *w = W; if (w->n_at_exit < 64) w->at_exit[w->n_at_exit++] = ARG(0); RET(ARG(0)); }
static void s_register_onexit_function(Cpu *c) { Win32 *w = W; if (w->n_at_exit < 64) w->at_exit[w->n_at_exit++] = ARG(1); RET(0); }
static void s_exit(Cpu *c) { cont_push(c, CONT_ATEXIT); Win32 *w = W; w->cont[w->ncont - 1].data[0] = 1; w->cont[w->ncont - 1].data[1] = ARG(0); w32_cont_resume(c); }
static void s_cexit(Cpu *c) { cont_push(c, CONT_ATEXIT); W->cont[W->ncont - 1].data[0] = 0; w32_cont_resume(c); }
static void s__exit(Cpu *c) { do_exit(c, (int)ARG(0)); }
static void s_abort(Cpu *c) { con_out(W, 2, "abort() called\n", 15); do_exit(c, 3); }
static void s_p___argc(Cpu *c) { RET(W->ubuf + 0x200); }
static void s_p___argv(Cpu *c) { RET(W->ubuf + 0x208); }
static void s_p___wargv(Cpu *c) { RET(W->ubuf + 0x218); }
static void s_p__acmdln(Cpu *c) { RET(W->ubuf + 0x210); }
static void s_p__wcmdln(Cpu *c) { RET(W->ubuf + 0x220); }
static void s_p__environ(Cpu *c) { RET(W->ubuf + 0x228); }
static void s_p__wenviron(Cpu *c) { RET(W->ubuf + 0x230); }
static void s_p__fmode(Cpu *c) { RET(W->ubuf + 0x240); }
static void s_p__commode(Cpu *c) { RET(W->ubuf + 0x248); }
static void s_p__pgmptr(Cpu *c) { RET(W->ubuf + 0x250); }
static void s_get_initial_narrow_environment(Cpu *c) { RET(rd64(c, W->ubuf + 0x228)); }
static void s_get_initial_wide_environment(Cpu *c) { RET(rd64(c, W->ubuf + 0x230)); }
static void s_errno(Cpu *c) { RET(W->ubuf + 0x258); }
static void s_doserrno(Cpu *c) { RET(W->ubuf + 0x25c); }
static void s_acrt_iob_func(Cpu *c) { RET(W->ubuf + (u64)(u32)ARG(0) * 0x60); }
static void s_iob_func(Cpu *c) { RET(W->ubuf); }
static void s_getmainargs(Cpu *c) { wr32(c, ARG(0), rd32(c, W->ubuf + 0x200)); wr64(c, ARG(1), rd64(c, W->ubuf + 0x208)); wr64(c, ARG(2), rd64(c, W->ubuf + 0x228)); RET(0); }
static void s_wgetmainargs(Cpu *c) { wr32(c, ARG(0), rd32(c, W->ubuf + 0x200)); wr64(c, ARG(1), rd64(c, W->ubuf + 0x218)); wr64(c, ARG(2), rd64(c, W->ubuf + 0x230)); RET(0); }
static void s_set_app_type(Cpu *c) { }
static void s_setusermatherr(Cpu *c) { }
static void s_controlfp(Cpu *c) { RET(0x9001f); }
static void s_controlfp_s(Cpu *c) { if (ARG(0)) wr32(c, ARG(0), 0x9001f); RET(0); }
static void s_fpreset(Cpu *c) { }
static void s_set_fmode(Cpu *c) { RET(0); }
static void s_signal(Cpu *c) { RET(0); }
static void s_raise(Cpu *c) { cpu_fault(c, "raise(%d)", (int)ARG(0)); }

static void stream_write(Cpu *c, u64 file, const char *s, size_t n) { int fd = fd_of_file(W, file); if (fd > 0) con_out(W, fd, s, n); }
static void s_stdio_common_vfprintf(Cpu *c) { ArgSrc src = { c, 1, 0, ARG(4) }; int n = g_format(c, g_fmt_out, sizeof g_fmt_out, ARG(2), 0, &src); if (c->status) return; stream_write(c, ARG(1), g_fmt_out, n < (int)sizeof g_fmt_out ? n : sizeof g_fmt_out - 1); RET(n); }
static void s_stdio_common_vfwprintf(Cpu *c) { ArgSrc src = { c, 1, 0, ARG(4) }; int n = g_format(c, g_fmt_out, sizeof g_fmt_out, ARG(2), 1, &src); if (c->status) return; stream_write(c, ARG(1), g_fmt_out, n < (int)sizeof g_fmt_out ? n : sizeof g_fmt_out - 1); RET(n); }
static void vsprintf_common(Cpu *c, u64 opts, u64 buf, u64 len, u64 fmt, u64 va, int wide) {
  ArgSrc src = { c, 1, 0, va }; int n = g_format(c, g_fmt_out, sizeof g_fmt_out, fmt, wide, &src); if (c->status) return;
  int standard = (opts & 2) != 0, trunc = (opts & 1) != 0;
  if (buf && len) {
    u64 cp = (u64)n < len ? (u64)n : len - 1;
    if (!wide) { u8 *p = mem_ptr(c->mem, buf, cp + 1, 1); if (p) { e_memcpy(p, g_fmt_out, cp); p[cp] = 0; } }
    else { for (u64 i = 0; i < cp; i++) wr16(c, buf + i * 2, (u8)g_fmt_out[i]); wr16(c, buf + cp * 2, 0); }
    if ((u64)n >= len && !standard && !trunc) { RET((u64)-1); return; }
    if ((u64)n >= len && trunc && !standard) { RET((u64)-1); return; }
  }
  RET(n);
}
static void s_stdio_common_vsprintf(Cpu *c) { vsprintf_common(c, ARG(0), ARG(1), ARG(2), ARG(3), ARG(5), 0); }
static void s_stdio_common_vsprintf_s(Cpu *c) { vsprintf_common(c, ARG(0) | 2, ARG(1), ARG(2), ARG(3), ARG(5), 0); }
static void s_stdio_common_vsnprintf_s(Cpu *c) { vsprintf_common(c, ARG(0) | 2, ARG(1), ARG(2) < ARG(3) ? ARG(2) : ARG(3) + 1, ARG(4), ARG(6), 0); }
static void s_stdio_common_vswprintf(Cpu *c) { vsprintf_common(c, ARG(0), ARG(1), ARG(2), ARG(3), ARG(5), 1); }
static void s_stdio_common_vswprintf_s(Cpu *c) { vsprintf_common(c, ARG(0) | 2, ARG(1), ARG(2), ARG(3), ARG(5), 1); }
static void s_printf(Cpu *c) { ArgSrc src = { c, 0, 1, 0 }; int n = g_format(c, g_fmt_out, sizeof g_fmt_out, ARG(0), 0, &src); if (c->status) return; con_out(W, 1, g_fmt_out, n < (int)sizeof g_fmt_out ? n : sizeof g_fmt_out - 1); RET(n); }
static void s_wprintf(Cpu *c) { ArgSrc src = { c, 0, 1, 0 }; int n = g_format(c, g_fmt_out, sizeof g_fmt_out, ARG(0), 1, &src); if (c->status) return; con_out(W, 1, g_fmt_out, n < (int)sizeof g_fmt_out ? n : sizeof g_fmt_out - 1); RET(n); }
static void s_vprintf(Cpu *c) { ArgSrc src = { c, 1, 0, ARG(1) }; int n = g_format(c, g_fmt_out, sizeof g_fmt_out, ARG(0), 0, &src); if (c->status) return; con_out(W, 1, g_fmt_out, n < (int)sizeof g_fmt_out ? n : sizeof g_fmt_out - 1); RET(n); }
static void s_fprintf(Cpu *c) { ArgSrc src = { c, 0, 2, 0 }; int n = g_format(c, g_fmt_out, sizeof g_fmt_out, ARG(1), 0, &src); if (c->status) return; stream_write(c, ARG(0), g_fmt_out, n < (int)sizeof g_fmt_out ? n : sizeof g_fmt_out - 1); RET(n); }
static void s_vfprintf(Cpu *c) { ArgSrc src = { c, 1, 0, ARG(2) }; int n = g_format(c, g_fmt_out, sizeof g_fmt_out, ARG(1), 0, &src); if (c->status) return; stream_write(c, ARG(0), g_fmt_out, n < (int)sizeof g_fmt_out ? n : sizeof g_fmt_out - 1); RET(n); }
static void s_sprintf(Cpu *c) { ArgSrc src = { c, 0, 2, 0 }; int n = g_format(c, g_fmt_out, sizeof g_fmt_out, ARG(1), 0, &src); if (c->status) return; u8 *p = mem_ptr(c->mem, ARG(0), n + 1, 1); if (p) e_memcpy(p, g_fmt_out, n + 1); RET(n); }
static void s_snprintf(Cpu *c) { ArgSrc src = { c, 0, 3, 0 }; int n = g_format(c, g_fmt_out, sizeof g_fmt_out, ARG(2), 0, &src); if (c->status) return; u64 len = ARG(1); if (len) { u64 cp = (u64)n < len ? (u64)n : len - 1; u8 *p = mem_ptr(c->mem, ARG(0), cp + 1, 1); if (p) { e_memcpy(p, g_fmt_out, cp); p[cp] = 0; } } RET(n); }
static void s_vsnprintf(Cpu *c) { ArgSrc src = { c, 1, 0, ARG(3) }; int n = g_format(c, g_fmt_out, sizeof g_fmt_out, ARG(2), 0, &src); if (c->status) return; u64 len = ARG(1); if (len) { u64 cp = (u64)n < len ? (u64)n : len - 1; u8 *p = mem_ptr(c->mem, ARG(0), cp + 1, 1); if (p) { e_memcpy(p, g_fmt_out, cp); p[cp] = 0; } } RET(n); }
static void s_vsprintf(Cpu *c) { ArgSrc src = { c, 1, 0, ARG(2) }; int n = g_format(c, g_fmt_out, sizeof g_fmt_out, ARG(1), 0, &src); if (c->status) return; u8 *p = mem_ptr(c->mem, ARG(0), n + 1, 1); if (p) e_memcpy(p, g_fmt_out, n + 1); RET(n); }
static void s_puts(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 20); if (!s) { RET(~0ull); return; } con_out(W, 1, s, e_strlen(s)); con_out(W, 1, "\n", 1); RET(0); }
static void s_putws(Cpu *c) { char b[4096]; int n = wstr_to_utf8(c, ARG(0), b, sizeof b, -1); con_out(W, 1, b, n); con_out(W, 1, "\n", 1); RET(0); }
static void s_fputs(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 20); if (!s) { RET(~0ull); return; } stream_write(c, ARG(1), s, e_strlen(s)); RET(0); }
static void s_putchar(Cpu *c) { char ch = (char)ARG(0); con_out(W, 1, &ch, 1); RET(ARG(0) & 0xff); }
static void s_fputc(Cpu *c) { char ch = (char)ARG(0); stream_write(c, ARG(1), &ch, 1); RET(ARG(0) & 0xff); }
static void s_fwrite(Cpu *c) { u64 n = ARG(1) * ARG(2); const u8 *p = mem_ptr(c->mem, ARG(0), n, 0); if (!p) { RET(0); return; } stream_write(c, ARG(3), (const char *)p, n); RET(ARG(2)); }
static void s_fflush(Cpu *c) { RET(0); }
static void s_setvbuf(Cpu *c) { RET(0); }
static void s_fopen(Cpu *c) { const char *n = rd_str(c, ARG(0), 1024); w32_report(W, "fopen(\"%s\") → no file system in the sandbox", n ? n : "?"); wr32(c, W->ubuf + 0x258, 2); RET(0); }
static void s_fopen_s(Cpu *c) { wr64(c, ARG(0), 0); RET(2); }
static void s_fclose(Cpu *c) { RET(0); }
static void s_fgets(Cpu *c) { RET(0); }
static void s_fread(Cpu *c) { RET(0); }
static void s_getchar(Cpu *c) { RET(~0ull); }
static void s_fgetc(Cpu *c) { RET(~0ull); }
static void s_getc(Cpu *c) { RET(~0ull); }
static void s_feof(Cpu *c) { RET(1); }
static void s_ferror(Cpu *c) { RET(0); }
static void s_fileno(Cpu *c) { int fd = fd_of_file(W, ARG(0)); RET(fd < 0 ? 0 : fd); }
static void s_isatty(Cpu *c) { RET(ARG(0) <= 2 ? 1 : 0); }
static void s_write(Cpu *c) { u64 fd = ARG(0); u32 n = (u32)ARG(2); if (fd == 1 || fd == 2) { const u8 *p = mem_ptr(c->mem, ARG(1), n, 0); if (p) con_out(W, (int)fd, (const char *)p, n); RET(n); } else RET(~0ull); }
static void s_read(Cpu *c) { RET(0); }
static void s_open(Cpu *c) { RET(~0ull); }
static void s_close(Cpu *c) { RET(0); }
static void s_time64(Cpu *c) { u64 t = unix_time(); if (ARG(0)) wr64(c, ARG(0), t); RET(t); }
static void s_clock(Cpu *c) { RET((u64)(host_now_ms() - W->start_ms)); }
static void s_localtime64(Cpu *c) { u64 t = rd64(c, ARG(0)); u64 a = W->ubuf + 0x300; u64 days = t / 86400, rem = t % 86400; s64 z = (s64)days + 719468; s64 era = z / 146097; s64 doe = z - era * 146097; s64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; s64 y = yoe + era * 400; s64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100); s64 mp = (5 * doy + 2) / 153; s64 d = doy - (153 * mp + 2) / 5 + 1; s64 m = mp < 10 ? mp + 3 : mp - 9; if (m <= 2) y++;
  wr32(c, a, (u32)(rem % 60)); wr32(c, a + 4, (u32)(rem / 60 % 60)); wr32(c, a + 8, (u32)(rem / 3600)); wr32(c, a + 12, (u32)d); wr32(c, a + 16, (u32)(m - 1)); wr32(c, a + 20, (u32)(y - 1900)); wr32(c, a + 24, (u32)((days + 4) % 7)); wr32(c, a + 28, 0); wr32(c, a + 32, 0); RET(a); }
static void s_gmtime64(Cpu *c) { s_localtime64(c); }
static void s_localtime64_s(Cpu *c) { c->r[RCX] = ARG(1); u64 dst = ARG(0); s_localtime64(c); u8 *s = mem_ptr(c->mem, c->r[RAX], 36, 0), *d = mem_ptr(c->mem, dst, 36, 1); if (s && d) e_memcpy(d, s, 36); RET(0); }
static void s_mktime64(Cpu *c) { RET(unix_time()); }
static void s_strftime(Cpu *c) { const char *f = rd_str(c, ARG(2), 256); char out[256]; int n = e_snprintf(out, sizeof out, "%s", f ? f : ""); if ((u64)n + 1 > ARG(1)) { RET(0); return; } u8 *p = mem_ptr(c->mem, ARG(0), n + 1, 1); if (p) e_memcpy(p, out, n + 1); RET(n); }
static void s_tzset(Cpu *c) { }
static void s_timezone(Cpu *c) { RET(W->ubuf + 0x260); }
static void s_daylight(Cpu *c) { RET(W->ubuf + 0x264); }
static void s_tzname(Cpu *c) { RET(W->ubuf + 0x268); }
static void s_getenv(Cpu *c) { RET(0); }
static void s_getenv_s(Cpu *c) { if (ARG(0)) wr64(c, ARG(0), 0); RET(0); }
static void s_system(Cpu *c) { RET(ARG(0) ? ~0ull : 0); }
static void s_rand(Cpu *c) { Win32 *w = W; w->rand_state = w->rand_state * 214013u + 2531011u; RET((w->rand_state >> 16) & 0x7fff); }
static void s_srand(Cpu *c) { W->rand_state = (u32)ARG(0); }
static void s_strlen(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 30); if (!s) { cpu_fault(c, "strlen on bad pointer"); return; } RET(e_strlen(s)); }
static void s_wcslen(Cpu *c) { int n = 0; while (rd16(c, ARG(0) + (u64)n * 2) && !c->status) n++; RET(n); }
static void s_strnlen(Cpu *c) { u64 n = 0, m = ARG(1); while (n < m && rd8(c, ARG(0) + n)) n++; RET(n); }
static void s_strcmp(Cpu *c) { const char *a = rd_str(c, ARG(0), 1 << 30), *b = rd_str(c, ARG(1), 1 << 30); if (!a || !b) { cpu_fault(c, "strcmp bad pointer"); return; } RET((u64)(s64)e_strcmp(a, b)); }
static void s_stricmp(Cpu *c) { const char *a = rd_str(c, ARG(0), 1 << 30), *b = rd_str(c, ARG(1), 1 << 30); if (!a || !b) { cpu_fault(c, "stricmp bad pointer"); return; } RET((u64)(s64)e_stricmp(a, b)); }
static void s_strncmp(Cpu *c) { u64 n = ARG(2); u64 a = ARG(0), b = ARG(1); for (u64 i = 0; i < n; i++) { u8 x = rd8(c, a + i), y = rd8(c, b + i); if (x != y) { RET((u64)(s64)(x - y)); return; } if (!x) break; } RET(0); }
static void s_strnicmp(Cpu *c) { u64 n = ARG(2); u64 a = ARG(0), b = ARG(1); for (u64 i = 0; i < n; i++) { u8 x = rd8(c, a + i), y = rd8(c, b + i); if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) { RET((u64)(s64)(x - y)); return; } if (!x) break; } RET(0); }
static void s_wcscmp(Cpu *c) { u64 a = ARG(0), b = ARG(1); for (u64 i = 0;; i++) { u16 x = rd16(c, a + i * 2), y = rd16(c, b + i * 2); if (x != y) { RET((u64)(s64)(x < y ? -1 : 1)); return; } if (!x) break; } RET(0); }
static void s_strcpy(Cpu *c) { const char *s = rd_str(c, ARG(1), 1 << 30); if (!s) { cpu_fault(c, "strcpy bad pointer"); return; } size_t l = e_strlen(s) + 1; u8 *d = mem_ptr(c->mem, ARG(0), l, 1); if (!d) { cpu_fault(c, "strcpy bad dest"); return; } e_memmove(d, s, l); RET(ARG(0)); }
static void s_strncpy(Cpu *c) { u64 n = ARG(2), d = ARG(0), s = ARG(1); u64 i = 0; for (; i < n; i++) { u8 ch = rd8(c, s + i); wr8(c, d + i, ch); if (!ch) { i++; break; } } for (; i < n; i++) wr8(c, d + i, 0); RET(d); }
static void s_strcat(Cpu *c) { const char *d0 = rd_str(c, ARG(0), 1 << 30), *s = rd_str(c, ARG(1), 1 << 30); if (!d0 || !s) { cpu_fault(c, "strcat bad pointer"); return; } size_t dl = e_strlen(d0), sl = e_strlen(s) + 1; u8 *d = mem_ptr(c->mem, ARG(0) + dl, sl, 1); if (d) e_memmove(d, s, sl); RET(ARG(0)); }
static void s_strchr(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 30); if (!s) { RET(0); return; } char *p = e_strchr(s, (int)ARG(1)); RET(p ? ARG(0) + (u64)(p - s) : 0); }
static void s_strrchr(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 30); if (!s) { RET(0); return; } const char *last = 0; char ch = (char)ARG(1); for (const char *p = s;; p++) { if (*p == ch) last = p; if (!*p) break; } RET(last ? ARG(0) + (u64)(last - s) : 0); }
static void s_strstr(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 30), *n = rd_str(c, ARG(1), 1 << 30); if (!s || !n) { RET(0); return; } size_t nl = e_strlen(n); for (const char *p = s; *p; p++) if (!e_strncmp(p, n, nl)) { RET(ARG(0) + (u64)(p - s)); return; } RET(nl ? 0 : ARG(0)); }
static void s_strdup(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 30); RET(s ? w32_guest_strdup(W, s) : 0); }
static void s_strtol(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 20); if (!s) { RET(0); return; } char *e; long long v = e_strtoll(s, &e, (int)ARG(2)); if (ARG(1)) wr64(c, ARG(1), ARG(0) + (u64)(e - s)); if (v > 0x7fffffff) v = 0x7fffffff; if (v < -0x7fffffffll - 1) v = -0x7fffffffll - 1; RET((u64)v); }
static void s_strtoul(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 20); if (!s) { RET(0); return; } char *e; long long v = e_strtoll(s, &e, (int)ARG(2)); if (ARG(1)) wr64(c, ARG(1), ARG(0) + (u64)(e - s)); RET((u64)(u32)v); }
static void s_strtoll(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 20); if (!s) { RET(0); return; } char *e; long long v = e_strtoll(s, &e, (int)ARG(2)); if (ARG(1)) wr64(c, ARG(1), ARG(0) + (u64)(e - s)); RET((u64)v); }
static void s_atoi(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 20); RET(s ? (u64)(u32)e_strtoll(s, 0, 10) : 0); }
static void s_atol(Cpu *c) { s_atoi(c); }
static void s_atoll(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 20); RET(s ? (u64)e_strtoll(s, 0, 10) : 0); }
static void s_strtod(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 20); if (!s) { RET(0); return; } char *e; double v = e_strtod(s, &e); if (ARG(1)) wr64(c, ARG(1), ARG(0) + (u64)(e - s)); c->x[0].f64[0] = v; }
static void s_atof(Cpu *c) { const char *s = rd_str(c, ARG(0), 1 << 20); c->x[0].f64[0] = s ? e_strtod(s, 0) : 0; }
static void s_toupper(Cpu *c) { int ch = (int)ARG(0); RET(ch >= 'a' && ch <= 'z' ? ch - 32 : ch); }
static void s_tolower(Cpu *c) { int ch = (int)ARG(0); RET(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch); }
static void s_isdigit(Cpu *c) { int ch = (int)ARG(0); RET(ch >= '0' && ch <= '9'); }
static void s_isalpha(Cpu *c) { int ch = (int)ARG(0) | 32; RET(ch >= 'a' && ch <= 'z'); }
static void s_isalnum(Cpu *c) { int ch = (int)ARG(0); RET((ch >= '0' && ch <= '9') || ((ch | 32) >= 'a' && (ch | 32) <= 'z')); }
static void s_isspace(Cpu *c) { int ch = (int)ARG(0); RET(ch == ' ' || (ch >= 9 && ch <= 13)); }
static void s_isupper(Cpu *c) { int ch = (int)ARG(0); RET(ch >= 'A' && ch <= 'Z'); }
static void s_islower(Cpu *c) { int ch = (int)ARG(0); RET(ch >= 'a' && ch <= 'z'); }
static void s_isprint(Cpu *c) { int ch = (int)ARG(0); RET(ch >= 32 && ch < 127); }
static void s_isxdigit(Cpu *c) { int ch = (int)ARG(0); RET((ch >= '0' && ch <= '9') || ((ch | 32) >= 'a' && (ch | 32) <= 'f')); }
static void s_ispunct(Cpu *c) { int ch = (int)ARG(0); RET((ch >= 33 && ch <= 47) || (ch >= 58 && ch <= 64) || (ch >= 91 && ch <= 96) || (ch >= 123 && ch <= 126)); }
static void s_pctype_func(Cpu *c) { RET(W->ubuf + 0x400); }
static void s_memcpy(Cpu *c) { u64 n = ARG(2); if (n) { u8 *d = mem_ptr(c->mem, ARG(0), n, 1); const u8 *s = mem_ptr(c->mem, ARG(1), n, 0); if (!d || !s) { cpu_fault(c, "memcpy fault"); return; } e_memmove(d, s, (size_t)n); } RET(ARG(0)); }
static void s_memset(Cpu *c) { u64 n = ARG(2); if (n) { u8 *d = mem_ptr(c->mem, ARG(0), n, 1); if (!d) { cpu_fault(c, "memset fault"); return; } e_memset(d, (int)ARG(1), (size_t)n); } RET(ARG(0)); }
static void s_memcmp(Cpu *c) { u64 n = ARG(2); if (!n) { RET(0); return; } const u8 *a = mem_ptr(c->mem, ARG(0), n, 0), *b = mem_ptr(c->mem, ARG(1), n, 0); if (!a || !b) { cpu_fault(c, "memcmp fault"); return; } RET((u64)(s64)e_memcmp(a, b, (size_t)n)); }
static void s_memchr(Cpu *c) { u64 n = ARG(2); const u8 *a = mem_ptr(c->mem, ARG(0), n, 0); if (!a) { RET(0); return; } for (u64 i = 0; i < n; i++) if (a[i] == (u8)ARG(1)) { RET(ARG(0) + i); return; } RET(0); }
static void s_qsort(Cpu *c) { u64 n = ARG(1); if (n < 2) return; cont_push(c, CONT_QSORT); Win32 *w = W; int i = w->ncont - 1; w->cont[i].data[0] = ARG(0); w->cont[i].data[1] = n; w->cont[i].data[2] = ARG(2); w->cont[i].data[3] = ARG(3); w->cont[i].data[4] = 1; w->cont[i].data[5] = 1;
  guest_call(c, ARG(3), ARG(0), ARG(0) + ARG(2), 0, 0); }
static void s_bsearch(Cpu *c) { cpu_fault(c, "bsearch with guest callback is not supported yet"); }
static void s_setjmp(Cpu *c) { u64 a = ARG(0); wr64(c, a, 0); wr64(c, a + 8, c->r[RBX]); wr64(c, a + 16, c->r[RSP]); wr64(c, a + 24, c->r[RBP]); wr64(c, a + 32, c->r[RSI]); wr64(c, a + 40, c->r[RDI]); wr64(c, a + 48, c->r[R12]); wr64(c, a + 56, c->r[R13]); wr64(c, a + 64, c->r[R14]); wr64(c, a + 72, c->r[R15]); wr64(c, a + 80, c->rip); for (int k = 6; k < 16; k++) { wr64(c, a + 96 + (k - 6) * 16, c->x[k].q[0]); wr64(c, a + 104 + (k - 6) * 16, c->x[k].q[1]); } RET(0); }
static void s_longjmp(Cpu *c) { u64 a = ARG(0); u64 v = ARG(1); c->r[RBX] = rd64(c, a + 8); c->r[RSP] = rd64(c, a + 16); c->r[RBP] = rd64(c, a + 24); c->r[RSI] = rd64(c, a + 32); c->r[RDI] = rd64(c, a + 40); c->r[R12] = rd64(c, a + 48); c->r[R13] = rd64(c, a + 56); c->r[R14] = rd64(c, a + 64); c->r[R15] = rd64(c, a + 72); c->rip = rd64(c, a + 80); for (int k = 6; k < 16; k++) { c->x[k].q[0] = rd64(c, a + 96 + (k - 6) * 16); c->x[k].q[1] = rd64(c, a + 104 + (k - 6) * 16); } RET(v ? v : 1); }
static void s_invalid_parameter_noinfo(Cpu *c) { cpu_fault(c, "CRT invalid parameter"); }
static void s_div(Cpu *c) { s32 a = (s32)ARG(0), b = (s32)ARG(1); if (!b) { cpu_fault(c, "div by zero"); return; } RET(((u64)(u32)(a % b) << 32) | (u32)(a / b)); }
static void s_abs(Cpu *c) { s32 v = (s32)ARG(0); RET((u32)(v < 0 ? -v : v)); }
static void s_labs(Cpu *c) { s_abs(c); }
static void s_llabs(Cpu *c) { s64 v = (s64)ARG(0); RET((u64)(v < 0 ? -v : v)); }
/* math: double in xmm0 (and xmm1) */
#define MATH1(name, op) static void s_##name(Cpu *c) { c->x[0].f64[0] = host_math(op, c->x[0].f64[0], 0); }
#define MATH2(name, op) static void s_##name(Cpu *c) { c->x[0].f64[0] = host_math(op, c->x[0].f64[0], c->x[1].f64[0]); }
MATH1(sin, 0) MATH1(cos, 1) MATH1(tan, 2) MATH1(exp, 3) MATH1(log, 4) MATH2(pow, 5) MATH2(atan2, 6) MATH1(asin, 7) MATH1(acos, 8) MATH1(atan, 9) MATH1(sinh, 10) MATH1(cosh, 11) MATH1(tanh, 12) MATH1(log10, 13) MATH1(log2, 14) MATH1(cbrt, 15)
static void s_sqrt(Cpu *c) { c->x[0].f64[0] = e_sqrt(c->x[0].f64[0]); }
static void s_floor(Cpu *c) { c->x[0].f64[0] = e_floor(c->x[0].f64[0]); }
static void s_ceil(Cpu *c) { c->x[0].f64[0] = e_ceil(c->x[0].f64[0]); }
static void s_fabs(Cpu *c) { c->x[0].f64[0] = e_fabs(c->x[0].f64[0]); }
static void s_fmod(Cpu *c) { c->x[0].f64[0] = e_fmod(c->x[0].f64[0], c->x[1].f64[0]); }
static void s_round(Cpu *c) { double v = c->x[0].f64[0]; c->x[0].f64[0] = v < 0 ? -e_floor(-v + 0.5) : e_floor(v + 0.5); }
static void s_trunc(Cpu *c) { c->x[0].f64[0] = __builtin_trunc(c->x[0].f64[0]); }
static void s_ldexp(Cpu *c) { double v = c->x[0].f64[0]; int e = (int)c->r[RDX]; while (e > 0) { v *= 2; e--; } while (e < 0) { v /= 2; e++; } c->x[0].f64[0] = v; }
static void s_frexp(Cpu *c) { double v = c->x[0].f64[0]; int e = 0; if (v != 0) { double a = v < 0 ? -v : v; while (a >= 1) { a /= 2; e++; } while (a < 0.5) { a *= 2; e--; } v = v < 0 ? -a : a; } wr32(c, c->r[RDX], (u32)e); c->x[0].f64[0] = v; }
static void s_modf(Cpu *c) { double v = c->x[0].f64[0]; double ip = __builtin_trunc(v); u64 bits; e_memcpy(&bits, &ip, 8); wr64(c, c->r[RDX], bits); c->x[0].f64[0] = v - ip; }
static void s_hypot(Cpu *c) { double a = c->x[0].f64[0], b = c->x[1].f64[0]; c->x[0].f64[0] = e_sqrt(a * a + b * b); }
static void s_sinf(Cpu *c) { c->x[0].f32[0] = (float)host_math(0, c->x[0].f32[0], 0); }
static void s_cosf(Cpu *c) { c->x[0].f32[0] = (float)host_math(1, c->x[0].f32[0], 0); }
static void s_tanf(Cpu *c) { c->x[0].f32[0] = (float)host_math(2, c->x[0].f32[0], 0); }
static void s_expf(Cpu *c) { c->x[0].f32[0] = (float)host_math(3, c->x[0].f32[0], 0); }
static void s_logf(Cpu *c) { c->x[0].f32[0] = (float)host_math(4, c->x[0].f32[0], 0); }
static void s_powf(Cpu *c) { c->x[0].f32[0] = (float)host_math(5, c->x[0].f32[0], c->x[1].f32[0]); }
static void s_atan2f(Cpu *c) { c->x[0].f32[0] = (float)host_math(6, c->x[0].f32[0], c->x[1].f32[0]); }
static void s_sqrtf(Cpu *c) { c->x[0].f32[0] = __builtin_sqrtf(c->x[0].f32[0]); }
static void s_floorf(Cpu *c) { c->x[0].f32[0] = __builtin_floorf(c->x[0].f32[0]); }
static void s_ceilf(Cpu *c) { c->x[0].f32[0] = __builtin_ceilf(c->x[0].f32[0]); }
static void s_fabsf(Cpu *c) { c->x[0].f32[0] = __builtin_fabsf(c->x[0].f32[0]); }
static void s_fmodf(Cpu *c) { c->x[0].f32[0] = (float)e_fmod(c->x[0].f32[0], c->x[1].f32[0]); }
static void s_roundf(Cpu *c) { float v = c->x[0].f32[0]; c->x[0].f32[0] = v < 0 ? -__builtin_floorf(-v + 0.5f) : __builtin_floorf(v + 0.5f); }
static void s_fmin(Cpu *c) { double a = c->x[0].f64[0], b = c->x[1].f64[0]; c->x[0].f64[0] = a != a ? b : b != b ? a : a < b ? a : b; }
static void s_fmax(Cpu *c) { double a = c->x[0].f64[0], b = c->x[1].f64[0]; c->x[0].f64[0] = a != a ? b : b != b ? a : a > b ? a : b; }
static void s_copysign(Cpu *c) { double a = e_fabs(c->x[0].f64[0]); c->x[0].f64[0] = (c->x[1].q[0] >> 63) ? -a : a; }
static void s_exp2(Cpu *c) { c->x[0].f64[0] = host_math(5, 2.0, c->x[0].f64[0]); }
static void s_lround(Cpu *c) { double v = c->x[0].f64[0]; RET((u64)(s64)(v < 0 ? -e_floor(-v + 0.5) : e_floor(v + 0.5))); }
static void s_dclass(Cpu *c) { double v = c->x[0].f64[0]; RET(v != v ? 2 : (v > 1.7976931348623157e308 || v < -1.7976931348623157e308) ? 1 : v == 0 ? 0x10 : 0x4); }
static void s_fdclass(Cpu *c) { s_dclass(c); }
static void s_dsign(Cpu *c) { RET(c->x[0].f64[0] < 0 ? 1 : 0); }

/* ---------- table ---------- */
#define S(n) { "", #n, s_##n }
#define SA(api, fn) { "", api, s_##fn }
static const ShimDef shims[] = {
  S(GetLastError), S(SetLastError), S(GetCurrentProcess), S(GetCurrentThread), S(GetCurrentProcessId), S(GetCurrentThreadId), S(ExitProcess), S(TerminateProcess),
  S(GetTickCount), S(GetTickCount64), S(QueryPerformanceCounter), S(QueryPerformanceFrequency), S(GetSystemTimeAsFileTime), S(GetSystemTimePreciseAsFileTime), S(GetSystemTime), S(GetLocalTime), S(Sleep), S(SleepEx),
  S(GetStdHandle), S(WriteFile), S(WriteConsoleA), S(WriteConsoleW), S(ReadFile), S(ReadConsoleA), S(GetConsoleMode), S(SetConsoleMode), S(GetConsoleOutputCP), S(GetConsoleCP), S(SetConsoleOutputCP), S(SetConsoleCP), S(GetConsoleScreenBufferInfo), S(SetConsoleTextAttribute), S(SetConsoleTitleA), S(GetConsoleWindow), S(AllocConsole), S(SetConsoleCtrlHandler), S(FlushFileBuffers), S(GetFileType), S(CloseHandle),
  S(CreateFileA), S(CreateFileW), S(GetFileAttributesA), S(GetFileAttributesW), S(GetFileAttributesExW), S(FindFirstFileA), S(FindFirstFileW), S(FindClose), S(DeleteFileA), S(GetFullPathNameA), S(GetCurrentDirectoryA), S(GetCurrentDirectoryW), S(GetTempPathA), S(SetFilePointer), S(GetFileSize),
  S(GetCommandLineA), S(GetCommandLineW), S(GetEnvironmentStrings), S(GetEnvironmentStringsW), S(FreeEnvironmentStringsW), S(FreeEnvironmentStringsA), S(GetEnvironmentVariableA), S(GetEnvironmentVariableW), S(SetEnvironmentVariableA), S(ExpandEnvironmentStringsA), S(GetStartupInfoA), S(GetStartupInfoW),
  S(GetModuleHandleA), S(GetModuleHandleW), S(GetModuleHandleExW), S(GetModuleHandleExA), S(LoadLibraryA), S(LoadLibraryW), S(LoadLibraryExA), S(LoadLibraryExW), S(FreeLibrary), S(GetProcAddress), S(GetModuleFileNameA), S(GetModuleFileNameW), S(GetSystemDirectoryA), S(GetWindowsDirectoryA),
  S(GetVersion), S(GetVersionExA), S(GetVersionExW), S(IsProcessorFeaturePresent), S(IsDebuggerPresent), S(IsDBCSLeadByte), S(IsDBCSLeadByteEx), S(GetACP), S(GetOEMCP), S(GetCPInfo), S(GetSystemInfo), S(GetNativeSystemInfo), S(GlobalMemoryStatusEx),
  S(SetUnhandledExceptionFilter), S(UnhandledExceptionFilter), S(AddVectoredExceptionHandler), S(RemoveVectoredExceptionHandler), S(RtlAddFunctionTable), S(RtlDeleteFunctionTable), S(RtlLookupFunctionEntry), S(RtlCaptureContext), S(RtlVirtualUnwind), S(RtlPcToFileHeader), S(RtlUnwindEx), S(RaiseException), S(EncodePointer), S(DecodePointer),
  S(OutputDebugStringA), S(OutputDebugStringW), S(FormatMessageA), S(FormatMessageW), S(SetErrorMode), S(GetErrorMode), S(SetThreadErrorMode), S(GetLogicalProcessorInformation), S(GetProcessAffinityMask), S(SetThreadAffinityMask), S(GetThreadPriority), S(SetThreadPriority), S(SetPriorityClass), S(GetPriorityClass), S(GetProcessTimes), S(GetThreadTimes),
  S(GetProcessHeap), S(HeapCreate), S(HeapDestroy), S(HeapAlloc), S(HeapFree), S(HeapReAlloc), S(HeapSize), S(HeapValidate), S(HeapSetInformation), S(HeapQueryInformation), S(HeapCompact), S(HeapLock), S(HeapUnlock), S(HeapWalk), S(GlobalAlloc), S(GlobalFree), S(GlobalLock), S(GlobalUnlock), S(LocalAlloc), S(LocalFree),
  S(VirtualAlloc), S(VirtualAllocEx), S(VirtualFree), S(VirtualProtect), S(VirtualLock), S(VirtualUnlock), S(VirtualQuery), S(FlushInstructionCache),
  S(TlsAlloc), S(TlsFree), S(TlsGetValue), S(TlsSetValue), S(FlsAlloc), S(FlsFree), S(FlsGetValue), S(FlsSetValue),
  S(InitializeCriticalSection), S(InitializeCriticalSectionAndSpinCount), S(InitializeCriticalSectionEx), S(DeleteCriticalSection), S(EnterCriticalSection), S(LeaveCriticalSection), S(TryEnterCriticalSection),
  S(InitializeSRWLock), S(AcquireSRWLockExclusive), S(ReleaseSRWLockExclusive), S(AcquireSRWLockShared), S(ReleaseSRWLockShared), S(TryAcquireSRWLockExclusive), S(InitializeConditionVariable), S(WakeAllConditionVariable), S(WakeConditionVariable), S(SleepConditionVariableSRW), S(SleepConditionVariableCS), S(InitOnceExecuteOnce), S(InitOnceBeginInitialize), S(InitOnceComplete),
  S(CreateEventA), S(CreateEventW), S(CreateEventExW), S(SetEvent), S(ResetEvent), S(CreateMutexA), S(CreateMutexW), S(ReleaseMutex), S(CreateSemaphoreA), S(CreateSemaphoreW), S(ReleaseSemaphore), S(WaitForSingleObject), S(WaitForSingleObjectEx), S(WaitForMultipleObjects), S(SignalObjectAndWait), S(CreateThread), S(CreateTimerQueue),
  S(GetThreadLocale), S(GetUserDefaultLCID), S(GetUserDefaultLangID), S(GetSystemDefaultLangID), S(GetUserDefaultUILanguage), S(GetLocaleInfoA), S(GetLocaleInfoW), S(GetLocaleInfoEx), S(IsValidLocale), S(IsValidCodePage), S(EnumSystemLocalesW), S(GetUserDefaultLocaleName), S(LCMapStringW), S(LCMapStringEx), S(LCMapStringA), S(CompareStringW), S(CompareStringA), S(CompareStringEx), S(GetStringTypeW), S(GetStringTypeA), S(MultiByteToWideChar), S(WideCharToMultiByte), S(CharUpperA),
  S(lstrlenA), S(lstrlenW), S(lstrcpyA), S(lstrcmpiA), S(lstrcmpA), S(InterlockedCompareExchange), S(InterlockedExchange), S(InterlockedIncrement), S(InterlockedDecrement), S(InterlockedExchangeAdd), S(QueueUserAPC),
  S(GetTimeZoneInformation), S(FileTimeToSystemTime), S(SystemTimeToFileTime), S(FileTimeToLocalFileTime), S(GetDateFormatA), S(GetTimeFormatA), S(SystemFunction036), S(GetComputerNameA), S(GetUserNameA), S(GetDriveTypeA), S(GetLogicalDrives), S(GetDiskFreeSpaceExA), S(SetHandleCount), S(GetHandleInformation), S(SetHandleInformation), S(DuplicateHandle), S(CreatePipe), S(PeekNamedPipe), S(GetOverlappedResult), S(CreateIoCompletionPort), S(Beep), S(DebugBreak), S(GetProcessId), S(K32GetProcessMemoryInfo),
  /* CRT */
  S(malloc), S(calloc), S(realloc), S(free), SA("_msize", msize), SA("_aligned_malloc", aligned_malloc), SA("_aligned_free", aligned_free), SA("_set_new_mode", set_new_mode), SA("_callnewh", ret0), SA("_query_new_handler", ret0), SA("_set_new_handler", ret0),
  SA("_initterm", initterm), SA("_initterm_e", initterm_e), S(atexit), SA("_onexit", onexit), SA("_crt_atexit", atexit), SA("_crt_at_quick_exit", atexit), SA("_register_onexit_function", register_onexit_function), SA("_initialize_onexit_table", ret0), SA("_execute_onexit_table", ret0),
  S(exit), SA("_Exit", _exit), SA("_exit", _exit), SA("_cexit", cexit), SA("_c_exit", ret0), S(abort), SA("quick_exit", _exit),
  SA("__p___argc", p___argc), SA("__p___argv", p___argv), SA("__p___wargv", p___wargv), SA("__p__acmdln", p__acmdln), SA("__p__wcmdln", p__wcmdln), SA("__p__environ", p__environ), SA("__p__wenviron", p__wenviron), SA("__p__fmode", p__fmode), SA("__p__commode", p__commode), SA("__p__pgmptr", p__pgmptr), SA("_get_initial_narrow_environment", get_initial_narrow_environment), SA("_get_initial_wide_environment", get_initial_wide_environment), SA("_errno", errno), SA("__doserrno", doserrno), SA("_get_errno", ret0),
  SA("__acrt_iob_func", acrt_iob_func), SA("__iob_func", iob_func), SA("__getmainargs", getmainargs), SA("__wgetmainargs", wgetmainargs), SA("__set_app_type", set_app_type), SA("_set_app_type", set_app_type), SA("__setusermatherr", setusermatherr), SA("_controlfp", controlfp), SA("_controlfp_s", controlfp_s), SA("_fpreset", fpreset), SA("_set_fmode", set_fmode), S(signal), S(raise), SA("_amsg_exit", abort),
  SA("_configure_narrow_argv", ret0), SA("_configure_wide_argv", ret0), SA("_initialize_narrow_environment", ret0), SA("_initialize_wide_environment", ret0), SA("_set_invalid_parameter_handler", ret0), SA("_set_thread_local_invalid_parameter_handler", ret0), SA("_seh_filter_exe", ret0), SA("_seh_filter_dll", ret0), SA("_configthreadlocale", ret0), SA("__lconv_init", ret0), SA("_setmode", ret0), SA("_set_abort_behavior", ret0), SA("_set_error_mode", ret0), SA("_set_printf_count_output", ret0), SA("_get_osfhandle", retm1), SA("_crt_debugger_hook", ret0), SA("_invalid_parameter_noinfo", invalid_parameter_noinfo), SA("_invalid_parameter_noinfo_noreturn", invalid_parameter_noinfo), SA("_beginthreadex", ret0), SA("_beginthread", retm1), SA("_endthreadex", _exit), SA("__threadid", ret0), SA("_lock", ret0), SA("_unlock", ret0), SA("__pctype_func", pctype_func), SA("___mb_cur_max_func", ret1), SA("___lc_codepage_func", ret0), SA("___lc_handle_func", ret0), SA("___lc_collate_cp_func", ret0), SA("_create_locale", ret0), SA("_free_locale", ret0), SA("_get_current_locale", ret0), SA("setlocale", ret0), SA("localeconv", ret0), SA("_setlocale", ret0),
  SA("__stdio_common_vfprintf", stdio_common_vfprintf), SA("__stdio_common_vfprintf_s", stdio_common_vfprintf), SA("__stdio_common_vfwprintf", stdio_common_vfwprintf), SA("__stdio_common_vsprintf", stdio_common_vsprintf), SA("__stdio_common_vsprintf_s", stdio_common_vsprintf_s), SA("__stdio_common_vsnprintf_s", stdio_common_vsnprintf_s), SA("__stdio_common_vswprintf", stdio_common_vswprintf), SA("__stdio_common_vswprintf_s", stdio_common_vswprintf_s), SA("__stdio_common_vsprintf_p", stdio_common_vsprintf),
  S(printf), S(wprintf), S(vprintf), S(fprintf), S(vfprintf), S(sprintf), SA("_snprintf", snprintf), S(snprintf), SA("_vsnprintf", vsnprintf), S(vsnprintf), S(vsprintf), SA("__mingw_vfprintf", vfprintf), SA("__mingw_vprintf", vprintf), SA("__mingw_vsprintf", vsprintf), SA("__mingw_vsnprintf", vsnprintf), SA("__mingw_printf", printf), SA("__mingw_fprintf", fprintf), SA("__mingw_sprintf", sprintf), SA("__mingw_snprintf", snprintf), SA("__ms_vsnprintf", vsnprintf),
  S(puts), SA("_putws", putws), S(fputs), S(putchar), S(fputc), SA("putc", fputc), S(fwrite), S(fflush), S(setvbuf), S(fopen), S(fopen_s), S(fclose), S(fgets), S(fread), S(getchar), S(fgetc), S(getc), S(feof), S(ferror), SA("_fileno", fileno), SA("_isatty", isatty), SA("_write", write), SA("_read", read), SA("_open", open), SA("_close", close), SA("_fdopen", ret0), SA("_wfopen", ret0), SA("_lseek", retm1), SA("_lseeki64", retm1), SA("_stat64", retm1), SA("_fstat64", retm1), SA("_wstat64", retm1), SA("_access", retm1), SA("_chsize", retm1), SA("_unlink", retm1), SA("remove", retm1), SA("rename", retm1), SA("_mkdir", retm1), SA("_getcwd", ret0), SA("_dup", retm1), SA("_dup2", retm1), SA("_pipe", retm1), SA("_commit", ret0), SA("_locking", ret0), SA("_filelengthi64", retm1), SA("_get_stream_buffer_pointers", ret0), SA("ungetc", retm1),
  SA("_time64", time64), SA("time", time64), S(clock), SA("_localtime64", localtime64), SA("_gmtime64", gmtime64), SA("_localtime64_s", localtime64_s), SA("_mktime64", mktime64), S(strftime), SA("_tzset", tzset), SA("__timezone", timezone), SA("__daylight", daylight), SA("__tzname", tzname), SA("_get_timezone", ret0), SA("_ftime64", ret0), SA("_difftime64", ret0),
  S(getenv), S(getenv_s), SA("_dupenv_s", ret0), SA("_putenv", ret0), S(system), S(rand), S(srand), SA("rand_s", ret0),
  S(strlen), S(wcslen), S(strnlen), S(strcmp), SA("_stricmp", stricmp), SA("_strcmpi", stricmp), SA("stricmp", stricmp), S(strncmp), SA("_strnicmp", strnicmp), S(wcscmp), S(strcpy), S(strncpy), S(strcat), S(strchr), S(strrchr), S(strstr), SA("_strdup", strdup), S(strdup), S(strtol), S(strtoul), S(strtoll), SA("_strtoi64", strtoll), SA("_strtoui64", strtoll), SA("strtoull", strtoll), S(atoi), S(atol), S(atoll), SA("_atoi64", atoll), S(strtod), S(atof), S(toupper), S(tolower), S(isdigit), S(isalpha), S(isalnum), S(isspace), S(isupper), S(islower), S(isprint), S(isxdigit), S(ispunct), SA("iscntrl", ret0), SA("isgraph", isprint), SA("iswspace", isspace), SA("iswdigit", isdigit), SA("iswalpha", isalpha), SA("towupper", toupper), SA("towlower", tolower), SA("_isctype", ret0), SA("strerror", ret0), SA("_strerror", ret0), SA("strerror_s", ret0), SA("strcpy_s", ret0), SA("strcat_s", ret0), SA("strncpy_s", ret0), SA("memcpy_s", ret0), SA("memmove_s", ret0), SA("strtok", ret0), SA("strtok_s", ret0), SA("strspn", ret0), SA("strcspn", ret0), SA("strpbrk", ret0), SA("_strlwr", ret0), SA("_strupr", ret0), SA("_strrev", ret0), SA("strcoll", strcmp), SA("_itoa", ret0), SA("_ltoa", ret0), SA("_ultoa", ret0), SA("_i64toa", ret0), SA("_ui64toa", ret0),
  S(memcpy), SA("memmove", memcpy), S(memset), S(memcmp), S(memchr), S(qsort), SA("qsort_s", bsearch), S(bsearch), SA("_setjmp", setjmp), SA("_setjmpex", setjmp), S(longjmp), S(div), SA("ldiv", div), S(abs), S(labs), SA("_abs64", llabs), S(llabs),
  S(sin), S(cos), S(tan), S(exp), S(log), S(pow), S(atan2), S(asin), S(acos), S(atan), S(sinh), S(cosh), S(tanh), S(log10), S(log2), S(cbrt), S(sqrt), S(floor), S(ceil), S(fabs), S(fmod), S(round), S(trunc), S(ldexp), S(frexp), S(modf), S(hypot), SA("_hypot", hypot), S(sinf), S(cosf), S(tanf), S(expf), S(logf), S(powf), S(atan2f), S(sqrtf), S(floorf), S(ceilf), S(fabsf), S(fmodf), S(roundf), SA("_dclass", dclass), SA("_fdclass", fdclass), SA("_dsign", dsign), SA("_fdsign", dsign), SA("_dtest", dclass), SA("_fdtest", fdclass), SA("_finite", ret1), SA("_isnan", ret0), SA("_copysign", copysign), S(copysign), S(exp2), SA("nearbyint", round), SA("rint", round), S(lround), SA("llround", lround), S(fmin), S(fmax),
  SA("__C_specific_handler", ret1), SA("__CxxFrameHandler3", ret1), SA("__CxxFrameHandler4", ret1), SA("_CxxThrowException", raise), SA("__std_exception_copy", ret0), SA("__std_exception_destroy", ret0), SA("__std_type_info_destroy_list", ret0), SA("__current_exception", ret0), SA("__current_exception_context", ret0), SA("_except_handler4_common", ret1), SA("__intrinsic_setjmp", setjmp), SA("__report_gsfailure", abort), SA("_purecall", abort), SA("terminate", abort), SA("__p__mbctype", pctype_func), SA("_ismbblead", ret0), SA("__mb_cur_max", ret1),
};
static ShimFn find_shim(const char *dll, const char *name) { (void)dll; for (size_t i = 0; i < sizeof shims / sizeof shims[0]; i++) if (!e_strcmp(shims[i].name, name)) return shims[i].fn; extern ShimFn gdi_find_shim(const char *name); return gdi_find_shim(name); }
int w32_have_shim(const char *dll, const char *name) { return find_shim(dll, name) != 0; }

/* ---------- init ---------- */
void w32_init(Win32 *w, Cpu *c, Mem *m) {
  e_memset(w, 0, sizeof *w); w->cpu = c; w->mem = m; c->w = w; c->mem = m;
  w->start_ms = host_now_ms(); w->rand_state = 1; w->next_handle = 0x100;
  u8 *shim = mem_map(m, G_SHIM_BASE, G_SHIM_SIZE, "shim"); e_memset(shim, 0xCC, 0x10000);
  mem_map(m, G_STACK_BASE, G_STACK_SIZE, "stack");
  w->heap_cur = G_HEAP_BASE; w->heap_end = G_HEAP_BASE; heap_grow(w, 0);
  w->fb_cur = G_FB_BASE;
  g_str_cur = G_SHIM_BASE + STR_OFF;
  w->teb = G_SHIM_BASE + TEB_OFF; w->peb = G_SHIM_BASE + PEB_OFF;
  c->gs_base = w->teb; c->fs_base = w->teb;
  wr64(c, w->teb + 0x08, G_STACK_BASE + G_STACK_SIZE); wr64(c, w->teb + 0x10, G_STACK_BASE); wr64(c, w->teb + 0x30, w->teb); wr64(c, w->teb + 0x40, 4242); wr64(c, w->teb + 0x48, 4243); wr64(c, w->teb + 0x60, w->peb);
  wr64(c, w->teb + 0x58, w32_heap_alloc(w, 64, 1));
  u64 params = G_SHIM_BASE + PARAMS_OFF; wr64(c, w->peb + 0x20, params); wr32(c, w->peb + 0x118, 10); wr32(c, w->peb + 0x11c, 0); wr16(c, w->peb + 0x120, 19045); wr64(c, w->peb + 0x18, shim_bump(w, 0x100));
  wr8(c, w->peb + 0x02, 0); /* BeingDebugged */
  w->ubuf = shim_bump(w, 0x1000);
  /* FILE structures: 3 × 0x60; mark fileno at +0x30?  UCRT layout is opaque; keep zero */
  wr32(c, w->ubuf + 0x240, 0x4000); wr32(c, w->ubuf + 0x248, 0);
  /* ctype table at ubuf+0x400: 256 shorts (pctype returns pointer to table) */
  for (int i = 0; i < 256; i++) { u16 t = 0; if (i >= 'A' && i <= 'Z') t |= 0x101; if (i >= 'a' && i <= 'z') t |= 0x102; if (i >= '0' && i <= '9') t |= 0x204; if (i == ' ' || (i >= 9 && i <= 13)) t |= 0x48; if (i < 32 || i == 127) t |= 0x20; if ((i >= 33 && i <= 47) || (i >= 58 && i <= 64) || (i >= 91 && i <= 96) || (i >= 123 && i <= 126)) t |= 0x10; wr16(c, w->ubuf + 0x400 + i * 2, t); }
  /* fixed slots */
  w->nslots = SLOT_FIRST; w->slots[SLOT_STARTUP].name = "__nebula_startup"; w->slots[SLOT_STARTUP].dll = "nebula"; w->slots[SLOT_CONT].name = "__nebula_continue"; w->slots[SLOT_CONT].dll = "nebula";
  w->screen_w = 800; w->screen_h = 600;
  extern void gdi_init(Win32 *w); gdi_init(w);
}

/* set up command line, argv, environment, stack and the start-up continuation (called by api.c after pe_load) */
void w32_start(Win32 *w, const char *cmdline) {
  Cpu *c = w->cpu;
  e_snprintf(w->cmdline, sizeof w->cmdline, "%s", cmdline);
  w->cmdline_a = w32_guest_strdup(w, w->cmdline); w->cmdline_w = guest_wstrdup(w, w->cmdline);
  /* argv split (quotes aware) */
  u64 argv = w32_heap_alloc(w, 8 * 64, 1); u64 wargv = w32_heap_alloc(w, 8 * 64, 1); int argc = 0;
  const char *p = w->cmdline; char tok[1024];
  while (*p && argc < 63) {
    while (*p == ' ') p++; if (!*p) break;
    int tl = 0, q = 0; while (*p && (q || *p != ' ')) { if (*p == '"') { q = !q; p++; continue; } if (tl < 1023) tok[tl++] = *p; p++; } tok[tl] = 0;
    wr64(c, argv + argc * 8, w32_guest_strdup(w, tok)); wr64(c, wargv + argc * 8, guest_wstrdup(w, tok)); argc++;
  }
  wr32(c, w->ubuf + 0x200, (u32)argc); wr64(c, w->ubuf + 0x208, argv); wr64(c, w->ubuf + 0x210, w->cmdline_a); wr64(c, w->ubuf + 0x218, wargv); wr64(c, w->ubuf + 0x220, w->cmdline_w);
  static const char *envs[] = { "PATH=C:\\Windows\\System32", "SYSTEMROOT=C:\\Windows", "TEMP=C:\\tmp", "TMP=C:\\tmp", "USERPROFILE=C:\\Users\\user", "HOMEPATH=\\Users\\user", "USERNAME=user", "COMPUTERNAME=NEBULA", "OS=Windows_NT", "NEBULA_X64=1", "PROCESSOR_ARCHITECTURE=AMD64", "NUMBER_OF_PROCESSORS=1", 0 };
  u64 envp = w32_heap_alloc(w, 8 * 16, 1); size_t total = 1; for (int i = 0; envs[i]; i++) total += e_strlen(envs[i]) + 1;
  u64 blk = w32_heap_alloc(w, total, 1), wblk = w32_heap_alloc(w, total * 2, 1); u64 o = 0;
  for (int i = 0; envs[i]; i++) { size_t l = e_strlen(envs[i]) + 1; u8 *d = mem_ptr(w->mem, blk + o, l, 1); if (d) e_memcpy(d, envs[i], l); for (size_t k = 0; k < l; k++) wr16(c, wblk + (o + k) * 2, (u8)envs[i][k]); wr64(c, envp + i * 8, blk + o); o += l; }
  w->env_a = blk; w->env_w = wblk; wr64(c, w->ubuf + 0x228, envp); wr64(c, w->ubuf + 0x230, 0);
  wr64(c, w->ubuf + 0x250, w32_guest_strdup(w, "C:\\nebula\\app.exe"));
  /* ProcessParameters: CommandLine UNICODE_STRING at +0x70, ImagePathName at +0x60 */
  u64 params = G_SHIM_BASE + PARAMS_OFF; size_t cl = e_strlen(w->cmdline);
  wr16(c, params + 0x70, (u16)(cl * 2)); wr16(c, params + 0x72, (u16)(cl * 2 + 2)); wr64(c, params + 0x78, w->cmdline_w);
  u64 img = guest_wstrdup(w, "C:\\nebula\\app.exe"); wr16(c, params + 0x60, 34); wr16(c, params + 0x62, 36); wr64(c, params + 0x68, img); wr64(c, params + 0x80, wblk);
  /* stack: top minus red zone; the start-up slot acts as the return address of "the OS" */
  c->r[RSP] = G_STACK_BASE + G_STACK_SIZE - 0x100;
  c->rip = SLOT_ADDR(SLOT_STARTUP); cpu_push(c, 0);   /* fake return address popped by dispatch */
}
