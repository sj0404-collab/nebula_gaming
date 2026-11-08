#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <stdio.h>
int main(void) {
  IMMDeviceEnumerator *e=0; IMMDeviceCollection *coll=0;
  struct { IMMDeviceCollection *p; unsigned long guard; } out={0,0x13572468};
  HRESULT hr=CoInitializeEx(NULL,COINIT_MULTITHREADED);
  if (FAILED(hr)) return 1;
  hr=CoCreateInstance(&CLSID_MMDeviceEnumerator,0,CLSCTX_INPROC_SERVER,&IID_IMMDeviceEnumerator,(void**)&e);
  if (FAILED(hr) || !e) { puts("audioenum: create FAIL"); return 2; }
  if (IMMDeviceEnumerator_AddRef(e)!=2 || IMMDeviceEnumerator_Release(e)!=1) { puts("audioenum: ref FAIL"); return 3; }
  hr=IMMDeviceEnumerator_EnumAudioEndpoints(e,eRender,DEVICE_STATE_ACTIVE,&out.p);
  if (FAILED(hr) || !out.p || out.guard!=0x13572468) { puts("audioenum: enum FAIL"); return 4; }
  coll=out.p; UINT n=0; hr=IMMDeviceCollection_GetCount(coll,&n);
  if (FAILED(hr) || n!=1) { puts("audioenum: count FAIL"); return 5; }
  IMMDeviceCollection_Release(coll); IMMDeviceEnumerator_Release(e); CoUninitialize(); puts("audioenum: PASS"); return 0;
}
