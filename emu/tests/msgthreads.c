/* Window messages belong to the creating thread, not a process-wide queue. */
#include <windows.h>
#include <stdio.h>
static HANDLE ready, go; static HWND worker_wnd; static DWORD worker_tid; static volatile LONG delivered;
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b) { if (m == WM_APP+1 && GetCurrentThreadId() == worker_tid) InterlockedIncrement(&delivered); return DefWindowProcW(w,m,a,b); }
static DWORD WINAPI worker(void *unused) {
  (void)unused; worker_tid = GetCurrentThreadId();
  worker_wnd = CreateWindowW(L"thread-msg-test", L"worker", WS_OVERLAPPED, 0,0,50,50,0,0,GetModuleHandleW(NULL),0);
  SetEvent(ready); if (WaitForSingleObject(go, 10000) != WAIT_OBJECT_0) return 2;
  MSG m; int found = 0, threadmsg = 0; while (PeekMessageW(&m, 0, 0, 0, PM_REMOVE)) { if (m.message == WM_APP+1) found++; if (m.message == WM_APP+2 && !m.hwnd && m.wParam == 123 && m.lParam == 456) threadmsg++; DispatchMessageW(&m); }
  return found == 1 && delivered == 1 && threadmsg == 1 ? 0 : 3;
}
int main(void) {
  WNDCLASSW wc = {0}; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"thread-msg-test";
  if (!RegisterClassW(&wc)) return 1;
  HWND main_wnd = CreateWindowW(wc.lpszClassName, L"main", WS_OVERLAPPED, 0,0,50,50,0,0,wc.hInstance,0);
  ready=CreateEventW(0,TRUE,FALSE,0); go=CreateEventW(0,TRUE,FALSE,0); DWORD tid=0; HANDLE th=CreateThread(0,0,worker,0,0,&tid);
  if (!main_wnd || !th || WaitForSingleObject(ready,10000)!=WAIT_OBJECT_0 || !worker_wnd || !tid) { puts("msgthreads: setup FAIL"); return 2; }
  DWORD pid=0; if (GetWindowThreadProcessId(worker_wnd,&pid)!=tid || pid!=GetCurrentProcessId() || GetWindowThreadProcessId(main_wnd,0)!=GetCurrentThreadId()) { puts("msgthreads: owner FAIL"); return 3; }
  if (!PostMessageW(worker_wnd, WM_APP+1, 0, 0)) return 4;
  if (!PostThreadMessageA(tid, WM_APP+2, 123, 456)) { puts("msgthreads: PostThreadMessageA FAIL"); return 7; }
  MSG m; while (PeekMessageW(&m,0,0,0,PM_REMOVE)) if (m.message==WM_APP+1 || m.message==WM_APP+2) { puts("msgthreads: wrong queue FAIL"); return 5; }
  SetEvent(go); DWORD status=99; if (WaitForSingleObject(th,10000)!=WAIT_OBJECT_0 || !GetExitCodeThread(th,&status) || status!=0) { printf("msgthreads: worker FAIL %lu\n",(unsigned long)status); return 6; }
  puts("msgthreads: PASS"); return 0;
}
