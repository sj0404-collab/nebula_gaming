/* msvcp140 xthreads exports (_Mtx_* / _Cnd_* / _Thrd_*) resolved through GetProcAddress: a producer/consumer queue
   guarded by an in-situ mutex + condition variable, worker threads via _Thrd_create / _Thrd_join, trylock semantics. */
#include <windows.h>
#include <stdio.h>
typedef struct { void *hnd; unsigned id; } thrd_t;
typedef struct { long long sec; long nsec; } xtime_t;
static int (__cdecl *p_Mtx_lock)(void *), (__cdecl *p_Mtx_unlock)(void *), (__cdecl *p_Mtx_trylock)(void *), (__cdecl *p_Mtx_current_owns)(void *);
static void (__cdecl *p_Mtx_init_in_situ)(void *, int), (__cdecl *p_Cnd_init_in_situ)(void *);
static int (__cdecl *p_Cnd_wait)(void *, void *), (__cdecl *p_Cnd_timedwait)(void *, void *, const xtime_t *), (__cdecl *p_Cnd_signal)(void *), (__cdecl *p_Cnd_broadcast)(void *);
static int (__cdecl *p_Thrd_create)(thrd_t *, int (__cdecl *)(void *), void *), (__cdecl *p_Thrd_join)(thrd_t, int *);
static unsigned (__cdecl *p_Thrd_id)(void);
static long long (__cdecl *p_Xtime_get_ticks)(void);
static int (__cdecl *pxtime_get)(xtime_t *, int);
static unsigned char g_mtx[80], g_cv[80];
static int g_q[64], g_head, g_tail, g_done, g_sum;
static int __cdecl producer(void *arg) {
  int id = (int)(size_t)arg;
  for (int i = 1; i <= 100; i++) {
    p_Mtx_lock(g_mtx);
    while (g_tail - g_head == 64) p_Cnd_wait(g_cv, g_mtx);
    g_q[g_tail++ & 63] = i * id; p_Cnd_broadcast(g_cv);
    p_Mtx_unlock(g_mtx);
  }
  p_Mtx_lock(g_mtx); g_done++; p_Cnd_broadcast(g_cv); p_Mtx_unlock(g_mtx);
  return 7 + id;
}
int main(void) {
  HMODULE m = LoadLibraryA("msvcp140.dll"); if (!m) { printf("no msvcp140\n"); return 1; }
#define L(n) *(FARPROC *)&p##n = GetProcAddress(m, #n); if (!p##n) { printf("missing %s\n", #n); return 1; }
  L(_Mtx_lock) L(_Mtx_unlock) L(_Mtx_trylock) L(_Mtx_current_owns) L(_Mtx_init_in_situ) L(_Cnd_init_in_situ) L(_Cnd_wait) L(_Cnd_timedwait)
  L(_Cnd_signal) L(_Cnd_broadcast) L(_Thrd_create) L(_Thrd_join) L(_Thrd_id) L(_Xtime_get_ticks) L(xtime_get)
  p_Mtx_init_in_situ(g_mtx, 2); p_Cnd_init_in_situ(g_cv);
  int r = p_Mtx_lock(g_mtx); int owns = p_Mtx_current_owns(g_mtx); int t = p_Mtx_trylock(g_mtx); p_Mtx_unlock(g_mtx);
  printf("lock=%d owns=%d relock=%d owns_after=%d\n", r, owns, t, p_Mtx_current_owns(g_mtx));
  thrd_t th[3];
  for (int i = 0; i < 3; i++) if (p_Thrd_create(&th[i], producer, (void *)(size_t)(i + 1)) != 0) { printf("create failed\n"); return 1; }
  int got = 0, timeouts = 0;
  p_Mtx_lock(g_mtx);
  while (g_done < 3 || g_head != g_tail) {
    if (g_head == g_tail) {
      xtime_t xt; pxtime_get(&xt, 1); xt.nsec += 20000000; if (xt.nsec >= 1000000000) { xt.sec++; xt.nsec -= 1000000000; }
      if (p_Cnd_timedwait(g_cv, g_mtx, &xt) == 2) timeouts++;
      continue;
    }
    g_sum += g_q[g_head++ & 63]; got++; p_Cnd_signal(g_cv);
  }
  p_Mtx_unlock(g_mtx);
  int codes = 0; for (int i = 0; i < 3; i++) { int rc = -1; p_Thrd_join(th[i], &rc); codes += rc; }
  long long t0 = p_Xtime_get_ticks(); Sleep(30); long long t1 = p_Xtime_get_ticks();
  printf("got=%d sum=%d exit=%d ticks_ok=%d tid_ok=%d timeouts_lt_1000=%d\n", got, g_sum, codes, (t1 - t0) >= 200000 && t1 > 130000000000000000LL, p_Thrd_id() == GetCurrentThreadId(), timeouts < 1000);
  return 0;
}
