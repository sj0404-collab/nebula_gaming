/* Run a guest executable repeatedly inside the same emulator instance. */
#include "../x64/emu.h"
#include <stdio.h>
#include <stdlib.h>
int x64_load(const u8 *, size_t, const char *, const char *);
int x64_run(u64); int x64_exit_code(void); const char *x64_error(void);
int main(int argc, char **argv) {
  if (argc != 2) return 1;
  FILE *f=fopen(argv[1],"rb"); if (!f) return 2;
  fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
  u8 *p=malloc((size_t)n); if (!p || fread(p,1,(size_t)n,f)!=(size_t)n) return 3; fclose(f);
  for (int i=0;i<3;i++) {
    if (x64_load(p,(size_t)n,"relaunch.exe","relaunch.exe")) { puts("reload: load FAIL"); return 4; }
    int r=0; for (int k=0;k<1000 && (r==RUN_OK || r==RUN_YIELD);k++) r=x64_run(1000000);
    if (r!=RUN_EXIT || x64_exit_code()!=0) { printf("reload: run %d FAIL r=%d exit=%d error=%s\n",i,r,x64_exit_code(),x64_error()); return 5; }
  }
  puts("reload: PASS"); return 0;
}
