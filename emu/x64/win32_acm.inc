/* msacm32 (Audio Compression Manager) and wininet.
 *
 *  ACM: the built-in PCM converter only — what games use acmStreamOpen for when they resample/widen wave data before handing
 *  it to waveOut/DirectSound (8<->16 bit, mono<->stereo, any sample rate, linear interpolation). Compressed formats (ADPCM,
 *  MP3, GSM…) have no codec here: acmStreamOpen fails with ACMERR_NOTPOSSIBLE and acmFormatSuggest only suggests PCM, so a
 *  program sees "no codec installed" rather than silence. Structures are 1-byte packed (msacm.h uses pshpack1).
 *
 *  WinINet: the browser gives the emulator no raw sockets, so there is no HTTP stack to wrap. Handles open (InternetOpen,
 *  option setting) and every connection attempt fails with ERROR_INTERNET_CANNOT_CONNECT / "not connected" — the code path a
 *  Windows program takes when the machine is offline, which every launcher/update check already handles. */
#define H_ACM_BASE 0x6800ull
#define ACM_MAX 32
typedef struct { int used; u16 sch, dch, sbits, dbits; u32 srate, drate; double phase; s32 last[2]; } AcmStream;
static AcmStream g_acm[ACM_MAX];
static AcmStream *acm_of(u64 h) { if (h < H_ACM_BASE || h >= H_ACM_BASE + ACM_MAX) return 0; AcmStream *a = &g_acm[h - H_ACM_BASE]; return a->used ? a : 0; }
/* WAVEFORMATEX: wFormatTag@0 nChannels@2 nSamplesPerSec@4 nAvgBytesPerSec@8 nBlockAlign@12 wBitsPerSample@14 cbSize@16 */
static int acm_pcm_in(Cpu *c, u64 wfx, u16 *ch, u32 *rate, u16 *bits) {
  if (!wfx) return 0; u16 tag = rd16(c, wfx); *ch = rd16(c, wfx + 2); *rate = rd32(c, wfx + 4); *bits = rd16(c, wfx + 14);
  if (tag == 0xFFFE) { /* WAVE_FORMAT_EXTENSIBLE: SubFormat GUID first dword 1 = PCM */ if (rd16(c, wfx + 16) < 22 || rd32(c, wfx + 24) != 1) return 0; }
  else if (tag != 1) return 0;
  return (*ch == 1 || *ch == 2) && (*bits == 8 || *bits == 16) && *rate >= 1000 && *rate <= 384000; }
static void acm_pcm_out(Cpu *c, u64 wfx, u16 ch, u32 rate, u16 bits) {
  wr16(c, wfx, 1); wr16(c, wfx + 2, ch); wr32(c, wfx + 4, rate); wr16(c, wfx + 12, (u16)(ch * bits / 8)); wr32(c, wfx + 8, rate * ch * bits / 8); wr16(c, wfx + 14, bits); wr16(c, wfx + 16, 0); }
#define ACMERR_NOTPOSSIBLE 512
#define ACMERR_BUSY 513
#define ACMERR_UNPREPARED 514
#define ACMERR_CANCELED 515
#define MMSYSERR_INVALHANDLE 5
#define MMSYSERR_INVALPARAM 11
#define MMSYSERR_INVALFLAG 10
static u32 acm_dst_bytes(AcmStream *a, u32 src_bytes) {   /* worst-case output size for src_bytes of input (what acmStreamSize reports) */
  u64 frames = src_bytes / (a->sch * a->sbits / 8); u64 of = (frames * a->drate + a->srate - 1) / a->srate + 2; return (u32)(of * a->dch * a->dbits / 8); }
static u32 acm_src_bytes(AcmStream *a, u32 dst_bytes) {
  u64 frames = dst_bytes / (a->dch * a->dbits / 8); u64 sf = frames * a->srate / a->drate; if (sf < 1) sf = 1; return (u32)(sf * a->sch * a->sbits / 8); }
static void s_acmGetVersion(Cpu *c) { RET(0x04000000u); }   /* 4.0 build 0 */
static void s_acmMetrics(Cpu *c) { u32 m = (u32)ARG(1); u64 p = ARG(2); if (!p) { RET(MMSYSERR_INVALPARAM); return; }
  switch (m) { case 1: case 2: case 3: case 4: wr32(c, p, 1); break;            /* ACM_METRIC_COUNT_DRIVERS / CODECS / CONVERTERS / FILTERS: the PCM converter */
    case 5: wr32(c, p, 0); break;                                               /* COUNT_DISABLED */
    case 6: case 7: case 8: wr32(c, p, 1); break;                               /* COUNT_HARDWARE / LOCAL_DRIVERS… */
    case 50: wr32(c, p, 40); break;                                             /* ACM_METRIC_MAX_SIZE_FORMAT */
    case 51: wr32(c, p, 0); break;                                              /* MAX_SIZE_FILTER */
    case 100: wr32(c, p, 0x04000000u); break;                                   /* DRIVER_SUPPORT */
    case 101: wr32(c, p, 1); break;                                             /* DRIVER_PRIORITY */
    default: RET(MMSYSERR_INVALPARAM); return; }
  RET(0); }
static void s_acmDriverEnum(Cpu *c) { RET(0); }                                 /* no enumerable drivers (the PCM converter is internal) */
static void s_acmDriverOpen(Cpu *c) { if (ARG(0)) wrp(c, ARG(0), 0); RET(MMSYSERR_INVALHANDLE); }
static void s_acmDriverClose(Cpu *c) { RET(MMSYSERR_INVALHANDLE); }
static void s_acmFormatTagEnum(Cpu *c) { RET(0); }
static void s_acmFormatEnum(Cpu *c) { RET(0); }
static void s_acmFormatChoose(Cpu *c) { RET(ACMERR_CANCELED); }
static void s_acmFormatTagDetails(Cpu *c) { u64 p = ARG(1); u32 q = (u32)ARG(2); if (!p) { RET(MMSYSERR_INVALPARAM); return; }
  /* ACMFORMATTAGDETAILS: cbStruct@0 dwFormatTagIndex@4 dwFormatTag@8 cbFormatSize@12 fdwSupport@16 cStandardFormats@20 szFormatTag@24 (48 wchar) */
  u32 tag = q == 1 ? rd32(c, p + 8) : q == 0 ? (rd32(c, p + 4) == 0 ? 1 : 0) : 0;
  if (tag != 1) { RET(ACMERR_NOTPOSSIBLE); return; }
  wr32(c, p + 4, 0); wr32(c, p + 8, 1); wr32(c, p + 12, 16); wr32(c, p + 16, 1); wr32(c, p + 20, 24);
  static const char nm[] = "PCM"; for (int i = 0; i < 4; i++) wr16(c, p + 24 + i * 2, (u16)nm[i]); RET(0); }
static void s_acmFormatDetails(Cpu *c) { u64 p = ARG(1); u32 q = (u32)ARG(2); if (!p) { RET(MMSYSERR_INVALPARAM); return; }
  /* ACMFORMATDETAILS: cbStruct@0 dwFormatIndex@4 dwFormatTag@8 fdwSupport@12 pwfx@16 cbwfx@16+psz szFormat@20+psz (128 wchar) */
  u64 wfx = rdp(c, p + 16); u16 ch, bits; u32 rate; if (q != 1 || !acm_pcm_in(c, wfx, &ch, &rate, &bits)) { RET(ACMERR_NOTPOSSIBLE); return; }
  wr32(c, p + 8, 1); wr32(c, p + 12, 1); char d[96]; e_snprintf(d, sizeof d, "%u Hz, %u Bit, %s", (unsigned)rate, (unsigned)bits, ch == 2 ? "Stereo" : "Mono");
  u64 o = p + 20 + PSZ; for (int i = 0; d[i]; i++) wr16(c, o + i * 2, (u16)d[i]); wr16(c, o + e_strlen(d) * 2, 0); RET(0); }
static void s_acmFormatSuggest(Cpu *c) { u64 src = ARG(1), dst = ARG(2); u32 cb = (u32)ARG(3), f = (u32)ARG(4); u16 ch, bits; u32 rate;
  if (!src || !dst || cb < 16) { RET(MMSYSERR_INVALPARAM); return; }
  u16 stag = rd16(c, src); u16 sch = rd16(c, src + 2); u32 srate = rd32(c, src + 4); u16 sbits = rd16(c, src + 14);
  int src_pcm = acm_pcm_in(c, src, &ch, &rate, &bits);
  /* ACM_FORMATSUGGESTF_WFORMATTAG 0x10000, NCHANNELS 0x20000, NSAMPLESPERSEC 0x40000, WBITSPERSAMPLE 0x80000 — honour what the caller pinned */
  u16 dtag = (f & 0x10000) ? rd16(c, dst) : 1; if (dtag != 1) { RET(ACMERR_NOTPOSSIBLE); return; }
  u16 dch = (f & 0x20000) ? rd16(c, dst + 2) : (src_pcm ? sch : (sch == 2 ? 2 : 1));
  u32 drate = (f & 0x40000) ? rd32(c, dst + 4) : (srate ? srate : 44100);
  u16 dbits = (f & 0x80000) ? rd16(c, dst + 14) : 16;
  if ((dch != 1 && dch != 2) || (dbits != 8 && dbits != 16) || drate < 1000) { RET(ACMERR_NOTPOSSIBLE); return; }
  if (!src_pcm) { e_log("[x64] acmFormatSuggest: no codec for wFormatTag 0x%x — only PCM<->PCM conversion is available\n", (unsigned)stag); RET(ACMERR_NOTPOSSIBLE); return; }
  acm_pcm_out(c, dst, dch, drate, dbits); RET(0); }
static void s_acmStreamOpen(Cpu *c) { u64 ph = ARG(0), src = ARG(2), dst = ARG(3), flt = ARG(4); u32 f = (u32)ARG(7);
  u16 sch, sbits, dch, dbits; u32 srate, drate;
  if (ph) wrp(c, ph, 0);
  if (!src || !dst) { RET(MMSYSERR_INVALPARAM); return; }
  if (!acm_pcm_in(c, src, &sch, &srate, &sbits) || !acm_pcm_in(c, dst, &dch, &drate, &dbits) || flt) {
    e_log("[x64] acmStreamOpen: tags 0x%x -> 0x%x not convertible (only PCM<->PCM)\n", (unsigned)rd16(c, src), (unsigned)rd16(c, dst)); RET(ACMERR_NOTPOSSIBLE); return; }
  if (f & 1) { RET(0); return; }   /* ACM_STREAMOPENF_QUERY */
  if (!ph) { RET(MMSYSERR_INVALPARAM); return; }
  for (int i = 0; i < ACM_MAX; i++) if (!g_acm[i].used) { AcmStream *a = &g_acm[i]; e_memset(a, 0, sizeof *a); a->used = 1; a->sch = sch; a->dch = dch; a->sbits = sbits; a->dbits = dbits; a->srate = srate; a->drate = drate; wrp(c, ph, H_ACM_BASE + i); RET(0); return; }
  RET(ACMERR_BUSY); }
static void s_acmStreamClose(Cpu *c) { AcmStream *a = acm_of(ARG(0)); if (!a) { RET(MMSYSERR_INVALHANDLE); return; } a->used = 0; RET(0); }
static void s_acmStreamReset(Cpu *c) { AcmStream *a = acm_of(ARG(0)); if (!a) { RET(MMSYSERR_INVALHANDLE); return; } a->phase = 0; a->last[0] = a->last[1] = 0; RET(0); }
static void s_acmStreamSize(Cpu *c) { AcmStream *a = acm_of(ARG(0)); u32 cb = (u32)ARG(1); u64 out = ARG(2); u32 f = (u32)ARG(3);
  if (!a) { RET(MMSYSERR_INVALHANDLE); return; } if (!out) { RET(MMSYSERR_INVALPARAM); return; }
  if (f == 0) wr32(c, out, acm_dst_bytes(a, cb));            /* ACM_STREAMSIZEF_SOURCE */
  else if (f == 1) wr32(c, out, acm_src_bytes(a, cb));       /* ACM_STREAMSIZEF_DESTINATION */
  else { RET(MMSYSERR_INVALFLAG); return; }
  RET(0); }
/* ACMSTREAMHEADER: cbStruct@0 fdwStatus@4 dwUser@8 pbSrc@8+p cbSrcLength@8+2p cbSrcLengthUsed@12+2p dwSrcUser@16+2p pbDst@16+3p cbDstLength@16+4p cbDstLengthUsed@20+4p */
#define ASH_STATUS 4
#define ASH_SRC (8 + PSZ)
#define ASH_SRCLEN (8 + 2 * PSZ)
#define ASH_SRCUSED (12 + 2 * PSZ)
#define ASH_DST (16 + 3 * PSZ)
#define ASH_DSTLEN (16 + 4 * PSZ)
#define ASH_DSTUSED (20 + 4 * PSZ)
#define ACMSTREAMHEADER_STATUSF_PREPARED 0x20000
#define ACMSTREAMHEADER_STATUSF_INQUEUE 0x10000
#define ACMSTREAMHEADER_STATUSF_DONE 0x00010000
static void s_acmStreamPrepareHeader(Cpu *c) { AcmStream *a = acm_of(ARG(0)); u64 h = ARG(1); if (!a) { RET(MMSYSERR_INVALHANDLE); return; }
  if (!h || rd32(c, h) < (u32)(24 + 4 * PSZ) || !rdp(c, h + ASH_SRC) || !rdp(c, h + ASH_DST)) { RET(MMSYSERR_INVALPARAM); return; }
  wr32(c, h + ASH_STATUS, rd32(c, h + ASH_STATUS) | ACMSTREAMHEADER_STATUSF_PREPARED); RET(0); }
static void s_acmStreamUnprepareHeader(Cpu *c) { AcmStream *a = acm_of(ARG(0)); u64 h = ARG(1); if (!a) { RET(MMSYSERR_INVALHANDLE); return; }
  if (!h) { RET(MMSYSERR_INVALPARAM); return; } u32 st = rd32(c, h + ASH_STATUS); if (!(st & ACMSTREAMHEADER_STATUSF_PREPARED)) { RET(ACMERR_UNPREPARED); return; }
  wr32(c, h + ASH_STATUS, st & ~(u32)ACMSTREAMHEADER_STATUSF_PREPARED); RET(0); }
static s32 acm_sample(const u8 *p, u16 bits, u16 ch, u64 frame, int k) {   /* 16-bit signed sample `k` of frame `frame` */
  if (bits == 8) return ((s32)p[frame * ch + k] - 128) << 8; const u8 *q = p + (frame * ch + k) * 2; return (s16)(q[0] | (q[1] << 8)); }
static void s_acmStreamConvert(Cpu *c) { AcmStream *a = acm_of(ARG(0)); u64 h = ARG(1); u32 f = (u32)ARG(2); if (!a) { RET(MMSYSERR_INVALHANDLE); return; }
  if (!h) { RET(MMSYSERR_INVALPARAM); return; } u32 st = rd32(c, h + ASH_STATUS); if (!(st & ACMSTREAMHEADER_STATUSF_PREPARED)) { RET(ACMERR_UNPREPARED); return; }
  u64 src = rdp(c, h + ASH_SRC), dst = rdp(c, h + ASH_DST); u32 slen = rd32(c, h + ASH_SRCLEN), dlen = rd32(c, h + ASH_DSTLEN);
  u32 sfs = a->sch * a->sbits / 8, dfs = a->dch * a->dbits / 8; u64 sframes = slen / sfs, dcap = dlen / dfs;
  const u8 *sp = mem_ptr(c->mem, src, slen, 0); u8 *dp = mem_ptr(c->mem, dst, dlen, 1); if (!sp || !dp) { RET(MMSYSERR_INVALPARAM); return; }
  if (f & 0x10) { a->phase = 0; a->last[0] = a->last[1] = 0; }   /* ACM_STREAMCONVERTF_START */
  int end = (f & 0x20) != 0;                                        /* ACM_STREAMCONVERTF_END: flush the tail sample */
  double step = (double)a->srate / (double)a->drate; double pos = a->phase; u64 out = 0; u64 used = 0;
  while (out < dcap) { u64 i0 = (u64)pos; if (i0 + 1 >= sframes) { if (!(end && i0 < sframes)) break; }
    double fr = pos - (double)i0; s32 s[2];
    for (int k = 0; k < 2; k++) { int sk = a->sch == 2 ? k : 0; s32 v0 = acm_sample(sp, a->sbits, a->sch, i0, sk); s32 v1 = i0 + 1 < sframes ? acm_sample(sp, a->sbits, a->sch, i0 + 1, sk) : v0; s[k] = (s32)(v0 + (v1 - v0) * fr); }
    if (a->dch == 1 && a->sch == 2) { s[0] = (s[0] + s[1]) / 2; }
    for (int k = 0; k < a->dch; k++) { s32 v = s[k]; if (a->dbits == 8) dp[out * a->dch + k] = (u8)((v >> 8) + 128); else { u8 *q = dp + (out * a->dch + k) * 2; q[0] = (u8)v; q[1] = (u8)(v >> 8); } }
    out++; pos += step; used = (u64)pos; if (used > sframes) used = sframes; }
  if (end) { used = sframes; a->phase = 0; } else { a->phase = pos - (double)used; }
  wr32(c, h + ASH_SRCUSED, (u32)(used * sfs)); wr32(c, h + ASH_DSTUSED, (u32)(out * dfs));
  wr32(c, h + ASH_STATUS, (st | ACMSTREAMHEADER_STATUSF_DONE) & ~(u32)ACMSTREAMHEADER_STATUSF_INQUEUE); RET(0); }
static void s_acmStreamMessage(Cpu *c) { RET(MMSYSERR_INVALHANDLE); }
static void s_acmDriverMessage(Cpu *c) { RET(MMSYSERR_INVALHANDLE); }
static void s_acmDriverAdd(Cpu *c) { RET(MMSYSERR_INVALPARAM); }
static void s_acmDriverID(Cpu *c) { RET(MMSYSERR_INVALHANDLE); }
static void s_acmDriverDetails(Cpu *c) { RET(MMSYSERR_INVALHANDLE); }
static void s_acmDriverPriority(Cpu *c) { RET(MMSYSERR_INVALHANDLE); }
static void s_acmFilterTagEnum(Cpu *c) { RET(0); }
static void s_acmFilterEnum(Cpu *c) { RET(0); }
static void s_acmFilterChoose(Cpu *c) { RET(ACMERR_CANCELED); }
static void s_acmFilterTagDetails(Cpu *c) { RET(ACMERR_NOTPOSSIBLE); }
static void s_acmFilterDetails(Cpu *c) { RET(ACMERR_NOTPOSSIBLE); }

/* ---- wininet: offline ---- */
#define H_INET_BASE 0x6900ull
#define INET_MAX 64
#define ERROR_INTERNET_CANNOT_CONNECT 12029
#define ERROR_INTERNET_NAME_NOT_RESOLVED 12007
#define ERROR_INTERNET_INVALID_OPERATION 12019
#define ERROR_INTERNET_INCORRECT_HANDLE_TYPE 12018
#define ERROR_INTERNET_INCORRECT_HANDLE_STATE 12019
#define ERROR_INTERNET_DISCONNECTED 12163
typedef struct { int used, kind; u64 ctx; u64 cb; } InetHandle;   /* kind: 1 session (InternetOpen) 2 connection 3 request */
static InetHandle g_inet[INET_MAX];
static InetHandle *inet_of(u64 h) { if (h < H_INET_BASE || h >= H_INET_BASE + INET_MAX) return 0; InetHandle *i = &g_inet[h - H_INET_BASE]; return i->used ? i : 0; }
static u64 inet_new(int kind, u64 ctx) { for (int i = 0; i < INET_MAX; i++) if (!g_inet[i].used) { g_inet[i].used = 1; g_inet[i].kind = kind; g_inet[i].ctx = ctx; g_inet[i].cb = 0; return H_INET_BASE + i; } return 0; }
static void s_InternetOpenW(Cpu *c) { u64 h = inet_new(1, 0); if (!h) { W->last_error = 8; RET(0); return; } RET(h); }
static void s_InternetOpenA(Cpu *c) { s_InternetOpenW(c); }
static void s_InternetCloseHandle(Cpu *c) { InetHandle *i = inet_of(ARG(0)); if (!i) { W->last_error = 6; RET(0); return; } i->used = 0; RET(1); }
static void s_InternetConnectW(Cpu *c) { InetHandle *s = inet_of(ARG(0)); if (!s || s->kind != 1) { W->last_error = ERROR_INTERNET_INCORRECT_HANDLE_TYPE; RET(0); return; }
  /* Windows returns a connection handle without touching the network; the failure comes at HttpSendRequest */
  u64 h = inet_new(2, ARG(7)); RET(h); }
static void s_InternetConnectA(Cpu *c) { s_InternetConnectW(c); }
static void s_HttpOpenRequestW(Cpu *c) { InetHandle *s = inet_of(ARG(0)); if (!s || s->kind != 2) { W->last_error = ERROR_INTERNET_INCORRECT_HANDLE_TYPE; RET(0); return; } u64 h = inet_new(3, ARG(7)); RET(h); }
static void s_HttpOpenRequestA(Cpu *c) { s_HttpOpenRequestW(c); }
static void inet_offline(Cpu *c, const char *api) { static int logged; if (!logged++) e_log("[x64] %s: no network access from the emulator (browser sandbox) — reporting ERROR_INTERNET_CANNOT_CONNECT\n", api); W->last_error = ERROR_INTERNET_CANNOT_CONNECT; RET(0); }
static void s_HttpSendRequestW(Cpu *c) { inet_offline(c, "HttpSendRequest"); }
static void s_HttpSendRequestA(Cpu *c) { inet_offline(c, "HttpSendRequest"); }
static void s_HttpSendRequestExW(Cpu *c) { inet_offline(c, "HttpSendRequestEx"); }
static void s_HttpSendRequestExA(Cpu *c) { inet_offline(c, "HttpSendRequestEx"); }
static void s_HttpEndRequestW(Cpu *c) { inet_offline(c, "HttpEndRequest"); }
static void s_HttpEndRequestA(Cpu *c) { inet_offline(c, "HttpEndRequest"); }
static void s_InternetOpenUrlW(Cpu *c) { inet_offline(c, "InternetOpenUrl"); }
static void s_InternetOpenUrlA(Cpu *c) { inet_offline(c, "InternetOpenUrl"); }
static void s_InternetReadFile(Cpu *c) { if (ARG(3)) wr32(c, ARG(3), 0); InetHandle *i = inet_of(ARG(0)); if (!i) { W->last_error = 6; RET(0); return; } W->last_error = ERROR_INTERNET_INCORRECT_HANDLE_STATE; RET(0); }
static void s_InternetReadFileExW(Cpu *c) { W->last_error = ERROR_INTERNET_INCORRECT_HANDLE_STATE; RET(0); }
static void s_InternetReadFileExA(Cpu *c) { s_InternetReadFileExW(c); }
static void s_InternetWriteFile(Cpu *c) { if (ARG(3)) wr32(c, ARG(3), 0); W->last_error = ERROR_INTERNET_INCORRECT_HANDLE_STATE; RET(0); }
static void s_InternetQueryDataAvailable(Cpu *c) { if (ARG(1)) wr32(c, ARG(1), 0); W->last_error = ERROR_INTERNET_INCORRECT_HANDLE_STATE; RET(0); }
static void s_HttpQueryInfoW(Cpu *c) { W->last_error = ERROR_INTERNET_INCORRECT_HANDLE_STATE; RET(0); }
static void s_HttpQueryInfoA(Cpu *c) { s_HttpQueryInfoW(c); }
static void s_HttpAddRequestHeadersW(Cpu *c) { InetHandle *i = inet_of(ARG(0)); if (!i || i->kind != 3) { W->last_error = ERROR_INTERNET_INCORRECT_HANDLE_TYPE; RET(0); return; } RET(1); }
static void s_HttpAddRequestHeadersA(Cpu *c) { s_HttpAddRequestHeadersW(c); }
static void s_InternetSetOptionW(Cpu *c) { InetHandle *i = inet_of(ARG(0)); if (ARG(0) && !i) { W->last_error = 6; RET(0); return; } RET(1); }
static void s_InternetSetOptionA(Cpu *c) { s_InternetSetOptionW(c); }
static void s_InternetQueryOptionW(Cpu *c) { u32 opt = (u32)ARG(1); u64 buf = ARG(2), len = ARG(3); u32 have = len ? rd32(c, len) : 0;
  if (opt == 50 /* INTERNET_OPTION_CONNECTED_STATE */) { if (have < 4 || !buf) { if (len) wr32(c, len, 4); W->last_error = 122; RET(0); return; } wr32(c, buf, 2 /* INTERNET_STATE_DISCONNECTED */); wr32(c, len, 4); RET(1); return; }
  if (opt == 40 /* INTERNET_OPTION_VERSION */) { if (have < 8 || !buf) { if (len) wr32(c, len, 8); W->last_error = 122; RET(0); return; } wr32(c, buf, 1); wr32(c, buf + 4, 2); wr32(c, len, 8); RET(1); return; }
  if (opt == 38 /* INTERNET_OPTION_PROXY */) { u32 need = (u32)(3 * PSZ); if (have < need || !buf) { if (len) wr32(c, len, need); W->last_error = 122; RET(0); return; } wr32(c, buf, 1 /* INTERNET_OPEN_TYPE_DIRECT */); wrp(c, buf + PSZ, 0); wrp(c, buf + 2 * PSZ, 0); wr32(c, len, need); RET(1); return; }
  W->last_error = 12018; RET(0); }
static void s_InternetQueryOptionA(Cpu *c) { s_InternetQueryOptionW(c); }
static void s_InternetGetConnectedState(Cpu *c) { if (ARG(0)) wr32(c, ARG(0), 0x20 /* INTERNET_CONNECTION_OFFLINE */); W->last_error = ERROR_INTERNET_DISCONNECTED; RET(0); }
static void s_InternetGetConnectedStateExW(Cpu *c) { if (ARG(0)) wr32(c, ARG(0), 0x20); if (ARG(1) && ARG(2)) wr16(c, ARG(1), 0); W->last_error = ERROR_INTERNET_DISCONNECTED; RET(0); }
static void s_InternetGetConnectedStateExA(Cpu *c) { if (ARG(0)) wr32(c, ARG(0), 0x20); if (ARG(1) && ARG(2)) wr8(c, ARG(1), 0); W->last_error = ERROR_INTERNET_DISCONNECTED; RET(0); }
static void s_InternetCheckConnectionW(Cpu *c) { W->last_error = ERROR_INTERNET_CANNOT_CONNECT; RET(0); }
static void s_InternetCheckConnectionA(Cpu *c) { s_InternetCheckConnectionW(c); }
static void s_InternetAttemptConnect(Cpu *c) { RET(ERROR_INTERNET_CANNOT_CONNECT); }
static void s_InternetSetStatusCallback(Cpu *c) { InetHandle *i = inet_of(ARG(0)); if (!i) { RET(0xFFFFFFFFFFFFFFFFull); return; } u64 old = i->cb; i->cb = ARG(1); RET(old); }
static void s_InternetSetStatusCallbackW(Cpu *c) { s_InternetSetStatusCallback(c); }
static void s_InternetSetStatusCallbackA(Cpu *c) { s_InternetSetStatusCallback(c); }
static void s_InternetGetLastResponseInfoW(Cpu *c) { if (ARG(0)) wr32(c, ARG(0), 0); if (ARG(2)) { if (ARG(1) && rd32(c, ARG(2))) wr16(c, ARG(1), 0); wr32(c, ARG(2), 0); } RET(1); }
static void s_InternetGetLastResponseInfoA(Cpu *c) { if (ARG(0)) wr32(c, ARG(0), 0); if (ARG(2)) { if (ARG(1) && rd32(c, ARG(2))) wr8(c, ARG(1), 0); wr32(c, ARG(2), 0); } RET(1); }
static void s_InternetGetCookieW(Cpu *c) { if (ARG(3)) wr32(c, ARG(3), 0); W->last_error = 259; RET(0); }   /* ERROR_NO_MORE_ITEMS: no cookies */
static void s_InternetGetCookieA(Cpu *c) { s_InternetGetCookieW(c); }
static void s_InternetGetCookieExW(Cpu *c) { s_InternetGetCookieW(c); }
static void s_InternetGetCookieExA(Cpu *c) { s_InternetGetCookieW(c); }
static void s_InternetSetCookieW(Cpu *c) { RET(1); }
static void s_InternetSetCookieA(Cpu *c) { RET(1); }
static void s_InternetSetCookieExW(Cpu *c) { RET(1); }
static void s_InternetSetCookieExA(Cpu *c) { RET(1); }
static void s_DeleteUrlCacheEntryW(Cpu *c) { W->last_error = 2; RET(0); }
static void s_DeleteUrlCacheEntryA(Cpu *c) { s_DeleteUrlCacheEntryW(c); }
static void s_DeleteUrlCacheEntry(Cpu *c) { s_DeleteUrlCacheEntryW(c); }
static void s_GetUrlCacheEntryInfoW(Cpu *c) { W->last_error = 2; RET(0); }
static void s_GetUrlCacheEntryInfoA(Cpu *c) { s_GetUrlCacheEntryInfoW(c); }
static void s_FtpOpenFileW(Cpu *c) { inet_offline(c, "FtpOpenFile"); }
static void s_FtpOpenFileA(Cpu *c) { inet_offline(c, "FtpOpenFile"); }
static void s_FtpGetFileW(Cpu *c) { inet_offline(c, "FtpGetFile"); }
static void s_FtpGetFileA(Cpu *c) { inet_offline(c, "FtpGetFile"); }
static void s_FtpFindFirstFileW(Cpu *c) { inet_offline(c, "FtpFindFirstFile"); }
static void s_FtpFindFirstFileA(Cpu *c) { inet_offline(c, "FtpFindFirstFile"); }
static void s_InternetFindNextFileW(Cpu *c) { W->last_error = 259; RET(0); }
static void s_InternetFindNextFileA(Cpu *c) { s_InternetFindNextFileW(c); }
static void s_InternetErrorDlg(Cpu *c) { RET(0); }   /* ERROR_SUCCESS: dialog "dismissed", caller does not retry */
static void s_InternetAutodial(Cpu *c) { W->last_error = ERROR_INTERNET_DISCONNECTED; RET(0); }
static void s_InternetAutodialHangup(Cpu *c) { RET(1); }
static void s_InternetGoOnline(Cpu *c) { RET(0); }
static void s_InternetGoOnlineW(Cpu *c) { RET(0); }
static void s_InternetGoOnlineA(Cpu *c) { RET(0); }

#define ACM_SHIMS S(acmGetVersion), S(acmMetrics), S(acmDriverEnum), S(acmDriverOpen), S(acmDriverClose), S(acmFormatTagEnum), S(acmFormatEnum), S(acmFormatChoose), S(acmFormatTagDetails), S(acmFormatDetails), \
  S(acmFormatSuggest), S(acmStreamOpen), S(acmStreamClose), S(acmStreamReset), S(acmStreamSize), S(acmStreamPrepareHeader), S(acmStreamUnprepareHeader), S(acmStreamConvert), S(acmStreamMessage), S(acmDriverMessage), \
  S(acmDriverAdd), S(acmDriverID), S(acmDriverDetails), S(acmDriverPriority), S(acmFilterTagEnum), S(acmFilterEnum), S(acmFilterChoose), S(acmFilterTagDetails), S(acmFilterDetails), \
  SA("acmDriverAddA", acmDriverAdd), SA("acmDriverAddW", acmDriverAdd), SA("acmDriverDetailsA", acmDriverDetails), SA("acmDriverDetailsW", acmDriverDetails), SA("acmFormatTagDetailsA", acmFormatTagDetails), SA("acmFormatTagDetailsW", acmFormatTagDetails), \
  SA("acmFormatDetailsA", acmFormatDetails), SA("acmFormatDetailsW", acmFormatDetails), SA("acmFormatTagEnumA", acmFormatTagEnum), SA("acmFormatTagEnumW", acmFormatTagEnum), SA("acmFormatEnumA", acmFormatEnum), SA("acmFormatEnumW", acmFormatEnum), \
  SA("acmFormatChooseA", acmFormatChoose), SA("acmFormatChooseW", acmFormatChoose), SA("acmFilterTagDetailsA", acmFilterTagDetails), SA("acmFilterTagDetailsW", acmFilterTagDetails), SA("acmFilterDetailsA", acmFilterDetails), SA("acmFilterDetailsW", acmFilterDetails), \
  SA("acmFilterTagEnumA", acmFilterTagEnum), SA("acmFilterTagEnumW", acmFilterTagEnum), SA("acmFilterEnumA", acmFilterEnum), SA("acmFilterEnumW", acmFilterEnum), SA("acmFilterChooseA", acmFilterChoose), SA("acmFilterChooseW", acmFilterChoose), \
  S(InternetOpenW), S(InternetOpenA), S(InternetCloseHandle), S(InternetConnectW), S(InternetConnectA), S(HttpOpenRequestW), S(HttpOpenRequestA), S(HttpSendRequestW), S(HttpSendRequestA), S(HttpSendRequestExW), S(HttpSendRequestExA), \
  S(HttpEndRequestW), S(HttpEndRequestA), S(InternetOpenUrlW), S(InternetOpenUrlA), S(InternetReadFile), S(InternetReadFileExW), S(InternetReadFileExA), S(InternetWriteFile), S(InternetQueryDataAvailable), S(HttpQueryInfoW), S(HttpQueryInfoA), \
  S(HttpAddRequestHeadersW), S(HttpAddRequestHeadersA), S(InternetSetOptionW), S(InternetSetOptionA), S(InternetQueryOptionW), S(InternetQueryOptionA), S(InternetGetConnectedState), S(InternetGetConnectedStateExW), S(InternetGetConnectedStateExA), \
  S(InternetCheckConnectionW), S(InternetCheckConnectionA), S(InternetAttemptConnect), S(InternetSetStatusCallback), S(InternetSetStatusCallbackW), S(InternetSetStatusCallbackA), S(InternetGetLastResponseInfoW), S(InternetGetLastResponseInfoA), \
  S(InternetGetCookieW), S(InternetGetCookieA), S(InternetGetCookieExW), S(InternetGetCookieExA), S(InternetSetCookieW), S(InternetSetCookieA), S(InternetSetCookieExW), S(InternetSetCookieExA), S(DeleteUrlCacheEntryW), S(DeleteUrlCacheEntryA), S(DeleteUrlCacheEntry), \
  S(GetUrlCacheEntryInfoW), S(GetUrlCacheEntryInfoA), S(FtpOpenFileW), S(FtpOpenFileA), S(FtpGetFileW), S(FtpGetFileA), S(FtpFindFirstFileW), S(FtpFindFirstFileA), S(InternetFindNextFileW), S(InternetFindNextFileA), S(InternetErrorDlg), \
  S(InternetAutodial), S(InternetAutodialHangup), S(InternetGoOnline), S(InternetGoOnlineW), S(InternetGoOnlineA),
