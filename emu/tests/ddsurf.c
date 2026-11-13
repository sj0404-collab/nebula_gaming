/* ddsurf.c — DirectDraw surfaces end to end: Lock/Unlock pixel memory, Blt (colour fill, copy, stretch, mirror, colour keys,
 * format conversion 32→16→8), BltFast, palettes, clippers, GetDC/ReleaseDC (GDI onto a surface), attached back buffers +
 * Flip (memory rotation), exclusive SetDisplayMode/RestoreDisplayMode, IDirectDraw(1) + IDirectDrawSurface(1) path.
 * Prints DDSURF OK when every check holds.  Built for x86 and x86-64 (zig cc -target x86-windows-gnu / x86_64-windows-gnu, -ldxguid -lgdi32). */
#define INITGUID
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
typedef HRESULT (WINAPI *PFN_DDCreateEx)(GUID *, LPVOID *, REFIID, IUnknown *);
typedef HRESULT (WINAPI *PFN_DDCreate)(GUID *, LPDIRECTDRAW *, IUnknown *);
static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { if (m == WM_DESTROY) { PostQuitMessage(0); return 0; } return DefWindowProcA(h, m, w, l); }
static LPDIRECTDRAWSURFACE7 mk(LPDIRECTDRAW7 dd, DWORD w, DWORD h, DWORD caps, int bpp, DWORD r, DWORD g, DWORD b, int pal) {
  DDSURFACEDESC2 d; memset(&d, 0, sizeof d); d.dwSize = sizeof d; d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT; d.dwWidth = w; d.dwHeight = h; d.ddsCaps.dwCaps = caps;
  if (bpp) { d.dwFlags |= DDSD_PIXELFORMAT; d.ddpfPixelFormat.dwSize = sizeof d.ddpfPixelFormat; d.ddpfPixelFormat.dwRGBBitCount = bpp; d.ddpfPixelFormat.dwFlags = pal ? (DDPF_RGB | DDPF_PALETTEINDEXED8) : DDPF_RGB; d.ddpfPixelFormat.dwRBitMask = r; d.ddpfPixelFormat.dwGBitMask = g; d.ddpfPixelFormat.dwBBitMask = b; }
  LPDIRECTDRAWSURFACE7 s = NULL; HRESULT hr = IDirectDraw7_CreateSurface(dd, &d, &s, NULL); CHECK(hr == DD_OK && s, "CreateSurface %lux%lu %d bpp caps %08lx → %08lx", w, h, bpp, caps, hr); return s; }
static DWORD px(LPDIRECTDRAWSURFACE7 s, int x, int y) {   /* read one pixel through Lock */
  DDSURFACEDESC2 d; memset(&d, 0, sizeof d); d.dwSize = sizeof d; HRESULT hr = IDirectDrawSurface7_Lock(s, NULL, &d, DDLOCK_WAIT | DDLOCK_READONLY, NULL); if (hr != DD_OK) { CHECK(0, "Lock %08lx", hr); return 0xdeadbeef; }
  BYTE *row = (BYTE *)d.lpSurface + y * d.lPitch; DWORD v = 0; int bpp = d.ddpfPixelFormat.dwRGBBitCount;
  if (bpp == 32) v = ((DWORD *)row)[x]; else if (bpp == 16) v = ((WORD *)row)[x]; else if (bpp == 8) v = row[x]; else if (bpp == 24) v = row[x * 3] | (row[x * 3 + 1] << 8) | (row[x * 3 + 2] << 16);
  IDirectDrawSurface7_Unlock(s, NULL); return v; }
static void fillrect(LPDIRECTDRAWSURFACE7 s, int x0, int y0, int x1, int y1, DWORD col) { DDBLTFX fx; memset(&fx, 0, sizeof fx); fx.dwSize = sizeof fx; fx.dwFillColor = col; RECT r = { x0, y0, x1, y1 }; HRESULT hr = IDirectDrawSurface7_Blt(s, &r, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx); CHECK(hr == DD_OK, "colorfill %08lx", hr); }
int main(void) {
  HMODULE h = LoadLibraryA("ddraw.dll"); if (!h) { printf("no ddraw.dll\n"); return 1; }
  PFN_DDCreateEx createEx = (PFN_DDCreateEx)GetProcAddress(h, "DirectDrawCreateEx"); PFN_DDCreate create = (PFN_DDCreate)GetProcAddress(h, "DirectDrawCreate");
  WNDCLASSA wc; memset(&wc, 0, sizeof wc); wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "ddsurf"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassA(&wc);
  HWND hwnd = CreateWindowExA(0, "ddsurf", "ddsurf", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 80, 400, 300, NULL, NULL, wc.hInstance, NULL); CHECK(hwnd != NULL, "CreateWindow");
  LPDIRECTDRAW7 dd = NULL; HRESULT hr = createEx(NULL, (LPVOID *)&dd, &IID_IDirectDraw7, NULL); CHECK(hr == DD_OK && dd, "DirectDrawCreateEx %08lx", hr); if (!dd) return 1;
  hr = IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL); CHECK(hr == DD_OK, "SetCooperativeLevel NORMAL %08lx", hr);
  /* ---- primary + clipper (windowed) ---- */
  DDSURFACEDESC2 d; memset(&d, 0, sizeof d); d.dwSize = sizeof d; d.dwFlags = DDSD_CAPS; d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE; LPDIRECTDRAWSURFACE7 prim = NULL;
  hr = IDirectDraw7_CreateSurface(dd, &d, &prim, NULL); CHECK(hr == DD_OK && prim, "primary %08lx", hr); if (!prim) return 1;
  memset(&d, 0, sizeof d); d.dwSize = sizeof d; hr = IDirectDrawSurface7_GetSurfaceDesc(prim, &d); CHECK(hr == DD_OK && d.dwWidth >= 640 && d.ddpfPixelFormat.dwRGBBitCount == 32 && d.lPitch >= (LONG)d.dwWidth * 4 && (d.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE), "primary desc %lux%lu %lu bpp pitch %ld caps %08lx", d.dwWidth, d.dwHeight, d.ddpfPixelFormat.dwRGBBitCount, d.lPitch, d.ddsCaps.dwCaps);
  DWORD pw = d.dwWidth, ph = d.dwHeight; printf("primary %lux%lu %lu bpp pitch %ld\n", pw, ph, d.ddpfPixelFormat.dwRGBBitCount, d.lPitch);
  LPDIRECTDRAWCLIPPER clip = NULL; hr = IDirectDraw7_CreateClipper(dd, 0, &clip, NULL); CHECK(hr == DD_OK && clip, "CreateClipper %08lx", hr);
  hr = IDirectDrawClipper_SetHWnd(clip, 0, hwnd); CHECK(hr == DD_OK, "SetHWnd %08lx", hr); hr = IDirectDrawSurface7_SetClipper(prim, clip); CHECK(hr == DD_OK, "SetClipper %08lx", hr);
  DWORD need = 0; hr = IDirectDrawClipper_GetClipList(clip, NULL, NULL, &need); CHECK(hr == DD_OK && need == sizeof(RGNDATAHEADER) + sizeof(RECT), "GetClipList size %lu (%08lx)", need, hr);
  RGNDATA *rg = (RGNDATA *)malloc(need); hr = IDirectDrawClipper_GetClipList(clip, NULL, rg, &need); RECT *cr = (RECT *)rg->Buffer; RECT wcr; GetClientRect(hwnd, &wcr); POINT org = { 0, 0 }; ClientToScreen(hwnd, &org);
  CHECK(hr == DD_OK && rg->rdh.nCount == 1 && cr->left == org.x && cr->top == org.y && cr->right - cr->left == wcr.right && cr->bottom - cr->top == wcr.bottom, "clip list rect %ld,%ld-%ld,%ld vs client %ld,%ld %ldx%ld", cr->left, cr->top, cr->right, cr->bottom, org.x, org.y, wcr.right, wcr.bottom);
  printf("clipper: window client at %ld,%ld %ldx%ld on the primary\n", cr->left, cr->top, cr->right - cr->left, cr->bottom - cr->top);
  /* a windowed program draws into its client rectangle on the primary: fill it, pixels inside change, pixels outside the window do not */
  DWORD before_out = px(prim, pw - 3, ph - 3); fillrect(prim, 0, 0, pw, ph, 0x00ff8040);
  CHECK(px(prim, org.x + 5, org.y + 5) == 0x00ff8040, "primary inside window %08lx", px(prim, org.x + 5, org.y + 5)); CHECK(px(prim, pw - 3, ph - 3) == before_out, "primary outside the window changed (%08lx → %08lx)", before_out, px(prim, pw - 3, ph - 3));
  /* ---- offscreen 32 bpp: Lock writes, Blt copy / stretch / mirror / colour key ---- */
  LPDIRECTDRAWSURFACE7 a = mk(dd, 64, 64, DDSCAPS_OFFSCREENPLAIN, 0, 0, 0, 0, 0), b = mk(dd, 128, 128, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY, 0, 0, 0, 0, 0); if (!a || !b) return 1;
  memset(&d, 0, sizeof d); d.dwSize = sizeof d; hr = IDirectDrawSurface7_Lock(a, NULL, &d, DDLOCK_WAIT, NULL); CHECK(hr == DD_OK && d.lpSurface && d.lPitch >= 256 && (d.dwFlags & DDSD_LPSURFACE), "Lock a %08lx pitch %ld", hr, d.lPitch);
  for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) ((DWORD *)((BYTE *)d.lpSurface + y * d.lPitch))[x] = (x < 32) ? 0x00ff0000 : 0x000000ff;   /* left half red, right half blue */
  hr = IDirectDrawSurface7_Unlock(a, NULL); CHECK(hr == DD_OK, "Unlock %08lx", hr); hr = IDirectDrawSurface7_Unlock(a, NULL); CHECK(hr == DDERR_NOTLOCKED, "double Unlock %08lx", hr);
  DDSURFACEDESC2 d2; memset(&d2, 0, sizeof d2); d2.dwSize = sizeof d2; hr = IDirectDrawSurface7_Lock(a, NULL, &d, DDLOCK_WAIT, NULL); HRESULT hr2 = IDirectDrawSurface7_Lock(a, NULL, &d2, DDLOCK_WAIT, NULL); CHECK(hr == DD_OK && hr2 == DDERR_SURFACEBUSY, "nested Lock %08lx %08lx", hr, hr2); IDirectDrawSurface7_Unlock(a, NULL);
  fillrect(b, 0, 0, 128, 128, 0x00008000);
  RECT dst = { 10, 10, 74, 74 }; hr = IDirectDrawSurface7_Blt(b, &dst, a, NULL, DDBLT_WAIT, NULL); CHECK(hr == DD_OK, "Blt copy %08lx", hr);
  CHECK(px(b, 10, 10) == 0x00ff0000 && px(b, 73, 73) == 0x000000ff && px(b, 9, 9) == 0x00008000 && px(b, 74, 74) == 0x00008000, "copy pixels %08lx %08lx %08lx %08lx", px(b, 10, 10), px(b, 73, 73), px(b, 9, 9), px(b, 74, 74));
  RECT whole = { 0, 0, 128, 128 }; hr = IDirectDrawSurface7_Blt(b, &whole, a, NULL, DDBLT_WAIT, NULL); CHECK(hr == DD_OK && px(b, 0, 0) == 0x00ff0000 && px(b, 63, 127) == 0x00ff0000 && px(b, 64, 0) == 0x000000ff && px(b, 127, 127) == 0x000000ff, "stretch 2x %08lx %08lx %08lx", px(b, 0, 0), px(b, 63, 127), px(b, 64, 0));
  DDBLTFX fx; memset(&fx, 0, sizeof fx); fx.dwSize = sizeof fx; fx.dwDDFX = DDBLTFX_MIRRORLEFTRIGHT; hr = IDirectDrawSurface7_Blt(b, &whole, a, NULL, DDBLT_WAIT | DDBLT_DDFX, &fx); CHECK(hr == DD_OK && px(b, 0, 0) == 0x000000ff && px(b, 127, 0) == 0x00ff0000, "mirror %08lx %08lx", px(b, 0, 0), px(b, 127, 0));
  DDCOLORKEY ck; ck.dwColorSpaceLowValue = ck.dwColorSpaceHighValue = 0x00ff0000; hr = IDirectDrawSurface7_SetColorKey(a, DDCKEY_SRCBLT, &ck); CHECK(hr == DD_OK, "SetColorKey %08lx", hr);
  DDCOLORKEY gk; hr = IDirectDrawSurface7_GetColorKey(a, DDCKEY_SRCBLT, &gk); CHECK(hr == DD_OK && gk.dwColorSpaceLowValue == 0x00ff0000, "GetColorKey %08lx %08lx", hr, gk.dwColorSpaceLowValue);
  fillrect(b, 0, 0, 128, 128, 0x00123456); hr = IDirectDrawSurface7_BltFast(b, 20, 20, a, NULL, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT); CHECK(hr == DD_OK, "BltFast %08lx", hr);
  CHECK(px(b, 25, 25) == 0x00123456 && px(b, 60, 25) == 0x000000ff, "colour-keyed BltFast %08lx %08lx", px(b, 25, 25), px(b, 60, 25));
  RECT srcr = { 32, 0, 64, 64 }; hr = IDirectDrawSurface7_BltFast(b, 120, 0, a, &srcr, DDBLTFAST_WAIT); CHECK(hr == DDERR_INVALIDRECT, "BltFast off the edge %08lx", hr);
  /* partially off-surface Blt is clipped (not rejected) */
  RECT off = { 100, 100, 164, 164 }; hr = IDirectDrawSurface7_Blt(b, &off, a, NULL, DDBLT_WAIT, NULL); CHECK(hr == DD_OK && px(b, 127, 127) == 0x00ff0000, "clipped Blt %08lx %08lx", hr, px(b, 127, 127));
  /* ROP BLACKNESS, destination colour key */
  fx.dwROP = BLACKNESS; RECT blk = { 0, 0, 8, 8 }; hr = IDirectDrawSurface7_Blt(b, &blk, NULL, NULL, DDBLT_ROP | DDBLT_WAIT, &fx); CHECK(hr == DD_OK && px(b, 3, 3) == 0, "BLACKNESS %08lx %08lx", hr, px(b, 3, 3));
  /* ---- 16 bpp 565 conversion and 24 bpp ---- */
  LPDIRECTDRAWSURFACE7 s16 = mk(dd, 64, 64, DDSCAPS_OFFSCREENPLAIN, 16, 0xf800, 0x07e0, 0x001f, 0), s24 = mk(dd, 64, 64, DDSCAPS_OFFSCREENPLAIN, 24, 0xff0000, 0xff00, 0xff, 0); if (!s16 || !s24) return 1;
  DDPIXELFORMAT pf; memset(&pf, 0, sizeof pf); pf.dwSize = sizeof pf; hr = IDirectDrawSurface7_GetPixelFormat(s16, &pf); CHECK(hr == DD_OK && pf.dwRGBBitCount == 16 && pf.dwRBitMask == 0xf800, "GetPixelFormat 16 %08lx %lu %08lx", hr, pf.dwRGBBitCount, pf.dwRBitMask);
  hr = IDirectDrawSurface7_Blt(s16, NULL, a, NULL, DDBLT_WAIT, NULL); CHECK(hr == DD_OK && px(s16, 0, 0) == 0xf800 && px(s16, 63, 63) == 0x001f, "32→16 %08lx %04lx %04lx", hr, px(s16, 0, 0), px(s16, 63, 63));
  hr = IDirectDrawSurface7_Blt(s24, NULL, s16, NULL, DDBLT_WAIT, NULL); CHECK(hr == DD_OK && px(s24, 0, 0) == 0xff0000 && px(s24, 63, 63) == 0x0000ff, "16→24 %08lx %06lx %06lx", hr, px(s24, 0, 0), px(s24, 63, 63));
  hr = IDirectDrawSurface7_Blt(b, NULL, s24, NULL, DDBLT_WAIT, NULL); CHECK(hr == DD_OK && px(b, 0, 0) == 0x00ff0000 && px(b, 127, 127) == 0x000000ff, "24→32 stretched %08lx %08lx %08lx", hr, px(b, 0, 0), px(b, 127, 127));
  /* ---- 8 bpp palettised ---- */
  PALETTEENTRY pe[256]; memset(pe, 0, sizeof pe); pe[1].peRed = 255; pe[2].peGreen = 255; pe[3].peBlue = 255; pe[200].peRed = 10; pe[200].peGreen = 20; pe[200].peBlue = 30;
  LPDIRECTDRAWPALETTE pal = NULL; hr = IDirectDraw7_CreatePalette(dd, DDPCAPS_8BIT | DDPCAPS_ALLOW256, pe, &pal, NULL); CHECK(hr == DD_OK && pal, "CreatePalette %08lx", hr);
  LPDIRECTDRAWSURFACE7 s8 = mk(dd, 64, 64, DDSCAPS_OFFSCREENPLAIN, 8, 0, 0, 0, 1); if (!s8) return 1; hr = IDirectDrawSurface7_SetPalette(s8, pal); CHECK(hr == DD_OK, "SetPalette %08lx", hr);
  hr = IDirectDrawSurface7_SetPalette(a, pal); CHECK(hr == DDERR_NOTPALETTIZED, "SetPalette on 32 bpp %08lx", hr);
  memset(&d, 0, sizeof d); d.dwSize = sizeof d; hr = IDirectDrawSurface7_Lock(s8, NULL, &d, DDLOCK_WAIT, NULL); CHECK(hr == DD_OK && d.ddpfPixelFormat.dwRGBBitCount == 8 && (d.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8), "Lock 8 bpp %08lx", hr);
  for (int y = 0; y < 64; y++) memset((BYTE *)d.lpSurface + y * d.lPitch, y < 32 ? 1 : 200, 64); IDirectDrawSurface7_Unlock(s8, NULL);
  hr = IDirectDrawSurface7_Blt(a, NULL, s8, NULL, DDBLT_WAIT, NULL); CHECK(hr == DD_OK && px(a, 0, 0) == 0x00ff0000 && px(a, 0, 63) == 0x000a141e, "8→32 through the palette %08lx %08lx %08lx", hr, px(a, 0, 0), px(a, 0, 63));
  PALETTEENTRY np = { 0, 0, 255, 0 }; hr = IDirectDrawPalette_SetEntries(pal, 0, 1, 1, &np); CHECK(hr == DD_OK, "SetEntries %08lx", hr);
  hr = IDirectDrawSurface7_Blt(a, NULL, s8, NULL, DDBLT_WAIT, NULL); CHECK(hr == DD_OK && px(a, 0, 0) == 0x000000ff, "palette animation %08lx", px(a, 0, 0));
  PALETTEENTRY ge[2]; hr = IDirectDrawPalette_GetEntries(pal, 0, 1, 2, ge); CHECK(hr == DD_OK && ge[0].peBlue == 255 && ge[1].peGreen == 255, "GetEntries %08lx", hr);
  hr = IDirectDrawSurface7_Blt(s8, NULL, a, NULL, DDBLT_WAIT, NULL); CHECK(hr == DD_OK && px(s8, 0, 0) == 1 && px(s8, 0, 63) == 200, "32→8 nearest palette index %08lx %lu %lu", hr, px(s8, 0, 0), px(s8, 0, 63));
  LPDIRECTDRAWPALETTE gp = NULL; hr = IDirectDrawSurface7_GetPalette(s8, &gp); CHECK(hr == DD_OK && gp == pal, "GetPalette %08lx", hr); if (gp) IDirectDrawPalette_Release(gp);
  /* ---- GetDC: GDI draws onto the surface ---- */
  HDC sdc = NULL; hr = IDirectDrawSurface7_GetDC(b, &sdc); CHECK(hr == DD_OK && sdc, "GetDC %08lx", hr);
  if (sdc) { RECT rr = { 0, 0, 128, 64 }; HBRUSH br = CreateSolidBrush(RGB(0, 128, 255)); FillRect(sdc, &rr, br); DeleteObject(br); SetPixel(sdc, 100, 100, RGB(255, 255, 0)); COLORREF got = GetPixel(sdc, 10, 10); CHECK(got == RGB(0, 128, 255), "GetPixel on surface DC %06lx", got);
    HDC dc2; hr2 = IDirectDrawSurface7_GetDC(b, &dc2); CHECK(hr2 == DDERR_DCALREADYCREATED, "second GetDC %08lx", hr2); hr = IDirectDrawSurface7_ReleaseDC(b, sdc); CHECK(hr == DD_OK, "ReleaseDC %08lx", hr); }
  CHECK(px(b, 10, 10) == 0x000080ff && px(b, 100, 100) == 0x00ffff00, "GDI output landed in the surface %08lx %08lx", px(b, 10, 10), px(b, 100, 100));
  hr = IDirectDrawSurface7_GetDC(s8, &sdc); if (hr == DD_OK) { SetPixel(sdc, 1, 1, RGB(10, 20, 30)); IDirectDrawSurface7_ReleaseDC(s8, sdc); } CHECK(hr == DD_OK && px(s8, 1, 1) == 200, "GDI onto 8 bpp picks the nearest palette entry %08lx %lu", hr, px(s8, 1, 1));
  /* ---- uniqueness, private data, caps, refcounts, EnumSurfaces ---- */
  DWORD u1 = 0, u2 = 0; IDirectDrawSurface7_GetUniquenessValue(b, &u1); fillrect(b, 0, 0, 1, 1, 0); IDirectDrawSurface7_GetUniquenessValue(b, &u2); CHECK(u2 != u1, "uniqueness %lu %lu", u1, u2);
  GUID g = { 0x11223344, 1, 2, { 3, 4, 5, 6, 7, 8, 9, 10 } }; DWORD val = 0xabcdef01; hr = IDirectDrawSurface7_SetPrivateData(b, &g, &val, 4, 0); DWORD got = 0, gsz = 4; hr2 = IDirectDrawSurface7_GetPrivateData(b, &g, &got, &gsz); CHECK(hr == DD_OK && hr2 == DD_OK && got == val, "private data %08lx %08lx %08lx", hr, hr2, got);
  DDSCAPS2 gc; hr = IDirectDrawSurface7_GetCaps(b, &gc); CHECK(hr == DD_OK && (gc.dwCaps & DDSCAPS_SYSTEMMEMORY) && (gc.dwCaps & DDSCAPS_OFFSCREENPLAIN), "GetCaps %08lx", gc.dwCaps);
  LPDIRECTDRAWSURFACE4 b4 = NULL; hr = IDirectDrawSurface7_QueryInterface(b, &IID_IDirectDrawSurface4, (void **)&b4); CHECK(hr == DD_OK && b4, "QI IDirectDrawSurface4 %08lx", hr);
  if (b4) { DDSURFACEDESC2 d4; memset(&d4, 0, sizeof d4); d4.dwSize = sizeof d4; hr = IDirectDrawSurface4_GetSurfaceDesc(b4, &d4); CHECK(hr == DD_OK && d4.dwWidth == 128, "surface4 desc %08lx", hr); CHECK(IDirectDrawSurface4_Release(b4) == 0, "surface4 refcount (per-interface, as ddraw.dll)"); }
  LPDIRECTDRAW7 back = NULL; hr = IDirectDrawSurface7_GetDDInterface(b, (void **)&back); CHECK(hr == DD_OK && back != NULL, "GetDDInterface %08lx", hr); if (back) IDirectDraw7_Release(back);
  /* ---- exclusive mode: SetDisplayMode, flipping chain ---- */
  hr = IDirectDraw7_SetDisplayMode(dd, 640, 480, 32, 0, 0); CHECK(hr == DDERR_NOEXCLUSIVEMODE, "SetDisplayMode without exclusive %08lx", hr);
  IDirectDrawSurface7_Release(prim); prim = NULL;
  hr = IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN); CHECK(hr == DD_OK, "SetCooperativeLevel exclusive %08lx", hr);
  hr = IDirectDraw7_SetDisplayMode(dd, 640, 480, 32, 0, 0); CHECK(hr == DD_OK, "SetDisplayMode 640x480x32 %08lx", hr);
  memset(&d, 0, sizeof d); d.dwSize = sizeof d; hr = IDirectDraw7_GetDisplayMode(dd, &d); CHECK(hr == DD_OK && d.dwWidth == 640 && d.dwHeight == 480, "GetDisplayMode after switch %lux%lu", d.dwWidth, d.dwHeight);
  CHECK(GetSystemMetrics(SM_CXSCREEN) == 640 && GetSystemMetrics(SM_CYSCREEN) == 480, "GetSystemMetrics after switch %dx%d", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
  memset(&d, 0, sizeof d); d.dwSize = sizeof d; d.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT; d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX; d.dwBackBufferCount = 1;
  hr = IDirectDraw7_CreateSurface(dd, &d, &prim, NULL); CHECK(hr == DD_OK && prim, "flipping primary %08lx", hr); if (!prim) return 1;
  memset(&d, 0, sizeof d); d.dwSize = sizeof d; IDirectDrawSurface7_GetSurfaceDesc(prim, &d); CHECK(d.dwWidth == 640 && d.dwHeight == 480 && d.dwBackBufferCount == 1 && (d.dwFlags & DDSD_BACKBUFFERCOUNT), "flipping primary desc %lux%lu back %lu", d.dwWidth, d.dwHeight, d.dwBackBufferCount);
  DDSCAPS2 want; memset(&want, 0, sizeof want); want.dwCaps = DDSCAPS_BACKBUFFER; LPDIRECTDRAWSURFACE7 bb = NULL; hr = IDirectDrawSurface7_GetAttachedSurface(prim, &want, &bb); CHECK(hr == DD_OK && bb, "GetAttachedSurface back buffer %08lx", hr); if (!bb) return 1;
  fillrect(prim, 0, 0, 640, 480, 0x00111111); fillrect(bb, 0, 0, 640, 480, 0x00222222);
  memset(&d, 0, sizeof d); d.dwSize = sizeof d; IDirectDrawSurface7_Lock(bb, NULL, &d, DDLOCK_WAIT, NULL); void *bbmem = d.lpSurface; IDirectDrawSurface7_Unlock(bb, NULL);
  memset(&d, 0, sizeof d); d.dwSize = sizeof d; IDirectDrawSurface7_Lock(prim, NULL, &d, DDLOCK_WAIT, NULL); void *pmem = d.lpSurface; IDirectDrawSurface7_Unlock(prim, NULL);
  hr = IDirectDrawSurface7_Flip(bb, NULL, DDFLIP_WAIT); CHECK(hr == DDERR_NOTFLIPPABLE, "Flip on a back buffer %08lx", hr);
  DWORD t0 = GetTickCount(); for (int i = 0; i < 6; i++) { hr = IDirectDrawSurface7_Flip(prim, NULL, DDFLIP_WAIT); CHECK(hr == DD_OK, "Flip %d %08lx", i, hr); } DWORD dt = GetTickCount() - t0;
  CHECK(px(prim, 5, 5) == 0x00111111 && px(bb, 5, 5) == 0x00222222, "after 6 flips (even) %08lx %08lx", px(prim, 5, 5), px(bb, 5, 5));
  hr = IDirectDrawSurface7_Flip(prim, NULL, DDFLIP_WAIT); CHECK(hr == DD_OK && px(prim, 5, 5) == 0x00222222 && px(bb, 5, 5) == 0x00111111, "after 7 flips (odd) %08lx %08lx", px(prim, 5, 5), px(bb, 5, 5));
  memset(&d, 0, sizeof d); d.dwSize = sizeof d; IDirectDrawSurface7_Lock(prim, NULL, &d, DDLOCK_WAIT, NULL); CHECK(d.lpSurface == bbmem, "Flip rotates the memory (primary now at the old back buffer)"); IDirectDrawSurface7_Unlock(prim, NULL);
  memset(&d, 0, sizeof d); d.dwSize = sizeof d; IDirectDrawSurface7_Lock(bb, NULL, &d, DDLOCK_WAIT, NULL); CHECK(d.lpSurface == pmem, "back buffer now at the old primary memory"); IDirectDrawSurface7_Unlock(bb, NULL);
  printf("7 flips with DDFLIP_WAIT took %lu ms (vsync pacing, ~16.7 ms each)\n", dt); CHECK(dt >= 60, "Flip pacing %lu ms", dt);
  t0 = GetTickCount(); for (int i = 0; i < 10; i++) IDirectDrawSurface7_Flip(prim, NULL, DDFLIP_NOVSYNC); dt = GetTickCount() - t0; CHECK(dt < 100, "DDFLIP_NOVSYNC %lu ms", dt);
  /* GDI surface + Flip to it; cooperative-level tests; a z-buffer attached by hand */
  LPDIRECTDRAWSURFACE7 gdis = NULL; hr = IDirectDraw7_GetGDISurface(dd, &gdis); CHECK(hr == DD_OK && gdis == prim, "GetGDISurface %08lx", hr); if (gdis) IDirectDrawSurface7_Release(gdis);
  hr = IDirectDraw7_TestCooperativeLevel(dd); CHECK(hr == DD_OK, "TestCooperativeLevel %08lx", hr);
  LPDIRECTDRAWSURFACE7 z = NULL; memset(&d, 0, sizeof d); d.dwSize = sizeof d; d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT; d.dwWidth = 640; d.dwHeight = 480; d.ddsCaps.dwCaps = DDSCAPS_ZBUFFER; d.ddpfPixelFormat.dwSize = sizeof d.ddpfPixelFormat; d.ddpfPixelFormat.dwFlags = DDPF_ZBUFFER; d.ddpfPixelFormat.dwZBufferBitDepth = 16; d.ddpfPixelFormat.dwZBitMask = 0xffff;
  hr = IDirectDraw7_CreateSurface(dd, &d, &z, NULL); CHECK(hr == DD_OK && z, "z-buffer %08lx", hr);
  if (z) { hr = IDirectDrawSurface7_AddAttachedSurface(bb, z); CHECK(hr == DD_OK, "AddAttachedSurface z %08lx", hr); memset(&want, 0, sizeof want); want.dwCaps = DDSCAPS_ZBUFFER; LPDIRECTDRAWSURFACE7 gz = NULL; hr = IDirectDrawSurface7_GetAttachedSurface(bb, &want, &gz); CHECK(hr == DD_OK && gz == z, "GetAttachedSurface z %08lx", hr); if (gz) IDirectDrawSurface7_Release(gz);
    DDBLTFX zfx; memset(&zfx, 0, sizeof zfx); zfx.dwSize = sizeof zfx; zfx.dwFillDepth = 0xffff; hr = IDirectDrawSurface7_Blt(z, NULL, NULL, NULL, DDBLT_DEPTHFILL | DDBLT_WAIT, &zfx); CHECK(hr == DD_OK && px(z, 100, 100) == 0xffff, "DEPTHFILL %08lx %04lx", hr, px(z, 100, 100));
    hr = IDirectDrawSurface7_DeleteAttachedSurface(bb, 0, z); CHECK(hr == DD_OK, "DeleteAttachedSurface %08lx", hr); IDirectDrawSurface7_Release(z); }
  hr = IDirectDraw7_RestoreDisplayMode(dd); CHECK(hr == DD_OK, "RestoreDisplayMode %08lx", hr);
  memset(&d, 0, sizeof d); d.dwSize = sizeof d; IDirectDraw7_GetDisplayMode(dd, &d); CHECK(d.dwWidth == pw && d.dwHeight == ph, "display mode restored %lux%lu", d.dwWidth, d.dwHeight);
  IDirectDrawSurface7_Release(bb); ULONG pr = IDirectDrawSurface7_Release(prim); CHECK(pr == 0, "primary final release %lu", pr); prim = NULL;
  hr = IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL); CHECK(hr == DD_OK, "back to NORMAL %08lx", hr);
  /* ---- IDirectDraw (1) + IDirectDrawSurface (1): DDSURFACEDESC / DDSCAPS of the old sizes ---- */
  LPDIRECTDRAW dd1 = NULL; hr = create(NULL, &dd1, NULL); CHECK(hr == DD_OK && dd1, "DirectDrawCreate %08lx", hr);
  if (dd1) { IDirectDraw_SetCooperativeLevel(dd1, hwnd, DDSCL_NORMAL); DDSURFACEDESC d1; memset(&d1, 0, sizeof d1); d1.dwSize = sizeof d1; d1.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT; d1.dwWidth = 32; d1.dwHeight = 32; d1.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    LPDIRECTDRAWSURFACE s1 = NULL; hr = IDirectDraw_CreateSurface(dd1, &d1, &s1, NULL); CHECK(hr == DD_OK && s1, "v1 CreateSurface %08lx", hr);
    if (s1) { memset(&d1, 0, sizeof d1); d1.dwSize = sizeof d1; hr = IDirectDrawSurface_Lock(s1, NULL, &d1, DDLOCK_WAIT, NULL); CHECK(hr == DD_OK && d1.lpSurface && d1.ddpfPixelFormat.dwRGBBitCount == 32, "v1 Lock %08lx", hr);
      if (hr == DD_OK) { ((DWORD *)d1.lpSurface)[0] = 0x00654321; IDirectDrawSurface_Unlock(s1, NULL); } memset(&d1, 0, sizeof d1); d1.dwSize = sizeof d1; IDirectDrawSurface_Lock(s1, NULL, &d1, DDLOCK_WAIT, NULL); CHECK(((DWORD *)d1.lpSurface)[0] == 0x00654321, "v1 pixel persists"); IDirectDrawSurface_Unlock(s1, NULL);
      LPDIRECTDRAWSURFACE7 s1as7 = NULL; hr = IDirectDrawSurface_QueryInterface(s1, &IID_IDirectDrawSurface7, (void **)&s1as7); CHECK(hr == DD_OK && s1as7, "v1 → v7 QI %08lx", hr); if (s1as7) { CHECK(px(s1as7, 0, 0) == 0x00654321, "same surface through v7"); IDirectDrawSurface7_Release(s1as7); }
      DDSURFACEDESC bad; memset(&bad, 0, sizeof bad); bad.dwSize = sizeof(DDSURFACEDESC2); hr = IDirectDrawSurface_GetSurfaceDesc(s1, &bad); CHECK(hr == DDERR_INVALIDPARAMS, "v1 with v2 dwSize %08lx", hr);
      CHECK(IDirectDrawSurface_Release(s1) == 0, "v1 surface release"); }
    CHECK(IDirectDraw_Release(dd1) == 0, "dd1 release"); }
  /* ---- teardown ---- */
  IDirectDrawPalette_Release(pal); IDirectDrawClipper_Release(clip); IDirectDrawSurface7_Release(s8); IDirectDrawSurface7_Release(s16); IDirectDrawSurface7_Release(s24); CHECK(IDirectDrawSurface7_Release(a) == 0, "a release"); CHECK(IDirectDrawSurface7_Release(b) == 0, "b release");
  ULONG r = IDirectDraw7_Release(dd); CHECK(r == 0, "dd release %lu", r); free(rg); DestroyWindow(hwnd);
  printf(fails ? "DDSURF FAILED (%d)\n" : "DDSURF OK\n", fails); return fails ? 1 : 0;
}
