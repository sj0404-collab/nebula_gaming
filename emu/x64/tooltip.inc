/* ---------- tooltips_class32 ----------
   Tools are registered with TTM_ADDTOOL (a window with TTF_IDISHWND or a rectangle of a window's client area; text inline,
   a string resource of hinst, or LPSTR_TEXTCALLBACK answered through WM_NOTIFY TTN_GETDISPINFO to the tool's window).
   Hover tracking is global (tip_tick from the message pump): the pointer resting on a tool for the initial delay posts a
   WM_TIMER to the tooltip window, whose procedure fetches the text (continuation CONT_TTDISP when it is a callback) and
   shows the popup below the cursor; it hides when the pointer leaves the tool, a button goes down or the autopop time
   passes.  Toolbars with TBSTYLE_TOOLTIPS own such a window (TB_GETTOOLTIPS) whose tools are their buttons: the toolbar
   receives TTN_GETDISPINFO, forwards it to its parent like comctl32 and falls back to the button's own text. */
#define TT_MAX 96
#define TT_CB 0xffffffffffffffffull
typedef struct { int used; u32 flags; u64 hwnd, id; int l, t, r, b; u64 hinst; u64 text_ptr; u64 lparam; char text[256]; } TtTool;
typedef struct { TtTool t[TT_MAX]; int n; int active; int maxw; int unicode; int tb;   /* owner toolbar window index (virtual tools = its buttons) or -1 */
  int hover; double hover_t; int hx, hy; int shown, cur; double shown_t; double last_hide_t; int pending; int track; int tx, ty;
  u32 bk, fg; int bk_set, fg_set; char title[80]; char text[1024]; int d_init, d_auto, d_reshow; int ml, mt, mr, mb; u64 nmbuf; } Tooltip;

static int tt_load_string(Cpu *c, u64 hmod, u32 id, u64 *str, u32 *len) { u64 e = rsrc_lookup(c, hmod, (id >> 4) + 1, 6, 1, 0); if (!e) return 0; u64 base = hmod ? hmod : W->image_base; u64 p = base + rd32(c, e);
  for (u32 k = 0; k < 16; k++) { u16 l = rd16(c, p); p += 2; if (k == (id & 15)) { *str = p; *len = l; return 1; } p += (u64)l * 2; } return 0; }
static Tooltip *tt_of(int i) { return i >= 0 && wnd_alive(i) && g_wnd[i].builtin == BI_TOOLTIP ? (Tooltip *)g_wnd[i].cc : 0; }
static Tooltip *tt_ensure(int i) { Wnd *n = &g_wnd[i]; if (!n->cc) { Tooltip *tt = emu_alloc(sizeof(Tooltip)); if (!tt) return 0; e_memset(tt, 0, sizeof *tt); tt->active = 1; tt->maxw = -1; tt->tb = -1; tt->hover = -1; tt->cur = -1; tt->d_init = 500; tt->d_auto = 5000; tt->d_reshow = 100; tt->ml = tt->mt = tt->mr = tt->mb = 3; tt->unicode = n->unicode_wnd; n->cc = tt; } return n->cc; }
/* TOOLINFO: x64 cbSize@0 uFlags@4 hwnd@8 uId@16 rect@24 hinst@40 lpszText@48 lParam@56 lpReserved@64; x86 0 4 8 12 16 32 36 40 44 */
static void tt_ti_read(Cpu *c, u64 ti, TtTool *t, int wide, u32 *cb) { int m = c->mode32; *cb = rd32(c, ti); t->flags = rd32(c, ti + 4); t->hwnd = rdp(c, ti + 8); t->id = rdp(c, ti + (m ? 12 : 16)); u64 r = ti + (m ? 16 : 24); t->l = (s32)rd32(c, r); t->t = (s32)rd32(c, r + 4); t->r = (s32)rd32(c, r + 8); t->b = (s32)rd32(c, r + 12);
  u32 min = m ? 40 : 56; if (*cb >= min) { t->hinst = rdp(c, ti + (m ? 32 : 40)); t->text_ptr = rdp(c, ti + (m ? 36 : 48)); } else { t->hinst = 0; t->text_ptr = 0; }
  if (*cb >= (u32)(m ? 44 : 64)) t->lparam = rdp(c, ti + (m ? 40 : 56)); else t->lparam = 0;
  t->text[0] = 0; if (t->text_ptr == TT_CB || t->text_ptr == 0xffffffffull) { t->text_ptr = TT_CB; } else if (t->text_ptr >= 0x10000) { text_in(c, t->text_ptr, wide, t->text, sizeof t->text); t->text_ptr = 1; /* literal, copied */ } else if (t->text_ptr && t->hinst) { u64 s; u32 len; if (tt_load_string(c, t->hinst, (u32)t->text_ptr, &s, &len)) { wstr_to_utf8(c, s, t->text, sizeof t->text, (int)len); } t->text_ptr = 1; } }
static void tt_ti_write(Cpu *c, u64 ti, const TtTool *t, int wide) { int m = c->mode32; u32 cb = rd32(c, ti); wr32(c, ti + 4, t->flags); wrp(c, ti + 8, t->hwnd); wrp(c, ti + (m ? 12 : 16), t->id); u64 r = ti + (m ? 16 : 24); wr32(c, r, (u32)t->l); wr32(c, r + 4, (u32)t->t); wr32(c, r + 8, (u32)t->r); wr32(c, r + 12, (u32)t->b);
  if (cb >= (u32)(m ? 40 : 56)) { wrp(c, ti + (m ? 32 : 40), t->hinst); u64 tp = rdp(c, ti + (m ? 36 : 48)); if (t->text_ptr == TT_CB) wrp(c, ti + (m ? 36 : 48), c->mode32 ? 0xffffffffull : TT_CB); else if (tp >= 0x10000) text_out_cap(c, tp, t->text, 256, wide); }
  if (cb >= (u32)(m ? 44 : 64)) wrp(c, ti + (m ? 40 : 56), t->lparam); }
static int tt_find(Tooltip *tt, u64 hwnd, u64 id) { for (int k = 0; k < TT_MAX; k++) if (tt->t[k].used && tt->t[k].hwnd == hwnd && tt->t[k].id == id) return k; return -1; }
static int tt_is_desc(int i, int anc) { for (int k = i; k >= 0; k = g_wnd[k].parent) if (k == anc) return 1; return 0; }
/* the tool under the framebuffer point, or -1; TT_MAX + k = button k of the owner toolbar */
static int tt_tool_at(Tooltip *tt, int fx, int fy) { int hit = hit_test(fx, fy); if (hit < 0) return -1;
  if (tt->tb >= 0) { if (hit != tt->tb || !wnd_alive(tt->tb)) return -1; Toolbar *tb = g_wnd[tt->tb].cc; if (!tb) return -1; int cx, cy; wnd_client_origin(tt->tb, &cx, &cy); int k = tb_hit(tb, fx - cx, fy - cy, tb->bh); return k >= 0 ? TT_MAX + k : -1; }
  for (int k = 0; k < TT_MAX; k++) { TtTool *t = &tt->t[k]; if (!t->used) continue;
    if (t->flags & 1) { int wi = wnd_of(t->id); if (wi < 0 || !g_wnd[wi].visible || !tt_is_desc(hit, wi)) continue; return k; }
    int wi = wnd_of(t->hwnd); if (wi < 0 || !g_wnd[wi].visible || !tt_is_desc(hit, wi)) continue; int cx, cy; wnd_client_origin(wi, &cx, &cy); int lx = fx - cx, ly = fy - cy; if (lx >= t->l && lx < t->r && ly >= t->t && ly < t->b) return k; }
  return -1; }
static void tt_uncover(int i) { Wnd *n = &g_wnd[i]; for (int k = 0; k < MAX_WND; k++) { Wnd *o = &g_wnd[k]; if (!o->used || o->destroyed || !o->visible || o->parent >= 0 || k == i) continue; if (n->x < o->x + o->w && n->x + n->w > o->x && n->y < o->y + o->h && n->y + n->h > o->y) wnd_invalidate(k, 1); } }
static void tt_hide(Win32 *w, int i) { Tooltip *tt = tt_of(i); if (!tt) return; if (tt->shown) { g_wnd[i].visible = 0; tt->shown = 0; tt->cur = -1; tt->last_hide_t = host_now_ms(); tt_uncover(i); w->dirty = 1; } tt->pending = 0; }
/* message-pump hook: hover state machine for every tooltip window of the process */
static void tip_tick(Win32 *w) { double now = host_now_ms();
  for (int i = 1; i < MAX_WND; i++) { Tooltip *tt = tt_of(i); if (!tt || g_wnd[i].pid != w->pid || tt->track) continue;
    int k = tt->active ? tt_tool_at(tt, g_mouse_x, g_mouse_y) : -1;
    if (g_mouse_btn) { tt_hide(w, i); tt->hover = -1; continue; }
    if (k != tt->hover) { tt->hover = k; tt->hover_t = now; tt->hx = g_mouse_x; tt->hy = g_mouse_y; if (tt->shown && k != tt->cur) tt_hide(w, i); continue; }
    if (k < 0) continue;
    if (tt->shown) { if (now - tt->shown_t > tt->d_auto) tt_hide(w, i); continue; }
    int delay = now - tt->last_hide_t < 1000 ? tt->d_reshow : tt->d_init; if (!tt->pending && now - tt->hover_t >= delay) { tt->pending = 1; post_msg_to(w, HWND_OF(i), 0x113, 0x7301, 0); } } }
/* lay the text out: lines split on \n, wrapped at maxw when set; returns the bubble size */
static int tt_layout(Win32 *w, int i, Tooltip *tt, int *lines, int *nl, int *tw, int *th) { Wnd *n = &g_wnd[i]; u64 f = n->font ? n->font : g_dlg_font; int cw, fh; font_dims(w, f, &cw, &fh); int maxw = tt->maxw > 0 ? tt->maxw : 0; int cnt = 0, wmax = 0; char *s = tt->text; int p = 0, len = (int)e_strlen(s);
  while (p <= len && cnt < 32) { int e = p; while (e < len && s[e] != '\n') e++; int q = e; if (q > p && s[q - 1] == '\r') q--; int start = p;
    while (cnt < 32) { int l = q - start; if (maxw && text_width(w, f, s + start, l) > maxw) { int cut = -1; int acc = 0; for (int x = start; x < q; x++) { if (s[x] == ' ') { int ww = text_width(w, f, s + start, x - start); if (ww > maxw) break; cut = x; } acc++; } if (cut < 0) { int ch = maxw / (cw ? cw : 7); if (ch < 1) ch = 1; cut = start + ch; if (cut > q) cut = q; } l = cut - start; lines[cnt * 2] = start; lines[cnt * 2 + 1] = l; cnt++; int ww = text_width(w, f, s + start, l); if (ww > wmax) wmax = ww; start = cut; while (start < q && s[start] == ' ') start++; if (start >= q) break; continue; }
      lines[cnt * 2] = start; lines[cnt * 2 + 1] = l; cnt++; int ww = text_width(w, f, s + start, l); if (ww > wmax) wmax = ww; break; }
    p = e + 1; if (e >= len) break; }
  if (cnt == 0) { lines[0] = 0; lines[1] = 0; cnt = 1; }
  int titlew = tt->title[0] ? text_width(w, f, tt->title, (int)e_strlen(tt->title)) : 0; if (titlew > wmax) wmax = titlew;
  *nl = cnt; *tw = wmax + tt->ml + tt->mr + 2; *th = cnt * fh + (tt->title[0] ? fh + 2 : 0) + tt->mt + tt->mb + 2; return fh; }
static void tt_show_now(Cpu *c, int i) { Win32 *w = W; Tooltip *tt = tt_of(i); if (!tt) return; Wnd *n = &g_wnd[i]; int lines[64], nl, tw, th; tt_layout(w, i, tt, lines, &nl, &tw, &th); if (!tt->text[0] && !tt->title[0]) { tt->pending = 0; return; }
  int x, y; if (tt->track) { x = tt->tx - SCREEN_X0; y = tt->ty - SCREEN_Y0; if (!(tt->cur >= 0 && tt->cur < TT_MAX && (tt->t[tt->cur].flags & 0x80))) y -= th; if (tt->cur >= 0 && tt->cur < TT_MAX && (tt->t[tt->cur].flags & 2)) x -= tw / 2; }
  else if (tt->cur >= 0 && tt->cur < TT_MAX && (tt->t[tt->cur].flags & 2 /* TTF_CENTERTIP */) && !(tt->t[tt->cur].flags & 1)) { int wi = wnd_of(tt->t[tt->cur].hwnd); int cx = 0, cy = 0; if (wi >= 0) wnd_client_origin(wi, &cx, &cy); TtTool *t = &tt->t[tt->cur]; x = cx + (t->l + t->r) / 2 - tw / 2; y = cy + t->b + 2; }
  else { x = g_mouse_x; y = g_mouse_y + 21; }
  if (x + tw > w->screen_w) x = w->screen_w - tw; if (x < 0) x = 0; if (y + th > w->screen_h) y = g_mouse_y - th - 2; if (y < 0) y = 0;
  n->x = x; n->y = y; n->w = tw; n->h = th; n->ncx = n->ncy = 0; n->visible = 1; n->invalid = 1; n->erase = 1; n->zseq = ++g_zseq; tt->shown = 1; tt->shown_t = host_now_ms(); tt->pending = 0; w->dirty = 1; (void)c; }
static void tt_paint(Cpu *c, int i) { Win32 *w = W; Wnd *n = &g_wnd[i]; Tooltip *tt = n->cc; u64 hdc = ctl_paint_begin(c, i); if (!hdc) { RET(0); return; } Surf s; if (dc_surf(c, hdc, &s) && tt) { GdiObj *d = cc_dc(w, hdc, n); int lines[64], nl, tw, th; int fh = tt_layout(w, i, tt, lines, &nl, &tw, &th);
    u32 bk = tt->bk_set ? cr2px(tt->bk) : 0xffffff, fg = tt->fg_set ? cr2px(tt->fg) : 0x000000; fill_rect_px(&s, 0, 0, n->w, n->h, bk); edge_rect(&s, 0, 0, n->w, n->h, 0x767676, 0x767676);
    if (d) { int y = 1 + tt->mt; if (tt->title[0]) { cc_text(&s, d, w, 1 + tt->ml, y, tt->title, fg); hline(&s, 1 + tt->ml, n->w - 2 - tt->mr, y + fh, 0xd0d0d0); y += fh + 2; } for (int k = 0; k < nl; k++) { char line[1024]; int l = lines[k * 2 + 1]; if (l > (int)sizeof line - 1) l = sizeof line - 1; e_memcpy(line, tt->text + lines[k * 2], l); line[l] = 0; cc_text(&s, d, w, 1 + tt->ml, y, line, fg); y += fh; } } }
  ctl_paint_end(c, i, hdc); RET(0); }
/* text of the hovered tool: literal → show; callback → TTN_GETDISPINFO to the tool window (CONT_TTDISP: data[0]=i data[1]=nmbuf data[2]=wide) */
static void tt_fetch_and_show(Cpu *c, int i, int k) { Win32 *w = W; Tooltip *tt = tt_of(i); if (!tt) { RET(0); return; } tt->cur = k; u64 P = cc_P(c); int wide = tt->unicode; u64 owner_h = 0, id = 0, lparam = 0; u32 flags = 0;
  if (k >= TT_MAX) { if (tt->tb < 0 || !wnd_alive(tt->tb)) { RET(0); return; } Toolbar *tb = g_wnd[tt->tb].cc; int b = k - TT_MAX; if (!tb || b < 0 || b >= tb->n) { RET(0); return; } owner_h = HWND_OF(tt->tb); id = tb->b[b].id; lparam = tb->b[b].data; }
  else { TtTool *t = &tt->t[k]; if (!t->used) { RET(0); return; } if (t->text_ptr != TT_CB) { e_snprintf(tt->text, sizeof tt->text, "%s", t->text); tt_show_now(c, i); RET(0); return; } owner_h = t->hwnd; id = t->id; lparam = t->lparam; flags = t->flags; }
  int oi = wnd_of(owner_h); if (oi < 0 || !g_wnd[oi].wndproc) { RET(0); return; }
  /* NMTTDISPINFO: hdr | lpszText | szText[80] | hinst | uFlags | lParam */ u64 sz = c->mode32 ? (wide ? 188 : 108) : (wide ? 216 : 136); if (!tt->nmbuf) tt->nmbuf = w32_heap_alloc(w, 256, 1); if (!tt->nmbuf) { RET(0); return; } u64 b = tt->nmbuf; u8 *z = mem_ptr(c->mem, b, 256, 1); if (z) e_memset(z, 0, 256);
  nm_hdr(c, b, i, wide ? (u32)-530 : (u32)-520); wrp(c, b + P, id); u64 sztext = b + 3 * P + P; wrp(c, b + 3 * P, sztext); u64 after = sztext + (wide ? 160 : 80); after = (after + P - 1) & ~(P - 1); wrp(c, after, 0); wr32(c, after + P, flags); wrp(c, after + P + P, lparam); (void)sz;
  w32_push_cont(c, CONT_TTDISP, (u64)i); w->cont[w->ncont - 1].data[1] = b; w->cont[w->ncont - 1].data[2] = (u64)wide; w->cont[w->ncont - 1].data[3] = (u64)k; w32_guest_call(c, g_wnd[oi].wndproc, owner_h, 0x4E, id, b); }
static void ttdisp_resume(Cpu *c, u64 rax) { Win32 *w = W; int ci = w->ncont - 1; int i = (int)w->cont[ci].data[0]; u64 b = w->cont[ci].data[1]; int wide = (int)w->cont[ci].data[2]; int k = (int)w->cont[ci].data[3]; (void)rax; Tooltip *tt = tt_of(i); if (!tt) { w32_cont_finish(c, 0); return; } u64 P = cc_P(c);
  u64 sztext = b + 4 * P; u64 after = sztext + (wide ? 160 : 80); after = (after + P - 1) & ~(P - 1); u64 lpsz = rdp(c, b + 3 * P), hinst = rdp(c, after); u32 fl = rd32(c, after + P); tt->text[0] = 0;
  if (lpsz == sztext || lpsz == 0) { if (lpsz) text_in(c, sztext, wide, tt->text, sizeof tt->text); } else if (lpsz == TT_CB || lpsz == 0xffffffffull) { } else if (lpsz >= 0x10000) text_in(c, lpsz, wide, tt->text, sizeof tt->text); else if (hinst) { u64 s; u32 len; if (tt_load_string(c, hinst, (u32)lpsz, &s, &len)) wstr_to_utf8(c, s, tt->text, sizeof tt->text, (int)len); }
  if ((fl & 0x8000 /* TTF_DI_SETITEM */) && k >= 0 && k < TT_MAX && tt->t[k].used) { e_snprintf(tt->t[k].text, sizeof tt->t[k].text, "%s", tt->text); tt->t[k].text_ptr = 1; }
  if (tt->hover == k || tt->track) tt_show_now(c, i); else tt->pending = 0; w32_cont_finish(c, 0); }
static void s_NebulaTooltipProc(Cpu *c) { int i = wnd_of(ARG(0)); if (i < 0) { RET(0); return; } SUPER_ADOPT(i, BI_TOOLTIP); u32 msg = (u32)ARG(1); u64 wp = ARG(2), lp = ARG(3); Wnd *n = &g_wnd[i]; Win32 *w = W; Tooltip *tt = msg == 0x02 || msg == 0x82 ? n->cc : tt_ensure(i); if (!tt && msg != 0x02 && msg != 0x82) { RET(0); return; }
  int wide = msg >= 0x432 && msg <= 0x43B; (void)wide;
  switch (msg) {
    case 0x01: { n->visible = 0; n->style = (n->style & ~0x10000000u) | 0x80000000u; /* created hidden; WS_POPUP */ RET(0); return; }
    case 0x02: { if (tt && tt->shown) tt_hide(w, i); RET(0); return; }
    case 0x0F: tt_paint(c, i); return;
    case 0x14: RET(1); return;
    case 0x113: { if (wp == 0x7301) { if (tt->pending && tt->hover >= 0 && !tt->shown) { tt_fetch_and_show(c, i, tt->hover); return; } tt->pending = 0; } RET(0); return; }
    case 0x21: RET(3 /* MA_NOACTIVATE */); return;
    case 0x401: { /* TTM_ACTIVATE */ tt->active = wp != 0; if (!tt->active) tt_hide(w, i); RET(0); return; }
    case 0x403: { /* TTM_SETDELAYTIME */ int ms = (int)(s16)(u16)lp; switch ((u32)wp) { case 0: tt->d_init = ms < 0 ? 500 : ms; tt->d_auto = ms < 0 ? 5000 : ms * 10; tt->d_reshow = ms < 0 ? 100 : ms / 5; break; case 1: tt->d_reshow = ms < 0 ? 100 : ms; break; case 2: tt->d_auto = ms < 0 ? 5000 : ms; break; case 3: tt->d_init = ms < 0 ? 500 : ms; break; } RET(0); return; }
    case 0x415: { u32 wh = (u32)wp; RET((u64)(u32)(wh == 1 ? tt->d_reshow : wh == 2 ? tt->d_auto : tt->d_init)); return; }
    case 0x404: case 0x432: { /* TTM_ADDTOOL A/W */ if (!lp) { RET(0); return; } TtTool t; u32 cb; e_memset(&t, 0, sizeof t); tt_ti_read(c, lp, &t, msg == 0x432, &cb); int k = tt_find(tt, t.hwnd, t.id); if (k < 0) { for (int q = 0; q < TT_MAX; q++) if (!tt->t[q].used) { k = q; break; } } if (k < 0) { RET(0); return; } t.used = 1; tt->t[k] = t; if (k >= tt->n) tt->n = k + 1; RET(1); return; }
    case 0x405: case 0x433: { /* TTM_DELTOOL */ if (!lp) { RET(0); return; } TtTool t; u32 cb; tt_ti_read(c, lp, &t, msg == 0x433, &cb); int k = tt_find(tt, t.hwnd, t.id); if (k >= 0) { if (tt->cur == k) tt_hide(w, i); tt->t[k].used = 0; if (tt->hover == k) tt->hover = -1; } RET(0); return; }
    case 0x406: case 0x434: { /* TTM_NEWTOOLRECT */ if (!lp) { RET(0); return; } TtTool t; u32 cb; tt_ti_read(c, lp, &t, msg == 0x434, &cb); int k = tt_find(tt, t.hwnd, t.id); if (k >= 0) { tt->t[k].l = t.l; tt->t[k].t = t.t; tt->t[k].r = t.r; tt->t[k].b = t.b; } RET(0); return; }
    case 0x407: RET(0); return;   /* TTM_RELAYEVENT: hover tracking is global */
    case 0x408: case 0x435: { /* TTM_GETTOOLINFO */ if (!lp) { RET(0); return; } TtTool t; u32 cb; tt_ti_read(c, lp, &t, msg == 0x435, &cb); int k = tt_find(tt, t.hwnd, t.id); if (k < 0) { RET(0); return; } tt_ti_write(c, lp, &tt->t[k], msg == 0x435); RET(1); return; }
    case 0x409: case 0x436: { /* TTM_SETTOOLINFO */ if (!lp) { RET(0); return; } TtTool t; u32 cb; tt_ti_read(c, lp, &t, msg == 0x436, &cb); int k = tt_find(tt, t.hwnd, t.id); if (k >= 0) { t.used = 1; tt->t[k] = t; if (tt->cur == k && tt->shown) { e_snprintf(tt->text, sizeof tt->text, "%s", t.text); wnd_invalidate(i, 1); } } RET(0); return; }
    case 0x40A: case 0x437: { /* TTM_HITTEST: TTHITTESTINFO { hwnd; POINT; TOOLINFO } */ if (!lp) { RET(0); return; } u64 P = cc_P(c); u64 hw = rdp(c, lp); int px = (s32)rd32(c, lp + P), py = (s32)rd32(c, lp + P + 4); int wi = wnd_of(hw); if (wi < 0) { RET(0); return; } int cx, cy; wnd_client_origin(wi, &cx, &cy); for (int k = 0; k < TT_MAX; k++) { TtTool *t = &tt->t[k]; if (!t->used) continue; int in = 0; if (t->flags & 1) in = wnd_of(t->id) == wi; else in = t->hwnd == hw && px >= t->l && px < t->r && py >= t->t && py < t->b; if (in) { tt_ti_write(c, lp + P + 8, t, msg == 0x437); RET(1); return; } } (void)cx; (void)cy; RET(0); return; }
    case 0x40B: case 0x438: { /* TTM_GETTEXT */ if (!lp) { RET(0); return; } TtTool t; u32 cb; tt_ti_read(c, lp, &t, msg == 0x438, &cb); int k = tt_find(tt, t.hwnd, t.id); u64 tp = rdp(c, lp + (c->mode32 ? 36 : 48)); u32 cap = msg == 0x438 && wp ? (u32)wp : 80; if (k >= 0 && tp >= 0x10000) text_out_cap(c, tp, tt->t[k].text_ptr == TT_CB ? "" : tt->t[k].text, cap, msg == 0x438); RET(0); return; }
    case 0x40C: case 0x439: { /* TTM_UPDATETIPTEXT */ if (!lp) { RET(0); return; } TtTool t; u32 cb; tt_ti_read(c, lp, &t, msg == 0x439, &cb); int k = tt_find(tt, t.hwnd, t.id); if (k >= 0) { tt->t[k].hinst = t.hinst; tt->t[k].text_ptr = t.text_ptr; e_snprintf(tt->t[k].text, sizeof tt->t[k].text, "%s", t.text); if (tt->shown && tt->cur == k) { if (t.text_ptr == TT_CB) tt_hide(w, i); else { e_snprintf(tt->text, sizeof tt->text, "%s", t.text); tt_show_now(c, i); } } } RET(0); return; }
    case 0x40D: { int cnt = 0; for (int k = 0; k < TT_MAX; k++) if (tt->t[k].used) cnt++; RET((u64)(u32)cnt); return; }   /* TTM_GETTOOLCOUNT */
    case 0x40E: case 0x43A: { /* TTM_ENUMTOOLS */ int idx = (int)wp, seen = 0; for (int k = 0; k < TT_MAX && lp; k++) if (tt->t[k].used) { if (seen == idx) { tt_ti_write(c, lp, &tt->t[k], msg == 0x43A); RET(1); return; } seen++; } RET(0); return; }
    case 0x40F: case 0x43B: { /* TTM_GETCURRENTTOOL */ int k = tt->shown ? tt->cur : -1; if (k < 0 || k >= TT_MAX) { RET(0); return; } if (lp) tt_ti_write(c, lp, &tt->t[k], msg == 0x43B); RET(1); return; }
    case 0x410: { /* TTM_WINDOWFROMPOINT */ int px = (s32)rd32(c, lp), py = (s32)rd32(c, lp + 4); int h = hit_test(px - SCREEN_X0, py - SCREEN_Y0); RET(h >= 0 ? HWND_OF(h) : 0); return; }
    case 0x411: { /* TTM_TRACKACTIVATE */ if (wp && lp) { TtTool t; u32 cb; tt_ti_read(c, lp, &t, tt->unicode, &cb); int k = tt_find(tt, t.hwnd, t.id); if (k < 0) { RET(0); return; } tt->track = 1; tt->cur = k; tt_fetch_and_show(c, i, k); return; } tt->track = 0; tt_hide(w, i); RET(0); return; }
    case 0x412: { tt->tx = (s16)(u16)lp; tt->ty = (s16)(u16)(lp >> 16); if (tt->track && tt->shown) tt_show_now(c, i); RET(0); return; }   /* TTM_TRACKPOSITION (screen coords) */
    case 0x413: tt->bk = (u32)wp; tt->bk_set = 1; RET(0); return; case 0x414: tt->fg = (u32)wp; tt->fg_set = 1; RET(0); return; case 0x416: RET(tt->bk_set ? tt->bk : 0xffffff); return; case 0x417: RET(tt->fg_set ? tt->fg : 0); return;
    case 0x418: { int old = tt->maxw; tt->maxw = (int)(s32)(u32)lp; RET((u64)(s64)old); return; } case 0x419: RET((u64)(s64)tt->maxw); return;
    case 0x41A: { if (lp) { tt->ml = (s32)rd32(c, lp); tt->mt = (s32)rd32(c, lp + 4); tt->mr = (s32)rd32(c, lp + 8); tt->mb = (s32)rd32(c, lp + 12); } RET(0); return; } case 0x41B: { if (lp) { wr32(c, lp, (u32)tt->ml); wr32(c, lp + 4, (u32)tt->mt); wr32(c, lp + 8, (u32)tt->mr); wr32(c, lp + 12, (u32)tt->mb); } RET(0); return; }
    case 0x41C: tt_hide(w, i); RET(0); return;   /* TTM_POP */
    case 0x41D: { if (tt->shown) wnd_invalidate(i, 1); RET(0); return; }   /* TTM_UPDATE */
    case 0x41E: { if (!lp) { RET(0); return; } TtTool t; u32 cb; tt_ti_read(c, lp, &t, tt->unicode, &cb); int k = tt_find(tt, t.hwnd, t.id); char save[1024]; e_snprintf(save, sizeof save, "%s", tt->text); e_snprintf(tt->text, sizeof tt->text, "%s", k >= 0 ? tt->t[k].text : t.text); int lines[64], nl, tw, th; tt_layout(w, i, tt, lines, &nl, &tw, &th); e_snprintf(tt->text, sizeof tt->text, "%s", save); RET(((u64)(u32)th << 16) | (u32)tw); return; }   /* TTM_GETBUBBLESIZE */
    case 0x41F: { if (lp) { int d = wp ? 1 : -1; wr32(c, lp, (u32)((s32)rd32(c, lp) - d * (tt->ml + 1))); wr32(c, lp + 4, (u32)((s32)rd32(c, lp + 4) - d * (tt->mt + 1))); wr32(c, lp + 8, (u32)((s32)rd32(c, lp + 8) + d * (tt->mr + 1))); wr32(c, lp + 12, (u32)((s32)rd32(c, lp + 12) + d * (tt->mb + 1))); } RET(1); return; }   /* TTM_ADJUSTRECT */
    case 0x420: case 0x421: { if (lp >= 0x10000) text_in(c, lp, msg == 0x421, tt->title, sizeof tt->title); else tt->title[0] = 0; RET(1); return; }   /* TTM_SETTITLE A/W (icon ignored) */
    case 0x422: { if (tt->hover >= 0 && !tt->shown) { tt->pending = 1; tt_fetch_and_show(c, i, tt->hover); return; } RET(0); return; }   /* TTM_POPUP */
    case 0x423: { if (lp) { u32 cch = rd32(c, lp + 4); u64 p = rdp(c, lp + 8); if (p && cch) put_w8(c, p, tt->title, cch); } RET(0); return; }   /* TTM_GETTITLE */
    case 0x30: { n->font = wp; if (tt->shown) wnd_invalidate(i, 1); RET(0); return; } case 0x31: RET(n->font); return;
    default: break; }
  call_shim_tail(c, s_DefWindowProcW, ARG(0), msg, wp, lp); }
/* toolbar side: TTN_GETDISPINFO for a button → the parent first (comctl32 forwards it), then the button's own text (CONT_TTFWD: data[0]=toolbar data[1]=lp data[2]=wide data[3]=button id) */
static void tb_tip_notify(Cpu *c, int i, u64 wp, u64 lp) { Win32 *w = W; Toolbar *tb = g_wnd[i].cc; u64 P = cc_P(c); u32 code = rd32(c, lp + 2 * P); int wide = code == (u32)-530; int p = g_wnd[i].parent; (void)tb;
  if (p < 0 || !wnd_alive(p) || !g_wnd[p].wndproc) { RET(0); return; } w32_push_cont(c, CONT_TTFWD, (u64)i); w->cont[w->ncont - 1].data[1] = lp; w->cont[w->ncont - 1].data[2] = (u64)wide; w->cont[w->ncont - 1].data[3] = rdp(c, lp + P); w32_guest_call(c, g_wnd[p].wndproc, HWND_OF(p), 0x4E, wp, lp); }
static void ttfwd_resume(Cpu *c, u64 rax) { Win32 *w = W; int ci = w->ncont - 1; int i = (int)w->cont[ci].data[0]; u64 lp = w->cont[ci].data[1]; int wide = (int)w->cont[ci].data[2]; u32 id = (u32)w->cont[ci].data[3]; (void)rax; u64 P = cc_P(c);
  if (wnd_alive(i) && g_wnd[i].cc) { Toolbar *tb = g_wnd[i].cc; u64 sztext = lp + 4 * P; u64 lpsz = rdp(c, lp + 3 * P); int empty = (lpsz == sztext || !lpsz) && (wide ? rd16(c, sztext) == 0 : rd8(c, sztext) == 0); int k = tb_index_of(tb, id);
    if (empty && k >= 0 && tb->b[k].text[0]) { char lab[96]; int mn; int ln = menu_label(tb->b[k].text, lab, sizeof lab, &mn); (void)ln; text_out_cap(c, sztext, lab, 80, wide); wrp(c, lp + 3 * P, sztext); } }
  w32_cont_finish(c, 0); }
/* a toolbar with TBSTYLE_TOOLTIPS owns a tooltip window whose tools are its buttons */
static u64 tb_tooltip_wnd(Cpu *c, int i) { Toolbar *tb = g_wnd[i].cc; if (!tb) return 0; if (tb->tipwnd > 0 && wnd_alive(tb->tipwnd) && g_wnd[tb->tipwnd].builtin == BI_TOOLTIP) return HWND_OF(tb->tipwnd);
  int ti = wnd_create_obj(c, "tooltips_class32", "", 0x80000001u, 0x8 /* WS_EX_TOPMOST */, 0, 0, 0, 0, HWND_OF(i), 0, g_wnd[i].inst, 0, 1, 0); if (ti < 0) return 0; g_wnd[ti].owner = i; g_wnd[ti].visible = 0; Tooltip *tt = tt_ensure(ti); if (tt) tt->tb = i; tb->tipwnd = ti; return HWND_OF(ti); }
static void tb_adopt_tip(int ti, int i) { if (g_wnd[ti].builtin != BI_TOOLTIP) return; Tooltip *tt = tt_ensure(ti); if (tt) tt->tb = i; }
#define TOOLTIP_SHIMS S(NebulaTooltipProc)
