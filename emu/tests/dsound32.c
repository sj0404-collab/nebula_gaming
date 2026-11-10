#define COBJMACROS
#include <windows.h>
#include <dsound.h>
#include <stdio.h>
#include <string.h>
int check_cooperative_stack(IDirectSound8 *ds);
typedef HRESULT (WINAPI *create_fn)(LPCGUID, LPDIRECTSOUND8 *, LPUNKNOWN);
int main(void) {
  HMODULE mod = LoadLibraryA("dsound.dll");
  create_fn create = mod ? (create_fn)GetProcAddress(mod, "DirectSoundCreate8") : NULL;
  struct { IDirectSound8 *p; DWORD guard; } out = {0,0x12345678};
  if (!create || FAILED(create(NULL,&out.p,NULL)) || !out.p) { puts("dsound32: create FAIL"); return 1; }
  if (check_cooperative_stack(out.p)) { puts("dsound32: stdcall stack FAIL"); return 2; }
  if (out.guard != 0x12345678) { puts("dsound32: create overwrote guard FAIL"); return 3; }
  WAVEFORMATEX fmt = { WAVE_FORMAT_PCM, 1, 22050, 22050, 1, 8, 0 };
  DSBUFFERDESC desc; memset(&desc,0,sizeof desc); desc.dwSize=sizeof desc; desc.dwBufferBytes=4096; desc.lpwfxFormat=&fmt;
  struct { IDirectSoundBuffer *p; DWORD guard; } buf = {0,0x13572468};
  if (FAILED(IDirectSound8_CreateSoundBuffer(out.p,&desc,&buf.p,NULL)) || !buf.p || buf.guard != 0x13572468) { puts("dsound32: buffer or guard FAIL"); return 4; }
  struct { void *p; DWORD guard; } p1 = {0,0x24681357}, p2 = {0,0x10293847}; DWORD n1=0,n2=0;
  if (FAILED(IDirectSoundBuffer_Lock(buf.p,0,0,&p1.p,&n1,&p2.p,&n2,DSBLOCK_ENTIREBUFFER)) || !p1.p || n1!=4096 || p1.guard!=0x24681357 || p2.guard!=0x10293847) { puts("dsound32: lock or guard FAIL"); return 5; }
  if (FAILED(IDirectSoundBuffer_Unlock(buf.p,p1.p,n1,p2.p,n2))) return 6;
  IDirectSoundBuffer_Release(buf.p); IDirectSound8_Release(out.p);
  puts("dsound32: PASS"); return 0;
}
