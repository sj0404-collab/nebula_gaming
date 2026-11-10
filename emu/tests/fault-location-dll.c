#include <windows.h>
__declspec(dllexport) void fault_probe(void) { *(volatile int *)0x1c = 42; }
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) { (void)h; (void)reason; (void)reserved; return TRUE; }
