#define COBJMACROS
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
static const GUID cls = {0x25e609e4,0xb259,0x11cf,{0xbf,0xc7,0x44,0x45,0x53,0x54,0,0}};
static const GUID iid = {0xbf798030,0x483a,0x4dda,{0xaa,0x99,0x5d,0x64,0xed,0x36,0x97,0}};
int main(void) {
  IDirectInput8A *di=0; HRESULT hr=CoInitializeEx(NULL, COINIT_MULTITHREADED);
  if (FAILED(hr)) return 1;
  hr=CoCreateInstance(&cls,NULL,CLSCTX_INPROC_SERVER,&iid,(void **)&di);
  if (FAILED(hr)||!di) { printf("dinputcom: CREATE FAIL %lx\n",(unsigned long)hr); return 2; }
  hr=IDirectInput8_Initialize(di,GetModuleHandleA(NULL),DIRECTINPUT_VERSION);
  if (FAILED(hr)) { puts("dinputcom: INIT FAIL"); return 3; }
  IDirectInput8_Release(di); CoUninitialize(); puts("dinputcom: PASS"); return 0;
}
