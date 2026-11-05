/* gltri.c — OpenGL through opengl32.dll: WGL context on a window, GL 3.3-style shaders + VBO/VAO triangle, a GL_QUADS draw with
   client-side vertex arrays (emulated by the bridge), glReadPixels verification and SwapBuffers. Build (x64 and x86):
   zig cc -target x86_64-windows-gnu -O1 -o gltri.exe gltri.c -luser32 -lgdi32 -lopengl32 */
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <string.h>
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C
typedef char GLchar; typedef ptrdiff_t GLsizeiptr;
typedef void (APIENTRY *P_GenBuffers)(GLsizei, GLuint *); typedef void (APIENTRY *P_BindBuffer)(GLenum, GLuint); typedef void (APIENTRY *P_BufferData)(GLenum, GLsizeiptr, const void *, GLenum);
typedef GLuint (APIENTRY *P_CreateShader)(GLenum); typedef void (APIENTRY *P_ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *); typedef void (APIENTRY *P_CompileShader)(GLuint);
typedef void (APIENTRY *P_GetShaderiv)(GLuint, GLenum, GLint *); typedef void (APIENTRY *P_GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef GLuint (APIENTRY *P_CreateProgram)(void); typedef void (APIENTRY *P_AttachShader)(GLuint, GLuint); typedef void (APIENTRY *P_LinkProgram)(GLuint); typedef void (APIENTRY *P_GetProgramiv)(GLuint, GLenum, GLint *); typedef void (APIENTRY *P_UseProgram)(GLuint);
typedef void (APIENTRY *P_GenVertexArrays)(GLsizei, GLuint *); typedef void (APIENTRY *P_BindVertexArray)(GLuint); typedef void (APIENTRY *P_EnableVertexAttribArray)(GLuint); typedef void (APIENTRY *P_VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
typedef GLint (APIENTRY *P_GetUniformLocation)(GLuint, const GLchar *); typedef void (APIENTRY *P_Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat); typedef GLint (APIENTRY *P_GetAttribLocation)(GLuint, const GLchar *);
typedef const char *(WINAPI *P_wglGetExtensionsStringARB)(HDC);
static void *gp(const char *n) { void *p = (void *)wglGetProcAddress(n); if (!p) printf("missing: %s\n", n); return p; }
static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) { if (m == WM_DESTROY) { PostQuitMessage(0); return 0; } return DefWindowProcA(h, m, w, l); }
static const char *vs_src = "#version 330 core\nlayout(location = 0) in vec2 pos; layout(location = 1) in vec3 col; out vec3 vcol; void main() { vcol = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *fs_src = "#version 330 core\nin vec3 vcol; out vec4 frag; uniform vec4 tint; void main() { frag = vec4(vcol, 1.0) * tint; }\n";
static const char *vs_legacy = "#version 120\nattribute vec2 pos; void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *fs_legacy = "#version 120\nvoid main() { gl_FragColor = vec4(1.0, 1.0, 0.0, 1.0); }\n";
static GLuint shader(P_CreateShader cs, P_ShaderSource ss, P_CompileShader cc, P_GetShaderiv gi, P_GetShaderInfoLog il, GLenum type, const char *src) {
  GLuint s = cs(type); ss(s, 1, &src, 0); cc(s); GLint ok = 0; gi(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[1024]; GLsizei n = 0; il(s, sizeof log, &n, log); printf("shader compile failed: %.*s\n", n, log); } return s; }
int main(void) {
  WNDCLASSA wc = {0}; wc.lpfnWndProc = wp; wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "gltri"; wc.style = CS_OWNDC; RegisterClassA(&wc);
  HWND h = CreateWindowA("gltri", "OpenGL triangle", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, 0, 0, wc.hInstance, 0);
  HDC dc = GetDC(h);
  PIXELFORMATDESCRIPTOR pfd = { sizeof pfd, 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 }; pfd.cDepthBits = 24; pfd.cStencilBits = 8;
  int pf = ChoosePixelFormat(dc, &pfd); printf("ChoosePixelFormat=%d\n", pf); if (!pf) return 2;
  PIXELFORMATDESCRIPTOR got; DescribePixelFormat(dc, pf, sizeof got, &got); printf("pixel format: color=%d depth=%d stencil=%d double=%d\n", got.cColorBits, got.cDepthBits, got.cStencilBits, (got.dwFlags & PFD_DOUBLEBUFFER) != 0);
  if (!SetPixelFormat(dc, pf, &pfd)) { printf("SetPixelFormat failed %lu\n", GetLastError()); return 3; }
  HGLRC rc = wglCreateContext(dc); if (!rc) { printf("wglCreateContext failed (%lu): no OpenGL on this host\n", GetLastError()); return 4; }
  if (!wglMakeCurrent(dc, rc)) { printf("wglMakeCurrent failed\n"); return 5; }
  printf("GL_VERSION=%s\nGL_RENDERER=%s\nGL_VENDOR=%s\nGLSL=%s\n", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VENDOR), (const char *)glGetString(0x8B8C));
  GLint maj = 0, min = 0; glGetIntegerv(GL_MAJOR_VERSION, &maj); glGetIntegerv(GL_MINOR_VERSION, &min); printf("context %d.%d\n", maj, min);
  P_wglGetExtensionsStringARB wglExt = (P_wglGetExtensionsStringARB)gp("wglGetExtensionsStringARB"); if (wglExt) printf("WGL: %s\n", wglExt(dc));
  const char *ext = (const char *)glGetString(GL_EXTENSIONS); printf("GL_EXTENSIONS has ARB_vertex_buffer_object: %d, framebuffer_object: %d\n", ext && strstr(ext, "GL_ARB_vertex_buffer_object") != 0, ext && strstr(ext, "GL_ARB_framebuffer_object") != 0);
  printf("unknown entry point: %p\n", (void *)wglGetProcAddress("glNebulaDoesNotExist"));
  P_GenBuffers GenBuffers = (P_GenBuffers)gp("glGenBuffers"); P_BindBuffer BindBuffer = (P_BindBuffer)gp("glBindBuffer"); P_BufferData BufferData = (P_BufferData)gp("glBufferData");
  P_CreateShader CreateShader = (P_CreateShader)gp("glCreateShader"); P_ShaderSource ShaderSource = (P_ShaderSource)gp("glShaderSource"); P_CompileShader CompileShader = (P_CompileShader)gp("glCompileShader"); P_GetShaderiv GetShaderiv = (P_GetShaderiv)gp("glGetShaderiv"); P_GetShaderInfoLog GetShaderInfoLog = (P_GetShaderInfoLog)gp("glGetShaderInfoLog");
  P_CreateProgram CreateProgram = (P_CreateProgram)gp("glCreateProgram"); P_AttachShader AttachShader = (P_AttachShader)gp("glAttachShader"); P_LinkProgram LinkProgram = (P_LinkProgram)gp("glLinkProgram"); P_GetProgramiv GetProgramiv = (P_GetProgramiv)gp("glGetProgramiv"); P_UseProgram UseProgram = (P_UseProgram)gp("glUseProgram");
  P_GenVertexArrays GenVertexArrays = (P_GenVertexArrays)gp("glGenVertexArrays"); P_BindVertexArray BindVertexArray = (P_BindVertexArray)gp("glBindVertexArray"); P_EnableVertexAttribArray EnableVertexAttribArray = (P_EnableVertexAttribArray)gp("glEnableVertexAttribArray"); P_VertexAttribPointer VertexAttribPointer = (P_VertexAttribPointer)gp("glVertexAttribPointer");
  P_GetUniformLocation GetUniformLocation = (P_GetUniformLocation)gp("glGetUniformLocation"); P_Uniform4f Uniform4f = (P_Uniform4f)gp("glUniform4f"); P_GetAttribLocation GetAttribLocation = (P_GetAttribLocation)gp("glGetAttribLocation");
  if (!GenBuffers || !CreateShader || !GenVertexArrays || !Uniform4f) { printf("core entry points missing\n"); return 6; }
  /* triangle: VAO + VBO, GLSL 330 */
  GLuint vao = 0, vbo = 0; GenVertexArrays(1, &vao); BindVertexArray(vao); GenBuffers(1, &vbo); BindBuffer(GL_ARRAY_BUFFER, vbo);
  const float tri[] = { -0.8f, -0.8f, 1, 0, 0,   0.8f, -0.8f, 0, 1, 0,   0.0f, 0.8f, 0, 0, 1 }; BufferData(GL_ARRAY_BUFFER, sizeof tri, tri, GL_STATIC_DRAW);
  EnableVertexAttribArray(0); VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 20, (void *)0); EnableVertexAttribArray(1); VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 20, (void *)8);
  GLuint prog = CreateProgram(); AttachShader(prog, shader(CreateShader, ShaderSource, CompileShader, GetShaderiv, GetShaderInfoLog, GL_VERTEX_SHADER, vs_src)); AttachShader(prog, shader(CreateShader, ShaderSource, CompileShader, GetShaderiv, GetShaderInfoLog, GL_FRAGMENT_SHADER, fs_src)); LinkProgram(prog);
  GLint linked = 0; GetProgramiv(prog, GL_LINK_STATUS, &linked); printf("program 330 linked=%d\n", linked);
  /* legacy GLSL 120 program (attribute/gl_FragColor) drawing a GL_QUADS quad from a client-side array: both are translated/emulated by the bridge */
  GLuint prog2 = CreateProgram(); AttachShader(prog2, shader(CreateShader, ShaderSource, CompileShader, GetShaderiv, GetShaderInfoLog, GL_VERTEX_SHADER, vs_legacy)); AttachShader(prog2, shader(CreateShader, ShaderSource, CompileShader, GetShaderiv, GetShaderInfoLog, GL_FRAGMENT_SHADER, fs_legacy)); LinkProgram(prog2);
  GetProgramiv(prog2, GL_LINK_STATUS, &linked); printf("program 120 linked=%d\n", linked); GLint posLoc = GetAttribLocation(prog2, "pos"); printf("attrib pos=%d\n", posLoc);
  GLuint vao2 = 0; GenVertexArrays(1, &vao2); BindVertexArray(vao2); BindBuffer(GL_ARRAY_BUFFER, 0);
  static const float quad[] = { 0.55f, 0.55f,  0.95f, 0.55f,  0.95f, 0.95f,  0.55f, 0.95f }; EnableVertexAttribArray(posLoc); VertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, quad);
  glBegin(GL_TRIANGLES); glEnd(); printf("glBegin/glEnd error=0x%x (fixed-function layer: 0x0)\n", glGetError());
  glViewport(0, 0, 640, 480);
  for (int frame = 0; frame < 3; frame++) {
    glClearColor(0.1f, 0.1f, 0.3f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    UseProgram(prog); Uniform4f(GetUniformLocation(prog, "tint"), 1, 1, 1, 1); BindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3);
    UseProgram(prog2); BindVertexArray(vao2); glDrawArrays(GL_QUADS, 0, 4);
    GLenum e = glGetError(); if (e) printf("frame %d: GL error 0x%x\n", frame, e);
    unsigned char px[4] = {0}; glReadPixels(320, 100, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px); unsigned char qx[4] = {0}; glReadPixels(600, 440, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, qx); unsigned char bg[4] = {0}; glReadPixels(5, 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bg);
    if (frame == 0) printf("pixels: triangle=(%d,%d,%d) quad=(%d,%d,%d) background=(%d,%d,%d)\n", px[0], px[1], px[2], qx[0], qx[1], qx[2], bg[0], bg[1], bg[2]);
    SwapBuffers(dc); MSG m; while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
    if (frame == 0) { int okTri = px[0] + px[1] + px[2] > 60 && bg[0] < 60 && bg[2] > 50, okQuad = qx[0] > 200 && qx[1] > 200 && qx[2] < 60; printf(okTri && okQuad ? "frame 0: triangle + GL_QUADS quad drawn and presented\n" : "frame 0: rendering mismatch (tri=%d quad=%d)\n", okTri, okQuad); }
  }
  Sleep(600);
  wglMakeCurrent(0, 0); wglDeleteContext(rc); ReleaseDC(h, dc); printf("gltri ok\n"); return 0; }   /* the window stays until exit so the last frame remains visible */
