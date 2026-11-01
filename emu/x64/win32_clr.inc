/* win32_clr.inc — Win32 surface needed by the official .NET runtime (hostfxr/hostpolicy/coreclr/System.*.dll, win-x64).
   Included from win32.c after the other shim files; CLR_SHIMS is appended at the END of shims[] so its entries win. */

/* ---- wide CRT conversions (UCRT api-ms-win-crt-convert / time / runtime) ---- */
static long long clr_wcstoll(Cpu *c, u64 s, u64 endp, int base, int *ok) {
  char buf[128]; wstr_to_utf8(c, s, buf, sizeof buf, 127); char *e; long long v = e_strtoll(buf, &e, base);
  if (endp) wr64(c, endp, s + (u64)(e - buf) * 2); if (ok) *ok = e != buf; return v;
}
static void s_wcstoul(Cpu *c)  { RET((u64)(u32)clr_wcstoll(c, ARG(0), ARG(1), (int)ARG(2), 0)); }
static void s_wcstol(Cpu *c)   { RET((u64)(s64)(s32)clr_wcstoll(c, ARG(0), ARG(1), (int)ARG(2), 0)); }
static void s_wcstoll(Cpu *c)  { RET((u64)clr_wcstoll(c, ARG(0), ARG(1), (int)ARG(2), 0)); }
static void s_wcstoull(Cpu *c) { RET((u64)clr_wcstoll(c, ARG(0), ARG(1), (int)ARG(2), 0)); }
static void s_wtoi(Cpu *c)     { RET((u64)(s64)(s32)clr_wcstoll(c, ARG(0), 0, 10, 0)); }
static void s_wtoi64(Cpu *c)   { RET((u64)clr_wcstoll(c, ARG(0), 0, 10, 0)); }
static void s_wcstod(Cpu *c)   { char buf[128]; wstr_to_utf8(c, ARG(0), buf, sizeof buf, 127); char *e; double v = e_strtod(buf, &e); if (ARG(1)) wr64(c, ARG(1), ARG(0) + (u64)(e - buf) * 2); c->x[0].f64[0] = v; }
/* wcsftime(buf, count, fmt, tm): format through the narrow implementation */
static void s_wcsftime(Cpu *c) { char f[256]; wstr_to_utf8(c, ARG(2), f, sizeof f, -1); char out[512]; int n = fmt_tm(c, out, sizeof out, f, ARG(3)); if ((u64)n + 1 > ARG(1)) { RET(0); return; } put_w(c, ARG(0), out, 0); RET(n); }
static void s_wcserror_s(Cpu *c) { put_w(c, ARG(0), errstr((int)ARG(2)), (u32)ARG(1)); RET(0); }
static void s_register_thread_local_exe_atexit_callback(Cpu *c) { RET(0); }
static void s_stdio_common_vsnwprintf_s(Cpu *c) { vsprintf_common(c, ARG(0) | 2, ARG(1), ARG(2) < ARG(3) ? ARG(2) : ARG(3) + 1, ARG(4), ARG(6), 1); }

/* ---- processor groups / NUMA: one group, one node ---- */
static void s_GetMaximumProcessorGroupCount(Cpu *c) { RET(1); }
static void s_GetCurrentProcessorNumberEx(Cpu *c) { if (ARG(0)) { wr16(c, ARG(0), 0); wr8(c, ARG(0) + 2, 0); } RET(0); }
static void s_GetNumaHighestNodeNumber(Cpu *c) { if (ARG(0)) wr32(c, ARG(0), 0); RET(1); }
static void s_GetNumaProcessorNodeEx(Cpu *c) { if (ARG(1)) wr16(c, ARG(1), 0); RET(1); }
static void s_GetProcessGroupAffinity(Cpu *c) { u64 cnt = ARG(1); u16 n = cnt ? rd16(c, cnt) : 0; if (cnt) wr16(c, cnt, 1); if (n < 1) { W->last_error = 122; RET(0); return; } if (ARG(2)) wr16(c, ARG(2), 0); RET(1); }
static void s_GetThreadGroupAffinity(Cpu *c) { u64 ga = ARG(1); if (ga) { wr64(c, ga, 1); wr16(c, ga + 8, 0); wr16(c, ga + 10, 0); wr16(c, ga + 12, 0); wr16(c, ga + 14, 0); } RET(1); }
static void s_SetThreadGroupAffinity(Cpu *c) { if (ARG(2)) { wr64(c, ARG(2), 1); wr16(c, ARG(2) + 8, 0); } RET(1); }
static void s_SetThreadIdealProcessorEx(Cpu *c) { if (ARG(2)) { wr16(c, ARG(2), 0); wr8(c, ARG(2) + 2, 0); } RET(1); }
static void s_GetThreadIdealProcessorEx(Cpu *c) { if (ARG(1)) { wr16(c, ARG(1), 0); wr8(c, ARG(1) + 2, 0); } RET(1); }
static void s_VirtualAllocExNuma(Cpu *c) { s_VirtualAllocEx(c); }
static void s_QueryInformationJobObject(Cpu *c) { W->last_error = 87; RET(0); }
static void s_RaiseFailFastException(Cpu *c) { cpu_fault(c, "RaiseFailFastException: the program asked to be terminated (record 0x%llx)", (unsigned long long)ARG(0)); }
/* XState (AVX register state in CONTEXT): we have no AVX, so the legacy CONTEXT is the whole state */
static void s_GetEnabledXStateFeatures(Cpu *c) { RET(3); }
static void s_InitializeContext(Cpu *c) { u64 len = ARG(3); u32 need = 0x4d0 + 16; u32 have = len ? rd32(c, len) : 0; if (len) wr32(c, len, need); if (!ARG(0) || have < need) { W->last_error = 122; RET(0); return; } u64 ctx = (ARG(0) + 15) & ~15ull; u8 *p = mem_ptr(c->mem, ctx, 0x4d0, 1); if (p) e_memset(p, 0, 0x4d0); wr32(c, ctx + 0x30, (u32)ARG(1)); if (ARG(2)) wr64(c, ARG(2), ctx); RET(1); }
static void s_SetXStateFeaturesMask(Cpu *c) { RET(1); }
static void s_LocateXStateFeature(Cpu *c) { u32 f = (u32)ARG(1); if (f == 1) { if (ARG(2)) wr32(c, ARG(2), 512); RET(ARG(0) + 0x100); return; } if (ARG(2)) wr32(c, ARG(2), 0); RET(0); }
static void s_CopyContext(Cpu *c) { u8 *d = mem_ptr(c->mem, ARG(0), 0x4d0, 1); const u8 *s = mem_ptr(c->mem, ARG(2), 0x4d0, 0); if (!d || !s) { W->last_error = 87; RET(0); return; } e_memcpy(d, s, 0x4d0); wr32(c, ARG(0) + 0x30, (u32)ARG(1)); RET(1); }
static void s_WerRegisterRuntimeExceptionModule(Cpu *c) { RET(0); }
static void s_GetSystemDefaultLCID(Cpu *c) { RET(0x409); }
static void s_DisconnectNamedPipe(Cpu *c) { RET(1); }
/* ---- ADVAPI32 tokens / SIDs ---- */
static void s_OpenThreadToken(Cpu *c) { W->last_error = 1008; RET(0); }   /* ERROR_NO_TOKEN: no impersonation */
static void s_RevertToSelf(Cpu *c) { RET(1); }
static void s_SetThreadToken(Cpu *c) { RET(1); }
static void s_SetKernelObjectSecurity(Cpu *c) { RET(1); }
static void s_GetSidSubAuthorityCount(Cpu *c) { RET(ARG(0) + 1); }
static void s_GetSidSubAuthority(Cpu *c) { RET(ARG(0) + 8 + ARG(1) * 4); }
/* ---- ole32 (COM is not provided: the runtime only touches it for interop/apartments) ---- */
static void s_CoTaskMemAlloc(Cpu *c) { RET(w32_heap_alloc(W, ARG(0) ? ARG(0) : 1, 0)); }
static void s_CoTaskMemRealloc(Cpu *c) { if (!ARG(1)) { if (ARG(0)) w32_heap_free(W, ARG(0)); RET(0); return; } RET(heap_realloc(W, ARG(0), ARG(1), 0)); }
static void s_CoCreateGuid(Cpu *c) { u8 *p = mem_ptr(c->mem, ARG(0), 16, 1); if (!p) { RET(0x80070057); return; } static u32 seed = 0x9e3779b9u; for (int i = 0; i < 16; i++) { seed = seed * 1103515245u + 12345u; p[i] = (u8)(seed >> 16); } p[7] = (p[7] & 0x0f) | 0x40; p[8] = (p[8] & 0x3f) | 0x80; RET(0); }
static void s_CoGetContextToken(Cpu *c) { RET(0x800401F0); }   /* CO_E_NOTINITIALIZED */
static void s_CoGetObjectContext(Cpu *c) { RET(0x800401F0); }
static void s_CoRegisterInitializeSpy(Cpu *c) { RET(0x80004001); }
static void s_CoRevokeInitializeSpy(Cpu *c) { RET(0); }
static void s_CoWaitForMultipleHandles(Cpu *c) { RET(0x80004001); }
static void s_CoNotImpl(Cpu *c) { RET(0x80004001); }   /* E_NOTIMPL */
/* combase / WinRT: no Windows Runtime here */
static void s_RoInitialize(Cpu *c) { RET(0); }
static void s_RoGetActivationFactory(Cpu *c) { if (ARG(2)) wr64(c, ARG(2), 0); RET(0x80040154); }   /* REGDB_E_CLASSNOTREG */
static void s_LoadStringW(Cpu *c) { if (ARG(3)) wr16(c, ARG(2), 0); RET(0); }

#define CLR_SHIMS \
  S(wcstoul), S(wcstol), S(wcstoll), S(wcstoull), SA("_wcstoui64", wcstoull), SA("_wcstoi64", wcstoll), SA("_wtoi", wtoi), SA("_wtol", wtoi), SA("_wtoi64", wtoi64), S(wcstod), S(wcsftime), SA("_wcserror_s", wcserror_s), \
  SA("_register_thread_local_exe_atexit_callback", register_thread_local_exe_atexit_callback), SA("__stdio_common_vsnwprintf_s", stdio_common_vsnwprintf_s), \
  S(GetMaximumProcessorGroupCount), S(GetCurrentProcessorNumberEx), S(GetNumaHighestNodeNumber), S(GetNumaProcessorNodeEx), S(GetProcessGroupAffinity), S(GetThreadGroupAffinity), S(SetThreadGroupAffinity), \
  S(SetThreadIdealProcessorEx), S(GetThreadIdealProcessorEx), S(VirtualAllocExNuma), S(QueryInformationJobObject), S(RaiseFailFastException), S(GetEnabledXStateFeatures), S(InitializeContext), S(SetXStateFeaturesMask), \
  S(LocateXStateFeature), S(CopyContext), S(WerRegisterRuntimeExceptionModule), S(GetSystemDefaultLCID), S(DisconnectNamedPipe), \
  S(OpenThreadToken), S(RevertToSelf), S(SetThreadToken), S(SetKernelObjectSecurity), S(GetSidSubAuthorityCount), S(GetSidSubAuthority), \
  S(CoTaskMemAlloc), S(CoTaskMemRealloc), S(CoCreateGuid), S(CoGetContextToken), S(CoGetObjectContext), S(CoRegisterInitializeSpy), S(CoRevokeInitializeSpy), S(CoWaitForMultipleHandles), \
  SA("CoCreateFreeThreadedMarshaler", CoNotImpl), SA("CoGetClassObject", CoNotImpl), SA("CoUnmarshalInterface", CoNotImpl), SA("CoMarshalInterface", CoNotImpl), SA("CoGetMarshalSizeMax", CoNotImpl), SA("CLSIDFromProgID", CoNotImpl), SA("CoReleaseMarshalData", CoNotImpl), \
  S(RoInitialize), SA("RoUninitialize", ret0), S(RoGetActivationFactory), SA("RoOriginateLanguageException", ret0), SA("RoOriginateError", ret0), SA("RoOriginateErrorW", ret0), SA("SetRestrictedErrorInfo", ret0), SA("GetRestrictedErrorInfo", ret1), SA("RoGetErrorReportingFlags", ret0), SA("RoSetErrorReportingFlags", ret0), SA("RoReportUnhandledError", ret0), \
  S(LoadStringW),
