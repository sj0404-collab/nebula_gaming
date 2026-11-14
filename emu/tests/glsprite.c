/* glsprite.c — the exact OpenGL path of a 2D sprite engine (SteamWorld Dig's GOG build, SDL2 + GL 2.0 entry points): legacy WGL
   context, GLSL shaders without #version that `#define lowp/mediump/highp` away, glBindAttribLocation before link, shaders detached
   and deleted after linking, NPOT RGBA textures uploaded as GL_UNSIGNED_INT_8_8_8_8_REV, one streaming VBO refilled with
   glBufferSubData every frame (interleaved pos 3f @0 · colour 4ub normalized @12 · uv 2f @16, stride 24), a static GL_UNSIGNED_SHORT
   index buffer, premultiplied blending (ONE, ONE_MINUS_SRC_ALPHA) over a transparent clear, an untextured colour program, and one
   draw from client-side arrays.  Every quad is read back with glReadPixels and compared with the expected colour, so a zero
   texcoord (corner texel), a zero colour attribute, a wrong upload or a wrong byte order is named in the output.
   Build (x64 and x86):
   zig cc -target x86_64-windows-gnu -O1 -o glsprite.exe glsprite.c -luser32 -lgdi32 -lopengl32
   zig cc -target x86-windows-gnu    -O1 -o glsprite32.exe glsprite.c -luser32 -lgdi32 -lopengl32 */
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <string.h>
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FUNC_ADD 0x8006
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#define GL_TEXTURE0 0x84C0
typedef char GLchar; typedef ptrdiff_t GLsizeiptr; typedef ptrdiff_t GLintptr;
typedef void (APIENTRY *P_GenBuffers)(GLsizei, GLuint *); typedef void (APIENTRY *P_BindBuffer)(GLenum, GLuint); typedef void (APIENTRY *P_BufferData)(GLenum, GLsizeiptr, const void *, GLenum); typedef void (APIENTRY *P_BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void *);
typedef GLuint (APIENTRY *P_CreateShader)(GLenum); typedef void (APIENTRY *P_ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *); typedef void (APIENTRY *P_CompileShader)(GLuint);
typedef void (APIENTRY *P_GetShaderiv)(GLuint, GLenum, GLint *); typedef void (APIENTRY *P_GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *); typedef void (APIENTRY *P_DeleteShader)(GLuint); typedef void (APIENTRY *P_DetachShader)(GLuint, GLuint);
typedef GLuint (APIENTRY *P_CreateProgram)(void); typedef void (APIENTRY *P_AttachShader)(GLuint, GLuint); typedef void (APIENTRY *P_LinkProgram)(GLuint); typedef void (APIENTRY *P_GetProgramiv)(GLuint, GLenum, GLint *); typedef void (APIENTRY *P_UseProgram)(GLuint);
typedef void (APIENTRY *P_BindAttribLocation)(GLuint, GLuint, const GLchar *); typedef void (APIENTRY *P_EnableVertexAttribArray)(GLuint); typedef void (APIENTRY *P_DisableVertexAttribArray)(GLuint); typedef void (APIENTRY *P_VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
typedef GLint (APIENTRY *P_GetUniformLocation)(GLuint, const GLchar *); typedef void (APIENTRY *P_Uniform1i)(GLint, GLint); typedef void (APIENTRY *P_UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void (APIENTRY *P_ActiveTexture)(GLenum); typedef void (APIENTRY *P_BlendEquation)(GLenum);
static int missing;
static void *gp(const char *n) { void *p = (void *)wglGetProcAddress(n); if (!p) { printf("missing: %s\n", n); missing++; } return p; }
static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) { if (m == WM_DESTROY) { PostQuitMessage(0); return 0; } return DefWindowProcA(h, m, w, l); }
/* the game's shaders, verbatim in spirit: no #version, desktop precision keywords defined away */
static const char *vs_tex =
  "#define lowp \n#define mediump \n#define highp \n\n"
  "attribute highp vec4 a_position;\nattribute highp vec2 a_texCoord;\nattribute highp vec4 a_color;\n\n"
  "uniform mat4 u_MVMatrix;\nuniform mat4 u_PMatrix;\n\nvarying lowp vec4 v_fragmentColor;\nvarying highp vec2 v_texCoord;\n\n"
  "void main()\n{\n\tgl_Position = u_PMatrix * (u_MVMatrix * a_position);\n\tv_fragmentColor = a_color;\n\tv_texCoord = a_texCoord;\n}\n";
static const char *fs_tex =
  "#define lowp \n#define mediump \n#define highp \n\n"
  "uniform sampler2D u_texture;\n\nvarying lowp vec4 v_fragmentColor;\nvarying highp vec2 v_texCoord;\n\n"
  "void main()\n{\n\tgl_FragColor = v_fragmentColor * texture2D(u_texture, v_texCoord);\n}\n";
static const char *vs_col =
  "#define lowp \n#define mediump \n#define highp \n\n"
  "attribute highp vec4 a_position;\nattribute highp vec4 a_color;\n\nuniform\t\tmat4 u_MVMatrix;\nuniform\t\tmat4 u_PMatrix;\n\nvarying mediump vec4 v_fragmentColor;\n\n"
  "void main()\n{\n\tgl_Position = u_PMatrix * (u_MVMatrix * a_position);\n\tv_fragmentColor = a_color;\n}\n";
static const char *fs_col =
  "#define lowp \n#define mediump \n#define highp \n\nvarying mediump vec4 v_fragmentColor;\n\nvoid main()\n{\n\tgl_FragColor = v_fragmentColor;\n}\n";
typedef struct { float x, y, z; unsigned char r, g, b, a; float u, v; } Vtx;   /* 24 bytes */
static P_CreateShader CreateShader; static P_ShaderSource ShaderSource; static P_CompileShader CompileShader; static P_GetShaderiv GetShaderiv; static P_GetShaderInfoLog GetShaderInfoLog; static P_DeleteShader DeleteShader; static P_DetachShader DetachShader;
static P_CreateProgram CreateProgram; static P_AttachShader AttachShader; static P_LinkProgram LinkProgram; static P_GetProgramiv GetProgramiv; static P_UseProgram UseProgram; static P_BindAttribLocation BindAttribLocation;
static GLuint shader(GLenum type, const char *src) {
  GLuint s = CreateShader(type); GLint len = (GLint)strlen(src); ShaderSource(s, 1, &src, &len); CompileShader(s); GLint ok = 0; GetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[1024]; GLsizei n = 0; GetShaderInfoLog(s, sizeof log, &n, log); printf("shader compile failed: %.*s\n", n, log); } return s; }
static GLuint program(const char *vs, const char *fs, int textured) {
  GLuint p = CreateProgram(), v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs); AttachShader(p, v); AttachShader(p, f);
  BindAttribLocation(p, 0, "a_position"); if (textured) BindAttribLocation(p, 1, "a_texCoord"); BindAttribLocation(p, textured ? 2 : 1, "a_color");
  LinkProgram(p); GLint linked = 0; GetProgramiv(p, GL_LINK_STATUS, &linked); printf("program %s linked=%d\n", textured ? "textured" : "colour", linked);
  DetachShader(p, v); DetachShader(p, f); DeleteShader(v); DeleteShader(f);   /* the game keeps only the program objects */
  return p; }
static void quad(Vtx *q, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
  Vtx v[4] = { { x0, y0, 0, r, g, b, a, u0, v0 }, { x1, y0, 0, r, g, b, a, u1, v0 }, { x1, y1, 0, r, g, b, a, u1, v1 }, { x0, y1, 0, r, g, b, a, u0, v1 } }; memcpy(q, v, sizeof v); }
static int near3(const unsigned char *px, int r, int g, int b) { return abs(px[0] - r) < 40 && abs(px[1] - g) < 40 && abs(px[2] - b) < 40; }
int main(void) {
  WNDCLASSA wc = {0}; wc.lpfnWndProc = wp; wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "glsprite"; wc.style = CS_OWNDC; RegisterClassA(&wc);
  HWND h = CreateWindowA("glsprite", "sprite engine path", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, 0, 0, wc.hInstance, 0);
  HDC dc = GetDC(h);
  PIXELFORMATDESCRIPTOR pfd = { sizeof pfd, 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 }; pfd.cDepthBits = 24; pfd.cStencilBits = 8;
  int pf = ChoosePixelFormat(dc, &pfd); if (!pf || !SetPixelFormat(dc, pf, &pfd)) { printf("SetPixelFormat failed %lu\n", GetLastError()); return 3; }
  HGLRC rc = wglCreateContext(dc); if (!rc) { printf("wglCreateContext failed (%lu): no OpenGL on this host\n", GetLastError()); return 4; }
  if (!wglMakeCurrent(dc, rc)) { printf("wglMakeCurrent failed\n"); return 5; }
  printf("GL_VERSION=%s\n", (const char *)glGetString(GL_VERSION));
  P_GenBuffers GenBuffers = (P_GenBuffers)gp("glGenBuffers"); P_BindBuffer BindBuffer = (P_BindBuffer)gp("glBindBuffer"); P_BufferData BufferData = (P_BufferData)gp("glBufferData"); P_BufferSubData BufferSubData = (P_BufferSubData)gp("glBufferSubData");
  CreateShader = (P_CreateShader)gp("glCreateShader"); ShaderSource = (P_ShaderSource)gp("glShaderSource"); CompileShader = (P_CompileShader)gp("glCompileShader"); GetShaderiv = (P_GetShaderiv)gp("glGetShaderiv"); GetShaderInfoLog = (P_GetShaderInfoLog)gp("glGetShaderInfoLog"); DeleteShader = (P_DeleteShader)gp("glDeleteShader"); DetachShader = (P_DetachShader)gp("glDetachShader");
  CreateProgram = (P_CreateProgram)gp("glCreateProgram"); AttachShader = (P_AttachShader)gp("glAttachShader"); LinkProgram = (P_LinkProgram)gp("glLinkProgram"); GetProgramiv = (P_GetProgramiv)gp("glGetProgramiv"); UseProgram = (P_UseProgram)gp("glUseProgram"); BindAttribLocation = (P_BindAttribLocation)gp("glBindAttribLocation");
  P_EnableVertexAttribArray EnableVertexAttribArray = (P_EnableVertexAttribArray)gp("glEnableVertexAttribArray"); P_DisableVertexAttribArray DisableVertexAttribArray = (P_DisableVertexAttribArray)gp("glDisableVertexAttribArray"); P_VertexAttribPointer VertexAttribPointer = (P_VertexAttribPointer)gp("glVertexAttribPointer");
  P_GetUniformLocation GetUniformLocation = (P_GetUniformLocation)gp("glGetUniformLocation"); P_Uniform1i Uniform1i = (P_Uniform1i)gp("glUniform1i"); P_UniformMatrix4fv UniformMatrix4fv = (P_UniformMatrix4fv)gp("glUniformMatrix4fv");
  P_ActiveTexture ActiveTexture = (P_ActiveTexture)gp("glActiveTexture"); P_BlendEquation BlendEquation = (P_BlendEquation)gp("glBlendEquation");
  if (missing) { printf("core entry points missing\n"); return 6; }
  /* texture 70x46 (NPOT): a 2x2 transparent corner at texel (0,0), the left half green, the right half blue — uploaded as 8_8_8_8_REV (bytes R,G,B,A in memory) */
  enum { TW = 70, TH = 46 }; static unsigned char tex[TW * TH * 4];
  for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) { unsigned char *p = tex + (y * TW + x) * 4; if (x < 2 && y < 2) { p[0] = p[1] = p[2] = p[3] = 0; } else if (x < TW / 2) { p[0] = 0; p[1] = 255; p[2] = 0; p[3] = 255; } else { p[0] = 0; p[1] = 0; p[2] = 255; p[3] = 255; } }
  GLuint t = 0; glGenTextures(1, &t); ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TW, TH, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, tex);
  printf("texture upload error=0x%x\n", glGetError());
  GLuint ptex = program(vs_tex, fs_tex, 1), pcol = program(vs_col, fs_col, 0);
  GLint uTex = GetUniformLocation(ptex, "u_texture"), uP = GetUniformLocation(ptex, "u_PMatrix"), uMV = GetUniformLocation(ptex, "u_MVMatrix"), cP = GetUniformLocation(pcol, "u_PMatrix"), cMV = GetUniformLocation(pcol, "u_MVMatrix");
  printf("uniforms: u_texture=%d u_PMatrix=%d u_MVMatrix=%d | colour: u_PMatrix=%d u_MVMatrix=%d\n", uTex, uP, uMV, cP, cMV);
  UseProgram(ptex); Uniform1i(uTex, 0);
  /* streaming VBO (refilled every frame), static USHORT index buffer */
  GLuint vbo = 0, ibo = 0, vbo2 = 0, vbo3 = 0, ibo2 = 0; GenBuffers(1, &vbo); GenBuffers(1, &ibo); GenBuffers(1, &vbo2); GenBuffers(1, &vbo3); GenBuffers(1, &ibo2);
  BindBuffer(GL_ARRAY_BUFFER, vbo); BufferData(GL_ARRAY_BUFFER, 4096 * sizeof(Vtx), 0, GL_DYNAMIC_DRAW);
  static unsigned short idx[6 * 64]; for (int q = 0; q < 64; q++) { idx[q * 6] = q * 4; idx[q * 6 + 1] = q * 4 + 1; idx[q * 6 + 2] = q * 4 + 2; idx[q * 6 + 3] = q * 4; idx[q * 6 + 4] = q * 4 + 2; idx[q * 6 + 5] = q * 4 + 3; }
  BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW); BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  BindBuffer(GL_ARRAY_BUFFER, vbo2); BufferData(GL_ARRAY_BUFFER, 4 * sizeof(Vtx), 0, GL_DYNAMIC_DRAW); BindBuffer(GL_ARRAY_BUFFER, 0);
  /* q5: an index buffer created and filled through GL_ARRAY_BUFFER (legal in desktop GL, forbidden for one object in WebGL2) and later bound as GL_ELEMENT_ARRAY_BUFFER;
     frame 1 re-fills it through GL_ARRAY_BUFFER again (second quad's indices) while it is still the element buffer — the draw must follow the new data */
  BindBuffer(GL_ARRAY_BUFFER, ibo2); BufferData(GL_ARRAY_BUFFER, 12 * sizeof(unsigned short), idx, GL_STATIC_DRAW); BindBuffer(GL_ARRAY_BUFFER, 0);
  BindBuffer(GL_ARRAY_BUFFER, vbo3); BufferData(GL_ARRAY_BUFFER, 8 * sizeof(Vtx), 0, GL_DYNAMIC_DRAW); BindBuffer(GL_ARRAY_BUFFER, 0);
  /* orthographic projection in pixels (column-major), identity modelview — the game's u_PMatrix/u_MVMatrix */
  const float proj[16] = { 2.0f / 640, 0, 0, 0,  0, 2.0f / 480, 0, 0,  0, 0, -1, 0,  -1, -1, 0, 1 }; const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
  static Vtx verts[8], colq[4], cli[6], dual[8];
  quad(verts, 40, 40, 240, 200, 0.05f, 0.1f, 0.45f, 0.9f, 255, 255, 255, 255);         /* q1: green part of the texture, white colour → green */
  quad(verts + 4, 300, 40, 600, 200, 0.55f, 0.1f, 0.95f, 0.9f, 128, 128, 128, 255);    /* q2: blue part × grey colour → (0,0,128) */
  quad(colq, 40, 260, 240, 420, 0, 0, 0, 0, 255, 255, 0, 255);                         /* q3: colour program → yellow */
  quad(dual, 40, 430, 600, 470, 0, 0, 0, 0, 255, 0, 255, 255); quad(dual + 4, 40, 430, 600, 470, 0, 0, 0, 0, 0, 255, 255, 255);   /* q5: magenta (indices 0..3), cyan (indices 4..7) */
  { Vtx q[4]; quad(q, 300, 260, 600, 420, 0.05f, 0.1f, 0.45f, 0.9f, 255, 255, 255, 255); cli[0] = q[0]; cli[1] = q[1]; cli[2] = q[2]; cli[3] = q[0]; cli[4] = q[2]; cli[5] = q[3]; }   /* q4: client arrays → green */
  int ok = 0;
  for (int frame = 0; frame < 3; frame++) {
    glClearColor(0, 0, 0, 0); glDepthMask(GL_TRUE); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); glViewport(0, 0, 640, 480);
    BindBuffer(GL_ARRAY_BUFFER, vbo); BufferSubData(GL_ARRAY_BUFFER, 0, sizeof verts, verts);
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); BlendEquation(GL_FUNC_ADD); glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    UseProgram(ptex); UniformMatrix4fv(uP, 1, GL_FALSE, proj); UniformMatrix4fv(uMV, 1, GL_FALSE, ident);
    EnableVertexAttribArray(0); EnableVertexAttribArray(1); EnableVertexAttribArray(2);
    VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)0); VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), (void *)12); VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)16);
    BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); glBindTexture(GL_TEXTURE_2D, t);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void *)0); glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void *)(6 * sizeof(unsigned short)));
    /* colour-only program from a second VBO (attribs 0 and 1) */
    UseProgram(pcol); UniformMatrix4fv(cP, 1, GL_FALSE, proj); UniformMatrix4fv(cMV, 1, GL_FALSE, ident); DisableVertexAttribArray(2);
    BindBuffer(GL_ARRAY_BUFFER, vbo2); BufferSubData(GL_ARRAY_BUFFER, 0, sizeof colq, colq);
    VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)0); VertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), (void *)12);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void *)0);
    /* q5: colour quad through the index buffer that was uploaded via GL_ARRAY_BUFFER */
    BindBuffer(GL_ARRAY_BUFFER, vbo3); BufferSubData(GL_ARRAY_BUFFER, 0, sizeof dual, dual);
    VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)0); VertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), (void *)12);
    BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo2);
    if (frame == 1) { BindBuffer(GL_ARRAY_BUFFER, ibo2); BufferSubData(GL_ARRAY_BUFFER, 0, 6 * sizeof(unsigned short), idx + 6); BindBuffer(GL_ARRAY_BUFFER, vbo3); }   /* now indices 4..7 → cyan */
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void *)0);
    /* textured quad from client-side arrays (no VBO bound) */
    UseProgram(ptex); BindBuffer(GL_ARRAY_BUFFER, 0); BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); EnableVertexAttribArray(2);
    VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), &cli[0].x); VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), &cli[0].r); VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), &cli[0].u);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    GLenum e = glGetError(); if (e) printf("frame %d: GL error 0x%x\n", frame, e);
    unsigned char q1[4] = {0}, q2[4] = {0}, q3[4] = {0}, q4[4] = {0}, q5[4] = {0}, bg[4] = {0};
    glReadPixels(140, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, q1); glReadPixels(450, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, q2); glReadPixels(140, 340, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, q3); glReadPixels(450, 340, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, q4); glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bg); glReadPixels(320, 450, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, q5);
    if (frame == 0 || frame == 2) {
      printf("frame %d pixels: q1 tex×white=(%d,%d,%d,%d) q2 tex×grey=(%d,%d,%d,%d) q3 colour=(%d,%d,%d,%d) q4 client=(%d,%d,%d,%d) q5 ibo-via-array=(%d,%d,%d,%d) bg=(%d,%d,%d,%d)\n", frame, q1[0], q1[1], q1[2], q1[3], q2[0], q2[1], q2[2], q2[3], q3[0], q3[1], q3[2], q3[3], q4[0], q4[1], q4[2], q4[3], q5[0], q5[1], q5[2], q5[3], bg[0], bg[1], bg[2], bg[3]);
      int o1 = near3(q1, 0, 255, 0) && q1[3] > 200, o2 = near3(q2, 0, 0, 128) && q2[3] > 200, o3 = near3(q3, 255, 255, 0), o4 = near3(q4, 0, 255, 0), ob = bg[0] < 20 && bg[1] < 20 && bg[2] < 20, o5 = frame == 0 ? near3(q5, 255, 0, 255) : near3(q5, 0, 255, 255);
      if (!o1) printf("FAIL q1: textured VBO quad %s\n", q1[3] == 0 ? "left nothing on the surface (alpha 0: texcoords hit the transparent corner texel, colour attribute zero, or geometry off-screen)" : "has the wrong colour (texture byte order / upload)");
      if (!o2) printf("FAIL q2: colour attribute × texture %s\n", q2[3] == 0 ? "left nothing" : "wrong (normalized ubyte colour or blue half of the texture)");
      if (!o3) printf("FAIL q3: colour-only program (second VBO, attribs 0/1)\n");
      if (!o4) printf("FAIL q4: client-side arrays path\n");
      if (!ob) printf("FAIL bg: the transparent clear is not transparent black\n");
      if (!o5) printf("FAIL q5: index buffer uploaded through GL_ARRAY_BUFFER %s\n", q5[3] == 0 ? "draws nothing when bound as GL_ELEMENT_ARRAY_BUFFER (WebGL2 one-kind-per-buffer rule not bridged)" : frame == 0 ? "wrong colour" : "did not pick up the re-upload through GL_ARRAY_BUFFER (stale twin)");
      ok = o1 && o2 && o3 && o4 && o5 && ob;
    }
    SwapBuffers(dc); MSG m; while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
  }
  Sleep(400);
  printf(ok ? "sprite path verified: textured VBO quads, colour quad, array-uploaded index buffer and client arrays all on the surface\n" : "sprite path FAILED (see FAIL lines)\n");
  wglMakeCurrent(0, 0); wglDeleteContext(rc); ReleaseDC(h, dc); printf("glsprite ok\n"); return 0; }
