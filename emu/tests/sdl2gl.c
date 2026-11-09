/* sdl2gl: the legal stand-in for the "SDL2 + OpenGL + OpenAL + VC++ runtime" stack that many 32-bit indie games use
 * (SteamWorld Dig, FTL, Braid, Bastion, ...). Everything is loaded at run time with LoadLibrary/GetProcAddress exactly
 * like such games do, from the OFFICIAL binaries put next to the exe: SDL2.dll (libsdl.org / github libsdl-org) and
 * OpenAL32.dll (openal-soft). No import libraries are needed, so this builds with `zig cc -target x86-windows-gnu`
 * (and x86_64-windows-gnu for the 64-bit DLLs).
 *   argv[1]  frames to render (default 90)
 *   argv[2]  "noal" to skip OpenAL, "nogl" to skip the GL context (software window only)
 * Prints one "sdl2gl: ..." line per stage and "SDL2GL PASS" at the end. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef struct { unsigned int type; unsigned char pad[60]; } SDL_Event;   /* SDL2 events are 56 bytes; keep slack */
typedef struct { int freq; unsigned short format; unsigned char channels, silence; unsigned short samples, padding; unsigned int size; void (*callback)(void *, unsigned char *, int); void *userdata; } SDL_AudioSpec;

static int (*p_SDL_Init)(unsigned int);
static void (*p_SDL_Quit)(void);
static const char *(*p_SDL_GetError)(void);
static void *(*p_SDL_CreateWindow)(const char *, int, int, int, int, unsigned int);
static void (*p_SDL_DestroyWindow)(void *);
static void *(*p_SDL_GL_CreateContext)(void *);
static void (*p_SDL_GL_DeleteContext)(void *);
static int (*p_SDL_GL_SetAttribute)(int, int);
static void (*p_SDL_GL_SwapWindow)(void *);
static void *(*p_SDL_GL_GetProcAddress)(const char *);
static int (*p_SDL_PollEvent)(SDL_Event *);
static void (*p_SDL_Delay)(unsigned int);
static unsigned int (*p_SDL_GetTicks)(void);
static const char *(*p_SDL_GetCurrentVideoDriver)(void);
static const char *(*p_SDL_GetCurrentAudioDriver)(void);
static unsigned int (*p_SDL_OpenAudioDevice)(const char *, int, const SDL_AudioSpec *, SDL_AudioSpec *, int);
static void (*p_SDL_PauseAudioDevice)(unsigned int, int);
static void (*p_SDL_CloseAudioDevice)(unsigned int);
static void (*p_SDL_GetVersion)(unsigned char *);
static int (*p_SDL_GetNumVideoDisplays)(void);
static int (*p_SDL_GetDesktopDisplayMode)(int, int *);
static void *(*p_SDL_GetWindowSurface)(void *);
static int (*p_SDL_UpdateWindowSurface)(void *);
static int (*p_SDL_FillRect)(void *, const void *, unsigned int);

static void (__stdcall *gl_ClearColor)(float, float, float, float);
static void (__stdcall *gl_Clear)(unsigned int);
static const unsigned char *(__stdcall *gl_GetString)(unsigned int);
static void (__stdcall *gl_Viewport)(int, int, int, int);
static void (__stdcall *gl_GetIntegerv)(unsigned int, int *);

static int audio_pos;
static void audio_cb(void *ud, unsigned char *stream, int len) {
  (void)ud; short *s = (short *)stream; int n = len / 2;
  for (int i = 0; i < n; i++) { s[i] = (short)(sin(audio_pos * 2 * 3.14159265 * 440 / 44100) * 8000); audio_pos++; }
}

#define LOADP(h, name) do { *(FARPROC *)&p_##name = GetProcAddress(h, #name); if (!p_##name) { printf("sdl2gl: missing export %s\n", #name); return 2; } } while (0)

static int do_openal(void) {
  HMODULE al = LoadLibraryA("OpenAL32.dll");
  if (!al) { printf("sdl2gl: OpenAL32.dll not found (%lu) - skipped\n", GetLastError()); return 0; }
  void *(__cdecl *alcOpenDevice)(const char *) = (void *)GetProcAddress(al, "alcOpenDevice");
  void *(__cdecl *alcCreateContext)(void *, const int *) = (void *)GetProcAddress(al, "alcCreateContext");
  char (__cdecl *alcMakeContextCurrent)(void *) = (void *)GetProcAddress(al, "alcMakeContextCurrent");
  void (__cdecl *alcDestroyContext)(void *) = (void *)GetProcAddress(al, "alcDestroyContext");
  char (__cdecl *alcCloseDevice)(void *) = (void *)GetProcAddress(al, "alcCloseDevice");
  const char *(__cdecl *alcGetString)(void *, int) = (void *)GetProcAddress(al, "alcGetString");
  void (__cdecl *alGenBuffers)(int, unsigned *) = (void *)GetProcAddress(al, "alGenBuffers");
  void (__cdecl *alBufferData)(unsigned, int, const void *, int, int) = (void *)GetProcAddress(al, "alBufferData");
  void (__cdecl *alGenSources)(int, unsigned *) = (void *)GetProcAddress(al, "alGenSources");
  void (__cdecl *alSourcei)(unsigned, int, int) = (void *)GetProcAddress(al, "alSourcei");
  void (__cdecl *alSourcePlay)(unsigned) = (void *)GetProcAddress(al, "alSourcePlay");
  void (__cdecl *alGetSourcei)(unsigned, int, int *) = (void *)GetProcAddress(al, "alGetSourcei");
  int (__cdecl *alGetError)(void) = (void *)GetProcAddress(al, "alGetError");
  const char *(__cdecl *alGetString)(int) = (void *)GetProcAddress(al, "alGetString");
  if (!alcOpenDevice || !alcCreateContext || !alcMakeContextCurrent || !alGenBuffers || !alBufferData || !alGenSources || !alSourcei || !alSourcePlay || !alGetSourcei) { puts("sdl2gl: OpenAL exports missing"); return 3; }
  void *dev = alcOpenDevice(NULL);
  if (!dev) { puts("sdl2gl: alcOpenDevice failed"); return 4; }
  printf("sdl2gl: OpenAL device '%s'\n", alcGetString ? alcGetString(dev, 0x1005 /*ALC_DEVICE_SPECIFIER*/) : "?");
  void *ctx = alcCreateContext(dev, NULL);
  if (!ctx || !alcMakeContextCurrent(ctx)) { puts("sdl2gl: alcCreateContext failed"); return 5; }
  printf("sdl2gl: OpenAL renderer '%s' version '%s'\n", alGetString ? alGetString(0xB003) : "?", alGetString ? alGetString(0xB002) : "?");
  static short pcm[44100 / 2]; for (int i = 0; i < 44100 / 2; i++) pcm[i] = (short)(sin(i * 2 * 3.14159265 * 660 / 44100) * 9000);
  unsigned buf = 0, src = 0; alGenBuffers(1, &buf); alBufferData(buf, 0x1101 /*AL_FORMAT_MONO16*/, pcm, sizeof pcm, 44100);
  alGenSources(1, &src); alSourcei(src, 0x1009 /*AL_BUFFER*/, (int)buf); alSourcePlay(src);
  p_SDL_Delay(300);
  int state = 0; alGetSourcei(src, 0x1010 /*AL_SOURCE_STATE*/, &state);
  printf("sdl2gl: OpenAL source state 0x%x (0x1012 = playing) error 0x%x\n", state, alGetError ? alGetError() : 0);
  if (state != 0x1012) return 6;
  alcMakeContextCurrent(NULL); if (alcDestroyContext) alcDestroyContext(ctx); if (alcCloseDevice) alcCloseDevice(dev);
  puts("sdl2gl: OpenAL tone played");
  return 0;
}

int main(int argc, char **argv) {
  int frames = argc > 1 ? atoi(argv[1]) : 90; if (frames <= 0) frames = 90;
  int noal = argc > 2 && !strcmp(argv[2], "noal"), nogl = argc > 2 && !strcmp(argv[2], "nogl");
  HMODULE sdl = LoadLibraryA("SDL2.dll");
  if (!sdl) { printf("sdl2gl: LoadLibrary(SDL2.dll) failed: %lu\n", GetLastError()); return 1; }
  printf("sdl2gl: SDL2.dll at %p (%u-bit)\n", (void *)sdl, (unsigned)(sizeof(void *) * 8));
  LOADP(sdl, SDL_Init); LOADP(sdl, SDL_Quit); LOADP(sdl, SDL_GetError); LOADP(sdl, SDL_CreateWindow); LOADP(sdl, SDL_DestroyWindow);
  LOADP(sdl, SDL_GL_CreateContext); LOADP(sdl, SDL_GL_DeleteContext); LOADP(sdl, SDL_GL_SetAttribute); LOADP(sdl, SDL_GL_SwapWindow); LOADP(sdl, SDL_GL_GetProcAddress);
  LOADP(sdl, SDL_PollEvent); LOADP(sdl, SDL_Delay); LOADP(sdl, SDL_GetTicks); LOADP(sdl, SDL_GetCurrentVideoDriver); LOADP(sdl, SDL_GetCurrentAudioDriver);
  LOADP(sdl, SDL_OpenAudioDevice); LOADP(sdl, SDL_PauseAudioDevice); LOADP(sdl, SDL_CloseAudioDevice); LOADP(sdl, SDL_GetVersion);
  LOADP(sdl, SDL_GetNumVideoDisplays); LOADP(sdl, SDL_GetDesktopDisplayMode); LOADP(sdl, SDL_GetWindowSurface); LOADP(sdl, SDL_UpdateWindowSurface); LOADP(sdl, SDL_FillRect);
  unsigned char ver[4] = {0}; p_SDL_GetVersion(ver); printf("sdl2gl: SDL version %u.%u.%u\n", ver[0], ver[1], ver[2]);
  if (p_SDL_Init(0x20 /*VIDEO*/ | 0x10 /*AUDIO*/ | 0x01 /*TIMER*/ | 0x4000 /*EVENTS*/) != 0) { printf("sdl2gl: SDL_Init failed: %s\n", p_SDL_GetError()); return 3; }
  printf("sdl2gl: SDL_Init ok video='%s' audio='%s' displays=%d\n", p_SDL_GetCurrentVideoDriver(), p_SDL_GetCurrentAudioDriver() ? p_SDL_GetCurrentAudioDriver() : "(none)", p_SDL_GetNumVideoDisplays());
  int mode[4] = {0}; if (p_SDL_GetDesktopDisplayMode(0, mode) == 0) printf("sdl2gl: desktop mode %dx%d format 0x%x\n", mode[1], mode[2], mode[0]);
  p_SDL_GL_SetAttribute(5 /*DOUBLEBUFFER*/, 1); p_SDL_GL_SetAttribute(6 /*DEPTH_SIZE*/, 16);
  void *win = p_SDL_CreateWindow("sdl2gl - SDL2 window", 0x1FFF0000, 0x1FFF0000, 640, 480, (nogl ? 0 : 0x2 /*OPENGL*/) | 0x4 /*SHOWN*/);
  if (!win) { printf("sdl2gl: SDL_CreateWindow failed: %s\n", p_SDL_GetError()); return 4; }
  puts("sdl2gl: window created");
  void *glc = NULL;
  if (!nogl) {
    glc = p_SDL_GL_CreateContext(win);
    if (!glc) { printf("sdl2gl: SDL_GL_CreateContext failed: %s\n", p_SDL_GetError()); return 5; }
    *(void **)&gl_ClearColor = p_SDL_GL_GetProcAddress("glClearColor"); *(void **)&gl_Clear = p_SDL_GL_GetProcAddress("glClear");
    *(void **)&gl_GetString = p_SDL_GL_GetProcAddress("glGetString"); *(void **)&gl_Viewport = p_SDL_GL_GetProcAddress("glViewport"); *(void **)&gl_GetIntegerv = p_SDL_GL_GetProcAddress("glGetIntegerv");
    if (!gl_ClearColor || !gl_Clear || !gl_GetString) { puts("sdl2gl: SDL_GL_GetProcAddress(glClear...) failed"); return 6; }
    printf("sdl2gl: GL '%s' / '%s' / '%s'\n", gl_GetString(0x1F00), gl_GetString(0x1F01), gl_GetString(0x1F02));
    int vp[4] = {0}; if (gl_GetIntegerv) gl_GetIntegerv(0x0BA2, vp); printf("sdl2gl: viewport %d %d %d %d\n", vp[0], vp[1], vp[2], vp[3]);
  }
  SDL_AudioSpec want, have; memset(&want, 0, sizeof want); memset(&have, 0, sizeof have);
  want.freq = 44100; want.format = 0x8010 /*S16LSB*/; want.channels = 1; want.samples = 1024; want.callback = audio_cb;
  unsigned dev = p_SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (dev) { p_SDL_PauseAudioDevice(dev, 0); printf("sdl2gl: SDL audio device %u opened freq=%d fmt=0x%x ch=%u samples=%u\n", dev, have.freq, have.format, have.channels, have.samples); }
  else printf("sdl2gl: SDL_OpenAudioDevice failed: %s\n", p_SDL_GetError());
  int rendered = 0, events = 0, quit = 0; unsigned t0 = p_SDL_GetTicks();
  for (int f = 0; f < frames && !quit; f++) {
    SDL_Event ev; while (p_SDL_PollEvent(&ev)) { events++; if (ev.type == 0x100 /*QUIT*/) quit = 1; }
    if (!nogl) { gl_ClearColor((f % 60) / 60.0f, 0.3f, 1.0f - (f % 60) / 60.0f, 1); gl_Clear(0x4000 | 0x100); p_SDL_GL_SwapWindow(win); }
    else { void *surf = p_SDL_GetWindowSurface(win); if (surf) { p_SDL_FillRect(surf, NULL, 0xFF000000u | ((f * 4) & 0xFF) << 16 | 0x4080); p_SDL_UpdateWindowSurface(win); } }
    rendered++; p_SDL_Delay(16);
  }
  printf("sdl2gl: rendered %d frames in %u ms, %d events, audio callback produced %d samples\n", rendered, p_SDL_GetTicks() - t0, events, audio_pos);
  if (dev) p_SDL_CloseAudioDevice(dev);
  int alrc = noal ? 0 : do_openal();
  if (glc) p_SDL_GL_DeleteContext(glc);
  p_SDL_DestroyWindow(win); p_SDL_Quit();
  if (alrc) { printf("sdl2gl: OpenAL stage failed (%d)\n", alrc); return 20 + alrc; }
  if (rendered < frames) { puts("sdl2gl: FAIL (quit early)"); return 7; }
  if (dev && audio_pos == 0) { puts("sdl2gl: FAIL (SDL audio callback never ran)"); return 8; }
  puts("SDL2GL PASS");
  return 0;
}
