#include <windows.h>
#include <stdio.h>
#include <string.h>
__declspec(noinline) static void crash_here(void) { *(volatile int *)0x1c = 42; }
int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "dll")) {
    HMODULE m = LoadLibraryA("faultprobe.dll");
    void (*fn)(void) = m ? (void (*)(void))GetProcAddress(m, "fault_probe") : NULL;
    if (!fn) { puts("fault-location: DLL load FAIL"); return 1; }
    fn();
  } else crash_here();
  return 0;
}
