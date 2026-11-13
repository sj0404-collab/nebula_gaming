/* shlwapi path and URL helpers that were still missing (both A and W: strings are read into UTF-8, processed, written
   back with the caller's width). Semantics follow the Windows documentation / Wine behaviour for the common cases:
   file URLs, percent escaping, root handling, relative paths. */
static int sh_is_sep(char ch) { return ch == '\\' || ch == '/'; }
static size_t sh_root_len(const char *p) {   /* "C:\" → 3, "\\server\share\" → through the share, "\" → 1, else 0 */
  if (p[0] && p[1] == ':') return sh_is_sep(p[2]) ? 3 : 2;
  if (p[0] == '\\' && p[1] == '\\') { size_t i = 2; int parts = 0; while (p[i]) { if (p[i] == '\\') { parts++; if (parts == 2) return i + 1; } i++; } return i; }
  if (p[0] == '\\') return 1;
  return 0; }
static int sh_has_scheme(const char *s, size_t *len) {   /* "scheme:" with ≥ 2 leading alnum/+-. characters */
  size_t i = 0; while (s[i] && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '+' || s[i] == '-' || s[i] == '.')) i++;
  if (i >= 2 && s[i] == ':') { if (len) *len = i; return 1; } return 0; }
/* out/cch protocol shared by the Url* functions: *pcch is the buffer size in characters; too small → E_POINTER with the needed size */
static void sh_url_out(Cpu *c, int wide, u64 dst, u64 pcch, const char *res) {
  u32 cap = pcch ? rd32(c, pcch) : 0; size_t need = 0; for (const char *q = res; *q; q++) if (!wide || ((u8)*q & 0xc0) != 0x80) need++;
  if (!dst || !pcch || need + 1 > cap) { if (pcch) wr32(c, pcch, (u32)need + 1); RET(0x80004003u); return; }
  path_str_out(c, dst, res, (u32)need + 1, wide); wr32(c, pcch, (u32)need); RET(0); }
static int sh_unreserved(u8 ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '*' || ch == '!' || ch == '(' || ch == ')' || ch == '\''; }
static void sh_escape(const char *in, char *out, size_t cap, u32 flags) {   /* URL_ESCAPE_SPACES_ONLY 0x04000000, URL_ESCAPE_PERCENT 0x1000, URL_ESCAPE_SEGMENT_ONLY 0x2000 */
  static const char hex[] = "0123456789ABCDEF"; size_t o = 0; int seg = (flags & 0x2000) != 0, spaces = (flags & 0x04000000u) != 0, pct = (flags & 0x1000) != 0; int in_query = 0;
  for (const u8 *p = (const u8 *)in; *p && o + 4 < cap; p++) { u8 ch = *p; int esc;
    if (!seg && !spaces && (ch == '?' || ch == '#')) in_query = 1;
    if (spaces) esc = ch == ' ';
    else if (in_query) esc = ch == ' ' || ch < 0x21 || ch > 0x7e || (ch == '%' && pct) || ch == '"' || ch == '<' || ch == '>' || ch == '^' || ch == '`' || ch == '{' || ch == '|' || ch == '}';
    else esc = !(sh_unreserved(ch) || (!seg && (ch == '/' || ch == ':' || ch == '@' || ch == '=' || ch == '&' || ch == ';' || ch == '$' || ch == ',' || ch == '+' || ch == '?' || ch == '#' || ch == '[' || ch == ']')) || (ch == '%' && !pct) || ch == '\\' );
    if (ch == '\\' && !seg) { out[o++] = '/'; continue; }
    if (esc) { out[o++] = '%'; out[o++] = hex[ch >> 4]; out[o++] = hex[ch & 15]; } else out[o++] = (char)ch; }
  out[o] = 0; }
static int sh_hex(int ch) { return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1; }
static void sh_unescape(const char *in, char *out, size_t cap, u32 flags) {   /* URL_DONT_UNESCAPE_EXTRA_INFO 0x02000000: stop decoding at ? or # */
  size_t o = 0; int stop = 0; for (const char *p = in; *p && o + 1 < cap; p++) {
    if ((flags & 0x02000000u) && (*p == '?' || *p == '#')) stop = 1;
    if (!stop && *p == '%' && sh_hex(p[1]) >= 0 && sh_hex(p[2]) >= 0) { out[o++] = (char)(sh_hex(p[1]) * 16 + sh_hex(p[2])); p += 2; } else out[o++] = *p; }
  out[o] = 0; }
#define SH_IN(var, idx) char var[2100]; pm_in(c, ARG(idx), wide, var, sizeof var)
static void sh_PathRenameExtension(Cpu *c, int wide) { SH_IN(p, 0); SH_IN(ext, 1); size_t n = e_strlen(p), cut = n; for (size_t i = n; i-- > 0;) { if (p[i] == '.') { cut = i; break; } if (sh_is_sep(p[i]) || p[i] == ':') break; }
  if (cut + e_strlen(ext) >= 260) { RET(0); return; } e_snprintf(p + cut, sizeof p - cut, "%s", ext); path_str_out(c, ARG(0), p, 260, wide); RET(1); }
static void sh_PathStripToRoot(Cpu *c, int wide) { SH_IN(p, 0); size_t r = sh_root_len(p); if (!r) { RET(0); return; } p[r] = 0; if (r == 2) { p[2] = '\\'; p[3] = 0; } path_str_out(c, ARG(0), p, 260, wide); RET(1); }
static void sh_PathSkipRoot(Cpu *c, int wide) { SH_IN(p, 0); size_t r = sh_root_len(p); if (!r || (r == 2)) { RET(0); return; } u64 a = ARG(0); if (wide) { size_t ch = 0; for (size_t i = 0; i < r; i++) if (((u8)p[i] & 0xc0) != 0x80) ch++; RET(a + ch * 2); } else RET(a + r); }
static void sh_PathIsRoot(Cpu *c, int wide) { SH_IN(p, 0); size_t n = e_strlen(p), r = sh_root_len(p); RET(n > 0 && r == n && !(p[0] && p[1] == ':' && n == 2)); }
static void sh_PathIsURL(Cpu *c, int wide) { SH_IN(p, 0); RET(sh_has_scheme(p, 0)); }
static void sh_UrlIs(Cpu *c, int wide) { SH_IN(u, 0); u32 what = (u32)ARG(1); size_t sl = 0; int url = sh_has_scheme(u, &sl);
  switch (what) { case 0: RET(url); return;                                                              /* URLIS_URL */
    case 1: RET(url && !(u[sl + 1] == '/' && u[sl + 2] == '/')); return;                                /* URLIS_OPAQUE */
    case 2: RET(url && (!e_strnicmp(u, "about:", 6) || !e_strnicmp(u, "javascript:", 11) || !e_strnicmp(u, "vbscript:", 9))); return;   /* URLIS_NOHISTORY */
    case 3: RET(!e_strnicmp(u, "file:", 5)); return;                                                     /* URLIS_FILEURL */
    case 4: RET(url); return;                                                                            /* URLIS_APPLIABLE */
    case 5: { size_t n = e_strlen(u); RET(n && (u[n - 1] == '/' || u[n - 1] == '\\')); return; }        /* URLIS_DIRECTORY */
    case 6: RET(e_strchr(u, '?') != 0); return;                                                          /* URLIS_HASQUERY */
    default: RET(0); return; } }
static void sh_UrlEscape(Cpu *c, int wide) { SH_IN(u, 0); char out[4200]; sh_escape(u, out, sizeof out, (u32)ARG(3)); sh_url_out(c, wide, ARG(1), ARG(2), out); }
static void sh_UrlUnescape(Cpu *c, int wide) { SH_IN(u, 0); u32 flags = (u32)ARG(3); char out[2100]; sh_unescape(u, out, sizeof out, flags);
  if (flags & 0x100000) { path_str_out(c, ARG(0), out, (u32)e_strlen(u) + 1, wide); RET(0); return; }   /* URL_UNESCAPE_INPLACE */
  sh_url_out(c, wide, ARG(1), ARG(2), out); }
static void sh_UrlCreateFromPath(Cpu *c, int wide) { SH_IN(p, 0); char url[4200]; size_t sl;
  if (sh_has_scheme(p, &sl) && !(p[1] == ':' && sl == 1)) { sh_url_out(c, wide, ARG(1), ARG(2), p); if (c->r[RAX] == 0) RET(1); return; }   /* already a URL: S_FALSE */
  char esc[4200]; { static const char hex[] = "0123456789ABCDEF"; size_t o = 0; for (const u8 *q = (const u8 *)p; *q && o + 4 < sizeof esc; q++) { u8 ch = *q; if (ch == '\\') esc[o++] = '/'; else if (sh_unreserved(ch) || ch == ':' || ch == '/') esc[o++] = (char)ch; else { esc[o++] = '%'; esc[o++] = hex[ch >> 4]; esc[o++] = hex[ch & 15]; } } esc[o] = 0; }   /* file URL: every unsafe byte escaped, drive colon kept */
  if (p[0] == '\\' && p[1] == '\\') e_snprintf(url, sizeof url, "file:%s", esc); else if (p[0] && p[1] == ':') e_snprintf(url, sizeof url, "file:///%s", esc); else e_snprintf(url, sizeof url, "file:%s", esc);
  sh_url_out(c, wide, ARG(1), ARG(2), url); }
static void sh_PathCreateFromUrl(Cpu *c, int wide) { SH_IN(u, 0); if (e_strnicmp(u, "file:", 5)) { RET(0x80070057u); return; } const char *p = u + 5; char raw[2100], path[2100];
  if (p[0] == '/' && p[1] == '/') { p += 2; if (*p == '/') { p++; e_snprintf(raw, sizeof raw, "%s", p); } else if (p[0] && p[1] == ':') e_snprintf(raw, sizeof raw, "%s", p); else e_snprintf(raw, sizeof raw, "\\\\%s", p); }
  else e_snprintf(raw, sizeof raw, "%s", p);
  sh_unescape(raw, path, sizeof path, 0); for (char *q = path; *q; q++) if (*q == '/') *q = '\\';
  if (path[0] == '\\' && path[1] != '\\' && path[2] == ':') { for (char *q = path; (q[0] = q[1]) != 0; q++) {} }   /* "/C:/x" → "C:\x" */
  sh_url_out(c, wide, ARG(1), ARG(2), path); }
static void sh_UrlGetPart(Cpu *c, int wide) { SH_IN(u, 0); u32 part = (u32)ARG(3); size_t sl; char out[2100] = ""; if (!sh_has_scheme(u, &sl)) { RET(0x80070057u); return; }
  const char *rest = u + sl + 1; const char *auth = 0; size_t alen = 0; if (rest[0] == '/' && rest[1] == '/') { auth = rest + 2; const char *e = auth; while (*e && *e != '/' && *e != '?' && *e != '#') e++; alen = (size_t)(e - auth); }
  const char *user = 0, *pass = 0, *host = auth; size_t ulen = 0, plen = 0, hlen = alen; if (auth) { for (size_t i = 0; i < alen; i++) if (auth[i] == '@') { user = auth; ulen = i; host = auth + i + 1; hlen = alen - i - 1; break; }
    if (user) for (size_t i = 0; i < ulen; i++) if (user[i] == ':') { pass = user + i + 1; plen = ulen - i - 1; ulen = i; break; } }
  const char *port = 0; size_t portlen = 0; if (host) for (size_t i = 0; i < hlen; i++) if (host[i] == ':') { port = host + i + 1; portlen = hlen - i - 1; hlen = i; break; }
  switch (part) { case 1: e_snprintf(out, sizeof out, "%.*s", (int)sl, u); break;   /* URL_PART_SCHEME */
    case 2: if (!host) { RET(0x80070057u); return; } e_snprintf(out, sizeof out, "%.*s", (int)hlen, host); break;
    case 3: if (!user) { RET(0x80070057u); return; } e_snprintf(out, sizeof out, "%.*s", (int)ulen, user); break;
    case 4: if (!pass) { RET(0x80070057u); return; } e_snprintf(out, sizeof out, "%.*s", (int)plen, pass); break;
    case 5: if (!port) { RET(0x80070057u); return; } e_snprintf(out, sizeof out, "%.*s", (int)portlen, port); break;
    case 6: { const char *q = e_strchr(u, '?'); if (!q) { RET(0x80070057u); return; } const char *h = e_strchr(q, '#'); e_snprintf(out, sizeof out, "%.*s", (int)(h ? h - q - 1 : (long)e_strlen(q + 1)), q + 1); break; }
    default: RET(0x80070057u); return; }
  if ((u32)ARG(4) & 0x1 && part == 2) {}   /* URL_PARTFLAG_KEEPSCHEME: scheme-qualified host is rarely wanted; plain host returned */
  sh_url_out(c, wide, ARG(1), ARG(2), out); }
/* canonical form: scheme lower-cased, "\" → "/", "." and ".." segments of the path resolved (URL_DONT_SIMPLIFY 0x08000000 keeps them), optional unescape / escape of unsafe characters */
static void sh_canon(const char *in, char *out, size_t cap, u32 flags) {
  char tmp[4200]; e_snprintf(tmp, sizeof tmp, "%s", in); size_t sl = 0; int url = sh_has_scheme(tmp, &sl); int file = url && !e_strnicmp(tmp, "file:", 5);
  for (size_t i = 0; i < sl; i++) if (tmp[i] >= 'A' && tmp[i] <= 'Z') tmp[i] += 32;
  if (flags & 0x10000000u) { char t2[4200]; sh_unescape(tmp, t2, sizeof t2, 0); e_snprintf(tmp, sizeof tmp, "%s", t2); }   /* URL_UNESCAPE */
  for (char *q = tmp; *q && *q != '?' && *q != '#'; q++) if (*q == '\\') *q = '/';
  if (!(flags & 0x08000000u)) {   /* simplify the path part (after the authority) */
    char *path = tmp + (url ? sl + 1 : 0); if (path[0] == '/' && path[1] == '/') { path += 2; while (*path && *path != '/' && *path != '?' && *path != '#') path++; }
    char *end = path; while (*end && *end != '?' && *end != '#') end++; char tail[2100]; e_snprintf(tail, sizeof tail, "%s", end); *end = 0;
    char seg[128][260]; int n = 0; int lead = path[0] == '/', trail = e_strlen(path) > 1 && path[e_strlen(path) - 1] == '/'; char *p = path;
    while (*p) { while (*p == '/') p++; if (!*p) break; char *e = p; while (*e && *e != '/') e++; size_t l = (size_t)(e - p); if (l == 1 && p[0] == '.') { if (!*e) trail = 1; p = e; continue; } if (l == 2 && p[0] == '.' && p[1] == '.') { if (n) n--; if (!*e) trail = 1; p = e; continue; } if (n < 128) { e_snprintf(seg[n], 260, "%.*s", (int)(l < 259 ? l : 259), p); n++; } p = e; }
    size_t o = 0; if (lead) path[o++] = '/'; for (int k = 0; k < n; k++) { if (k) path[o++] = '/'; size_t l = e_strlen(seg[k]); e_memcpy(path + o, seg[k], l); o += l; } if (trail && n) path[o++] = '/'; path[o] = 0; e_snprintf(path + o, sizeof tmp - (size_t)(path + o - tmp), "%s", tail); }
  if (flags & 0x20000000u) sh_escape(tmp, out, cap, 0);   /* URL_ESCAPE_UNSAFE */
  else if (flags & 0x04000000u) sh_escape(tmp, out, cap, 0x04000000u);
  else e_snprintf(out, cap, "%s", tmp); (void)file; }
static void sh_UrlCanonicalize(Cpu *c, int wide) { SH_IN(u, 0); char out[4200]; sh_canon(u, out, sizeof out, (u32)ARG(3)); sh_url_out(c, wide, ARG(1), ARG(2), out); }
static void sh_UrlCombine(Cpu *c, int wide) { SH_IN(base, 0); SH_IN(rel, 1); u32 flags = (u32)ARG(4); char j[4200], out[4200]; size_t sl;
  if (sh_has_scheme(rel, &sl) || !base[0]) e_snprintf(j, sizeof j, "%s", rel);
  else if (rel[0] == '#' || rel[0] == '?') { char *cut = e_strchr(base, rel[0] == '#' ? '#' : '?'); if (cut) *cut = 0; if (rel[0] == '#') { char *q = e_strchr(base, '?'); (void)q; } e_snprintf(j, sizeof j, "%s%s", base, rel); }
  else { size_t bsl = 0; sh_has_scheme(base, &bsl); char *p = base + (bsl ? bsl + 1 : 0); char *authend = p; if (p[0] == '/' && p[1] == '/') { authend = p + 2; while (*authend && *authend != '/' && *authend != '?' && *authend != '#') authend++; }
    if (rel[0] == '/' || rel[0] == '\\') { if (rel[1] == '/' || rel[1] == '\\') e_snprintf(j, sizeof j, "%.*s%s", (int)(bsl ? bsl + 1 : 0), base, rel); else e_snprintf(j, sizeof j, "%.*s%s", (int)(authend - base), base, rel); }
    else { char *last = 0; for (char *q = authend; *q && *q != '?' && *q != '#'; q++) if (*q == '/' || *q == '\\') last = q; if (last) e_snprintf(j, sizeof j, "%.*s/%s", (int)(last - base), base, rel); else e_snprintf(j, sizeof j, "%.*s/%s", (int)(authend - base), base, rel); } }
  sh_canon(j, out, sizeof out, flags); sh_url_out(c, wide, ARG(2), ARG(3), out); }
static void sh_PathRemoveBlanks(Cpu *c, int wide) { SH_IN(p, 0); char *s = p; while (*s == ' ') s++; size_t n = e_strlen(s); while (n && s[n - 1] == ' ') s[--n] = 0; path_str_out(c, ARG(0), s, (u32)e_strlen(p) + 1, wide); }
static void sh_PathQuoteSpaces(Cpu *c, int wide) { SH_IN(p, 0); if (!e_strchr(p, ' ') || p[0] == '"' || e_strlen(p) + 2 >= 260) { RET(0); return; } char q[2100]; e_snprintf(q, sizeof q, "\"%s\"", p); path_str_out(c, ARG(0), q, 260, wide); RET(1); }
static void sh_PathUnquoteSpaces(Cpu *c, int wide) { SH_IN(p, 0); size_t n = e_strlen(p); if (n >= 2 && p[0] == '"' && p[n - 1] == '"') { p[n - 1] = 0; path_str_out(c, ARG(0), p + 1, (u32)n + 1, wide); RET(1); return; } RET(0); }
static void sh_PathAddExtension(Cpu *c, int wide) { SH_IN(p, 0); SH_IN(ext, 1); if (!p[0]) { RET(0); return; } size_t n = e_strlen(p); for (size_t i = n; i-- > 0;) { if (p[i] == '.') { RET(0); return; } if (sh_is_sep(p[i])) break; }
  const char *e = ARG(1) ? ext : ".exe"; if (n + e_strlen(e) >= 260) { RET(0); return; } e_snprintf(p + n, sizeof p - n, "%s", e); path_str_out(c, ARG(0), p, 260, wide); RET(1); }
static int sh_prefix_eq(const char *a, const char *b, size_t n) { for (size_t i = 0; i < n; i++) { int x = a[i], y = b[i]; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x == '/') x = '\\'; if (y == '/') y = '\\'; if (x != y) return 0; } return 1; }
static void sh_PathIsPrefix(Cpu *c, int wide) { SH_IN(pre, 0); SH_IN(p, 1); size_t n = e_strlen(pre); while (n && sh_is_sep(pre[n - 1])) n--; RET(n && e_strlen(p) >= n && sh_prefix_eq(pre, p, n) && (p[n] == 0 || sh_is_sep(p[n]))); }
static void sh_PathCommonPrefix(Cpu *c, int wide) { SH_IN(a, 0); SH_IN(b, 1); size_t i = 0, last = 0; while (a[i] && b[i] && sh_prefix_eq(a + i, b + i, 1)) { i++; if (sh_is_sep(a[i - 1])) last = i - 1; }
  if (!a[i] && !b[i]) last = i; else if ((!a[i] && sh_is_sep(b[i])) || (!b[i] && sh_is_sep(a[i]))) last = i;
  if (last == 2 && a[1] == ':') last = 3;   /* keep the backslash of a drive root */
  char out[2100]; e_snprintf(out, sizeof out, "%.*s", (int)last, a); if (ARG(2)) path_str_out(c, ARG(2), out, 260, wide); RET(last); }
static void sh_PathRelativePathTo(Cpu *c, int wide) { SH_IN(from, 1); u32 fattr = (u32)ARG(2); SH_IN(to, 3); u32 tattr = (u32)ARG(4); char fd[2100], td[2100];
  e_snprintf(fd, sizeof fd, "%s", from); if (!(fattr & 0x10)) { char *l = 0; for (char *q = fd; *q; q++) if (sh_is_sep(*q)) l = q; if (l) *l = 0; else fd[0] = 0; }
  e_snprintf(td, sizeof td, "%s", to); char tfile[2100] = ""; if (!(tattr & 0x10)) { char *l = 0; for (char *q = td; *q; q++) if (sh_is_sep(*q)) l = q; if (l) { e_snprintf(tfile, sizeof tfile, "%s", l + 1); *l = 0; } else { e_snprintf(tfile, sizeof tfile, "%s", td); td[0] = 0; } }
  size_t rf = sh_root_len(fd), rt = sh_root_len(td); if (!rf || rf != rt || !sh_prefix_eq(fd, td, rf > 2 ? rf - 1 : rf)) { RET(0); return; }
  char fs[64][260], ts[64][260]; int nf = 0, nt = 0; for (char *p = fd + rf; *p;) { while (sh_is_sep(*p)) p++; if (!*p) break; char *e = p; while (*e && !sh_is_sep(*e)) e++; if (nf < 64) e_snprintf(fs[nf++], 260, "%.*s", (int)(e - p), p); p = e; }
  for (char *p = td + rt; *p;) { while (sh_is_sep(*p)) p++; if (!*p) break; char *e = p; while (*e && !sh_is_sep(*e)) e++; if (nt < 64) e_snprintf(ts[nt++], 260, "%.*s", (int)(e - p), p); p = e; }
  int k = 0; while (k < nf && k < nt && !e_stricmp(fs[k], ts[k])) k++;
  char out[4200] = "."; size_t o = 1; for (int i = k; i < nf; i++) { if (i == k) { o = 0; out[0] = 0; } o += (size_t)e_snprintf(out + o, sizeof out - o, "%s..", i == k ? "" : "\\"); }
  for (int i = k; i < nt; i++) o += (size_t)e_snprintf(out + o, sizeof out - o, "\\%s", ts[i]); if (tfile[0]) o += (size_t)e_snprintf(out + o, sizeof out - o, "\\%s", tfile);
  if (o >= 260) { RET(0); return; } path_str_out(c, ARG(0), out, 260, wide); RET(1); }
static void sh_PathGetDriveNumber(Cpu *c, int wide) { SH_IN(p, 0); int ch = p[0] >= 'a' && p[0] <= 'z' ? p[0] - 32 : p[0]; RET(ch >= 'A' && ch <= 'Z' && p[1] == ':' ? (u64)(ch - 'A') : (u64)(s64)-1); }
static void sh_PathBuildRoot(Cpu *c, int wide) { int d = (int)ARG(1); if (!ARG(0) || d < 0 || d > 25) { RET(0); return; } char r[4] = { (char)('A' + d), ':', '\\', 0 }; path_str_out(c, ARG(0), r, 4, wide); RET(ARG(0)); }
static void sh_PathIsSameRoot(Cpu *c, int wide) { SH_IN(a, 0); SH_IN(b, 1); size_t ra = sh_root_len(a), rb = sh_root_len(b); RET(ra && ra == rb && sh_prefix_eq(a, b, ra)); }
static void sh_PathIsFileSpec(Cpu *c, int wide) { SH_IN(p, 0); for (const char *q = p; *q; q++) if (sh_is_sep(*q) || *q == ':') { RET(0); return; } RET(1); }
static void sh_PathFindNextComponent(Cpu *c, int wide) { SH_IN(p, 0); if (!p[0]) { RET(0); return; } size_t i = 0; while (p[i] && !sh_is_sep(p[i])) i++; if (p[i]) i++; u64 a = ARG(0); if (wide) { size_t ch = 0; for (size_t k = 0; k < i; k++) if (((u8)p[k] & 0xc0) != 0x80) ch++; RET(a + ch * 2); } else RET(a + i); }
static void sh_PathParseIconLocation(Cpu *c, int wide) { SH_IN(p, 0); int idx = 0; char *comma = 0; for (char *q = p; *q; q++) if (*q == ',') comma = q; if (comma) { *comma = 0; const char *n = comma + 1; int neg = 0; if (*n == '-') { neg = 1; n++; } while (*n >= '0' && *n <= '9') idx = idx * 10 + (*n++ - '0'); if (neg) idx = -idx; }
  size_t n = e_strlen(p); if (n >= 2 && p[0] == '"' && p[n - 1] == '"') { p[n - 1] = 0; for (char *q = p; (q[0] = q[1]) != 0; q++) {} }
  path_str_out(c, ARG(0), p, 260, wide); RET((u64)(s64)idx); }
static void sh_PathSearchAndQualify(Cpu *c, int wide) { SH_IN(p, 0); u32 cap = (u32)ARG(2); char out[2100];
  if (sh_root_len(p) >= 3 || (p[0] == '\\' && p[1] == '\\')) e_snprintf(out, sizeof out, "%s", p);
  else if (p[0] && p[1] == ':') e_snprintf(out, sizeof out, "%s\\%s", vfs_cwd(), p + 2);
  else if (sh_is_sep(p[0])) e_snprintf(out, sizeof out, "%.2s%s", vfs_cwd(), p);
  else { const char *cwd = vfs_cwd(); e_snprintf(out, sizeof out, "%s%s%s", cwd, cwd[e_strlen(cwd) - 1] == '\\' ? "" : "\\", p);
    if (!e_strchr(p, '\\') && vfs_stat(out, 0) == 0) { static const char *const dirs[] = { "C:\\Windows\\System32", "C:\\Windows", 0 }; for (int i = 0; dirs[i]; i++) { char t[2100]; e_snprintf(t, sizeof t, "%s\\%s", dirs[i], p); if (vfs_stat(t, 0)) { e_snprintf(out, sizeof out, "%s", t); break; } } } }
  char canon[2100]; path_canon(out, canon, sizeof canon); if (e_strlen(canon) >= cap) { RET(0); return; } path_str_out(c, ARG(1), canon, cap, wide); RET(1); }
static void sh_PathMakePretty(Cpu *c, int wide) { SH_IN(p, 0); for (const char *q = p; *q; q++) if (*q >= 'a' && *q <= 'z') { RET(0); return; } int first = 1; for (char *q = p; *q; q++) { if (*q >= 'A' && *q <= 'Z' && !first) *q += 32; first = sh_is_sep(*q); } path_str_out(c, ARG(0), p, (u32)e_strlen(p) + 1, wide); RET(1); }
static void sh_PathFindOnPath(Cpu *c, int wide) { SH_IN(p, 0); if (vfs_stat(p, 0)) { RET(1); return; } if (e_strchr(p, '\\') || e_strchr(p, '/')) { RET(0); return; }
  char t[2100]; const char *cwd = vfs_cwd(); e_snprintf(t, sizeof t, "%s%s%s", cwd, cwd[e_strlen(cwd) - 1] == '\\' ? "" : "\\", p); if (vfs_stat(t, 0)) { path_str_out(c, ARG(0), t, 260, wide); RET(1); return; }
  u64 dirs = ARG(1); if (dirs) for (int i = 0; i < 64; i++) { u64 d = rdp(c, dirs + (u64)i * PSZ); if (!d) break; char dd[2100]; pm_in(c, d, wide, dd, sizeof dd); e_snprintf(t, sizeof t, "%s\\%s", dd, p); if (vfs_stat(t, 0)) { path_str_out(c, ARG(0), t, 260, wide); RET(1); return; } }
  static const char *const sys[] = { "C:\\Windows\\System32", "C:\\Windows", 0 }; for (int i = 0; sys[i]; i++) { e_snprintf(t, sizeof t, "%s\\%s", sys[i], p); if (vfs_stat(t, 0)) { path_str_out(c, ARG(0), t, 260, wide); RET(1); return; } }
  RET(0); }
static void sh_PathIsDirectoryEmpty(Cpu *c, int wide) { SH_IN(p, 0); RET(vfs_stat(p, 0) == 2 && !vfs_dir_nonempty(p)); }
static void sh_PathIsLFNFileSpec(Cpu *c, int wide) { SH_IN(p, 0); size_t n = e_strlen(p), dot = n; int dots = 0; for (size_t i = 0; i < n; i++) if (p[i] == '.') { dots++; dot = i; } else if (p[i] == ' ') { RET(1); return; }
  if (dots > 1) { RET(1); return; } size_t base = dot, ext = dot < n ? n - dot - 1 : 0; RET(base > 8 || ext > 3); }
static void sh_PathCompactPathEx(Cpu *c, int wide) { SH_IN(p, 1); u32 max = (u32)ARG(2); if (!ARG(0) || !max) { RET(0); return; } size_t n = e_strlen(p); char out[2100];
  if (n + 1 <= max) e_snprintf(out, sizeof out, "%s", p);
  else if (max <= 4) e_snprintf(out, sizeof out, "%.*s", (int)(max - 1), "...");
  else { const char *file = p; for (const char *q = p; *q; q++) if (sh_is_sep(*q)) file = q + 1; size_t fl = e_strlen(file); size_t room = max - 1;
    if (fl + 4 > room) e_snprintf(out, sizeof out, "...%.*s", (int)(room - 3), file + (fl - (room - 3)));   /* the file name alone does not fit: keep its tail */
    else e_snprintf(out, sizeof out, "%.*s...\\%s", (int)(room - fl - 4), p, file); }
  path_str_out(c, ARG(0), out, max, wide); RET(1); }
static void sh_PathCompactPath(Cpu *c, int wide) { SH_IN(p, 1); u32 dx = (u32)ARG(2); if (!ARG(1)) { RET(0); return; } int cw = 7; u32 maxch = dx / (u32)cw; if (maxch < 4) maxch = 4;   /* average character width of the UI font */
  size_t n = e_strlen(p); if (n <= maxch) { RET(1); return; } char out[2100]; const char *file = p; for (const char *q = p; *q; q++) if (sh_is_sep(*q)) file = q + 1; size_t fl = e_strlen(file);
  if (fl + 4 > maxch) e_snprintf(out, sizeof out, "%.*s...", (int)(maxch > 3 ? maxch - 3 : 1), p); else e_snprintf(out, sizeof out, "%.*s...\\%s", (int)(maxch - fl - 4), p, file);
  path_str_out(c, ARG(1), out, (u32)n + 1, wide); RET(1); }
#define SH_AW(n) static void s_##n##W(Cpu *c) { sh_##n(c, 1); } static void s_##n##A(Cpu *c) { sh_##n(c, 0); }
SH_AW(PathRenameExtension) SH_AW(PathStripToRoot) SH_AW(PathSkipRoot) SH_AW(PathIsRoot) SH_AW(PathIsURL) SH_AW(UrlIs) SH_AW(UrlEscape) SH_AW(UrlUnescape) SH_AW(UrlCreateFromPath)
static void s_PathCreateFromUrlA(Cpu *c) { sh_PathCreateFromUrl(c, 0); }   /* the W variant lives in win32_rt.inc */
SH_AW(UrlGetPart) SH_AW(UrlCanonicalize) SH_AW(UrlCombine) SH_AW(PathRemoveBlanks) SH_AW(PathQuoteSpaces) SH_AW(PathUnquoteSpaces) SH_AW(PathAddExtension) SH_AW(PathIsPrefix) SH_AW(PathCommonPrefix)
SH_AW(PathRelativePathTo) SH_AW(PathGetDriveNumber) SH_AW(PathBuildRoot) SH_AW(PathIsSameRoot) SH_AW(PathIsFileSpec) SH_AW(PathFindNextComponent) SH_AW(PathParseIconLocation) SH_AW(PathSearchAndQualify)
SH_AW(PathMakePretty) SH_AW(PathFindOnPath) SH_AW(PathIsDirectoryEmpty) SH_AW(PathIsLFNFileSpec) SH_AW(PathCompactPathEx) SH_AW(PathCompactPath)
#define SH_S(n) S(n##W), S(n##A),
#define SHLWAPI2_SHIMS \
  SH_S(PathRenameExtension) SH_S(PathStripToRoot) SH_S(PathSkipRoot) SH_S(PathIsRoot) SH_S(PathIsURL) SH_S(UrlIs) SH_S(UrlEscape) SH_S(UrlUnescape) SH_S(UrlCreateFromPath) SH_S(UrlGetPart) SH_S(UrlCanonicalize) SH_S(UrlCombine) \
  SH_S(PathRemoveBlanks) SH_S(PathQuoteSpaces) SH_S(PathUnquoteSpaces) SH_S(PathAddExtension) SH_S(PathIsPrefix) SH_S(PathCommonPrefix) SH_S(PathRelativePathTo) SH_S(PathGetDriveNumber) SH_S(PathBuildRoot) SH_S(PathIsSameRoot) \
  SH_S(PathIsFileSpec) SH_S(PathFindNextComponent) SH_S(PathParseIconLocation) SH_S(PathSearchAndQualify) SH_S(PathMakePretty) SH_S(PathFindOnPath) SH_S(PathIsDirectoryEmpty) SH_S(PathIsLFNFileSpec) SH_S(PathCompactPathEx) SH_S(PathCompactPath) \
  S(PathCreateFromUrlA),
