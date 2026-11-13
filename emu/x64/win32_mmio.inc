/* winmm multimedia file I/O (mmio*): RIFF chunk navigation over guest files and memory buffers — WAV/AVI loaders in
   games and Corona/SDL-era engines. Streams are unbuffered (file bytes go straight to the VFS); memory streams
   (FOURCC_MEM, pchBuffer/cchBuffer in MMIOINFO) expose the buffer through mmioGetInfo like Windows does. Custom
   I/O procedures (mmioInstallIOProc) are not supported and say so. */
#define H_MMIO_BASE 0x6000ull
#define MMIO_MAX 64
#define FOURCC_RIFF 0x46464952u
#define FOURCC_LIST 0x5453494cu
#define FOURCC_MEM  0x204d454du
typedef struct { int used, fd, mem, write; u64 buf; s64 pos, size; u32 flags; } MmioStream;
static MmioStream g_mmio[MMIO_MAX];
static MmioStream *mmio_of(u64 h) { if (h < H_MMIO_BASE || h >= H_MMIO_BASE + MMIO_MAX) return 0; MmioStream *m = &g_mmio[h - H_MMIO_BASE]; return m->used ? m : 0; }
static s64 mmio_read(Cpu *c, MmioStream *m, u64 dst, s64 n) {
  if (n <= 0) return 0;
  if (m->mem) { if (m->pos >= m->size) return 0; if (n > m->size - m->pos) n = m->size - m->pos; const u8 *s = mem_ptr(c->mem, m->buf + (u64)m->pos, (u64)n, 0); u8 *d = mem_ptr(c->mem, dst, (u64)n, 1); if (!s || !d) return -1; e_memcpy(d, s, (size_t)n); m->pos += n; return n; }
  u8 *d = mem_ptr(c->mem, dst, (u64)n, 1); if (!d) return -1; vfs_seek(m->fd, m->pos, 0); s64 got = vfs_read(m->fd, d, (u64)n); if (got > 0) m->pos += got; return got; }
static s64 mmio_write(Cpu *c, MmioStream *m, u64 src, s64 n) {
  if (n <= 0) return 0; const u8 *s = mem_ptr(c->mem, src, (u64)n, 0); if (!s) return -1;
  if (m->mem) { if (m->pos + n > m->size) n = m->size - m->pos; if (n <= 0) return -1; u8 *d = mem_ptr(c->mem, m->buf + (u64)m->pos, (u64)n, 1); if (!d) return -1; e_memcpy(d, s, (size_t)n); m->pos += n; return n; }
  vfs_seek(m->fd, m->pos, 0); s64 w = vfs_write(m->fd, s, (u64)n); if (w > 0) { m->pos += w; if (m->pos > m->size) m->size = m->pos; } return w; }
static int mmio_rd32(Cpu *c, MmioStream *m, u32 *v) { u8 b[4]; s64 save = m->pos;
  if (m->mem) { if (m->pos + 4 > m->size) return 0; const u8 *s = mem_ptr(c->mem, m->buf + (u64)m->pos, 4, 0); if (!s) return 0; e_memcpy(b, s, 4); }
  else { vfs_seek(m->fd, m->pos, 0); if (vfs_read(m->fd, b, 4) != 4) { m->pos = save; return 0; } }
  m->pos += 4; *v = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24); return 1; }
static void mmio_wr32(Cpu *c, MmioStream *m, u32 v) { u8 b[4] = { (u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24) };
  if (m->mem) { if (m->pos + 4 <= m->size) { u8 *d = mem_ptr(c->mem, m->buf + (u64)m->pos, 4, 1); if (d) e_memcpy(d, b, 4); m->pos += 4; } return; }
  vfs_seek(m->fd, m->pos, 0); if (vfs_write(m->fd, b, 4) == 4) { m->pos += 4; if (m->pos > m->size) m->size = m->pos; } }
/* MMIOINFO field offsets (x86 / x64) — mmsystem.h structures are 1-byte packed, so x64 is 100 bytes with no padding */
#define MI_FLAGS 0
#define MI_IOPROC 4
#define MI_ERR 16
#define MI_CCH (c->mode32 ? 20 : 28)
#define MI_BUF (c->mode32 ? 24 : 32)
#define MI_NEXT (c->mode32 ? 28 : 40)
#define MI_ENDR (c->mode32 ? 32 : 48)
#define MI_ENDW (c->mode32 ? 36 : 56)
#define MI_BUFOFF (c->mode32 ? 40 : 64)
#define MI_DISKOFF (c->mode32 ? 44 : 68)
#define MI_HMMIO (c->mode32 ? 68 : 92)
#define MI_SIZE (c->mode32 ? 72 : 100)
static void mmio_open(Cpu *c, const char *name, u64 info, u32 flags) {
  int i = 0; while (i < MMIO_MAX && g_mmio[i].used) i++; if (i >= MMIO_MAX) { if (info) wr32(c, info + MI_ERR, 0x104); RET(0); return; }   /* MMIOERR_TOOMANYOPENFILES */
  MmioStream *m = &g_mmio[i]; e_memset(m, 0, sizeof *m); m->flags = flags; m->write = (flags & 3) != 0;
  u32 ioproc = info ? rd32(c, info + MI_IOPROC) : 0; u64 pproc = info ? rdp(c, info + 8) : 0;   /* pIOProc follows fccIOProc at 8 on both bitnesses */
  if (info && pproc && ioproc != FOURCC_MEM) { e_log("[winmm] mmioOpen: custom I/O procedure (fccIOProc 0x%08x) is not supported\n", ioproc); wr32(c, info + MI_ERR, 0x10a); RET(0); return; }   /* MMIOERR_UNBUFFERED is the closest documented code */
  if (info && (ioproc == FOURCC_MEM || (!name && rdp(c, info + MI_BUF)))) {
    m->mem = 1; m->buf = rdp(c, info + MI_BUF); m->size = (s32)rd32(c, info + MI_CCH); m->pos = 0;
    if (!m->buf || m->size < 0) { wr32(c, info + MI_ERR, 0x10b); RET(0); return; }   /* MMIOERR_PATHNOTFOUND */
  } else {
    if (!name || !*name) { if (info) wr32(c, info + MI_ERR, 0x10b); RET(0); return; }
    int acc = (flags & 3) == 0 ? 1 : (flags & 3) == 1 ? 2 : 3; int disp = (flags & 0x1000) ? 2 : (flags & 0x2000) == 0 ? 3 : 3;   /* MMIO_CREATE → CREATE_ALWAYS */
    if (flags & 0x1000) acc |= 2;
    int fd = vfs_open(name, acc, disp, 0); if (fd < 0) { if (info) wr32(c, info + MI_ERR, 0x10b); RET(0); return; }
    m->fd = fd; m->size = vfs_size(fd); m->pos = 0; }
  m->used = 1; u64 h = H_MMIO_BASE + (u64)i; if (info) { wr32(c, info + MI_ERR, 0); wrp(c, info + MI_HMMIO, h); } RET(h); }
static void s_mmioOpenA(Cpu *c) { u64 n = ARG(0); if (ARG(2) & 0x20) { RET(0); return; }   /* MMIO_PARSE/DELETE flags are not open requests */
  const char *name = n ? rd_str(c, n, 1024) : 0; mmio_open(c, name, ARG(1), (u32)ARG(2)); }
static void s_mmioOpenW(Cpu *c) { u64 n = ARG(0); char name[1024]; name[0] = 0; if (n) wstr_to_utf8(c, n, name, sizeof name, -1); mmio_open(c, n ? name : 0, ARG(1), (u32)ARG(2)); }
static void s_mmioClose(Cpu *c) { MmioStream *m = mmio_of(ARG(0)); if (!m) { RET(5); return; } if (!m->mem) vfs_close(m->fd); m->used = 0; RET(0); }   /* MMSYSERR_INVALHANDLE */
static void s_mmioRead(Cpu *c) { MmioStream *m = mmio_of(ARG(0)); if (!m) { RET((u64)(s64)-1); return; } RET((u64)(s64)(s32)mmio_read(c, m, ARG(1), (s32)ARG(2))); }
static void s_mmioWrite(Cpu *c) { MmioStream *m = mmio_of(ARG(0)); if (!m) { RET((u64)(s64)-1); return; } RET((u64)(s64)(s32)mmio_write(c, m, ARG(1), (s32)ARG(2))); }
static void s_mmioSeek(Cpu *c) { MmioStream *m = mmio_of(ARG(0)); if (!m) { RET((u64)(s64)-1); return; } s64 off = (s32)ARG(1); int org = (int)ARG(2);
  s64 np = org == 0 ? off : org == 1 ? m->pos + off : m->size + off; if (np < 0) { RET((u64)(s64)-1); return; } m->pos = np; RET((u64)np); }
/* MMCKINFO: ckid 0, cksize 4, fccType 8, dwDataOffset 12, dwFlags 16 */
static void s_mmioDescend(Cpu *c) { MmioStream *m = mmio_of(ARG(0)); u64 ck = ARG(1), parent = ARG(2); u32 flags = (u32)ARG(3); if (!m || !ck) { RET(5); return; }
  u32 want_id = rd32(c, ck), want_type = rd32(c, ck + 8); s64 limit = -1;
  if (parent) { u32 pofs = rd32(c, parent + 12), psz = rd32(c, parent + 4); limit = (s64)pofs + psz; }
  for (;;) {
    if (limit >= 0 && m->pos + 8 > limit) { RET(0x101); return; }   /* MMIOERR_CHUNKNOTFOUND */
    s64 at = m->pos; u32 id, sz; if (!mmio_rd32(c, m, &id) || !mmio_rd32(c, m, &sz)) { m->pos = at; RET(0x101); return; }
    u32 type = 0; int is_list = id == FOURCC_RIFF || id == FOURCC_LIST; s64 data = m->pos;
    if (is_list) { if (!mmio_rd32(c, m, &type)) { m->pos = at; RET(0x101); return; } }
    int hit = 0;
    if (flags & 0x20) hit = id == FOURCC_RIFF && (!want_type || type == want_type);        /* MMIO_FINDRIFF */
    else if (flags & 0x40) hit = id == FOURCC_LIST && (!want_type || type == want_type);   /* MMIO_FINDLIST */
    else if (flags & 0x10) hit = id == want_id;                                            /* MMIO_FINDCHUNK */
    else hit = 1;
    if (hit) { wr32(c, ck, id); wr32(c, ck + 4, sz); wr32(c, ck + 8, is_list ? type : 0); wr32(c, ck + 12, (u32)data); wr32(c, ck + 16, 0); RET(0); return; }
    m->pos = data + sz + (sz & 1);   /* chunks are word aligned */
  } }
static void s_mmioAscend(Cpu *c) { MmioStream *m = mmio_of(ARG(0)); u64 ck = ARG(1); if (!m || !ck) { RET(5); return; }
  u32 data = rd32(c, ck + 12), sz = rd32(c, ck + 4), fl = rd32(c, ck + 16);
  if (fl & 0x10000000u) {   /* MMIO_DIRTY: the chunk was created with mmioCreateChunk; patch its size */
    s64 end = m->pos; u32 real = (u32)(end - data); if (real & 1) { u8 z = 0; u64 tmp = W->ubuf; wr8(c, tmp, z); mmio_write(c, m, tmp, 1); end++; }
    m->pos = (s64)data - 4; mmio_wr32(c, m, real); wr32(c, ck + 4, real); wr32(c, ck + 16, fl & ~0x10000000u); m->pos = end; RET(0); return; }
  m->pos = (s64)data + sz + (sz & 1); RET(0); }
static void s_mmioCreateChunk(Cpu *c) { MmioStream *m = mmio_of(ARG(0)); u64 ck = ARG(1); u32 flags = (u32)ARG(2); if (!m || !ck) { RET(5); return; }
  u32 id = (flags & 0x20) ? FOURCC_RIFF : (flags & 0x40) ? FOURCC_LIST : rd32(c, ck); u32 sz = rd32(c, ck + 4);
  mmio_wr32(c, m, id); mmio_wr32(c, m, sz); wr32(c, ck, id); wr32(c, ck + 12, (u32)m->pos);
  if (id == FOURCC_RIFF || id == FOURCC_LIST) mmio_wr32(c, m, rd32(c, ck + 8));
  wr32(c, ck + 16, 0x10000000u); RET(0); }
static void s_mmioGetInfo(Cpu *c) { MmioStream *m = mmio_of(ARG(0)); u64 info = ARG(1); if (!m || !info) { RET(5); return; } u8 *p = mem_ptr(c->mem, info, (u64)MI_SIZE, 1); if (!p) { RET(5); return; } e_memset(p, 0, (size_t)MI_SIZE);
  wr32(c, info + MI_FLAGS, m->flags); wr32(c, info + MI_IOPROC, m->mem ? FOURCC_MEM : 0x20534f44u /* 'DOS ' */); wrp(c, info + MI_HMMIO, ARG(0)); wr32(c, info + MI_DISKOFF, (u32)m->pos);
  if (m->mem) { wr32(c, info + MI_CCH, (u32)m->size); wrp(c, info + MI_BUF, m->buf); wrp(c, info + MI_NEXT, m->buf + (u64)m->pos); wrp(c, info + MI_ENDR, m->buf + (u64)m->size); wrp(c, info + MI_ENDW, m->buf + (u64)m->size); wr32(c, info + MI_BUFOFF, 0); }
  RET(0); }
static void s_mmioSetInfo(Cpu *c) { MmioStream *m = mmio_of(ARG(0)); u64 info = ARG(1); if (!m || !info) { RET(5); return; } if (m->mem) { u64 next = rdp(c, info + MI_NEXT); if (next >= m->buf && next <= m->buf + (u64)m->size) m->pos = (s64)(next - m->buf); } RET(0); }
static void s_mmioAdvance(Cpu *c) { MmioStream *m = mmio_of(ARG(0)); u64 info = ARG(1); if (!m) { RET(5); return; }
  if (m->mem) { if (info) { u64 next = rdp(c, info + MI_NEXT); if (next >= m->buf && next <= m->buf + (u64)m->size) m->pos = (s64)(next - m->buf); } RET(m->pos >= m->size ? 0x105 : 0); return; }   /* MMIOERR_CANNOTREAD at the end */
  RET(0x109); }   /* MMIOERR_UNBUFFERED: file streams have no I/O buffer here */
static void s_mmioFlush(Cpu *c) { RET(mmio_of(ARG(0)) ? 0 : 5); }
static void s_mmioSetBuffer(Cpu *c) { RET(mmio_of(ARG(0)) ? 0 : 5); }   /* unbuffered streams: accepted, reads stay direct */
static void s_mmioStringToFOURCCA(Cpu *c) { const char *s = ARG(0) ? rd_str(c, ARG(0), 8) : 0; u32 flags = (u32)ARG(1); u8 b[4] = { ' ', ' ', ' ', ' ' }; for (int i = 0; s && i < 4 && s[i]; i++) { u8 ch = (u8)s[i]; if ((flags & 0x10) && ch >= 'a' && ch <= 'z') ch -= 32; b[i] = ch; } RET((u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24)); }
static void s_mmioStringToFOURCCW(Cpu *c) { char s[8] = ""; if (ARG(0)) wstr_to_utf8(c, ARG(0), s, sizeof s, 4); u32 flags = (u32)ARG(1); u8 b[4] = { ' ', ' ', ' ', ' ' }; for (int i = 0; i < 4 && s[i]; i++) { u8 ch = (u8)s[i]; if ((flags & 0x10) && ch >= 'a' && ch <= 'z') ch -= 32; b[i] = ch; } RET((u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24)); }
static void s_mmioInstallIOProcA(Cpu *c) { e_log("[winmm] mmioInstallIOProc: custom I/O procedures are not supported\n"); RET(0); }
static void s_mmioSendMessage(Cpu *c) { RET(0); }
static void s_mmioRenameA(Cpu *c) { RET(0x10b); }
#define MMIO_SHIMS \
  S(mmioOpenA), S(mmioOpenW), S(mmioClose), S(mmioRead), S(mmioWrite), S(mmioSeek), S(mmioDescend), S(mmioAscend), S(mmioCreateChunk), S(mmioGetInfo), S(mmioSetInfo), S(mmioAdvance), S(mmioFlush), S(mmioSetBuffer), \
  S(mmioStringToFOURCCA), S(mmioStringToFOURCCW), S(mmioInstallIOProcA), SA("mmioInstallIOProcW", mmioInstallIOProcA), S(mmioSendMessage), S(mmioRenameA), SA("mmioRenameW", mmioRenameA),
