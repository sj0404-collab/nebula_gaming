/* user32 + gdi32 personality: one top-level window, message queue, software GDI on a BGRA framebuffer. */
#include "emu.h"
#include "font8x8_basic.h"

#define ARG(n) w32_arg(c, n)
#define RET(v) (c->r[RAX] = (u64)(v))
#define W (c->w)
#define HWND_MAIN 0x10042ull
#define GDI_TAG   0x01000000ull
#define CONT_RET_RAX 4
#define CONT_RET_VAL 5
#define CONT_CREATEWND 6
#define CONT_DESTROYWND 100
#define SLOT_CONT_ADDR (G_SHIM_BASE + 16)

int w32_format_args(Cpu *c, char *out, size_t n, u64 fmt, int wide, int first_arg);
void w32_retry_yield(Cpu *c);
void w32_push_cont(Cpu *c, int kind, u64 val);
void w32_guest_call(Cpu *c, u64 fn, u64 a0, u64 a1, u64 a2, u64 a3);
void w32_cont_finish(Cpu *c, u64 rax);

/* ---------- GDI objects ---------- */
static GdiObj *gdi_new(Win32 *w, int kind) {
  for (int i = 1; i < MAX_GDI; i++) if (!w->gdi[i].kind) { GdiObj *o = &w->gdi[i]; e_memset(o, 0, sizeof *o); o->kind = kind; o->handle = GDI_TAG | (u64)i; if (i >= w->ngdi) w->ngdi = i + 1; return o; }
  return 0;
}
static GdiObj *gdi_get(Win32 *w, u64 h) { if ((h & ~0xffffull) != GDI_TAG) return 0; u64 i = h & 0xffff; if (i >= MAX_GDI || !w->gdi[i].kind) return 0; return &w->gdi[i]; }
enum { K_DC = 1, K_BITMAP, K_BRUSH, K_PEN, K_FONT, K_RGN };
static u64 stock[20];
static u64 window_dc;

static u32 cr2px(u32 cr) { return ((cr & 0xff) << 16) | (cr & 0xff00) | ((cr >> 16) & 0xff); }
static u32 px2cr(u32 px) { return ((px & 0xff) << 16) | (px & 0xff00) | ((px >> 16) & 0xff); }

typedef struct { u32 *px; int w, h; int bottom_up; } Surf;
static inline u32 *srow(Surf *s, int y) { return s->bottom_up ? s->px + (size_t)(s->h - 1 - y) * s->w : s->px + (size_t)y * s->w; }
static int dc_surf(Cpu *c, u64 hdc, Surf *s) {
  Win32 *w = W; GdiObj *dc = gdi_get(w, hdc); if (!dc || dc->kind != K_DC) return 0;
  if (hdc == window_dc) { s->px = w->screen; s->w = w->screen_w; s->h = w->screen_h; s->bottom_up = 0; return s->px != 0; }
  GdiObj *bm = gdi_get(w, dc->bitmap); if (!bm || bm->kind != K_BITMAP) return 0;
  s->px = (u32 *)mem_ptr(w->mem, bm->bits, (u64)bm->bw * bm->bh * 4, 1); s->w = bm->bw; s->h = bm->bh; s->bottom_up = bm->font_bold; /* font_bold reused as bottom-up flag for bitmaps */
  return s->px != 0;
}
static void mark_screen(Win32 *w, u64 hdc) { if (hdc == window_dc) { w->dirty = 1; w->blits_since_yield++; } }
static u32 dc_brush_px(Win32 *w, GdiObj *dc, int *null_brush) { GdiObj *b = gdi_get(w, dc->brush); *null_brush = 0; if (!b) return 0xffffff; if (b->kind == K_BRUSH && b->pen_width == -1) { *null_brush = 1; return 0; } return b->color; }
static u32 dc_pen_px(Win32 *w, GdiObj *dc, int *null_pen) { GdiObj *p = gdi_get(w, dc->pen); *null_pen = 0; if (!p) return 0; if (p->pen_width == -1) { *null_pen = 1; return 0; } return p->color; }

static void fill_rect_px(Surf *s, int x0, int y0, int x1, int y1, u32 px) {
  if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > s->w) x1 = s->w; if (y1 > s->h) y1 = s->h;
  for (int y = y0; y < y1; y++) { u32 *r = srow(s, y); for (int x = x0; x < x1; x++) r[x] = px; }
}
static void hline(Surf *s, int x0, int x1, int y, u32 px) { if (y < 0 || y >= s->h) return; if (x0 > x1) { int t = x0; x0 = x1; x1 = t; } if (x0 < 0) x0 = 0; if (x1 >= s->w) x1 = s->w - 1; u32 *r = srow(s, y); for (int x = x0; x <= x1; x++) r[x] = px; }
static void put_px(Surf *s, int x, int y, u32 px) { if (x < 0 || y < 0 || x >= s->w || y >= s->h) return; srow(s, y)[x] = px; }
static void line_px(Surf *s, int x0, int y0, int x1, int y1, u32 px) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
  for (int n = 0; n < 100000; n++) { put_px(s, x0, y0, px); if (x0 == x1 && y0 == y1) break; int e2 = 2 * err; if (e2 > -dy) { err -= dy; x0 += sx; } if (e2 < dx) { err += dx; y0 += sy; } }
}
/* scanline polygon fill (even-odd) */
static void fill_poly(Surf *s, const s32 *pts, int n, u32 px) {
  if (n < 3) return; int ymin = pts[1], ymax = pts[1];
  for (int i = 1; i < n; i++) { if (pts[i * 2 + 1] < ymin) ymin = pts[i * 2 + 1]; if (pts[i * 2 + 1] > ymax) ymax = pts[i * 2 + 1]; }
  if (ymin < 0) ymin = 0; if (ymax > s->h) ymax = s->h;
  for (int y = ymin; y < ymax; y++) {
    float yc = (float)y + 0.5f; float xs[64]; int nx = 0;
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; float x0 = (float)pts[i * 2], y0 = (float)pts[i * 2 + 1], x1 = (float)pts[j * 2], y1 = (float)pts[j * 2 + 1];
      if ((y0 <= yc && y1 > yc) || (y1 <= yc && y0 > yc)) { float x = x0 + (yc - y0) * (x1 - x0) / (y1 - y0); if (nx < 64) xs[nx++] = x; } }
    for (int i = 1; i < nx; i++) { float v = xs[i]; int j = i - 1; while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; } xs[j + 1] = v; }
    for (int i = 0; i + 1 < nx; i += 2) { int xa = (int)(xs[i] + 0.5f), xb = (int)(xs[i + 1] + 0.5f); if (xb > xa) hline(s, xa, xb - 1, y, px); }
  }
}
static void fill_ellipse(Surf *s, int l, int t, int r, int b, u32 px, int fill, u32 pen, int outline) {
  int w = r - l, h = b - t; if (w <= 0 || h <= 0) return; float cx = (l + r) * 0.5f, cy = (t + b) * 0.5f, rx = w * 0.5f, ry = h * 0.5f;
  for (int y = t; y < b; y++) { float dy = (y + 0.5f - cy) / ry; float v = 1 - dy * dy; if (v < 0) continue; float hw = rx * e_sqrt(v); int xa = (int)(cx - hw + 0.5f), xb = (int)(cx + hw - 0.5f);
    if (fill) hline(s, xa, xb, y, px); if (outline) { put_px(s, xa, y, pen); put_px(s, xb, y, pen); if (y == t || y == b - 1) hline(s, xa, xb, y, pen); } }
}
static void draw_text(Surf *s, GdiObj *dc, Win32 *w, int x, int y, const char *str, int len) {
  GdiObj *f = gdi_get(w, dc->font); int fh = f && f->kind == K_FONT ? f->font_h : 16; int bold = f && f->kind == K_FONT ? f->font_bold : 0;
  if (fh < 4) fh = 4; int cw = (fh * 11 + 10) / 20; if (cw < 3) cw = 3;
  u32 fg = cr2px(dc->text_color), bg = cr2px(dc->bk_color); int opaque = dc->bk_mode != 1;
  if (opaque) fill_rect_px(s, x, y, x + cw * len, y + fh, bg);
  for (int i = 0; i < len; i++) { u8 ch = (u8)str[i]; if (ch >= 128) ch = '?'; const unsigned char *g = font8x8_basic[ch]; int gx0 = x + i * cw;
    for (int py = 0; py < fh; py++) { int gy = py * 8 / fh; int yy = y + py; if (yy < 0 || yy >= s->h) continue; u32 *row = srow(s, yy);
      for (int px = 0; px < cw; px++) { int gx = px * 8 / cw; int on = (g[gy] >> gx) & 1; if (!on && bold && gx > 0) on = (g[gy] >> (gx - 1)) & 1; if (on) { int xx = gx0 + px; if (xx >= 0 && xx < s->w) row[xx] = fg; } } } }
}

/* ---------- messages ---------- */
static void post_msg(Win32 *w, u32 msg, u64 wp, u64 lp) {
  int next = (w->msg_tail + 1) % MAX_MSGS; if (next == w->msg_head) return;
  GuestMsg *m = &w->msgs[w->msg_tail]; m->hwnd = HWND_MAIN; m->msg = msg; m->wparam = wp; m->lparam = lp; m->time = (u32)(host_now_ms() - w->start_ms); w->msg_tail = next;
}
void w32_post_msg(Win32 *w, u32 msg, u64 wp, u64 lp) { post_msg(w, msg, wp, lp); }
void w32_key(Win32 *w, int vk, int down) {
  if (vk < 0 || vk > 255) return; int prev = w->key_state[vk]; w->key_state[vk] = down;
  u64 lp = 1 | ((u64)(vk & 0x7f) << 16) | (prev ? 1ull << 30 : 0) | (down ? 0 : (1ull << 31) | (1ull << 30));
  int sys = (vk == 0x12) || (w->key_state[0x12] && vk != 0x12 && vk != 0x11);
  post_msg(w, down ? (sys ? 0x104 : 0x100) : (sys ? 0x105 : 0x101), (u64)vk, lp);
}
static int g_mouse_x, g_mouse_y, g_mouse_btn;
void w32_mouse(Win32 *w, int x, int y, int ev) {
  g_mouse_x = x; g_mouse_y = y;
  if (ev == 1) g_mouse_btn |= 1; else if (ev == 2) g_mouse_btn &= ~1; else if (ev == 3) g_mouse_btn |= 2; else if (ev == 4) g_mouse_btn &= ~2;
  u64 lp = (u64)(u16)x | ((u64)(u16)y << 16);
  static const u32 msgs[] = { 0x200, 0x201, 0x202, 0x204, 0x205 };
  post_msg(w, msgs[ev < 5 ? ev : 0], (u64)g_mouse_btn, lp);
}
void w32_tick(Win32 *w) { (void)w; }
static int timers_n; static struct { u64 id; u32 ms; double next; u64 proc; } timers[16];
static void check_timers(Win32 *w) {
  double now = host_now_ms();
  for (int i = 0; i < timers_n; i++) if (now >= timers[i].next) { timers[i].next = now + timers[i].ms; post_msg(w, 0x113, timers[i].id, timers[i].proc); }
}
static void write_msg(Cpu *c, u64 a, GuestMsg *m) { wr64(c, a, m->hwnd); wr32(c, a + 8, m->msg); wr32(c, a + 12, 0); wr64(c, a + 16, m->wparam); wr64(c, a + 24, m->lparam); wr32(c, a + 32, m->time); wr32(c, a + 36, (u32)g_mouse_x); wr32(c, a + 40, (u32)g_mouse_y); wr32(c, a + 44, 0); }
static int next_message(Cpu *c, GuestMsg *out, int remove) {
  Win32 *w = W; check_timers(w);
  if (w->msg_head != w->msg_tail) { *out = w->msgs[w->msg_head]; if (remove) w->msg_head = (w->msg_head + 1) % MAX_MSGS; return 1; }
  if (w->quit_posted) { out->hwnd = 0; out->msg = 0x12; out->wparam = (u64)(u32)c->exit_code; out->lparam = 0; out->time = 0; if (remove) w->quit_posted = 0; return 1; }
  if (w->win.created && w->win.style & 0x40000000u) { out->hwnd = HWND_MAIN; out->msg = 0xF; out->wparam = out->lparam = 0; out->time = 0; return 1; }   /* 0x40000000 = our "invalid" flag */
  return 0;
}
static void s_PeekMessageA(Cpu *c) {
  Win32 *w = W; GuestMsg m; u32 flags = (u32)ARG(4);
  if (next_message(c, &m, flags & 1)) { write_msg(c, ARG(0), &m); RET(1); return; }
  if (w->blits_since_yield || w->yield_frames++ > 20000) { w->blits_since_yield = 0; w->yield_frames = 0; w32_retry_yield(c); return; }
  RET(0);
}
static void s_GetMessageA(Cpu *c) {
  GuestMsg m; if (next_message(c, &m, 1)) { write_msg(c, ARG(0), &m); RET(m.msg == 0x12 ? 0 : 1); return; }
  W->blits_since_yield = 0; w32_retry_yield(c);
}
static void s_TranslateMessage(Cpu *c) {
  u64 a = ARG(0); u32 msg = rd32(c, a + 8); u64 vk = rd64(c, a + 16);
  if (msg == 0x100) { int shift = W->key_state[0x10]; u32 ch = 0;
    if (vk >= 'A' && vk <= 'Z') ch = shift ? (u32)vk : (u32)vk + 32; else if (vk >= '0' && vk <= '9') ch = (u32)vk; else if (vk == 0x20 || vk == 0x0D || vk == 0x08 || vk == 0x1B || vk == 0x09) ch = (u32)vk; else if (vk >= 0x60 && vk <= 0x69) ch = '0' + (u32)vk - 0x60;
    if (ch) post_msg(W, 0x102, ch, rd64(c, a + 24)); RET(1); return; }
  RET(0);
}
static void dispatch_to_wndproc(Cpu *c, u64 hwnd, u32 msg, u64 wp, u64 lp) {
  Win32 *w = W;
  if (msg == 0x113 && lp) { w32_push_cont(c, CONT_RET_RAX, 0); w32_guest_call(c, lp, hwnd, msg, wp, (u64)(u32)(host_now_ms() - w->start_ms)); return; }
  if (hwnd != HWND_MAIN || !w->win.wndproc) { RET(0); return; }
  w32_push_cont(c, CONT_RET_RAX, 0); w32_guest_call(c, w->win.wndproc, hwnd, msg, wp, lp);
}
static void s_DispatchMessageA(Cpu *c) { u64 a = ARG(0); dispatch_to_wndproc(c, rd64(c, a), rd32(c, a + 8), rd64(c, a + 16), rd64(c, a + 24)); }
static void s_SendMessageA(Cpu *c) { dispatch_to_wndproc(c, ARG(0), (u32)ARG(1), ARG(2), ARG(3)); }
static void s_PostMessageA(Cpu *c) { if (ARG(0) == HWND_MAIN || ARG(0) == 0) post_msg(W, (u32)ARG(1), ARG(2), ARG(3)); RET(1); }
static void s_PostQuitMessage(Cpu *c) { W->quit_posted = 1; c->exit_code = (int)ARG(0); }
static void s_SetTimer(Cpu *c) { u64 id = ARG(1) ? ARG(1) : 1; u32 ms = (u32)ARG(2); if (ms < 10) ms = 10; for (int i = 0; i < timers_n; i++) if (timers[i].id == id) { timers[i].ms = ms; timers[i].next = host_now_ms() + ms; timers[i].proc = ARG(3); RET(id); return; } if (timers_n < 16) { timers[timers_n].id = id; timers[timers_n].ms = ms; timers[timers_n].next = host_now_ms() + ms; timers[timers_n].proc = ARG(3); timers_n++; } RET(id); }
static void s_KillTimer(Cpu *c) { for (int i = 0; i < timers_n; i++) if (timers[i].id == ARG(1)) { timers[i] = timers[--timers_n]; break; } RET(1); }
static void s_MsgWaitForMultipleObjects(Cpu *c) { Win32 *w = W; if (w->msg_head != w->msg_tail || w->quit_posted) { RET(ARG(0)); return; } w->blits_since_yield = 0; w32_retry_yield(c); }
static void s_WaitMessage(Cpu *c) { s_MsgWaitForMultipleObjects(c); }

/* ---------- window ---------- */
static void s_RegisterClassA(Cpu *c) { Win32 *w = W; u64 a = ARG(0); w->wndclass_proc = rd64(c, a + 8); w->wndclass_registered = 1; const char *n = rd_str(c, rd64(c, a + 64), 63); e_snprintf(w->wndclass_name, sizeof w->wndclass_name, "%s", n ? n : "?"); w->win.brush = rd64(c, a + 48); RET(0xC001); }
static void s_RegisterClassExA(Cpu *c) { Win32 *w = W; u64 a = ARG(0); w->wndclass_proc = rd64(c, a + 16); w->wndclass_registered = 1; const char *n = rd_str(c, rd64(c, a + 72), 63); e_snprintf(w->wndclass_name, sizeof w->wndclass_name, "%s", n ? n : "?"); w->win.brush = rd64(c, a + 56); RET(0xC001); }
static void s_RegisterClassW(Cpu *c) { s_RegisterClassA(c); }
static void s_RegisterClassExW(Cpu *c) { s_RegisterClassExA(c); }
static void s_UnregisterClassA(Cpu *c) { RET(1); }
static void s_GetClassInfoExA(Cpu *c) { RET(0); }
static void s_AdjustWindowRectEx(Cpu *c) { u64 r = ARG(0); u32 style = (u32)ARG(1); if (style & 0x00C00000u) { wr32(c, r, rd32(c, r) - 8); wr32(c, r + 4, rd32(c, r + 4) - 31); wr32(c, r + 8, rd32(c, r + 8) + 8); wr32(c, r + 12, rd32(c, r + 12) + 8); } RET(1); }
static void s_AdjustWindowRect(Cpu *c) { s_AdjustWindowRectEx(c); }
static void create_window(Cpu *c, u64 title_a, int wide, u32 style, int x, int y, int cx, int cy, u64 param) {
  Win32 *w = W; (void)x; (void)y;
  if (w->win.created) { w32_report(w, "second top-level window requested — only one window is supported; returning the same HWND"); RET(HWND_MAIN); return; }
  if (!w->wndclass_registered) { w32_report(w, "CreateWindow before RegisterClass"); RET(0); return; }
  int cw = cx, ch = cy; if ((u32)cx == 0x80000000u || cx <= 0) cw = 800; if ((u32)cy == 0x80000000u || cy <= 0) ch = 600;
  if (style & 0x00C00000u) { cw -= 16; ch -= 39; } if (cw < 16) cw = 16; if (ch < 16) ch = 16; if (cw > 4096) cw = 4096; if (ch > 4096) ch = 4096;
  w->win.hwnd = HWND_MAIN; w->win.w = cw; w->win.h = ch; w->win.style = style & ~0x40000000u; w->win.wndproc = w->wndclass_proc; w->win.created = 1; w->win.shown = 0;
  if (wide) { char t[128]; int n = 0; for (int i = 0; i < 127; i++) { u16 chh = rd16(c, title_a + i * 2); if (!chh) break; t[n++] = chh < 128 ? (char)chh : '?'; } t[n] = 0; e_snprintf(w->win.title, sizeof w->win.title, "%s", t); }
  else { const char *t = rd_str(c, title_a, 127); e_snprintf(w->win.title, sizeof w->win.title, "%s", t ? t : ""); }
  w->screen_w = cw; w->screen_h = ch; w->screen = emu_alloc((size_t)cw * ch * 4); w->dirty = 1;
  /* CREATESTRUCT for WM_CREATE */
  u64 cs = w32_heap_alloc(w, 80, 1); wr64(c, cs, param); wr64(c, cs + 8, w->image_base); wr64(c, cs + 32, HWND_MAIN); wr32(c, cs + 40, ch); wr32(c, cs + 44, cw); wr32(c, cs + 48, 64); wr32(c, cs + 52, 64); wr32(c, cs + 56, style); wr64(c, cs + 64, title_a);
  w32_push_cont(c, CONT_CREATEWND, 0);
  w32_guest_call(c, w->win.wndproc, HWND_MAIN, 0x0001 /* WM_CREATE */, 0, cs);
}
static void s_CreateWindowExA(Cpu *c) { create_window(c, ARG(2), 0, (u32)ARG(3), (int)ARG(4), (int)ARG(5), (int)ARG(6), (int)ARG(7), ARG(11)); }
static void s_CreateWindowExW(Cpu *c) { create_window(c, ARG(2), 1, (u32)ARG(3), (int)ARG(4), (int)ARG(5), (int)ARG(6), (int)ARG(7), ARG(11)); }
static void s_DefWindowProcA(Cpu *c) {
  Win32 *w = W; u32 msg = (u32)ARG(1);
  switch (msg) {
    case 0x81: RET(1); return;                                           /* WM_NCCREATE */
    case 0x0F: w->win.style &= ~0x40000000u; RET(0); return;             /* WM_PAINT: validate */
    case 0x14: { Surf s; if (dc_surf(c, window_dc, &s)) { GdiObj *b = gdi_get(w, w->win.brush); fill_rect_px(&s, 0, 0, s.w, s.h, b ? b->color : 0); mark_screen(w, window_dc); } RET(1); return; } /* WM_ERASEBKGND */
    case 0x10: { w32_push_cont(c, CONT_DESTROYWND, 0); w32_guest_call(c, w->win.wndproc, HWND_MAIN, 0x0002, 0, 0); return; }   /* WM_CLOSE → WM_DESTROY */
    default: RET(0); return;
  }
}
static void s_DefWindowProcW(Cpu *c) { s_DefWindowProcA(c); }
static void s_DestroyWindow(Cpu *c) { Win32 *w = W; if (!w->win.created) { RET(0); return; } w32_push_cont(c, CONT_DESTROYWND, 0); w32_guest_call(c, w->win.wndproc, HWND_MAIN, 0x0002, 0, 0); }
void gdi_cont_resume(Cpu *c, int kind, u64 rax) { (void)rax; if (kind == CONT_DESTROYWND) { W->win.created = 0; W->win.style = 0; w32_cont_finish(c, 1); return; } cpu_fault(c, "bad continuation kind %d", kind); }
static void s_ShowWindow(Cpu *c) { Win32 *w = W; int prev = w->win.shown; w->win.shown = ARG(1) != 0; if (w->win.shown && !prev) { w->win.style |= 0x40000000u; w->dirty = 1; } RET(prev); }
static void s_UpdateWindow(Cpu *c) { Win32 *w = W; if (w->win.created && (w->win.style & 0x40000000u)) { w->win.style &= ~0x40000000u; w32_push_cont(c, CONT_RET_VAL, 1); w32_guest_call(c, w->win.wndproc, HWND_MAIN, 0x0F, 0, 0); return; } RET(1); }
static void s_InvalidateRect(Cpu *c) { W->win.style |= 0x40000000u; RET(1); }
static void s_InvalidateRgn(Cpu *c) { W->win.style |= 0x40000000u; RET(1); }
static void s_ValidateRect(Cpu *c) { W->win.style &= ~0x40000000u; RET(1); }
static void s_RedrawWindow(Cpu *c) { W->win.style |= 0x40000000u; RET(1); }
static void s_GetClientRect(Cpu *c) { u64 r = ARG(1); wr32(c, r, 0); wr32(c, r + 4, 0); wr32(c, r + 8, (u32)W->win.w); wr32(c, r + 12, (u32)W->win.h); RET(1); }
static void s_GetWindowRect(Cpu *c) { u64 r = ARG(1); wr32(c, r, 64); wr32(c, r + 4, 64); wr32(c, r + 8, 64 + (u32)W->win.w); wr32(c, r + 12, 64 + (u32)W->win.h); RET(1); }
static void s_BeginPaint(Cpu *c) { Win32 *w = W; u64 ps = ARG(1); w->win.style &= ~0x40000000u; wr64(c, ps, window_dc); wr32(c, ps + 8, 0); wr32(c, ps + 12, 0); wr32(c, ps + 16, 0); wr32(c, ps + 20, (u32)w->win.w); wr32(c, ps + 24, (u32)w->win.h); RET(window_dc); }
static void s_EndPaint(Cpu *c) { W->dirty = 1; W->blits_since_yield++; RET(1); }
static void s_GetDC(Cpu *c) { RET(window_dc); }
static void s_GetWindowDC(Cpu *c) { RET(window_dc); }
static void s_ReleaseDC(Cpu *c) { W->dirty = 1; RET(1); }
static void s_SetWindowTextA(Cpu *c) { const char *t = rd_str(c, ARG(1), 127); e_snprintf(W->win.title, sizeof W->win.title, "%s", t ? t : ""); RET(1); }
static void s_GetWindowTextA(Cpu *c) { size_t l = e_strlen(W->win.title); u32 n = (u32)ARG(2); if (l + 1 > n) l = n ? n - 1 : 0; u8 *p = mem_ptr(c->mem, ARG(1), l + 1, 1); if (p) { e_memcpy(p, W->win.title, l); p[l] = 0; } RET(l); }
static void s_SetWindowPos(Cpu *c) { RET(1); }
static void s_MoveWindow(Cpu *c) { RET(1); }
static u64 wnd_userdata, wnd_long[8];
static void s_SetWindowLongPtrA(Cpu *c) { int idx = (int)(s32)ARG(1); u64 old; if (idx == -21) { old = wnd_userdata; wnd_userdata = ARG(2); } else if (idx == -4) { old = W->win.wndproc; W->win.wndproc = ARG(2); } else if (idx == -16) { old = W->win.style; W->win.style = (u32)ARG(2) | (W->win.style & 0x40000000u); } else { old = wnd_long[idx & 7]; wnd_long[idx & 7] = ARG(2); } RET(old); }
static void s_GetWindowLongPtrA(Cpu *c) { int idx = (int)(s32)ARG(1); if (idx == -21) RET(wnd_userdata); else if (idx == -4) RET(W->win.wndproc); else if (idx == -16) RET(W->win.style & ~0x40000000u); else if (idx == -20) RET(0); else RET(wnd_long[idx & 7]); }
static void s_SetFocus(Cpu *c) { RET(HWND_MAIN); }
static void s_GetFocus(Cpu *c) { RET(W->win.created ? HWND_MAIN : 0); }
static void s_SetForegroundWindow(Cpu *c) { RET(1); }
static void s_GetForegroundWindow(Cpu *c) { RET(W->win.created ? HWND_MAIN : 0); }
static void s_GetActiveWindow(Cpu *c) { RET(W->win.created ? HWND_MAIN : 0); }
static void s_SetActiveWindow(Cpu *c) { RET(HWND_MAIN); }
static void s_IsWindow(Cpu *c) { RET(ARG(0) == HWND_MAIN && W->win.created); }
static void s_IsWindowVisible(Cpu *c) { RET(W->win.shown); }
static void s_IsIconic(Cpu *c) { RET(0); }
static void s_IsZoomed(Cpu *c) { RET(0); }
static void s_GetDesktopWindow(Cpu *c) { RET(0x10001); }
static void s_GetParent(Cpu *c) { RET(0); }
static void s_FindWindowA(Cpu *c) { RET(0); }
static void s_EnableWindow(Cpu *c) { RET(0); }
static void s_BringWindowToTop(Cpu *c) { RET(1); }
static void s_SetCapture(Cpu *c) { RET(0); }
static void s_ReleaseCapture(Cpu *c) { RET(1); }
static void s_ClientToScreen(Cpu *c) { u64 p = ARG(1); wr32(c, p, rd32(c, p) + 64); wr32(c, p + 4, rd32(c, p + 4) + 64); RET(1); }
static void s_ScreenToClient(Cpu *c) { u64 p = ARG(1); wr32(c, p, rd32(c, p) - 64); wr32(c, p + 4, rd32(c, p + 4) - 64); RET(1); }
static void s_MapWindowPoints(Cpu *c) { RET(0); }
static void s_GetCursorPos(Cpu *c) { wr32(c, ARG(0), (u32)(g_mouse_x + 64)); wr32(c, ARG(0) + 4, (u32)(g_mouse_y + 64)); RET(1); }
static void s_SetCursorPos(Cpu *c) { RET(1); }
static void s_ShowCursor(Cpu *c) { RET(ARG(0) ? 1 : (u64)-1); }
static void s_SetCursor(Cpu *c) { RET(0); }
static void s_ClipCursor(Cpu *c) { RET(1); }
static void s_LoadCursorA(Cpu *c) { RET(0x7A001); }
static void s_LoadIconA(Cpu *c) { RET(0x7B001); }
static void s_LoadImageA(Cpu *c) { RET(0x7C001); }
static void s_DestroyCursor(Cpu *c) { RET(1); }
static void s_GetAsyncKeyState(Cpu *c) { int vk = (int)ARG(0) & 0xff; RET(W->key_state[vk] ? 0x8000 : 0); }
static void s_GetKeyState(Cpu *c) { s_GetAsyncKeyState(c); }
static void s_GetKeyboardState(Cpu *c) { for (int i = 0; i < 256; i++) wr8(c, ARG(0) + i, W->key_state[i] ? 0x80 : 0); RET(1); }
static void s_MapVirtualKeyA(Cpu *c) { RET(ARG(0)); }
static void s_GetSystemMetrics(Cpu *c) { int i = (int)ARG(0); RET(i == 0 ? 1280 : i == 1 ? 720 : i == 4 ? 23 : i == 7 ? 1 : i == 8 ? 1 : i == 32 ? 8 : i == 33 ? 8 : i == 15 ? 1 : i == 16 ? 1280 : i == 17 ? 720 : i == 61 ? 1280 : i == 62 ? 720 : i == 78 ? 1280 : i == 79 ? 720 : 0); }
static void s_SystemParametersInfoA(Cpu *c) { if ((u32)ARG(0) == 0x30 && ARG(2)) { wr32(c, ARG(2), 0); wr32(c, ARG(2) + 4, 0); wr32(c, ARG(2) + 8, 1280); wr32(c, ARG(2) + 12, 680); RET(1); return; } RET(0); }
static void s_MessageBoxA(Cpu *c) { const char *t = rd_str(c, ARG(1), 2048), *cap = rd_str(c, ARG(2), 256); w32_report(W, "MessageBox [%s]: %s", cap ? cap : "", t ? t : ""); e_log("MessageBox [%s]: %s\n", cap ? cap : "", t ? t : ""); RET(1); }
static void s_MessageBoxW(Cpu *c) { char t[1024], cap[128]; int n = 0; for (int i = 0; i < 1023; i++) { u16 ch = rd16(c, ARG(1) + i * 2); if (!ch) break; t[n++] = ch < 128 ? (char)ch : '?'; } t[n] = 0; n = 0; if (ARG(2)) for (int i = 0; i < 127; i++) { u16 ch = rd16(c, ARG(2) + i * 2); if (!ch) break; cap[n++] = ch < 128 ? (char)ch : '?'; } cap[n] = 0; w32_report(W, "MessageBox [%s]: %s", cap, t); e_log("MessageBox [%s]: %s\n", cap, t); RET(1); }
static void s_wsprintfA(Cpu *c) { char out[1024]; int n = w32_format_args(c, out, sizeof out, ARG(1), 0, 2); if (c->status) return; u8 *p = mem_ptr(c->mem, ARG(0), n + 1, 1); if (p) e_memcpy(p, out, n + 1); RET(n); }
static void s_SetProcessDPIAware(Cpu *c) { RET(1); }
static void s_SetProcessDpiAwarenessContext(Cpu *c) { RET(1); }
static void s_GetDpiForWindow(Cpu *c) { RET(96); }
static void s_GetDpiForSystem(Cpu *c) { RET(96); }
static void s_EnumDisplaySettingsA(Cpu *c) { RET(0); }
static void s_ChangeDisplaySettingsA(Cpu *c) { RET(0); }
static void s_RegisterClipboardFormatA(Cpu *c) { RET(0xC100); }
static void s_GetDlgItem(Cpu *c) { RET(0); }
static void s_SetRect(Cpu *c) { u64 r = ARG(0); wr32(c, r, (u32)ARG(1)); wr32(c, r + 4, (u32)ARG(2)); wr32(c, r + 8, (u32)ARG(3)); wr32(c, r + 12, (u32)ARG(4)); RET(1); }
static void s_PtInRect(Cpu *c) { u64 r = ARG(0); s32 x = (s32)(u32)ARG(1), y = (s32)(u32)(ARG(1) >> 32); RET(x >= (s32)rd32(c, r) && x < (s32)rd32(c, r + 8) && y >= (s32)rd32(c, r + 4) && y < (s32)rd32(c, r + 12)); }
static void s_FillRect(Cpu *c) { Win32 *w = W; Surf s; if (!dc_surf(c, ARG(0), &s)) { RET(0); return; } u64 r = ARG(1); u64 hb = ARG(2); u32 px = 0;
  GdiObj *b = gdi_get(w, hb); if (b) px = b->color; else if (hb < 32) { static const u32 sys[] = { 0xc0c0c0, 0x000000, 0x000080, 0x808080, 0xc0c0c0, 0xffffff, 0x000000, 0x000000, 0x000000, 0xffffff, 0xc0c0c0, 0xc0c0c0, 0x808080, 0x000080, 0xffffff, 0xc0c0c0 }; px = sys[(hb - 1) & 15]; }
  fill_rect_px(&s, (s32)rd32(c, r), (s32)rd32(c, r + 4), (s32)rd32(c, r + 8), (s32)rd32(c, r + 12), px); mark_screen(w, ARG(0)); RET(1); }
static void s_DrawTextA(Cpu *c) { Win32 *w = W; Surf s; if (!dc_surf(c, ARG(0), &s)) { RET(0); return; } GdiObj *dc = gdi_get(w, ARG(0)); const char *t = rd_str(c, ARG(1), 4096); int len = (int)(s32)ARG(2); if (!t) { RET(0); return; } if (len < 0) len = (int)e_strlen(t); u64 r = ARG(3); u32 fmt = (u32)ARG(4);
  int x = (s32)rd32(c, r), y = (s32)rd32(c, r + 4), rw = (s32)rd32(c, r + 8) - x, rh = (s32)rd32(c, r + 12) - y; GdiObj *f = gdi_get(w, dc->font); int fh = f ? f->font_h : 16; int cw = (fh * 11 + 10) / 20;
  if (fmt & 1) x += (rw - cw * len) / 2; else if (fmt & 2) x += rw - cw * len; if (fmt & 4) y += (rh - fh) / 2; else if (fmt & 8) y += rh - fh;
  if (!(fmt & 0x400)) draw_text(&s, dc, w, x, y, t, len); mark_screen(w, ARG(0)); RET(fh); }

/* ---------- gdi32 ---------- */
static u64 make_dc(Win32 *w) { GdiObj *dc = gdi_new(w, K_DC); if (!dc) return 0; dc->text_color = 0; dc->bk_color = 0xffffff; dc->bk_mode = 2; dc->pen = stock[7]; dc->brush = stock[0]; dc->font = stock[13]; return dc->handle; }
static void s_CreateCompatibleDC(Cpu *c) { RET(make_dc(W)); }
static void s_CreateDCA(Cpu *c) { RET(window_dc); }
static void s_DeleteDC(Cpu *c) { GdiObj *o = gdi_get(W, ARG(0)); if (o && o->kind == K_DC && ARG(0) != window_dc) { o->kind = 0; RET(1); } else RET(0); }
static u64 make_bitmap(Cpu *c, int bw, int bh, int bottom_up, u64 *bits_out) {
  Win32 *w = W; if (bw <= 0) bw = 1; if (bh <= 0) bh = 1; if ((u64)bw * bh > 64ull << 20) { w32_report(w, "bitmap %dx%d too large", bw, bh); return 0; }
  u64 sz = (u64)bw * bh * 4; u64 addr = w->fb_cur; if (!mem_map(w->mem, addr, sz, "dib")) return 0; w->fb_cur += (sz + 0xffff) & ~0xffffull;
  GdiObj *bm = gdi_new(w, K_BITMAP); if (!bm) return 0; bm->bw = bw; bm->bh = bh; bm->bits = addr; bm->font_bold = bottom_up; if (bits_out) *bits_out = addr; return bm->handle;
}
static void s_CreateDIBSection(Cpu *c) { u64 bmi = ARG(1); s32 bw = (s32)rd32(c, bmi + 4), bh = (s32)rd32(c, bmi + 8); u16 bpp = rd16(c, bmi + 14); u32 comp = rd32(c, bmi + 16);
  if (bpp != 32 || (comp != 0 && comp != 3)) w32_report(W, "CreateDIBSection: %u bpp (compression %u) treated as 32 bpp — colours will be wrong", bpp, comp);
  int bottom_up = bh > 0; if (bh < 0) bh = -bh; u64 bits = 0; u64 h = make_bitmap(c, bw, bh, bottom_up, &bits); if (ARG(3)) wr64(c, ARG(3), bits); RET(h); }
static void s_CreateCompatibleBitmap(Cpu *c) { RET(make_bitmap(c, (int)ARG(1), (int)ARG(2), 0, 0)); }
static void s_CreateBitmap(Cpu *c) { u64 bits; u64 h = make_bitmap(c, (int)ARG(0), (int)ARG(1), 0, &bits); if (ARG(4) && h) { u32 bpp = (u32)ARG(3) * (u32)ARG(2); if (bpp == 32) { u64 n = (u64)(int)ARG(0) * (int)ARG(1) * 4; u8 *s = mem_ptr(c->mem, ARG(4), n, 0), *d = mem_ptr(c->mem, bits, n, 1); if (s && d) e_memcpy(d, s, (size_t)n); } } RET(h); }
static void s_DeleteObject(Cpu *c) { GdiObj *o = gdi_get(W, ARG(0)); if (!o) { RET(0); return; } for (int i = 0; i < 20; i++) if (stock[i] == ARG(0)) { RET(1); return; } o->kind = 0; RET(1); }
static void s_SelectObject(Cpu *c) { Win32 *w = W; GdiObj *dc = gdi_get(w, ARG(0)), *o = gdi_get(w, ARG(1)); if (!dc || dc->kind != K_DC || !o) { RET(0); return; } u64 prev = 0;
  switch (o->kind) { case K_BITMAP: prev = dc->bitmap ? dc->bitmap : stock[19]; dc->bitmap = ARG(1); break; case K_BRUSH: prev = dc->brush; dc->brush = ARG(1); break; case K_PEN: prev = dc->pen; dc->pen = ARG(1); break; case K_FONT: prev = dc->font; dc->font = ARG(1); break; case K_RGN: prev = 1; break; default: prev = 0; }
  RET(prev); }
static void s_GetStockObject(Cpu *c) { u32 i = (u32)ARG(0); RET(i < 20 ? stock[i] : 0); }
static void s_GetCurrentObject(Cpu *c) { GdiObj *dc = gdi_get(W, ARG(0)); if (!dc) { RET(0); return; } u32 t = (u32)ARG(1); RET(t == 1 ? dc->pen : t == 2 ? dc->brush : t == 6 ? dc->font : t == 7 ? dc->bitmap : 0); }
static void s_CreateSolidBrush(Cpu *c) { GdiObj *b = gdi_new(W, K_BRUSH); if (!b) { RET(0); return; } b->color = cr2px((u32)ARG(0)); RET(b->handle); }
static void s_CreateHatchBrush(Cpu *c) { GdiObj *b = gdi_new(W, K_BRUSH); if (!b) { RET(0); return; } b->color = cr2px((u32)ARG(1)); RET(b->handle); }
static void s_CreatePatternBrush(Cpu *c) { GdiObj *b = gdi_new(W, K_BRUSH); if (!b) { RET(0); return; } b->color = 0x808080; RET(b->handle); }
static void s_CreateBrushIndirect(Cpu *c) { GdiObj *b = gdi_new(W, K_BRUSH); if (!b) { RET(0); return; } b->color = cr2px(rd32(c, ARG(0) + 4)); if (rd32(c, ARG(0)) == 1) b->pen_width = -1; RET(b->handle); }
static void s_CreatePen(Cpu *c) { GdiObj *p = gdi_new(W, K_PEN); if (!p) { RET(0); return; } p->color = cr2px((u32)ARG(2)); p->pen_width = (u32)ARG(0) == 5 ? -1 : ((int)ARG(1) > 0 ? (int)ARG(1) : 1); RET(p->handle); }
static void s_ExtCreatePen(Cpu *c) { GdiObj *p = gdi_new(W, K_PEN); if (!p) { RET(0); return; } p->color = cr2px(rd32(c, ARG(2) + 4)); p->pen_width = (int)ARG(1) > 0 ? (int)ARG(1) : 1; RET(p->handle); }
static void s_CreateFontA(Cpu *c) { GdiObj *f = gdi_new(W, K_FONT); if (!f) { RET(0); return; } int h = (int)(s32)ARG(0); if (h < 0) h = -h; if (!h) h = 16; f->font_h = h; f->font_bold = (int)ARG(4) >= 600; RET(f->handle); }
static void s_CreateFontIndirectA(Cpu *c) { GdiObj *f = gdi_new(W, K_FONT); if (!f) { RET(0); return; } int h = (s32)rd32(c, ARG(0)); if (h < 0) h = -h; if (!h) h = 16; f->font_h = h; f->font_bold = (s32)rd32(c, ARG(0) + 16) >= 600; RET(f->handle); }
static void s_SetTextColor(Cpu *c) { GdiObj *dc = gdi_get(W, ARG(0)); if (!dc) { RET(0xffffffffu); return; } u32 old = dc->text_color; dc->text_color = (u32)ARG(1); RET(old); }
static void s_SetBkColor(Cpu *c) { GdiObj *dc = gdi_get(W, ARG(0)); if (!dc) { RET(0xffffffffu); return; } u32 old = dc->bk_color; dc->bk_color = (u32)ARG(1); RET(old); }
static void s_SetBkMode(Cpu *c) { GdiObj *dc = gdi_get(W, ARG(0)); if (!dc) { RET(0); return; } int old = dc->bk_mode; dc->bk_mode = (int)ARG(1); RET(old); }
static void s_GetTextColor(Cpu *c) { GdiObj *dc = gdi_get(W, ARG(0)); RET(dc ? dc->text_color : 0); }
static void s_GetBkColor(Cpu *c) { GdiObj *dc = gdi_get(W, ARG(0)); RET(dc ? dc->bk_color : 0xffffff); }
static void s_SetTextAlign(Cpu *c) { RET(0); }
static void s_SetROP2(Cpu *c) { RET(13); }
static void s_SetStretchBltMode(Cpu *c) { RET(1); }
static void s_SetBrushOrgEx(Cpu *c) { RET(1); }
static void s_GdiFlush(Cpu *c) { RET(1); }
static void s_SaveDC(Cpu *c) { RET(1); }
static void s_RestoreDC(Cpu *c) { RET(1); }
static void s_SelectClipRgn(Cpu *c) { RET(1); }
static void s_IntersectClipRect(Cpu *c) { RET(2); }
static void s_SetMapMode(Cpu *c) { RET(1); }
static void s_SetViewportOrgEx(Cpu *c) { RET(1); }
static void s_SetWindowOrgEx(Cpu *c) { RET(1); }
static void s_GetDeviceCaps(Cpu *c) { int i = (int)ARG(1); RET(i == 8 ? W->screen_w : i == 10 ? W->screen_h : i == 12 ? 32 : i == 14 ? 1 : i == 88 ? 96 : i == 90 ? 96 : i == 116 ? 60 : i == 4 ? 1 : i == 24 ? 0x4000000 : i == 2 ? 0x4000 : 0); }
static void s_GetObjectA(Cpu *c) { GdiObj *o = gdi_get(W, ARG(0)); if (!o) { RET(0); return; } u64 a = ARG(2);
  if (o->kind == K_BITMAP) { if (!a) { RET(32); return; } wr32(c, a, 0); wr32(c, a + 4, (u32)o->bw); wr32(c, a + 8, (u32)o->bh); wr32(c, a + 12, (u32)o->bw * 4); wr16(c, a + 16, 1); wr16(c, a + 18, 32); wr64(c, a + 24, o->bits); RET(32); return; }
  if (o->kind == K_BRUSH) { if (a) { wr32(c, a, 0); wr32(c, a + 4, px2cr(o->color)); wr64(c, a + 8, 0); } RET(16); return; }
  if (o->kind == K_PEN) { if (a) { wr32(c, a, 0); wr32(c, a + 4, (u32)o->pen_width); wr32(c, a + 12, px2cr(o->color)); } RET(16); return; }
  if (o->kind == K_FONT) { if (a) { u8 *p = mem_ptr(c->mem, a, 92, 1); if (p) e_memset(p, 0, 92); wr32(c, a, (u32)-o->font_h); wr32(c, a + 16, o->font_bold ? 700 : 400); } RET(92); return; }
  RET(0); }
static void s_GetTextExtentPoint32A(Cpu *c) { GdiObj *dc = gdi_get(W, ARG(0)); GdiObj *f = dc ? gdi_get(W, dc->font) : 0; int fh = f ? f->font_h : 16; int cw = (fh * 11 + 10) / 20; wr32(c, ARG(3), (u32)(cw * (int)ARG(2))); wr32(c, ARG(3) + 4, (u32)fh); RET(1); }
static void s_GetTextMetricsA(Cpu *c) { GdiObj *dc = gdi_get(W, ARG(0)); GdiObj *f = dc ? gdi_get(W, dc->font) : 0; int fh = f ? f->font_h : 16; int cw = (fh * 11 + 10) / 20; u64 a = ARG(1); u8 *p = mem_ptr(c->mem, a, 56, 1); if (p) e_memset(p, 0, 56); wr32(c, a, (u32)fh); wr32(c, a + 4, (u32)(fh - 3)); wr32(c, a + 8, 3); wr32(c, a + 20, (u32)cw); wr32(c, a + 24, (u32)cw); wr32(c, a + 28, f && f->font_bold ? 700 : 400); RET(1); }
static void text_out(Cpu *c, u64 hdc, int x, int y, u64 str, int len, int wide) { Win32 *w = W; Surf s; GdiObj *dc = gdi_get(w, hdc); if (!dc || !dc_surf(c, hdc, &s)) { RET(0); return; } char buf[1024]; int n = 0;
  if (len < 0) len = 0; if (len > 1000) len = 1000;
  for (int i = 0; i < len; i++) { u32 ch = wide ? rd16(c, str + i * 2) : rd8(c, str + i); if (c->status) return; buf[n++] = ch < 128 ? (char)ch : '?'; }
  draw_text(&s, dc, w, x, y, buf, n); mark_screen(w, hdc); RET(1); }
static void s_TextOutA(Cpu *c) { text_out(c, ARG(0), (int)(s32)ARG(1), (int)(s32)ARG(2), ARG(3), (int)(s32)ARG(4), 0); }
static void s_TextOutW(Cpu *c) { text_out(c, ARG(0), (int)(s32)ARG(1), (int)(s32)ARG(2), ARG(3), (int)(s32)ARG(4), 1); }
static void s_ExtTextOutA(Cpu *c) { u64 rc = ARG(4); if ((ARG(3) & 2) && rc) { Surf s; GdiObj *dc = gdi_get(W, ARG(0)); if (dc && dc_surf(c, ARG(0), &s)) fill_rect_px(&s, (s32)rd32(c, rc), (s32)rd32(c, rc + 4), (s32)rd32(c, rc + 8), (s32)rd32(c, rc + 12), cr2px(dc->bk_color)); } text_out(c, ARG(0), (int)(s32)ARG(1), (int)(s32)ARG(2), ARG(5), (int)(s32)ARG(6), 0); }
static void s_ExtTextOutW(Cpu *c) { u64 rc = ARG(4); if ((ARG(3) & 2) && rc) { Surf s; GdiObj *dc = gdi_get(W, ARG(0)); if (dc && dc_surf(c, ARG(0), &s)) fill_rect_px(&s, (s32)rd32(c, rc), (s32)rd32(c, rc + 4), (s32)rd32(c, rc + 8), (s32)rd32(c, rc + 12), cr2px(dc->bk_color)); } text_out(c, ARG(0), (int)(s32)ARG(1), (int)(s32)ARG(2), ARG(5), (int)(s32)ARG(6), 1); }

static u32 rop_apply(u32 rop, u32 d, u32 s, u32 p) {
  switch (rop) { case 0x00CC0020: return s; case 0x00EE0086: return d | s; case 0x008800C6: return d & s; case 0x00660046: return d ^ s; case 0x00330008: return ~s & 0xffffff; case 0x00550009: return ~d & 0xffffff; case 0x00000042: return 0; case 0x00FF0062: return 0xffffff; case 0x00F00021: return p; case 0x005A0049: return d ^ p; case 0x00A000C9: return d & p; case 0x00FA0089: return d | p; case 0x00440328: return s & ~d; default: return s; }
}
static void blit(Cpu *c, u64 hdst, int dx, int dy, int dw, int dh, u64 hsrc, int sx, int sy, int sw, int sh, u32 rop) {
  Win32 *w = W; Surf d, s; if (!dc_surf(c, hdst, &d)) { RET(0); return; }
  int has_src = hsrc && dc_surf(c, hsrc, &s); GdiObj *ddc = gdi_get(w, hdst); int nb; u32 pat = ddc ? dc_brush_px(w, ddc, &nb) : 0;
  if (rop != 0x00CC0020 && rop != 0x00000042 && rop != 0x00FF0062 && rop != 0x00F00021 && rop != 0x00EE0086 && rop != 0x008800C6 && rop != 0x00660046 && rop != 0x00330008 && rop != 0x00550009) { static int warned; if (!warned++) w32_report(w, "BitBlt: raster op 0x%08x approximated as SRCCOPY", rop); }
  int flipx = 0, flipy = 0; if (dw < 0) { dw = -dw; dx -= dw; flipx ^= 1; } if (dh < 0) { dh = -dh; dy -= dh; flipy ^= 1; } if (sw < 0) { sw = -sw; sx -= sw; flipx ^= 1; } if (sh < 0) { sh = -sh; sy -= sh; flipy ^= 1; }
  if (dw <= 0 || dh <= 0) { RET(1); return; }
  for (int y = 0; y < dh; y++) { int yy = dy + y; if (yy < 0 || yy >= d.h) continue; u32 *drow = srow(&d, yy); int syy = sy + (flipy ? dh - 1 - y : y) * sh / dh; u32 *srw = 0; if (has_src && syy >= 0 && syy < s.h) srw = srow(&s, syy);
    if (dw == sw && !flipx && srw && rop == 0x00CC0020) { int x0 = dx < 0 ? -dx : 0, x1 = dw; if (dx + x1 > d.w) x1 = d.w - dx; int sx0 = sx + x0; if (sx0 < 0) { x0 += -sx0; sx0 = 0; } int cnt = x1 - x0; if (sx0 + cnt > s.w) cnt = s.w - sx0; if (cnt > 0) e_memcpy(drow + dx + x0, srw + sx0, (size_t)cnt * 4); continue; }
    for (int x = 0; x < dw; x++) { int xx = dx + x; if (xx < 0 || xx >= d.w) continue; u32 sv = 0; if (srw) { int sxx = sx + (flipx ? dw - 1 - x : x) * sw / dw; if (sxx >= 0 && sxx < s.w) sv = srw[sxx]; } drow[xx] = rop_apply(rop, drow[xx], sv, pat); } }
  mark_screen(w, hdst); RET(1);
}
static void s_BitBlt(Cpu *c) { int wdt = (int)(s32)ARG(3), hgt = (int)(s32)ARG(4); blit(c, ARG(0), (int)(s32)ARG(1), (int)(s32)ARG(2), wdt, hgt, ARG(5), (int)(s32)ARG(6), (int)(s32)ARG(7), wdt, hgt, (u32)ARG(8)); }
static void s_StretchBlt(Cpu *c) { blit(c, ARG(0), (int)(s32)ARG(1), (int)(s32)ARG(2), (int)(s32)ARG(3), (int)(s32)ARG(4), ARG(5), (int)(s32)ARG(6), (int)(s32)ARG(7), (int)(s32)ARG(8), (int)(s32)ARG(9), (u32)ARG(10)); }
static void s_PatBlt(Cpu *c) { blit(c, ARG(0), (int)(s32)ARG(1), (int)(s32)ARG(2), (int)(s32)ARG(3), (int)(s32)ARG(4), 0, 0, 0, (int)(s32)ARG(3), (int)(s32)ARG(4), (u32)ARG(5)); }
/* StretchDIBits(hdc, xd, yd, wd, hd, xs, ys, ws, hs, bits, bmi, usage, rop): copy from guest 32-bpp DIB memory */
static void s_StretchDIBits(Cpu *c) { Win32 *w = W; Surf d; if (!dc_surf(c, ARG(0), &d)) { RET(0); return; } u64 bmi = ARG(10); s32 bw = (s32)rd32(c, bmi + 4), bh = (s32)rd32(c, bmi + 8); u16 bpp = rd16(c, bmi + 14); int bottom_up = bh > 0; if (bh < 0) bh = -bh;
  if (bpp != 32 && bpp != 24) { w32_report(w, "StretchDIBits: %u bpp not supported", bpp); RET(0); return; }
  int dx = (s32)ARG(1), dy = (s32)ARG(2), dw = (s32)ARG(3), dh = (s32)ARG(4), sx = (s32)ARG(5), sy = (s32)ARG(6), sw = (s32)ARG(7), sh = (s32)ARG(8); if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) { RET(0); return; }
  int stride = bpp == 32 ? bw * 4 : ((bw * 3 + 3) & ~3); const u8 *bits = mem_ptr(c->mem, ARG(9), (u64)stride * bh, 0); if (!bits) { RET(0); return; }
  for (int y = 0; y < dh; y++) { int yy = dy + y; if (yy < 0 || yy >= d.h) continue; int syy = sy + y * sh / dh; if (syy < 0 || syy >= bh) continue; const u8 *row = bits + (size_t)(bottom_up ? bh - 1 - syy : syy) * stride; u32 *drow = srow(&d, yy);
    for (int x = 0; x < dw; x++) { int xx = dx + x; if (xx < 0 || xx >= d.w) continue; int sxx = sx + x * sw / dw; if (sxx < 0 || sxx >= bw) continue; drow[xx] = bpp == 32 ? *(const u32 *)(row + sxx * 4) : (u32)row[sxx * 3] | (u32)row[sxx * 3 + 1] << 8 | (u32)row[sxx * 3 + 2] << 16; } }
  mark_screen(w, ARG(0)); RET(dh); }
static void s_SetDIBitsToDevice(Cpu *c) { /* (hdc, xd, yd, w, h, xs, ys, start, lines, bits, bmi, usage) */ u64 a9 = ARG(9), a10 = ARG(10); int wdt = (s32)ARG(3), hgt = (s32)ARG(4);
  /* reuse StretchDIBits by rewriting args: place onto stack is messy — emulate directly */ Win32 *w = W; Surf d; if (!dc_surf(c, ARG(0), &d)) { RET(0); return; } s32 bw = (s32)rd32(c, a10 + 4), bh = (s32)rd32(c, a10 + 8); u16 bpp = rd16(c, a10 + 14); int bottom_up = bh > 0; if (bh < 0) bh = -bh; if (bpp != 32) { w32_report(w, "SetDIBitsToDevice: %u bpp not supported", bpp); RET(0); return; }
  const u32 *bits = (const u32 *)mem_ptr(c->mem, a9, (u64)bw * bh * 4, 0); if (!bits) { RET(0); return; } int dx = (s32)ARG(1), dy = (s32)ARG(2), sx = (s32)ARG(5), sy = (s32)ARG(6);
  for (int y = 0; y < hgt; y++) { int yy = dy + y, syy = sy + y; if (yy < 0 || yy >= d.h || syy < 0 || syy >= bh) continue; const u32 *row = bits + (size_t)(bottom_up ? bh - 1 - syy : syy) * bw; u32 *drow = srow(&d, yy); for (int x = 0; x < wdt; x++) { int xx = dx + x, sxx = sx + x; if (xx < 0 || xx >= d.w || sxx < 0 || sxx >= bw) continue; drow[xx] = row[sxx]; } }
  mark_screen(w, ARG(0)); RET(hgt); }
static void s_GetDIBits(Cpu *c) { RET(0); }
static void s_Rectangle(Cpu *c) { Win32 *w = W; Surf s; GdiObj *dc = gdi_get(w, ARG(0)); if (!dc || !dc_surf(c, ARG(0), &s)) { RET(0); return; } int nb, np; u32 b = dc_brush_px(w, dc, &nb), p = dc_pen_px(w, dc, &np); int l = (s32)ARG(1), t = (s32)ARG(2), r = (s32)ARG(3), bt = (s32)ARG(4);
  if (!nb) fill_rect_px(&s, l, t, r, bt, b); if (!np) { hline(&s, l, r - 1, t, p); hline(&s, l, r - 1, bt - 1, p); line_px(&s, l, t, l, bt - 1, p); line_px(&s, r - 1, t, r - 1, bt - 1, p); } mark_screen(w, ARG(0)); RET(1); }
static void s_RoundRect(Cpu *c) { s_Rectangle(c); }
static void s_Ellipse(Cpu *c) { Win32 *w = W; Surf s; GdiObj *dc = gdi_get(w, ARG(0)); if (!dc || !dc_surf(c, ARG(0), &s)) { RET(0); return; } int nb, np; u32 b = dc_brush_px(w, dc, &nb), p = dc_pen_px(w, dc, &np); fill_ellipse(&s, (s32)ARG(1), (s32)ARG(2), (s32)ARG(3), (s32)ARG(4), b, !nb, p, !np); mark_screen(w, ARG(0)); RET(1); }
static int cur_x, cur_y;
static void s_MoveToEx(Cpu *c) { if (ARG(3)) { wr32(c, ARG(3), (u32)cur_x); wr32(c, ARG(3) + 4, (u32)cur_y); } cur_x = (s32)ARG(1); cur_y = (s32)ARG(2); RET(1); }
static void s_LineTo(Cpu *c) { Win32 *w = W; Surf s; GdiObj *dc = gdi_get(w, ARG(0)); if (!dc || !dc_surf(c, ARG(0), &s)) { RET(0); return; } int np; u32 p = dc_pen_px(w, dc, &np); int x = (s32)ARG(1), y = (s32)ARG(2); if (!np) line_px(&s, cur_x, cur_y, x, y, p); cur_x = x; cur_y = y; mark_screen(w, ARG(0)); RET(1); }
static void s_Polyline(Cpu *c) { Win32 *w = W; Surf s; GdiObj *dc = gdi_get(w, ARG(0)); if (!dc || !dc_surf(c, ARG(0), &s)) { RET(0); return; } int np; u32 p = dc_pen_px(w, dc, &np); int n = (int)ARG(2); u64 a = ARG(1); for (int i = 1; i < n && !np; i++) line_px(&s, (s32)rd32(c, a + (i - 1) * 8), (s32)rd32(c, a + (i - 1) * 8 + 4), (s32)rd32(c, a + i * 8), (s32)rd32(c, a + i * 8 + 4), p); mark_screen(w, ARG(0)); RET(1); }
static void s_Polygon(Cpu *c) { Win32 *w = W; Surf s; GdiObj *dc = gdi_get(w, ARG(0)); if (!dc || !dc_surf(c, ARG(0), &s)) { RET(0); return; } int nb, np; u32 b = dc_brush_px(w, dc, &nb), p = dc_pen_px(w, dc, &np); int n = (int)ARG(2); if (n > 64) n = 64; s32 pts[128]; for (int i = 0; i < n; i++) { pts[i * 2] = (s32)rd32(c, ARG(1) + i * 8); pts[i * 2 + 1] = (s32)rd32(c, ARG(1) + i * 8 + 4); }
  if (!nb) fill_poly(&s, pts, n, b); if (!np) for (int i = 0; i < n; i++) { int j = (i + 1) % n; line_px(&s, pts[i * 2], pts[i * 2 + 1], pts[j * 2], pts[j * 2 + 1], p); } mark_screen(w, ARG(0)); RET(1); }
static void s_SetPixel(Cpu *c) { Win32 *w = W; Surf s; if (!dc_surf(c, ARG(0), &s)) { RET(0xffffffffu); return; } put_px(&s, (s32)ARG(1), (s32)ARG(2), cr2px((u32)ARG(3))); mark_screen(w, ARG(0)); RET(ARG(3)); }
static void s_SetPixelV(Cpu *c) { s_SetPixel(c); RET(1); }
static void s_GetPixel(Cpu *c) { Surf s; if (!dc_surf(c, ARG(0), &s)) { RET(0xffffffffu); return; } int x = (s32)ARG(1), y = (s32)ARG(2); if (x < 0 || y < 0 || x >= s.w || y >= s.h) { RET(0xffffffffu); return; } RET(px2cr(srow(&s, y)[x])); }
static void s_CreatePolygonRgn(Cpu *c) { GdiObj *r = gdi_new(W, K_RGN); if (!r) { RET(0); return; } int n = (int)ARG(1); if (n > 32) n = 32; r->npts = n; for (int i = 0; i < n; i++) { r->pts[i * 2] = (s32)rd32(c, ARG(0) + i * 8); r->pts[i * 2 + 1] = (s32)rd32(c, ARG(0) + i * 8 + 4); } RET(r->handle); }
static void s_CreateRectRgn(Cpu *c) { GdiObj *r = gdi_new(W, K_RGN); if (!r) { RET(0); return; } r->npts = 0; r->rx = (s32)ARG(0); r->ry = (s32)ARG(1); r->rw = (s32)ARG(2) - r->rx; r->rh = (s32)ARG(3) - r->ry; RET(r->handle); }
static void s_CreateRectRgnIndirect(Cpu *c) { GdiObj *r = gdi_new(W, K_RGN); if (!r) { RET(0); return; } u64 a = ARG(0); r->npts = 0; r->rx = (s32)rd32(c, a); r->ry = (s32)rd32(c, a + 4); r->rw = (s32)rd32(c, a + 8) - r->rx; r->rh = (s32)rd32(c, a + 12) - r->ry; RET(r->handle); }
static void s_CreateEllipticRgn(Cpu *c) { GdiObj *r = gdi_new(W, K_RGN); if (!r) { RET(0); return; } r->npts = -1; r->rx = (s32)ARG(0); r->ry = (s32)ARG(1); r->rw = (s32)ARG(2) - r->rx; r->rh = (s32)ARG(3) - r->ry; RET(r->handle); }
static void s_CombineRgn(Cpu *c) { GdiObj *d = gdi_get(W, ARG(0)), *a = gdi_get(W, ARG(1)); if (d && a) { u64 h = d->handle; *d = *a; d->handle = h; } RET(2); }
static void s_FillRgn(Cpu *c) { Win32 *w = W; Surf s; if (!dc_surf(c, ARG(0), &s)) { RET(0); return; } GdiObj *r = gdi_get(w, ARG(1)), *b = gdi_get(w, ARG(2)); if (!r || !b) { RET(0); return; }
  if (r->npts > 0) fill_poly(&s, r->pts, r->npts, b->color); else if (r->npts < 0) fill_ellipse(&s, r->rx, r->ry, r->rx + r->rw, r->ry + r->rh, b->color, 1, 0, 0); else fill_rect_px(&s, r->rx, r->ry, r->rx + r->rw, r->ry + r->rh, b->color); mark_screen(w, ARG(0)); RET(1); }
static void s_PaintRgn(Cpu *c) { GdiObj *dc = gdi_get(W, ARG(0)); if (!dc) { RET(0); return; } c->r[R8] = dc->brush; s_FillRgn(c); }
static void s_OffsetRgn(Cpu *c) { GdiObj *r = gdi_get(W, ARG(0)); if (!r) { RET(0); return; } int dx = (s32)ARG(1), dy = (s32)ARG(2); r->rx += dx; r->ry += dy; for (int i = 0; i < r->npts; i++) { r->pts[i * 2] += dx; r->pts[i * 2 + 1] += dy; } RET(2); }
static void s_PtInRegion(Cpu *c) { GdiObj *r = gdi_get(W, ARG(0)); if (!r) { RET(0); return; } int x = (s32)ARG(1), y = (s32)ARG(2); if (r->npts <= 0) { RET(x >= r->rx && x < r->rx + r->rw && y >= r->ry && y < r->ry + r->rh); return; } int in = 0; for (int i = 0, j = r->npts - 1; i < r->npts; j = i++) { int xi = r->pts[i * 2], yi = r->pts[i * 2 + 1], xj = r->pts[j * 2], yj = r->pts[j * 2 + 1]; if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi + (yj == yi)) + xi)) in = !in; } RET(in); }
static void s_GetRgnBox(Cpu *c) { GdiObj *r = gdi_get(W, ARG(0)); if (!r) { RET(0); return; } u64 a = ARG(1); if (r->npts > 0) { int x0 = r->pts[0], y0 = r->pts[1], x1 = x0, y1 = y0; for (int i = 1; i < r->npts; i++) { if (r->pts[i * 2] < x0) x0 = r->pts[i * 2]; if (r->pts[i * 2] > x1) x1 = r->pts[i * 2]; if (r->pts[i * 2 + 1] < y0) y0 = r->pts[i * 2 + 1]; if (r->pts[i * 2 + 1] > y1) y1 = r->pts[i * 2 + 1]; } wr32(c, a, (u32)x0); wr32(c, a + 4, (u32)y0); wr32(c, a + 8, (u32)x1); wr32(c, a + 12, (u32)y1); } else { wr32(c, a, (u32)r->rx); wr32(c, a + 4, (u32)r->ry); wr32(c, a + 8, (u32)(r->rx + r->rw)); wr32(c, a + 12, (u32)(r->ry + r->rh)); } RET(2); }
static void s_GetSysColor(Cpu *c) { static const u32 sys[] = { 0xc0c0c0, 0x000000, 0x800000, 0x808080, 0xc0c0c0, 0xffffff, 0x000000, 0x000000, 0x000000, 0xffffff, 0xc0c0c0, 0xc0c0c0, 0x808080, 0x800000, 0xffffff, 0xf0f0f0 }; RET(sys[ARG(0) & 15]); }
static void s_GetSysColorBrush(Cpu *c) { RET(stock[(ARG(0) & 15) == 0 ? 1 : 0]); }
static void s_GetNearestColor(Cpu *c) { RET(ARG(1)); }
static void s_GetTextFaceA(Cpu *c) { const char *n = "Nebula Mono"; size_t l = e_strlen(n) + 1; if ((u32)ARG(1) >= l && ARG(2)) { u8 *p = mem_ptr(c->mem, ARG(2), l, 1); if (p) e_memcpy(p, n, l); } RET(l); }
static void s_GetCharWidth32A(Cpu *c) { GdiObj *dc = gdi_get(W, ARG(0)); GdiObj *f = dc ? gdi_get(W, dc->font) : 0; int fh = f ? f->font_h : 16; int cw = (fh * 11 + 10) / 20; for (u64 i = ARG(1); i <= ARG(2) && i - ARG(1) < 256; i++) wr32(c, ARG(3) + (i - ARG(1)) * 4, (u32)cw); RET(1); }
static void s_CreateFontW(Cpu *c) { s_CreateFontA(c); }
static void s_CreateFontIndirectW(Cpu *c) { s_CreateFontIndirectA(c); }
static void s_GetTextMetricsW(Cpu *c) { s_GetTextMetricsA(c); }
static void s_GetTextExtentPoint32W(Cpu *c) { s_GetTextExtentPoint32A(c); }
static void s_GetObjectW(Cpu *c) { s_GetObjectA(c); }
static void s_PeekMessageW(Cpu *c) { s_PeekMessageA(c); }
static void s_GetMessageW(Cpu *c) { s_GetMessageA(c); }
static void s_DispatchMessageW(Cpu *c) { s_DispatchMessageA(c); }
static void s_SendMessageW(Cpu *c) { s_SendMessageA(c); }
static void s_PostMessageW(Cpu *c) { s_PostMessageA(c); }
static void s_LoadCursorW(Cpu *c) { s_LoadCursorA(c); }
static void s_LoadIconW(Cpu *c) { s_LoadIconA(c); }
static void s_SetWindowTextW(Cpu *c) { Win32 *w = W; char t[128]; int n = 0; for (int i = 0; i < 127; i++) { u16 ch = rd16(c, ARG(1) + i * 2); if (!ch) break; t[n++] = ch < 128 ? (char)ch : '?'; } t[n] = 0; e_snprintf(w->win.title, sizeof w->win.title, "%s", t); RET(1); }
static void s_SetWindowLongPtrW(Cpu *c) { s_SetWindowLongPtrA(c); }
static void s_GetWindowLongPtrW(Cpu *c) { s_GetWindowLongPtrA(c); }
static void s_SetWindowLongA(Cpu *c) { s_SetWindowLongPtrA(c); }
static void s_GetWindowLongA(Cpu *c) { s_GetWindowLongPtrA(c); }
static void s_DrawTextW(Cpu *c) { RET(16); }
static void s_GetClassNameA(Cpu *c) { size_t l = e_strlen(W->wndclass_name); u32 n = (u32)ARG(2); if (l + 1 > n) l = n ? n - 1 : 0; u8 *p = mem_ptr(c->mem, ARG(1), l + 1, 1); if (p) { e_memcpy(p, W->wndclass_name, l); p[l] = 0; } RET(l); }
static void s_GetUpdateRect(Cpu *c) { Win32 *w = W; if (ARG(1)) { wr32(c, ARG(1), 0); wr32(c, ARG(1) + 4, 0); wr32(c, ARG(1) + 8, (u32)w->win.w); wr32(c, ARG(1) + 12, (u32)w->win.h); } RET((w->win.style & 0x40000000u) != 0); }
static void s_timeGetTime(Cpu *c) { RET((u32)(host_now_ms() - W->start_ms + 100000.0)); }
static void s_timeBeginPeriod(Cpu *c) { RET(0); }
static void s_timeEndPeriod(Cpu *c) { RET(0); }
static void s_PlaySoundA(Cpu *c) { RET(1); }
static void s_waveOutGetNumDevs(Cpu *c) { RET(0); }
static void s_CoInitialize(Cpu *c) { RET(0); }
static void s_CoInitializeEx(Cpu *c) { RET(0); }
static void s_CoUninitialize(Cpu *c) { }
static void s_CoCreateInstance(Cpu *c) { w32_report(W, "CoCreateInstance: COM objects are not available"); RET(0x80040154u); }
static void s_RegOpenKeyExA(Cpu *c) { RET(2); }
static void s_RegQueryValueExA(Cpu *c) { RET(2); }
static void s_RegCloseKey(Cpu *c) { RET(0); }
static void s_ShellExecuteA(Cpu *c) { RET(42); }
static void s_CommandLineToArgvW(Cpu *c) { wr32(c, ARG(1), rd32(c, W->ubuf + 0x200)); RET(rd64(c, W->ubuf + 0x218)); }

#define S(n) { "", #n, s_##n }
static const ShimDef gdi_shims[] = {
  S(PeekMessageA), S(PeekMessageW), S(GetMessageA), S(GetMessageW), S(TranslateMessage), S(DispatchMessageA), S(DispatchMessageW), S(SendMessageA), S(SendMessageW), S(PostMessageA), S(PostMessageW), S(PostQuitMessage), S(SetTimer), S(KillTimer), S(MsgWaitForMultipleObjects), S(WaitMessage),
  S(RegisterClassA), S(RegisterClassExA), S(RegisterClassW), S(RegisterClassExW), S(UnregisterClassA), S(GetClassInfoExA), S(AdjustWindowRect), S(AdjustWindowRectEx), S(CreateWindowExA), S(CreateWindowExW), S(DefWindowProcA), S(DefWindowProcW), S(DestroyWindow), S(ShowWindow), S(UpdateWindow), S(InvalidateRect), S(InvalidateRgn), S(ValidateRect), S(RedrawWindow), S(GetClientRect), S(GetWindowRect), S(BeginPaint), S(EndPaint), S(GetDC), S(GetWindowDC), S(ReleaseDC),
  S(SetWindowTextA), S(SetWindowTextW), S(GetWindowTextA), S(SetWindowPos), S(MoveWindow), S(SetWindowLongPtrA), S(GetWindowLongPtrA), S(SetWindowLongPtrW), S(GetWindowLongPtrW), S(SetWindowLongA), S(GetWindowLongA), S(SetFocus), S(GetFocus), S(SetForegroundWindow), S(GetForegroundWindow), S(GetActiveWindow), S(SetActiveWindow), S(IsWindow), S(IsWindowVisible), S(IsIconic), S(IsZoomed), S(GetDesktopWindow), S(GetParent), S(FindWindowA), S(EnableWindow), S(BringWindowToTop), S(SetCapture), S(ReleaseCapture), S(ClientToScreen), S(ScreenToClient), S(MapWindowPoints),
  S(GetCursorPos), S(SetCursorPos), S(ShowCursor), S(SetCursor), S(ClipCursor), S(LoadCursorA), S(LoadCursorW), S(LoadIconA), S(LoadIconW), S(LoadImageA), S(DestroyCursor), S(GetAsyncKeyState), S(GetKeyState), S(GetKeyboardState), S(MapVirtualKeyA), S(GetSystemMetrics), S(SystemParametersInfoA), S(MessageBoxA), S(MessageBoxW), S(wsprintfA), S(SetProcessDPIAware), S(SetProcessDpiAwarenessContext), S(GetDpiForWindow), S(GetDpiForSystem), S(EnumDisplaySettingsA), S(ChangeDisplaySettingsA), S(RegisterClipboardFormatA), S(GetDlgItem), S(SetRect), S(PtInRect), S(FillRect), S(DrawTextA), S(DrawTextW), S(GetClassNameA), S(GetUpdateRect), S(GetSysColor), S(GetSysColorBrush),
  S(CreateCompatibleDC), S(CreateDCA), S(DeleteDC), S(CreateDIBSection), S(CreateCompatibleBitmap), S(CreateBitmap), S(DeleteObject), S(SelectObject), S(GetStockObject), S(GetCurrentObject), S(CreateSolidBrush), S(CreateHatchBrush), S(CreatePatternBrush), S(CreateBrushIndirect), S(CreatePen), S(ExtCreatePen), S(CreateFontA), S(CreateFontW), S(CreateFontIndirectA), S(CreateFontIndirectW), S(SetTextColor), S(SetBkColor), S(SetBkMode), S(GetTextColor), S(GetBkColor), S(SetTextAlign), S(SetROP2), S(SetStretchBltMode), S(SetBrushOrgEx), S(GdiFlush), S(SaveDC), S(RestoreDC), S(SelectClipRgn), S(IntersectClipRect), S(SetMapMode), S(SetViewportOrgEx), S(SetWindowOrgEx), S(GetDeviceCaps), S(GetObjectA), S(GetObjectW),
  S(GetTextExtentPoint32A), S(GetTextExtentPoint32W), S(GetTextMetricsA), S(GetTextMetricsW), S(TextOutA), S(TextOutW), S(ExtTextOutA), S(ExtTextOutW), S(BitBlt), S(StretchBlt), S(PatBlt), S(StretchDIBits), S(SetDIBitsToDevice), S(GetDIBits), S(Rectangle), S(RoundRect), S(Ellipse), S(MoveToEx), S(LineTo), S(Polyline), S(Polygon), S(SetPixel), S(SetPixelV), S(GetPixel),
  S(CreatePolygonRgn), S(CreateRectRgn), S(CreateRectRgnIndirect), S(CreateEllipticRgn), S(CombineRgn), S(FillRgn), S(PaintRgn), S(OffsetRgn), S(PtInRegion), S(GetRgnBox), S(GetNearestColor), S(GetTextFaceA), S(GetCharWidth32A),
  S(timeGetTime), S(timeBeginPeriod), S(timeEndPeriod), S(PlaySoundA), S(waveOutGetNumDevs), S(CoInitialize), S(CoInitializeEx), S(CoUninitialize), S(CoCreateInstance), S(RegOpenKeyExA), S(RegQueryValueExA), S(RegCloseKey), S(ShellExecuteA), S(CommandLineToArgvW),
};
ShimFn gdi_find_shim(const char *name) { for (size_t i = 0; i < sizeof gdi_shims / sizeof gdi_shims[0]; i++) if (!e_strcmp(gdi_shims[i].name, name)) return gdi_shims[i].fn; return 0; }

void gdi_init(Win32 *w) {
  /* stock objects */
  static const u32 brush_cols[] = { 0xffffff, 0xc0c0c0, 0x808080, 0x404040, 0x000000 };
  for (int i = 0; i < 5; i++) { GdiObj *b = gdi_new(w, K_BRUSH); b->color = brush_cols[i]; stock[i] = b->handle; }
  { GdiObj *b = gdi_new(w, K_BRUSH); b->pen_width = -1; stock[5] = b->handle; }
  { GdiObj *p = gdi_new(w, K_PEN); p->color = 0xffffff; p->pen_width = 1; stock[6] = p->handle; }
  { GdiObj *p = gdi_new(w, K_PEN); p->color = 0; p->pen_width = 1; stock[7] = p->handle; }
  { GdiObj *p = gdi_new(w, K_PEN); p->pen_width = -1; stock[8] = p->handle; }
  for (int i = 10; i <= 17; i++) { GdiObj *f = gdi_new(w, K_FONT); f->font_h = 16; stock[i] = f->handle; }
  { GdiObj *b = gdi_new(w, K_BRUSH); b->color = 0xffffff; stock[18] = b->handle; }
  { GdiObj *b = gdi_new(w, K_BITMAP); b->bw = b->bh = 1; b->bits = 0; stock[19] = b->handle; }   /* default 1x1 bitmap handle */
  { GdiObj *p = gdi_new(w, K_PEN); p->color = 0; p->pen_width = 1; stock[9] = p->handle; }
  window_dc = make_dc(w);
}
