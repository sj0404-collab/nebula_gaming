/* font.c — GDI text engine checks: proportional metrics, extents, partial widths, char widths, face names, font enumeration,
 * DrawText measurement/ellipsis, monospace vs sans, bold, Cyrillic.  Prints "FONT OK n/n" (or FAILED). */
#include <windows.h>
#include <stdio.h>
#include <string.h>
static int g_ok, g_n;
static void chk(int ok, const char *what) { g_n++; if (ok) g_ok++; else printf("FAIL %s\n", what); }
static int g_fam, g_cour, g_last_type; static wchar_t g_first[64];
static int CALLBACK famproc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp) { (void)tm; g_fam++; g_last_type = (int)type; if (!g_first[0]) lstrcpyW(g_first, lf->lfFaceName); if (lp == 77 && !lstrcmpiW(lf->lfFaceName, L"Courier New")) g_cour++; return lp == 5 ? 0 : 1; }
static int CALLBACK famproc_a(const LOGFONTA *lf, const TEXTMETRICA *tm, DWORD type, LPARAM lp) { (void)tm; (void)type; (void)lp; return lf->lfFaceName[0] ? 1 : 0; }
static int tw(HDC dc, HFONT f, const wchar_t *s) { HGDIOBJ o = SelectObject(dc, f); SIZE sz = { 0, 0 }; GetTextExtentPoint32W(dc, s, lstrlenW(s), &sz); SelectObject(dc, o); return sz.cx; }
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  if (m == WM_PAINT) { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc); FillRect(dc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
    static const struct { const wchar_t *face; int h; int w; int it; const wchar_t *txt; } rows[] = { { L"Segoe UI", -12, 400, 0, L"Segoe UI 9pt: The quick brown fox — Съешь же ещё этих мягких французских булок" }, { L"Segoe UI", -12, 700, 0, L"Bold: Файл Правка Вид Справка 0123456789" }, { L"Segoe UI", -12, 400, 1, L"Italic synthesised from the upright outlines" }, { L"MS Shell Dlg", -11, 400, 0, L"MS Shell Dlg 8pt dialog text (condensed): Find All in All Opened Documents" }, { L"Courier New", -13, 400, 0, L"Courier New 10pt: int main(void) { return 0; } // моноширинный" }, { L"Consolas", -14, 700, 0, L"Consolas bold 0O 1lI {}[]" }, { L"Segoe UI", -20, 400, 0, L"20 px heading — Заголовок" }, { L"Tahoma", -32, 400, 0, L"Large 32 px Tahoma" } };
    int y = 8; for (int i = 0; i < 8; i++) { HFONT f = CreateFontW(rows[i].h, 0, 0, 0, rows[i].w, rows[i].it, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, rows[i].face); HGDIOBJ o = SelectObject(dc, f); SetBkMode(dc, TRANSPARENT); TextOutW(dc, 8, y, rows[i].txt, lstrlenW(rows[i].txt)); TEXTMETRICW tm; GetTextMetricsW(dc, &tm); y += tm.tmHeight + 6; SelectObject(dc, o); DeleteObject(f); }
    { HFONT f = CreateFontW(-12, 0, 0, 0, 400, 0, 1, 0, 0, 0, 0, 0, 0, L"Segoe UI"); HGDIOBJ o = SelectObject(dc, f); SetTextColor(dc, RGB(0, 102, 204)); TextOutW(dc, 8, y, L"Underlined link text", 20); SelectObject(dc, o); DeleteObject(f); y += 20; }
    { RECT r = { 8, y, 208, y + 20 }; SetTextColor(dc, 0); Rectangle(dc, 7, y - 1, 209, y + 21); DrawTextW(dc, L"A very long caption that gets an end ellipsis", -1, &r, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER); }
    EndPaint(h, &ps); return 0; }
  if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcW(h, m, wp, lp); }
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) { (void)hp; (void)cmd; (void)show;
  HDC dc = GetDC(NULL);
  HFONT ui = CreateFontW(-12, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI"), uib = CreateFontW(-12, 0, 0, 0, 700, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI"), mono = CreateFontW(-13, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, FIXED_PITCH, L"Courier New");
  chk(ui && uib && mono, "CreateFontW returns fonts");
  { HGDIOBJ o = SelectObject(dc, ui); TEXTMETRICW tm; memset(&tm, 0, sizeof tm); chk(GetTextMetricsW(dc, &tm), "GetTextMetricsW"); chk(tm.tmHeight == tm.tmAscent + tm.tmDescent && tm.tmAscent >= 10 && tm.tmAscent <= 14 && tm.tmDescent >= 2 && tm.tmDescent <= 5, "12 px sans: ascent 10..14, descent 2..5, height = sum");
    chk(tm.tmAveCharWidth >= 4 && tm.tmAveCharWidth <= 8 && tm.tmMaxCharWidth > tm.tmAveCharWidth, "tmAveCharWidth 4..8 < tmMaxCharWidth"); chk(tm.tmWeight == 400 && !tm.tmItalic && tm.tmCharSet == ANSI_CHARSET, "weight / italic / charset"); chk((tm.tmPitchAndFamily & TMPF_TRUETYPE) && (tm.tmPitchAndFamily & TMPF_FIXED_PITCH), "TrueType, variable pitch flags (TMPF_FIXED_PITCH set = variable)");
    wchar_t face[64] = L""; chk(GetTextFaceW(dc, 64, face) > 0 && !lstrcmpW(face, L"Segoe UI"), "GetTextFaceW returns the selected face"); SelectObject(dc, o); }
  int wi = tw(dc, ui, L"iiii"), wW = tw(dc, ui, L"WWWW"), wm_i = tw(dc, mono, L"iiii"), wm_W = tw(dc, mono, L"WWWW");
  chk(wi > 0 && wW > wi * 2, "sans: WWWW is more than twice as wide as iiii (proportional)"); chk(wm_i == wm_W && wm_i == 4 * 8, "Courier New 10 pt: every character 8 px wide");
  chk(tw(dc, uib, L"Bold text") >= tw(dc, ui, L"Bold text"), "bold face is at least as wide as regular"); chk(tw(dc, ui, L"Съешь же ещё") > tw(dc, ui, L"Съешь"), "Cyrillic text measures");
  { HGDIOBJ o = SelectObject(dc, ui); const wchar_t *s = L"Hello wide world"; int n = lstrlenW(s); int dx[32]; int fit = -1; SIZE sz; chk(GetTextExtentExPointW(dc, s, n, 40, &fit, dx, &sz), "GetTextExtentExPointW"); int mono_ok = 1; for (int k = 1; k < n; k++) if (dx[k] < dx[k - 1]) mono_ok = 0; chk(mono_ok && dx[n - 1] == sz.cx, "partial widths monotonic, last = total"); chk(fit > 2 && fit < n && dx[fit - 1] <= 40 && dx[fit] > 40, "nFit = characters within 40 px");
    SIZE s2; GetTextExtentPoint32W(dc, s, n, &s2); chk(s2.cx == sz.cx, "GetTextExtentPoint32 agrees with ExPoint"); int cw[3]; chk(GetCharWidth32W(dc, 'i', 'k', cw) && cw[0] < cw[2] && cw[0] > 0, "GetCharWidth32W: i narrower than k"); ABC abc[1]; chk(GetCharABCWidthsW(dc, 'W', 'W', abc) && abc[0].abcB > cw[0], "GetCharABCWidthsW");
    RECT r = { 0, 0, 0, 0 }; int h = DrawTextW(dc, s, n, &r, DT_CALCRECT | DT_SINGLELINE); chk(h > 0 && r.right == sz.cx && r.bottom == h, "DT_CALCRECT width = text extent"); RECT r2 = { 0, 0, 60, 0 }; DrawTextW(dc, L"one two three four five six seven", -1, &r2, DT_CALCRECT | DT_WORDBREAK); chk(r2.bottom >= 3 * h, "DT_WORDBREAK wraps at 60 px into 3+ lines"); SelectObject(dc, o); }
  { LOGFONTW lf; memset(&lf, 0, sizeof lf); chk(GetObjectW(ui, sizeof lf, &lf) == sizeof lf && lf.lfHeight == -12 && lf.lfWeight == 400 && !lstrcmpW(lf.lfFaceName, L"Segoe UI"), "GetObjectW(LOGFONTW) round-trips height / weight / face"); lf.lfWeight = 700; lf.lfItalic = 1; HFONT f2 = CreateFontIndirectW(&lf); LOGFONTW lf2; GetObjectW(f2, sizeof lf2, &lf2); chk(lf2.lfWeight == 700 && lf2.lfItalic == 1, "CreateFontIndirectW keeps weight/italic"); DeleteObject(f2); }
  { LOGFONTW lf; memset(&lf, 0, sizeof lf); lf.lfCharSet = DEFAULT_CHARSET; g_fam = 0; g_first[0] = 0; int r = EnumFontFamiliesExW(dc, &lf, famproc, 1, 0); chk(r == 1 && g_fam >= 8 && g_first[0] && g_last_type == TRUETYPE_FONTTYPE, "EnumFontFamiliesExW enumerates 8+ families, TRUETYPE_FONTTYPE"); g_fam = 0; EnumFontFamiliesExW(dc, &lf, famproc, 5, 0); chk(g_fam == 1, "callback returning 0 stops the enumeration");
    lstrcpyW(lf.lfFaceName, L"Courier New"); g_fam = 0; g_cour = 0; EnumFontFamiliesExW(dc, &lf, famproc, 77, 0); chk(g_fam == 1 && g_cour == 1, "face filter enumerates only Courier New"); chk(EnumFontFamiliesA(dc, NULL, famproc_a, 0) == 1, "EnumFontFamiliesA"); }
  { HGDIOBJ o = SelectObject(dc, mono); TEXTMETRICW tm; GetTextMetricsW(dc, &tm); chk(tm.tmAveCharWidth == 8 && tm.tmHeight >= 14 && tm.tmHeight <= 17 && !(tm.tmPitchAndFamily & TMPF_FIXED_PITCH), "Courier New 10 pt: tmAveCharWidth 8, height 14..17, fixed pitch"); SelectObject(dc, o); }
  ReleaseDC(NULL, dc);
  WNDCLASSW wc; memset(&wc, 0, sizeof wc); wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = L"FontTest"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassW(&wc);
  HWND h = CreateWindowExW(0, L"FontTest", L"Font engine", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 10, 10, 640, 300, NULL, NULL, hi, NULL); UpdateWindow(h);
  printf(g_ok == g_n ? "FONT OK %d/%d\n" : "FAILED %d/%d\n", g_ok, g_n); fflush(stdout);
  MSG msg; DWORD t0 = GetTickCount(); while (GetTickCount() - t0 < 1500 && PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); if (msg.message == WM_QUIT) break; }
  { MSG m2; while (GetTickCount() - t0 < 1200) { if (PeekMessageW(&m2, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m2); DispatchMessageW(&m2); } else Sleep(10); } }
  DestroyWindow(h); return g_ok == g_n ? 0 : 1; }
