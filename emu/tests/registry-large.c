#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#define N 32768
int main(void) {
  HKEY key=0; DWORD disp=0, type=0, n=N;
  unsigned char *a=malloc(N), *b=malloc(N); if (!a || !b) return 1;
  for (DWORD i=0;i<N;i++) a[i]=(unsigned char)(i*31u);
  LONG r=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\NebulaRegression",0,0,0,KEY_ALL_ACCESS,0,&key,&disp);
  if (r!=ERROR_SUCCESS) { printf("registry-large: create FAIL %ld\n",r); return 2; }
  r=RegSetValueExW(key,L"binary",0,REG_BINARY,a,N);
  if (r!=ERROR_SUCCESS) { printf("registry-large: set FAIL %ld\n",r); return 3; }
  r=RegQueryValueExW(key,L"binary",0,&type,b,&n);
  if (r!=ERROR_SUCCESS || type!=REG_BINARY || n!=N) { printf("registry-large: query FAIL %ld size %lu\n",r,(unsigned long)n); return 4; }
  for (DWORD i=0;i<N;i++) if (a[i]!=b[i]) { puts("registry-large: content FAIL"); return 5; }
  RegCloseKey(key); free(a); free(b); puts("registry-large: PASS"); return 0;
}
