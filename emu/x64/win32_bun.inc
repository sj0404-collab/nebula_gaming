/* win32_bun.inc — what Bun-built executables (opencode.exe) need beyond node's surface: the native-API file calls Bun uses instead of
 * CreateFileW (NtCreateFile / NtOpenFile / NtQueryAttributesFile / NtCreateNamedPipeFile with OBJECT_ATTRIBUTES + NT paths), ntdll
 * aliases (RtlWaitOnAddress, RtlExitUserProcess, RtlFillMemory) and a handful of kernel32 stubs (fibers, jobs, pseudo consoles, resources).
 * Included from win32.c after win32_nt.inc. */

/* OBJECT_ATTRIBUTES { ULONG Length @0; HANDLE RootDirectory @8; PUNICODE_STRING ObjectName @16; ULONG Attributes @24; ... }
 * UNICODE_STRING { USHORT Length @0 (bytes); USHORT MaximumLength @2; PWSTR Buffer @8 }
 * NT paths: "\??\C:\x", "\??\UNC\srv\share", "\Device\NamedPipe\name", "\??\pipe\name"; relative names resolve against RootDirectory. */
static int e_strnicmp_(const char *a, const char *b, size_t n) { for (size_t i = 0; i < n; i++) { int x = (u8)a[i], y = (u8)b[i]; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return x - y; if (!x) return 0; } return 0; }
static void s_GetModuleFileNameW(Cpu *c);
static int nt_object_path(Cpu *c, u64 oa, char *out, size_t cap) {
  if (!oa) return 0; u64 root = rd64(c, oa + 8), us = rd64(c, oa + 16); char name[1024]; name[0] = 0;
  if (us) { u32 len = rd16(c, us) / 2; u64 buf = rd64(c, us + 8); if (buf && len) { size_t o = 0; for (u32 k = 0; k < len && o + 4 < sizeof name; k++) { u32 ch = rd16(c, buf + k * 2);
        if (ch < 0x80) name[o++] = (char)ch; else if (ch < 0x800) { name[o++] = (char)(0xC0 | (ch >> 6)); name[o++] = (char)(0x80 | (ch & 63)); } else { name[o++] = (char)(0xE0 | (ch >> 12)); name[o++] = (char)(0x80 | ((ch >> 6) & 63)); name[o++] = (char)(0x80 | (ch & 63)); } } name[o] = 0; } }
  const char *n = name;
  if (!e_strnicmp_(n, "\\??\\UNC\\", 8)) { e_snprintf(out, cap, "\\\\%s", n + 8); return 1; }
  if (!e_strnicmp_(n, "\\??\\pipe\\", 9)) { e_snprintf(out, cap, "\\\\.\\pipe\\%s", n + 9); return 1; }
  if (!e_strnicmp_(n, "\\Device\\NamedPipe\\", 18)) { e_snprintf(out, cap, "\\\\.\\pipe\\%s", n + 18); return 1; }
  if (!e_strnicmp_(n, "\\??\\", 4)) n += 4; else if (!e_strnicmp_(n, "\\DosDevices\\", 12)) n += 12;
  if (!e_strnicmp_(n, "\\\\?\\", 4)) n += 4;
  if (root && root != ~0ull) { int fd = handle_fd(root); const char *base = fd > 0 ? vfs_fd_path(fd) : 0; if (!base) return 0;
    if (!*n) e_snprintf(out, cap, "%s", base); else { size_t bl = e_strlen(base); e_snprintf(out, cap, "%s%s%s", base, bl && (base[bl - 1] == '\\' || base[bl - 1] == '/') ? "" : "\\", n); } return 1; }
  if (!e_strnicmp_(n, "\\Device\\Null", 12) || !e_strnicmp_(n, "\\??\\NUL", 7)) { e_snprintf(out, cap, "NUL"); return 1; }
  if (!*n) return 0;
  e_snprintf(out, cap, "%s", n); return 1;
}
/* NtCreateFile(&h, access, oa, iosb, alloc, attrs, share, disposition, options, ea, ealen) */
static void s_NtCreateFile(Cpu *c) {
  u64 hp = ARG(0); u32 access = (u32)ARG(1); u64 oa = ARG(2), iosb = ARG(3); u32 disp = (u32)ARG(7), opts = (u32)ARG(8);
  char path[1024]; if (!nt_object_path(c, oa, path, sizeof path)) { nt_iosb(c, iosb, 0xC0000033u, 0); RET(0xC0000033u); return; }   /* OBJECT_NAME_INVALID */
  /* named pipes: client side of a CreateNamedPipe server, or the server end created through NtCreateNamedPipeFile */
  if (npipe_prefix(path)) { for (int k = 0; k < NPIPE_MAX; k++) if (g_npipes[k].used && !e_stricmp(g_npipes[k].name, path)) { int fd = g_npipes[k].fd; g_npipes[k].used = 0; vfs_set_inherit(fd, 0); wr64(c, hp, H_OF(fd)); nt_iosb(c, iosb, 0, 1); RET(0); return; }
    nt_iosb(c, iosb, 0xC0000034u, 0); RET(0xC0000034u); return; }
  if (!e_stricmp(path, "CONOUT$")) { wr64(c, hp, 0x11); nt_iosb(c, iosb, 0, 1); RET(0); return; } if (!e_stricmp(path, "CONIN$")) { wr64(c, hp, 0x10); nt_iosb(c, iosb, 0, 1); RET(0); return; }
  int wdisp = disp == 0 ? 2 : disp == 1 ? 3 : disp == 2 ? 1 : disp == 3 ? 4 : disp == 4 ? 5 : 2;   /* SUPERSEDE OPEN CREATE OPEN_IF OVERWRITE OVERWRITE_IF */
  u64 size = 0; int st = vfs_stat(path, &size);   /* 0 missing, 1 file, 2 dir, 3 null device */
  if ((opts & 1) && disp != 1 && st == 0) { if (!vfs_mkdir(path)) { nt_iosb(c, iosb, 0xC000003Au, 0); RET(0xC000003Au); return; } st = 2; }   /* FILE_DIRECTORY_FILE + create */
  if ((opts & 1) && st == 1) { nt_iosb(c, iosb, 0xC0000103u, 0); RET(0xC0000103u); return; }     /* NOT_A_DIRECTORY */
  if ((opts & 0x40) && st == 2) { nt_iosb(c, iosb, 0xC00000BAu, 0); RET(0xC00000BAu); return; }  /* FILE_IS_A_DIRECTORY */
  if (st == 2 && disp == 2) { nt_iosb(c, iosb, 0xC0000035u, 0); RET(0xC0000035u); return; }      /* OBJECT_NAME_COLLISION */
  int acc = ((access & 0x80000001u) ? 1 : 0) | ((access & 0x40000006u) ? 2 : 0); if (!acc || st == 2) acc = st == 2 ? 1 : 1;
  if (st == 2) wdisp = 3;
  int fd = vfs_open(path, acc, wdisp, (access & 4) && !(access & 0x40000002u));
  if (fd < 0) { u32 ns = fd == -2 ? 0xC0000034u : fd == -17 ? 0xC0000035u : fd == -21 ? 0xC00000BAu : fd == -24 ? 0xC000009Au : 0xC0000022u; nt_iosb(c, iosb, ns, 0); RET(ns); return; }
  u64 info = st == 0 ? 2 : (disp == 4 || disp == 5) ? 3 : disp == 0 ? 0 : 1;   /* FILE_CREATED / OVERWRITTEN / SUPERSEDED / OPENED */
  wr64(c, hp, H_FILE_BASE + (u64)fd); nt_iosb(c, iosb, 0, info); RET(0);
}
/* NtOpenFile(&h, access, oa, iosb, share, options) = NtCreateFile with FILE_OPEN */
static void s_NtOpenFile(Cpu *c) { u64 a0 = ARG(0), a1 = ARG(1), a2 = ARG(2), a3 = ARG(3), opts = ARG(5);
  u64 *sp = (u64 *)mem_ptr(c->mem, c->r[RSP], 0x60, 1); if (!sp) { RET(NT_INVALID_PARAMETER); return; }
  sp[4] = a0; sp[5] = a1; sp[6] = a2; sp[7] = a3; sp[8] = 0; sp[9] = 0; sp[10] = 0; sp[11] = 1; sp[12] = opts; sp[13] = 0; sp[14] = 0;   /* rebuild the 11-argument frame (ARG(n>=4) reads [rsp+8+8n]) */
  c->r[RCX] = a0; c->r[RDX] = a1; c->r[R8] = a2; c->r[R9] = a3; s_NtCreateFile(c); }
/* NtQueryAttributesFile(oa, FILE_BASIC_INFORMATION*) */
static void s_NtQueryAttributesFile(Cpu *c) { char path[1024]; if (!nt_object_path(c, ARG(0), path, sizeof path)) { RET(0xC0000033u); return; }
  u64 size = 0; int st = vfs_stat(path, &size); if (!st) { RET(0xC0000034u); return; }
  u64 a = ARG(1), ft = vfs_filetime(); wr64(c, a, ft); wr64(c, a + 8, ft); wr64(c, a + 16, ft); wr64(c, a + 24, ft); wr32(c, a + 32, st == 2 ? 0x10 : 0x80); wr32(c, a + 36, 0); RET(0); }
/* NtQueryFullAttributesFile(oa, FILE_NETWORK_OPEN_INFORMATION* 56 B: 4 times, AllocationSize @32, EndOfFile @40, Attributes @48) */
static void s_NtQueryFullAttributesFile(Cpu *c) { char path[1024]; if (!nt_object_path(c, ARG(0), path, sizeof path)) { RET(0xC0000033u); return; }
  u64 size = 0; int st = vfs_stat(path, &size); if (!st) { RET(0xC0000034u); return; }
  u64 a = ARG(1), ft = vfs_filetime(); for (int k = 0; k < 4; k++) wr64(c, a + 8 * k, ft); wr64(c, a + 32, (size + 4095) & ~4095ull); wr64(c, a + 40, size); wr32(c, a + 48, st == 2 ? 0x10 : 0x80); wr32(c, a + 52, 0); RET(0); }
/* NtCreateNamedPipeFile(&h, access, oa, iosb, share, disposition, options, type, readmode, completionmode, maxinst, inquota, outquota, timeout):
 * server end; the client opens the same name through NtCreateFile/CreateFileW. Direction from the access mask (GENERIC_READ/WRITE, FILE_READ/WRITE_DATA). */
static void s_NtCreateNamedPipeFile(Cpu *c) { u64 hp = ARG(0); u32 access = (u32)ARG(1); u64 iosb = ARG(3);
  char path[1024]; if (!nt_object_path(c, ARG(2), path, sizeof path) || !npipe_prefix(path)) { nt_iosb(c, iosb, 0xC0000033u, 0); RET(0xC0000033u); return; }
  int r, wfd; if (vfs_pipe(&r, &wfd)) { nt_iosb(c, iosb, 0xC000009Au, 0); RET(0xC000009Au); return; }
  int server_writes = (access & 0x40000002u) && !(access & 0x80000001u);
  int ours = server_writes ? wfd : r, theirs = ours == r ? wfd : r; vfs_set_inherit(ours, 0); vfs_set_inherit(theirs, 0);
  int k; for (k = 0; k < NPIPE_MAX && g_npipes[k].used; k++) { } if (k == NPIPE_MAX) { vfs_close(r); vfs_close(wfd); nt_iosb(c, iosb, 0xC000009Au, 0); RET(0xC000009Au); return; }
  g_npipes[k].used = 1; g_npipes[k].fd = theirs; e_snprintf(g_npipes[k].name, sizeof g_npipes[k].name, "%s", path);
  wr64(c, hp, H_OF(ours)); nt_iosb(c, iosb, 0, 2); RET(0); }
static void s_RtlExitUserProcess(Cpu *c) { do_exit(c, (int)ARG(0)); }
static void s_RtlFillMemory(Cpu *c) { u8 *p = mem_ptr(c->mem, ARG(0), ARG(1), 1); if (p) e_memset(p, (int)(ARG(2) & 0xff), (size_t)ARG(1)); RET(0); }
/* RtlWaitOnAddress(addr, cmp, size, timeout*) — like WaitOnAddress but the timeout is a LARGE_INTEGER* (negative = relative 100 ns) and the result is NTSTATUS */
static void s_RtlWaitOnAddress(Cpu *c) { int retry = W->wait_active && W->wait_rip == c->rip;
  if (!retry) { u64 tp = ARG(3); u32 ms = 0xffffffffu; if (tp) { s64 t = (s64)rd64(c, tp); ms = t >= 0 ? 0 : (u32)((-t) / 10000); } c->r[R9] = ms; }   /* re-executed after a park: R9 already holds the ms */
  s_WaitOnAddress(c); if (W->block != BLOCK_NONE) return; c->r[RAX] = c->r[RAX] ? 0 : 0x102; }   /* 0x102 = STATUS_TIMEOUT */
static void s_IsThreadAFiber(Cpu *c) { RET(0); }
static void s_GetHostNameW(Cpu *c) { static const char h[] = "nebula"; if ((u32)ARG(1) > 6) { for (int i = 0; i < 7; i++) wr16(c, ARG(0) + 2 * i, (u8)h[i]); RET(0); return; } W->last_error = 10014; RET(-1); }
static void s_GlobalSize(Cpu *c) { RET(w32_heap_size(W, ARG(0))); }
static void s_IsProcessInJob(Cpu *c) { if (ARG(2)) wr32(c, ARG(2), 0); RET(1); }
static void s_QueryDosDeviceW(Cpu *c) { W->last_error = 2; RET(0); }
static void s_K32GetModuleFileNameExW(Cpu *c) { c->r[RCX] = ARG(1); c->r[RDX] = ARG(2); c->r[R8] = ARG(3); s_GetModuleFileNameW(c); }
static void s_CreateDirectoryExW(Cpu *c) { c->r[RCX] = ARG(1); c->r[RDX] = ARG(2); s_CreateDirectoryW(c); }
static void s_CreatePseudoConsole(Cpu *c) { RET(0x80004001u); }   /* E_NOTIMPL: Bun falls back to pipes */
static void s_ResizePseudoConsole(Cpu *c) { RET(0x80004001u); }
static void s_ClosePseudoConsole(Cpu *c) { RET(0); }
static void s_CreateMemoryResourceNotification(Cpu *c) { KObj *k = 0; int ex = 0; u64 h = new_kobj_handle(c, KO_EVENT, 0, 0, &ex, &k); RET(h); }   /* never signalled */
/* GetTimeZoneInformationForYear(year, dynamic*, TIME_ZONE_INFORMATION* 172 B): UTC */
static void s_GetTimeZoneInformationForYear(Cpu *c) { u8 *p = mem_ptr(c->mem, ARG(2), 172, 1); if (p) { e_memset(p, 0, 172); static const char n[] = "UTC"; for (int i = 0; i < 4; i++) { p[4 + 2 * i] = (u8)n[i]; p[88 + 2 * i] = (u8)n[i]; } } RET(1); }

#define BUN_SHIMS \
  S(NtCreateFile), S(NtOpenFile), S(NtQueryAttributesFile), S(NtQueryFullAttributesFile), S(NtCreateNamedPipeFile), S(RtlExitUserProcess), S(RtlFillMemory), S(RtlWaitOnAddress), \
  SA("RtlWakeAddressAll", WakeByAddressAll), SA("RtlWakeAddressSingle", WakeByAddressSingle), S(IsThreadAFiber), S(GetHostNameW), SA("RtlDllShutdownInProgress", ret0), S(GlobalSize), S(IsProcessInJob), S(QueryDosDeviceW), S(K32GetModuleFileNameExW), \
  S(CreateDirectoryExW), S(CreatePseudoConsole), S(ResizePseudoConsole), S(ClosePseudoConsole), S(CreateMemoryResourceNotification), S(GetTimeZoneInformationForYear), \
  SA("CreateJobObjectA", CreateJobObjectW), SA("AttachConsole", ret0), SA("BeginUpdateResourceW", ret0), SA("EndUpdateResourceW", ret0), SA("UpdateResourceW", ret0), \
  SA("EnumResourceLanguagesW", ret0), SA("EnumResourceNamesW", ret0), SA("FindResourceExW", ret0), SA("FindResourceW", ret0), SA("FreeResource", ret1),
