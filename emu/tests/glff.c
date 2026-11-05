/* glff.c — the fixed-function OpenGL 1.x pipeline + GLU through opengl32.dll / glu32.dll, verified pixel by pixel with glReadPixels:
   immediate mode (glBegin/glEnd, all primitive kinds), matrix stacks, display lists, lighting, texturing (texenv, mipmaps),
   fog, alpha test, clip planes, polygon mode, raster position + glDrawPixels/glBitmap, client-side vertex arrays,
   attribute stacks, glGet* of legacy state, gluOrtho2D/gluPerspective/gluLookAt/gluProject/gluSphere/gluCylinder.
   Prints "ALL OK n/n" at the end. Build (x64 and x86):
   zig cc -target x86_64-windows-gnu -O1 -o glff.exe glff.c -luser32 -lgdi32 -lopengl32 -lglu32
   zig cc -target x86-windows-gnu    -O1 -o glff32.exe glff.c -luser32 -lgdi32 -lopengl32 -lglu32 */
#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
typedef GLboolean (APIENTRY *P_gluCheckExtension)(const GLubyte *, const GLubyte *);
static int g_ok = 0, g_total = 0;
static void check(const char *name, int ok) { g_total++; if (ok) g_ok++; printf("%s %s\n", ok ? "ok  " : "FAIL", name); }
static void px(int x, int y, unsigned char *out) { glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out); }
static int near3(const unsigned char *p, int r, int g, int b, int tol) { return abs(p[0] - r) <= tol && abs(p[1] - g) <= tol && abs(p[2] - b) <= tol; }
static void checkpx(const char *name, int x, int y, int r, int g, int b, int tol) { unsigned char p[4] = {0}; px(x, y, p); g_total++; if (near3(p, r, g, b, tol)) g_ok++; printf("%s %s: (%d,%d)=(%d,%d,%d) expected (%d,%d,%d)\n", near3(p, r, g, b, tol) ? "ok  " : "FAIL", name, x, y, p[0], p[1], p[2], r, g, b); }
static void clear(void) { glClearColor(0.1f, 0.1f, 0.3f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }
static void ortho(void) { glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluOrtho2D(0, 640, 0, 480); glMatrixMode(GL_MODELVIEW); glLoadIdentity(); }
static void quad(float x0, float y0, float x1, float y1) { glBegin(GL_QUADS); glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1); glEnd(); }
static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) { if (m == WM_DESTROY) { PostQuitMessage(0); return 0; } return DefWindowProcA(h, m, w, l); }
static void pump(HDC dc) { SwapBuffers(dc); MSG m; while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); } }
static void errcheck(const char *where) { GLenum e = glGetError(); char buf[96]; snprintf(buf, sizeof buf, "no GL error after %s (0x%x)", where, e); check(buf, e == 0); }

int main(void) {
  WNDCLASSA wc = {0}; wc.lpfnWndProc = wp; wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "glff"; wc.style = CS_OWNDC; RegisterClassA(&wc);
  HWND h = CreateWindowA("glff", "OpenGL 1.x fixed function", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, 0, 0, wc.hInstance, 0);
  HDC dc = GetDC(h);
  PIXELFORMATDESCRIPTOR pfd = { sizeof pfd, 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 }; pfd.cDepthBits = 24; pfd.cStencilBits = 8;
  int pf = ChoosePixelFormat(dc, &pfd); if (!pf || !SetPixelFormat(dc, pf, &pfd)) { printf("no pixel format\n"); return 2; }
  HGLRC rc = wglCreateContext(dc); if (!rc) { printf("wglCreateContext failed (%lu): no OpenGL on this host\n", GetLastError()); return 4; }
  if (!wglMakeCurrent(dc, rc)) { printf("wglMakeCurrent failed\n"); return 5; }
  printf("GL_VERSION=%s\nGL_RENDERER=%s\nGLU_VERSION=%s\n", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER), (const char *)gluGetString(GLU_VERSION));
  RECT cr; GetClientRect(h, &cr); glViewport(0, 0, 640, 480);
  glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);

  /* ---- 1. immediate mode, matrix stack, glRect, shading ---- */
  ortho(); clear();
  glColor3f(1, 0, 0); quad(10, 10, 110, 110);
  checkpx("GL_QUADS red quad", 60, 60, 255, 0, 0, 2); checkpx("background untouched", 5, 5, 26, 26, 77, 3);
  glBegin(GL_TRIANGLES); glColor3f(1, 0, 0); glVertex2i(150, 10); glColor3f(0, 1, 0); glVertex2i(250, 10); glColor3f(0, 0, 1); glVertex2i(200, 110); glEnd();
  { unsigned char p[4]; px(200, 40, p); check("GL_TRIANGLES smooth shading mixes the three colours", p[0] > 40 && p[1] > 40 && p[2] > 20 && p[0] < 200); }
  glShadeModel(GL_FLAT); glBegin(GL_TRIANGLES); glColor3f(1, 0, 0); glVertex2i(300, 10); glColor3f(0, 1, 0); glVertex2i(400, 10); glColor3f(0, 0, 1); glVertex2i(350, 110); glEnd(); glShadeModel(GL_SMOOTH);
  checkpx("GL_FLAT takes the last vertex colour", 350, 40, 0, 0, 255, 2);
  glColor3ub(255, 255, 0); glRectf(450, 10, 550, 110); checkpx("glRectf + glColor3ub", 500, 60, 255, 255, 0, 2);
  glPushMatrix(); glTranslatef(0, 150, 0); glScalef(2, 1, 1); glColor3f(0, 1, 1); quad(10, 10, 60, 110); glPopMatrix();
  checkpx("glPushMatrix/glTranslatef/glScalef", 60, 210, 0, 255, 255, 2); checkpx("glPopMatrix restores (nothing at the untranslated spot)", 60, 420, 26, 26, 77, 3);
  glBegin(GL_TRIANGLE_STRIP); glColor3f(1, 0, 1); glVertex2i(150, 160); glVertex2i(150, 260); glVertex2i(250, 160); glVertex2i(250, 260); glEnd(); checkpx("GL_TRIANGLE_STRIP", 200, 210, 255, 0, 255, 2);
  glBegin(GL_TRIANGLE_FAN); glColor3f(1, 1, 1); glVertex2i(350, 210); for (int i = 0; i <= 16; i++) glVertex2f(350 + 50 * cosf(i * 6.2832f / 16), 210 + 50 * sinf(i * 6.2832f / 16)); glEnd(); checkpx("GL_TRIANGLE_FAN disc", 350, 210, 255, 255, 255, 2);
  glBegin(GL_POLYGON); glColor3f(0.5f, 0.5f, 0.5f); glVertex2i(450, 160); glVertex2i(550, 160); glVertex2i(570, 210); glVertex2i(550, 260); glVertex2i(450, 260); glEnd(); checkpx("GL_POLYGON", 500, 210, 128, 128, 128, 3);
  glLineWidth(1); glColor3f(1, 1, 1); glBegin(GL_LINES); glVertex2f(10.5f, 300.5f); glVertex2f(110.5f, 300.5f); glEnd(); { unsigned char p[4]; px(60, 300, p); check("GL_LINES draws a line", p[0] > 200); }
  glBegin(GL_POINTS); glVertex2f(150.5f, 300.5f); glEnd(); { unsigned char p[4]; px(150, 300, p); check("GL_POINTS draws a point", p[0] > 200); }
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glColor3f(1, 0.5f, 0); quad(200.5f, 280.5f, 300.5f, 330.5f); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  checkpx("glPolygonMode(GL_LINE): interior stays empty", 250, 305, 26, 26, 77, 3); { unsigned char p[4]; px(200, 305, p); check("glPolygonMode(GL_LINE): edge drawn", p[0] > 200 && p[1] > 90); }
  errcheck("immediate mode");
  pump(dc);

  /* ---- 2. display lists ---- */
  clear(); GLuint list = glGenLists(2); check("glGenLists returns a name", list != 0);
  glNewList(list, GL_COMPILE); glColor3f(0, 1, 0); quad(10, 10, 110, 110); glEndList();
  checkpx("GL_COMPILE does not draw", 60, 60, 26, 26, 77, 3);
  glCallList(list); checkpx("glCallList draws the compiled quad", 60, 60, 0, 255, 0, 2); check("glIsList", glIsList(list) == GL_TRUE && glIsList(list + 1) == GL_TRUE && glIsList(list + 2) == GL_FALSE);
  glNewList(list + 1, GL_COMPILE); glPushMatrix(); glTranslatef(200, 0, 0); glCallList(list); glPopMatrix(); glEndList();
  glColor3f(1, 1, 1); glCallList(list + 1); checkpx("nested glCallList inside a list with glTranslatef", 260, 60, 0, 255, 0, 2);
  { GLuint ids[2] = { list, list + 1 }; glTranslatef(0, 150, 0); glCallLists(2, GL_UNSIGNED_INT, ids); glLoadIdentity(); checkpx("glCallLists(GL_UNSIGNED_INT)", 260, 210, 0, 255, 0, 2); }
  glDeleteLists(list, 2); check("glDeleteLists", !glIsList(list)); errcheck("display lists");
  pump(dc);

  /* ---- 3. lighting ---- */
  clear(); glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); check("glIsEnabled(GL_LIGHTING)", glIsEnabled(GL_LIGHTING) == GL_TRUE);
  { float pos[4] = { 0, 0, 1, 0 }, white[4] = { 1, 1, 1, 1 }, none[4] = { 0, 0, 0, 1 }; glLightfv(GL_LIGHT0, GL_POSITION, pos); glLightfv(GL_LIGHT0, GL_DIFFUSE, white); glLightfv(GL_LIGHT0, GL_AMBIENT, none); glLightModelfv(GL_LIGHT_MODEL_AMBIENT, none); }
  { float dif[4] = { 1, 0.5f, 0, 1 }, amb[4] = { 0, 0, 0, 1 }; glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, dif); glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, amb); }
  glNormal3f(0, 0, 1); quad(10, 10, 110, 110); checkpx("directional light on a facing quad = material diffuse", 60, 60, 255, 128, 0, 3);
  glNormal3f(0, 0, -1); quad(150, 10, 250, 110); checkpx("quad facing away is black", 200, 60, 0, 0, 0, 3);
  glNormal3f(0.7071f, 0, 0.7071f); quad(300, 10, 400, 110); { unsigned char p[4]; px(350, 60, p); check("45-degree normal gives half intensity", p[0] > 160 && p[0] < 200 && p[1] > 75 && p[1] < 105); }
  glEnable(GL_COLOR_MATERIAL); glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE); glColor3f(0, 0, 1); glNormal3f(0, 0, 1); quad(450, 10, 550, 110); checkpx("GL_COLOR_MATERIAL tracks glColor", 500, 60, 0, 0, 255, 3); glDisable(GL_COLOR_MATERIAL);
  { float gray[4] = { 0.5f, 0.5f, 0.5f, 1 }; glLightModelfv(GL_LIGHT_MODEL_AMBIENT, gray); float amb[4] = { 1, 1, 1, 1 }, dif0[4] = { 0, 0, 0, 1 }; glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, amb); glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, dif0); quad(10, 150, 110, 250); checkpx("global ambient * material ambient", 60, 200, 128, 128, 128, 3); }
  { float dif[4] = { 1, 1, 1, 1 }, spec[4] = { 1, 1, 1, 1 }; glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, dif); glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec); glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 8); glLightfv(GL_LIGHT0, GL_SPECULAR, spec); float none[4] = { 0, 0, 0, 1 }; glLightModelfv(GL_LIGHT_MODEL_AMBIENT, none);
    float lpos[4] = { 60, 350, 50, 1 }; glLightfv(GL_LIGHT0, GL_POSITION, lpos); glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1); quad(10, 300, 110, 400); unsigned char c[4], e[4]; px(60, 350, c); px(15, 305, e); check("positional light: brighter under the light than at the corner", c[0] >= e[0] && c[0] > 200); glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1); float spec0[4] = { 0, 0, 0, 1 }; glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec0); }
  glDisable(GL_LIGHTING); glDisable(GL_LIGHT0); glColor3f(1, 1, 1); errcheck("lighting");
  pump(dc);

  /* ---- 4. texturing ---- */
  clear(); GLuint tex[2]; glGenTextures(2, tex); glBindTexture(GL_TEXTURE_2D, tex[0]);
  { unsigned char img[16] = { 255, 0, 0, 255,  0, 255, 0, 255,  0, 0, 255, 255,  255, 255, 255, 255 }; glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, img); }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); glEnable(GL_TEXTURE_2D);
  glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex2f(10, 10); glTexCoord2f(1, 0); glVertex2f(110, 10); glTexCoord2f(1, 1); glVertex2f(110, 110); glTexCoord2f(0, 1); glVertex2f(10, 110); glEnd();
  checkpx("texture texel (0,0) red", 30, 30, 255, 0, 0, 2); checkpx("texel (1,0) green", 90, 30, 0, 255, 0, 2); checkpx("texel (0,1) blue", 30, 90, 0, 0, 255, 2); checkpx("texel (1,1) white", 90, 90, 255, 255, 255, 2);
  glColor3f(0, 1, 0); glBegin(GL_QUADS); glTexCoord2f(0.75f, 0.75f); glVertex2f(150, 10); glTexCoord2f(0.75f, 0.75f); glVertex2f(250, 10); glTexCoord2f(0.75f, 0.75f); glVertex2f(250, 110); glTexCoord2f(0.75f, 0.75f); glVertex2f(150, 110); glEnd(); checkpx("GL_MODULATE: white texel * green colour", 200, 60, 0, 255, 0, 2);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); glBegin(GL_QUADS); glTexCoord2f(0.75f, 0.75f); glVertex2f(300, 10); glTexCoord2f(0.75f, 0.75f); glVertex2f(400, 10); glTexCoord2f(0.75f, 0.75f); glVertex2f(400, 110); glTexCoord2f(0.75f, 0.75f); glVertex2f(300, 110); glEnd(); checkpx("GL_REPLACE ignores glColor", 350, 60, 255, 255, 255, 2);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); glColor3f(1, 1, 1);
  glBindTexture(GL_TEXTURE_2D, tex[1]); { unsigned char img[27]; for (int i = 0; i < 9; i++) { img[i * 3] = 0; img[i * 3 + 1] = 128; img[i * 3 + 2] = 255; } int r = gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, 3, 3, GL_RGB, GL_UNSIGNED_BYTE, img); check("gluBuild2DMipmaps(3x3 RGB) returns 0", r == 0); }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex2f(450, 10); glTexCoord2f(1, 0); glVertex2f(550, 10); glTexCoord2f(1, 1); glVertex2f(550, 110); glTexCoord2f(0, 1); glVertex2f(450, 110); glEnd(); checkpx("mipmapped non-power-of-two texture", 500, 60, 0, 128, 255, 4);
  glDisable(GL_TEXTURE_2D); glColor3f(1, 1, 0); quad(10, 150, 110, 250); checkpx("glDisable(GL_TEXTURE_2D) goes back to plain colour", 60, 200, 255, 255, 0, 2);
  { GLint w = 0; glBindTexture(GL_TEXTURE_2D, tex[0]); glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w); check("glGetTexLevelParameteriv(GL_TEXTURE_WIDTH)", w == 2); }
  /* 1D texture: a 4-texel gradient strip */
  glBindTexture(GL_TEXTURE_1D, tex[1]); { unsigned char strip[12] = { 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0 }; glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 4, 0, GL_RGB, GL_UNSIGNED_BYTE, strip); }
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); glEnable(GL_TEXTURE_1D); glColor3f(1, 1, 1);
  glBegin(GL_QUADS); glTexCoord1f(0); glVertex2f(150, 150); glTexCoord1f(1); glVertex2f(250, 150); glTexCoord1f(1); glVertex2f(250, 250); glTexCoord1f(0); glVertex2f(150, 250); glEnd(); glDisable(GL_TEXTURE_1D);
  checkpx("GL_TEXTURE_1D texel 1 (green)", 190, 200, 0, 255, 0, 2); checkpx("GL_TEXTURE_1D texel 3 (yellow)", 245, 200, 255, 255, 0, 2);
  /* texgen: object-linear planes map the quad's x/y onto s/t */
  { float sp[4] = { 0.01f, 0, 0, -0.1f }, tp[4] = { 0, 0.01f, 0, -1.6f }; glBindTexture(GL_TEXTURE_2D, tex[0]); glEnable(GL_TEXTURE_2D); glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR); glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR); glTexGenfv(GL_S, GL_OBJECT_PLANE, sp); glTexGenfv(GL_T, GL_OBJECT_PLANE, tp); glEnable(GL_TEXTURE_GEN_S); glEnable(GL_TEXTURE_GEN_T); glColor3f(1, 1, 1);
    quad(10, 160, 110, 260); glDisable(GL_TEXTURE_GEN_S); glDisable(GL_TEXTURE_GEN_T); glDisable(GL_TEXTURE_2D); checkpx("GL_OBJECT_LINEAR texgen (texel 0,0)", 30, 180, 255, 0, 0, 2); checkpx("GL_OBJECT_LINEAR texgen (texel 1,1)", 90, 240, 255, 255, 255, 2); }
  /* multitexture (ARB entry points through wglGetProcAddress): unit 1 adds a second texture */
  { typedef void (APIENTRY *P_ActiveTexture)(GLenum); typedef void (APIENTRY *P_MultiTexCoord2f)(GLenum, GLfloat, GLfloat); P_ActiveTexture act = (P_ActiveTexture)wglGetProcAddress("glActiveTextureARB"); P_MultiTexCoord2f mtc = (P_MultiTexCoord2f)wglGetProcAddress("glMultiTexCoord2fARB"); check("glActiveTextureARB/glMultiTexCoord2fARB exported", act && mtc);
    if (act && mtc) { act(0x84C1 /* GL_TEXTURE1 */); glBindTexture(GL_TEXTURE_2D, tex[0]); glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, 0x0104 /* GL_ADD */); act(0x84C0); glBindTexture(GL_TEXTURE_2D, tex[0]); glEnable(GL_TEXTURE_2D);
      glBegin(GL_QUADS); for (int i = 0; i < 4; i++) { glTexCoord2f(0.25f, 0.25f); mtc(0x84C1, 0.75f, 0.25f); glVertex2f(300 + (i == 1 || i == 2) * 100, 160 + (i >= 2) * 100); } glEnd();
      act(0x84C1); glDisable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); act(0x84C0); glDisable(GL_TEXTURE_2D); checkpx("two texture units: red (unit 0) + green (unit 1, GL_ADD)", 350, 210, 255, 255, 0, 2); } }
  errcheck("texturing");
  pump(dc);

  /* ---- 5. fog, alpha test, clip planes, depth ---- */
  clear(); glEnable(GL_FOG); glFogi(GL_FOG_MODE, GL_LINEAR); glFogf(GL_FOG_START, 0); glFogf(GL_FOG_END, 1); { float fc[4] = { 0, 0, 1, 1 }; glFogfv(GL_FOG_COLOR, fc); }
  glColor3f(1, 0, 0); glPushMatrix(); glTranslatef(0, 0, -0.5f); quad(10, 10, 110, 110); glPopMatrix(); glDisable(GL_FOG);
  { unsigned char p[4]; px(60, 60, p); check("GL_LINEAR fog halfway between red and the fog colour", p[0] > 100 && p[0] < 160 && p[2] > 100 && p[2] < 160); }
  glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.5f); glColor4f(0, 1, 0, 0.2f); quad(150, 10, 250, 110); glColor4f(0, 1, 0, 0.8f); quad(300, 10, 400, 110); glDisable(GL_ALPHA_TEST);
  checkpx("alpha test rejects alpha 0.2", 200, 60, 26, 26, 77, 3); checkpx("alpha test passes alpha 0.8", 350, 60, 0, 255, 0, 2);
  { double eq[4] = { 1, 0, 0, -500 }; glClipPlane(GL_CLIP_PLANE0, eq); glEnable(GL_CLIP_PLANE0); glColor3f(1, 1, 1); quad(450, 10, 550, 110); glDisable(GL_CLIP_PLANE0); checkpx("clip plane cuts x < 500", 475, 60, 26, 26, 77, 3); checkpx("clip plane keeps x >= 500", 525, 60, 255, 255, 255, 2); }
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glColor3f(1, 0, 0); glPushMatrix(); glTranslatef(0, 0, 0.2f); quad(10, 150, 110, 250); glPopMatrix(); glColor3f(0, 0, 1); quad(10, 150, 110, 250); glDisable(GL_DEPTH_TEST);
  checkpx("depth test: nearer red quad stays in front", 60, 200, 255, 0, 0, 2);
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glColor4f(1, 1, 1, 0.5f); glColor4f(1, 1, 1, 1); quad(150, 150, 250, 250); glColor4f(0, 0, 0, 0.5f); quad(150, 150, 250, 250); glDisable(GL_BLEND); checkpx("blending with the vertex alpha", 200, 200, 128, 128, 128, 3);
  errcheck("fog/alpha/clip/depth");
  pump(dc);

  /* ---- 6. raster position, glDrawPixels, glBitmap, glCopyPixels, glPixelZoom ---- */
  clear(); { unsigned char img[8 * 8 * 4]; for (int i = 0; i < 64; i++) { img[i * 4] = 255; img[i * 4 + 1] = 255; img[i * 4 + 2] = 0; img[i * 4 + 3] = 255; } glRasterPos2i(300, 300); glDrawPixels(8, 8, GL_RGBA, GL_UNSIGNED_BYTE, img); checkpx("glDrawPixels at the raster position", 304, 304, 255, 255, 0, 2); checkpx("glDrawPixels does not spill", 310, 304, 26, 26, 77, 3); }
  { unsigned char bits[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }; glColor3f(0, 1, 1); glRasterPos2i(400, 300); glBitmap(8, 8, 0, 0, 8, 0, bits); checkpx("glBitmap in the current colour", 404, 304, 0, 255, 255, 2); glBitmap(8, 8, 0, 0, 8, 0, bits); checkpx("glBitmap advances the raster position", 412, 304, 0, 255, 255, 2); }
  { unsigned char img[4 * 4] = { 255, 0, 255, 255,  255, 0, 255, 255,  255, 0, 255, 255,  255, 0, 255, 255 }; glPixelZoom(4, 4); glRasterPos2i(500, 300); glDrawPixels(2, 2, GL_RGBA, GL_UNSIGNED_BYTE, img); glPixelZoom(1, 1); checkpx("glPixelZoom(4,4) scales the image", 506, 306, 255, 0, 255, 2); }
  { float rp[4] = { 0 }; glRasterPos2i(123, 45); glGetFloatv(GL_CURRENT_RASTER_POSITION, rp); check("glGetFloatv(GL_CURRENT_RASTER_POSITION) in window coordinates", fabsf(rp[0] - 123) < 0.6f && fabsf(rp[1] - 45) < 0.6f); }
  glColor3f(1, 1, 1); quad(10, 10, 50, 50); glRasterPos2i(100, 10); glCopyPixels(10, 10, 40, 40, GL_COLOR); checkpx("glCopyPixels copies the framebuffer", 120, 30, 255, 255, 255, 2);
  { unsigned char out[4 * 4 * 4] = { 0 }; glReadPixels(10, 10, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, out); check("glReadPixels block", out[0] == 255 && out[60] == 255); }
  errcheck("raster ops");
  pump(dc);

  /* ---- 7. client-side vertex arrays ---- */
  clear(); { static const float v[] = { 10, 10, 110, 10, 110, 110, 10, 110 }; static const unsigned char c[] = { 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255 };
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY); glVertexPointer(2, GL_FLOAT, 0, v); glColorPointer(4, GL_UNSIGNED_BYTE, 0, c); glDrawArrays(GL_QUADS, 0, 4); checkpx("glVertexPointer/glColorPointer + glDrawArrays(GL_QUADS)", 60, 60, 255, 0, 0, 2);
    static const unsigned short idx[] = { 0, 1, 2, 0, 2, 3 }; glDisableClientState(GL_COLOR_ARRAY); glColor3f(0, 1, 0); glPushMatrix(); glTranslatef(150, 0, 0); glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx); glPopMatrix(); checkpx("glDrawElements(GL_UNSIGNED_SHORT) with the current colour", 210, 60, 0, 255, 0, 2);
    glBegin(GL_QUADS); glColor3f(0, 0, 1); glPushMatrix(); for (int i = 0; i < 4; i++) glArrayElement(i); glEnd(); glPopMatrix(); checkpx("glArrayElement inside glBegin", 60, 60, 0, 0, 255, 2);
    static const float inter[] = { 1, 1, 1, 310, 10, 0,  1, 1, 1, 410, 10, 0,  1, 1, 1, 410, 110, 0,  1, 1, 1, 310, 110, 0 }; glInterleavedArrays(GL_C3F_V3F, 0, inter); glDrawArrays(GL_QUADS, 0, 4); checkpx("glInterleavedArrays(GL_C3F_V3F)", 360, 60, 255, 255, 255, 2);
    glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_COLOR_ARRAY); }
  { static const float v[] = { 450, 10, 550, 10, 550, 110, 450, 110 }, t[] = { 0, 0, 1, 0, 1, 1, 0, 1 }; glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY); glVertexPointer(2, GL_FLOAT, 0, v); glTexCoordPointer(2, GL_FLOAT, 0, t); glBindTexture(GL_TEXTURE_2D, tex[0]); glEnable(GL_TEXTURE_2D); glColor3f(1, 1, 1); glDrawArrays(GL_TRIANGLE_FAN, 0, 4); glDisable(GL_TEXTURE_2D); glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY); checkpx("glTexCoordPointer textured fan", 530, 90, 255, 255, 255, 2); checkpx("glTexCoordPointer textured fan (texel 0,0)", 470, 30, 255, 0, 0, 2); }
  errcheck("vertex arrays");
  pump(dc);

  /* ---- 8. GLU: perspective, lookAt, quadrics, project ---- */
  clear(); glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluPerspective(60, 640.0 / 480.0, 0.1, 100); glMatrixMode(GL_MODELVIEW); glLoadIdentity(); gluLookAt(0, 0, 5, 0, 0, 0, 0, 1, 0);
  glEnable(GL_DEPTH_TEST); GLUquadric *q = gluNewQuadric(); check("gluNewQuadric", q != 0); glColor3f(1, 0, 1); gluSphere(q, 1.0, 24, 16);
  checkpx("gluSphere at the centre", 320, 240, 255, 0, 255, 2); checkpx("gluSphere leaves the corner empty", 20, 20, 26, 26, 77, 3);
  glPushMatrix(); glTranslatef(2.5f, 0, 0); glRotatef(90, 1, 0, 0); glColor3f(0, 1, 0); gluCylinder(q, 0.5, 0.5, 2, 16, 2); glPopMatrix(); { unsigned char p[4]; px(528, 200, p); check("gluCylinder", p[1] > 200 && p[0] < 50); }
  glPushMatrix(); glTranslatef(-2.5f, 0, 0); glColor3f(1, 1, 0); gluDisk(q, 0, 0.8, 16, 1); glPopMatrix(); checkpx("gluDisk", 140, 240, 255, 255, 0, 2);
  gluDeleteQuadric(q);
  { double mv[16], pr[16]; int vp[4]; glGetDoublev(GL_MODELVIEW_MATRIX, mv); glGetDoublev(GL_PROJECTION_MATRIX, pr); glGetIntegerv(GL_VIEWPORT, vp); double wx, wy, wz, ox, oy, oz;
    int r1 = gluProject(0, 0, 0, mv, pr, vp, &wx, &wy, &wz); int r2 = gluUnProject(wx, wy, wz, mv, pr, vp, &ox, &oy, &oz);
    check("gluProject(origin) lands in the window centre", r1 == GL_TRUE && fabs(wx - 320) < 0.5 && fabs(wy - 240) < 0.5); check("gluUnProject round trip", r2 == GL_TRUE && fabs(ox) < 1e-3 && fabs(oy) < 1e-3 && fabs(oz) < 1e-3); check("glGetDoublev(GL_MODELVIEW_MATRIX) from gluLookAt", fabs(mv[14] + 5) < 1e-5 && fabs(mv[0] - 1) < 1e-5); }
  glDisable(GL_DEPTH_TEST); errcheck("GLU");
  { const char *es = (const char *)gluErrorString(GL_INVALID_ENUM); check("gluErrorString(GL_INVALID_ENUM)", es && strstr(es, "enum")); P_gluCheckExtension chk = (P_gluCheckExtension)GetProcAddress(GetModuleHandleA("glu32.dll"), "gluCheckExtension"); check("gluCheckExtension via GetProcAddress(glu32.dll)", chk && chk((const GLubyte *)"GL_ARB_vertex_buffer_object", glGetString(GL_EXTENSIONS)) == GL_TRUE && chk((const GLubyte *)"GL_NEBULA_nope", glGetString(GL_EXTENSIONS)) == GL_FALSE); }
  pump(dc);

  /* ---- 9. state queries and attribute stacks ---- */
  ortho(); { GLint mm = 0; glGetIntegerv(GL_MATRIX_MODE, &mm); check("glGetIntegerv(GL_MATRIX_MODE)", mm == GL_MODELVIEW); float m[16]; glLoadIdentity(); glTranslatef(3, 4, 5); glGetFloatv(GL_MODELVIEW_MATRIX, m); check("glGetFloatv(GL_MODELVIEW_MATRIX) after glTranslatef", m[12] == 3 && m[13] == 4 && m[14] == 5 && m[0] == 1); glLoadIdentity();
    GLint depth = 0; glGetIntegerv(GL_MAX_MODELVIEW_STACK_DEPTH, &depth); check("GL_MAX_MODELVIEW_STACK_DEPTH >= 32", depth >= 32); GLint lights = 0; glGetIntegerv(GL_MAX_LIGHTS, &lights); check("GL_MAX_LIGHTS == 8", lights == 8); }
  { glColor3f(0.25f, 0.5f, 0.75f); glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT); glColor3f(1, 1, 1); glEnable(GL_LIGHTING); glPopAttrib(); float c[4]; glGetFloatv(GL_CURRENT_COLOR, c); check("glPushAttrib/glPopAttrib(GL_CURRENT_BIT) restores glColor", fabsf(c[0] - 0.25f) < 1e-3 && fabsf(c[2] - 0.75f) < 1e-3); check("glPopAttrib(GL_ENABLE_BIT) restores GL_LIGHTING", !glIsEnabled(GL_LIGHTING)); }
  { glEnableClientState(GL_VERTEX_ARRAY); glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT); glDisableClientState(GL_VERTEX_ARRAY); glPopClientAttrib(); check("glPushClientAttrib/glPopClientAttrib", glIsEnabled(GL_VERTEX_ARRAY)); glDisableClientState(GL_VERTEX_ARRAY); }
  { glMatrixMode(GL_TEXTURE); glLoadIdentity(); glTranslatef(0.5f, 0, 0); glMatrixMode(GL_MODELVIEW); clear(); glBindTexture(GL_TEXTURE_2D, tex[0]); glEnable(GL_TEXTURE_2D); glColor3f(1, 1, 1);
    glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex2f(10, 10); glTexCoord2f(0, 0); glVertex2f(110, 10); glTexCoord2f(0, 0); glVertex2f(110, 110); glTexCoord2f(0, 0); glVertex2f(10, 110); glEnd(); glDisable(GL_TEXTURE_2D); checkpx("texture matrix shifts s by 0.5 (texel 1,0 green)", 60, 60, 0, 255, 0, 2); glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); }
  { float v[4]; glGetLightfv(GL_LIGHT0, GL_DIFFUSE, v); check("glGetLightfv(GL_LIGHT0, GL_DIFFUSE)", v[0] == 1 && v[3] == 1); glGetMaterialfv(GL_FRONT, GL_SHININESS, v); check("glGetMaterialfv(GL_SHININESS)", v[0] == 8); GLint e = 0; glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &e); check("glGetTexEnviv", e == GL_MODULATE); }
  glBegin(GL_QUADS); glMatrixMode(GL_PROJECTION); glEnd(); check("glMatrixMode inside glBegin/glEnd → GL_INVALID_OPERATION", glGetError() == GL_INVALID_OPERATION); glMatrixMode(GL_MODELVIEW);
  glDeleteTextures(2, tex); errcheck("state queries");
  pump(dc);

  /* final showcase frame (stays on screen): lit sphere + textured quad + wireframe cylinder in perspective */
  clear(); glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluPerspective(50, 640.0 / 480.0, 0.1, 100); glMatrixMode(GL_MODELVIEW); glLoadIdentity(); gluLookAt(0, 1.5, 6, 0, 0, 0, 0, 1, 0);
  glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glEnable(GL_COLOR_MATERIAL); glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
  { float lp[4] = { 2, 4, 5, 1 }, w4[4] = { 1, 1, 1, 1 }, a4[4] = { 0.15f, 0.15f, 0.15f, 1 }; glLightfv(GL_LIGHT0, GL_POSITION, lp); glLightfv(GL_LIGHT0, GL_DIFFUSE, w4); glLightfv(GL_LIGHT0, GL_SPECULAR, w4); glLightModelfv(GL_LIGHT_MODEL_AMBIENT, a4); glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, w4); glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 32); }
  GLUquadric *q2 = gluNewQuadric(); gluQuadricNormals(q2, GLU_SMOOTH); glColor3f(0.9f, 0.2f, 0.6f); gluSphere(q2, 1.2, 32, 24);
  glPushMatrix(); glTranslatef(-2.8f, -0.5f, 0); glRotatef(-90, 1, 0, 0); glColor3f(0.2f, 0.8f, 0.3f); gluCylinder(q2, 0.6, 0.3, 1.8, 20, 4); glPopMatrix();
  glPushMatrix(); glTranslatef(2.8f, -0.5f, 0); glRotatef(-90, 1, 0, 0); glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glColor3f(1, 0.9f, 0.2f); gluSphere(q2, 0.9, 16, 12); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glPopMatrix(); gluDeleteQuadric(q2);
  glDisable(GL_LIGHTING); { GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t); unsigned char chk[8 * 8 * 3]; for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { int on = (x ^ y) & 1; unsigned char *p = chk + (y * 8 + x) * 3; p[0] = on ? 230 : 40; p[1] = on ? 230 : 60; p[2] = on ? 230 : 120; } gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, 8, 8, GL_RGB, GL_UNSIGNED_BYTE, chk);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); glEnable(GL_TEXTURE_2D); glColor3f(1, 1, 1);
    glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex3f(-5, -1.5f, -4); glTexCoord2f(4, 0); glVertex3f(5, -1.5f, -4); glTexCoord2f(4, 4); glVertex3f(5, -1.5f, 4); glTexCoord2f(0, 4); glVertex3f(-5, -1.5f, 4); glEnd(); glDisable(GL_TEXTURE_2D); glDeleteTextures(1, &t); }
  glDisable(GL_DEPTH_TEST); pump(dc);
  printf("%s %d/%d\n", g_ok == g_total ? "ALL OK" : "FAILED", g_ok, g_total);
  Sleep(400); wglMakeCurrent(0, 0); wglDeleteContext(rc); ReleaseDC(h, dc); return g_ok == g_total ? 0 : 1; }
