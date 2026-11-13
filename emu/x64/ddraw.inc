/* ddraw.inc — DirectDraw (ddraw.dll) in the GDI/USER translation unit: devices, surfaces, palettes and clippers.
 *
 * Devices (IDirectDraw / 2 / 4 / 7): capabilities, video memory, device identifier, display modes (enumeration through a
 * guest callback continuation), cooperative level, display-mode switching (the framebuffer really changes size; the host
 * scales it like a monitor would), GDI surface, surface enumeration.
 *
 * Surfaces (IDirectDrawSurface / 2 / 3 / 4 / 7): real pixel memory in guest address space in the surface's own format —
 * 8 bpp palettised, 16 bpp (any RGB masks: 565, 555, 1555, 4444), 24 bpp, 32 bpp (any masks), z-buffers — so Lock hands
 * out a pointer the program writes through directly.  Blt (colour fill, copy, stretch, mirror, source/destination colour
 * keys, format conversion), BltFast, Flip (pointer rotation like real DirectDraw, paced at the monitor rate), GetDC /
 * ReleaseDC (GDI drawing onto a surface through a 32-bpp DIB and the surface's palette), attached surfaces (back buffers,
 * z-buffers, mip levels), colour keys, client-memory surfaces (DDSD_LPSURFACE / SetSurfaceDesc), private data,
 * uniqueness values.  The primary surface is the desktop: every change to it (Unlock, Blt, Flip, ReleaseDC, palette
 * change) is converted to the framebuffer — the whole screen in exclusive mode, the clipper window's client rectangle in
 * windowed mode — exactly where the program put it.
 *
 * Palettes (IDirectDrawPalette): 256 / 16 / 4 / 2 entries, SetEntries re-presents a palettised primary (palette animation).
 * Clippers (IDirectDrawClipper): window-bound (the window's client rectangle on screen) or explicit clip lists.
 *
 * Not provided: Direct3D through DirectDraw (IDirect3D / IDirect3D2 / 3 / 7 — QueryInterface says E_NOINTERFACE and logs
 * it), overlays, FourCC/YUV surfaces, gamma ramps.  Video memory is reported as 512 MB (browsers do not expose the real
 * amount; said once in the log).
 *
 * Guest COM object (32 B): [0] vtable [8] id [12] refcount [16] interface version [20] kind (0 device, 1 surface, 2 palette, 3 clipper) [24] cooperative-level flags (device) */
enum { CONT_DDENUM = 130, CONT_DDENUMSURF = 131, CONT_DDENUMATT = 132 };
#define DD_OK 0
#define DD_FALSE 1
#define DDERR_INVALIDPARAMS 0x80070057u
#define DDERR_UNSUPPORTED 0x80004001u
#define DDERR_OUTOFMEMORY 0x8007000Eu
#define DDERR_GENERIC 0x80004005u
#define DDERR_(code) (0x88760000u | (u32)(code))
#define DDERR_ALREADYINITIALIZED DDERR_(5)
#define DDERR_INCOMPATIBLEPRIMARY DDERR_(95)
#define DDERR_INVALIDOBJECT DDERR_(130)
#define DDERR_INVALIDPIXELFORMAT DDERR_(145)
#define DDERR_INVALIDRECT DDERR_(150)
#define DDERR_INVALIDMODE DDERR_(155)
#define DDERR_NOCLIPLIST DDERR_(205)
#define DDERR_NOCOLORKEY DDERR_(215)
#define DDERR_NOEXCLUSIVEMODE DDERR_(225)
#define DDERR_NOPALETTEATTACHED DDERR_(405)
#define DDERR_NOTFOUND DDERR_(255)
#define DDERR_NODC DDERR_(290)
#define DDERR_UNSUPPORTEDMODE DDERR_(310)
#define DDERR_NOCLIPPERATTACHED DDERR_(400)
#define DDERR_SURFACEALREADYATTACHED DDERR_(410)
#define DDERR_SURFACEBUSY DDERR_(430)
#define DDERR_CANTLOCKSURFACE DDERR_(435)
#define DDERR_SURFACELOST DDERR_(450)
#define DDERR_SURFACENOTATTACHED DDERR_(460)
#define DDERR_PRIMARYSURFACEALREADYEXISTS DDERR_(564)
#define DDERR_NOTFLIPPABLE DDERR_(582)
#define DDERR_NOTLOCKED DDERR_(584)
#define DDERR_NOTPALETTIZED DDERR_(589)
#define DDERR_TESTFINISHED DDERR_(692)
#define DDERR_INVALIDSURFACETYPE DDERR_(220)
#define DDERR_REGIONTOOSMALL DDERR_(295)
#define DDERR_CLIPPERISUSINGHWND DDERR_(206)
#define DDERR_INVALIDCLIPLIST DDERR_(270)
#define DDERR_BLTFASTCANTCLIP DDERR_(230)
#define DDERR_CANNOTATTACHSURFACE DDERR_(380)
#define DDERR_CANTDUPLICATE DDERR_(583)
#define DDERR_WASSTILLDRAWING DDERR_(540)
#define DDERR_NOTAOVERLAYSURFACE DDERR_(580)
#define DDERR_DCALREADYCREATED DDERR_(620)
#define DDERR_MOREDATA DDERR_(690)
#ifndef E_NOINTERFACE_
#define E_NOINTERFACE_ 0x80004002u
#endif
#define DD_VIDMEM (512u << 20)
#define DD_MAX 32
#define DDS_MAX 512
#define DDP_MAX 64
#define DDC_MAX 64
#define DDS_PRIV 8
enum { DDK_DEVICE = 0, DDK_SURFACE = 1, DDK_PALETTE = 2, DDK_CLIPPER = 3 };
/* a guest COM pointer may be any of the interface versions of one object; each version has its own guest object sharing the host record */
typedef struct { int used; u64 addr; Win32 *w; u32 coop; u64 coop_hwnd; } DdDev;
typedef struct { int used; Win32 *own; int dev; int ver; u32 flags; u32 pal[256]; int ncol; u32 gen; u64 ifaces[1]; int refs; } DdPal;
typedef struct { int used; Win32 *own; int dev; u64 hwnd; int nrects; s32 rects[16][4]; int changed; u64 ifaces[1]; int refs; } DdClip;
typedef struct {
  int used; Win32 *own; int dev; int refs;
  int w, h, bpp, pitch;                 /* native format */
  u32 rmask, gmask, bmask, amask; int rsh, gsh, bsh, ash, rbits, gbits, bbits, abits; int pal8, zbuf;
  u64 bits; int client_mem;             /* guest address of the pixel memory (client_mem: supplied by the program, never unmapped) */
  u32 caps, caps2; u32 pf_flags;        /* DDSCAPS / DDSCAPS2 / DDPF flags as created */
  int primary, backbuffers;
  int att[8]; int natt; int att_parent;   /* attached surfaces (back buffers, z-buffer, mip chain) */
  int palette, clipper;
  u32 ck[4][2]; u8 ck_set[4];           /* colour keys: 0 dest blt, 1 dest overlay, 2 src blt, 3 src overlay */
  int locked; u64 lock_rect[4];
  u64 dc, dc_bitmap; int dc_out;        /* GetDC: memory DC + 32-bpp bitmap, dc_out while handed out */
  u64 ifaces[8];                        /* guest objects per interface version index (1,2,3,4,7 → slots) */
  u32 uniq; u8 priv_guid[DDS_PRIV][16]; u64 priv_data[DDS_PRIV]; u32 priv_size[DDS_PRIV], priv_flags[DDS_PRIV]; int npriv;
  u32 priority, lod; int lost;
} DdSurf;
static DdDev g_ddobj[DD_MAX];
static DdSurf g_dds[DDS_MAX];
static DdPal g_ddp[DDP_MAX];
static DdClip g_ddc[DDC_MAX];
static int g_dd_log_mem, g_dd_log_d3d, g_dd_log_rop;
static Win32 *g_dd_mode_owner; static u64 g_dd_mode_hwnd;
static double g_dd_last_flip_ms;
static const char *const vt_IDirectDraw[] = { "QueryInterface", "AddRef", "Release", "Compact", "CreateClipper", "CreatePalette", "CreateSurface", "DuplicateSurface",
  "EnumDisplayModes", "EnumSurfaces", "FlipToGDISurface", "GetCaps", "GetDisplayMode", "GetFourCCCodes", "GetGDISurface", "GetMonitorFrequency", "GetScanLine",
  "GetVerticalBlankStatus", "Initialize", "RestoreDisplayMode", "SetCooperativeLevel", "SetDisplayMode", "WaitForVerticalBlank",
  "GetAvailableVidMem",                                                                    /* IDirectDraw2 (24) */
  "GetSurfaceFromDC", "RestoreAllSurfaces", "TestCooperativeLevel", "GetDeviceIdentifier",  /* IDirectDraw4 (28) */
  "StartModeTest", "EvaluateMode" };                                                       /* IDirectDraw7 (30) */
static const char *const vt_IDirectDrawSurface[] = { "QueryInterface", "AddRef", "Release", "AddAttachedSurface", "AddOverlayDirtyRect", "Blt", "BltBatch", "BltFast",
  "DeleteAttachedSurface", "EnumAttachedSurfaces", "EnumOverlayZOrders", "Flip", "GetAttachedSurface", "GetBltStatus", "GetCaps", "GetClipper", "GetColorKey", "GetDC",
  "GetFlipStatus", "GetOverlayPosition", "GetPalette", "GetPixelFormat", "GetSurfaceDesc", "Initialize", "IsLost", "Lock", "ReleaseDC", "Restore", "SetClipper",
  "SetColorKey", "SetOverlayPosition", "SetPalette", "Unlock", "UpdateOverlay", "UpdateOverlayDisplay", "UpdateOverlayZOrder",   /* 36: IDirectDrawSurface */
  "GetDDInterface", "PageLock", "PageUnlock",                                                                                    /* 39: IDirectDrawSurface2 */
  "SetSurfaceDesc",                                                                                                              /* 40: IDirectDrawSurface3 */
  "SetPrivateData", "GetPrivateData", "FreePrivateData", "GetUniquenessValue", "ChangeUniquenessValue",                          /* 45: IDirectDrawSurface4 */
  "SetPriority", "GetPriority", "SetLOD", "GetLOD" };                                                                            /* 49: IDirectDrawSurface7 */
static const char *const vt_IDirectDrawPalette[] = { "QueryInterface", "AddRef", "Release", "GetCaps", "GetEntries", "Initialize", "SetEntries" };
static const char *const vt_IDirectDrawClipper[] = { "QueryInterface", "AddRef", "Release", "GetClipList", "GetHWnd", "Initialize", "IsClipListChanged", "SetClipList", "SetHWnd" };
static int dd_nmethods(int ver) { return ver == 1 ? 23 : ver == 2 ? 24 : ver == 4 ? 28 : 30; }
static const char *dd_ifname(int ver) { return ver == 1 ? "IDirectDraw" : ver == 2 ? "IDirectDraw2" : ver == 4 ? "IDirectDraw4" : "IDirectDraw7"; }
static int dds_nmethods(int ver) { return ver == 1 ? 36 : ver == 2 ? 39 : ver == 3 ? 40 : ver == 4 ? 45 : 49; }
static const char *dds_ifname(int ver) { return ver == 1 ? "IDirectDrawSurface" : ver == 2 ? "IDirectDrawSurface2" : ver == 3 ? "IDirectDrawSurface3" : ver == 4 ? "IDirectDrawSurface4" : "IDirectDrawSurface7"; }
static int dds_slot(int ver) { return ver == 7 ? 5 : ver; }   /* ifaces[] index */
static u64 g_ddvt[8], g_ddsvt[8], g_ddpvt, g_ddcvt; static Win32 *g_ddvt_owner;
static u64 dd_make_vtable(Cpu *c, const char *iface, const char *const *names, int n) {
  Win32 *w = W; u64 psz = PSZ_OF(c); u64 vt = w32_heap_alloc(w, (u64)n * psz, 1); if (!vt) return 0;
  for (int i = 0; i < n; i++) { char nm[80]; e_snprintf(nm, sizeof nm, "%s::%s", iface, names[i]); wrp(c, vt + psz * i, w32_shim_slot(w, "ddraw", nm)); } return vt; }
static void dd_vt_reset(Win32 *w) { if (g_ddvt_owner != w) { e_memset(g_ddvt, 0, sizeof g_ddvt); e_memset(g_ddsvt, 0, sizeof g_ddsvt); g_ddpvt = g_ddcvt = 0; g_ddvt_owner = w; } }
static u64 dd_vtable(Cpu *c, int ver) { dd_vt_reset(W); if (!g_ddvt[ver]) g_ddvt[ver] = dd_make_vtable(c, dd_ifname(ver), vt_IDirectDraw, dd_nmethods(ver)); return g_ddvt[ver]; }
static u64 dds_vtable(Cpu *c, int ver) { dd_vt_reset(W); if (!g_ddsvt[ver]) g_ddsvt[ver] = dd_make_vtable(c, dds_ifname(ver), vt_IDirectDrawSurface, dds_nmethods(ver)); return g_ddsvt[ver]; }
static u64 ddp_vtable(Cpu *c) { dd_vt_reset(W); if (!g_ddpvt) g_ddpvt = dd_make_vtable(c, "IDirectDrawPalette", vt_IDirectDrawPalette, 7); return g_ddpvt; }
static u64 ddc_vtable(Cpu *c) { dd_vt_reset(W); if (!g_ddcvt) g_ddcvt = dd_make_vtable(c, "IDirectDrawClipper", vt_IDirectDrawClipper, 9); return g_ddcvt; }
static u64 dd_obj_new(Cpu *c, u64 vt, int id, int ver, int kind) { u64 a = w32_heap_alloc(W, 32, 1); if (!vt || !a) return 0; wrp(c, a, vt); wr32(c, a + 8, (u32)id); wr32(c, a + 12, 1); wr32(c, a + 16, (u32)ver); wr32(c, a + 20, (u32)kind); wr32(c, a + 24, 0); return a; }
static int dd_obj_kind(Cpu *c, u64 self, int kind, u32 *id) { if (!self || !mem_ptr(c->mem, self, 32, 0)) return 0; if (rd32(c, self + 20) != (u32)kind) return 0; *id = rd32(c, self + 8); return 1; }
/* ---------------- devices ---------------- */
static u64 dd_new(Cpu *c, int ver) {
  int id; for (id = 1; id < DD_MAX && g_ddobj[id].used; id++) { } if (id == DD_MAX) return 0;
  u64 a = dd_obj_new(c, dd_vtable(c, ver), id, ver, DDK_DEVICE); if (!a) return 0;
  g_ddobj[id].used = 1; g_ddobj[id].addr = a; g_ddobj[id].w = W; g_ddobj[id].coop = 0; g_ddobj[id].coop_hwnd = 0; return a;
}
static DdDev *dd_of(Cpu *c, u64 self) { u32 id; if (!dd_obj_kind(c, self, DDK_DEVICE, &id)) return 0; if (id >= DD_MAX || !g_ddobj[id].used || g_ddobj[id].addr != self) return 0; return &g_ddobj[id]; }
static int dd_id(DdDev *d) { return (int)(d - g_ddobj); }
/* IID first dwords: IDirectDraw 6C14DB80, IDirectDraw2 B3A6F3E0, IDirectDraw4 9C59509A, IDirectDraw7 15E65EC0, IUnknown 00000000 */
static int dd_ver_of_iid(u32 d1) { return d1 == 0x6C14DB80u ? 1 : d1 == 0xB3A6F3E0u ? 2 : d1 == 0x9C59509Au ? 4 : d1 == 0x15E65EC0u ? 7 : d1 == 0 ? 1 : 0; }
static const char *dd_iid_name(u32 d1) {   /* interfaces programs ask DirectDraw objects for that this driver does not provide */
  switch (d1) { case 0x3BBA0080u: return "IDirect3D"; case 0x6AAE1EC1u: return "IDirect3D2"; case 0xBB223240u: return "IDirect3D3"; case 0xF5049E77u: return "IDirect3D7";
    case 0x69C11C3Eu: return "IDirectDrawGammaControl"; case 0x4B9F0EE0u: return "IDirectDrawColorControl"; case 0x2A1F7D53u: return "IDirect3DTexture"; case 0x93281502u: return "IDirect3DTexture2";
    case 0x6C14DB82u: return "IDirectDrawFactory"; case 0x9C59509Bu: return "IDirectDrawKernel"; default: return 0; } }
static void dd_QueryInterface(Cpu *c) {
  DdDev *o = dd_of(c, ARG(0)); u64 riid = ARG(1), out = ARG(2); if (!o || !out || !riid) { RET(DDERR_INVALIDPARAMS); return; }
  u32 d1 = rd32(c, riid); int ver = dd_ver_of_iid(d1);
  if (!ver) { wrp(c, out, 0); const char *nm = dd_iid_name(d1); if (!g_dd_log_d3d++ || nm) e_log("[ddraw] IDirectDraw::QueryInterface %s{%08x-…}: E_NOINTERFACE (%s)\n", nm ? nm : "", d1, nm && nm[7] == '3' ? "Direct3D 3/5/6/7 through DirectDraw is not provided by this driver" : "not provided"); RET(E_NOINTERFACE_); return; }
  if (d1 == 0) ver = (int)rd32(c, o->addr + 16);   /* IUnknown: the object itself */
  u64 n = dd_new(c, ver); if (!n) { RET(DDERR_OUTOFMEMORY); return; } DdDev *d = dd_of(c, n); d->coop = o->coop; d->coop_hwnd = o->coop_hwnd; wrp(c, out, n); RET(DD_OK);
}
static void dd_AddRef(Cpu *c) { DdDev *o = dd_of(c, ARG(0)); if (!o) { RET(0); return; } u32 r = rd32(c, o->addr + 12) + 1; wr32(c, o->addr + 12, r); RET(r); }
static void dd_Release(Cpu *c) { DdDev *o = dd_of(c, ARG(0)); if (!o) { RET(0); return; } u32 r = rd32(c, o->addr + 12); if (r) r--; wr32(c, o->addr + 12, r); if (!r) { o->used = 0; w32_heap_free(W, o->addr); o->addr = 0; } RET(r); }
static void dd_Compact(Cpu *c) { RET(DD_OK); }
static void dd_Initialize(Cpu *c) { RET(DDERR_ALREADYINITIALIZED); }
static void dd_WaitForVerticalBlank(Cpu *c) { RET(DD_OK); }
static void dd_GetScanLine(Cpu *c) { if (ARG(1)) wr32(c, ARG(1), (u32)((u64)host_now_ms() % 16) * ((u32)W->screen_h / 16)); RET(DD_OK); }
static void dd_GetVerticalBlankStatus(Cpu *c) { if (ARG(1)) wr32(c, ARG(1), 0); RET(DD_OK); }
static void dd_GetMonitorFrequency(Cpu *c) { if (!ARG(1)) { RET(DDERR_INVALIDPARAMS); return; } wr32(c, ARG(1), 60); RET(DD_OK); }
static void dd_GetFourCCCodes(Cpu *c) { if (ARG(1)) wr32(c, ARG(1), 0); RET(DD_OK); }
static void dd_SetCooperativeLevel(Cpu *c) { DdDev *o = dd_of(c, ARG(0)); if (!o) { RET(DDERR_INVALIDPARAMS); return; } u32 fl = (u32)ARG(2);
  if ((fl & 0x10) && !(fl & 0x1)) { RET(DDERR_INVALIDPARAMS); return; }   /* DDSCL_EXCLUSIVE requires DDSCL_FULLSCREEN */
  if ((fl & 0x1) && !ARG(1) && !(fl & 0x80)) { RET(DDERR_INVALIDPARAMS); return; }
  o->coop = fl; if (ARG(1)) o->coop_hwnd = ARG(1); wr32(c, o->addr + 24, fl);
  for (int i = 1; i < DD_MAX; i++) if (g_ddobj[i].used && g_ddobj[i].w == W) { g_ddobj[i].coop = fl; if (ARG(1)) g_ddobj[i].coop_hwnd = ARG(1); }   /* one level per process */
  RET(DD_OK); }
static void dd_TestCooperativeLevel(Cpu *c) { RET(DD_OK); }
static void dd_StartModeTest(Cpu *c) { RET(DDERR_TESTFINISHED); }
static void dd_EvaluateMode(Cpu *c) { if (ARG(2)) wr32(c, ARG(2), 0); RET(DD_OK); }
static int dd_cur_bpp(void) { return g_dd_mode_set ? g_dd_mode_bpp : 32; }
static int dd_cur_w(void) { return g_dd_mode_set ? g_dd_mode_w : DESK_W; }
static int dd_cur_h(void) { return g_dd_mode_set ? g_dd_mode_h : DESK_H; }
/* DDCAPS (DDCAPS_DX7, 380 B): dwSize 0, dwCaps 4, dwCaps2 8, dwCKeyCaps 12, dwFXCaps 16, dwFXAlphaCaps 20, dwPalCaps 24, dwSVCaps 28, dwAlphaBltConstBitDepths 32 … dwZBufferBitDepths 56, dwVidMemTotal 60,
   dwVidMemFree 64, dwMaxVisibleOverlays 68, dwCurrVisibleOverlays 72, dwNumFourCCCodes 76, dwAlignBoundarySrc 80 … dwRops[8] 92 (DDCAPS_DX1: 92 = ddsCaps), ddsOldCaps 96, dwMinOverlayStretch 100 … ddsCaps 364 */
static void dd_fill_caps(Cpu *c, u64 p) {
  u32 sz = rd32(c, p); if (sz < 8) return; u8 *m = mem_ptr(c->mem, p, sz, 1); if (!m) return; e_memset(m + 4, 0, sz - 4);
  u32 scaps = 0x4u | 0x8u | 0x10u | 0x20u | 0x40u | 0x100u | 0x200u | 0x800u | 0x1000u | 0x2000u | 0x4000u | 0x8000u | 0x20000u | 0x400000u | 0x10000000u;
  wr32(c, p + 4, 0x1u | 0x20u | 0x40u | 0x80u | 0x200u | 0x1000u | 0x20000u | 0x40000u | 0x80000u | 0x2000000u | 0x4000000u | 0x8u | 0x10u);   /* DDCAPS_3D BLTSTRETCH BLT BLTQUEUE COLORKEY GDI PALETTE PALETTEVSYNC READSCANLINE CANBLTSYSMEM BLTCOLORFILL BLTDEPTHFILL BLTFOURCC */
  if (sz >= 12) wr32(c, p + 8, 0x1u | 0x200u | 0x1000u | 0x80000u | 0x800000u | 0x2000000u);   /* DDCAPS2_CERTIFIED NONLOCALVIDMEM WIDESURFACES CANRENDERWINDOWED CANMANAGETEXTURE PRIMARYGAMMA? (no: 0x2000000 = DDCAPS2_FLIPNOVSYNC) */
  if (sz >= 16) wr32(c, p + 12, 0x1u | 0x2u | 0x4u | 0x100u | 0x200u | 0x400u);   /* DDCKEYCAPS_DESTBLT DESTBLTCLRSPACE DESTBLTCLRSPACEYUV? no: SRCBLT SRCBLTCLRSPACE … (dest + src blt keys, colour spaces) */
  if (sz >= 20) wr32(c, p + 16, 0x20u | 0x40u | 0x80u | 0x400u | 0x1000u | 0x4000u | 0x10000u | 0x8u);   /* DDFXCAPS_BLTARITHSTRETCHY MIRRORLEFTRIGHT MIRRORUPDOWN SHRINKX SHRINKY STRETCHX STRETCHY BLTALPHA? (0x8 = BLTARITHSTRETCHYN) */
  if (sz >= 28) wr32(c, p + 24, 0x4u | 0x10u | 0x40u | 0x80u | 0x1u | 0x100u | 0x200u);   /* DDPCAPS_8BIT PRIMARYSURFACE ALLOW256 VSYNC 4BIT 1BIT 2BIT */
  if (sz >= 60) wr32(c, p + 56, 0x400u | 0x200u | 0x100u);   /* DDBD_16 | DDBD_24 | DDBD_32 */
  if (sz >= 68) { wr32(c, p + 60, DD_VIDMEM); wr32(c, p + 64, DD_VIDMEM - (2u << 20)); }
  if (sz >= 100) { for (int k = 0; k < 8; k++) wr32(c, p + 92 - 32 + 32 + k * 4, 0); u32 rops[8] = {0}; rops[0xCC >> 5] |= 1u << (0xCC & 31); rops[0x00 >> 5] |= 1u << 0; rops[0xFF >> 5] |= 1u << (0xFF & 31); rops[0xF0 >> 5] |= 1u << (0xF0 & 31); rops[0x66 >> 5] |= 1u << (0x66 & 31); rops[0x88 >> 5] |= 1u << (0x88 & 31); rops[0xEE >> 5] |= 1u << (0xEE & 31); rops[0x55 >> 5] |= 1u << (0x55 & 31);   /* SRCCOPY BLACKNESS WHITENESS PATCOPY SRCINVERT SRCAND SRCPAINT DSTINVERT */
    if (sz >= 124) for (int k = 0; k < 8; k++) wr32(c, p + 92 + k * 4, rops[k]); wr32(c, p + 96, scaps); if (sz >= 124) wr32(c, p + 96 + 28 - 28, scaps); }
  if (sz >= 380) wr32(c, p + 364, scaps);
}
static void dd_GetCaps(Cpu *c) { if (!g_dd_log_mem++) e_log("[ddraw] IDirectDraw::GetCaps / GetAvailableVidMem: video memory reported as 512 MB (the browser does not expose the adapter's real amount); display %dx%d %d bpp 60 Hz\n", dd_cur_w(), dd_cur_h(), dd_cur_bpp());
  if (!ARG(1) && !ARG(2)) { RET(DDERR_INVALIDPARAMS); return; } if (ARG(1)) dd_fill_caps(c, ARG(1)); if (ARG(2)) dd_fill_caps(c, ARG(2)); RET(DD_OK); }
static u64 dd_vidmem_used(Win32 *w) { u64 n = 0; for (int i = 1; i < DDS_MAX; i++) if (g_dds[i].used && g_dds[i].own == w && !g_dds[i].client_mem && (g_dds[i].caps & 0x4000)) n += (u64)g_dds[i].pitch * g_dds[i].h; return n; }
static void dd_GetAvailableVidMem(Cpu *c) { if (!g_dd_log_mem++) e_log("[ddraw] GetAvailableVidMem: video memory reported as 512 MB (the browser does not expose the adapter's real amount)\n");
  u64 used = dd_vidmem_used(W); u32 fre = (u32)(DD_VIDMEM - (2u << 20) - (used > DD_VIDMEM - (4u << 20) ? DD_VIDMEM - (4u << 20) : used));
  if (ARG(2)) wr32(c, ARG(2), DD_VIDMEM); if (ARG(3)) wr32(c, ARG(3), fre); RET(DD_OK); }
/* ---- DDSURFACEDESC / DDSURFACEDESC2 layout (32-bit / 64-bit offsets) ---- */
static int ddsd_p(Cpu *c) { return c->mode32 ? 0 : 4; }                 /* lpSurface slot: 36 / 40 */
static int ddsd_d(Cpu *c) { return c->mode32 ? 0 : 8; }                 /* everything after lpSurface shifts by 8 on 64-bit */
static u32 ddsd_size(Cpu *c, int v2) { return v2 ? (c->mode32 ? 124u : 136u) : (c->mode32 ? 108u : 120u); }
static u64 ddsd_pf(Cpu *c, u64 p) { return p + 72 + ddsd_d(c); }
static u64 ddsd_caps(Cpu *c, u64 p) { return p + 104 + ddsd_d(c); }
static void dd_fill_pf(Cpu *c, u64 pf, int bpp, u32 flags, u32 r, u32 g, u32 b, u32 a) { wr32(c, pf, 32); wr32(c, pf + 4, flags); wr32(c, pf + 8, 0); wr32(c, pf + 12, (u32)bpp); wr32(c, pf + 16, r); wr32(c, pf + 20, g); wr32(c, pf + 24, b); wr32(c, pf + 28, a); }
static void dd_mode_masks(int bpp, u32 *r, u32 *g, u32 *b) { if (bpp == 32 || bpp == 24) { *r = 0xff0000u; *g = 0xff00u; *b = 0xffu; } else if (bpp == 16) { *r = 0xf800u; *g = 0x07e0u; *b = 0x001fu; } else if (bpp == 15) { *r = 0x7c00u; *g = 0x03e0u; *b = 0x001fu; } else { *r = *g = *b = 0; } }
static void dd_fill_mode(Cpu *c, u64 p, u32 w, u32 h, u32 bpp, int v2) {
  u32 sz = rd32(c, p); u8 *m = mem_ptr(c->mem, p, sz, 1); if (!m || sz < 24) return; e_memset(m + 4, 0, sz - 4);
  wr32(c, p + 4, 0x1u | 0x2u | 0x8u | 0x1000u | 0x40000u | 0x20u);   /* CAPS, HEIGHT, WIDTH, PITCH, PIXELFORMAT, REFRESHRATE */
  wr32(c, p + 8, h); wr32(c, p + 12, w); wr32(c, p + 16, (w * ((bpp + 7) / 8) + 7) & ~7u); wr32(c, p + 24, 60);
  if (sz < ddsd_size(c, v2)) return; u32 r, g, b; dd_mode_masks((int)bpp, &r, &g, &b);
  dd_fill_pf(c, ddsd_pf(c, p), (int)bpp, 0x40u | (bpp == 8 ? 0x20u : 0), r, g, b, 0); wr32(c, ddsd_caps(c, p), 0x200u | 0x8000u | 0x4000u | 0x10000000u);
}
static void dd_GetDisplayMode(Cpu *c) { u64 p = ARG(1); if (!p) { RET(DDERR_INVALIDPARAMS); return; } int ver = (int)rd32(c, ARG(0) + 16); dd_fill_mode(c, p, (u32)dd_cur_w(), (u32)dd_cur_h(), (u32)dd_cur_bpp(), ver >= 4); RET(DD_OK); }
/* display modes offered: the current desktop plus the classic set, 32 / 16 / 8 bpp (all 60 Hz) */
static const u16 dd_modes[][2] = { {320, 200}, {320, 240}, {400, 300}, {512, 384}, {640, 400}, {640, 480}, {720, 480}, {800, 600}, {1024, 768}, {1152, 864}, {1280, 720}, {1280, 768}, {1280, 800}, {1280, 960}, {1280, 1024}, {1366, 768}, {1440, 900}, {1600, 900}, {1600, 1200}, {1680, 1050}, {1920, 1080}, {1920, 1200} };
#define DD_NMODES ((int)(sizeof dd_modes / sizeof dd_modes[0]))
static int dd_mode_at(Cpu *c, int k, u32 *w, u32 *h, u32 *bpp) {   /* k over 3 × (DD_NMODES + 1): index DD_NMODES = the current desktop; -1 = skip */
  int bppi = k / (DD_NMODES + 1), mi = k % (DD_NMODES + 1); if (bppi > 2) return 0; *bpp = bppi == 0 ? 32 : bppi == 1 ? 16 : 8; int dw = DESK_W, dh = DESK_H;
  if (mi == DD_NMODES) { *w = (u32)dw; *h = (u32)dh; for (int i = 0; i < DD_NMODES; i++) if (dd_modes[i][0] == *w && dd_modes[i][1] == *h) return -1; return 1; }
  *w = dd_modes[mi][0]; *h = dd_modes[mi][1]; if (*w > 4096u || *h > 4096u) return -1; return 1;
}
static int dd_enum_step(Cpu *c, int ci) {   /* data: 0 desc (guest heap), 1 callback, 2 context, 3 next index, 4 v2 flag, 5 filter desc */
  Win32 *w = W; for (;;) {
    int k = (int)w->cont[ci].data[3]; u32 mw, mh, bpp; int r = dd_mode_at(c, k, &mw, &mh, &bpp); if (!r) return 0; w->cont[ci].data[3] = (u64)(k + 1); if (r < 0) continue;
    u64 f = w->cont[ci].data[5];
    if (f) { u32 fl = rd32(c, f + 4); if ((fl & 0x2) && rd32(c, f + 8) != mh) continue; if ((fl & 0x4) && rd32(c, f + 12) != mw) continue; if ((fl & 0x1000) && rd32(c, ddsd_pf(c, f) + 12) != bpp) continue; if ((fl & 0x40000) && rd32(c, f + 24) && rd32(c, f + 24) != 60) continue; }
    u64 p = w->cont[ci].data[0]; wr32(c, p, ddsd_size(c, (int)w->cont[ci].data[4])); dd_fill_mode(c, p, mw, mh, bpp, (int)w->cont[ci].data[4]);
    w32_guest_call(c, w->cont[ci].data[1], p, w->cont[ci].data[2], 0, 0); return 1;
  }
}
static void dd_EnumDisplayModes(Cpu *c) {
  u64 filter = ARG(2), ctx = ARG(3), cb = ARG(4); if (!cb) { RET(DDERR_INVALIDPARAMS); return; }
  int ver = (int)rd32(c, ARG(0) + 16); u64 p = w32_heap_alloc(W, 144, 1); if (!p) { RET(DDERR_OUTOFMEMORY); return; }
  w32_push_cont(c, CONT_DDENUM, p); int i = W->ncont - 1; W->cont[i].data[1] = cb; W->cont[i].data[2] = ctx; W->cont[i].data[3] = 0; W->cont[i].data[4] = ver >= 4; W->cont[i].data[5] = filter;
  if (!dd_enum_step(c, i)) { w32_heap_free(W, p); w32_cont_finish(c, DD_OK); }
}
static void dd_enum_resume(Cpu *c, u64 rax) { Win32 *w = W; int i = w->ncont - 1; if ((u32)rax == 0 || !dd_enum_step(c, i)) { w32_heap_free(w, w->cont[i].data[0]); w32_cont_finish(c, DD_OK); } }
/* DDDEVICEIDENTIFIER(2): szDriver[512] 0, szDescription[512] 512, liDriverVersion 1024, dwVendorId 1032, dwDeviceId 1036, dwSubSysId 1040, dwRevision 1044, guidDeviceIdentifier 1048, dwWHQLLevel 1064 (v2 only) */
static void dd_GetDeviceIdentifier(Cpu *c) {
  u64 p = ARG(1); int ver = (int)rd32(c, ARG(0) + 16); u32 sz = ver >= 7 ? 1068 : 1064; u8 *m = mem_ptr(c->mem, p, sz, 1); if (!m) { RET(DDERR_INVALIDPARAMS); return; }
  e_memset(m, 0, sz); const char *drv = "nebula_webgl.dll", *desc = "Nebula OS display adapter (WebGL 2 on the device GPU)";
  for (int i = 0; drv[i]; i++) m[i] = (u8)drv[i]; for (int i = 0; desc[i]; i++) m[512 + i] = (u8)desc[i];
  wr32(c, p + 1024, 0x00020000u); wr32(c, p + 1028, 0x00330001u);   /* driver version 1.51.1.0 */ wr32(c, p + 1032, 0x1AF4u); wr32(c, p + 1036, 0x1050u); wr32(c, p + 1040, 0); wr32(c, p + 1044, 1);
  wr32(c, p + 1048, 0x4E45424Cu); wr32(c, p + 1052, 0x574C4744u); wr32(c, p + 1056, 0x00010000u); wr32(c, p + 1060, 0x00000001u); RET(DD_OK);
}
/* ---------------- pixel formats ---------------- */
static void dd_mask_shift(u32 m, int *sh, int *bits) { int s = 0, b = 0; if (m) { while (!((m >> s) & 1)) s++; while (s + b < 32 && ((m >> (s + b)) & 1)) b++; } *sh = s; *bits = b; }
static void dds_set_format(DdSurf *s, int bpp, u32 pf_flags, u32 r, u32 g, u32 b, u32 a) {
  s->bpp = bpp; s->pf_flags = pf_flags; s->pal8 = (pf_flags & 0x20u) || (bpp == 8 && !r); s->zbuf = (pf_flags & 0x400u) != 0;
  if (!s->pal8 && !s->zbuf && !r && !g && !b) dd_mode_masks(bpp == 16 && (pf_flags & 0x1u) ? 15 : bpp, &r, &g, &b);
  s->rmask = r; s->gmask = g; s->bmask = b; s->amask = (pf_flags & 0x1u) ? a : 0;
  dd_mask_shift(s->rmask, &s->rsh, &s->rbits); dd_mask_shift(s->gmask, &s->gsh, &s->gbits); dd_mask_shift(s->bmask, &s->bsh, &s->bbits); dd_mask_shift(s->amask, &s->ash, &s->abits);
}
static inline u32 dd_expand_chan(u32 v, int bits) { if (!bits) return 0; if (bits >= 8) return v >> (bits - 8); u32 o = v << (8 - bits); return o | (o >> bits); }
static inline u32 dd_native_rd(const DdSurf *s, const u8 *row, int x) { switch (s->bpp) { case 8: return row[x]; case 16: return row[x * 2] | (u32)row[x * 2 + 1] << 8; case 24: return row[x * 3] | (u32)row[x * 3 + 1] << 8 | (u32)row[x * 3 + 2] << 16; case 32: return ((const u32 *)row)[x]; case 4: return (row[x >> 1] >> ((x & 1) ? 0 : 4)) & 15; case 1: return (row[x >> 3] >> (7 - (x & 7))) & 1; default: return 0; } }
static inline void dd_native_wr(const DdSurf *s, u8 *row, int x, u32 v) { switch (s->bpp) { case 8: row[x] = (u8)v; break; case 16: row[x * 2] = (u8)v; row[x * 2 + 1] = (u8)(v >> 8); break; case 24: row[x * 3] = (u8)v; row[x * 3 + 1] = (u8)(v >> 8); row[x * 3 + 2] = (u8)(v >> 16); break; case 32: ((u32 *)row)[x] = v; break;
  case 4: if (x & 1) row[x >> 1] = (u8)((row[x >> 1] & 0xf0) | (v & 15)); else row[x >> 1] = (u8)((row[x >> 1] & 0x0f) | ((v & 15) << 4)); break; case 1: { u8 m = (u8)(0x80 >> (x & 7)); if (v & 1) row[x >> 3] |= m; else row[x >> 3] &= (u8)~m; break; } default: break; } }
static DdPal *dds_pal_of(DdSurf *s);
/* native pixel → 0x00RRGGBB (palettised: through the attached palette, else the primary's; z-buffers: grey) */
static inline u32 dd_to_rgb(const DdSurf *s, const u32 *pal, u32 v) {
  if (s->pal8) return pal ? pal[v & 255] & 0xffffff : (v & 255) * 0x010101u;
  if (s->zbuf) { u32 g = s->bpp >= 24 ? v >> (s->bpp - 8) : s->bpp == 16 ? v >> 8 : v; return (g & 255) * 0x010101u; }
  u32 r = dd_expand_chan((v & s->rmask) >> s->rsh, s->rbits), g = dd_expand_chan((v & s->gmask) >> s->gsh, s->gbits), b = dd_expand_chan((v & s->bmask) >> s->bsh, s->bbits); return r << 16 | g << 8 | b; }
static inline u32 dd_from_rgb(const DdSurf *s, u32 rgb) {
  u32 r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
  u32 v = ((s->rbits ? r >> (8 - (s->rbits > 8 ? 8 : s->rbits)) : 0) << s->rsh) | ((s->gbits ? g >> (8 - (s->gbits > 8 ? 8 : s->gbits)) : 0) << s->gsh) | ((s->bbits ? b >> (8 - (s->bbits > 8 ? 8 : s->bbits)) : 0) << s->bsh);
  if (s->abits) v |= s->amask; return v; }
/* nearest palette index for packing GDI output into a palettised surface: 15-bit lookup table rebuilt when the palette changes */
static u8 *g_dd_lut; static u32 g_dd_lut_gen; static int g_dd_lut_pal;
static u32 dd_pal_nearest(DdPal *p, u32 rgb) {
  if (!p) return (rgb & 255);   /* no palette: grey ramp */
  u32 key = ((rgb >> 9) & 0x7c00) | ((rgb >> 6) & 0x3e0) | ((rgb >> 3) & 0x1f);
  if (!g_dd_lut) g_dd_lut = emu_alloc(32768); if (!g_dd_lut) return 0;
  if (g_dd_lut_pal != (int)(p - g_ddp) + 1 || g_dd_lut_gen != p->gen) { e_memset(g_dd_lut, 0xff, 32768); g_dd_lut_pal = (int)(p - g_ddp) + 1; g_dd_lut_gen = p->gen; }
  if (g_dd_lut[key] != 0xff) return g_dd_lut[key];
  int best = 0; u32 bd = 0xffffffffu; for (int i = 0; i < p->ncol; i++) { u32 e = p->pal[i]; if ((e & 0xffffff) == (rgb & 0xffffff)) { best = i; break; } int dr = (int)((e >> 16) & 255) - (int)((rgb >> 16) & 255), dg = (int)((e >> 8) & 255) - (int)((rgb >> 8) & 255), db = (int)(e & 255) - (int)(rgb & 255); u32 d = (u32)(dr * dr + dg * dg + db * db); if (d < bd) { bd = d; best = i; } }
  g_dd_lut[key] = (u8)best; return (u32)best; }
/* ---------------- surfaces ---------------- */
static DdSurf *dds_of(Cpu *c, u64 self) { u32 id; if (!dd_obj_kind(c, self, DDK_SURFACE, &id)) return 0; if (id >= DDS_MAX || !g_dds[id].used) return 0; DdSurf *s = &g_dds[id]; for (int k = 0; k < 8; k++) if (s->ifaces[k] == self) return s; return 0; }
static int dds_id(DdSurf *s) { return (int)(s - g_dds); }
static DdPal *ddp_of(Cpu *c, u64 self) { u32 id; if (!dd_obj_kind(c, self, DDK_PALETTE, &id)) return 0; if (id >= DDP_MAX || !g_ddp[id].used || g_ddp[id].ifaces[0] != self) return 0; return &g_ddp[id]; }
static DdClip *ddc_of(Cpu *c, u64 self) { u32 id; if (!dd_obj_kind(c, self, DDK_CLIPPER, &id)) return 0; if (id >= DDC_MAX || !g_ddc[id].used || g_ddc[id].ifaces[0] != self) return 0; return &g_ddc[id]; }
static DdSurf *dds_primary(Win32 *w) { for (int i = 1; i < DDS_MAX; i++) if (g_dds[i].used && g_dds[i].own == w && g_dds[i].primary) return &g_dds[i]; return 0; }
static DdPal *dds_pal_of(DdSurf *s) { if (s->palette > 0 && g_ddp[s->palette].used) return &g_ddp[s->palette]; DdSurf *p = dds_primary(s->own); if (p && p != s && p->palette > 0 && g_ddp[p->palette].used) return &g_ddp[p->palette]; return 0; }
static u8 *dds_mem(DdSurf *s) { return mem_ptr(s->own->mem, s->bits, (u64)s->pitch * s->h, 1); }
static u64 dds_iface(Cpu *c, DdSurf *s, int ver) {   /* the guest object for one interface version (created on demand; one AddRef per QueryInterface is done by the caller) */
  int k = dds_slot(ver); if (s->ifaces[k]) return s->ifaces[k]; u64 a = dd_obj_new(c, dds_vtable(c, ver), dds_id(s), ver, DDK_SURFACE); if (!a) return 0; wr32(c, a + 12, 0); s->ifaces[k] = a; return a; }
static int dds_total_refs(Cpu *c, DdSurf *s) { int n = 0; for (int k = 0; k < 8; k++) if (s->ifaces[k]) n += (int)rd32(c, s->ifaces[k] + 12); return n; }
static void dds_dc_free(Win32 *w, DdSurf *s) { if (s->dc) { GdiObj *o = gdi_get(w, s->dc); if (o) o->kind = 0; } if (s->dc_bitmap) { GdiObj *o = gdi_get(w, s->dc_bitmap); if (o) { mem_unmap(w->mem, o->bits); o->kind = 0; } } s->dc = s->dc_bitmap = 0; s->dc_out = 0; }
static void dds_free(Cpu *c, DdSurf *s);
static void dds_detach_all(Cpu *c, DdSurf *s) { for (int k = 0; k < s->natt; k++) { DdSurf *a = &g_dds[s->att[k]]; if (a->used && a->att_parent == dds_id(s)) { a->att_parent = 0; if (!dds_total_refs(c, a)) dds_free(c, a); } } s->natt = 0; }
static void dds_free(Cpu *c, DdSurf *s) {
  Win32 *w = s->own; if (!s->used) return; s->used = 0; dds_detach_all(c, s);
  if (s->att_parent > 0 && g_dds[s->att_parent].used) { DdSurf *p = &g_dds[s->att_parent]; for (int k = 0; k < p->natt; k++) if (p->att[k] == dds_id(s)) { p->att[k] = p->att[--p->natt]; break; } }
  dds_dc_free(w, s); if (!s->client_mem && s->bits) mem_unmap(w->mem, s->bits);
  for (int k = 0; k < 8; k++) if (s->ifaces[k]) { w32_heap_free(w, s->ifaces[k]); s->ifaces[k] = 0; }
  for (int k = 0; k < s->npriv; k++) if (s->priv_data[k] && !(s->priv_flags[k] & 1)) w32_heap_free(w, s->priv_data[k]);
  if (s->palette > 0 && g_ddp[s->palette].used && --g_ddp[s->palette].refs <= 0 && !rd32(c, g_ddp[s->palette].ifaces[0] + 12)) { w32_heap_free(w, g_ddp[s->palette].ifaces[0]); g_ddp[s->palette].used = 0; }
  if (s->clipper > 0 && g_ddc[s->clipper].used && --g_ddc[s->clipper].refs <= 0 && !rd32(c, g_ddc[s->clipper].ifaces[0] + 12)) { w32_heap_free(w, g_ddc[s->clipper].ifaces[0]); g_ddc[s->clipper].used = 0; }
  s->bits = 0; s->palette = s->clipper = 0;
}
/* ---- presenting the primary surface to the framebuffer ---- */
static void dd_rect_clip(int *x0, int *y0, int *x1, int *y1, int w, int h) { if (*x0 < 0) *x0 = 0; if (*y0 < 0) *y0 = 0; if (*x1 > w) *x1 = w; if (*y1 > h) *y1 = h; }
static int ddc_window_rect(DdClip *cl, int *x0, int *y0, int *x1, int *y1) {   /* clipper window: client rectangle in primary (= screen) coordinates */
  int i = wnd_of(cl->hwnd); if (i < 0 || !wnd_alive(i)) return 0; int sx, sy; wnd_client_origin(i, &sx, &sy); *x0 = sx + SCREEN_X0; *y0 = sy + SCREEN_Y0; *x1 = *x0 + wnd_client_w(i); *y1 = *y0 + wnd_client_h(i); return g_wnd[i].visible || 1; }
static void dd_present_rect(DdSurf *s, int x0, int y0, int x1, int y1) {   /* primary pixels [x0,x1)×[y0,y1) → framebuffer (same coordinates minus the screen origin) */
  Win32 *w = s->own; if (!w->screen || !s->primary) return; u8 *m = dds_mem(s); if (!m) return;
  dd_rect_clip(&x0, &y0, &x1, &y1, s->w, s->h); int fx0 = x0 - SCREEN_X0, fy0 = y0 - SCREEN_Y0, fx1 = x1 - SCREEN_X0, fy1 = y1 - SCREEN_Y0;
  if (fx0 < 0) { x0 += -fx0; fx0 = 0; } if (fy0 < 0) { y0 += -fy0; fy0 = 0; } if (fx1 > w->screen_w) { x1 -= fx1 - w->screen_w; fx1 = w->screen_w; } if (fy1 > w->screen_h) { y1 -= fy1 - w->screen_h; fy1 = w->screen_h; }
  if (x1 <= x0 || y1 <= y0) return; DdPal *pal = dds_pal_of(s); const u32 *pe = pal ? pal->pal : 0;
  for (int y = y0; y < y1; y++) { const u8 *row = m + (size_t)y * s->pitch; u32 *d = w->screen + (size_t)(y - SCREEN_Y0) * w->screen_w + (x0 - SCREEN_X0);
    if (s->bpp == 32 && s->rmask == 0xff0000u && s->gmask == 0xff00u && s->bmask == 0xffu) { const u32 *sp = (const u32 *)row + x0; for (int x = x0; x < x1; x++) *d++ = *sp++ & 0xffffff; }
    else for (int x = x0; x < x1; x++) *d++ = dd_to_rgb(s, pe, dd_native_rd(s, row, x)); }
  w->dirty = 1; w->blits_since_yield++;
}
static void dd_present(DdSurf *s, const int *rc) {   /* rc: primary-coordinate rectangle or NULL = all; clipped to the clipper window when one is attached */
  if (!s->primary) return; int x0 = rc ? rc[0] : 0, y0 = rc ? rc[1] : 0, x1 = rc ? rc[2] : s->w, y1 = rc ? rc[3] : s->h;
  if (s->clipper > 0 && g_ddc[s->clipper].used) { DdClip *cl = &g_ddc[s->clipper]; if (cl->hwnd) { int cx0, cy0, cx1, cy1; if (!ddc_window_rect(cl, &cx0, &cy0, &cx1, &cy1)) return; if (cx0 > x0) x0 = cx0; if (cy0 > y0) y0 = cy0; if (cx1 < x1) x1 = cx1; if (cy1 < y1) y1 = cy1; dd_present_rect(s, x0, y0, x1, y1); return; }
    if (cl->nrects) { for (int k = 0; k < cl->nrects; k++) { int rx0 = cl->rects[k][0] > x0 ? cl->rects[k][0] : x0, ry0 = cl->rects[k][1] > y0 ? cl->rects[k][1] : y0, rx1 = cl->rects[k][2] < x1 ? cl->rects[k][2] : x1, ry1 = cl->rects[k][3] < y1 ? cl->rects[k][3] : y1; dd_present_rect(s, rx0, ry0, rx1, ry1); } return; } }
  dd_present_rect(s, x0, y0, x1, y1);
}
static void dd_present_all_palettised(Win32 *w, DdPal *p) { DdSurf *s = dds_primary(w); if (s && s->pal8 && dds_pal_of(s) == p) dd_present(s, 0); }
/* the primary starts with what is on the desktop (windowed programs that only draw their own rectangle leave the rest intact) */
static void dd_primary_sync_in(DdSurf *s) { Win32 *w = s->own; if (!w->screen) return; u8 *m = dds_mem(s); if (!m) return; DdPal *pal = dds_pal_of(s);
  for (int y = 0; y < s->h; y++) { int fy = y - SCREEN_Y0; if (fy < 0 || fy >= w->screen_h) continue; u8 *row = m + (size_t)y * s->pitch;
    for (int x = 0; x < s->w; x++) { int fx = x - SCREEN_X0; if (fx < 0 || fx >= w->screen_w) continue; u32 rgb = w->screen[(size_t)fy * w->screen_w + fx] & 0xffffff; dd_native_wr(s, row, x, s->pal8 ? dd_pal_nearest(pal, rgb) : dd_from_rgb(s, rgb)); } } }
/* ---- creation ---- */
static int dds_alloc(Cpu *c, int sw, int sh, int bpp, u32 pf_flags, u32 r, u32 g, u32 b, u32 a, u32 caps, u32 caps2, u64 client_bits, int client_pitch, int dev) {
  Win32 *w = W; int id; for (id = 1; id < DDS_MAX && g_dds[id].used; id++) { } if (id == DDS_MAX) return -1;
  if (sw <= 0) sw = 1; if (sh <= 0) sh = 1; if ((u64)sw * sh > 64ull << 20) { e_log("[ddraw] surface %dx%d too large\n", sw, sh); return -1; }
  DdSurf *s = &g_dds[id]; e_memset(s, 0, sizeof *s); s->used = 1; s->own = w; s->dev = dev; s->w = sw; s->h = sh; s->caps = caps; s->caps2 = caps2; dds_set_format(s, bpp, pf_flags, r, g, b, a);
  s->pitch = client_pitch > 0 ? client_pitch : (int)(((u64)sw * s->bpp / 8 + 7) & ~7ull); if (s->bpp < 8) s->pitch = (int)((((u64)sw * s->bpp + 31) / 32) * 4);
  if (client_bits) { s->bits = client_bits; s->client_mem = 1; }
  else { u64 sz = (u64)s->pitch * sh; u64 addr = w->fb_cur; if (!mem_map(w->mem, addr, sz, "ddraw")) { s->used = 0; return -1; } w->fb_cur += (sz + 0xffff) & ~0xffffull; s->bits = addr; u8 *m = dds_mem(s); if (m) e_memset(m, 0, (size_t)sz); }
  s->uniq = 1; return id;
}
/* DDSURFACEDESC(2) of a surface (what GetSurfaceDesc / Lock / EnumSurfaces report) */
static void dds_fill_desc(Cpu *c, DdSurf *s, u64 p, int v2, u64 lock_ptr) {
  u32 sz = rd32(c, p); u8 *m = mem_ptr(c->mem, p, sz, 1); if (!m || sz < 24) return; e_memset(m + 4, 0, sz - 4);
  u32 fl = 0x1u | 0x2u | 0x4u | 0x8u | 0x1000u; wr32(c, p + 8, (u32)s->h); wr32(c, p + 12, (u32)s->w); wr32(c, p + 16, (u32)s->pitch);
  if (s->backbuffers) { fl |= 0x20u; wr32(c, p + 20, (u32)s->backbuffers); }
  if (s->zbuf && !v2) { fl |= 0x40u; wr32(c, p + 24, (u32)s->bpp); }
  if (s->caps & 0x400000u) { int n = 0; for (int k = dds_id(s); k > 0 && g_dds[k].natt; ) { int nx = -1; for (int j = 0; j < g_dds[k].natt; j++) if (g_dds[g_dds[k].att[j]].caps & 0x400000u) nx = g_dds[k].att[j]; if (nx < 0) break; n++; k = nx; } fl |= 0x20000u; wr32(c, p + 24, (u32)n + 1); }
  if (lock_ptr) { fl |= 0x800u; wrp(c, p + 36 + ddsd_p(c), lock_ptr); }
  u64 ck = p + 40 + ddsd_d(c); { static const int ord[4] = { 1, 0, 3, 2 }; static const u32 fbit[4] = { 0x2000u, 0x4000u, 0x8000u, 0x10000u };   /* desc order: dest overlay, dest blt, src overlay, src blt */
    for (int k = 0; k < 4; k++) if (s->ck_set[ord[k]]) { fl |= fbit[k]; wr32(c, ck + k * 8, s->ck[ord[k]][0]); wr32(c, ck + k * 8 + 4, s->ck[ord[k]][1]); } }
  dd_fill_pf(c, ddsd_pf(c, p), s->bpp, s->pf_flags, s->rmask, s->gmask, s->bmask, s->amask); wr32(c, ddsd_caps(c, p), s->caps); if (v2) wr32(c, ddsd_caps(c, p) + 4, s->caps2);
  wr32(c, p + 4, fl);
}
static int dd_hwnd_alive(u64 h) { int i = wnd_of(h); return i >= 0 && wnd_alive(i); }
static DdDev *dd_dev_of_proc(Win32 *w) { for (int i = 1; i < DD_MAX; i++) if (g_ddobj[i].used && g_ddobj[i].w == w) return &g_ddobj[i]; return 0; }
static void dd_CreateSurface(Cpu *c) {   /* (this, LPDDSURFACEDESC(2), LPDIRECTDRAWSURFACE *, IUnknown *outer) */
  DdDev *d = dd_of(c, ARG(0)); u64 desc = ARG(1), out = ARG(2); if (!d || !desc || !out) { RET(DDERR_INVALIDPARAMS); return; } if (ARG(3)) { RET(0x80040110u); return; }   /* CLASS_E_NOAGGREGATION */
  int dver = (int)rd32(c, d->addr + 16), v2 = dver >= 4, sver = dver == 1 ? 1 : dver == 2 ? 3 : dver == 4 ? 4 : 7;   /* IDirectDraw → IDirectDrawSurface, IDirectDraw2 → IDirectDrawSurface3 (what ddraw.dll hands out), 4 → 4, 7 → 7 */
  u32 sz = rd32(c, desc); if (sz != ddsd_size(c, v2)) { e_log("[ddraw] CreateSurface: dwSize %u (expected %u for %s)\n", sz, ddsd_size(c, v2), dd_ifname(dver)); RET(DDERR_INVALIDPARAMS); return; }
  u32 fl = rd32(c, desc + 4), caps = (fl & 0x1) ? rd32(c, ddsd_caps(c, desc)) : 0, caps2 = (fl & 0x1) && v2 ? rd32(c, ddsd_caps(c, desc) + 4) : 0; wrp(c, out, 0);
  int primary = (caps & 0x200u) != 0; int sw, sh, bpp = dd_cur_bpp(); u32 pff = 0x40u | (bpp == 8 ? 0x20u : 0), r = 0, g = 0, b = 0, a = 0; dd_mode_masks(bpp, &r, &g, &b);
  if (primary) { if (dds_primary(W)) { RET(DDERR_PRIMARYSURFACEALREADYEXISTS); return; } if ((fl & 0x4) && rd32(c, desc + 12) != (u32)dd_cur_w()) { RET(DDERR_INVALIDPARAMS); return; } sw = dd_cur_w(); sh = dd_cur_h(); }
  else { if (!(fl & 0x2) || !(fl & 0x4)) { RET(DDERR_INVALIDPARAMS); return; } sw = (int)rd32(c, desc + 12); sh = (int)rd32(c, desc + 8); if (sw <= 0 || sh <= 0 || sw > 16384 || sh > 16384) { RET(DDERR_INVALIDPARAMS); return; } }
  if ((fl & 0x1000u) && !primary) { u64 pf = ddsd_pf(c, desc); u32 pfl = rd32(c, pf + 4); if (pfl & 0x4u) { e_log("[ddraw] CreateSurface: FourCC %08x surfaces (YUV / compressed) are not provided\n", rd32(c, pf + 8)); RET(DDERR_INVALIDPIXELFORMAT); return; }
    if (pfl & 0x400u) { bpp = (int)rd32(c, pf + 12); pff = 0x400u; r = g = b = a = 0; if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) { RET(DDERR_INVALIDPIXELFORMAT); return; } }
    else if (pfl & 0x20u) { bpp = 8; pff = pfl; r = g = b = a = 0; } else if (pfl & 0x8u) { bpp = 4; pff = pfl; r = g = b = a = 0; } else if (pfl & 0x40u) { bpp = (int)rd32(c, pf + 12); pff = pfl; r = rd32(c, pf + 16); g = rd32(c, pf + 20); b = rd32(c, pf + 24); a = rd32(c, pf + 28); if (bpp != 16 && bpp != 24 && bpp != 32 && bpp != 8) { RET(DDERR_INVALIDPIXELFORMAT); return; } if (bpp == 8 && !r) pff |= 0x20u; }
    else if (pfl & 0x20000u) { bpp = (int)rd32(c, pf + 12); pff = pfl; r = g = b = rd32(c, pf + 16); a = rd32(c, pf + 28); }   /* luminance: all channels from the one mask */
    else { e_log("[ddraw] CreateSurface: pixel format flags %08x not provided\n", pfl); RET(DDERR_INVALIDPIXELFORMAT); return; } }
  if ((caps & 0x20000u) && !(fl & 0x1000u)) { bpp = (fl & 0x40u) ? (int)rd32(c, desc + 24) : 16; if (!bpp) bpp = 16; pff = 0x400u; r = g = b = a = 0; }   /* z-buffer by DDSD_ZBUFFERBITDEPTH */
  if (caps & 0x80u) { e_log("[ddraw] CreateSurface: overlay surfaces are not provided\n"); RET(DDERR_UNSUPPORTED); return; }
  u64 client = 0; int cpitch = 0; if (fl & 0x800u) { client = rdp(c, desc + 36 + ddsd_p(c)); cpitch = (fl & 0x8u) ? (int)rd32(c, desc + 16) : 0; if (!client) { RET(DDERR_INVALIDPARAMS); return; } if (!mem_ptr(c->mem, client, (u64)(cpitch ? cpitch : sw * bpp / 8) * sh, 1)) { RET(DDERR_INVALIDPARAMS); return; } }
  if (!(caps & 0x800u)) caps |= 0x4000u | 0x10000000u;   /* not SYSTEMMEMORY → VIDEOMEMORY | LOCALVIDMEM (what the driver would pick) */
  if (primary) caps |= 0x8000u;   /* VISIBLE */
  int nback = (fl & 0x20u) ? (int)rd32(c, desc + 20) : 0; if (nback && !(caps & 0x8u)) { RET(DDERR_INVALIDPARAMS); return; } if (nback > 3) nback = 3; if ((caps & 0x10u) && !primary && !nback) { RET(DDERR_INVALIDPARAMS); return; }
  int id = dds_alloc(c, sw, sh, bpp, pff, r, g, b, a, caps | (nback ? 0x20u : 0), caps2, client, cpitch, dd_id(d)); if (id < 0) { RET(DDERR_OUTOFMEMORY); return; }
  DdSurf *s = &g_dds[id]; s->primary = primary; s->backbuffers = nback;
  if (nback) { int prev = id; for (int k = 0; k < nback; k++) { int bid = dds_alloc(c, sw, sh, bpp, pff, r, g, b, a, (caps & ~(0x200u | 0x20u | 0x8000u)) | 0x4u | 0x10u | (k == 0 ? 0x4u : 0), caps2, 0, 0, dd_id(d)); if (bid < 0) { dds_free(c, s); RET(DDERR_OUTOFMEMORY); return; } g_dds[prev].att[g_dds[prev].natt++] = bid; g_dds[bid].att_parent = prev; prev = bid; } }
  if (primary) dd_primary_sync_in(s);
  u64 obj = dds_iface(c, s, sver); if (!obj) { dds_free(c, s); RET(DDERR_OUTOFMEMORY); return; } wr32(c, obj + 12, 1); wrp(c, out, obj);
  e_log("[ddraw] CreateSurface %s %dx%d %d bpp%s%s%s → %s\n", primary ? "primary" : (caps & 0x1000u) ? "texture" : (caps & 0x20000u) ? "z-buffer" : "offscreen", sw, sh, bpp, s->pal8 ? " palettised" : "", nback ? " + back buffers" : "", client ? " (client memory)" : "", dds_ifname(sver));
  RET(DD_OK);
}
static void dd_CreatePalette(Cpu *c) {   /* (this, flags, PALETTEENTRY *, IDirectDrawPalette **, outer) */
  DdDev *d = dd_of(c, ARG(0)); u32 fl = (u32)ARG(1); u64 ent = ARG(2), out = ARG(3); if (!d || !out) { RET(DDERR_INVALIDPARAMS); return; } if (ARG(4)) { RET(0x80040110u); return; }
  int id; for (id = 1; id < DDP_MAX && g_ddp[id].used; id++) { } if (id == DDP_MAX) { RET(DDERR_OUTOFMEMORY); return; }
  int n = (fl & 0x4u) ? 256 : (fl & 0x1u) ? 16 : (fl & 0x200u) ? 4 : (fl & 0x100u) ? 2 : 256; if (!ent) { RET(DDERR_INVALIDPARAMS); return; }
  u64 a = dd_obj_new(c, ddp_vtable(c), id, 1, DDK_PALETTE); if (!a) { RET(DDERR_OUTOFMEMORY); return; }
  DdPal *p = &g_ddp[id]; e_memset(p, 0, sizeof *p); p->used = 1; p->own = W; p->dev = dd_id(d); p->flags = fl & ~0x8u; p->ncol = n; p->ifaces[0] = a; p->gen = 1;
  for (int i = 0; i < n; i++) { u32 e = rd32(c, ent + i * 4); p->pal[i] = ((e & 255) << 16) | (e & 0xff00) | ((e >> 16) & 255) | (e & 0xff000000u); }   /* PALETTEENTRY r,g,b,flags → 0x00RRGGBB (flags kept in the top byte) */
  wrp(c, out, a); RET(DD_OK);
}
static void dd_clipper_new(Cpu *c, u64 out, DdDev *d) {
  int id; for (id = 1; id < DDC_MAX && g_ddc[id].used; id++) { } if (id == DDC_MAX) { RET(DDERR_OUTOFMEMORY); return; }
  u64 a = dd_obj_new(c, ddc_vtable(c), id, 1, DDK_CLIPPER); if (!a) { RET(DDERR_OUTOFMEMORY); return; }
  DdClip *cl = &g_ddc[id]; e_memset(cl, 0, sizeof *cl); cl->used = 1; cl->own = W; cl->ifaces[0] = a; cl->dev = d ? dd_id(d) : 0; wrp(c, out, a); RET(DD_OK);
}
static void dd_CreateClipper(Cpu *c) { /* (this, flags, IDirectDrawClipper **, outer) */ if (!ARG(2)) { RET(DDERR_INVALIDPARAMS); return; } if (ARG(3)) { RET(0x80040110u); return; } dd_clipper_new(c, ARG(2), dd_of(c, ARG(0))); }
static void s_DirectDrawCreateClipper(Cpu *c) { /* (flags, IDirectDrawClipper **, outer) */ if (!ARG(1)) { RET(DDERR_INVALIDPARAMS); return; } if (ARG(2)) { RET(0x80040110u); return; } dd_clipper_new(c, ARG(1), 0); }
static void dd_DuplicateSurface(Cpu *c) {   /* a duplicate shares the pixel memory */
  DdDev *d = dd_of(c, ARG(0)); DdSurf *s = dds_of(c, ARG(1)); u64 out = ARG(2); if (!d || !s || !out) { RET(DDERR_INVALIDPARAMS); return; } if (s->primary) { RET(DDERR_CANTDUPLICATE); return; }
  int id = dds_alloc(c, s->w, s->h, s->bpp, s->pf_flags, s->rmask, s->gmask, s->bmask, s->amask, s->caps, s->caps2, s->bits, s->pitch, dd_id(d)); if (id < 0) { RET(DDERR_OUTOFMEMORY); return; }
  DdSurf *n = &g_dds[id]; n->palette = s->palette; if (n->palette > 0) g_ddp[n->palette].refs++; e_memcpy(n->ck, s->ck, sizeof n->ck); e_memcpy(n->ck_set, s->ck_set, sizeof n->ck_set);
  int ver = (int)rd32(c, ARG(1) + 16); u64 obj = dds_iface(c, n, ver); wr32(c, obj + 12, 1); wrp(c, out, obj); RET(DD_OK);
}
static void dd_GetGDISurface(Cpu *c) { DdSurf *p = dds_primary(W); if (!ARG(1)) { RET(DDERR_INVALIDPARAMS); return; } if (!p) { wrp(c, ARG(1), 0); RET(DDERR_NOTFOUND); return; } int ver = (int)rd32(c, ARG(0) + 16); u64 obj = dds_iface(c, p, ver == 1 ? 1 : ver == 2 ? 3 : ver); wr32(c, obj + 12, rd32(c, obj + 12) + 1); wrp(c, ARG(1), obj); RET(DD_OK); }
static void dd_GetSurfaceFromDC(Cpu *c) { u64 hdc = ARG(1), out = ARG(2); if (!out) { RET(DDERR_INVALIDPARAMS); return; } for (int i = 1; i < DDS_MAX; i++) if (g_dds[i].used && g_dds[i].own == W && g_dds[i].dc == hdc && g_dds[i].dc_out) { u64 obj = dds_iface(c, &g_dds[i], 7); wr32(c, obj + 12, rd32(c, obj + 12) + 1); wrp(c, out, obj); RET(DD_OK); return; } wrp(c, out, 0); RET(DDERR_NOTFOUND); }
static void dd_FlipToGDISurface(Cpu *c) { wnd_invalidate_all(); W->dirty = 1; RET(DD_OK); }
static void dd_RestoreAllSurfaces(Cpu *c) { for (int i = 1; i < DDS_MAX; i++) if (g_dds[i].used && g_dds[i].own == W) g_dds[i].lost = 0; RET(DD_OK); }
/* EnumSurfaces: DDENUMSURFACES_ALL 1 / MATCH 2 / NOMATCH 4 / CANBECREATED 8 / DOESEXIST 0x10 */
static int dd_caps_match(Cpu *c, DdSurf *s, u64 desc) { if (!desc) return 1; u32 fl = rd32(c, desc + 4); if ((fl & 0x1) && (rd32(c, ddsd_caps(c, desc)) & ~s->caps)) return 0; if ((fl & 0x2) && rd32(c, desc + 8) != (u32)s->h) return 0; if ((fl & 0x4) && rd32(c, desc + 12) != (u32)s->w) return 0; if ((fl & 0x1000) && rd32(c, ddsd_pf(c, desc) + 12) != (u32)s->bpp) return 0; return 1; }
static int dd_enumsurf_step(Cpu *c, int ci) {   /* data: 0 desc buffer, 1 callback, 2 context, 3 next id, 4 v2, 5 filter desc, 6 flags, 7 iface version */
  Win32 *w = W; for (;;) { int id = (int)w->cont[ci].data[3]; if (id >= DDS_MAX) return 0; w->cont[ci].data[3] = (u64)(id + 1); DdSurf *s = &g_dds[id]; if (!s->used || s->own != w) continue;
    u32 flags = (u32)w->cont[ci].data[6]; int m = dd_caps_match(c, s, w->cont[ci].data[5]); if ((flags & 0x2) && !m) continue; if ((flags & 0x4) && m) continue;
    u64 p = w->cont[ci].data[0]; wr32(c, p, ddsd_size(c, (int)w->cont[ci].data[4])); dds_fill_desc(c, s, p, (int)w->cont[ci].data[4], 0);
    u64 obj = dds_iface(c, s, (int)w->cont[ci].data[7]); wr32(c, obj + 12, rd32(c, obj + 12) + 1);   /* the callback receives a reference it must Release */
    w32_guest_call(c, w->cont[ci].data[1], obj, p, w->cont[ci].data[2], 0); return 1; } }
static void dd_EnumSurfaces(Cpu *c) {   /* (this, flags, LPDDSURFACEDESC, context, callback) */
  u32 flags = (u32)ARG(1); u64 desc = ARG(2), ctx = ARG(3), cb = ARG(4); if (!cb) { RET(DDERR_INVALIDPARAMS); return; } int ver = (int)rd32(c, ARG(0) + 16), v2 = ver >= 4;
  if (flags & 0x8) { /* CANBECREATED: one callback with the description as given */ if (!desc) { RET(DDERR_INVALIDPARAMS); return; } w32_push_cont(c, CONT_DDENUMSURF, 0); W->cont[W->ncont - 1].data[3] = DDS_MAX; w32_guest_call(c, cb, 0, desc, ctx, 0); return; }
  u64 p = w32_heap_alloc(W, 144, 1); if (!p) { RET(DDERR_OUTOFMEMORY); return; }
  w32_push_cont(c, CONT_DDENUMSURF, p); int i = W->ncont - 1; W->cont[i].data[1] = cb; W->cont[i].data[2] = ctx; W->cont[i].data[3] = 1; W->cont[i].data[4] = (u64)v2; W->cont[i].data[5] = desc; W->cont[i].data[6] = flags; W->cont[i].data[7] = (u64)(ver == 1 ? 1 : ver == 2 ? 3 : ver);
  if (!dd_enumsurf_step(c, i)) { w32_heap_free(W, p); w32_cont_finish(c, DD_OK); }
}
static void dd_enumsurf_resume(Cpu *c, u64 rax) { Win32 *w = W; int i = w->ncont - 1; if ((u32)rax == 0 || !dd_enumsurf_step(c, i)) { if (w->cont[i].data[0]) w32_heap_free(w, w->cont[i].data[0]); w32_cont_finish(c, DD_OK); } }
/* ---- display mode switching: the framebuffer takes the mode's size (the host scales it like a monitor would); the cooperative-level window covers it ---- */
static void dd_mode_restore(Cpu *c) {
  if (!g_dd_mode_set) return; Win32 *w = W; g_dd_mode_set = 0; g_dd_mode_w = g_dd_mode_h = 0; g_dd_mode_owner = 0; g_scr_x0 = g_desk_w ? 0 : 64; g_scr_y0 = g_desk_w ? 0 : 64;
  if (w->screen) { if (g_desk_w) screen_ensure(w, g_desk_w, g_desk_h); else if (g_wnd[0].used && !g_wnd[0].destroyed) screen_ensure(w, wnd_client_w(0), wnd_client_h(0)); }
  wnd_invalidate_all(); w->dirty = 1; e_log("[ddraw] RestoreDisplayMode: desktop %dx%d\n", DESK_W, DESK_H);
}
static void dd_RestoreDisplayMode(Cpu *c) { dd_mode_restore(c); RET(DD_OK); }
static void dd_SetDisplayMode(Cpu *c) {   /* IDirectDraw: (this, w, h, bpp); IDirectDraw2+: (this, w, h, bpp, refresh, flags) */
  DdDev *d = dd_of(c, ARG(0)); if (!d) { RET(DDERR_INVALIDPARAMS); return; } int ver = (int)rd32(c, d->addr + 16); u32 mw = (u32)ARG(1), mh = (u32)ARG(2), bpp = (u32)ARG(3), rr = ver >= 2 ? (u32)ARG(4) : 0;
  if (!(d->coop & 0x10)) { RET(DDERR_NOEXCLUSIVEMODE); return; }
  if (mw < 64 || mh < 64 || mw > 4096 || mh > 4096 || (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) || (rr && rr != 60)) { RET(DDERR_INVALIDMODE); return; }
  if (dds_primary(W) && (int)bpp != dd_cur_bpp()) { e_log("[ddraw] SetDisplayMode %ux%u %u bpp while a %d-bpp primary exists: DDERR_INVALIDMODE (release the surfaces first, as on Windows)\n", mw, mh, bpp, dd_cur_bpp()); RET(DDERR_INVALIDMODE); return; }
  g_dd_mode_set = 1; g_dd_mode_w = (int)mw; g_dd_mode_h = (int)mh; g_dd_mode_bpp = (int)bpp; g_dd_mode_owner = W; g_dd_mode_hwnd = d->coop_hwnd; g_scr_x0 = 0; g_scr_y0 = 0;   /* primary coordinates = framebuffer coordinates */
  Win32 *w = W; screen_ensure(w, (int)mw, (int)mh);
  if (w->screen) for (size_t k = 0; k < (size_t)w->screen_w * w->screen_h; k++) w->screen[k] = 0; w->dirty = 1;
  DdSurf *p = dds_primary(W); if (p && (p->w != (int)mw || p->h != (int)mh)) { /* same depth, new size: the primary (and its chain) is re-sized in place */ for (int k = dds_id(p); k > 0; ) { DdSurf *s = &g_dds[k]; if (!s->client_mem) { mem_unmap(w->mem, s->bits); s->w = (int)mw; s->h = (int)mh; s->pitch = (int)(((u64)mw * s->bpp / 8 + 7) & ~7ull); u64 sz = (u64)s->pitch * s->h, addr = w->fb_cur; if (mem_map(w->mem, addr, sz, "ddraw")) { w->fb_cur += (sz + 0xffff) & ~0xffffull; s->bits = addr; } } dds_dc_free(w, s); k = s->natt ? s->att[0] : 0; } }
  e_log("[ddraw] SetDisplayMode %ux%u %u bpp (exclusive): framebuffer re-sized, window %llx covers it\n", mw, mh, bpp, (unsigned long long)d->coop_hwnd);
  int i = wnd_of(d->coop_hwnd); if (i >= 0 && wnd_alive(i) && g_wnd[i].parent < 0) { Wnd *n = &g_wnd[i]; n->style = (n->style & ~(0x00C00000u | 0x00040000u | 0x00400000u | 0x00800000u)) | 0x80000000u | 0x10000000u; n->ncx = 0; n->ncy = 0;
    if (wnd_set_pos(c, i, 0, 0, (int)mw, (int)mh, 0x4u | 0x10u | 0x20u | 0x40u, DD_OK)) return; }
  RET(DD_OK);
}
/* ---------------- IDirectDrawSurface ---------------- */
static int dds_ver_of_iid(u32 d1) { return d1 == 0x6C14DB81u ? 1 : d1 == 0x57805885u ? 2 : d1 == 0xDA044E00u ? 3 : d1 == 0x0B2B8630u ? 4 : d1 == 0x06675A80u ? 7 : d1 == 0 ? 0 : -1; }
static void dds_QueryInterface(Cpu *c) {
  DdSurf *s = dds_of(c, ARG(0)); u64 riid = ARG(1), out = ARG(2); if (!s || !riid || !out) { RET(DDERR_INVALIDPARAMS); return; } u32 d1 = rd32(c, riid); int ver = dds_ver_of_iid(d1);
  if (ver < 0) { wrp(c, out, 0); const char *nm = dd_iid_name(d1); if (nm || g_dd_log_d3d++ < 4) e_log("[ddraw] IDirectDrawSurface::QueryInterface %s{%08x-…}: E_NOINTERFACE%s\n", nm ? nm : "", d1, nm && nm[7] == '3' ? " (Direct3D through DirectDraw is not provided)" : ""); RET(E_NOINTERFACE_); return; }
  if (ver == 0) ver = (int)rd32(c, ARG(0) + 16); u64 obj = dds_iface(c, s, ver); if (!obj) { RET(DDERR_OUTOFMEMORY); return; } wr32(c, obj + 12, rd32(c, obj + 12) + 1); wrp(c, out, obj); RET(DD_OK);
}
static void dds_AddRef(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); if (!s) { RET(0); return; } u32 r = rd32(c, ARG(0) + 12) + 1; wr32(c, ARG(0) + 12, r); RET(r); }
static void dds_Release(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); if (!s) { RET(0); return; } u32 r = rd32(c, ARG(0) + 12); if (r) r--; wr32(c, ARG(0) + 12, r);
  if (!r && !dds_total_refs(c, s) && !(s->att_parent > 0 && g_dds[s->att_parent].used)) dds_free(c, s); RET(r); }
static void dds_GetSurfaceDesc(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); u64 p = ARG(1); if (!s || !p) { RET(DDERR_INVALIDPARAMS); return; } int ver = (int)rd32(c, ARG(0) + 16); u32 sz = rd32(c, p); if (sz != ddsd_size(c, ver >= 4)) { RET(DDERR_INVALIDPARAMS); return; } dds_fill_desc(c, s, p, ver >= 4, s->locked ? s->bits : 0); RET(DD_OK); }
static void dds_GetPixelFormat(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); u64 p = ARG(1); if (!s || !p || rd32(c, p) < 32) { RET(DDERR_INVALIDPARAMS); return; } dd_fill_pf(c, p, s->bpp, s->pf_flags, s->rmask, s->gmask, s->bmask, s->amask); RET(DD_OK); }
static void dds_GetCaps(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); u64 p = ARG(1); if (!s || !p) { RET(DDERR_INVALIDPARAMS); return; } int ver = (int)rd32(c, ARG(0) + 16); wr32(c, p, s->caps); if (ver >= 4) { wr32(c, p + 4, s->caps2); wr32(c, p + 8, 0); wr32(c, p + 12, 0); } RET(DD_OK); }
static void dds_IsLost(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); RET(s ? (s->lost ? DDERR_SURFACELOST : DD_OK) : DDERR_INVALIDPARAMS); }
static void dds_Restore(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); if (!s) { RET(DDERR_INVALIDPARAMS); return; } s->lost = 0; RET(DD_OK); }
static void dds_Initialize(Cpu *c) { RET(DDERR_ALREADYINITIALIZED); }
static void dds_GetBltStatus(Cpu *c) { RET(DD_OK); }
static void dds_GetFlipStatus(Cpu *c) { RET(DD_OK); }
static void dds_PageLock(Cpu *c) { RET(DD_OK); }
static void dds_PageUnlock(Cpu *c) { RET(DD_OK); }
static void dds_AddOverlayDirtyRect(Cpu *c) { RET(DDERR_UNSUPPORTED); }
static void dds_EnumOverlayZOrders(Cpu *c) { RET(DD_OK); }
static void dds_GetOverlayPosition(Cpu *c) { RET(DDERR_NOTAOVERLAYSURFACE); }
static void dds_SetOverlayPosition(Cpu *c) { RET(DDERR_NOTAOVERLAYSURFACE); }
static void dds_UpdateOverlay(Cpu *c) { RET(DDERR_NOTAOVERLAYSURFACE); }
static void dds_UpdateOverlayDisplay(Cpu *c) { RET(DDERR_UNSUPPORTED); }
static void dds_UpdateOverlayZOrder(Cpu *c) { RET(DDERR_NOTAOVERLAYSURFACE); }
static void dds_BltBatch(Cpu *c) { RET(DDERR_UNSUPPORTED); }   /* never implemented by ddraw.dll either */
static void dds_GetDDInterface(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); u64 out = ARG(1); if (!s || !out) { RET(DDERR_INVALIDPARAMS); return; } DdDev *d = s->dev > 0 && g_ddobj[s->dev].used ? &g_ddobj[s->dev] : dd_dev_of_proc(W); if (!d) { RET(DDERR_INVALIDOBJECT); return; } wr32(c, d->addr + 12, rd32(c, d->addr + 12) + 1); wrp(c, out, d->addr); RET(DD_OK); }
static void dds_GetUniquenessValue(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); if (!s || !ARG(1)) { RET(DDERR_INVALIDPARAMS); return; } wr32(c, ARG(1), s->uniq); RET(DD_OK); }
static void dds_ChangeUniquenessValue(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); if (!s) { RET(DDERR_INVALIDPARAMS); return; } s->uniq++; RET(DD_OK); }
static void dds_SetPriority(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); if (!s) { RET(DDERR_INVALIDPARAMS); return; } if (!(s->caps2 & 0x10u)) { RET(DDERR_INVALIDOBJECT); return; } s->priority = (u32)ARG(1); RET(DD_OK); }
static void dds_GetPriority(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); if (!s || !ARG(1)) { RET(DDERR_INVALIDPARAMS); return; } if (!(s->caps2 & 0x10u)) { RET(DDERR_INVALIDOBJECT); return; } wr32(c, ARG(1), s->priority); RET(DD_OK); }
static void dds_SetLOD(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); if (!s) { RET(DDERR_INVALIDPARAMS); return; } if (!(s->caps2 & 0x10u)) { RET(DDERR_INVALIDOBJECT); return; } s->lod = (u32)ARG(1); RET(DD_OK); }
static void dds_GetLOD(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); if (!s || !ARG(1)) { RET(DDERR_INVALIDPARAMS); return; } if (!(s->caps2 & 0x10u)) { RET(DDERR_INVALIDOBJECT); return; } wr32(c, ARG(1), s->lod); RET(DD_OK); }
/* private data (IDirectDrawSurface4+): DDSPD_IUNKNOWNPOINTER 1 / DDSPD_VOLATILE 2 */
static int dds_priv_find(Cpu *c, DdSurf *s, u64 guid) { const u8 *g = mem_ptr(c->mem, guid, 16, 0); if (!g) return -1; for (int k = 0; k < s->npriv; k++) if (!e_memcmp(s->priv_guid[k], g, 16)) return k; return -1; }
static void dds_SetPrivateData(Cpu *c) { /* (this, guid, data, size, flags) */ DdSurf *s = dds_of(c, ARG(0)); const u8 *g = mem_ptr(c->mem, ARG(1), 16, 0); u32 size = (u32)ARG(3), fl = (u32)ARG(4); if (!s || !g || !ARG(2)) { RET(DDERR_INVALIDPARAMS); return; }
  int k = dds_priv_find(c, s, ARG(1)); if (k < 0) { if (s->npriv == DDS_PRIV) { RET(DDERR_OUTOFMEMORY); return; } k = s->npriv++; e_memcpy(s->priv_guid[k], g, 16); s->priv_data[k] = 0; } else if (s->priv_data[k] && !(s->priv_flags[k] & 1)) w32_heap_free(W, s->priv_data[k]);
  if (fl & 1) { s->priv_data[k] = rdp(c, ARG(2)); s->priv_size[k] = (u32)PSZ_OF(c); } else { u64 a = w32_heap_alloc(W, size ? size : 1, 1); if (!a) { RET(DDERR_OUTOFMEMORY); return; } u8 *d = mem_ptr(c->mem, a, size, 1); const u8 *src = mem_ptr(c->mem, ARG(2), size, 0); if (d && src) e_memcpy(d, src, size); s->priv_data[k] = a; s->priv_size[k] = size; }
  s->priv_flags[k] = fl; RET(DD_OK); }
static void dds_GetPrivateData(Cpu *c) { /* (this, guid, data, *size) */ DdSurf *s = dds_of(c, ARG(0)); if (!s || !ARG(3)) { RET(DDERR_INVALIDPARAMS); return; } int k = dds_priv_find(c, s, ARG(1)); if (k < 0) { RET(DDERR_NOTFOUND); return; }
  u32 cap = rd32(c, ARG(3)); wr32(c, ARG(3), s->priv_size[k]); if (!ARG(2) || cap < s->priv_size[k]) { RET(DDERR_MOREDATA); return; }
  if (s->priv_flags[k] & 1) wrp(c, ARG(2), s->priv_data[k]); else { u8 *d = mem_ptr(c->mem, ARG(2), s->priv_size[k], 1); const u8 *src = mem_ptr(c->mem, s->priv_data[k], s->priv_size[k], 0); if (d && src) e_memcpy(d, src, s->priv_size[k]); } RET(DD_OK); }
static void dds_FreePrivateData(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); if (!s) { RET(DDERR_INVALIDPARAMS); return; } int k = dds_priv_find(c, s, ARG(1)); if (k < 0) { RET(DDERR_NOTFOUND); return; } if (s->priv_data[k] && !(s->priv_flags[k] & 1)) w32_heap_free(W, s->priv_data[k]); s->npriv--; for (; k < s->npriv; k++) { e_memcpy(s->priv_guid[k], s->priv_guid[k + 1], 16); s->priv_data[k] = s->priv_data[k + 1]; s->priv_size[k] = s->priv_size[k + 1]; s->priv_flags[k] = s->priv_flags[k + 1]; } RET(DD_OK); }
/* ---- attachments ---- */
static void dds_AddAttachedSurface(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)), *a = dds_of(c, ARG(1)); if (!s || !a) { RET(DDERR_INVALIDPARAMS); return; } if (a == s) { RET(DDERR_CANNOTATTACHSURFACE); return; } for (int k = 0; k < s->natt; k++) if (s->att[k] == dds_id(a)) { RET(DDERR_SURFACEALREADYATTACHED); return; } if (s->natt == 8) { RET(DDERR_CANNOTATTACHSURFACE); return; }
  if (!(a->caps & (0x20000u | 0x4u | 0x400000u | 0x2u | 0x1000u))) { RET(DDERR_CANNOTATTACHSURFACE); return; } s->att[s->natt++] = dds_id(a); a->att_parent = dds_id(s); wr32(c, ARG(1) + 12, rd32(c, ARG(1) + 12) + 1); RET(DD_OK); }
static void dds_DeleteAttachedSurface(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); u64 obj = ARG(2); if (!s) { RET(DDERR_INVALIDPARAMS); return; }
  if (!obj) { for (int k = 0; k < s->natt; k++) { DdSurf *a = &g_dds[s->att[k]]; if (a->used && a->att_parent == dds_id(s)) { a->att_parent = 0; if (!dds_total_refs(c, a)) dds_free(c, a); } } s->natt = 0; RET(DD_OK); return; }
  DdSurf *a = dds_of(c, obj); if (!a) { RET(DDERR_INVALIDPARAMS); return; } for (int k = 0; k < s->natt; k++) if (s->att[k] == dds_id(a)) { s->att[k] = s->att[--s->natt]; a->att_parent = 0; u32 r = rd32(c, obj + 12); if (r) wr32(c, obj + 12, r - 1); if (!dds_total_refs(c, a)) dds_free(c, a); RET(DD_OK); return; } RET(DDERR_SURFACENOTATTACHED); }
static void dds_GetAttachedSurface(Cpu *c) { /* (this, LPDDSCAPS(2), LPDIRECTDRAWSURFACE *) */ DdSurf *s = dds_of(c, ARG(0)); u64 caps = ARG(1), out = ARG(2); if (!s || !caps || !out) { RET(DDERR_INVALIDPARAMS); return; } u32 want = rd32(c, caps); int ver = (int)rd32(c, ARG(0) + 16);
  for (int k = 0; k < s->natt; k++) { DdSurf *a = &g_dds[s->att[k]]; if (a->used && (a->caps & want) == want) { u64 obj = dds_iface(c, a, ver); wr32(c, obj + 12, rd32(c, obj + 12) + 1); wrp(c, out, obj); RET(DD_OK); return; } }
  wrp(c, out, 0); RET(DDERR_NOTFOUND); }
static int dd_enumatt_step(Cpu *c, int ci) {   /* data: 0 desc, 1 cb, 2 ctx, 3 next k, 4 surface id, 5 v2, 6 ver */
  Win32 *w = W; for (;;) { DdSurf *s = &g_dds[w->cont[ci].data[4]]; if (!s->used) return 0; int k = (int)w->cont[ci].data[3]; if (k >= s->natt) return 0; w->cont[ci].data[3] = (u64)(k + 1); DdSurf *a = &g_dds[s->att[k]]; if (!a->used) continue;
    u64 p = w->cont[ci].data[0]; wr32(c, p, ddsd_size(c, (int)w->cont[ci].data[5])); dds_fill_desc(c, a, p, (int)w->cont[ci].data[5], 0); u64 obj = dds_iface(c, a, (int)w->cont[ci].data[6]); wr32(c, obj + 12, rd32(c, obj + 12) + 1);
    w32_guest_call(c, w->cont[ci].data[1], obj, p, w->cont[ci].data[2], 0); return 1; } }
static void dds_EnumAttachedSurfaces(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); u64 ctx = ARG(1), cb = ARG(2); if (!s || !cb) { RET(DDERR_INVALIDPARAMS); return; } int ver = (int)rd32(c, ARG(0) + 16); u64 p = w32_heap_alloc(W, 144, 1); if (!p) { RET(DDERR_OUTOFMEMORY); return; }
  w32_push_cont(c, CONT_DDENUMATT, p); int i = W->ncont - 1; W->cont[i].data[1] = cb; W->cont[i].data[2] = ctx; W->cont[i].data[3] = 0; W->cont[i].data[4] = (u64)dds_id(s); W->cont[i].data[5] = (u64)(ver >= 4); W->cont[i].data[6] = (u64)ver;
  if (!dd_enumatt_step(c, i)) { w32_heap_free(W, p); w32_cont_finish(c, DD_OK); } }
static void dd_enumatt_resume(Cpu *c, u64 rax) { Win32 *w = W; int i = w->ncont - 1; if ((u32)rax == 0 || !dd_enumatt_step(c, i)) { w32_heap_free(w, w->cont[i].data[0]); w32_cont_finish(c, DD_OK); } }
/* ---- colour keys / palette / clipper ---- */
static int dd_ck_index(u32 fl) { return (fl & 0x2) ? 0 : (fl & 0x4) ? 1 : (fl & 0x8) ? 2 : (fl & 0x10) ? 3 : -1; }   /* 0 dest blt, 1 dest overlay, 2 src blt, 3 src overlay */
static void dds_SetColorKey(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); u32 fl = (u32)ARG(1); u64 ck = ARG(2); if (!s) { RET(DDERR_INVALIDPARAMS); return; } int k = dd_ck_index(fl); if (k < 0) { RET(DDERR_INVALIDPARAMS); return; }
  if (!ck) { s->ck_set[k] = 0; RET(DD_OK); return; } s->ck[k][0] = rd32(c, ck); s->ck[k][1] = (fl & 0x1) ? rd32(c, ck + 4) : rd32(c, ck); if (s->ck[k][1] < s->ck[k][0]) s->ck[k][1] = s->ck[k][0]; s->ck_set[k] = 1; RET(DD_OK); }
static void dds_GetColorKey(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); u32 fl = (u32)ARG(1); u64 ck = ARG(2); if (!s || !ck) { RET(DDERR_INVALIDPARAMS); return; } int k = dd_ck_index(fl); if (k < 0) { RET(DDERR_INVALIDPARAMS); return; } if (!s->ck_set[k]) { RET(DDERR_NOCOLORKEY); return; } wr32(c, ck, s->ck[k][0]); wr32(c, ck + 4, s->ck[k][1]); RET(DD_OK); }
static void dd_obj_unref(Cpu *c, u64 a) { u32 r = rd32(c, a + 12); if (r) wr32(c, a + 12, r - 1); }
static void dds_SetPalette(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); if (!s) { RET(DDERR_INVALIDPARAMS); return; } if (!s->pal8) { RET(DDERR_NOTPALETTIZED); return; }
  DdPal *p = ARG(1) ? ddp_of(c, ARG(1)) : 0; if (ARG(1) && !p) { RET(DDERR_INVALIDPARAMS); return; } int nid = p ? (int)(p - g_ddp) : 0; if (s->palette == nid) { RET(DD_OK); return; }
  if (s->palette > 0 && g_ddp[s->palette].used) { g_ddp[s->palette].refs--; dd_obj_unref(c, g_ddp[s->palette].ifaces[0]); }
  s->palette = nid; if (p) { p->refs++; wr32(c, ARG(1) + 12, rd32(c, ARG(1) + 12) + 1); if (s->primary) p->flags |= 0x10u; } if (s->primary) dd_present(s, 0); RET(DD_OK); }
static void dds_GetPalette(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); u64 out = ARG(1); if (!s || !out) { RET(DDERR_INVALIDPARAMS); return; } if (!(s->palette > 0 && g_ddp[s->palette].used)) { wrp(c, out, 0); RET(DDERR_NOPALETTEATTACHED); return; } u64 a = g_ddp[s->palette].ifaces[0]; wr32(c, a + 12, rd32(c, a + 12) + 1); wrp(c, out, a); RET(DD_OK); }
static void dds_SetClipper(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); if (!s) { RET(DDERR_INVALIDPARAMS); return; }
  DdClip *cl = ARG(1) ? ddc_of(c, ARG(1)) : 0; if (ARG(1) && !cl) { RET(DDERR_INVALIDPARAMS); return; } int nid = cl ? (int)(cl - g_ddc) : 0; if (!nid && !s->clipper) { RET(DDERR_NOCLIPPERATTACHED); return; } if (s->clipper == nid) { RET(DD_OK); return; }
  if (s->clipper > 0 && g_ddc[s->clipper].used) { g_ddc[s->clipper].refs--; dd_obj_unref(c, g_ddc[s->clipper].ifaces[0]); }
  s->clipper = nid; if (cl) { cl->refs++; wr32(c, ARG(1) + 12, rd32(c, ARG(1) + 12) + 1); } RET(DD_OK); }
static void dds_GetClipper(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); u64 out = ARG(1); if (!s || !out) { RET(DDERR_INVALIDPARAMS); return; } if (!(s->clipper > 0 && g_ddc[s->clipper].used)) { wrp(c, out, 0); RET(DDERR_NOCLIPPERATTACHED); return; } u64 a = g_ddc[s->clipper].ifaces[0]; wr32(c, a + 12, rd32(c, a + 12) + 1); wrp(c, out, a); RET(DD_OK); }
/* ---- Lock / Unlock ---- */
static int dd_read_rect(Cpu *c, u64 rc, int *r) { if (!rc) return 0; if (!mem_ptr(c->mem, rc, 16, 0)) return -1; r[0] = (s32)rd32(c, rc); r[1] = (s32)rd32(c, rc + 4); r[2] = (s32)rd32(c, rc + 8); r[3] = (s32)rd32(c, rc + 12); return 1; }
static void dds_Lock(Cpu *c) { /* (this, LPRECT, LPDDSURFACEDESC, flags, HANDLE) */ DdSurf *s = dds_of(c, ARG(0)); u64 rc = ARG(1), desc = ARG(2); u32 fl = (u32)ARG(3); if (!s || !desc) { RET(DDERR_INVALIDPARAMS); return; } int ver = (int)rd32(c, ARG(0) + 16);
  if (rd32(c, desc) != ddsd_size(c, ver >= 4)) { RET(DDERR_INVALIDPARAMS); return; } if (s->lost) { RET(DDERR_SURFACELOST); return; } if (s->locked) { RET((fl & 0x4000u) ? DDERR_WASSTILLDRAWING : DDERR_SURFACEBUSY); return; }
  int r[4] = { 0, 0, s->w, s->h }; if (rc) { int k = dd_read_rect(c, rc, r); if (k < 0 || r[0] < 0 || r[1] < 0 || r[2] > s->w || r[3] > s->h || r[0] >= r[2] || r[1] >= r[3]) { RET(DDERR_INVALIDRECT); return; } }
  u64 ptr = s->bits + (u64)r[1] * s->pitch + (u64)r[0] * s->bpp / 8; dds_fill_desc(c, s, desc, ver >= 4, ptr); s->locked = 1; s->lock_rect[0] = r[0]; s->lock_rect[1] = r[1]; s->lock_rect[2] = r[2]; s->lock_rect[3] = r[3]; s->uniq++; RET(DD_OK); }
static void dds_Unlock(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); if (!s) { RET(DDERR_INVALIDPARAMS); return; } if (!s->locked) { RET(DDERR_NOTLOCKED); return; } s->locked = 0;
  if (s->primary) dd_present_rect(s, s->lock_rect[0], s->lock_rect[1], s->lock_rect[2], s->lock_rect[3]); RET(DD_OK); }
/* ---- Blt ---- */
static inline int dd_in_key(u32 v, const u32 *k) { return v >= k[0] && v <= k[1]; }
/* copy src rectangle → dest rectangle: nearest-neighbour stretch, mirror, source / destination colour keys, format conversion (same format: native copy, no rounding) */
static void dd_blit(DdSurf *d, int dx0, int dy0, int dx1, int dy1, DdSurf *s, int sx0, int sy0, int sx1, int sy1, const u32 *skey, const u32 *dkey, int mirror_lr, int mirror_ud, u32 rop) {
  u8 *dm = dds_mem(d); const u8 *sm = dds_mem(s); if (!dm || !sm) return; int dw = dx1 - dx0, dh = dy1 - dy0, sw = sx1 - sx0, sh = sy1 - sy0; if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
  int same = d->bpp == s->bpp && d->rmask == s->rmask && d->gmask == s->gmask && d->bmask == s->bmask && d->pal8 == s->pal8 && d->zbuf == s->zbuf;
  DdPal *sp = s->pal8 ? dds_pal_of(s) : 0, *dp = d->pal8 ? dds_pal_of(d) : 0; const u32 *spe = sp ? sp->pal : 0;
  int overlap = d == s && dy0 > sy0;   /* copying downwards inside one surface: walk rows backwards */
  if (same && !skey && !dkey && !mirror_lr && !mirror_ud && dw == sw && dh == sh && d->bpp >= 8 && rop == 0xCC0020u) { size_t nb = (size_t)dw * d->bpp / 8; for (int yy = 0; yy < dh; yy++) { int y = overlap ? dh - 1 - yy : yy; e_memmove(dm + (size_t)(dy0 + y) * d->pitch + (size_t)dx0 * d->bpp / 8, sm + (size_t)(sy0 + y) * s->pitch + (size_t)sx0 * s->bpp / 8, nb); } return; }
  for (int yy = 0; yy < dh; yy++) { int y = overlap ? dh - 1 - yy : yy; int syy = dh == sh ? y : (int)((s64)y * sh / dh); if (mirror_ud) syy = sh - 1 - syy; const u8 *srow = sm + (size_t)(sy0 + syy) * s->pitch; u8 *drow = dm + (size_t)(dy0 + y) * d->pitch;
    for (int x = 0; x < dw; x++) { int sxx = dw == sw ? x : (int)((s64)x * sw / dw); if (mirror_lr) sxx = sw - 1 - sxx; u32 v = dd_native_rd(s, srow, sx0 + sxx); if (skey && dd_in_key(v, skey)) continue;
      u32 o; if (same) o = v; else { u32 rgb = dd_to_rgb(s, spe, v); o = d->pal8 ? dd_pal_nearest(dp, rgb) : dd_from_rgb(d, rgb); }
      if (dkey || rop != 0xCC0020u) { u32 cur = dd_native_rd(d, drow, dx0 + x); if (dkey && !dd_in_key(cur, dkey)) continue; switch (rop) { case 0x660046u: o ^= cur; break; case 0x8800C6u: o &= cur; break; case 0xEE0086u: o |= cur; break; case 0x00550009u: o = ~cur; break; default: break; } }
      dd_native_wr(d, drow, dx0 + x, o); } }
}
static void dd_fill(DdSurf *d, int x0, int y0, int x1, int y1, u32 v, const u32 *dkey) { u8 *dm = dds_mem(d); if (!dm) return; for (int y = y0; y < y1; y++) { u8 *row = dm + (size_t)y * d->pitch; if (d->bpp == 32 && !dkey) { u32 *p = (u32 *)row; for (int x = x0; x < x1; x++) p[x] = v; } else if (d->bpp == 8 && !dkey) e_memset(row + x0, (int)(v & 255), (size_t)(x1 - x0)); else for (int x = x0; x < x1; x++) { if (dkey && !dd_in_key(dd_native_rd(d, row, x), dkey)) continue; dd_native_wr(d, row, x, v); } } }
static int dd_clip_dest(DdSurf *d, int *r) {   /* clip a destination rectangle to the surface and to the clipper (window client rect or the bounding box of the clip list) */
  if (r[0] < 0) r[0] = 0; if (r[1] < 0) r[1] = 0; if (r[2] > d->w) r[2] = d->w; if (r[3] > d->h) r[3] = d->h;
  if (d->clipper > 0 && g_ddc[d->clipper].used) { DdClip *cl = &g_ddc[d->clipper]; int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0; int have = 0;
    if (cl->hwnd) { have = ddc_window_rect(cl, &cx0, &cy0, &cx1, &cy1); if (have && !d->primary) { int i = wnd_of(cl->hwnd); if (i >= 0) { cx0 = 0; cy0 = 0; cx1 = wnd_client_w(i); cy1 = wnd_client_h(i); } } }
    else if (cl->nrects) { cx0 = cl->rects[0][0]; cy0 = cl->rects[0][1]; cx1 = cl->rects[0][2]; cy1 = cl->rects[0][3]; for (int k = 1; k < cl->nrects; k++) { if (cl->rects[k][0] < cx0) cx0 = cl->rects[k][0]; if (cl->rects[k][1] < cy0) cy0 = cl->rects[k][1]; if (cl->rects[k][2] > cx1) cx1 = cl->rects[k][2]; if (cl->rects[k][3] > cy1) cy1 = cl->rects[k][3]; } have = 1; }
    if (have) { if (cx0 > r[0]) r[0] = cx0; if (cy0 > r[1]) r[1] = cy0; if (cx1 < r[2]) r[2] = cx1; if (cy1 < r[3]) r[3] = cy1; } }
  return r[2] > r[0] && r[3] > r[1]; }
static u64 dd_bltfx_off(Cpu *c, int field) { /* 0 dwFillColor, 1 ddckDestColorkey, 2 ddckSrcColorkey */ return c->mode32 ? (field == 0 ? 80u : field == 1 ? 84u : 92u) : (field == 0 ? 104u : field == 1 ? 112u : 120u); }
static void dds_Blt(Cpu *c) { /* (this, LPRECT dst, src surface, LPRECT src, flags, LPDDBLTFX) */
  DdSurf *d = dds_of(c, ARG(0)); u64 drc = ARG(1), srcobj = ARG(2), srcrc = ARG(3), fx = ARG(5); u32 fl = (u32)ARG(4); if (!d) { RET(DDERR_INVALIDPARAMS); return; } if (d->lost) { RET(DDERR_SURFACELOST); return; } if (d->locked) { RET(DDERR_SURFACEBUSY); return; }
  int dr[4] = { 0, 0, d->w, d->h }; if (drc && dd_read_rect(c, drc, dr) < 0) { RET(DDERR_INVALIDPARAMS); return; } if (dr[0] > dr[2] || dr[1] > dr[3]) { RET(DDERR_INVALIDRECT); return; } int odr[4] = { dr[0], dr[1], dr[2], dr[3] };
  u32 dkey[2], skey[2]; const u32 *dk = 0, *sk = 0;
  if (fl & 0x4000u) { if (!fx) { RET(DDERR_INVALIDPARAMS); return; } dkey[0] = rd32(c, fx + dd_bltfx_off(c, 1)); dkey[1] = rd32(c, fx + dd_bltfx_off(c, 1) + 4); dk = dkey; } else if (fl & 0x2000u) { if (!d->ck_set[0]) { RET(DDERR_NOCOLORKEY); return; } dk = d->ck[0]; }
  if (fl & 0x400u) {   /* DDBLT_COLORFILL */
    if (!fx) { RET(DDERR_INVALIDPARAMS); return; } if (!dd_clip_dest(d, dr)) { RET(DD_OK); return; } dd_fill(d, dr[0], dr[1], dr[2], dr[3], rd32(c, fx + dd_bltfx_off(c, 0)), dk); d->uniq++; if (d->primary) dd_present_rect(d, dr[0], dr[1], dr[2], dr[3]); RET(DD_OK); return; }
  if (fl & 0x2000000u) {   /* DDBLT_DEPTHFILL */
    if (!fx) { RET(DDERR_INVALIDPARAMS); return; } if (!dd_clip_dest(d, dr)) { RET(DD_OK); return; } dd_fill(d, dr[0], dr[1], dr[2], dr[3], rd32(c, fx + dd_bltfx_off(c, 0)), 0); RET(DD_OK); return; }
  u32 rop = 0xCC0020u; if (fl & 0x20000u) { if (!fx) { RET(DDERR_INVALIDPARAMS); return; } rop = rd32(c, fx + 8);
    if (rop == 0x00000042u || rop == 0x00FF0062u || rop == 0x00550009u) { if (!dd_clip_dest(d, dr)) { RET(DD_OK); return; } if (rop == 0x00550009u) dd_blit(d, dr[0], dr[1], dr[2], dr[3], d, dr[0], dr[1], dr[2], dr[3], 0, dk, 0, 0, rop); else dd_fill(d, dr[0], dr[1], dr[2], dr[3], rop == 0x00000042u ? 0 : 0xffffffffu, dk); d->uniq++; if (d->primary) dd_present_rect(d, dr[0], dr[1], dr[2], dr[3]); RET(DD_OK); return; }
    if (rop != 0xCC0020u && rop != 0x660046u && rop != 0x8800C6u && rop != 0xEE0086u) { if (g_dd_log_rop++ < 3) e_log("[ddraw] Blt with ROP %08x: only SRCCOPY / SRCAND / SRCPAINT / SRCINVERT / BLACKNESS / WHITENESS / DSTINVERT are provided → SRCCOPY\n", rop); rop = 0xCC0020u; } }
  DdSurf *s = srcobj ? dds_of(c, srcobj) : 0; if (!s) { RET(DDERR_INVALIDPARAMS); return; } if (s->lost) { RET(DDERR_SURFACELOST); return; }
  int sr[4] = { 0, 0, s->w, s->h }; if (srcrc && dd_read_rect(c, srcrc, sr) < 0) { RET(DDERR_INVALIDPARAMS); return; } if (sr[0] < 0 || sr[1] < 0 || sr[2] > s->w || sr[3] > s->h || sr[0] >= sr[2] || sr[1] >= sr[3]) { RET(DDERR_INVALIDRECT); return; }
  if (fl & 0x10000u) { if (!fx) { RET(DDERR_INVALIDPARAMS); return; } skey[0] = rd32(c, fx + dd_bltfx_off(c, 2)); skey[1] = rd32(c, fx + dd_bltfx_off(c, 2) + 4); sk = skey; } else if (fl & 0x8000u) { if (!s->ck_set[2]) { RET(DDERR_NOCOLORKEY); return; } sk = s->ck[2]; }
  int mlr = 0, mud = 0; if ((fl & 0x800u) && fx) { u32 ddfx = rd32(c, fx + 4); mlr = (ddfx & 0x2u) != 0; mud = (ddfx & 0x4u) != 0;   /* DDBLTFX_MIRRORLEFTRIGHT / MIRRORUPDOWN */ }
  if (!dd_clip_dest(d, dr)) { RET(DD_OK); return; }
  if (dr[0] != odr[0] || dr[1] != odr[1] || dr[2] != odr[2] || dr[3] != odr[3]) {   /* destination was clipped: take the matching part of the source (keeps the stretch factor) */
    int ow = odr[2] - odr[0], oh = odr[3] - odr[1], sw = sr[2] - sr[0], sh = sr[3] - sr[1]; if (ow > 0 && oh > 0) { int nx0 = sr[0] + (int)((s64)(dr[0] - odr[0]) * sw / ow), nx1 = sr[0] + (int)((s64)(dr[2] - odr[0]) * sw / ow), ny0 = sr[1] + (int)((s64)(dr[1] - odr[1]) * sh / oh), ny1 = sr[1] + (int)((s64)(dr[3] - odr[1]) * sh / oh);
      if (mlr) { int t = sr[2] - (nx0 - sr[0]); nx0 = sr[2] - (nx1 - sr[0]); nx1 = t; } if (mud) { int t = sr[3] - (ny0 - sr[1]); ny0 = sr[3] - (ny1 - sr[1]); ny1 = t; } sr[0] = nx0; sr[1] = ny0; sr[2] = nx1 > nx0 ? nx1 : nx0 + 1; sr[3] = ny1 > ny0 ? ny1 : ny0 + 1; if (sr[2] > s->w) sr[2] = s->w; if (sr[3] > s->h) sr[3] = s->h; } }
  dd_blit(d, dr[0], dr[1], dr[2], dr[3], s, sr[0], sr[1], sr[2], sr[3], sk, dk, mlr, mud, rop); d->uniq++; if (d->primary) dd_present_rect(d, dr[0], dr[1], dr[2], dr[3]); RET(DD_OK);
}
static void dds_BltFast(Cpu *c) { /* (this, x, y, src, LPRECT src, trans) */ DdSurf *d = dds_of(c, ARG(0)), *s = dds_of(c, ARG(3)); int x = (int)(s32)ARG(1), y = (int)(s32)ARG(2); u32 tr = (u32)ARG(5); if (!d || !s) { RET(DDERR_INVALIDPARAMS); return; } if (d->lost || s->lost) { RET(DDERR_SURFACELOST); return; } if (d->locked) { RET(DDERR_SURFACEBUSY); return; }
  if (d->clipper > 0 && g_ddc[d->clipper].used) { RET(DDERR_BLTFASTCANTCLIP); return; }
  int sr[4] = { 0, 0, s->w, s->h }; if (ARG(4) && dd_read_rect(c, ARG(4), sr) < 0) { RET(DDERR_INVALIDPARAMS); return; } if (sr[0] < 0 || sr[1] < 0 || sr[2] > s->w || sr[3] > s->h || sr[0] >= sr[2] || sr[1] >= sr[3]) { RET(DDERR_INVALIDRECT); return; }
  int sw = sr[2] - sr[0], sh = sr[3] - sr[1]; if (x < 0 || y < 0 || x + sw > d->w || y + sh > d->h) { RET(DDERR_INVALIDRECT); return; }
  const u32 *sk = 0, *dk = 0; if (tr & 1) { if (!s->ck_set[2]) { RET(DDERR_NOCOLORKEY); return; } sk = s->ck[2]; } if (tr & 2) { if (!d->ck_set[0]) { RET(DDERR_NOCOLORKEY); return; } dk = d->ck[0]; }
  dd_blit(d, x, y, x + sw, y + sh, s, sr[0], sr[1], sr[2], sr[3], sk, dk, 0, 0, 0xCC0020u); d->uniq++; if (d->primary) dd_present_rect(d, x, y, x + sw, y + sh); RET(DD_OK); }
/* ---- Flip: rotate the pixel memory through the chain (what the hardware does), then show the new front buffer; paced to the 60 Hz monitor unless DDFLIP_NOVSYNC ---- */
static void dd_swap_bits(DdSurf *a, DdSurf *b) { u64 t = a->bits; a->bits = b->bits; b->bits = t; int cm = a->client_mem; a->client_mem = b->client_mem; b->client_mem = cm; a->uniq++; b->uniq++; }
static void dds_Flip(Cpu *c) { /* (this, override surface, flags) */ DdSurf *s = dds_of(c, ARG(0)); u32 fl = (u32)ARG(2); if (!s) { RET(DDERR_INVALIDPARAMS); return; } if (s->lost) { RET(DDERR_SURFACELOST); return; }
  if (!(s->caps & 0x10u) || !s->natt || s->att_parent > 0) { RET(DDERR_NOTFLIPPABLE); return; } if (s->locked) { RET(DDERR_SURFACEBUSY); return; }
  Win32 *w = W; double now = host_now_ms();
  if (w->sleeping) { if (now >= w->sleep_until_ms) { w->sleeping = 0; RET(DD_OK); return; } w->block = BLOCK_SLEEP; w32_retry_yield(c); return; }   /* second entry after the pacing wait */
  if (ARG(1)) { DdSurf *o = dds_of(c, ARG(1)); if (!o) { RET(DDERR_INVALIDPARAMS); return; } int ok = 0; for (int k = dds_id(s); k > 0 && g_dds[k].natt; k = g_dds[k].att[0]) if (g_dds[k].att[0] == dds_id(o)) ok = 1; if (!ok) { RET(DDERR_NOTFLIPPABLE); return; } dd_swap_bits(s, o); }
  else { /* front ← back1 ← back2 ← … ← old front */ int chain[8]; int n = 0; for (int k = dds_id(s); k > 0 && n < 8; k = g_dds[k].natt ? g_dds[k].att[0] : 0) { chain[n++] = k; if (!g_dds[k].natt || !(g_dds[g_dds[k].att[0]].caps & 0x4u)) break; } u64 first = g_dds[chain[0]].bits; int fcm = g_dds[chain[0]].client_mem; for (int k = 0; k + 1 < n; k++) { g_dds[chain[k]].bits = g_dds[chain[k + 1]].bits; g_dds[chain[k]].client_mem = g_dds[chain[k + 1]].client_mem; g_dds[chain[k]].uniq++; } g_dds[chain[n - 1]].bits = first; g_dds[chain[n - 1]].client_mem = fcm; }
  for (int k = dds_id(s); k > 0; k = g_dds[k].natt ? g_dds[k].att[0] : 0) { dds_dc_free(w, &g_dds[k]); if (!g_dds[k].natt) break; }   /* GetDC bitmaps are bound to the old memory */
  if (s->primary) dd_present(s, 0);
  if (!(fl & 0x8u) && !(fl & 0x20u)) {   /* not DDFLIP_NOVSYNC / DDFLIP_DONOTWAIT: wait for the next vertical blank (DDFLIP_INTERVAL2/3/4: that many) */
    u32 iv = (fl >> 24) & 7; double period = (iv >= 2 && iv <= 4) ? 16.6667 * iv : 16.6667; double until = g_dd_last_flip_ms + period; if (until < now) until = now; g_dd_last_flip_ms = until;
    if (until > now + 0.5) { w->sleeping = 1; w->sleep_until_ms = until; w->block = BLOCK_SLEEP; w32_retry_yield(c); return; } }
  RET(DD_OK); }
/* ---- GetDC / ReleaseDC: a 32-bpp memory DC mirrors the surface while it is handed out ---- */
static void dds_GetDC(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); u64 out = ARG(1); if (!s || !out) { RET(DDERR_INVALIDPARAMS); return; } if (s->lost) { RET(DDERR_SURFACELOST); return; } if (s->dc_out) { RET(DDERR_DCALREADYCREATED); return; } if (s->locked) { RET(DDERR_SURFACEBUSY); return; } if (s->zbuf) { RET(DDERR_NODC); return; }
  Win32 *w = W; if (!s->dc_bitmap) { u64 bits; u64 h = make_bitmap(c, s->w, s->h, 0, &bits); if (!h) { RET(DDERR_NODC); return; } s->dc_bitmap = h; }
  if (!s->dc) { u64 h = make_dc(w); if (!h) { RET(DDERR_NODC); return; } s->dc = h; } GdiObj *dc = gdi_get(w, s->dc), *bm = gdi_get(w, s->dc_bitmap); if (!dc || !bm) { s->dc = s->dc_bitmap = 0; RET(DDERR_NODC); return; } dc->bitmap = s->dc_bitmap; dc->wnd = 0;
  u32 *px = (u32 *)mem_ptr(w->mem, bm->bits, (u64)s->w * s->h * 4, 1); const u8 *m = dds_mem(s); if (!px || !m) { RET(DDERR_NODC); return; } DdPal *pal = dds_pal_of(s); const u32 *pe = pal ? pal->pal : 0;
  for (int y = 0; y < s->h; y++) { const u8 *row = m + (size_t)y * s->pitch; u32 *d = px + (size_t)y * s->w; if (s->bpp == 32 && s->rmask == 0xff0000u && s->gmask == 0xff00u && s->bmask == 0xffu) e_memcpy(d, row, (size_t)s->w * 4); else for (int x = 0; x < s->w; x++) d[x] = dd_to_rgb(s, pe, dd_native_rd(s, row, x)); }
  s->dc_out = 1; wrp(c, out, s->dc); RET(DD_OK); }
static void dds_ReleaseDC(Cpu *c) { DdSurf *s = dds_of(c, ARG(0)); if (!s) { RET(DDERR_INVALIDPARAMS); return; } if (!s->dc_out || ARG(1) != s->dc) { RET(DDERR_NODC); return; } Win32 *w = W; GdiObj *bm = gdi_get(w, s->dc_bitmap);
  const u32 *px = bm ? (const u32 *)mem_ptr(w->mem, bm->bits, (u64)s->w * s->h * 4, 0) : 0; u8 *m = dds_mem(s); if (px && m) { DdPal *pal = dds_pal_of(s);
    for (int y = 0; y < s->h; y++) { u8 *row = m + (size_t)y * s->pitch; const u32 *sp = px + (size_t)y * s->w; if (s->bpp == 32 && s->rmask == 0xff0000u && s->gmask == 0xff00u && s->bmask == 0xffu) { for (int x = 0; x < s->w; x++) ((u32 *)row)[x] = (((u32 *)row)[x] & 0xff000000u) | (sp[x] & 0xffffff); } else for (int x = 0; x < s->w; x++) { u32 rgb = sp[x] & 0xffffff; dd_native_wr(s, row, x, s->pal8 ? dd_pal_nearest(pal, rgb) : dd_from_rgb(s, rgb)); } } }
  s->dc_out = 0; s->uniq++; if (s->primary) dd_present(s, 0); RET(DD_OK); }
static void dds_SetSurfaceDesc(Cpu *c) { /* (this, LPDDSURFACEDESC(2), flags): client memory, pitch, size, pixel format of a system-memory surface */ DdSurf *s = dds_of(c, ARG(0)); u64 desc = ARG(1); if (!s || !desc) { RET(DDERR_INVALIDPARAMS); return; } int ver = (int)rd32(c, ARG(0) + 16);
  if (rd32(c, desc) != ddsd_size(c, ver >= 4)) { RET(DDERR_INVALIDPARAMS); return; } u32 fl = rd32(c, desc + 4); if (s->primary || s->locked || s->dc_out) { RET(DDERR_INVALIDSURFACETYPE); return; }
  if (!(fl & 0x800u)) { RET(DDERR_INVALIDPARAMS); return; } u64 ptr = rdp(c, desc + 36 + ddsd_p(c)); if (!ptr) { RET(DDERR_INVALIDPARAMS); return; }
  int nw = (fl & 0x4) ? (int)rd32(c, desc + 12) : s->w, nh = (fl & 0x2) ? (int)rd32(c, desc + 8) : s->h; int bpp = s->bpp; u32 pff = s->pf_flags, r = s->rmask, g = s->gmask, b = s->bmask, a = s->amask;
  if (fl & 0x1000) { u64 pf = ddsd_pf(c, desc); u32 pfl = rd32(c, pf + 4); if (pfl & 0x4u) { RET(DDERR_INVALIDPIXELFORMAT); return; } bpp = (int)rd32(c, pf + 12); pff = pfl; r = rd32(c, pf + 16); g = rd32(c, pf + 20); b = rd32(c, pf + 24); a = rd32(c, pf + 28); if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) { RET(DDERR_INVALIDPIXELFORMAT); return; } }
  int pitch = (fl & 0x8) ? (int)rd32(c, desc + 16) : (int)(((u64)nw * bpp / 8 + 7) & ~7ull); if (nw <= 0 || nh <= 0 || pitch < nw * bpp / 8 || !mem_ptr(c->mem, ptr, (u64)pitch * nh, 1)) { RET(DDERR_INVALIDPARAMS); return; }
  if (!s->client_mem && s->bits) mem_unmap(W->mem, s->bits); dds_dc_free(W, s); s->bits = ptr; s->client_mem = 1; s->w = nw; s->h = nh; s->pitch = pitch; dds_set_format(s, bpp, pff, r, g, b, a); s->uniq++; RET(DD_OK); }
/* ---------------- IDirectDrawPalette ---------------- */
static void ddp_QueryInterface(Cpu *c) { DdPal *p = ddp_of(c, ARG(0)); u64 riid = ARG(1), out = ARG(2); if (!p || !riid || !out) { RET(DDERR_INVALIDPARAMS); return; } u32 d1 = rd32(c, riid); if (d1 != 0 && d1 != 0x6C14DB84u) { wrp(c, out, 0); RET(E_NOINTERFACE_); return; } wr32(c, ARG(0) + 12, rd32(c, ARG(0) + 12) + 1); wrp(c, out, ARG(0)); RET(DD_OK); }
static void ddp_AddRef(Cpu *c) { DdPal *p = ddp_of(c, ARG(0)); if (!p) { RET(0); return; } u32 r = rd32(c, ARG(0) + 12) + 1; wr32(c, ARG(0) + 12, r); RET(r); }
static void ddp_Release(Cpu *c) { DdPal *p = ddp_of(c, ARG(0)); if (!p) { RET(0); return; } u32 r = rd32(c, ARG(0) + 12); if (r) r--; wr32(c, ARG(0) + 12, r); if (!r && p->refs <= 0) { w32_heap_free(W, p->ifaces[0]); p->used = 0; } RET(r); }
static void ddp_GetCaps(Cpu *c) { DdPal *p = ddp_of(c, ARG(0)); if (!p || !ARG(1)) { RET(DDERR_INVALIDPARAMS); return; } wr32(c, ARG(1), p->flags); RET(DD_OK); }
static void ddp_Initialize(Cpu *c) { RET(DDERR_ALREADYINITIALIZED); }
static void ddp_GetEntries(Cpu *c) { /* (this, flags, base, count, PALETTEENTRY *) */ DdPal *p = ddp_of(c, ARG(0)); u32 base = (u32)ARG(2), n = (u32)ARG(3); u64 out = ARG(4); if (!p || !out || base + n > (u32)p->ncol) { RET(DDERR_INVALIDPARAMS); return; }
  for (u32 i = 0; i < n; i++) { u32 e = p->pal[base + i]; wr32(c, out + i * 4, ((e >> 16) & 255) | (e & 0xff00) | ((e & 255) << 16) | (e & 0xff000000u)); } RET(DD_OK); }
static void ddp_SetEntries(Cpu *c) { DdPal *p = ddp_of(c, ARG(0)); u32 base = (u32)ARG(2), n = (u32)ARG(3); u64 ent = ARG(4); if (!p || !ent || base + n > (u32)p->ncol) { RET(DDERR_INVALIDPARAMS); return; }
  for (u32 i = 0; i < n; i++) { u32 e = rd32(c, ent + i * 4); p->pal[base + i] = ((e & 255) << 16) | (e & 0xff00) | ((e >> 16) & 255) | (e & 0xff000000u); } p->gen++; dd_present_all_palettised(W, p); RET(DD_OK); }
/* ---------------- IDirectDrawClipper ---------------- */
static void ddc_QueryInterface(Cpu *c) { DdClip *cl = ddc_of(c, ARG(0)); u64 riid = ARG(1), out = ARG(2); if (!cl || !riid || !out) { RET(DDERR_INVALIDPARAMS); return; } u32 d1 = rd32(c, riid); if (d1 != 0 && d1 != 0x6C14DB85u) { wrp(c, out, 0); RET(E_NOINTERFACE_); return; } wr32(c, ARG(0) + 12, rd32(c, ARG(0) + 12) + 1); wrp(c, out, ARG(0)); RET(DD_OK); }
static void ddc_AddRef(Cpu *c) { DdClip *cl = ddc_of(c, ARG(0)); if (!cl) { RET(0); return; } u32 r = rd32(c, ARG(0) + 12) + 1; wr32(c, ARG(0) + 12, r); RET(r); }
static void ddc_Release(Cpu *c) { DdClip *cl = ddc_of(c, ARG(0)); if (!cl) { RET(0); return; } u32 r = rd32(c, ARG(0) + 12); if (r) r--; wr32(c, ARG(0) + 12, r); if (!r && cl->refs <= 0) { w32_heap_free(W, cl->ifaces[0]); cl->used = 0; } RET(r); }
static void ddc_Initialize(Cpu *c) { RET(DDERR_ALREADYINITIALIZED); }
static void ddc_SetHWnd(Cpu *c) { DdClip *cl = ddc_of(c, ARG(0)); if (!cl) { RET(DDERR_INVALIDPARAMS); return; } if (ARG(2) && !dd_hwnd_alive(ARG(2))) { RET(DDERR_INVALIDPARAMS); return; } cl->hwnd = ARG(2); cl->nrects = 0; cl->changed = 1; RET(DD_OK); }
static void ddc_GetHWnd(Cpu *c) { DdClip *cl = ddc_of(c, ARG(0)); if (!cl || !ARG(1)) { RET(DDERR_INVALIDPARAMS); return; } wrp(c, ARG(1), cl->hwnd); RET(DD_OK); }
static void ddc_IsClipListChanged(Cpu *c) { DdClip *cl = ddc_of(c, ARG(0)); if (!cl || !ARG(1)) { RET(DDERR_INVALIDPARAMS); return; } wr32(c, ARG(1), cl->changed ? 1 : 0); cl->changed = 0; RET(DD_OK); }
static void ddc_SetClipList(Cpu *c) { /* (this, RGNDATA *, flags) */ DdClip *cl = ddc_of(c, ARG(0)); u64 rg = ARG(1); if (!cl) { RET(DDERR_INVALIDPARAMS); return; } if (cl->hwnd) { RET(DDERR_CLIPPERISUSINGHWND); return; }
  if (!rg) { cl->nrects = 0; cl->changed = 1; RET(DD_OK); return; } if (rd32(c, rg) != 32 || rd32(c, rg + 4) != 1) { RET(DDERR_INVALIDCLIPLIST); return; }
  u32 n = rd32(c, rg + 8); if (n > 16) n = 16; for (u32 k = 0; k < n; k++) for (int j = 0; j < 4; j++) cl->rects[k][j] = (s32)rd32(c, rg + 32 + k * 16 + j * 4); cl->nrects = (int)n; cl->changed = 1; RET(DD_OK); }
static void ddc_GetClipList(Cpu *c) { /* (this, LPRECT, RGNDATA *, *size) */ DdClip *cl = ddc_of(c, ARG(0)); u64 rc = ARG(1), rg = ARG(2), psz = ARG(3); if (!cl || !psz) { RET(DDERR_INVALIDPARAMS); return; }
  s32 R[16][4]; int n = 0; if (cl->hwnd) { int x0, y0, x1, y1; if (!ddc_window_rect(cl, &x0, &y0, &x1, &y1)) { RET(DDERR_INVALIDPARAMS); return; } R[0][0] = x0; R[0][1] = y0; R[0][2] = x1; R[0][3] = y1; n = 1; } else if (cl->nrects) { n = cl->nrects; e_memcpy(R, cl->rects, sizeof R); } else { RET(DDERR_NOCLIPLIST); return; }
  if (rc) { int r[4]; if (dd_read_rect(c, rc, r) > 0) { int m = 0; for (int k = 0; k < n; k++) { s32 x0 = R[k][0] > r[0] ? R[k][0] : r[0], y0 = R[k][1] > r[1] ? R[k][1] : r[1], x1 = R[k][2] < r[2] ? R[k][2] : r[2], y1 = R[k][3] < r[3] ? R[k][3] : r[3]; if (x1 > x0 && y1 > y0) { R[m][0] = x0; R[m][1] = y0; R[m][2] = x1; R[m][3] = y1; m++; } } n = m; } }
  u32 need = 32 + 16u * (u32)n; u32 cap = rd32(c, psz); wr32(c, psz, need); if (!rg) { RET(DD_OK); return; } if (cap < need) { RET(DDERR_REGIONTOOSMALL); return; }
  wr32(c, rg, 32); wr32(c, rg + 4, 1); wr32(c, rg + 8, (u32)n); wr32(c, rg + 12, 16u * (u32)n); s32 bx0 = n ? R[0][0] : 0, by0 = n ? R[0][1] : 0, bx1 = n ? R[0][2] : 0, by1 = n ? R[0][3] : 0;
  for (int k = 0; k < n; k++) { for (int j = 0; j < 4; j++) wr32(c, rg + 32 + (u64)k * 16 + (u64)j * 4, (u32)R[k][j]); if (R[k][0] < bx0) bx0 = R[k][0]; if (R[k][1] < by0) by0 = R[k][1]; if (R[k][2] > bx1) bx1 = R[k][2]; if (R[k][3] > by1) by1 = R[k][3]; }
  wr32(c, rg + 16, (u32)bx0); wr32(c, rg + 20, (u32)by0); wr32(c, rg + 24, (u32)bx1); wr32(c, rg + 28, (u32)by1); RET(DD_OK); }
/* ---------------- exports ---------------- */
static void s_DirectDrawCreate(Cpu *c) {   /* (GUID *driver, IDirectDraw **pp, IUnknown *outer) → IDirectDraw (v1); callers QueryInterface IDirectDraw2/4/7 */
  u64 out = ARG(1); if (!out) { RET(DDERR_INVALIDPARAMS); return; } if (ARG(2)) { RET(0x80040110u); return; }
  u64 a = dd_new(c, 1); if (!a) { RET(DDERR_OUTOFMEMORY); return; } wrp(c, out, a); e_log("[ddraw] DirectDrawCreate → display %dx%d %d bpp\n", dd_cur_w(), dd_cur_h(), dd_cur_bpp()); RET(DD_OK); }
static void s_DirectDrawCreateEx(Cpu *c) {   /* (GUID *driver, void **pp, REFIID iid, IUnknown *outer): iid must be IID_IDirectDraw7 */
  u64 out = ARG(1), riid = ARG(2); if (!out || !riid) { RET(DDERR_INVALIDPARAMS); return; } if (ARG(3)) { RET(0x80040110u); return; }
  if (rd32(c, riid) != 0x15E65EC0u) { wrp(c, out, 0); RET(DDERR_INVALIDPARAMS); return; }
  u64 a = dd_new(c, 7); if (!a) { RET(DDERR_OUTOFMEMORY); return; } wrp(c, out, a); e_log("[ddraw] DirectDrawCreateEx → IDirectDraw7, display %dx%d %d bpp\n", dd_cur_w(), dd_cur_h(), dd_cur_bpp()); RET(DD_OK); }
static u64 dd_guest_wstr(Win32 *w, const char *s) { size_t n = e_strlen(s); u64 a = w32_heap_alloc(w, n * 2 + 2, 1); for (size_t i = 0; i <= n; i++) { u8 *p = mem_ptr(w->mem, a + i * 2, 2, 1); if (p) { p[0] = (u8)s[i]; p[1] = 0; } } return a; }
/* DirectDrawEnumerate{A,W}(callback(GUID *, desc, name, ctx)) / Ex(callback(GUID *, desc, name, ctx, HMONITOR)): one display driver (NULL GUID) */
static void dd_enumerate(Cpu *c, int wide, int ex) {
  u64 cb = ARG(0), ctx = ARG(1); if (!cb) { RET(DDERR_INVALIDPARAMS); return; } const char *desc = "Primary Display Driver", *name = "display";
  u64 d = wide ? dd_guest_wstr(W, desc) : w32_guest_strdup(W, desc), n = wide ? dd_guest_wstr(W, name) : w32_guest_strdup(W, name);
  w32_push_cont(c, CONT_DDENUM, 0); W->cont[W->ncont - 1].data[3] = 0x7fffffff;   /* the resume sees the enumeration as finished */
  if (ex) w32_guest_call6(c, cb, 0, d, n, ctx, 0, 0); else w32_guest_call(c, cb, 0, d, n, ctx);
}
static void s_DirectDrawEnumerateA(Cpu *c) { dd_enumerate(c, 0, 0); }
static void s_DirectDrawEnumerateW(Cpu *c) { dd_enumerate(c, 1, 0); }
static void s_DirectDrawEnumerateExA(Cpu *c) { dd_enumerate(c, 0, 1); }
static void s_DirectDrawEnumerateExW(Cpu *c) { dd_enumerate(c, 1, 1); }
/* process exit: its surfaces / palettes / clippers / devices go away; an exclusive display mode is restored */
static void dd_proc_exit(Win32 *w) {
  for (int i = 1; i < DDS_MAX; i++) if (g_dds[i].used && g_dds[i].own == w) { DdSurf *s = &g_dds[i]; s->used = 0; dds_dc_free(w, s); if (!s->client_mem && s->bits) mem_unmap(w->mem, s->bits); s->bits = 0; }
  for (int i = 1; i < DDP_MAX; i++) if (g_ddp[i].used && g_ddp[i].own == w) g_ddp[i].used = 0;
  for (int i = 1; i < DDC_MAX; i++) if (g_ddc[i].used && g_ddc[i].own == w) g_ddc[i].used = 0;
  for (int i = 1; i < DD_MAX; i++) if (g_ddobj[i].used && g_ddobj[i].w == w) g_ddobj[i].used = 0;
  if (g_dd_mode_set && g_dd_mode_owner == w) { g_dd_mode_set = 0; g_dd_mode_w = g_dd_mode_h = 0; g_dd_mode_owner = 0; g_scr_x0 = g_desk_w ? 0 : 64; g_scr_y0 = g_desk_w ? 0 : 64; if (w->screen) { if (g_desk_w) screen_ensure(w, g_desk_w, g_desk_h); } }
}
static void ddraw_cont_dispatch(Cpu *c, int kind, u64 rax) { if (kind == CONT_DDENUM) dd_enum_resume(c, rax); else if (kind == CONT_DDENUMSURF) dd_enumsurf_resume(c, rax); else dd_enumatt_resume(c, rax); }
#define DDM(iface, m, fn) { "", iface "::" m, fn }
#define DD_IFACE(iface) \
  DDM(iface, "QueryInterface", dd_QueryInterface), DDM(iface, "AddRef", dd_AddRef), DDM(iface, "Release", dd_Release), DDM(iface, "Compact", dd_Compact), DDM(iface, "CreateClipper", dd_CreateClipper), \
  DDM(iface, "CreatePalette", dd_CreatePalette), DDM(iface, "CreateSurface", dd_CreateSurface), DDM(iface, "DuplicateSurface", dd_DuplicateSurface), DDM(iface, "EnumDisplayModes", dd_EnumDisplayModes), \
  DDM(iface, "EnumSurfaces", dd_EnumSurfaces), DDM(iface, "FlipToGDISurface", dd_FlipToGDISurface), DDM(iface, "GetCaps", dd_GetCaps), DDM(iface, "GetDisplayMode", dd_GetDisplayMode), \
  DDM(iface, "GetFourCCCodes", dd_GetFourCCCodes), DDM(iface, "GetGDISurface", dd_GetGDISurface), DDM(iface, "GetMonitorFrequency", dd_GetMonitorFrequency), DDM(iface, "GetScanLine", dd_GetScanLine), \
  DDM(iface, "GetVerticalBlankStatus", dd_GetVerticalBlankStatus), DDM(iface, "Initialize", dd_Initialize), DDM(iface, "RestoreDisplayMode", dd_RestoreDisplayMode), DDM(iface, "SetCooperativeLevel", dd_SetCooperativeLevel), \
  DDM(iface, "SetDisplayMode", dd_SetDisplayMode), DDM(iface, "WaitForVerticalBlank", dd_WaitForVerticalBlank), DDM(iface, "GetAvailableVidMem", dd_GetAvailableVidMem), DDM(iface, "GetSurfaceFromDC", dd_GetSurfaceFromDC), \
  DDM(iface, "RestoreAllSurfaces", dd_RestoreAllSurfaces), DDM(iface, "TestCooperativeLevel", dd_TestCooperativeLevel), DDM(iface, "GetDeviceIdentifier", dd_GetDeviceIdentifier), DDM(iface, "StartModeTest", dd_StartModeTest), \
  DDM(iface, "EvaluateMode", dd_EvaluateMode)
#define DDS_IFACE(iface) \
  DDM(iface, "QueryInterface", dds_QueryInterface), DDM(iface, "AddRef", dds_AddRef), DDM(iface, "Release", dds_Release), DDM(iface, "AddAttachedSurface", dds_AddAttachedSurface), DDM(iface, "AddOverlayDirtyRect", dds_AddOverlayDirtyRect), \
  DDM(iface, "Blt", dds_Blt), DDM(iface, "BltBatch", dds_BltBatch), DDM(iface, "BltFast", dds_BltFast), DDM(iface, "DeleteAttachedSurface", dds_DeleteAttachedSurface), DDM(iface, "EnumAttachedSurfaces", dds_EnumAttachedSurfaces), \
  DDM(iface, "EnumOverlayZOrders", dds_EnumOverlayZOrders), DDM(iface, "Flip", dds_Flip), DDM(iface, "GetAttachedSurface", dds_GetAttachedSurface), DDM(iface, "GetBltStatus", dds_GetBltStatus), DDM(iface, "GetCaps", dds_GetCaps), \
  DDM(iface, "GetClipper", dds_GetClipper), DDM(iface, "GetColorKey", dds_GetColorKey), DDM(iface, "GetDC", dds_GetDC), DDM(iface, "GetFlipStatus", dds_GetFlipStatus), DDM(iface, "GetOverlayPosition", dds_GetOverlayPosition), \
  DDM(iface, "GetPalette", dds_GetPalette), DDM(iface, "GetPixelFormat", dds_GetPixelFormat), DDM(iface, "GetSurfaceDesc", dds_GetSurfaceDesc), DDM(iface, "Initialize", dds_Initialize), DDM(iface, "IsLost", dds_IsLost), \
  DDM(iface, "Lock", dds_Lock), DDM(iface, "ReleaseDC", dds_ReleaseDC), DDM(iface, "Restore", dds_Restore), DDM(iface, "SetClipper", dds_SetClipper), DDM(iface, "SetColorKey", dds_SetColorKey), \
  DDM(iface, "SetOverlayPosition", dds_SetOverlayPosition), DDM(iface, "SetPalette", dds_SetPalette), DDM(iface, "Unlock", dds_Unlock), DDM(iface, "UpdateOverlay", dds_UpdateOverlay), DDM(iface, "UpdateOverlayDisplay", dds_UpdateOverlayDisplay), \
  DDM(iface, "UpdateOverlayZOrder", dds_UpdateOverlayZOrder), DDM(iface, "GetDDInterface", dds_GetDDInterface), DDM(iface, "PageLock", dds_PageLock), DDM(iface, "PageUnlock", dds_PageUnlock), DDM(iface, "SetSurfaceDesc", dds_SetSurfaceDesc), \
  DDM(iface, "SetPrivateData", dds_SetPrivateData), DDM(iface, "GetPrivateData", dds_GetPrivateData), DDM(iface, "FreePrivateData", dds_FreePrivateData), DDM(iface, "GetUniquenessValue", dds_GetUniquenessValue), DDM(iface, "ChangeUniquenessValue", dds_ChangeUniquenessValue), \
  DDM(iface, "SetPriority", dds_SetPriority), DDM(iface, "GetPriority", dds_GetPriority), DDM(iface, "SetLOD", dds_SetLOD), DDM(iface, "GetLOD", dds_GetLOD)
#define DDRAW_SHIMS \
  S(DirectDrawCreate), S(DirectDrawCreateEx), S(DirectDrawCreateClipper), S(DirectDrawEnumerateA), S(DirectDrawEnumerateW), S(DirectDrawEnumerateExA), S(DirectDrawEnumerateExW), \
  DD_IFACE("IDirectDraw"), DD_IFACE("IDirectDraw2"), DD_IFACE("IDirectDraw4"), DD_IFACE("IDirectDraw7"), \
  DDS_IFACE("IDirectDrawSurface"), DDS_IFACE("IDirectDrawSurface2"), DDS_IFACE("IDirectDrawSurface3"), DDS_IFACE("IDirectDrawSurface4"), DDS_IFACE("IDirectDrawSurface7"), \
  DDM("IDirectDrawPalette", "QueryInterface", ddp_QueryInterface), DDM("IDirectDrawPalette", "AddRef", ddp_AddRef), DDM("IDirectDrawPalette", "Release", ddp_Release), DDM("IDirectDrawPalette", "GetCaps", ddp_GetCaps), \
  DDM("IDirectDrawPalette", "GetEntries", ddp_GetEntries), DDM("IDirectDrawPalette", "Initialize", ddp_Initialize), DDM("IDirectDrawPalette", "SetEntries", ddp_SetEntries), \
  DDM("IDirectDrawClipper", "QueryInterface", ddc_QueryInterface), DDM("IDirectDrawClipper", "AddRef", ddc_AddRef), DDM("IDirectDrawClipper", "Release", ddc_Release), DDM("IDirectDrawClipper", "GetClipList", ddc_GetClipList), \
  DDM("IDirectDrawClipper", "GetHWnd", ddc_GetHWnd), DDM("IDirectDrawClipper", "Initialize", ddc_Initialize), DDM("IDirectDrawClipper", "IsClipListChanged", ddc_IsClipListChanged), DDM("IDirectDrawClipper", "SetClipList", ddc_SetClipList), \
  DDM("IDirectDrawClipper", "SetHWnd", ddc_SetHWnd)
