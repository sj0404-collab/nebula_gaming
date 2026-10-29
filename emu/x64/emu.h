/*
 * Nebula x64 — a user-mode x86-64 emulator with a Win32 personality, written to be compiled
 * to wasm64 (and natively for tests).
 *
 *   emu.h     shared types: guest memory regions, CPU state, Win32 shim table, host interface
 *   mem.c     guest address space (flat 64-bit, region table, bump allocator)
 *   cpu.c     instruction decoder + interpreter (integer ISA, SSE/SSE2 scalar+packed subset, x87 control NOPs)
 *   pe.c      PE32+ loader (sections, base relocations, imports → shim slots, TLS callbacks)
 *   win32.c   kernel32 / msvcrt / user32 / gdi32 shims (enough for mingw-w64 CRT start-up, console and GDI apps)
 *   api.c     exported entry points used by the JS host (wasm64) and by the native test runner
 */
#ifndef NEBULA_EMU_H
#define NEBULA_EMU_H
#include <stdint.h>
#include <stddef.h>

typedef uint8_t  u8;  typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t   s8;  typedef int16_t  s16; typedef int32_t  s32; typedef int64_t  s64;

/* ---------- host interface (implemented in host_native.c or imported from JS in wasm) ---------- */
void   host_write(int fd, const char *p, size_t n);        /* console output (1 stdout, 2 stderr, 3 emulator log) */
double host_now_ms(void);                                  /* monotonic milliseconds */
void   host_present(const u32 *bgra, int w, int h);        /* framebuffer of the GUI window changed */
int    host_key(int vk);                                   /* 1 if virtual key currently pressed */
void   host_exit(int code);                                /* guest called ExitProcess */
double host_math(int op, double a, double b);              /* 0 sin 1 cos 2 tan 3 exp 4 log 5 pow 6 atan2 7 asin 8 acos 9 atan 10 sinh 11 cosh 12 tanh 13 log10 14 log2 15 cbrt */

/* ---------- guest memory ---------- */
#define MAX_REGIONS 32
typedef struct { u64 base, size; u8 *host; const char *name; u32 prot; } Region;

typedef struct {
  Region  r[MAX_REGIONS];
  int     n;
  int     last;                       /* last-hit cache */
  u64     fault_addr;                 /* set on failed translation */
  int     fault_write;
} Mem;

u8  *mem_map(Mem *m, u64 base, u64 size, const char *name);   /* add region (host memory allocated) */
u8  *mem_ptr(Mem *m, u64 addr, u64 size, int write);          /* NULL on fault */
Region *mem_region(Mem *m, u64 addr);
void mem_release(Mem *m);                                     /* give all regions back to the pool */
void *emu_alloc(size_t n);                                    /* zeroed host memory, never freed (bump) */
size_t emu_alloc_used(void);

/* ---------- CPU ---------- */
typedef union { u64 q[2]; u32 d[4]; u16 w[8]; u8 b[16]; double f64[2]; float f32[4]; } Xmm;

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
enum { F_CF = 1u << 0, F_PF = 1u << 2, F_AF = 1u << 4, F_ZF = 1u << 6, F_SF = 1u << 7, F_DF = 1u << 10, F_OF = 1u << 11 };

/* why cpu_run returned */
enum {
  RUN_OK = 0,            /* budget exhausted, call again */
  RUN_EXIT = 1,          /* process exited (cpu->exit_code) */
  RUN_FAULT = 2,         /* memory fault / unsupported instruction / unknown import — see cpu->err */
  RUN_YIELD = 3,         /* guest blocked in a wait (Sleep / PeekMessage with no work) — host may present a frame */
  RUN_BREAK = 4,         /* int3 / debugger break */
};

typedef struct Cpu Cpu;
typedef struct Win32 Win32;

struct Cpu {
  u64  r[16];
  u64  rip;
  u64  rflags;
  Xmm  x[16];
  u32  mxcsr;
  u64  fs_base, gs_base;
  u16  fpu_cw;                        /* x87 control word (only stored) */
  /* run control */
  u64  icount;                        /* instructions executed */
  int  status;                        /* RUN_* */
  int  exit_code;
  char err[256];
  u64  err_rip;
  Mem *mem;
  Win32 *w;
  /* tracing / stats */
  int  trace;
  u32  unknown_op[4];                 /* first unsupported opcode bytes */
};

void cpu_reset(Cpu *c);
int  cpu_run(Cpu *c, u64 max_insns);  /* returns RUN_* */
void cpu_fault(Cpu *c, const char *fmt, ...);
/* stack helpers (used by the loader and shims) */
void cpu_push(Cpu *c, u64 v);
u64  cpu_pop(Cpu *c);

/* memory accessors with fault handling (return 0 / set cpu->status on fault) */
u8  rd8 (Cpu *c, u64 a); u16 rd16(Cpu *c, u64 a); u32 rd32(Cpu *c, u64 a); u64 rd64(Cpu *c, u64 a);
void wr8(Cpu *c, u64 a, u8 v); void wr16(Cpu *c, u64 a, u16 v); void wr32(Cpu *c, u64 a, u32 v); void wr64(Cpu *c, u64 a, u64 v);
const char *rd_str(Cpu *c, u64 a, size_t max);     /* NUL-terminated guest string → host pointer (NULL if unmapped) */

/* ---------- Win32 shims ---------- */
typedef void (*ShimFn)(Cpu *c);
typedef struct { const char *dll; const char *name; ShimFn fn; } ShimDef;

#define SHIM_SLOT_SIZE 16
#define MAX_SHIMS 1024
typedef struct { const char *dll; const char *name; ShimFn fn; int calls; } ShimSlot;

/* guest layout */
#define G_SHIM_BASE   0x0000000000200000ull    /* import trampolines, TEB/PEB, strings  (2 MB) */
#define G_SHIM_SIZE   0x0000000000200000ull
#define G_STACK_BASE  0x0000000010000000ull    /* 8 MB stack */
#define G_STACK_SIZE  0x0000000000800000ull
#define G_HEAP_BASE   0x0000000020000000ull    /* guest heap; VirtualAlloc/HeapAlloc carve from here */
#define G_FB_BASE     0x0000000070000000ull    /* DIB sections (framebuffers) */

typedef struct { u64 base, size; int free; } HeapBlock;

typedef struct {
  u64 hwnd; int w, h; char title[128];
  u64 wndproc; u32 style; int shown; int created; u64 brush;
} GuestWindow;

typedef struct {
  u64 handle; int kind;               /* 1 DC, 2 BITMAP, 3 BRUSH, 4 PEN, 5 FONT, 6 RGN */
  /* DC */ u64 bitmap; u32 text_color, bk_color; int bk_mode; u64 font, brush, pen;
  /* BITMAP */ int bw, bh; u64 bits; /* guest address of BGRA pixels */
  /* BRUSH/PEN */ u32 color; int pen_width;
  /* FONT */ int font_h; int font_bold;
  /* RGN */ int rx, ry, rw, rh; int npts; s32 pts[64];
} GdiObj;

#define MAX_GDI 2048
#define MAX_MSGS 256
typedef struct { u64 hwnd; u32 msg; u64 wparam, lparam; u32 time; } GuestMsg;

/* ---- per-process file-descriptor / environment state (vfs.c) ---- */
#define FD_MAX 128
enum { FD_FILE = 1, FD_CONSOLE, FD_PIPE, FD_MAP, FD_PROC, FD_NULL };
typedef struct { int kind; int node; u64 pos; int append, eof, err, unget, alias, obj, wend; int noinherit; } FdEnt;   /* noinherit: not copied into children */
typedef struct { int used; char dir[512]; char pat[128]; int cursor; int dots; } VFind;
typedef struct { u64 addr, size; int fd; } MapView;
typedef struct {
  FdEnt fd[FD_MAX]; int redir[3]; char cwd[512]; VFind find[32];
  char *env[256]; int nenv, env_dirty; u64 strtok_next;
  MapView views[32]; int nviews;
} ProcFs;
extern ProcFs *vfs_P;
void vfs_use(ProcFs *p); ProcFs *vfs_root_fs(void); void vfs_init_proc(ProcFs *p, const ProcFs *parent); void vfs_release_proc(ProcFs *p);
int  vfs_kind(int fd); int vfs_fd_obj(int fd);
int  vfs_pipe(int *rfd, int *wfd); int vfs_pipe_avail(int fd); int vfs_pipe_id(int fd); int vfs_pipe_would_block(int pi, int for_write);
int  vfs_map_create(int file_fd, u64 size); u8 *vfs_map_data(int fd, u64 *size); int vfs_map_write_back(int fd, const u8 *src, u64 n);
int  vfs_proc_handle(int pid); int vfs_fd_pid(int fd);
/* ---- process table (api.c) ---- */
enum { BLOCK_NONE = 0, BLOCK_STDIN, BLOCK_PROC, BLOCK_PIPE_R, BLOCK_PIPE_W, BLOCK_SLEEP };
int  proc_spawn(Win32 *parent, const char *exe_path, const char *cmdline, const int std_fd[3]);   /* returns pid or -errno */
int  proc_state(int pid, int *exit_code);           /* 0 unknown, 1 running, 2 exited */
void proc_handle_ref(int pid, int delta);
ProcFs *proc_fs(int pid);
int  proc_read_mem(int pid, u64 addr, u8 *buf, u64 n);           /* ReadProcessMemory */
u64  proc_image_base(int pid, const char **name);
double proc_start_ms(int pid);
void proc_kill(int pid, int code);
int  proc_enum(int idx, int *pid, int *ppid, const char **name);   /* 1 if idx valid */

struct Win32 {
  Cpu *cpu;
  Mem *mem;
  ShimSlot slots[MAX_SHIMS]; int nslots;
  u64 image_base; u64 image_size; u64 entry; int subsystem;   /* 2 GUI, 3 console */
  char exe_name[260]; char cmdline[1024];
  u64 teb, peb, cmdline_a, cmdline_w, env_w, env_a;
  u64 last_error;
  /* heap */
  u64 heap_cur, heap_end; HeapBlock blocks[65536]; int nblocks;
  u64 fb_cur;                           /* bump pointer for DIB sections */
  u32 tls_slots[64]; u64 tls_values[64];
  u64 tls_callbacks[16]; int n_tls_cb;
  /* continuation stack for guest callbacks (wndproc, TLS callbacks, _initterm, atexit) */
  struct { int kind; u64 ret_addr, rsp, data[6]; int idx; } cont[64]; int ncont;
  int sleeping;
  int yield_frames; int dirty;
  /* window + GDI */
  GuestWindow win; GdiObj gdi[MAX_GDI]; int ngdi; u64 next_handle;
  GuestMsg msgs[MAX_MSGS]; int msg_head, msg_tail;
  u32 *screen; int screen_w, screen_h;  /* host-side composed window contents (BGRA) */
  int frames_presented; int blits_since_yield;
  int key_state[256]; int key_prev[256];
  double start_ms, sleep_until_ms; u64 fake_time_ms;
  int unknown_import_calls;
  char report[4096]; int report_len;     /* capability report text */
  /* console */
  char con[65536]; int con_len;
  u64 at_exit[64]; int n_at_exit;
  int exited;
  int wndclass_registered; u64 wndclass_proc; char wndclass_name[64];
  u64 ubuf;                              /* small scratch buffer in guest memory */
  int quit_posted;
  /* x87 / misc */
  u32 rand_state;
  /* process */
  ProcFs *fs; int pid, ppid; u64 str_cur;
  int block; int block_pids[8]; int nblock_pids; int block_pipe;
  int spawn_wait_pid, spawn_wait_mode;
  int pw_active, pw_fd; u64 pw_rip, pw_total, pw_done;         /* partially completed pipe write, resumed on shim retry */                        /* _spawn*(P_WAIT/P_OVERLAY) in progress */
};

void   w32_init(Win32 *w, Cpu *c, Mem *m);
void   w32_set_current(Win32 *w);
u64    w32_shim_slot(Win32 *w, const char *dll, const char *name);   /* returns guest address of the import trampoline */
int    w32_dispatch(Cpu *c, u64 addr);                              /* called by cpu when RIP enters the shim region */
u64    w32_heap_alloc(Win32 *w, u64 size, int zero);
int    w32_heap_free(Win32 *w, u64 addr);
u64    w32_heap_size(Win32 *w, u64 addr);
u64    w32_guest_strdup(Win32 *w, const char *s);
void   w32_report(Win32 *w, const char *fmt, ...);
void   w32_tick(Win32 *w);                                         /* called by host once per frame: input + timers */
void   w32_key(Win32 *w, int vk, int down);
void   w32_mouse(Win32 *w, int x, int y, int event);                 /* event: 0 move, 1 ldown, 2 lup, 3 rdown, 4 rup */
void   w32_start(Win32 *w, const char *cmdline);
void   w32_cont_resume(Cpu *c);
u64    w32_arg(Cpu *c, int n);
double w32_sleep_remaining_ms(Win32 *w);

/* ---------- loader ---------- */
int  pe_load(Win32 *w, const u8 *bytes, size_t len, const char *name);
/* static inspection (no execution): fills a JSON-ish report of arch/subsystem/imports and supported ratio */
int  pe_inspect(const u8 *bytes, size_t len, char *out, size_t out_sz);

/* tiny libc replacements (we are freestanding in wasm) */
size_t e_strlen(const char *s); int e_strcmp(const char *a, const char *b); int e_stricmp(const char *a, const char *b);
int e_strncmp(const char *a, const char *b, size_t n);
void *e_memcpy(void *d, const void *s, size_t n); void *e_memset(void *d, int c, size_t n); int e_memcmp(const void *a, const void *b, size_t n);
void *e_memmove(void *d, const void *s, size_t n);
char *e_strcpy(char *d, const char *s); char *e_strchr(const char *s, int c);
int e_snprintf(char *buf, size_t n, const char *fmt, ...);
int e_vsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap);
double e_strtod(const char *s, char **end); long long e_strtoll(const char *s, char **end, int base);
double e_sqrt(double x); double e_floor(double x); double e_ceil(double x); double e_fabs(double x); double e_fmod(double a, double b);
void e_log(const char *fmt, ...);                      /* emulator diagnostics → host fd 3 */


/* ---- vfs.c: in-memory file system + stdin queue ---- */
typedef struct { const char *path; const u8 *data; u64 size; int is_dir; int written; } VNodeInfo;
const char *vfs_cwd(void);
void vfs_normalize(const char *in, char *out, size_t cap);
int  vfs_find_node(const char *path);
int  vfs_add(const char *path, const u8 *data, u64 n);
int  vfs_mkdir(const char *path);
int  vfs_remove(const char *path);
int  vfs_rename(const char *from, const char *to);
int  vfs_chdir(const char *path);
int  vfs_stat(const char *path, u64 *size);
const VNodeInfo *vfs_node_info(int i, VNodeInfo *out);
int  vfs_node_count(void);
int  vfs_open(const char *path, int access, int disp, int append);
int  vfs_close(int fd);
int  vfs_is_null_dev(const char *path);
int  vfs_place_low(int fd);
int  vfs_set_inherit(int fd, int inherit);   /* returns previous flag or -1 */
int  vfs_is_open(int fd);
int  vfs_fd_node(int fd);
s64  vfs_read(int fd, u8 *buf, u64 n);
s64  vfs_write(int fd, const u8 *buf, u64 n);
s64  vfs_seek(int fd, s64 off, int whence);
s64  vfs_tell(int fd);
s64  vfs_size(int fd);
int  vfs_eof(int fd);
void vfs_clearerr(int fd);
int  vfs_ungetc(int fd, int ch);
int  vfs_truncate(int fd);
int  vfs_fd_isdir(int fd);
const char *vfs_fd_path(int fd);
int  vfs_dup(int fd);
int  vfs_dup2(int src, int dst);
int  vfs_resolve(int fd);
void vfs_close_all(void);
int  vfs_find_first(const char *pattern);
int  vfs_find_next(int h, char *name, size_t cap, u64 *size, int *is_dir);
void vfs_find_close(int h);
void vfs_stdin_push(const u8 *d, u64 n);
void vfs_stdin_eof(void);
int  vfs_stdin_waiting(void);
s64  vfs_stdin_read(u8 *buf, u64 n);
int  vfs_stdin_available(void);

#endif
