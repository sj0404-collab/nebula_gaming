/* advapi32 access-control structures: SECURITY_DESCRIPTOR / ACL / ACE manipulation (pure in-memory layouts, done exactly)
   plus Get/SetFileSecurity over the virtual disk. The disk keeps no per-file ACLs (one user, one session): GetFileSecurity
   returns the owner = the logged-on user and a DACL (user, Administrators, SYSTEM: full control), SetFileSecurity accepts
   and keeps nothing - the same picture HotSpot, installers and file managers see on a fresh NTFS user profile.
   Included from win32.c (sid_put / sid_len live there). Layouts (x64 / x86):
     SECURITY_DESCRIPTOR absolute { u8 rev; u8 sbz; u16 control; PSID owner; PSID group; PACL sacl; PACL dacl } = 40 / 20
     self-relative: the four pointers are u32 offsets from the descriptor start
     ACL { u8 rev; u8 sbz; u16 size; u16 count; u16 sbz2 }  ACE_HEADER { u8 type; u8 flags; u16 size }  ACCESS_ALLOWED_ACE { hdr; u32 mask; SID } */
#define SD_CTRL_DACL_PRESENT 0x0004
#define SD_CTRL_DACL_DEFAULTED 0x0008
#define SD_CTRL_SELF_RELATIVE 0x8000
static u32 acl_sid_len(Cpu *c, u64 sid) { return sid ? 8u + 4u * rd8(c, sid + 1) : 0; }
static u64 sd_field(Cpu *c, u64 sd, int k) { /* k: 0 owner 1 group 2 sacl 3 dacl → absolute pointer, honouring self-relative descriptors */ u64 P = PSZ_OF(c); u16 ctrl = rd16(c, sd + 2);
  if (ctrl & SD_CTRL_SELF_RELATIVE) { u32 off = rd32(c, sd + 4 + 4 * (u64)k); return off ? sd + off : 0; } return rdp(c, sd + 4 + ((P == 8) ? 4 : 0) + P * (u64)k); }
static void sd_set_field(Cpu *c, u64 sd, int k, u64 v) { u64 P = PSZ_OF(c); wrp(c, sd + 4 + ((P == 8) ? 4 : 0) + P * (u64)k, v); }
static void s_InitializeSecurityDescriptor(Cpu *c) { u64 sd = ARG(0); if (!sd || (u32)ARG(1) != 1) { W->last_error = 87; RET(0); return; } u8 *p = mem_ptr(c->mem, sd, PSZ_OF(c) == 8 ? 40 : 20, 1); if (!p) { RET(0); return; } e_memset(p, 0, PSZ_OF(c) == 8 ? 40 : 20); p[0] = 1; RET(1); }
static void s_SetSecurityDescriptorDacl(Cpu *c) { /* (sd, present, dacl, defaulted) */ u64 sd = ARG(0); if (!sd) { W->last_error = 87; RET(0); return; } u16 ctrl = rd16(c, sd + 2) & ~(SD_CTRL_DACL_PRESENT | SD_CTRL_DACL_DEFAULTED);
  if (ARG(1)) { ctrl |= SD_CTRL_DACL_PRESENT; sd_set_field(c, sd, 3, ARG(2)); if (ARG(3)) ctrl |= SD_CTRL_DACL_DEFAULTED; } else sd_set_field(c, sd, 3, 0); wr16(c, sd + 2, ctrl); RET(1); }
static void s_GetSecurityDescriptorDacl(Cpu *c) { /* (sd, *present, *dacl, *defaulted) */ u64 sd = ARG(0); if (!sd || !ARG(1) || !ARG(2) || !ARG(3)) { W->last_error = 87; RET(0); return; } u16 ctrl = rd16(c, sd + 2);
  wr32(c, ARG(1), (ctrl & SD_CTRL_DACL_PRESENT) ? 1 : 0); wrp(c, ARG(2), (ctrl & SD_CTRL_DACL_PRESENT) ? sd_field(c, sd, 3) : 0); wr32(c, ARG(3), (ctrl & SD_CTRL_DACL_DEFAULTED) ? 1 : 0); RET(1); }
static void s_SetSecurityDescriptorOwner(Cpu *c) { u64 sd = ARG(0); if (!sd) { W->last_error = 87; RET(0); return; } sd_set_field(c, sd, 0, ARG(1)); u16 ctrl = rd16(c, sd + 2) & ~0x1; if (ARG(2)) ctrl |= 0x1; wr16(c, sd + 2, ctrl); RET(1); }
static void s_SetSecurityDescriptorGroup(Cpu *c) { u64 sd = ARG(0); if (!sd) { W->last_error = 87; RET(0); return; } sd_set_field(c, sd, 1, ARG(1)); u16 ctrl = rd16(c, sd + 2) & ~0x2; if (ARG(2)) ctrl |= 0x2; wr16(c, sd + 2, ctrl); RET(1); }
static void s_GetSecurityDescriptorOwner(Cpu *c) { u64 sd = ARG(0); if (!sd || !ARG(1) || !ARG(2)) { W->last_error = 87; RET(0); return; } wrp(c, ARG(1), sd_field(c, sd, 0)); wr32(c, ARG(2), (rd16(c, sd + 2) & 0x1) ? 1 : 0); RET(1); }
static void s_GetSecurityDescriptorGroup(Cpu *c) { u64 sd = ARG(0); if (!sd || !ARG(1) || !ARG(2)) { W->last_error = 87; RET(0); return; } wrp(c, ARG(1), sd_field(c, sd, 1)); wr32(c, ARG(2), (rd16(c, sd + 2) & 0x2) ? 1 : 0); RET(1); }
static void s_GetSecurityDescriptorControl(Cpu *c) { u64 sd = ARG(0); if (!sd || !ARG(1) || !ARG(2)) { W->last_error = 87; RET(0); return; } wr16(c, ARG(1), rd16(c, sd + 2)); wr32(c, ARG(2), 1); RET(1); }
static void s_IsValidSecurityDescriptor(Cpu *c) { RET(ARG(0) && rd8(c, ARG(0)) == 1); }
static void s_GetSecurityDescriptorLength(Cpu *c) { u64 sd = ARG(0); u32 n = 20; if (sd) { for (int k = 0; k < 2; k++) { u64 s = sd_field(c, sd, k); n += acl_sid_len(c, s); } for (int k = 2; k < 4; k++) { u64 a = sd_field(c, sd, k); if (a) n += rd16(c, a + 2); } } RET(n); }
/* ACLs */
static void s_InitializeAcl(Cpu *c) { /* (acl, len, rev) */ u64 a = ARG(0); u32 len = (u32)ARG(1); if (!a || len < 8 || len > 0xffff || ((u32)ARG(2) != 2 && (u32)ARG(2) != 4)) { W->last_error = 87; RET(0); return; } u8 *p = mem_ptr(c->mem, a, len, 1); if (!p) { RET(0); return; } e_memset(p, 0, len); wr8(c, a, (u8)ARG(2)); wr16(c, a + 2, (u16)len); RET(1); }
static void s_IsValidAcl(Cpu *c) { RET(ARG(0) && (rd8(c, ARG(0)) == 2 || rd8(c, ARG(0)) == 4) && rd16(c, ARG(0) + 2) >= 8); }
static u32 acl_used(Cpu *c, u64 a) { u32 n = rd16(c, a + 4), off = 8; for (u32 i = 0; i < n; i++) { u16 sz = rd16(c, a + off + 2); if (!sz) break; off += sz; } return off; }
static void s_GetAclInformation(Cpu *c) { /* (acl, buf, len, class) */ u64 a = ARG(0), b = ARG(1); u32 len = (u32)ARG(2), cls = (u32)ARG(3); if (!a || !b) { W->last_error = 87; RET(0); return; }
  if (cls == 1) { if (len < 4) { W->last_error = 122; RET(0); return; } wr32(c, b, rd8(c, a)); RET(1); return; }
  if (cls == 2) { if (len < 12) { W->last_error = 122; RET(0); return; } u32 size = rd16(c, a + 2), used = acl_used(c, a); wr32(c, b, rd16(c, a + 4)); wr32(c, b + 4, used); wr32(c, b + 8, size > used ? size - used : 0); RET(1); return; }
  W->last_error = 87; RET(0); }
static void s_GetAce(Cpu *c) { /* (acl, index, *ace) */ u64 a = ARG(0); u32 idx = (u32)ARG(1); if (!a || !ARG(2) || idx >= rd16(c, a + 4)) { W->last_error = 87; RET(0); return; } u32 off = 8; for (u32 i = 0; i < idx; i++) off += rd16(c, a + off + 2); wrp(c, ARG(2), a + off); RET(1); }
static int acl_append(Cpu *c, u64 a, const u8 *ace, u32 n, u32 index) { u32 size = rd16(c, a + 2), used = acl_used(c, a); if (used + n > size) { W->last_error = 1130; return 0; }   /* ERROR_ALLOTTED_SPACE_EXCEEDED */
  u32 count = rd16(c, a + 4), at = used; if (index < count) { at = 8; for (u32 i = 0; i < index; i++) at += rd16(c, a + at + 2); u8 *p = mem_ptr(c->mem, a + at, used - at + n, 1); if (!p) return 0; e_memmove(p + n, p, used - at); }
  u8 *d = mem_ptr(c->mem, a + at, n, 1); if (!d) return 0; e_memcpy(d, ace, n); wr16(c, a + 4, (u16)(count + 1)); return 1; }
static void add_allow_deny(Cpu *c, u8 type, u32 mask, u64 sid, u8 flags) { u64 a = ARG(0); u32 sl = acl_sid_len(c, sid); if (!a || !sid || sl < 8) { W->last_error = 87; RET(0); return; } u8 ace[8 + 68]; if (sl > 68) { W->last_error = 1337; RET(0); return; }
  ace[0] = type; ace[1] = flags; ace[2] = (u8)(8 + sl); ace[3] = (u8)((8 + sl) >> 8); e_memcpy(ace + 4, &mask, 4); const u8 *s = mem_ptr(c->mem, sid, sl, 0); if (!s) { RET(0); return; } e_memcpy(ace + 8, s, sl); RET(acl_append(c, a, ace, 8 + sl, 0xffffffffu)); }
static void s_AddAccessAllowedAce(Cpu *c) { add_allow_deny(c, 0, (u32)ARG(2), ARG(3), 0); }
static void s_AddAccessAllowedAceEx(Cpu *c) { add_allow_deny(c, 0, (u32)ARG(3), ARG(4), (u8)ARG(2)); }
static void s_AddAccessDeniedAce(Cpu *c) { add_allow_deny(c, 1, (u32)ARG(2), ARG(3), 0); }
static void s_AddAccessDeniedAceEx(Cpu *c) { add_allow_deny(c, 1, (u32)ARG(3), ARG(4), (u8)ARG(2)); }
static void s_AddAce(Cpu *c) { /* (acl, rev, index, list, len) */ u64 a = ARG(0), list = ARG(3); u32 len = (u32)ARG(4); if (!a || !list || !len) { W->last_error = 87; RET(0); return; } const u8 *src = mem_ptr(c->mem, list, len, 0); if (!src) { RET(0); return; }
  u8 tmp[1024]; if (len > sizeof tmp) { W->last_error = 1130; RET(0); return; } e_memcpy(tmp, src, len); u32 off = 0, index = (u32)ARG(2); while (off < len) { u16 sz = (u16)(tmp[off + 2] | (tmp[off + 3] << 8)); if (!sz || off + sz > len) break; if (!acl_append(c, a, tmp + off, sz, index)) { RET(0); return; } if (index != 0xffffffffu) index++; off += sz; } RET(1); }
static void s_DeleteAce(Cpu *c) { u64 a = ARG(0); u32 idx = (u32)ARG(1), n = rd16(c, a + 4); if (!a || idx >= n) { W->last_error = 87; RET(0); return; } u32 used = acl_used(c, a), off = 8; for (u32 i = 0; i < idx; i++) off += rd16(c, a + off + 2); u16 sz = rd16(c, a + off + 2);
  u8 *p = mem_ptr(c->mem, a + off, used - off, 1); if (!p) { RET(0); return; } e_memmove(p, p + sz, used - off - sz); wr16(c, a + 4, (u16)(n - 1)); RET(1); }
/* file security: a self-relative descriptor built on demand */
static u32 build_file_sd(Cpu *c, u64 buf, u32 cap, u32 info) { static const u32 user[5] = { 21, 1000, 2000, 3000, 500 }, admins[2] = { 32, 544 }, sys[1] = { 18 }, none[5] = { 21, 1000, 2000, 3000, 513 };
  u32 need = 20; u32 owner_off = 0, group_off = 0, dacl_off = 0;
  if (info & 1) { owner_off = need; need += 28; } if (info & 2) { group_off = need; need += 28; } if (info & 4) { dacl_off = need; need += 8 + (8 + 28) + (8 + 16) + (8 + 12); }
  if (cap < need || !buf) return need;
  u8 *p = mem_ptr(c->mem, buf, need, 1); if (!p) return need; e_memset(p, 0, need); wr8(c, buf, 1); wr16(c, buf + 2, SD_CTRL_SELF_RELATIVE | ((info & 4) ? SD_CTRL_DACL_PRESENT : 0) | 0x0400 /* SE_DACL_AUTO_INHERITED */);
  if (owner_off) { wr32(c, buf + 4, owner_off); sid_put(c, buf + owner_off, 5, user); } if (group_off) { wr32(c, buf + 8, group_off); sid_put(c, buf + group_off, 5, none); }
  if (dacl_off) { wr32(c, buf + 16, dacl_off); u64 a = buf + dacl_off; u32 total = 8 + 36 + 24 + 20; wr8(c, a, 2); wr16(c, a + 2, (u16)total); wr16(c, a + 4, 3); u64 e = a + 8;
    wr8(c, e, 0); wr8(c, e + 1, 0x3 /* OBJECT_INHERIT | CONTAINER_INHERIT */); wr16(c, e + 2, 36); wr32(c, e + 4, 0x1f01ff); sid_put(c, e + 8, 5, user); e += 36;
    wr8(c, e, 0); wr8(c, e + 1, 0x3); wr16(c, e + 2, 24); wr32(c, e + 4, 0x1f01ff); sid_put(c, e + 8, 2, admins); e += 24;
    wr8(c, e, 0); wr8(c, e + 1, 0x3); wr16(c, e + 2, 20); wr32(c, e + 4, 0x1f01ff); sid_put(c, e + 8, 1, sys); }
  return need; }
static void file_security_get(Cpu *c, int wide) { /* (name, info, sd, len, *needed) */ char path[1040]; if (wide) wstr_to_utf8(c, ARG(0), path, sizeof path, -1); else { const char *s = rd_str(c, ARG(0), 1000); e_snprintf(path, sizeof path, "%s", s ? s : ""); }
  u64 sz; if (!path[0] || !vfs_stat(path, &sz)) { W->last_error = 2; RET(0); return; } u32 need = build_file_sd(c, ARG(2), (u32)ARG(3), (u32)ARG(1)); if (ARG(4)) wr32(c, ARG(4), need); if (need > (u32)ARG(3) || !ARG(2)) { W->last_error = 122; RET(0); return; } RET(1); }
static void s_GetFileSecurityW(Cpu *c) { file_security_get(c, 1); }
static void s_GetFileSecurityA(Cpu *c) { file_security_get(c, 0); }
static void file_security_set(Cpu *c, int wide) { char path[1040]; if (wide) wstr_to_utf8(c, ARG(0), path, sizeof path, -1); else { const char *s = rd_str(c, ARG(0), 1000); e_snprintf(path, sizeof path, "%s", s ? s : ""); } u64 sz; if (!path[0] || !vfs_stat(path, &sz)) { W->last_error = 2; RET(0); return; } if (!ARG(2)) { W->last_error = 87; RET(0); return; } RET(1); }
static void s_SetFileSecurityW(Cpu *c) { file_security_set(c, 1); }
static void s_SetFileSecurityA(Cpu *c) { file_security_set(c, 0); }
static void s_GetKernelObjectSecurity(Cpu *c) { /* (handle, info, sd, len, *needed) */ u32 need = build_file_sd(c, ARG(2), (u32)ARG(3), (u32)ARG(1)); if (ARG(4)) wr32(c, ARG(4), need); if (need > (u32)ARG(3) || !ARG(2)) { W->last_error = 122; RET(0); return; } RET(1); }
static void s_GetSecurityInfo(Cpu *c) { /* (handle, type, info, *owner, *group, *dacl, *sacl, *sd): one heap block, LocalFree'd by the caller */ u32 info = (u32)ARG(2); u32 need = build_file_sd(c, 0, 0, info); u64 blk = w32_heap_alloc(W, need, 1); if (!blk) { RET(8); return; } build_file_sd(c, blk, need, info);
  if (ARG(3)) wrp(c, ARG(3), (info & 1) ? blk + rd32(c, blk + 4) : 0); if (ARG(4)) wrp(c, ARG(4), (info & 2) ? blk + rd32(c, blk + 8) : 0); if (ARG(5)) wrp(c, ARG(5), (info & 4) ? blk + rd32(c, blk + 16) : 0); if (ARG(6)) wrp(c, ARG(6), 0); if (ARG(7)) wrp(c, ARG(7), blk); RET(0); }
static void s_GetNamedSecurityInfoW(Cpu *c) { /* (name, type, info, *owner, *group, *dacl, *sacl, *sd) */ char path[1040]; wstr_to_utf8(c, ARG(0), path, sizeof path, -1); u64 sz; if ((u32)ARG(1) == 1 && !vfs_stat(path, &sz)) { RET(2); return; } s_GetSecurityInfo(c); }
static void s_GetNamedSecurityInfoA(Cpu *c) { const char *s = rd_str(c, ARG(0), 1000); u64 sz; if ((u32)ARG(1) == 1 && (!s || !vfs_stat(s, &sz))) { RET(2); return; } s_GetSecurityInfo(c); }
static void s_SetSecurityInfo(Cpu *c) { RET(0); }
static void s_SetNamedSecurityInfoW(Cpu *c) { RET(0); }
static void s_SetNamedSecurityInfoA(Cpu *c) { RET(0); }
static void s_SetKernelObjectSecurity(Cpu *c) { RET(1); }
static void s_MakeSelfRelativeSD(Cpu *c) { /* (abs, self, *len) */ u64 sd = ARG(0), out = ARG(1); if (!sd || !ARG(2)) { W->last_error = 87; RET(0); return; } u32 need = 20; u32 lens[4]; for (int k = 0; k < 4; k++) { u64 f = sd_field(c, sd, k); lens[k] = !f ? 0 : k < 2 ? acl_sid_len(c, f) : rd16(c, f + 2); need += lens[k]; }
  if (rd32(c, ARG(2)) < need || !out) { wr32(c, ARG(2), need); W->last_error = 122; RET(0); return; } u8 *p = mem_ptr(c->mem, out, need, 1); if (!p) { RET(0); return; } e_memset(p, 0, 20); wr8(c, out, 1); wr16(c, out + 2, rd16(c, sd + 2) | SD_CTRL_SELF_RELATIVE); u32 off = 20;
  for (int k = 0; k < 4; k++) { if (!lens[k]) continue; const u8 *s = mem_ptr(c->mem, sd_field(c, sd, k), lens[k], 0); u8 *d = mem_ptr(c->mem, out + off, lens[k], 1); if (s && d) e_memcpy(d, s, lens[k]); wr32(c, out + 4 + 4 * (u64)k, off); off += lens[k]; } wr32(c, ARG(2), need); RET(1); }
static void s_MakeAbsoluteSD(Cpu *c) { /* (self, abs, *absLen, dacl, *daclLen, sacl, *saclLen, owner, *ownerLen, group, *groupLen) */ u64 sd = ARG(0), P = PSZ_OF(c); u32 abs_need = P == 8 ? 40 : 20; u32 lens[4]; for (int k = 0; k < 4; k++) { u64 f = sd_field(c, sd, k); lens[k] = !f ? 0 : k < 2 ? acl_sid_len(c, f) : rd16(c, f + 2); }
  u64 outs[4] = { ARG(7), ARG(9), ARG(5), ARG(3) }; u64 lenp[4] = { ARG(8), ARG(10), ARG(6), ARG(4) }; int small = rd32(c, ARG(2)) < abs_need || !ARG(1); for (int k = 0; k < 4; k++) if (lenp[k] && rd32(c, lenp[k]) < lens[k]) small = 1;
  if (small) { wr32(c, ARG(2), abs_need); for (int k = 0; k < 4; k++) if (lenp[k]) wr32(c, lenp[k], lens[k]); W->last_error = 122; RET(0); return; }
  u8 *p = mem_ptr(c->mem, ARG(1), abs_need, 1); if (!p) { RET(0); return; } e_memset(p, 0, abs_need); wr8(c, ARG(1), 1); wr16(c, ARG(1) + 2, rd16(c, sd + 2) & ~SD_CTRL_SELF_RELATIVE);
  for (int k = 0; k < 4; k++) { if (!lens[k] || !outs[k]) continue; const u8 *s = mem_ptr(c->mem, sd_field(c, sd, k), lens[k], 0); u8 *d = mem_ptr(c->mem, outs[k], lens[k], 1); if (s && d) e_memcpy(d, s, lens[k]); sd_set_field(c, ARG(1), k, outs[k]); } RET(1); }
/* access checks: the token of the one logged-on user (TOKEN_USER S-1-5-21-1000-2000-3000-500, Administrators, Users, Everyone, Authenticated
   Users, INTERACTIVE) evaluated against a DACL exactly like AccessCheck does - ACEs in order, DENY wins when it covers a still-wanted right,
   no DACL = everything, empty DACL = nothing, the owner always has READ_CONTROL | WRITE_DAC, MAXIMUM_ALLOWED collects every allowed right */
static int acl_token_sid(Cpu *c, u64 sid) {   /* does this SID belong to our token? */
  if (!sid) return 0; u8 n = rd8(c, sid + 1); if (rd8(c, sid) != 1 || n > 15) return 0; u8 auth = rd8(c, sid + 7); u32 sub[15]; for (u32 i = 0; i < n; i++) sub[i] = rd32(c, sid + 8 + 4 * i);
  if (auth == 1 && n == 1 && sub[0] == 0) return 1;                                     /* Everyone */
  if (auth != 5) return 0;
  if (n == 1 && (sub[0] == 11 || sub[0] == 4)) return 1;                                 /* Authenticated Users, INTERACTIVE */
  if (n == 2 && sub[0] == 32 && (sub[1] == 544 || sub[1] == 545)) return 1;              /* Administrators, Users */
  if (n == 5 && sub[0] == 21 && sub[1] == 1000 && sub[2] == 2000 && sub[3] == 3000 && sub[4] == 500) return 1;   /* the user */
  return 0;
}
static void s_MapGenericMask(Cpu *c) { /* (DWORD *mask, GENERIC_MAPPING *map) */ u64 pm = ARG(0), gm = ARG(1); if (!pm || !gm) return; u32 m = rd32(c, pm);
  if (m & 0x80000000u) m |= rd32(c, gm); if (m & 0x40000000u) m |= rd32(c, gm + 4); if (m & 0x20000000u) m |= rd32(c, gm + 8); if (m & 0x10000000u) m |= rd32(c, gm + 12);
  wr32(c, pm, m & 0x0fffffffu); }
static void s_AccessCheck(Cpu *c) { /* (sd, token, desired, mapping, privset, *privlen, *granted, *status) */
  u64 sd = ARG(0), gm = ARG(3), ps = ARG(4), ppl = ARG(5), pg = ARG(6), pst = ARG(7); u32 desired = (u32)ARG(2);
  if (!sd || !pg || !pst || !ppl) { W->last_error = 87; RET(0); return; }
  if (rd32(c, ppl) < 20) { wr32(c, ppl, 20); W->last_error = 122; RET(0); return; }   /* PRIVILEGE_SET with one LUID_AND_ATTRIBUTES */
  if (ps) { wr32(c, ps, 0); wr32(c, ps + 4, 0); } wr32(c, ppl, 20);
  if (gm) { if (desired & 0x80000000u) desired |= rd32(c, gm); if (desired & 0x40000000u) desired |= rd32(c, gm + 4); if (desired & 0x20000000u) desired |= rd32(c, gm + 8); if (desired & 0x10000000u) desired |= rd32(c, gm + 12); desired &= 0x0fffffffu; }
  int maxallowed = (desired & 0x02000000u) != 0; u32 want = desired & ~0x02000000u, granted = 0, denied = 0; u16 ctrl = rd16(c, sd + 2); u64 dacl = (ctrl & SD_CTRL_DACL_PRESENT) ? sd_field(c, sd, 3) : 0;
  if (acl_token_sid(c, sd_field(c, sd, 0))) granted |= 0x00020000u | 0x00040000u;   /* the owner: READ_CONTROL | WRITE_DAC */
  if (!(ctrl & SD_CTRL_DACL_PRESENT) || !dacl) granted = 0x1fffffffu;   /* no DACL: unrestricted */
  else { u32 n = rd16(c, dacl + 4), off = 8;
    for (u32 i = 0; i < n; i++) { u8 type = rd8(c, dacl + off), flags = rd8(c, dacl + off + 1); u16 sz = rd16(c, dacl + off + 2); u32 mask = rd32(c, dacl + off + 4); u64 sid = dacl + off + 8;
      if (!(flags & 0x8) && (type == 0 || type == 1) && acl_token_sid(c, sid)) { if (type == 0) granted |= mask & ~denied; else denied |= mask & ~granted; }
      if (sz < 8) break; off += sz; } }
  u32 result = maxallowed ? granted : (want & granted) == want ? want : 0; int ok = maxallowed ? granted != 0 : result != 0 || want == 0;
  wr32(c, pg, ok ? result : 0); wr32(c, pst, ok ? 1 : 0); if (!ok) W->last_error = 5; RET(1); }
static void s_DuplicateTokenEx(Cpu *c) { /* (token, access, sa, impersonation level, type, *new) */ if (!ARG(5)) { W->last_error = 87; RET(0); return; } wrp(c, ARG(5), 0x7002 + (u32)ARG(4)); RET(1); }   /* a token handle of the same user */
static void s_DuplicateToken(Cpu *c) { if (!ARG(2)) { W->last_error = 87; RET(0); return; } wrp(c, ARG(2), 0x7003); RET(1); }
static void s_ImpersonateLoggedOnUser(Cpu *c) { RET(1); }
static void s_ImpersonateSelf(Cpu *c) { RET(1); }
/* LookupAccountSid / LookupAccountName: the accounts the token (GetTokenInformation) and the ACL helpers hand out. Anything else → ERROR_NONE_MAPPED. */
typedef struct { const char *name, *domain; u32 use; u8 nsub; u32 auth; u32 sub[5]; } AcctEnt;
static const AcctEnt g_accts[] = {
  { "user", "NEBULA", 1, 5, 5, { 21, 1000, 2000, 3000, 500 } }, { "NEBULA", "", 3, 4, 5, { 21, 1000, 2000, 3000 } },   /* SidTypeUser, SidTypeDomain */
  { "Administrators", "BUILTIN", 4, 2, 5, { 32, 544 } }, { "Users", "BUILTIN", 4, 2, 5, { 32, 545 } }, { "BUILTIN", "", 3, 1, 5, { 32 } },   /* SidTypeAlias */
  { "Everyone", "", 5, 1, 1, { 0 } }, { "Authenticated Users", "NT AUTHORITY", 5, 1, 5, { 11 } }, { "INTERACTIVE", "NT AUTHORITY", 5, 1, 5, { 4 } },   /* SidTypeWellKnownGroup */
  { "SYSTEM", "NT AUTHORITY", 5, 1, 5, { 18 } }, { "LOCAL SERVICE", "NT AUTHORITY", 5, 1, 5, { 19 } }, { "NETWORK SERVICE", "NT AUTHORITY", 5, 1, 5, { 20 } },
  { "High Mandatory Level", "Mandatory Label", 10, 1, 16, { 12288 } },   /* SidTypeLabel */
};
static int acct_of_sid(Cpu *c, u64 sid) { if (!sid) return -1; u8 n = rd8(c, sid + 1); u32 auth = rd8(c, sid + 7) | (u32)rd8(c, sid + 6) << 8 | (u32)rd8(c, sid + 5) << 16;
  for (size_t i = 0; i < sizeof g_accts / sizeof g_accts[0]; i++) { const AcctEnt *a = &g_accts[i]; if (a->nsub != n || a->auth != auth) continue; int ok = 1; for (int k = 0; k < n; k++) if (rd32(c, sid + 8 + 4 * k) != a->sub[k]) { ok = 0; break; } if (ok) return (int)i; } return -1; }
static int acct_put_str(Cpu *c, const char *s, u64 buf, u64 cch_p, int wide) {   /* → 1 ok, 0 too small (ERROR_INSUFFICIENT_BUFFER, *cch = needed) */
  u32 need = (u32)e_strlen(s) + 1, cap = cch_p ? rd32(c, cch_p) : 0; if (cch_p) wr32(c, cch_p, need); if (!buf || cap < need) return 0;   /* failure: *cch = required size including the NUL */
  if (wide) put_w(c, buf, s, cap); else put_a(c, buf, s, cap); if (cch_p) wr32(c, cch_p, need - 1); return 1; }   /* success: characters copied, without the NUL */
static void lookup_account_sid(Cpu *c, int wide) { /* (system, sid, name, *cchName, domain, *cchDomain, *use) */ int i = acct_of_sid(c, ARG(1)); if (i < 0) { W->last_error = 1332; RET(0); return; }
  int a = acct_put_str(c, g_accts[i].name, ARG(2), ARG(3), wide), b = acct_put_str(c, g_accts[i].domain, ARG(4), ARG(5), wide); if (ARG(6)) wr32(c, ARG(6), g_accts[i].use);
  if (!a || !b) { W->last_error = 122; RET(0); return; } RET(1); }
static void s_LookupAccountSidW(Cpu *c) { lookup_account_sid(c, 1); }
static void s_LookupAccountSidA(Cpu *c) { lookup_account_sid(c, 0); }
static void lookup_account_name(Cpu *c, int wide) { /* (system, name, sid, *cbSid, domain, *cchDomain, *use) */ char name[128]; if (wide) { u64 p = ARG(1); size_t o = 0; for (; o + 1 < sizeof name; o++) { u16 ch = rd16(c, p + o * 2); if (!ch) break; name[o] = ch < 128 ? (char)ch : '?'; } name[o] = 0; } else { const char *s = (const char *)mem_ptr(c->mem, ARG(1), 1, 0); size_t o = 0; for (; s && s[o] && o + 1 < sizeof name; o++) name[o] = s[o]; name[o] = 0; }
  const char *bare = name; for (size_t k = 0; name[k]; k++) if (name[k] == '\\') bare = name + k + 1;   /* DOMAIN\name */
  int i = -1; for (size_t k = 0; k < sizeof g_accts / sizeof g_accts[0]; k++) if (!e_stricmp(g_accts[k].name, bare)) { i = (int)k; break; } if (i < 0) { W->last_error = 1332; RET(0); return; }
  const AcctEnt *a = &g_accts[i]; u32 need = 8 + 4u * a->nsub, cap = ARG(3) ? rd32(c, ARG(3)) : 0; if (ARG(3)) wr32(c, ARG(3), need);
  int ok = ARG(2) && cap >= need; if (ok) { u64 s = ARG(2); wr8(c, s, 1); wr8(c, s + 1, a->nsub); for (int k = 0; k < 6; k++) wr8(c, s + 2 + k, k < 3 ? 0 : (u8)(a->auth >> (8 * (5 - k)))); for (int k = 0; k < a->nsub; k++) wr32(c, s + 8 + 4 * k, a->sub[k]); }
  int b = acct_put_str(c, a->domain, ARG(4), ARG(5), wide); if (ARG(6)) wr32(c, ARG(6), a->use); if (!ok || !b) { W->last_error = 122; RET(0); return; } RET(1); }
static void s_LookupAccountNameW(Cpu *c) { lookup_account_name(c, 1); }
static void s_LookupAccountNameA(Cpu *c) { lookup_account_name(c, 0); }
#define ACL_SHIMS \
  S(LookupAccountSidW), S(LookupAccountSidA), S(LookupAccountNameW), S(LookupAccountNameA), \
  S(MapGenericMask), S(AccessCheck), S(DuplicateTokenEx), S(DuplicateToken), S(ImpersonateLoggedOnUser), S(ImpersonateSelf), \
  S(InitializeSecurityDescriptor), S(SetSecurityDescriptorDacl), S(GetSecurityDescriptorDacl), S(SetSecurityDescriptorOwner), S(SetSecurityDescriptorGroup), S(GetSecurityDescriptorOwner), S(GetSecurityDescriptorGroup), S(GetSecurityDescriptorControl), S(IsValidSecurityDescriptor), S(GetSecurityDescriptorLength), \
  S(InitializeAcl), S(IsValidAcl), S(GetAclInformation), S(GetAce), S(AddAccessAllowedAce), S(AddAccessAllowedAceEx), S(AddAccessDeniedAce), S(AddAccessDeniedAceEx), S(AddAce), S(DeleteAce), \
  S(GetFileSecurityW), S(GetFileSecurityA), S(SetFileSecurityW), S(SetFileSecurityA), S(GetKernelObjectSecurity), S(SetKernelObjectSecurity), S(GetSecurityInfo), S(GetNamedSecurityInfoW), S(GetNamedSecurityInfoA), S(SetSecurityInfo), S(SetNamedSecurityInfoW), S(SetNamedSecurityInfoA), S(MakeSelfRelativeSD), S(MakeAbsoluteSD),
