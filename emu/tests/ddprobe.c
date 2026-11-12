#define INITGUID
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
typedef HRESULT (WINAPI *PFN_DDCreateEx)(GUID *, LPVOID *, REFIID, IUnknown *);
typedef HRESULT (WINAPI *PFN_DDCreate)(GUID *, LPDIRECTDRAW *, IUnknown *);
typedef HRESULT (WINAPI *PFN_DDEnumA)(LPDDENUMCALLBACKA, LPVOID);
static int nmodes, modes32, bigw; static DWORD curw, curh;
static HRESULT WINAPI modecb(LPDDSURFACEDESC2 d, LPVOID ctx) { nmodes++; if (d->ddpfPixelFormat.dwRGBBitCount == 32) modes32++; if (d->dwWidth > bigw) bigw = d->dwWidth; (void)ctx; return DDENUMRET_OK; }
static int ndrv; static BOOL WINAPI drvcb(GUID *g, LPSTR desc, LPSTR name, LPVOID ctx) { ndrv++; printf("driver: %s (%s) guid=%p\n", desc, name, (void *)g); (void)ctx; return TRUE; }
int main(void) {
  HMODULE h = LoadLibraryA("ddraw.dll"); if (!h) { printf("no ddraw.dll\n"); return 1; }
  PFN_DDCreateEx createEx = (PFN_DDCreateEx)GetProcAddress(h, "DirectDrawCreateEx"); PFN_DDCreate create = (PFN_DDCreate)GetProcAddress(h, "DirectDrawCreate"); PFN_DDEnumA enumA = (PFN_DDEnumA)GetProcAddress(h, "DirectDrawEnumerateA");
  if (!createEx || !create || !enumA) { printf("missing exports\n"); return 1; }
  enumA(drvcb, NULL);
  LPDIRECTDRAW7 dd7 = NULL; HRESULT hr = createEx(NULL, (LPVOID *)&dd7, &IID_IDirectDraw7, NULL);
  if (FAILED(hr) || !dd7) { printf("DirectDrawCreateEx failed %08lx\n", hr); return 1; }
  hr = IDirectDraw7_SetCooperativeLevel(dd7, NULL, DDSCL_NORMAL); printf("SetCooperativeLevel %08lx\n", hr);
  DDCAPS caps; memset(&caps, 0, sizeof caps); caps.dwSize = sizeof caps; hr = IDirectDraw7_GetCaps(dd7, &caps, NULL);
  printf("GetCaps %08lx caps=%08lx vidmem total %lu MB free %lu MB\n", hr, caps.dwCaps, caps.dwVidMemTotal >> 20, caps.dwVidMemFree >> 20);
  DDSCAPS2 sc; memset(&sc, 0, sizeof sc); sc.dwCaps = DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM; DWORD total = 0, fre = 0; hr = IDirectDraw7_GetAvailableVidMem(dd7, &sc, &total, &fre);
  printf("GetAvailableVidMem %08lx total %lu MB free %lu MB\n", hr, total >> 20, fre >> 20);
  DDDEVICEIDENTIFIER2 id; memset(&id, 0, sizeof id); hr = IDirectDraw7_GetDeviceIdentifier(dd7, &id, 0);
  printf("GetDeviceIdentifier %08lx '%s' '%s' vendor %04lx device %04lx\n", hr, id.szDriver, id.szDescription, id.dwVendorId, id.dwDeviceId);
  DDSURFACEDESC2 d; memset(&d, 0, sizeof d); d.dwSize = sizeof d; hr = IDirectDraw7_GetDisplayMode(dd7, &d); curw = d.dwWidth; curh = d.dwHeight;
  printf("GetDisplayMode %08lx %lux%lu %lu bpp pitch %ld refresh %lu\n", hr, d.dwWidth, d.dwHeight, d.ddpfPixelFormat.dwRGBBitCount, d.lPitch, d.dwRefreshRate);
  hr = IDirectDraw7_EnumDisplayModes(dd7, 0, NULL, NULL, modecb); printf("EnumDisplayModes %08lx modes %d (32bpp %d) max width %d\n", hr, nmodes, modes32, bigw);
  DWORD freq = 0; IDirectDraw7_GetMonitorFrequency(dd7, &freq);
  LPDIRECTDRAW dd1 = NULL; hr = create(NULL, &dd1, NULL); LPDIRECTDRAW2 dd2 = NULL; LPDIRECTDRAW4 dd4 = NULL;
  HRESULT q2 = IDirectDraw_QueryInterface(dd1, &IID_IDirectDraw2, (void **)&dd2), q4 = IDirectDraw_QueryInterface(dd1, &IID_IDirectDraw4, (void **)&dd4);
  DWORD t2 = 0, f2 = 0; DDSCAPS sc1; sc1.dwCaps = DDSCAPS_VIDEOMEMORY; HRESULT a2 = IDirectDraw2_GetAvailableVidMem(dd2, &sc1, &t2, &f2);
  printf("DirectDrawCreate %08lx QI2 %08lx QI4 %08lx vidmem2 %lu MB; refs: dd4 release %lu dd2 release %lu dd1 release %lu\n", hr, q2, q4, t2 >> 20, IDirectDraw4_Release(dd4), IDirectDraw2_Release(dd2), IDirectDraw_Release(dd1));
  ULONG r7 = IDirectDraw7_Release(dd7);
  int ok = ndrv == 1 && caps.dwVidMemTotal >= (64u << 20) && total == caps.dwVidMemTotal && fre > 0 && id.szDescription[0] && d.ddpfPixelFormat.dwRGBBitCount == 32 && curw >= 640 && nmodes >= 4 && modes32 >= 2 && bigw <= (int)curw && freq == 60 && SUCCEEDED(q2) && SUCCEEDED(q4) && SUCCEEDED(a2) && r7 == 0;
  printf(ok ? "DDPROBE OK\n" : "DDPROBE MISMATCH\n"); return ok ? 0 : 1;
}
