/* Synthetic MSVC x86 RTTI: validates __RTDynamicCast's real cdecl and metadata format. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef void *(__cdecl *cast_fn)(void *, long, void *, void *, int);
typedef struct { uint32_t vftable, spare; char name[16]; } Type;
typedef struct { uint32_t type, contained; int32_t mdisp, pdisp, vdisp; uint32_t flags; } Base;
typedef struct { uint32_t signature, attrs, count, array; } Hier;
typedef struct { uint32_t signature, offset, cd, type, hierarchy; } Locator;
static uint32_t p(void *v) { return (uint32_t)(uintptr_t)v; }
int main(void) {
  HMODULE m = LoadLibraryA("vcruntime140.dll");
  cast_fn cast = m ? (cast_fn)GetProcAddress(m, "__RTDynamicCast") : NULL;
  if (!cast || sizeof(void *) != 4) { puts("rtti-single: missing FAIL"); return 1; }
  Type derived = {0,0,".?AVDerived@@"}, base = {0,0,".?AVBase@@"}, other = {0,0,".?AVOther@@"};
  Type clone = {0,0,".?AVDerived@@"};
  Base ds = {p(&derived),1,0,-1,0,0}, bs = {p(&base),0,0,-1,0,0};
  uint32_t list[] = {p(&ds),p(&bs)};
  Hier h = {0,0,2,p(list)}; Locator col = {0,0,0,p(&derived),p(&h)};
  uint32_t table[] = {p(&col),0}; uint32_t obj[] = {p(&table[1])};
  if (cast(NULL,0,&base,&derived,0) != NULL) return 2;
  if (cast(obj,0,&base,&derived,0) != obj) return 3;
  if (cast(obj,0,&base,&clone,0) != obj) return 4;
  if (cast(obj,0,&derived,&base,0) != obj) return 5;
  if (cast(obj,0,&base,&other,0) != NULL) return 6;
  puts("rtti-single: PASS"); return 0;
}
