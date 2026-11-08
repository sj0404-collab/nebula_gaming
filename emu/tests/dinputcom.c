#define COBJMACROS
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <initguid.h>
#include <dinput.h>
#include <stdio.h>
/* Use SDK-defined GUIDs: hand-written copies can make implementation and test
   agree with each other while disagreeing with real programs. */
int main(void) {
  IDirectInput8A *di=0; IDirectInput8W *wide=0; void *bad=(void*)1;
  HRESULT hr=CoInitializeEx(NULL, COINIT_MULTITHREADED);
  if (FAILED(hr)) return 1;
  hr=CoCreateInstance(&CLSID_DirectInput8,NULL,CLSCTX_INPROC_SERVER,&IID_IDirectInput8A,(void **)&di);
  if (FAILED(hr)||!di) { printf("dinputcom: CREATE A FAIL %lx\n",(unsigned long)hr); return 2; }
  hr=IDirectInput8_Initialize(di,GetModuleHandleA(NULL),DIRECTINPUT_VERSION);
  if (FAILED(hr)) { puts("dinputcom: INIT FAIL"); return 3; }
  IDirectInput8_Release(di);
  hr=CoCreateInstance(&CLSID_DirectInput8,NULL,CLSCTX_INPROC_SERVER,&IID_IDirectInput8W,(void **)&wide);
  if (FAILED(hr)||!wide) { printf("dinputcom: CREATE W FAIL %lx\n",(unsigned long)hr); return 4; }
  IDirectInput8_Release(wide);
  hr=CoCreateInstance(&CLSID_DirectInput8,NULL,CLSCTX_INPROC_SERVER,&IID_IDirectInputDevice8A,&bad);
  if (hr != E_NOINTERFACE || bad) { puts("dinputcom: BAD IID FAIL"); return 5; }
  CoUninitialize(); puts("dinputcom: PASS"); return 0;
}
