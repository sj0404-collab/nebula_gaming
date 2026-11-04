/* trackbar.inc — msctls_trackbar32 (slider) and msctls_updown32 (spin button), the real comctl32 behaviour:
   TBM_* range/position/line/page/tick/selection/thumb messages, mouse dragging of the thumb with capture, page jumps on
   the channel, keyboard (arrows, PgUp/PgDn, Home/End), WM_HSCROLL/WM_VSCROLL notifications (TB_THUMBTRACK while dragging,
   TB_THUMBPOSITION + TB_ENDTRACK on release, TB_LINEUP/… for keys) and NM_RELEASEDCAPTURE;
   UDM_* range/position/base/buddy/acceleration, auto-buddy (the previous sibling) aligned left/right inside the buddy,
   UDS_SETBUDDYINT (decimal with thousands separators or hex), UDN_DELTAPOS before the change (a non-zero reply cancels),
   the buddy's text updated, WM_VSCROLL/WM_HSCROLL SB_THUMBPOSITION after it and SB_ENDSCROLL on release, UDS_ARROWKEYS
   from the buddy edit, UDS_WRAP, acceleration while the button is held (timer).  Painting follows the Windows 10 look. */
typedef struct { int min, max, pos, line, page, ticfreq, thumb_len, sel0, sel1, nt; int tics[64]; int drag, drag_off, hot; int tipside; u64 tooltip; int buddy_l, buddy_r; int posnotify; } Trackbar;
typedef struct { int min, max, pos, base; int buddy; int naccel; struct { u32 sec, inc; } accel[8]; int pressed, hot; double press_t; u64 nmbuf, txtbuf; int pending_delta; int unicode; } UpDown;

static void host_timer_set(int wi, u64 id, u32 ms) { if (ms < 10) ms = 10; for (int k = 0; k < g_timers_n; k++) if (g_timers[k].id == id && g_timers[k].wnd == wi) { g_timers[k].ms = ms; g_timers[k].next = host_now_ms() + ms; return; } if (g_timers_n < 32) { g_timers[g_timers_n].id = id; g_timers[g_timers_n].wnd = wi; g_timers[g_timers_n].ms = ms; g_timers[g_timers_n].next = host_now_ms() + ms; g_timers[g_timers_n].proc = 0; g_timers_n++; } }
static void host_timer_kill(int wi, u64 id) { for (int k = 0; k < g_timers_n; k++) if (g_timers[k].id == id && g_timers[k].wnd == wi) { g_timers[k] = g_timers[--g_timers_n]; return; } }

/* ---------- trackbar ---------- */
#define TBS_AUTOTICKS 1
#define TBS_VERT 2
#define TBS_TOP 4
#define TBS_BOTH 8
#define TBS_NOTICKS 0x10
#define TBS_ENABLESELRANGE 0x20
#define TBS_NOTHUMB 0x80
#define TBS_REVERSED 0x200
static int trk_vert(Wnd *n) { return (n->style & TBS_VERT) != 0; }
static int trk_thumb_w(Trackbar *t) { (void)t; return 11; }
/* geometry: channel span along the axis [a0, a1] (thumb centre positions), thumb extent across the axis [b0, b1] */
static void trk_geom(int i, Trackbar *t, int *a0, int *a1, int *b0, int *b1, int *tick_b) { Wnd *n = &g_wnd[i]; int cw = wnd_client_w(i), ch = wnd_client_h(i); int v = trk_vert(n); int along = v ? ch : cw, across = v ? cw : ch; int tw = trk_thumb_w(t);
  int L = t->thumb_len; int ticks = !(n->style & TBS_NOTICKS); int both = (n->style & TBS_BOTH) != 0; int room = across - (ticks ? (both ? 2 * 7 : 7) : 0) - 2; if (L > room) L = room; if (L < 6) L = 6;
  int top; if (!ticks || both) top = (across - L) / 2; else if (n->style & TBS_TOP) top = across - 1 - L; else top = 1;
  *b0 = top; *b1 = top + L; *a0 = 6 + tw / 2; *a1 = along - 6 - tw / 2 - 1; if (*a1 < *a0) *a1 = *a0; *tick_b = (n->style & TBS_TOP) ? top - 2 : top + L + 1; }
static int trk_pos_to_px(Trackbar *t, Wnd *n, int a0, int a1, int pos) { int range = t->max - t->min; if (range <= 0) return a0; int p = pos - t->min; if (p < 0) p = 0; if (p > range) p = range; int x = a0 + (int)((long long)(a1 - a0) * p / range); if (n->style & TBS_REVERSED) x = a1 - (x - a0); return x; }
static int trk_px_to_pos(Trackbar *t, Wnd *n, int a0, int a1, int px) { int range = t->max - t->min; if (range <= 0 || a1 <= a0) return t->min; if (n->style & TBS_REVERSED) px = a1 - (px - a0); if (px < a0) px = a0; if (px > a1) px = a1; return t->min + (int)(((long long)(px - a0) * range + (a1 - a0) / 2) / (a1 - a0)); }
static void trk_thumb_rect(int i, Trackbar *t, int *l, int *tp, int *r, int *b) { Wnd *n = &g_wnd[i]; int a0, a1, b0, b1, tk; trk_geom(i, t, &a0, &a1, &b0, &b1, &tk); int cx = trk_pos_to_px(t, n, a0, a1, t->pos); int tw = trk_thumb_w(t); if (trk_vert(n)) { *l = b0; *r = b1; *tp = cx - tw / 2; *b = cx - tw / 2 + tw; } else { *l = cx - tw / 2; *r = cx - tw / 2 + tw; *tp = b0; *b = b1; } }
static void trk_channel_rect(int i, Trackbar *t, int *l, int *tp, int *r, int *b) { Wnd *n = &g_wnd[i]; int a0, a1, b0, b1, tk; trk_geom(i, t, &a0, &a1, &b0, &b1, &tk); int tw = trk_thumb_w(t); int mid = (b0 + b1) / 2; if (trk_vert(n)) { *l = mid - 2; *r = mid + 2; *tp = a0 - tw / 2; *b = a1 + tw / 2 + 1; } else { *l = a0 - tw / 2; *r = a1 + tw / 2 + 1; *tp = mid - 2; *b = mid + 2; } }
static void trk_paint(Cpu *c, int i, Trackbar *t) { Wnd *n = &g_wnd[i]; u64 hdc = ctl_paint_begin(c, i); Surf s; if (!hdc) return; if (dc_surf(c, hdc, &s)) { int cw = wnd_client_w(i), ch = wnd_client_h(i); int en = wnd_enabled_chain(i);
    if (!(n->style & 0x1000)) { u32 bg = n->parent >= 0 && wnd_alive(n->parent) && g_wnd[n->parent].bgcol_set ? g_wnd[n->parent].bgcol : 0xf0f0f0; fill_rect_px(&s, 0, 0, cw, ch, bg); }   /* TBS_TRANSPARENTBKGND leaves the parent's paint */
    int l, tp, r, b; trk_channel_rect(i, t, &l, &tp, &r, &b); fill_rect_px(&s, l, tp, r, b, 0xe7eaea); edge_rect(&s, l, tp, r, b, 0xd6d6d6, 0xd6d6d6);
    if ((n->style & TBS_ENABLESELRANGE) && t->sel1 > t->sel0) { int a0, a1, b0, b1, tk; trk_geom(i, t, &a0, &a1, &b0, &b1, &tk); int p0 = trk_pos_to_px(t, n, a0, a1, t->sel0), p1 = trk_pos_to_px(t, n, a0, a1, t->sel1); if (p0 > p1) { int q = p0; p0 = p1; p1 = q; } if (trk_vert(n)) fill_rect_px(&s, l + 1, p0, r - 1, p1 + 1, 0x0078d7); else fill_rect_px(&s, p0, tp + 1, p1 + 1, b - 1, 0x0078d7); }
    if (!(n->style & TBS_NOTICKS)) { int a0, a1, b0, b1, tk; trk_geom(i, t, &a0, &a1, &b0, &b1, &tk); int both = (n->style & TBS_BOTH) != 0; u32 tc = 0x606060; int freq = t->ticfreq > 0 ? t->ticfreq : 1;
      #define TB_TICK(p) do { int x_ = trk_pos_to_px(t, n, a0, a1, (int)(p)); int y0_ = (n->style & TBS_TOP) ? tk - 3 : tk, y1_ = (n->style & TBS_TOP) ? tk + 1 : tk + 4; if (trk_vert(n)) { hline(&s, y0_, y1_ - 1, x_, tc); if (both) hline(&s, b0 - 5, b0 - 2, x_, tc); } else { line_px(&s, x_, y0_, x_, y1_ - 1, tc); if (both) line_px(&s, x_, b0 - 5, x_, b0 - 2, tc); } } while (0)
      if (t->nt == 0 || (n->style & TBS_AUTOTICKS)) { long long p = t->min; int guard = 0; for (;;) { if (p > t->max) p = t->max; TB_TICK(p); if (p >= t->max || ++guard > 4096) break; p += freq; } } else { TB_TICK(t->min); TB_TICK(t->max); }
      for (int k = 0; k < t->nt; k++) TB_TICK(t->tics[k]);
      #undef TB_TICK
    }
    if (!(n->style & TBS_NOTHUMB)) { int tl, tt, tr, tb; trk_thumb_rect(i, t, &tl, &tt, &tr, &tb); u32 fill = !en ? 0xcccccc : t->drag ? 0xcccccc : t->hot ? 0x171717 : 0x007ad9; u32 border = !en ? 0xb0b0b0 : t->drag ? 0x999999 : t->hot ? 0x171717 : 0x2c628b; fill_rect_px(&s, tl, tt, tr, tb, fill); edge_rect(&s, tl, tt, tr, tb, border, border);
      if (!(n->style & TBS_BOTH) && !(n->style & TBS_NOTICKS)) { /* pointed towards the ticks */ int up = (n->style & TBS_TOP) != 0; int w = tr - tl; for (int k = 0; k < w / 2; k++) { int yy = up ? tt + k : tb - 1 - k; u32 bgp = 0xf0f0f0; if (trk_vert(n)) { } else { int cut = w / 2 - k; for (int q = 0; q < cut; q++) { put_px(&s, tl + q, yy, bgp); put_px(&s, tr - 1 - q, yy, bgp); } put_px(&s, tl + cut, yy, border); put_px(&s, tr - 1 - cut, yy, border); } } } }
    if (n->focus_draw && g_focus == i) { for (int x = 0; x < cw; x += 2) { put_px(&s, x, 0, 0x000000); put_px(&s, x, ch - 1, 0x000000); } for (int y = 0; y < ch; y += 2) { put_px(&s, 0, y, 0x000000); put_px(&s, cw - 1, y, 0x000000); } } }
  ctl_paint_end(c, i, hdc); }
static void trk_notify(Cpu *c, int i, int code, int pos, int sync, u64 ret);
static void tbend_resume(Cpu *c, u64 rax) { (void)rax; Win32 *w = W; u64 *d = w->cont[w->ncont - 1].data; int i = (int)d[0]; u64 ret = d[1]; w32_cont_finish(c, ret); if (!wnd_alive(i) || !g_wnd[i].cc) return; Trackbar *t = (Trackbar *)g_wnd[i].cc; trk_notify(c, i, 8 /* TB_ENDTRACK */, t->pos, 1, ret); }
/* code followed by TB_ENDTRACK, both sent synchronously and in order (comctl32 does the same) */
static void trk_notify2(Cpu *c, int i, int code, int pos, u64 ret) { Wnd *n = &g_wnd[i]; int p = n->parent; if (p < 0 || !wnd_alive(p)) { RET(ret); return; } Win32 *w = W; u32 wm = trk_vert(n) ? 0x115 : 0x114; u64 wp = (u64)(u32)code | ((u64)(u32)(code == 4 || code == 5 ? pos : (u16)pos) << 16); w32_push_cont(c, CONT_TBEND, (u64)i); w->cont[w->ncont - 1].data[1] = ret; w32_guest_call(c, g_wnd[p].wndproc, HWND_OF(p), wm, wp, HWND_OF(i)); }
static void trk_notify(Cpu *c, int i, int code, int pos, int sync, u64 ret) { Wnd *n = &g_wnd[i]; u32 wm = trk_vert(n) ? 0x115 : 0x114; u64 wp = (u64)(u32)code | ((u64)(u32)(u16)pos << 16); if (code == 4 || code == 5) wp = (u64)(u32)code | ((u64)(u32)pos << 16); int p = n->parent; if (p < 0 || !wnd_alive(p)) { if (sync) RET(ret); return; } if (sync) send_to(c, p, wm, wp, HWND_OF(i), CONT_RET_VAL_, ret); else post_msg_to(W, HWND_OF(p), wm, wp, HWND_OF(i)); }
static int trk_clamp(Trackbar *t, int p) { if (p < t->min) p = t->min; if (p > t->max) p = t->max; return p; }
static void s_NebulaTrackbarProc(Cpu *c) { int i = wnd_of(ARG(0)); if (i < 0) { RET(0); return; } SUPER_ADOPT(i, BI_TRACKBAR); u32 msg = (u32)ARG(1); u64 wp = ARG(2), lp = ARG(3); Wnd *n = &g_wnd[i]; Win32 *w = W; Trackbar *t = (Trackbar *)n->cc;
  if (!t && msg != 0x02 && msg != 0x82) { t = n->cc = emu_alloc(sizeof(Trackbar)); if (!t) { RET(0); return; } e_memset(t, 0, sizeof *t); t->min = 0; t->max = 100; t->line = 1; t->page = 20; t->ticfreq = 1; t->thumb_len = 20; t->tipside = 0; }
  switch (msg) {
    case 0x01: RET(0); return;
    case 0x02: case 0x82: { if (t) { emu_free(t); n->cc = 0; } RET(0); return; }
    case 0x0F: trk_paint(c, i, t); RET(0); return;
    case 0x14: RET(1); return;   /* WM_ERASEBKGND: painted in WM_PAINT */
    case 0x400: RET((u32)t->pos); return;   /* TBM_GETPOS */
    case 0x401: RET((u32)t->min); return; case 0x402: RET((u32)t->max); return;
    case 0x403: RET((u32)wp < (u32)t->nt ? (u32)t->tics[wp] : 0xffffffffu); return;   /* TBM_GETTIC */
    case 0x404: { int p = (int)(s32)(u32)lp; if (p < t->min || p > t->max || t->nt >= 64) { RET(0); return; } t->tics[t->nt++] = p; wnd_invalidate(i, 1); RET(1); return; }   /* TBM_SETTIC */
    case 0x405: { int old = t->pos; t->pos = trk_clamp(t, (int)(s32)(u32)lp); if (wp && t->pos != old) wnd_invalidate(i, 1); if (t->posnotify && t->pos != old) { trk_notify(c, i, 4, t->pos, 1, 0); return; } RET(0); return; }   /* TBM_SETPOS */
    case 0x406: { t->min = (s16)(u16)lp; t->max = (s16)(u16)(lp >> 16); if (t->max < t->min) t->max = t->min; t->pos = trk_clamp(t, t->pos); if (wp) wnd_invalidate(i, 1); RET(0); return; }   /* TBM_SETRANGE */
    case 0x407: { t->min = (int)(s32)(u32)lp; if (t->max < t->min) t->max = t->min; t->pos = trk_clamp(t, t->pos); if (wp) wnd_invalidate(i, 1); RET(0); return; }
    case 0x408: { t->max = (int)(s32)(u32)lp; if (t->max < t->min) t->min = t->max; t->pos = trk_clamp(t, t->pos); if (wp) wnd_invalidate(i, 1); RET(0); return; }
    case 0x409: { t->nt = 0; if (wp) wnd_invalidate(i, 1); RET(0); return; }   /* TBM_CLEARTICS */
    case 0x40A: { t->sel0 = (s16)(u16)lp; t->sel1 = (s16)(u16)(lp >> 16); if (wp) wnd_invalidate(i, 1); RET(0); return; }
    case 0x40B: { t->sel0 = (int)(s32)(u32)lp; if (wp) wnd_invalidate(i, 1); RET(0); return; } case 0x40C: { t->sel1 = (int)(s32)(u32)lp; if (wp) wnd_invalidate(i, 1); RET(0); return; }
    case 0x40E: { if (!t->nt) { RET(0); return; } u64 m = w32_heap_alloc(w, (u64)t->nt * 4, 1); for (int k = 0; k < t->nt; k++) wr32(c, m + 4 * (u64)k, (u32)t->tics[k]); RET(m); return; }   /* TBM_GETPTICS */
    case 0x40F: { int k = (int)(u32)wp; if (k < 0 || k >= t->nt) { RET(0xffffffffu); return; } int a0, a1, b0, b1, tk; trk_geom(i, t, &a0, &a1, &b0, &b1, &tk); RET((u32)trk_pos_to_px(t, n, a0, a1, t->tics[k])); return; }
    case 0x410: { if (n->style & TBS_NOTICKS) { RET(0); return; } if (t->nt) { RET((u32)t->nt + 2); return; } int freq = t->ticfreq > 0 ? t->ticfreq : 1; RET((u32)((t->max - t->min) / freq + 1 + (((t->max - t->min) % freq) ? 1 : 0))); return; }   /* TBM_GETNUMTICS */
    case 0x411: RET((u32)t->sel0); return; case 0x412: RET((u32)t->sel1); return;
    case 0x413: { t->sel0 = t->sel1 = 0; if (wp) wnd_invalidate(i, 1); RET(0); return; }
    case 0x414: { t->ticfreq = (int)(u32)wp > 0 ? (int)(u32)wp : 1; wnd_invalidate(i, 1); RET(0); return; }
    case 0x415: { int old = t->page; t->page = (int)(s32)(u32)lp; if (t->page < 0) t->page = (t->max - t->min) / 5; RET((u32)old); return; } case 0x416: RET((u32)t->page); return;
    case 0x417: { int old = t->line; t->line = (int)(s32)(u32)lp; if (t->line < 0) t->line = 1; RET((u32)old); return; } case 0x418: RET((u32)t->line); return;
    case 0x419: { if (lp) { int l, tp, r, b; trk_thumb_rect(i, t, &l, &tp, &r, &b); wr32(c, lp, (u32)l); wr32(c, lp + 4, (u32)tp); wr32(c, lp + 8, (u32)r); wr32(c, lp + 12, (u32)b); } RET(0); return; }   /* TBM_GETTHUMBRECT */
    case 0x41A: { if (lp) { int l, tp, r, b; trk_channel_rect(i, t, &l, &tp, &r, &b); wr32(c, lp, (u32)l); wr32(c, lp + 4, (u32)tp); wr32(c, lp + 8, (u32)r); wr32(c, lp + 12, (u32)b); } RET(0); return; }   /* TBM_GETCHANNELRECT */
    case 0x41B: { t->thumb_len = (int)(u32)wp; if (t->thumb_len < 6) t->thumb_len = 6; wnd_invalidate(i, 1); RET(0); return; } case 0x41C: RET((u32)t->thumb_len); return;
    case 0x41D: { t->tooltip = wp; RET(0); return; } case 0x41E: RET(t->tooltip); return; case 0x41F: { int old = t->tipside; t->tipside = (int)(u32)wp; RET((u32)old); return; }
    case 0x420: { int b = wnd_of(lp); u64 old = wp ? (t->buddy_l > 0 ? HWND_OF(t->buddy_l) : 0) : (t->buddy_r > 0 ? HWND_OF(t->buddy_r) : 0); if (wp) t->buddy_l = b; else t->buddy_r = b; RET(old); return; }   /* TBM_SETBUDDY */
    case 0x421: { int b = wp ? t->buddy_l : t->buddy_r; RET(b > 0 && wnd_alive(b) ? HWND_OF(b) : 0); return; }
    case 0x422: { int old = t->posnotify; t->posnotify = wp != 0; RET((u32)old); return; }   /* TBM_SETPOSNOTIFY */
    case 0x2005: RET(1); return; case 0x2006: RET(1); return;
    case 0x87: RET(1); return;   /* WM_GETDLGCODE: DLGC_WANTARROWS */
    case 0x201: { if (!wnd_enabled_chain(i)) { RET(0); return; } int lx = (s16)(u16)lp, ly = (s16)(u16)(lp >> 16); int tl, tt, tr, tb; trk_thumb_rect(i, t, &tl, &tt, &tr, &tb); int v = trk_vert(n); int a0, a1, b0, b1, tk; trk_geom(i, t, &a0, &a1, &b0, &b1, &tk); int nc0 = w->ncont; set_focus(c, i); int pushed = w->ncont > nc0;
      if (lx >= tl && lx < tr && ly >= tt && ly < tb && !(n->style & TBS_NOTHUMB)) { t->drag = 1; t->drag_off = (v ? ly : lx) - (v ? (tt + tb) / 2 : (tl + tr) / 2); g_capture = i; wnd_invalidate(i, 1); if (!pushed) RET(0); return; }
      /* a click on the channel pages towards the pointer */ int click = v ? ly : lx; int cur = trk_pos_to_px(t, n, a0, a1, t->pos); int rev = (n->style & TBS_REVERSED) != 0; int dir = (click > cur) ^ rev ? 1 : -1; int old = t->pos; t->pos = trk_clamp(t, t->pos + dir * (t->page > 0 ? t->page : 1)); if (t->pos != old) { wnd_invalidate(i, 1); int code = dir > 0 ? 3 : 2; if (pushed) { trk_notify(c, i, code, t->pos, 0, 0); trk_notify(c, i, 8, t->pos, 0, 0); return; } trk_notify2(c, i, code, t->pos, 0); return; } if (!pushed) RET(0); return; }
    case 0x200: { int lx = (s16)(u16)lp, ly = (s16)(u16)(lp >> 16); if (t->drag) { int a0, a1, b0, b1, tk; trk_geom(i, t, &a0, &a1, &b0, &b1, &tk); int np = trk_px_to_pos(t, n, a0, a1, (trk_vert(n) ? ly : lx) - t->drag_off); if (np != t->pos) { t->pos = np; wnd_invalidate(i, 1); trk_notify(c, i, 5 /* TB_THUMBTRACK */, t->pos, 1, 0); return; } RET(0); return; }
      int tl, tt, tr, tb; trk_thumb_rect(i, t, &tl, &tt, &tr, &tb); int hot = lx >= tl && lx < tr && ly >= tt && ly < tb; if (hot != t->hot) { t->hot = hot; wnd_invalidate(i, 1); } RET(0); return; }
    case 0x2A3: { if (t->hot) { t->hot = 0; wnd_invalidate(i, 1); } RET(0); return; }
    case 0x202: { if (!t->drag) { RET(0); return; } t->drag = 0; if (g_capture == i) g_capture = -1; wnd_invalidate(i, 1); int p = n->parent; if (p >= 0 && wnd_alive(p)) { /* NM_RELEASEDCAPTURE, TB_THUMBPOSITION, TB_ENDTRACK */ u64 nm = w32_heap_alloc(w, 24, 1); if (nm) { wrp(c, nm, HWND_OF(i)); wrp(c, nm + PSZ_OF(c), n->id); wr32(c, nm + 2 * PSZ_OF(c), (u32)-16); post_msg_to(w, HWND_OF(p), 0x4E, n->id, nm); } } trk_notify2(c, i, 4, t->pos, 0); return; }
    case 0x1F: { if (t->drag) { t->drag = 0; wnd_invalidate(i, 1); } RET(0); return; }   /* WM_CANCELMODE */
    case 0x100: { int code = -1, np = t->pos; int v = trk_vert(n); int rev = (n->style & TBS_REVERSED) != 0; int dl = (n->style & 0x400) ? -1 : 1; /* TBS_DOWNISLEFT */
      switch (wp) { case 0x25: np -= t->line; code = 0; break; case 0x26: np += v || rev ? -t->line : (dl > 0 ? -t->line : t->line); code = 0; break; case 0x27: np += t->line; code = 1; break; case 0x28: np += v || rev ? t->line : (dl > 0 ? t->line : -t->line); code = 1; break; case 0x21: np -= t->page; code = 2; break; case 0x22: np += t->page; code = 3; break; case 0x24: np = t->min; code = 6; break; case 0x23: np = t->max; code = 7; break; default: break; }
      if (code < 0) { RET(0); return; } np = trk_clamp(t, np); if (np != t->pos) { t->pos = np; wnd_invalidate(i, 1); } trk_notify2(c, i, code, t->pos, 0); return; }
    case 0x20A: { int d = (s16)(u16)(wp >> 16); int np = trk_clamp(t, t->pos + (d > 0 ? -t->line : t->line)); if (np != t->pos) { t->pos = np; wnd_invalidate(i, 1); trk_notify2(c, i, 4, t->pos, 0); return; } RET(0); return; }
    case 0x05: wnd_invalidate(i, 1); RET(0); return;
    default: if (ctl_common(c, i, msg, wp, lp)) return; def_window_proc(c, i, msg, wp, lp); return; } }

/* ---------- up-down ---------- */
#define UDS_WRAP 1
#define UDS_SETBUDDYINT 2
#define UDS_ALIGNRIGHT 4
#define UDS_ALIGNLEFT 8
#define UDS_AUTOBUDDY 0x10
#define UDS_ARROWKEYS 0x20
#define UDS_HORZ 0x40
#define UDS_NOTHOUSANDS 0x80
#define UDS_HOTTRACK 0x100
static UpDown *ud_of(int i) { return i >= 0 && wnd_alive(i) && g_wnd[i].builtin == BI_UPDOWN ? (UpDown *)g_wnd[i].cc : 0; }
static void ud_format(UpDown *u, Wnd *n, int v, char *out, int cap) { if (u->base == 16) { e_snprintf(out, cap, "0x%X", (unsigned)v); return; } char tmp[24]; e_snprintf(tmp, sizeof tmp, "%d", v < 0 ? -v : v); int len = (int)e_strlen(tmp); int o = 0; if (v < 0 && o < cap - 1) out[o++] = '-'; for (int k = 0; k < len && o < cap - 1; k++) { if (!(n->style & UDS_NOTHOUSANDS) && k && (len - k) % 3 == 0 && o < cap - 1) out[o++] = ','; out[o++] = tmp[k]; } out[o] = 0; }
static int ud_parse(UpDown *u, const char *s, int *err) { int neg = 0; long v = 0; int any = 0; *err = 0; while (*s == ' ' || *s == '\t') s++; if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++; if (u->base == 16) { if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2; for (; *s; s++) { int d; if (*s >= '0' && *s <= '9') d = *s - '0'; else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10; else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10; else break; v = v * 16 + d; any = 1; } } else { for (; *s; s++) { if (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); any = 1; } else if (*s == ',' || *s == ' ') continue; else break; } } if (!any) *err = 1; return (int)(neg ? -v : v); }
static int ud_read_buddy(UpDown *u, int *err) { *err = 0; if (u->buddy <= 0 || !wnd_alive(u->buddy)) return u->pos; const char *t = g_wnd[u->buddy].text ? g_wnd[u->buddy].text : ""; return ud_parse(u, t, err); }
static void ud_align_buddy(int i, UpDown *u) { Wnd *n = &g_wnd[i]; int b = u->buddy; if (b <= 0 || !wnd_alive(b) || !(n->style & (UDS_ALIGNRIGHT | UDS_ALIGNLEFT))) return; Wnd *bw = &g_wnd[b]; int w = n->w > 0 ? n->w : 16; if (w > bw->w / 2) w = bw->w / 2; if (n->style & UDS_ALIGNRIGHT) { bw->w -= w; n->x = bw->x + bw->w; } else { n->x = bw->x; bw->x += w; bw->w -= w; } n->y = bw->y; n->w = w; n->h = bw->h; n->ncx = n->ncy = 0; wnd_invalidate(b, 1); wnd_invalidate(i, 1); }
static int ud_wrap(UpDown *u, Wnd *n, int v) { int lo = u->min < u->max ? u->min : u->max, hi = u->min < u->max ? u->max : u->min; if (n->style & UDS_WRAP) { if (hi > lo) { int span = hi - lo + 1; while (v < lo) v += span; while (v > hi) v -= span; } else v = lo; } else { if (v < lo) v = lo; if (v > hi) v = hi; } return v; }
static void ud_paint(Cpu *c, int i, UpDown *u) { Wnd *n = &g_wnd[i]; u64 hdc = ctl_paint_begin(c, i); if (!hdc) return; Surf s; if (dc_surf(c, hdc, &s)) { int cw = wnd_client_w(i), ch = wnd_client_h(i); int en = wnd_enabled_chain(i); int horz = (n->style & UDS_HORZ) != 0;
    for (int k = 1; k <= 2; k++) { int l, t, r, b; if (horz) { l = k == 1 ? 0 : cw / 2; r = k == 1 ? cw / 2 : cw; t = 0; b = ch; } else { l = 0; r = cw; t = k == 1 ? 0 : ch / 2; b = k == 1 ? ch / 2 : ch; }
      int pressed = u->pressed == k, hot = u->hot == k && en; u32 fill = !en ? 0xf0f0f0 : pressed ? 0xcce4f7 : hot ? 0xe5f1fb : 0xe1e1e1; u32 border = !en ? 0xd9d9d9 : pressed ? 0x005499 : hot ? 0x0078d7 : 0xadadad; fill_rect_px(&s, l, t, r, b, fill); edge_rect(&s, l, t, r, b, border, border);
      u32 fg = en ? 0x000000 : 0xa0a0a0; int cx = (l + r) / 2, cy = (t + b) / 2; int sz = ((r - l) < (b - t) ? (r - l) : (b - t)) >= 12 ? 3 : 2;
      /* arrow: k == 1 is up (or left), k == 2 is down (or right) */ for (int q = 0; q <= sz; q++) { if (horz) { int xx = k == 1 ? cx + sz / 2 - q : cx - sz / 2 + q; line_px(&s, xx, cy - (sz - q), xx, cy + (sz - q), fg); } else { int yy = k == 1 ? cy + sz / 2 - q : cy - sz / 2 + q; hline(&s, cx - (sz - q), cx + (sz - q), yy, fg); } } } }
  ctl_paint_end(c, i, hdc); }
static void ud_set_buddy_text_direct(Cpu *c, int i, UpDown *u, int v) { Wnd *n = &g_wnd[i]; int b = u->buddy; if (b <= 0 || !wnd_alive(b) || !(n->style & UDS_SETBUDDYINT)) return; char txt[40]; ud_format(u, n, v, txt, sizeof txt); Wnd *bw = &g_wnd[b]; wnd_set_text(bw, txt); if (bw->builtin == BI_EDIT) { bw->sel0 = bw->sel1 = (int)e_strlen(bw->text); bw->modified = 1; } wnd_invalidate(b, 1); }
static int ud_buddy_is_guest(UpDown *u) { int b = u->buddy; return b > 0 && wnd_alive(b) && (g_wnd[b].builtin == 0 || g_wnd[b].superclassed || g_wnd[b].nsub > 0 || g_wnd[b].wndproc != builtin_slot(g_wnd[b].builtin)); }
/* step: UDN_DELTAPOS to the parent (continuation CONT_UDDELTA), then the position, the buddy text (WM_SETTEXT through the buddy's own procedure when it is subclassed — CONT_UDTEXT), then WM_VSCROLL/WM_HSCROLL SB_THUMBPOSITION; ret = value for the original caller */
static void ud_apply(Cpu *c, int i, UpDown *u, int delta, u64 ret);
static void ud_step(Cpu *c, int i, UpDown *u, int delta, u64 ret) { Wnd *n = &g_wnd[i]; Win32 *w = W; int p = n->parent; if (!(n->style & UDS_SETBUDDYINT) || u->buddy <= 0) { } else { int err; int cur = ud_read_buddy(u, &err); if (!err) u->pos = cur; }
  if (p >= 0 && wnd_alive(p)) { if (!u->nmbuf) u->nmbuf = w32_heap_alloc(w, 48, 1); u64 nm = u->nmbuf; int P = PSZ_OF(c); wrp(c, nm, HWND_OF(i)); wrp(c, nm + P, n->id); wr32(c, nm + 2 * P, (u32)-722); wr32(c, nm + 3 * P, (u32)u->pos); wr32(c, nm + 3 * P + 4, (u32)delta); w32_push_cont(c, CONT_UDDELTA, (u64)i); w->cont[w->ncont - 1].data[1] = (u64)(u32)delta; w->cont[w->ncont - 1].data[2] = ret; w32_guest_call(c, g_wnd[p].wndproc, HWND_OF(p), 0x4E, n->id, nm); return; }
  ud_apply(c, i, u, delta, ret); }
static void ud_apply(Cpu *c, int i, UpDown *u, int delta, u64 ret) { Wnd *n = &g_wnd[i]; Win32 *w = W; u->pos = ud_wrap(u, n, u->pos + delta); int p = n->parent; u32 wm = (n->style & UDS_HORZ) ? 0x114 : 0x115; u64 swp = 4u | ((u64)(u32)(u16)u->pos << 16);
  if ((n->style & UDS_SETBUDDYINT) && u->buddy > 0 && wnd_alive(u->buddy) && ud_buddy_is_guest(u)) { char txt[40]; ud_format(u, n, u->pos, txt, sizeof txt); if (!u->txtbuf) u->txtbuf = w32_heap_alloc(w, 128, 1); u64 tb = u->txtbuf; if (g_wnd[u->buddy].unicode_wnd || !c->mode32) put_w8(c, tb, txt, (int)e_strlen(txt) + 1); else { u8 *pp = mem_ptr(c->mem, tb, 64, 1); if (pp) e_memcpy(pp, txt, e_strlen(txt) + 1); } w32_push_cont(c, CONT_UDTEXT, (u64)i); w->cont[w->ncont - 1].data[1] = ret; w32_guest_call(c, g_wnd[u->buddy].wndproc, HWND_OF(u->buddy), 0x0C, 0, tb); return; }
  ud_set_buddy_text_direct(c, i, u, u->pos); if (p >= 0 && wnd_alive(p)) { send_to(c, p, wm, swp, HWND_OF(i), CONT_RET_VAL_, ret); return; } RET(ret); }
static void uddelta_resume(Cpu *c, u64 rax) { Win32 *w = W; u64 *d = w->cont[w->ncont - 1].data; int i = (int)d[0]; int delta = (int)(s32)(u32)d[1]; u64 ret = d[2]; w32_cont_finish(c, ret); UpDown *u = ud_of(i); if (!u) return; if ((u32)rax != 0) return;   /* the parent refused the change */
  /* NMUPDOWN.iDelta may have been adjusted by the parent */ if (u->nmbuf) delta = (int)(s32)rd32(c, u->nmbuf + 3 * PSZ_OF(c) + 4); ud_apply(c, i, u, delta, ret); }
static void udtext_resume(Cpu *c, u64 rax) { (void)rax; Win32 *w = W; u64 *d = w->cont[w->ncont - 1].data; int i = (int)d[0]; u64 ret = d[1]; w32_cont_finish(c, ret); UpDown *u = ud_of(i); if (!u) return; Wnd *n = &g_wnd[i]; int p = n->parent; u32 wm = (n->style & UDS_HORZ) ? 0x114 : 0x115; if (p >= 0 && wnd_alive(p)) send_to(c, p, wm, 4u | ((u64)(u32)(u16)u->pos << 16), HWND_OF(i), CONT_RET_VAL_, ret); }
static int ud_find_auto_buddy(int i) { Wnd *n = &g_wnd[i]; int best = -1; for (int k = 1; k < MAX_WND; k++) { if (k == i || !wnd_alive(k) || g_wnd[k].parent != n->parent) continue; if (g_wnd[k].builtin == BI_UPDOWN) continue; if (g_wnd[k].cseq >= n->cseq) continue; if (best < 0 || g_wnd[k].cseq > g_wnd[best].cseq) best = k; } return best; }
static int ud_inc_now(UpDown *u) { double held = host_now_ms() - u->press_t; int inc = 1; for (int k = 0; k < u->naccel; k++) if (held >= u->accel[k].sec * 1000.0) inc = (int)u->accel[k].inc; return inc > 0 ? inc : 1; }
/* the buddy edit forwards VK_UP/VK_DOWN here (UDS_ARROWKEYS); returns 1 when consumed */
static int updown_buddy_key(Cpu *c, int edit, u32 vk) { if (vk != 0x26 && vk != 0x28) return 0; for (int k = 1; k < MAX_WND; k++) { UpDown *u = ud_of(k); if (!u || u->buddy != edit || !(g_wnd[k].style & UDS_ARROWKEYS) || !wnd_enabled_chain(k)) continue; u->press_t = host_now_ms(); ud_step(c, k, u, vk == 0x26 ? 1 : -1, 0); return 1; } return 0; }
static void s_NebulaUpDownProc(Cpu *c) { int i = wnd_of(ARG(0)); if (i < 0) { RET(0); return; } SUPER_ADOPT(i, BI_UPDOWN); u32 msg = (u32)ARG(1); u64 wp = ARG(2), lp = ARG(3); Wnd *n = &g_wnd[i]; Win32 *w = W; UpDown *u = (UpDown *)n->cc;
  if (!u && msg != 0x02 && msg != 0x82) { u = n->cc = emu_alloc(sizeof(UpDown)); if (!u) { RET(0); return; } e_memset(u, 0, sizeof *u); u->min = 0; u->max = 100; u->base = 10; u->buddy = -1; u->naccel = 3; u->accel[0].sec = 0; u->accel[0].inc = 1; u->accel[1].sec = 2; u->accel[1].inc = 5; u->accel[2].sec = 5; u->accel[2].inc = 20; if (n->w <= 0) n->w = 16; if (n->style & UDS_AUTOBUDDY) { u->buddy = ud_find_auto_buddy(i); ud_align_buddy(i, u); } }
  switch (msg) {
    case 0x01: RET(0); return;
    case 0x02: case 0x82: { if (u) { if (u->nmbuf) w32_heap_free(w, u->nmbuf); if (u->txtbuf) w32_heap_free(w, u->txtbuf); emu_free(u); n->cc = 0; } host_timer_kill(i, 1); RET(0); return; }
    case 0x0F: ud_paint(c, i, u); RET(0); return;
    case 0x14: RET(1); return;
    case 0x465: { u->max = (s16)(u16)lp; u->min = (s16)(u16)(lp >> 16); RET(0); return; }   /* UDM_SETRANGE: LOWORD = upper, HIWORD = lower */
    case 0x466: RET(((u32)(u16)u->min << 16) | (u16)u->max); return;
    case 0x467: { int old = u->pos; int v = (s16)(u16)lp; u->pos = ud_wrap(u, n, v); ud_set_buddy_text_direct(c, i, u, u->pos); if (ud_buddy_is_guest(u) && (n->style & UDS_SETBUDDYINT) && u->buddy > 0) { char txt[40]; ud_format(u, n, u->pos, txt, sizeof txt); if (!u->txtbuf) u->txtbuf = w32_heap_alloc(w, 128, 1); if (g_wnd[u->buddy].unicode_wnd || !c->mode32) put_w8(c, u->txtbuf, txt, (int)e_strlen(txt) + 1); else { u8 *pp = mem_ptr(c->mem, u->txtbuf, 64, 1); if (pp) e_memcpy(pp, txt, e_strlen(txt) + 1); } send_to(c, u->buddy, 0x0C, 0, u->txtbuf, CONT_RET_VAL_, (u32)(u16)old); return; } RET((u32)(u16)old); return; }   /* UDM_SETPOS */
    case 0x468: { int err; int v = (n->style & UDS_SETBUDDYINT) && u->buddy > 0 ? ud_read_buddy(u, &err) : (err = 0, u->pos); if (!err) u->pos = ud_wrap(u, n, v); RET(((u32)(err ? 1 : 0) << 16) | (u16)u->pos); return; }   /* UDM_GETPOS */
    case 0x469: { int old = u->buddy; u->buddy = wnd_of(wp); ud_align_buddy(i, u); if ((n->style & UDS_SETBUDDYINT) && u->buddy > 0) ud_set_buddy_text_direct(c, i, u, u->pos); RET(old > 0 && wnd_alive(old) ? HWND_OF(old) : 0); return; }
    case 0x46A: RET(u->buddy > 0 && wnd_alive(u->buddy) ? HWND_OF(u->buddy) : 0); return;
    case 0x46B: { int cnt = (int)(u32)wp; if (cnt > 8) cnt = 8; if (cnt < 0) cnt = 0; u->naccel = cnt; for (int k = 0; k < cnt; k++) { u->accel[k].sec = rd32(c, lp + 8 * (u64)k); u->accel[k].inc = rd32(c, lp + 8 * (u64)k + 4); } RET(1); return; }
    case 0x46C: { int cnt = (int)(u32)wp; if (lp) for (int k = 0; k < cnt && k < u->naccel; k++) { wr32(c, lp + 8 * (u64)k, u->accel[k].sec); wr32(c, lp + 8 * (u64)k + 4, u->accel[k].inc); } RET((u32)u->naccel); return; }
    case 0x46D: { int old = u->base; if (wp == 10 || wp == 16) { u->base = (int)wp; ud_set_buddy_text_direct(c, i, u, u->pos); } RET((u32)old); return; } case 0x46E: RET((u32)u->base); return;
    case 0x46F: { u->min = (int)(s32)(u32)wp; u->max = (int)(s32)(u32)lp; RET(0); return; }   /* UDM_SETRANGE32 */
    case 0x470: { if (wp) wr32(c, wp, (u32)u->min); if (lp) wr32(c, lp, (u32)u->max); RET(0); return; }
    case 0x471: { int old = u->pos; u->pos = ud_wrap(u, n, (int)(s32)(u32)lp); ud_set_buddy_text_direct(c, i, u, u->pos); if (ud_buddy_is_guest(u) && (n->style & UDS_SETBUDDYINT) && u->buddy > 0) { char txt[40]; ud_format(u, n, u->pos, txt, sizeof txt); if (!u->txtbuf) u->txtbuf = w32_heap_alloc(w, 128, 1); if (g_wnd[u->buddy].unicode_wnd || !c->mode32) put_w8(c, u->txtbuf, txt, (int)e_strlen(txt) + 1); else { u8 *pp = mem_ptr(c->mem, u->txtbuf, 64, 1); if (pp) e_memcpy(pp, txt, e_strlen(txt) + 1); } send_to(c, u->buddy, 0x0C, 0, u->txtbuf, CONT_RET_VAL_, (u32)old); return; } RET((u32)old); return; }   /* UDM_SETPOS32 */
    case 0x472: { int err; int v = (n->style & UDS_SETBUDDYINT) && u->buddy > 0 ? ud_read_buddy(u, &err) : (err = 0, u->pos); if (!err) u->pos = ud_wrap(u, n, v); if (lp) wr32(c, lp, err ? 1 : 0); RET((u32)u->pos); return; }
    case 0x2005: { int old = u->unicode; u->unicode = wp != 0; RET((u32)old); return; } case 0x2006: RET((u32)u->unicode); return;
    case 0x201: case 0x203: { if (!wnd_enabled_chain(i)) { RET(0); return; } int lx = (s16)(u16)lp, ly = (s16)(u16)(lp >> 16); int cw = wnd_client_w(i), ch = wnd_client_h(i); int k = (n->style & UDS_HORZ) ? (lx < cw / 2 ? 1 : 2) : (ly < ch / 2 ? 1 : 2); u->pressed = k; u->press_t = host_now_ms(); g_capture = i; wnd_invalidate(i, 1); host_timer_set(i, 1, 500); if (u->buddy > 0 && wnd_alive(u->buddy) && g_wnd[u->buddy].builtin == BI_EDIT) { int nc0 = w->ncont; set_focus(c, u->buddy); if (w->ncont > nc0) { u->pending_delta = k == 1 ? 1 : -1; return; } } ud_step(c, i, u, k == 1 ? 1 : -1, 0); return; }
    case 0x113: { if (!u->pressed) { host_timer_kill(i, 1); RET(0); return; } int lx = g_mouse_x, ly = g_mouse_y; int sx, sy; wnd_client_origin(i, &sx, &sy); lx -= sx; ly -= sy; int cw = wnd_client_w(i), ch = wnd_client_h(i); int k = (n->style & UDS_HORZ) ? (lx < cw / 2 ? 1 : 2) : (ly < ch / 2 ? 1 : 2); int inside = lx >= 0 && ly >= 0 && lx < cw && ly < ch && k == u->pressed; host_timer_set(i, 1, 60); int d = u->pending_delta; u->pending_delta = 0; if (d) { ud_step(c, i, u, d, 0); return; } if (!inside) { RET(0); return; } int inc = ud_inc_now(u); ud_step(c, i, u, u->pressed == 1 ? inc : -inc, 0); return; }
    case 0x202: { if (!u->pressed) { RET(0); return; } u->pressed = 0; if (g_capture == i) g_capture = -1; host_timer_kill(i, 1); wnd_invalidate(i, 1); int p = n->parent; if (p >= 0 && wnd_alive(p)) { u64 nm = w32_heap_alloc(w, 24, 1); if (nm) { wrp(c, nm, HWND_OF(i)); wrp(c, nm + PSZ_OF(c), n->id); wr32(c, nm + 2 * PSZ_OF(c), (u32)-16); post_msg_to(w, HWND_OF(p), 0x4E, n->id, nm); } post_msg_to(w, HWND_OF(p), (n->style & UDS_HORZ) ? 0x114 : 0x115, 8 /* SB_ENDSCROLL */, HWND_OF(i)); } int d = u->pending_delta; u->pending_delta = 0; if (d) { ud_step(c, i, u, d, 0); return; }   /* the press only moved the focus into the buddy (a notification chain was running): step now */ RET(0); return; }
    case 0x200: { int lx = (s16)(u16)lp, ly = (s16)(u16)(lp >> 16); int cw = wnd_client_w(i), ch = wnd_client_h(i); int k = lx < 0 || ly < 0 || lx >= cw || ly >= ch ? 0 : (n->style & UDS_HORZ) ? (lx < cw / 2 ? 1 : 2) : (ly < ch / 2 ? 1 : 2); if (k != u->hot) { u->hot = k; wnd_invalidate(i, 1); } RET(0); return; }
    case 0x2A3: { if (u->hot) { u->hot = 0; wnd_invalidate(i, 1); } RET(0); return; }
    case 0x1F: { if (u->pressed) { u->pressed = 0; host_timer_kill(i, 1); wnd_invalidate(i, 1); } RET(0); return; }
    case 0x05: wnd_invalidate(i, 1); RET(0); return;
    default: if (ctl_common(c, i, msg, wp, lp)) return; def_window_proc(c, i, msg, wp, lp); return; } }
