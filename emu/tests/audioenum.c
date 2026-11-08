#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <propsys.h>
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
  IMMDevice *dev=0; IPropertyStore *props=0;
  struct { PROPVARIANT pv; unsigned long guard; } value; ZeroMemory(&value,sizeof(value)); value.guard=0x24681357;
  const PROPERTYKEY friendly={{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},14};
  hr=IMMDeviceCollection_Item(coll,0,&dev); if (FAILED(hr)||!dev) { puts("audioenum: device FAIL"); return 6; }
  hr=IMMDevice_OpenPropertyStore(dev,0,&props); if (FAILED(hr)||!props) { puts("audioenum: props FAIL"); return 7; }
  hr=IPropertyStore_GetValue(props,&friendly,&value.pv);
  if (FAILED(hr)||value.pv.vt!=VT_LPWSTR||!value.pv.pwszVal||value.guard!=0x24681357) { puts("audioenum: value FAIL"); return 8; }
  PropVariantClear(&value.pv); IPropertyStore_Release(props); IMMDevice_Release(dev);
  IMMDeviceCollection_Release(coll); IMMDeviceEnumerator_Release(e); CoUninitialize(); puts("audioenum: PASS"); return 0;
}
