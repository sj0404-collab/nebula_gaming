/* ddraw.inc — DirectDraw device objects (DirectDrawCreate / DirectDrawCreateEx / DirectDrawEnumerate*): IDirectDraw, 2, 4 and 7.
 *
 * What programs use them for today: hardware-capability probes (installer helper DLLs, launchers, old game engines at
 * start-up) — GetCaps, GetAvailableVidMem, GetDeviceIdentifier, GetDisplayMode, EnumDisplayModes, SetCooperativeLevel,
 * GetMonitorFrequency, TestCooperativeLevel.  All of that is answered with the real display of this session (the
 * emulator's screen size, 32 bpp, 60 Hz) and a device identifier naming the WebGL adapter class.
 *
 * Video memory: browsers do not expose the adapter's memory, so the object reports 512 MB total / free and says so once
 * in the log.  Surfaces (CreateSurface / CreateClipper / CreatePalette / SetDisplayMode) — i.e. actual DirectDraw
 * rendering — are not implemented: those methods stop with their exact name instead of returning a fake object.
 *
 * object (guest heap, 32 B): [0] vtable [8] id [12] refcount [16] interface version (1/2/4/7) [20] cooperative-level flags */
enum { CONT_DDENUM = 130 };
#define DD_OK 0
#define DDERR_INVALIDPARAMS 0x88760057u
#define DDERR_UNSUPPORTED 0x80004001u
#define DDERR_OUTOFMEMORY 0x8007000Eu
#define DD_VIDMEM (512u << 20)
#define DD_MAX 32
typedef struct { int used; u64 addr; Win32 *w; } DdObj;
static DdObj g_ddobj[DD_MAX];
static int g_dd_log_mem;
static const char *const vt_IDirectDraw[] = { "QueryInterface", "AddRef", "Release", "Compact", "CreateClipper", "CreatePalette", "CreateSurface", "DuplicateSurface", "EnumDisplayModes", "EnumSurfaces",
  "FlipToGDISurface", "GetCaps", "GetDisplayMode", "GetFourCCCodes", "GetGDISurface", "GetMonitorFrequency", "GetScanLine", "GetVerticalBlankStatus", "Initialize", "RestoreDisplayMode", "SetCooperativeLevel",
  "SetDisplayMode", "WaitForVerticalBlank",                                                     /* IDirectDraw: 23 */
  "GetAvailableVidMem",                                                                         /* IDirectDraw2: 24 */
  "GetSurfaceFromDC", "RestoreAllSurfaces", "TestCooperativeLevel", "GetDeviceIdentifier",     /* IDirectDraw4: 28 */
  "StartModeTest", "EvaluateMode" };                                                            /* IDirectDraw7: 30 */
static int dd_nmethods(int ver) { return ver == 1 ? 23 : ver == 2 ? 24 : ver == 4 ? 28 : 30; }
static const char *dd_ifname(int ver) { return ver == 1 ? "IDirectDraw" : ver == 2 ? "IDirectDraw2" : ver == 4 ? "IDirectDraw4" : "IDirectDraw7"; }
static u64 g_ddvt[8]; static Win32 *g_ddvt_owner;
static u64 dd_vtable(Cpu *c, int ver) {
  Win32 *w = W; if (g_ddvt_owner != w) { e_memset(g_ddvt, 0, sizeof g_ddvt); g_ddvt_owner = w; }
  if (g_ddvt[ver]) return g_ddvt[ver];
  int n = dd_nmethods(ver); u64 psz = PSZ_OF(c); u64 vt = w32_heap_alloc(w, (u64)n * psz, 1); if (!vt) return 0;
  for (int i = 0; i < n; i++) { char nm[64]; e_snprintf(nm, sizeof nm, "%s::%s", dd_ifname(ver), vt_IDirectDraw[i]); wrp(c, vt + psz * i, w32_shim_slot(w, "ddraw", nm)); }
  g_ddvt[ver] = vt; return vt;
}
static u64 dd_new(Cpu *c, int ver) {
  int id; for (id = 1; id < DD_MAX && g_ddobj[id].used; id++) { } if (id == DD_MAX) return 0;
  u64 vt = dd_vtable(c, ver), a = w32_heap_alloc(W, 32, 1); if (!vt || !a) return 0;
  wrp(c, a, vt); wr32(c, a + 8, (u32)id); wr32(c, a + 12, 1); wr32(c, a + 16, (u32)ver); wr32(c, a + 20, 0);
  g_ddobj[id].used = 1; g_ddobj[id].addr = a; g_ddobj[id].w = W; return a;
}
static DdObj *dd_of(Cpu *c, u64 self) {
  if (!self || !mem_ptr(c->mem, self, 24, 0)) return 0; u32 id = rd32(c, self + 8);
  if (id >= DD_MAX || !g_ddobj[id].used || g_ddobj[id].addr != self) return 0; return &g_ddobj[id];
}
/* IID first dwords: IDirectDraw 6C14DB80, IDirectDraw2 B3A6F3E0, IDirectDraw4 9C59509A, IDirectDraw7 15E65EC0, IUnknown 00000000 */
static int dd_ver_of_iid(u32 d1) { return d1 == 0x6C14DB80u ? 1 : d1 == 0xB3A6F3E0u ? 2 : d1 == 0x9C59509Au ? 4 : d1 == 0x15E65EC0u ? 7 : d1 == 0 ? 1 : 0; }
static void dd_QueryInterface(Cpu *c) {
  DdObj *o = dd_of(c, ARG(0)); u64 riid = ARG(1), out = ARG(2); if (!o || !out || !riid) { RET(DDERR_INVALIDPARAMS); return; }
  int ver = dd_ver_of_iid(rd32(c, riid));
  if (!ver) { wrp(c, out, 0); e_log("[ddraw] QueryInterface for {%08x-…}: E_NOINTERFACE (Direct3D through DirectDraw is not provided)\n", rd32(c, riid)); RET(E_NOINTERFACE_); return; }
  if (ver == 1 && rd32(c, riid) == 0) ver = (int)rd32(c, o->addr + 16);   /* IUnknown: the object itself */
  u64 n = dd_new(c, ver); if (!n) { RET(DDERR_OUTOFMEMORY); return; } wr32(c, n + 20, rd32(c, o->addr + 20)); wrp(c, out, n); RET(DD_OK);
}
static void dd_AddRef(Cpu *c) { DdObj *o = dd_of(c, ARG(0)); if (!o) { RET(0); return; } u32 r = rd32(c, o->addr + 12) + 1; wr32(c, o->addr + 12, r); RET(r); }
static void dd_Release(Cpu *c) { DdObj *o = dd_of(c, ARG(0)); if (!o) { RET(0); return; } u32 r = rd32(c, o->addr + 12); if (r) r--; wr32(c, o->addr + 12, r); if (!r) { o->used = 0; w32_heap_free(W, o->addr); } RET(r); }
static void dd_Compact(Cpu *c) { RET(DD_OK); }
static void dd_Initialize(Cpu *c) { RET(0x88760197u); }   /* DDERR_ALREADYINITIALIZED */
static void dd_FlipToGDISurface(Cpu *c) { RET(DD_OK); }
static void dd_RestoreDisplayMode(Cpu *c) { RET(DD_OK); }
static void dd_RestoreAllSurfaces(Cpu *c) { RET(DD_OK); }
static void dd_WaitForVerticalBlank(Cpu *c) { RET(DD_OK); }
static void dd_GetScanLine(Cpu *c) { if (ARG(1)) wr32(c, ARG(1), (u32)((u64)host_now_ms() % 16) * ((u32)W->screen_h / 16)); RET(DD_OK); }
static void dd_GetVerticalBlankStatus(Cpu *c) { if (ARG(1)) wr32(c, ARG(1), 0); RET(DD_OK); }
static void dd_GetMonitorFrequency(Cpu *c) { if (!ARG(1)) { RET(DDERR_INVALIDPARAMS); return; } wr32(c, ARG(1), 60); RET(DD_OK); }
static void dd_GetFourCCCodes(Cpu *c) { if (ARG(1)) wr32(c, ARG(1), 0); RET(DD_OK); }
static void dd_SetCooperativeLevel(Cpu *c) { DdObj *o = dd_of(c, ARG(0)); if (!o) { RET(DDERR_INVALIDPARAMS); return; } wr32(c, o->addr + 20, (u32)ARG(2)); RET(DD_OK); }
static void dd_TestCooperativeLevel(Cpu *c) { RET(DD_OK); }
static void dd_GetGDISurface(Cpu *c) { if (ARG(1)) wrp(c, ARG(1), 0); RET(0x887600DAu); }   /* DDERR_NOTFOUND */
static void dd_GetSurfaceFromDC(Cpu *c) { if (ARG(2)) wrp(c, ARG(2), 0); RET(0x887600DAu); }
static void dd_EnumSurfaces(Cpu *c) { RET(DD_OK); }   /* no surfaces exist */
static void dd_StartModeTest(Cpu *c) { RET(0x8876021Du); }   /* DDERR_TESTFINISHED */
static void dd_EvaluateMode(Cpu *c) { if (ARG(2)) wr32(c, ARG(2), 0); RET(DD_OK); }
/* DDCAPS (DX7 layout, 380 B): dwSize 0, dwCaps 4, dwCaps2 8, dwCKeyCaps 12, dwFXCaps 16, dwFXAlphaCaps 20, dwPalCaps 24, dwSVCaps 28, … dwZBufferBitDepths 56, dwVidMemTotal 60, dwVidMemFree 64,
   dwMaxVisibleOverlays 68, dwCurrVisibleOverlays 72, dwNumFourCCCodes 76, … ddsCaps at 0x160 (DDSCAPS2, DX6+) */
static void dd_fill_caps(Cpu *c, u64 p) {
  u32 sz = rd32(c, p); if (sz < 8) return; u8 *m = mem_ptr(c->mem, p, sz, 1); if (!m) return; e_memset(m + 4, 0, sz - 4);
  u32 scaps = 0x4u | 0x8u | 0x10u | 0x20u | 0x40u | 0x100u | 0x200u | 0x800u | 0x1000u | 0x2000u | 0x4000u | 0x8000u | 0x20000u | 0x400000u | 0x10000000u;   /* DDSCAPS_*: back/front buffers, flip, offscreen, palette, primary, sysmem, texture, 3-D device, vidmem, visible, z-buffer, mipmap, local vidmem */
  wr32(c, p + 4, 0x1u | 0x20u | 0x40u | 0x80u | 0x200u | 0x1000u | 0x20000u | 0x40000u | 0x80000u | 0x2000000u | 0x4000000u);   /* DDCAPS_3D BLTSTRETCH BLT BLTQUEUE COLORKEY GDI PALETTE PALETTEVSYNC READSCANLINE CANBLTSYSMEM BLTCOLORFILL */
  if (sz >= 12) wr32(c, p + 8, 0x1u | 0x200u | 0x1000u | 0x80000u | 0x800000u);   /* DDCAPS2_CERTIFIED NONLOCALVIDMEM WIDESURFACES CANRENDERWINDOWED CANMANAGETEXTURE */
  if (sz >= 16) wr32(c, p + 12, 0x1u | 0x100u);   /* DDCKEYCAPS_DESTBLT SRCBLT */
  if (sz >= 20) wr32(c, p + 16, 0x20u | 0x40u | 0x80u | 0x400u | 0x1000u | 0x4000u | 0x10000u);   /* DDFXCAPS_BLTARITHSTRETCHY MIRRORLEFTRIGHT MIRRORUPDOWN SHRINKX SHRINKY STRETCHX STRETCHY */
  if (sz >= 28) wr32(c, p + 24, 0x4u | 0x10u | 0x40u | 0x80u);   /* DDPCAPS_8BIT PRIMARYSURFACE ALLOW256 VSYNC */
  if (sz >= 60) wr32(c, p + 56, 0x400u | 0x200u | 0x100u);   /* DDBD_16 | DDBD_24 | DDBD_32 z-buffer depths */
  if (sz >= 68) { wr32(c, p + 60, DD_VIDMEM); wr32(c, p + 64, DD_VIDMEM - (2u << 20)); }
  if (sz >= 100) wr32(c, p + 96, scaps);    /* ddsOldCaps.dwCaps (DDCAPS_DX3/5/6/7) */
  if (sz >= 380) wr32(c, p + 364, scaps);   /* ddsCaps.dwCaps (DDCAPS_DX6/7: DDSCAPS2 is the last field) */
}
static void dd_GetCaps(Cpu *c) {
  u64 drv = ARG(1), hel = ARG(2); if (!drv && !hel) { RET(DDERR_INVALIDPARAMS); return; }
  if (drv) dd_fill_caps(c, drv); if (hel) dd_fill_caps(c, hel);
  if (!g_dd_log_mem++) e_log("[ddraw] IDirectDraw::GetCaps / GetAvailableVidMem: video memory reported as %u MB (the browser does not expose the adapter's real amount); display %dx%d 32 bpp 60 Hz\n", DD_VIDMEM >> 20, W->screen_w, W->screen_h);
  RET(DD_OK);
}
static void dd_GetAvailableVidMem(Cpu *c) {
  u64 total = ARG(2), fre = ARG(3); if (!total && !fre) { RET(DDERR_INVALIDPARAMS); return; }
  if (total) wr32(c, total, DD_VIDMEM); if (fre) wr32(c, fre, DD_VIDMEM - (2u << 20));
  if (!g_dd_log_mem++) e_log("[ddraw] IDirectDraw::GetAvailableVidMem: video memory reported as %u MB (the browser does not expose the adapter's real amount)\n", DD_VIDMEM >> 20);
  RET(DD_OK);
}
/* DDSURFACEDESC2 (x86 124 B / x64 136 B): dwSize 0, dwFlags 4, dwHeight 8, dwWidth 12, lPitch 16, dwBackBufferCount 20, dwRefreshRate 24, dwAlphaBitDepth 28, dwReserved 32, lpSurface 36 (x64: 40, 8 B),
   then (shifted by d) color keys 40..72, ddpfPixelFormat 72 (32 B), ddsCaps 104 (16 B), dwTextureStage 120 */
static void dd_fill_mode(Cpu *c, u64 p, u32 w, u32 h, u32 bpp, int v2) {
  u32 sz = rd32(c, p); int d = c->mode32 ? 0 : 8; u32 want = v2 ? 124u + (u32)d : 108u + (u32)d; u8 *m = mem_ptr(c->mem, p, sz, 1); if (!m || sz < want) return; e_memset(m + 4, 0, sz - 4);
  wr32(c, p + 4, 0x1u | 0x2u | 0x8u | 0x1000u | 0x40000u | 0x20u);   /* CAPS, HEIGHT, WIDTH, PITCH, PIXELFORMAT, REFRESHRATE */
  wr32(c, p + 8, h); wr32(c, p + 12, w); wr32(c, p + 16, w * (bpp / 8)); wr32(c, p + 24, 60);
  u64 pf = p + 72 + (u64)d; wr32(c, pf, 32); wr32(c, pf + 4, 0x40u | (bpp == 8 ? 0x20u : 0));   /* DDPF_RGB (+ PALETTEINDEXED8) */
  wr32(c, pf + 12, bpp);
  if (bpp == 32) { wr32(c, pf + 16, 0xff0000u); wr32(c, pf + 20, 0xff00u); wr32(c, pf + 24, 0xffu); }
  else if (bpp == 16) { wr32(c, pf + 16, 0xf800u); wr32(c, pf + 20, 0x07e0u); wr32(c, pf + 24, 0x001fu); }
}
static void dd_GetDisplayMode(Cpu *c) { u64 p = ARG(1); if (!p) { RET(DDERR_INVALIDPARAMS); return; } int ver = (int)rd32(c, ARG(0) + 16); dd_fill_mode(c, p, (u32)W->screen_w, (u32)W->screen_h, 32, ver >= 4); RET(DD_OK); }
/* display modes offered: the current screen plus the classic set, 32 and 16 bpp (DDEDM_REFRESHRATES adds nothing: all are 60 Hz) */
static const u16 dd_modes[][2] = { {640, 480}, {800, 600}, {1024, 768}, {1152, 864}, {1280, 720}, {1280, 800}, {1280, 1024}, {1366, 768}, {1440, 900}, {1600, 900}, {1680, 1050}, {1920, 1080} };
#define DD_NMODES ((int)(sizeof dd_modes / sizeof dd_modes[0]))
static int dd_mode_at(Cpu *c, int k, u32 *w, u32 *h, u32 *bpp) {   /* k over 2 × (DD_NMODES + 1): index DD_NMODES = the current screen */
  int bppi = k / (DD_NMODES + 1), mi = k % (DD_NMODES + 1); if (bppi > 1) return 0; *bpp = bppi ? 16 : 32;
  if (mi == DD_NMODES) { *w = (u32)W->screen_w; *h = (u32)W->screen_h; for (int i = 0; i < DD_NMODES; i++) if (dd_modes[i][0] == *w && dd_modes[i][1] == *h) return -1; return 1; }
  *w = dd_modes[mi][0]; *h = dd_modes[mi][1]; if (*w > (u32)W->screen_w || *h > (u32)W->screen_h) return -1; return 1;   /* -1: skip (duplicate / larger than the screen) */
}
static int dd_enum_step(Cpu *c, int ci) {   /* data: 0 desc (guest heap), 1 callback, 2 context, 3 next index, 4 v2 flag, 5 filter desc */
  Win32 *w = W; for (;;) {
    int k = (int)w->cont[ci].data[3]; u32 mw, mh, bpp; int r = dd_mode_at(c, k, &mw, &mh, &bpp); if (!r) return 0; w->cont[ci].data[3] = (u64)(k + 1); if (r < 0) continue;
    u64 f = w->cont[ci].data[5];   /* a filter descriptor restricts width/height/bit depth */
    if (f) { u32 fl = rd32(c, f + 4); if ((fl & 0x2) && rd32(c, f + 8) != mh) continue; if ((fl & 0x4) && rd32(c, f + 12) != mw) continue; if ((fl & 0x1000) && rd32(c, f + 72 + (c->mode32 ? 0 : 8) + 12) != bpp) continue; }
    u64 p = w->cont[ci].data[0]; wr32(c, p, (u32)(w->cont[ci].data[4] ? 124 + (c->mode32 ? 0 : 8) : 108 + (c->mode32 ? 0 : 8))); dd_fill_mode(c, p, mw, mh, bpp, (int)w->cont[ci].data[4]);
    guest_call(c, w->cont[ci].data[1], p, w->cont[ci].data[2], 0, 0); return 1;
  }
}
static void dd_EnumDisplayModes(Cpu *c) {
  u64 filter = ARG(2), ctx = ARG(3), cb = ARG(4); if (!cb) { RET(DDERR_INVALIDPARAMS); return; }
  int ver = (int)rd32(c, ARG(0) + 16); u64 p = w32_heap_alloc(W, 144, 1); if (!p) { RET(DDERR_OUTOFMEMORY); return; }
  int i = cont_push(c, CONT_DDENUM); if (i < 0) { w32_heap_free(W, p); return; }
  W->cont[i].data[0] = p; W->cont[i].data[1] = cb; W->cont[i].data[2] = ctx; W->cont[i].data[3] = 0; W->cont[i].data[4] = ver >= 4; W->cont[i].data[5] = filter;
  if (!dd_enum_step(c, i)) { w32_heap_free(W, p); cont_finish(c, DD_OK); }
}
static int ddraw_cont_resume(Cpu *c, int kind, u64 rax) {
  if (kind != CONT_DDENUM) return 0; Win32 *w = W; int i = w->ncont - 1;
  if ((u32)rax == 0 || !dd_enum_step(c, i)) { w32_heap_free(w, w->cont[i].data[0]); cont_finish(c, DD_OK); }   /* DDENUMRET_CANCEL = 0 */
  return 1;
}
/* DDDEVICEIDENTIFIER(2): szDriver[512] 0, szDescription[512] 512, liDriverVersion 1024, dwVendorId 1032, dwDeviceId 1036, dwSubSysId 1040, dwRevision 1044, guidDeviceIdentifier 1048, dwWHQLLevel 1064 (v2 only) */
static void dd_GetDeviceIdentifier(Cpu *c) {
  u64 p = ARG(1); int ver = (int)rd32(c, ARG(0) + 16); u32 sz = ver >= 7 ? 1068 : 1064; u8 *m = mem_ptr(c->mem, p, sz, 1); if (!m) { RET(DDERR_INVALIDPARAMS); return; }
  e_memset(m, 0, sz); const char *drv = "nebula_webgl.dll", *desc = "Nebula OS display adapter (WebGL 2 on the device GPU)";
  for (int i = 0; drv[i]; i++) m[i] = (u8)drv[i]; for (int i = 0; desc[i]; i++) m[512 + i] = (u8)desc[i];
  wr32(c, p + 1024, 0x00020000u); wr32(c, p + 1028, 0x00010031u);   /* liDriverVersion 1.49.2.0 (product.version / build.revision) */
  wr32(c, p + 1032, 0x1AF4u); wr32(c, p + 1036, 0x1050u); wr32(c, p + 1040, 0); wr32(c, p + 1044, 1);   /* vendor/device ids of a virtual display adapter */
  wr32(c, p + 1048, 0x4E45424Cu); wr32(c, p + 1052, 0x574C4744u); wr32(c, p + 1056, 0x00010000u); wr32(c, p + 1060, 0x00000001u);
  RET(DD_OK);
}
/* not implemented: actual DirectDraw rendering objects — say so with the exact method name */
static void dd_CreateSurface(Cpu *c) { cpu_fault(c, "IDirectDraw::CreateSurface: DirectDraw surfaces (2-D DirectDraw rendering) are not implemented — the program's hardware probes (GetCaps/GetAvailableVidMem/EnumDisplayModes) work, drawing through DirectDraw does not yet"); }
static void dd_CreateClipper(Cpu *c) { cpu_fault(c, "IDirectDraw::CreateClipper: DirectDraw surfaces/clippers are not implemented"); }
static void dd_CreatePalette(Cpu *c) { cpu_fault(c, "IDirectDraw::CreatePalette: DirectDraw palettes/surfaces are not implemented"); }
static void dd_DuplicateSurface(Cpu *c) { cpu_fault(c, "IDirectDraw::DuplicateSurface: DirectDraw surfaces are not implemented"); }
static void dd_SetDisplayMode(Cpu *c) { u32 mw = (u32)ARG(1), mh = (u32)ARG(2); e_log("[ddraw] SetDisplayMode %ux%u: the desktop keeps its size (exclusive-mode display switching is not provided); a full-screen primary surface would be needed next, which is not implemented\n", mw, mh); RET(DD_OK); }

static void s_DirectDrawCreate(Cpu *c) {   /* (GUID *driver, IDirectDraw **pp, IUnknown *outer) → IDirectDraw (v1); callers QueryInterface IDirectDraw2/4/7 from it */
  u64 pp = ARG(1); if (!pp) { RET(DDERR_INVALIDPARAMS); return; } if (ARG(2)) { RET(0x80040110u); return; }   /* CLASS_E_NOAGGREGATION */
  u64 a = dd_new(c, 1); if (!a) { RET(DDERR_OUTOFMEMORY); return; } wrp(c, pp, a); e_log("[ddraw] DirectDrawCreate → display %dx%d (device object; surfaces not implemented)\n", W->screen_w, W->screen_h); RET(DD_OK);
}
static void s_DirectDrawCreateEx(Cpu *c) {   /* (GUID *driver, void **pp, REFIID iid, IUnknown *outer): iid must be IID_IDirectDraw7 */
  u64 pp = ARG(1), iid = ARG(2); if (!pp || !iid) { RET(DDERR_INVALIDPARAMS); return; } if (ARG(3)) { RET(0x80040110u); return; }
  if (rd32(c, iid) != 0x15E65EC0u) { wrp(c, pp, 0); RET(0x88760058u); return; }   /* DDERR_INVALIDDIRECTDRAWGUID */
  u64 a = dd_new(c, 7); if (!a) { RET(DDERR_OUTOFMEMORY); return; } wrp(c, pp, a); e_log("[ddraw] DirectDrawCreateEx → IDirectDraw7, display %dx%d (device object; surfaces not implemented)\n", W->screen_w, W->screen_h); RET(DD_OK);
}
static void s_DirectDrawCreateClipper(Cpu *c) { cpu_fault(c, "DirectDrawCreateClipper: DirectDraw clippers/surfaces are not implemented"); }
/* DirectDrawEnumerate{A,W}(callback(GUID*, desc, name, ctx)) / Ex(callback(GUID*, desc, name, ctx, HMONITOR), ctx, flags): one entry, the primary display (GUID NULL) */
static void dd_enumerate(Cpu *c, int wide, int ex) {
  u64 cb = ARG(0), ctx = ARG(1); if (!cb) { RET(DDERR_INVALIDPARAMS); return; }
  const char *desc = "Primary Display Driver", *name = "display"; u64 p = w32_heap_alloc(W, 128, 1); if (!p) { RET(DDERR_OUTOFMEMORY); return; }
  u64 pd = p, pn = p + 64; for (int i = 0; i <= 22; i++) { if (wide) wr16(c, pd + 2 * i, (u8)desc[i]); else wr8(c, pd + i, (u8)desc[i]); }
  for (int i = 0; i <= 7; i++) { if (wide) wr16(c, pn + 2 * i, (u8)name[i]); else wr8(c, pn + i, (u8)name[i]); }
  int i = cont_push(c, CONT_AUDIO_CB); if (i < 0) { w32_heap_free(W, p); return; }   /* same shape as the audio enumeration: free the strings, return 0 */
  W->cont[i].data[0] = p; guest_call(c, cb, 0, pd, pn, ctx); (void)ex;
}
static void s_DirectDrawEnumerateA(Cpu *c) { dd_enumerate(c, 0, 0); }
static void s_DirectDrawEnumerateW(Cpu *c) { dd_enumerate(c, 1, 0); }
static void s_DirectDrawEnumerateExA(Cpu *c) { dd_enumerate(c, 0, 1); }
static void s_DirectDrawEnumerateExW(Cpu *c) { dd_enumerate(c, 1, 1); }

#define DDM(iface, m, fn) { "", iface "::" m, fn }
#define DD_IFACE(iface) \
  DDM(iface, "QueryInterface", dd_QueryInterface), DDM(iface, "AddRef", dd_AddRef), DDM(iface, "Release", dd_Release), DDM(iface, "Compact", dd_Compact), DDM(iface, "CreateClipper", dd_CreateClipper), \
  DDM(iface, "CreatePalette", dd_CreatePalette), DDM(iface, "CreateSurface", dd_CreateSurface), DDM(iface, "DuplicateSurface", dd_DuplicateSurface), DDM(iface, "EnumDisplayModes", dd_EnumDisplayModes), \
  DDM(iface, "EnumSurfaces", dd_EnumSurfaces), DDM(iface, "FlipToGDISurface", dd_FlipToGDISurface), DDM(iface, "GetCaps", dd_GetCaps), DDM(iface, "GetDisplayMode", dd_GetDisplayMode), \
  DDM(iface, "GetFourCCCodes", dd_GetFourCCCodes), DDM(iface, "GetGDISurface", dd_GetGDISurface), DDM(iface, "GetMonitorFrequency", dd_GetMonitorFrequency), DDM(iface, "GetScanLine", dd_GetScanLine), \
  DDM(iface, "GetVerticalBlankStatus", dd_GetVerticalBlankStatus), DDM(iface, "Initialize", dd_Initialize), DDM(iface, "RestoreDisplayMode", dd_RestoreDisplayMode), \
  DDM(iface, "SetCooperativeLevel", dd_SetCooperativeLevel), DDM(iface, "SetDisplayMode", dd_SetDisplayMode), DDM(iface, "WaitForVerticalBlank", dd_WaitForVerticalBlank), \
  DDM(iface, "GetAvailableVidMem", dd_GetAvailableVidMem), DDM(iface, "GetSurfaceFromDC", dd_GetSurfaceFromDC), DDM(iface, "RestoreAllSurfaces", dd_RestoreAllSurfaces), \
  DDM(iface, "TestCooperativeLevel", dd_TestCooperativeLevel), DDM(iface, "GetDeviceIdentifier", dd_GetDeviceIdentifier), DDM(iface, "StartModeTest", dd_StartModeTest), DDM(iface, "EvaluateMode", dd_EvaluateMode),
#define DDRAW_SHIMS \
  S(DirectDrawCreate), S(DirectDrawCreateEx), S(DirectDrawCreateClipper), S(DirectDrawEnumerateA), S(DirectDrawEnumerateW), S(DirectDrawEnumerateExA), S(DirectDrawEnumerateExW), \
  DD_IFACE("IDirectDraw") DD_IFACE("IDirectDraw2") DD_IFACE("IDirectDraw4") DD_IFACE("IDirectDraw7")
