/* aclcheck.c — what java.nio's WindowsSecurity.checkAccessMask / Files.isWritable do on Windows: the file's security
   descriptor, an impersonation token duplicated from the process token, generic rights mapped with MapGenericMask and
   AccessCheck over the DACL.  Then a hand-built descriptor: ALLOW read for Everyone, DENY write for the user — the result
   must be exactly what Windows computes (read granted, write refused, MAXIMUM_ALLOWED = read only).
   Build: zig cc -target x86_64-windows-gnu -O1 -o aclcheck64.exe aclcheck.c -ladvapi32  (and x86-windows-gnu). */
#include <windows.h>
#include <stdio.h>
#include <sddl.h>

static int fails;
#define CHECK(cond, what) do { if (cond) printf("  ok   %s\n", what); else { printf("  FAIL %s (err %lu)\n", what, GetLastError()); fails++; } } while (0)

static BOOL access_check(PSECURITY_DESCRIPTOR sd, HANDLE tok, DWORD desired, DWORD *granted) {
  GENERIC_MAPPING gm = { FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS };
  MapGenericMask(&desired, &gm);
  PRIVILEGE_SET ps; DWORD psl = sizeof ps; BOOL status = FALSE; *granted = 0;
  if (!AccessCheck(sd, tok, desired, &gm, &ps, &psl, granted, &status)) return FALSE;
  return status;
}

int main(void) {
  char path[MAX_PATH]; GetModuleFileNameA(NULL, path, MAX_PATH);
  /* 1. the file's own descriptor, like nio does it */
  DWORD need = 0; GetFileSecurityA(path, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, NULL, 0, &need);
  CHECK(need > 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "GetFileSecurity size query");
  PSECURITY_DESCRIPTOR sd = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, need);
  CHECK(GetFileSecurityA(path, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, sd, need, &need), "GetFileSecurity");
  HANDLE ptok = NULL, itok = NULL;
  CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &ptok), "OpenProcessToken");
  CHECK(DuplicateTokenEx(ptok, TOKEN_QUERY, NULL, SecurityImpersonation, TokenImpersonation, &itok) && itok, "DuplicateTokenEx -> impersonation token");
  DWORD granted = 0;
  CHECK(access_check(sd, itok, GENERIC_READ, &granted) && (granted & FILE_READ_DATA), "AccessCheck: the own exe is readable");
  CHECK(access_check(sd, itok, GENERIC_WRITE, &granted) && (granted & FILE_WRITE_DATA), "AccessCheck: and writable (own file, full control)");
  CHECK(access_check(sd, itok, GENERIC_EXECUTE, &granted) && (granted & FILE_EXECUTE), "AccessCheck: and executable");
  /* 2. a descriptor with a DENY ace, built with the public API */
  PSID everyone = NULL, user = NULL; DWORD ulen = 0;
  SID_IDENTIFIER_AUTHORITY world = SECURITY_WORLD_SID_AUTHORITY;
  CHECK(AllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &everyone), "Everyone SID");
  GetTokenInformation(ptok, TokenUser, NULL, 0, &ulen); TOKEN_USER *tu = (TOKEN_USER *)LocalAlloc(LPTR, ulen);
  CHECK(GetTokenInformation(ptok, TokenUser, tu, ulen, &ulen), "TokenUser"); user = tu->User.Sid;
  DWORD alen = sizeof(ACL) + 2 * (sizeof(ACCESS_ALLOWED_ACE) + 68); PACL acl = (PACL)LocalAlloc(LPTR, alen);
  CHECK(InitializeAcl(acl, alen, ACL_REVISION), "InitializeAcl");
  CHECK(AddAccessDeniedAce(acl, ACL_REVISION, FILE_WRITE_DATA, user), "DENY FILE_WRITE_DATA for the user");
  CHECK(AddAccessAllowedAce(acl, ACL_REVISION, FILE_GENERIC_READ | FILE_GENERIC_WRITE, everyone), "ALLOW read+write for Everyone (after the DENY, as Windows orders them)");
  SECURITY_DESCRIPTOR asd; CHECK(InitializeSecurityDescriptor(&asd, SECURITY_DESCRIPTOR_REVISION), "InitializeSecurityDescriptor");
  CHECK(SetSecurityDescriptorDacl(&asd, TRUE, acl, FALSE), "SetSecurityDescriptorDacl");
  CHECK(SetSecurityDescriptorOwner(&asd, everyone, FALSE), "owner = Everyone (so the owner rule does not interfere)");
  CHECK(access_check(&asd, itok, GENERIC_READ, &granted) && (granted & FILE_READ_DATA), "DACL: read allowed through Everyone");
  SetLastError(0);
  BOOL w = access_check(&asd, itok, GENERIC_WRITE, &granted);
  CHECK(!w && GetLastError() == ERROR_ACCESS_DENIED && granted == 0, "DACL: write refused by the DENY ace (ERROR_ACCESS_DENIED, granted 0)");
  CHECK(access_check(&asd, itok, MAXIMUM_ALLOWED, &granted) && (granted & FILE_READ_DATA) && !(granted & FILE_WRITE_DATA), "DACL: MAXIMUM_ALLOWED = read without write");
  /* 3. no DACL at all = unrestricted */
  SECURITY_DESCRIPTOR nsd; InitializeSecurityDescriptor(&nsd, SECURITY_DESCRIPTOR_REVISION); SetSecurityDescriptorOwner(&nsd, everyone, FALSE);
  CHECK(access_check(&nsd, itok, GENERIC_ALL, &granted) && (granted & FILE_ALL_ACCESS) == FILE_ALL_ACCESS, "no DACL: everything granted");
  /* 4. an empty DACL = nothing */
  PACL empty = (PACL)LocalAlloc(LPTR, sizeof(ACL)); InitializeAcl(empty, sizeof(ACL), ACL_REVISION); SetSecurityDescriptorDacl(&nsd, TRUE, empty, FALSE);
  SetLastError(0); CHECK(!access_check(&nsd, itok, GENERIC_READ, &granted) && granted == 0, "empty DACL: nothing granted");
  CloseHandle(itok); CloseHandle(ptok);
  printf(fails ? "ACLCHECK FAILED (%d)\n" : "ACLCHECK OK\n", fails);
  return fails ? 1 : 0;
}
