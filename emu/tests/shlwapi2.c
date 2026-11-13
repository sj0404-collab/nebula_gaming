/* shlwapi: URL escaping / canonicalisation / combination / parts, file URL ↔ path, relative paths, root helpers, compaction */
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  wchar_t b[1024]; DWORD n;
  n = 1024; UrlEscapeW(L"http://h/a b/c#d e", b, &n, 0); printf("escape=%ls n=%lu\n", b, (unsigned long)n);
  n = 1024; UrlEscapeW(L"a b/c?d e", b, &n, URL_ESCAPE_SPACES_ONLY); printf("spaces=%ls\n", b);
  n = 1024; UrlUnescapeW(L"a%20b%2Fc%zz", b, &n, 0); printf("unescape=%ls\n", b);
  wcscpy(b, L"x%41y"); UrlUnescapeW(b, 0, 0, URL_UNESCAPE_INPLACE); printf("inplace=%ls\n", b);
  n = 1024; HRESULT hr = UrlCreateFromPathW(L"C:\\My Dir\\f#1.txt", b, &n, 0); printf("fileurl=%ls hr=%lx\n", b, (unsigned long)hr);
  n = 1024; hr = PathCreateFromUrlA("file:///C:/My%20Dir/f.txt", (char *)b, &n, 0); printf("path=%s hr=%lx\n", (char *)b, (unsigned long)hr);
  n = 1024; UrlGetPartW(L"https://user:pw@host.example:8443/p/q?x=1#f", b, &n, URL_PART_HOSTNAME, 0); printf("host=%ls", b);
  n = 1024; UrlGetPartW(L"https://user:pw@host.example:8443/p/q?x=1#f", b, &n, URL_PART_PORT, 0); printf(" port=%ls", b);
  n = 1024; UrlGetPartW(L"https://user:pw@host.example:8443/p/q?x=1#f", b, &n, URL_PART_QUERY, 0); printf(" query=%ls", b);
  n = 1024; UrlGetPartW(L"https://user:pw@host.example:8443/p/q?x=1#f", b, &n, URL_PART_SCHEME, 0); printf(" scheme=%ls\n", b);
  n = 1024; UrlCanonicalizeW(L"HTTP://h/a/./b/../c\\d?q=1", b, &n, 0); printf("canon=%ls\n", b);
  n = 1024; UrlCombineW(L"http://h/dir/page.html?q", L"../img/x.png", b, &n, 0); printf("combine=%ls", b);
  n = 1024; UrlCombineW(L"http://h/dir/page.html", L"/root.css", b, &n, 0); printf(" abs=%ls", b);
  n = 1024; UrlCombineW(L"http://h/dir/page.html", L"#frag", b, &n, 0); printf(" frag=%ls\n", b);
  printf("is: url=%d file=%d dir=%d query=%d pathisurl=%d\n", UrlIsW(L"http://x", URLIS_URL), UrlIsW(L"file://x", URLIS_FILEURL), UrlIsW(L"http://x/d/", URLIS_DIRECTORY), UrlIsW(L"http://x?a", URLIS_HASQUERY), PathIsURLW(L"C:\\x"));
  wcscpy(b, L"C:\\dir\\file.txt"); PathRenameExtensionW(b, L".bak"); printf("rename=%ls", b);
  wcscpy(b, L"C:\\dir\\sub\\x"); PathStripToRootW(b); printf(" root=%ls skip=%ls isroot=%d/%d", b, PathSkipRootW(L"\\\\srv\\share\\d"), PathIsRootW(L"C:\\"), PathIsRootW(L"C:\\x"));
  PathRelativePathToW(b, L"C:\\a\\b\\c.txt", 0, L"C:\\a\\d\\e.txt", 0); printf(" rel=%ls", b);
  PathCommonPrefixW(L"C:\\a\\b\\c", L"C:\\a\\bx\\d", b); printf(" common=%ls drive=%d", b, PathGetDriveNumberW(L"d:\\x"));
  wcscpy(b, L"  x y  "); PathRemoveBlanksW(b); printf(" blanks=[%ls]", b);
  wcscpy(b, L"C:\\a b\\c"); PathQuoteSpacesW(b); printf(" quote=%ls", b); PathUnquoteSpacesW(b); printf(" unq=%ls", b);
  wcscpy(b, L"C:\\d\\prog"); PathAddExtensionW(b, L".exe"); printf(" addext=%ls\n", b);
  PathCompactPathExW(b, L"C:\\very\\long\\directory\\name\\file.txt", 20, 0); printf("compact=%ls prefix=%d samerout=%d filespec=%d/%d lfn=%d/%d next=%ls\n", b, PathIsPrefixW(L"C:\\a", L"C:\\a\\b"), PathIsSameRootW(L"C:\\a", L"c:\\b"), PathIsFileSpecW(L"x.txt"), PathIsFileSpecW(L"d\\x.txt"), PathIsLFNFileSpecW(L"longfilename.txt"), PathIsLFNFileSpecW(L"SHORT.TXT"), PathFindNextComponentW(L"a\\b\\c"));
  wcscpy(b, L"\"C:\\x\\y.dll\",-7"); int idx = PathParseIconLocationW(b); printf("icon=%ls idx=%d", b, idx);
  wchar_t out[260]; printf(" sq=%d pretty=%d\n", PathSearchAndQualifyW(L"notepad.exe", out, 260) ? 1 : 0, PathMakePrettyW(wcscpy(b, L"C:\\ABC\\DEF")));
  printf("%ls\n", b);
  return 0;
}
