# v1.43.2 — GOG installers reach the branded wizard: msi.dll, a JIT `push esp` bug, 32-bit pseudo-handles, real JPEG decoding

**"Cannot Import dll: msi.dll" (phone report, v1.43.1).** GOG's Inno Setup scripts import `MsiQueryProductState` / `MsiEnumRelatedProducts`
/ `MsiGetProductInfo` / `MsiSetInternalUI` / `MsiGetFileVersion` / `MsiGetFileHash` from msi.dll to look for installed
redistributables. There is a real msi.dll now (`emu/x64/win32_msi.inc`): an empty product database with correct Windows
Installer return codes (`INSTALLSTATE_UNKNOWN`, `ERROR_NO_MORE_ITEMS`, `ERROR_UNKNOWN_PRODUCT`), `MsiSetInternalUI` state, and
`MsiGetFileVersion` / `MsiGetFileHash` over real files (version resource + MD5). Every Msi* entry is listed for the x86 stdcall
table — the first version corrupted the PascalScript stack on x86 because 159 names had no argument size. `tests/msi.c` runs
on x86 and x64 and requires a clean exit code.

**"Error reading BackButton.Width" after the language dialog (JIT only).** Found with the runner: the GOG wizard opened when
the language dialog was confirmed within seconds, and failed when it had been left open for a minute. The idle loop made
Delphi's `push esp` hot, and the translator pushed the *already decremented* stack pointer (Intel: the value before the push).
Fixed in `jit.inc`; `jitdiff.c` now checks `push esp`/`push rsp` + `pop esp` in both bitnesses. sitecheck got a `jitskip`
input (bisect by instruction kind, the way this one was found) and keeps the Inno Setup Log in the artifact.

**32-bit pseudo-handles.** `GetCurrentProcess()` / `GetCurrentThread()` arrive from x86 code as `0xFFFFFFFF` / `0xFFFFFFFE`;
every consumer compared against the 64-bit values, so psapi `EnumProcessModules`, `GetProcessId`, `GetExitCodeProcess`,
`WaitForSingleObject(self)`, `DuplicateHandle`, `CreateFileMapping(INVALID_HANDLE_VALUE)` (page-file mappings!),
`Suspend/TerminateThread`, `QueueUserAPC` failed with "invalid handle" on x86. Normalised (`h_norm`), covered by `tests/ptrout.c`.

**x86 pointer-sized out-parameters.** `SysReAllocStringLen`, `fopen_s`, `CreatePipe`, `DuplicateHandle`, `getenv_s`, … wrote
8 bytes into a 4-byte slot on x86 — `SysReAllocStringLen` clobbered PascalScript's variant stack in the GOG script.

**Real JPEG decoder (GDI+ / WIC).** Baseline + progressive, restart markers, any chroma sampling, libjpeg-style fancy
upsampling, Adobe RGB/CMYK; tested against libjpeg references (`tests/jpegtest.c`). The GOG wizard background is a JPEG.

**Where the GOG installer is now (runner, JIT on):** language dialog → branded Welcome page with background, logo, OpenSans
text, Options / Install buttons (732 M instructions). The rest of the wizard (Install click → directory → file copy) is the
next step. Native run.sh: 379 + msi + jpeg + ptrout tests per bitness; CI e2e and jitdiff green.

# v1.43.1 — the APK finally ships the emulator that was tested; exe names up to 259 characters

**Why v1.42.0 / v1.43.0 behaved differently on the phone than on the runner.** The CI job that built the APK (and the e2e
job before it) used the *committed* `public/x64/nebula_x64.wasm` from 5 November instead of the module the emulator job
had just built and tested — so neither the Cyrillic/space name fix nor the x87 text fix ever reached the phone (the mangled
"The syttem cannot hind the fild spdcihied" box in the user's screenshots is that old module). Both jobs now download the
`nebula_x64-wasm64` artifact of the same run, and the Android job unpacks the signed APK and fails if the sha256 of the
embedded wasm differs from the one built in the run.

**Long names.** `Setup_Shattered_Pixel_Dungeon_v4.0.0_(64bit)_(93711)_RUS-….exe` (70 characters) hit a 64-byte process
name buffer: the file landed on C:\nebula under a truncated name while the command line kept the full one, so the Inno
loader could not reopen itself → "The system cannot find the file specified" after 0.1 M instructions. Process and module
names are 259 characters now; `run.sh` runs the upath test under a 70+ character Cyrillic name on x86 and x64, and the
sitecheck under the exact GOG-style name reaches the language dialog (682 M instructions).

# v1.43.0 — any file name works (Cyrillic, spaces), ANSI programs stop corrupting their heap, text is no longer mangled, OpenGL on the GPU, Inno Setup 5 runs, real version.dll

**"File not found" on every exe (phone report).** v1.42.0's `…W` path writers emitted one byte per UTF-16 unit, so a program
whose name contained a non-ASCII character or a space (`Установка Inno (1).exe`) could not reopen its own file: Inno's
SetupLdr stopped after 147 k instructions with an error box. Fixed at the root (`put_w8` / `utf8_u16len` everywhere a path
goes out), and every W path API now returns **UTF-16 unit counts**, not UTF-8 byte counts (`GetFullPathNameW`, `SearchPathW`,
`GetLongPathNameW`, `GetCurrentDirectoryW`, `GetEnvironmentVariableW`, `ExpandEnvironmentStringsW`, `GetFinalPathNameByHandleW`,
`PathCch*`, `_wgetcwd`, `_wfullpath`, process image names) with file-part pointers in units. `WideCharToMultiByte(CP_ACP /
CP_OEMCP / CP_THREAD_ACP)` is UTF-8 like `MultiByteToWideChar` already was (GetACP = 65001). New `tests/upath.c` runs an exe
named `Папка тест\Установка Inno 5 (1).exe` on x86 and x64 and checks all of it. Verified on the runner: the same Inno 6.5.4
file under that name goes from the error box to the language dialog (682 M instructions).

**Mangled letters in message boxes (s→t, f→h, e→d).** Not the font: Delphi's `Move` copies 8-byte chunks through the x87
(`fild/fistp m64`), and the emulator's 53-bit x87 rounded values ≥ 2^53 — every fourth UTF-16 character pair came out ±1–4.
The x87 now keeps an exact 64-bit shadow for `fild/fistp m64` and `fld/fstp m80` (`tests/x87copy.c`, x86 + x64, bit-exact).

**ANSI programs (Delphi VCL, MFC) corrupted their heap.** `CallWindowProcA` to a built-in control (an app subclassing an
EDIT, which every VCL program does) passed the caller's ANSI buffer to a control that speaks UTF-16: `WM_GETTEXT` wrote twice
the bytes into an 8-byte Delphi string → random "Access violation at address 00410711" later. Conversion is now keyed on the
*receiving procedure* (built-in = convert, application proc = pass through), so SendMessageA → app proc → CallWindowProcA
converts exactly once. `tests/ansisub.c` covers it with guard bytes. Inno Setup **5.6.1** (ANSI build, Delphi 2 era) now
shows its wizard under a Cyrillic name — it also needed `OemToChar/CharToOem`, `CharLowerA`, `SendNotifyMessageA` /
`SendMessageTimeoutA` (HWND_BROADCAST), `WinHelp`, `GetShortPathNameA`, `WNetGetUniversalNameA/WNetEnumResourceA`,
suffix-less `SHBrowseForFolder` / `SHGetPathFromIDList` exports.

**version.dll for real.** `GetFileVersionInfoSize/GetFileVersionInfo/VerQueryValue` (A and W) parse the file's own
`RT_VERSION` resource on the virtual disk and walk `VS_VERSIONINFO` (`\`, `\VarFileInfo\Translation`,
`\StringFileInfo\<lang>\<key>`); the A variants hand out 8-bit copies in the slack `GetFileVersionInfoSize` reserves.
Tested over putty.exe on x86 and x64 (`tests/verinfo.c`).

**A launcher waiting for its child no longer burns the CPU.** `PeekMessage` synthesised `WM_PAINT` for *any* invalid window,
so Inno's SetupLdr received its child's paint messages 2.6 million times in 25 s and re-posted every one of them. Paint
messages are now synthesised only for the pumping process.

**OpenGL on the GPU, swap-chain pacing.** The GL present path composites with a key colour on the GPU instead of
`readPixels + getError` per frame, the GL-only flip skips `texSubImage2D`, the swap chain is paced; `next_invalid` is O(1)
when nothing is invalid, `GetMessage`/`MsgWaitForMultipleObjects` pauses are capped at 1 ms, the frame loop continues through
a `MessageChannel` hop, the gamepad is polled at ≤4 ms. Shattered Pixel Dungeon on the runner: 1915 frames / 30 s at
106 MIPS with Mesa llvmpipe (`game-video.yml` `gpu=mesa-gl`, the default now) vs 1200 with SwiftShader.

**Tooling.** `sitecheck.yml` (download any URL, run it under a chosen file name natively with `X64_LOG=1` — which now lists
every import without a shim at load time — and in the browser; optional `--apitrace`; the loader's extracted child exe is
kept as an artifact), `argv[0]` quoting as Explorer does.

Honest limits: Inno 5's 32-bit code is mostly interpreted (JIT coverage of 32-bit mode is the next step); no D3D9/DirectDraw yet.

# v1.42.0 — 3.5× faster in the browser (28 → 100+ MIPS in Shattered Pixel Dungeon), mouse clicks land where the picture is, a new game actually starts; x86 JIT 3×

**Speed — where the time really went.** A profile of the real JVM under the emulator (new `jitprof.yml` *jvm* job: the
Shattered Pixel Dungeon JVM runs under `wasm_run.mjs` with `node --cpu-prof`) and a reading of the browser frame loop found
three things that had nothing to do with the JIT:
- The host frame loop handed the CPU back to the browser whenever **every** guest thread was momentarily blocked — and a
  JVM parks its threads for a millisecond at a time. Each hand-off cost a 4 ms timer (16 ms without audio); the game got
  ~20 % of the main thread. Now a guest that only pauses for ≤1.5 ms is waited for inside the slice, and a busy guest
  continues through a `MessageChannel` task (not clamped like nested timers; input and painting still run in between).
  Result in the recorded runs: 28–33 MIPS → **103–120 MIPS**, title screen after ~180 s instead of ~680 s, 10–20 fps in
  the menus instead of 4.
- Every scheduler pick copied the current thread's 12 KB context out and back in (64 continuation frames). Now a thread
  that stays current copies nothing, and a switch copies only the live continuations.
- Each PeekMessage walked the whole 1024-entry window table twice looking for something to repaint; now it exits at once
  when nothing is invalid. The software audio mixer no longer mixes 200 ms of sound per scheduler pick when there is no host
  output (node tests) — it is paced by the clock like the real output.

**JIT fast paths for what the JVM and C++ compilers actually emit** (all were falling back to the interpreter, one step
each): `inc/dec r32` in 32-bit mode, `push imm`, `leave`, `bt/bts/btr/btc` (register, immediate and memory bit strings),
`bsf/bsr`, `bswap`, `shld/shrd`, `mul/imul/div/idiv` (one-operand forms), `imul` with live flags, `shufps/shufpd`,
`cvtdq2ps`, `movlps/movhps/movlhps/movhlps`, `66 90`; 32-bit `call/jmp/push/mov` memory forms that were excluded wholesale.
`emu/tests/jitdiff.c` got a `misc` section covering every new kind (zero inputs for bsf/bsr, overflow for imul, 64-bit
division, the SSE shuffles, indirect calls); native = wasm interpreter = wasm JIT on x64 and x86 (`jitdiff.yml` bisects
the new kinds too). Bench (`jitprof.yml`): x86 JIT **97 → 298–438 MIPS**, `bits` 2322 → 72–196 ms; x64 unchanged to
slightly faster (318 → 326–461 MIPS, machine-dependent).

**Mouse.** Clicks reached the game but at the wrong place: the pointer was scaled to the *launcher's* unused 800×600
framebuffer (`x64_screen_w/h` reported the root process) while the picture came from the child's 1280×720 one. The host
now asks for the framebuffer that was presented last. `input!…` statistics in the report (`last_target_…_screen_x_y_client_x_y`,
window rect / non-client origin) make this checkable from a recorded run.

**A new game starts.** Clicking *Start* killed the JVM with `Internal Error (0xc06d007f)`: `net.dll` delay-loads
`iphlpapi.dll`, `NetworkInterface.getAll` (SecureRandom seeding) called `GetIfTable`, which did not exist — a delay-load
"procedure not found" is fatal inside HotSpot. `GetIfTable / GetIpAddrTable / GetIfEntry / GetFriendlyIfIndex /
GetNumberOfInterfaces / GetAdaptersInfo / GetIpForwardTable` now describe one loopback interface (127.0.0.1/8), as on a
Windows machine without network. Recorded: Welcome → title → *Choose Your Hero* (Warrior, splash art) → *Start* →
*Descending…* (sewer intro, Continue) with music — `game.mp4` (screenshots) and `game-smooth.mp4` (in-page
`MediaRecorder` of the canvas, 30 fps) in the `game-video` artifact; `record.mjs` keeps console / report / `hs_err` logs
when the program dies on an action.

**Windows behaviour.** Files have real times now: creation / last write (`SetFileTime` honoured) in every query
(`GetFileAttributesEx`, `GetFileInformationByHandle(Ex)`, `GetFileTime`, `FindFirstFile`, `NtQuery*`). Before, every query
returned "now", so Notepad++ asked to *Reload* a file it had just opened as soon as the second changed.

# v1.41.0 — JIT: `cvtsi2ss` converted ints as UNSIGNED (black screen in Shattered Pixel Dungeon); recorded game video with sound; JIT differential test

**What was wrong.** With the x86-64 → wasm JIT on (the default since v1.38.0) Shattered Pixel Dungeon started, played its
music, presented 400+ frames — and the window stayed black. With the JIT off (`nebula_jit=0`) the title screen was drawn.
A new GL call tracer in the WebGL2 bridge (`localStorage nebula_gltrace = "<afterFrame>:<calls>"`, decoded uniforms /
attributes / texture uploads) showed the only difference: the camera matrix uploaded with `glUniformMatrix4fv` had
`tx = 2.887e+16` instead of `-1.002` — every sprite was translated off-screen. Bisecting by instruction kind in the browser
(`nebula_jitskip = lo:hi` → `x64_jit_skip`, seven parallel runner jobs) pointed at the SSE conversions, and the emitter
had `F32_CVT_I64S = 0xB5`: that is wasm's `f32.convert_i64_u`. `f32.convert_i64_s` is `0xB4`. Every `cvtsi2ss` of a
negative integer produced a huge positive float (the `f64` form, `0xB9`, was right, which is why .NET and the integer
tests never noticed). One byte — `emu/x64/jit.inc`.

**Proof, all on the GitHub runner (nothing is built or tested in the sandbox any more).**
- `.github/workflows/game-video.yml` (manual): builds the wasm from source, installs the official GPL Windows build of
  Shattered Pixel Dungeon v4.0.2 through the Programs UI exactly as a user would, waits for the game loop, then records
  15 s of the screen canvas **with the sound the audio bridge schedules** (`e2e/record.mjs` hooks `AudioBufferSourceNode.start`,
  ffmpeg muxes per-frame durations + PCM → `game.mp4`). Before the fix: black frames, music playing; after the fix with
  the JIT on: the title screen (logo, intro text, “Enter the Dungeon”), music at −25 dB mean. The JIT reaches the title
  ~2× sooner than the interpreter (640 s vs 1290 s on a 2-vCPU runner; 30 MIPS vs 17 MIPS — a JVM interpreting its own
  bytecode is a hard case for a block JIT).
- `emu/tests/jitdiff.c` + `.github/workflows/jitdiff.yml` (~5 min, runs on every push touching the JIT): the same
  deterministic float/int/SSE workload (the noosa camera matrix, scalar chains with int↔float↔double conversions,
  NaN compares, min/max, sign-mask negation, packed loops, and hand-written HotSpot/clang forms such as
  `xorps xmm0,xmm0; cvtsi2ss xmm0, eax`) must print identical checksums under the native runner, the wasm interpreter
  and the wasm JIT, for x64 and x86. It does now — and the checksums equal the ones a real x86-64 CPU prints for the same
  source (`6b10a989 aca66469 94e334b9 cd28baa0 4bd76ec9`). On a mismatch the workflow bisects by instruction kind itself.
  The same step runs in the main CI.
- `e2e/record.mjs --jit 0|1 --jitskip lo:hi --gltrace a:n` and the workflow inputs make every such experiment a one-line
  dispatch; artifacts: `game.mp4`, `title.png`, `audio.wav`, `report.txt`, `gltrace.txt`.

**Also.** CI runs on every push to `wip-dotnet` (not only `main`/tags); the `.NET under JIT` CI step no longer fails on the
guest's own exit code 3 (pipefail); the game-video workflow builds the wasm module from the current `emu/x64` so the
committed `public/x64/nebula_x64.wasm` cannot mask a change.

**Honest limits.** Unchanged from v1.40.1 (no D3D9/DirectDraw, fixed-function GL subset, SPD needs 2.6 GB of guest
address space, a few frames per second on the runner). The audio capture in the recorder is the emulator's output stream,
not a microphone — it is exactly what the device would play.

# v1.40.1 — x64 JIT: two region-cache bugs fixed (CoreCLR / System.Text.Json fault)

Since v1.38.0 (the x86-64 → wasm JIT) the browser e2e test that runs a real C# program on the official .NET 10 runtime
failed at the JSON step with `unhandled exception 0xc0000005 at 0x107fe0d8` (CI of v1.38.0, v1.39.0 and v1.40.0 red;
the interpreter alone — `nebula_jit=0` — was fine). The fault was reproduced deterministically outside the browser
(`node emu/tests/wasm_run.mjs --jit 1 … nebula-dotnet-test.exe linq-throw-nre-json-reflect-regex-gc`) and traced with
a new per-instruction history to two bugs in the region cache of `emu/x64/jit.inc`:

1. **A compiled region's function-table slot could be freed while still referenced.** While the blocks of a freshly
   compiled region are published into the 2-way set-associative block table, `jb_lookup()` may evict a block of that
   very region; its refcount went through 0 and the slot was pushed onto the free list, the next translation reused
   it, and an older entry `(rip A, pc N)` then executed block N of an unrelated region — in the crash a `ret` of the
   wrong function popped a stack address and CoreCLR's exception dispatch found no frame. A reference is now held
   during publishing, the hot block is published last and the entry is looked up again after translation.
2. **Stale detection inside a region rewound to the entry rip.** Every block checks its code bytes on entry (CoreCLR
   patches the write barrier after a GC, regenerates stubs, …). When a *later* block of the region was stale the function
   returned "stale" and the interpreter resumed at the region **entry** although the earlier blocks had already run — the
   write barrier was re-executed with `rcx` already shifted by 22 bits → AV at 0x1800 on the finalizer thread. The stale
   exit now sets `rip` to the stale block.

Result: the .NET e2e test (LINQ, exceptions, hardware NullReferenceException, JSON, reflection, Regex, GC on CoreCLR with
the JIT on) passes again in the browser; `wasm_run` on the full test mode exits with the expected code 3.

Diagnostics that stay in the build (used to find this, useful for the next one): `x64_history_raw(n, at_exc)` — raw
block history with JIT region exits (`>x`) and hardware exceptions (`!x`), plus a snapshot taken at the last hardware
exception; `x64_last_exc_rip()`; `x64_jit_skip(lo, hi)` / `wasm_run --jitskip lo:hi` — step instruction kinds `[lo,hi)`
instead of translating them (bisection of translation bugs); `wasm_run -v` prints all of it on a FAULT.

Android-only deliverable as before: the APK from this tag is the release.

# v1.40.0 — OpenGL 1.x fixed-function pipeline + GLU on WebGL2 (Android)

**Classic OpenGL games now render.** Until now the `opengl32.dll` bridge exposed WebGL2 as a core-profile GL 3.3 context:
any program that used `glBegin/glEnd`, `glMatrixMode`, `glLight*`, display lists or `glu32.dll` got `GL_INVALID_OPERATION`
and a black window. v1.40.0 adds a complete fixed-function layer (`src/lib/x64/glff.ts`, ~800 lines) that translates
GL 1.0–1.5 state into generated GLSL ES 3.00 programs on the fly — the same approach ANGLE/Mesa use for compatibility
contexts — so games written against Quake/Half-Life-era OpenGL, GLUT/SDL 1.2 samples, Java/LWJGL 2 and old Delphi/VB
programs draw on the phone's GPU through WebGL2.

**What is implemented (every item is verified pixel by pixel by `glff.exe` / `glff32.exe` — 96 checks, ALL OK on x64 and x86):**

* **Immediate mode**: `glBegin`/`glEnd` with all ten primitive kinds (`GL_POINTS`, `GL_LINES`, `GL_LINE_STRIP`,
  `GL_LINE_LOOP`, `GL_TRIANGLES`, `GL_TRIANGLE_STRIP`, `GL_TRIANGLE_FAN`, `GL_QUADS`, `GL_QUAD_STRIP`, `GL_POLYGON`),
  `glVertex2/3/4{s,i,f,d}[v]`, `glColor3/4{b,ub,s,us,i,ui,f,d}[v]`, `glNormal3*`, `glTexCoord1/2/3/4*`,
  `glMultiTexCoord*` / `glMultiTexCoord*ARB` (4 units), `glRect*`, `glArrayElement`, `glEdgeFlag` (accepted).
  Vertices are batched into one interleaved stream VBO per `glEnd` (own VAO; the program's VAO/VBO/program bindings are
  restored afterwards, so mixed GLSL + immediate-mode code keeps working).
* **Matrix stacks**: `GL_MODELVIEW` (32 deep), `GL_PROJECTION`, `GL_TEXTURE` (per unit), `glLoadIdentity`, `glLoadMatrix*`,
  `glMultMatrix*`, `glLoadTransposeMatrix*`, `glMultTransposeMatrix*`, `glTranslate*`, `glRotate*`, `glScale*`,
  `glOrtho`, `glFrustum`, `glPushMatrix`/`glPopMatrix` with `GL_STACK_OVERFLOW`/`UNDERFLOW`; `glGet*v` of
  `GL_MODELVIEW_MATRIX`, `GL_PROJECTION_MATRIX`, `GL_TEXTURE_MATRIX`, `GL_MATRIX_MODE`, stack depths.
* **Lighting**: 8 lights (`glLight*` position/direction/ambient/diffuse/specular/spot exponent & cutoff/attenuation),
  `glMaterial*` (front/back, `GL_SHININESS`, `GL_EMISSION`), `glColorMaterial`, `glLightModel*` (global ambient,
  local viewer, two-sided), `glShadeModel(GL_FLAT/GL_SMOOTH)`, `GL_NORMALIZE`/`GL_RESCALE_NORMAL`, `glGetLight*`/
  `glGetMaterial*` — per-vertex Gouraud as in the GL 1.x spec (positional and directional lights, spotlights, specular
  with half-vector).
* **Texturing**: `GL_TEXTURE_2D` and `GL_TEXTURE_1D` (stored as height-1 2D textures: `glTexImage1D`, `glTexSubImage1D`,
  `glCopyTexImage1D`), two active fixed-function units, `glTexEnv*` with `GL_MODULATE`, `GL_REPLACE`, `GL_DECAL`,
  `GL_BLEND`, `GL_ADD` and `GL_COMBINE`/`GL_COMBINE_ALPHA` (`GL_ARB_texture_env_combine` sources/operands/scale),
  `glTexGen*` with `GL_OBJECT_LINEAR`, `GL_EYE_LINEAR`, `GL_SPHERE_MAP`, `GL_NORMAL_MAP`, `GL_REFLECTION_MAP`,
  texture matrices, `GL_TEXTURE_LOD_BIAS` accepted.
* **Fog** (`GL_LINEAR`, `GL_EXP`, `GL_EXP2`, by eye-space depth), **alpha test** (`glAlphaFunc`, all eight functions),
  **six user clip planes** (`glClipPlane` in eye space, `GL_CLIP_PLANE0-5`), **`glPolygonMode(GL_LINE/GL_POINT)`** (polygon
  outlines without the triangle diagonals — the classic wireframe look), `glPointSize`, `glDepthRange`, `glFog*`.
* **Display lists**: `glGenLists`, `glNewList(GL_COMPILE / GL_COMPILE_AND_EXECUTE)`, `glEndList`, `glCallList`,
  `glCallLists` (all eight index types + `glListBase`), `glDeleteLists`, `glIsList`, nested lists, lists containing
  matrix operations, state changes and `glBegin/glEnd` batches (compiled once into GPU-ready vertex data; attributes not
  set inside the list are taken from the current state at call time, exactly as the spec requires), pointer arguments
  (`glTexImage2D`, `glLightfv`, …) snapshotted at compile time.
* **Attribute stacks**: `glPushAttrib`/`glPopAttrib` for `GL_CURRENT_BIT`, `GL_ENABLE_BIT`, `GL_LIGHTING_BIT`,
  `GL_TEXTURE_BIT`, `GL_FOG_BIT`, `GL_COLOR_BUFFER_BIT`, `GL_DEPTH_BUFFER_BIT`, `GL_STENCIL_BUFFER_BIT`, `GL_POLYGON_BIT`,
  `GL_LINE_BIT`, `GL_POINT_BIT`, `GL_SCISSOR_BIT`, `GL_VIEWPORT_BIT`, `GL_TRANSFORM_BIT`, `GL_HINT_BIT`, `GL_LIST_BIT`;
  `glPushClientAttrib`/`glPopClientAttrib` (`GL_CLIENT_VERTEX_ARRAY_BIT`).
* **Raster operations**: `glRasterPos*`, `glWindowPos*`, `glBitmap` (with the raster-position advance used for text),
  `glDrawPixels` (all GL 1.x formats/types the texture path accepts), `glCopyPixels(GL_COLOR)`, `glPixelZoom`,
  `GL_CURRENT_RASTER_POSITION`/`COLOR`/`POSITION_VALID` queries — drawn through a small blit program so they respect
  depth/scissor/blend like real bitmaps.
* **Client-side vertex arrays (GL 1.1)**: `glEnableClientState`/`glDisableClientState`, `glVertexPointer`, `glNormalPointer`,
  `glColorPointer`, `glTexCoordPointer` (per `glClientActiveTexture` unit), `glInterleavedArrays` (all 14 formats),
  `glDrawArrays`/`glDrawElements`/`glDrawRangeElements`/`glMultiDrawArrays` with these arrays, `GL_DOUBLE` arrays converted,
  VBO-backed (`GL_ARRAY_BUFFER` bound) and plain memory pointers, `glGetPointerv`.
* **GLU (`glu32.dll`, 53 exports, also for 32-bit programs with doubles on the x86 stack)**: `gluPerspective`, `gluOrtho2D`,
  `gluLookAt`, `gluPickMatrix`, `gluProject`, `gluUnProject`, `gluBuild1DMipmaps`/`gluBuild2DMipmaps`
  (non-power-of-two sources rescaled, every level generated), `gluScaleImage`, `gluErrorString`, `gluGetString`,
  `gluCheckExtension`, quadrics (`gluNewQuadric`, `gluQuadricNormals/Texture/DrawStyle/Orientation`, `gluSphere`,
  `gluCylinder`, `gluDisk`, `gluPartialDisk`, `gluDeleteQuadric`).
* **State queries**: `glIsEnabled`/`glGet*v` for all the legacy enables and values above (`GL_CURRENT_COLOR`,
  `GL_CURRENT_NORMAL`, `GL_LIGHTING`, `GL_FOG_*`, `GL_ALPHA_TEST_*`, `GL_SHADE_MODEL`, `GL_POLYGON_MODE`,
  `GL_MAX_LIGHTS` = 8, `GL_MAX_CLIP_PLANES` = 6, stack depths, `GL_LIST_INDEX`/`GL_LIST_MODE`, …),
  `glGetTexEnv*`, `glGetTexGen*`, `glGetClipPlane`; `GL_INVALID_OPERATION` for state calls inside `glBegin/glEnd`.
* The GL export table grew from 762 to **848 entry points** (`emu/tools/glgen.py` now also reads Wine's `glu32.spec`;
  `glActiveTextureARB`, `glClientActiveTexture`, `glMultiTexCoord*`, `glWindowPos*`, `glLoadTransposeMatrix*`,
  `glFogCoord*`, `glSecondaryColor*` are resolvable through `wglGetProcAddress`).

**Honest limits (each answered with a GL error and one log line, never a silent fake):**
`glRenderMode(GL_SELECT/GL_FEEDBACK)` selection and feedback buffers, the accumulation buffer (`glAccum`),
evaluators (`glMap*`/`glEvalMesh*` — NURBS curves/surfaces in GLU as well), `gluTess*` tessellation objects
(`gluNewTess` returns NULL), `glLogicOp`, `glLineStipple`/`glPolygonStipple`, `GL_COLOR_MATRIX`, secondary colour and
fog-coordinate per-vertex attributes (accepted, not used), `glPolygonMode(GL_BACK)` separate from the front mode,
`GL_TEXTURE_3D`/cube maps in the fixed-function stages, and only texture units 0 and 1 are sampled by the generated shaders (units 2–3 keep their state; `GL_MAX_TEXTURE_UNITS` still reports 8 for GLSL programs).
Lighting is per-vertex (Gouraud) exactly like GL 1.x, so highly tessellated objects look right, flat-shaded big quads look
like they did on 1990s hardware.

**Tests:** `emu/tests/glff.c` (`glff.exe` x64 / `glff32.exe` x86, built with `-lopengl32 -lglu32`) — 96 pixel-verified checks
covering every bullet above, in the Programs → tests list and in the browser e2e (`e2e/run.mjs`, 51 tests, both exes end
with `ALL OK 96/96` on SwiftShader); `tests/run.sh` (250 checks) runs both on the native null driver. `gltri.exe` now
reports `glBegin/glEnd error=0x0`.

# v1.39.0 — Game controllers: XInput, DirectInput8, winmm + the phone's own pads and an on-screen controller (Android)

**Windows games now see a real game controller.** Every Windows gamepad API a game may use is implemented on top of one
XInput-shaped controller state (`emu/x64/emu.h: PadState g_pads[4]`) that the host fills from the browser's **Gamepad API**
(any Bluetooth/USB pad paired with the phone — Xbox, DualShock/DualSense, 8BitDo, generic "standard mapping" pads) or
from the new **on-screen controller** in the Programs window (touch bar → 🎮): left analog stick, d-pad, A/B/X/Y,
LB/RB, LT/RT, Back/Start.  The on-screen pad is merged into controller 0 together with a physical pad (buttons OR-ed,
larger axis deflection wins), so a game can be played with the thumb-stick on glass and a physical pad at the same time.

**Windows side — what games get (`emu/x64/win32_compat.inc`, `input.inc`, `win32.c`):**

* **XInput 1.4 / 1.3 / 9.1.0** (`xinput1_4.dll`, `xinput1_3.dll`, `xinput9_1_0.dll`, `xinputuap.dll`): `XInputGetState`,
  `XInputSetState` (→ **rumble on the phone's pad** through `vibrationActuator.playEffect('dual-rumble')`),
  `XInputGetCapabilities` (gamepad subtype, voice/FFB flags honest), `XInputEnable`, `XInputGetBatteryInformation`
  (wired/full), `XInputGetKeystroke` (connected → `ERROR_EMPTY`, i.e. "no keystroke pending": games then poll `GetState`),
  `XInputGetDSoundAudioDeviceGuids`/`XInputGetAudioDeviceIds` (no headset), plus
  the **undocumented ordinals 100 (`XInputGetStateEx`, Guide button) and 108 (`XInputGetCapabilitiesEx`, VID 045E / PID 028E)**
  that SDL2, Unity and MonoGame resolve by number. `dwPacketNumber` changes only when the state changes; unplugged slots
  return `ERROR_DEVICE_NOT_CONNECTED` exactly like Windows.
* **winmm joystick** (`joyGetNumDevs` = 16, `joyGetDevCapsA/W` — "Controller (XBOX 360 For Windows)", 5 axes, 10 buttons,
  POV, `joyGetPos`, `joyGetPosEx` (`JOYINFOEX`: X/Y/Z/R/U axes 0..65535, buttons, POV in centidegrees / `JOY_POVCENTERED`),
  `joyGetThreshold`/`joySetThreshold`, `joyConfigChanged`; `joySetCapture` answers `JOYERR_UNPLUGGED` so programs fall back
  to polling, which every one of them does) — what old Win32/DirectX-7-era games and SDL 1.2 use.
* **DirectInput8 game controller** (`IDirectInput8::EnumDevices(DI8DEVCLASS_GAMECTRL / DI8DEVTYPE_GAMEPAD)`, `CreateDevice`
  by instance/product GUID, `IDirectInputDevice8::SetDataFormat(c_dfDIJoystick/2)`, `SetCooperativeLevel`, `EnumObjects`
  (5 axes, 10 buttons, 1 POV with the standard `DIJOFS_*` offsets, usage page 1 / usages 0x30-0x35, 0x39, 9/1-10),
  `GetObjectInfo` by offset / id / usage, `SetProperty`/`GetProperty` (`DIPROP_RANGE` — default 0..65535, custom ranges
  honoured in `GetDeviceState`; `DIPROP_BUFFERSIZE`, `DIPROP_AXISMODE`, `DIPROP_GRANULARITY`, dead zone / saturation
  / autocenter / calibration mode answered with Windows' defaults, the rest `DIERR_UNSUPPORTED`), `Acquire`/`Unacquire`,
  `Poll`, `GetDeviceState` (`DIJOYSTATE` layout — also what `c_dfDIJoystick2` reads for its first 80 bytes — axes scaled
  to the game's range, triggers on the Z axis as on Windows, POV in hundredths of degrees, `DIERR_NOTACQUIRED`/
  `DIERR_INPUTLOST` on unplug), `GetDeviceData` (buffered button/axis changes),
  `GetCapabilities`, `GetDeviceInfo`, `RunControlPanel`/`SendDeviceData`/`CreateEffect` → honest `DIERR_UNSUPPORTED`.
* **Hot-plug** in all three APIs: a pad connected after the game started is seen on the next poll, an unplugged one
  reports the right error, and winmm captured windows get `MM_JOY*` messages only while the pad exists.
* 32-bit (x86) programs get the same through the stdcall thunks (`stdcall32.inc` regenerated).

**Host side (`src/lib/x64/pad.ts`, `runtime.ts`, `VirtualPad.tsx`):** standard-mapping Gamepad → XInput layout
(buttons 0-3 → A B X Y, 4/5 → LB/RB, 6/7 → analog LT/RT, 8/9 → Back/Start, 10/11 → stick clicks, 12-15 → d-pad, 16 →
Guide), stick axes scaled to ±32767 with Y inverted as XInput expects, polled once per emulator frame (`x64_pad` export);
non-standard mappings fall back to the first four axes/buttons; `XInputSetState` → `playEffect('dual-rumble')` with the
motors' strengths, or a no-op if the pad has no actuator. The status bar shows what the emulator currently sees
(`🎮 Xbox Wireless Controller (STANDARD GAMEPAD …)`, `on-screen pad`).

**Tests:** `emu/tests/pad.c` (33 checks — XInput connect/disconnect/packet numbers/caps/ordinals 100+108/rumble, winmm
caps/pos/posex/capture message, DirectInput8 enumeration/objects/ranges/state/scaled axes/POV/unplug → `DIERR_INPUTLOST`)
runs natively with the host schedule `--pad '0:1,0x1010,255,0,16384,-8192,0,0@300' --pad '0:0,0,…@2500'` for x64 and x86
(248 checks total in `tests/run.sh`), and in the browser e2e suite with a simulated Gamepad API device (33/33 plus the
vibration actuator being driven) and with the on-screen controller (49 e2e tests).

**Honest limits:** no `MM_JOY*` capture messages (polling only); `XInputGetKeystroke` never reports `VK_PAD_*` keystrokes
(games use `GetState`); Chrome/WebView exposes a pad to the page only after a button is pressed on it (browser privacy rule —
press any button once after pairing); the Gamepad API has no battery level (reported as "wired"), no gyro/touchpad, and
rumble is only available where the browser implements `vibrationActuator` (Chrome ≥ 68 on Android, pads that support it);
DirectInput force-feedback effects are not implemented (games fall back to no rumble when `CreateEffect` fails, XInput
rumble works); the on-screen pad has one analog stick (right stick / stick clicks are buttons-only there); HID/raw-input
access to the pad (`RIM_TYPEHID`) is not exposed — games that bypass all three APIs and read HID reports directly see no pad.

# v1.38.0 — x86→wasm JIT: hot code runs as WebAssembly (Android)

**The emulator now compiles hot x86/x86-64 code to WebAssembly at run time** (`emu/x64/jit.inc`, 900 lines) instead of
interpreting every instruction. It is a *region translator*: when a basic block has been entered 300 times the translator
collects the blocks reachable from it through direct branches, `call`s and their returns (up to 160 blocks / 2400
instructions inside one code mapping), and emits one small wasm module for the region — a single function with a
`br_table` dispatch loop, so loops, `call`/`ret` pairs and `jcc` chains inside the region stay in compiled code. The page
compiles the module with `new WebAssembly.Module` (synchronously, typically 0.1–0.4 ms), instantiates it against the
emulator's memory64 and publishes its entry in the main module's exported function table (`table.grow`/`set` — table64);
the interpreter calls it through a 2-way set-associative block table (64 K entries). Nothing is trusted blindly: every block
re-checks its own code bytes on entry (self-modifying / regenerated code → the region is dropped and re-translated, exactly
what the decode cache already did per instruction), the time slice (`icount` budget) is honoured on backward branches, page
faults leave the region with the precise `rip`, and anything the translator does not handle is executed by the interpreter
through a per-instruction call-out (`jit_step`) without leaving the region.

**What is translated inline:** integer moves/ALU/shifts/test/cmp/lea/movzx/movsx/cmov/imul/push/pop/call/ret/jcc/jmp with
**flags computed only when a later instruction reads them** (per-block liveness), the 16-bit (`66`) forms, `fs:`/`gs:`
segment forms (TEB/TLS access, `__readgsqword`), 8-bit immediates/tests, and the SSE scalar and packed subset games lean on:
`movss/movsd/movaps/movups/movdqa/movdqu/movd/movq`, `add/sub/mul/div ss/sd/ps/pd`, `sqrt`, `min/max` (unordered/equal →
source operand, like the interpreter), `and/andn/or/xor` (`ps` and `pd` and the integer `pand/pandn/por/pxor`), `comiss/ucomiss/
comisd/ucomisd` (`ZF|PF|CF` on unordered), `cvtsi2ss/sd`, `cvtss2sd/cvtsd2ss`, `cvt(t)ss2si/sd2si` with the exact x86 overflow
result (`INT_MIN`), bit-identical to the interpreter (f32 arithmetic performed in f64 then rounded, as before). Memory
accesses go through a module-local TLB probe (4 K pages, same table as the interpreter).

**Measured in the sandbox (Node 24 V8, same engine as Chrome):**

| | interpreter (v1.37) | JIT (v1.38) |
|---|---|---|
| `bench.exe` sieve 2 M | 1002 ms | **81–87 ms** |
| `bench.exe` mandelbrot 200×100×200 (SSE2) | 454 ms | **20–30 ms** |
| `bench.exe` memcpy 128 MB + 1 M random reads | 410 ms | **82–119 ms** |
| `bench.exe` total | 35 MIPS | **245–320 MIPS** |
| CPython 3.12 `demo.py` (270 M instructions, 2300 regions) | 10.3–10.8 s | **5.2 s** |

CPython is the hard case — a 2.5 MB interpreter with computed-goto dispatch, i.e. an indirect jump after every bytecode,
which ends a region; those exits (6.4 M) and the remaining 15 M interpreted instructions in cold code are where the rest of
its time goes. Tight numeric/game loops get ×7–×12; mixed code ×2. Correctness: all 54 test executables (console, GUI,
x86 and x64, SEH/C++ EH, .NET, Lua, SQLite, 7-Zip…) produce byte-identical output with the JIT on and off; CPython's
output is identical to the interpreter's.

**In the app:** nothing to configure — the JIT is on whenever the browser can grow the exported table (Chrome/Android
WebView 119+). The run report shows `x86→wasm JIT: N regions (KB, compile ms)` plus the counters (`exec` region entries,
`dsteps` interpreter call-outs, `dinterp` instructions interpreted outside regions, the top stepped opcodes). `localStorage
nebula_jit = 0` switches it off, a number > 1 sets the hot threshold. New `e2e` check: `bench.exe` in the browser must
compile regions, print the same results and run the sieve in < 600 ms.

**Also:** the interpreter's decode cache grew from 32 K to 128 K entries (large programs such as CPython and the JVM no longer
thrash it: `nodc` 1094 → 76); `emu/tests/wasm_run.mjs` runs any test executable through the real wasm64 module under Node
(`--jit 0|1|N`, `--mount`, `JIT_DUMP=1` for the stats) — the sandbox-side equivalent of the browser run.

**Honest limits.** Still an emulator, not native: ~250–320 MIPS peak on a desktop core in V8, less on a phone (expect
×3–×8 over v1.37 on game loops, not more). Not translated yet (interpreted through call-outs, correct but slow): x87, the
packed-integer SSE2 set (`paddd`, `pshufd`, `punpck*`, `cvtdq2ps`, `cmpps` — the top stepped opcodes of NebulaStrike32),
`bt`/`bts`, `div`/`idiv`, string ops (`rep movs/stos`), `imul` with live flags, `adc`/`sbb`. Indirect jumps/calls
(function pointers, vtables, computed goto) and `ret` to a caller outside the region end the region. Regions are compiled
on the main thread (0.1–0.4 ms each, 0.2–1.5 s in total for a big program — visible as a slightly slower first minute).
V8 runs a region in its baseline tier until it is hot enough for the optimising tier (same policy as for any wasm). No
D3D9, no gamepad yet — next.

# v1.37.0 — Notepad++ Preferences + Replace, real TrackMouseEvent, PuTTY panels, Q-Dir fonts (Android)

**Notepad++ Settings → Preferences (x64 and x86).** The dialog crashed with `ColourPicker::init : CreateWindowEx() function
return null` (0xc0000409): Preferences creates 26 child page dialogs with ~540 windows at once and the window table held 192.
`MAX_WND` is 1024 now (tree-view handles carry a 10-bit window index, `TV_HBASE 0x544xxxxx`). Three Windows behaviours were
also missing: a `WS_CHILD` dialog is placed at its template position inside the parent's client (`DIALOGEX 115, 10` → right
of the category list; before, pages landed 64 px off and overlapped the bottom), `WM_ERASEBKGND` and `WM_ENABLE` reach an
application's subclass of a common control (`SetWindowSubclass` chain or a replaced `GWLP_WNDPROC`) — Notepad++'s colour
pickers are plain `Button`s that paint their colour in `WM_ERASEBKGND` and turn `COLOR_3DFACE` grey on `WM_ENABLE(FALSE)` —
and `BM_SETCHECK` clamps to the style's maximum (Notepad++ sends flag values like 2 to two-state check boxes, which painted as
indeterminate). All 24 categories open and render: General, Toolbar, Tab Bar, Editing 1/2 (track bars, combos), Dark Mode
(13 pickers, grey while light mode is on — exactly as on Windows), Margins, New Document, Default Directory, Recent Files,
File Association, Language, Indentation (list box), Highlighting, Print (header/footer combos), Searching, Backup,
Auto-Completion, Multi-Instance & Date (the 1985-10-26 sample date is Notepad++'s own `_BTTF_time`), Delimiter, Performance,
Cloud & Link (multi-line edit with scrollbar), Search Engine, MISC. — the list box scrolls to keep the selection visible.
`reports/npp64_prefs.png`, `reports/npp32_prefs.png`.

**Notepad++ Replace.** Tab from the "Find what" combo went back to the same combo (its focus lives in the inner Edit, which
is not a direct child of the dialog), so the replacement text landed in the search field. `GetNextDlgTabItem` /
`IsDialogMessage` now walk up from a focused child to the dialog-level control first, like Windows. Ctrl+H → `abc` → Tab →
`xyz` → Replace All rewrites the document ("Replace All: 3 occurrences were replaced in entire file" in the dialog's status
bar), Esc, Save As → `rep.txt` = `xyz xyz xyz` (now a `tests/run.sh` check). `reports/npp64_replace.png`.

**`TrackMouseEvent` is real** (it returned TRUE and never sent anything). `TME_LEAVE` → `WM_MOUSELEAVE` when the pointer or the
capture moves to another window (`WM_NCMOUSELEAVE` with `TME_NONCLIENT`), immediately when the pointer is already elsewhere at
the time of the call; `TME_HOVER` → `WM_MOUSEHOVER` after `dwHoverTime` (default 400 ms) at rest, re-armed by every call;
`TME_QUERY` reports what is tracked; `TME_CANCEL` stops it. Built-in push buttons end their hot state the same way instead of
staying highlighted. New `tests/tme.c` (x64 + x86, 14 checks each, incl. a subclassed Button). Q-Dir's toolbar buttons no
longer stick in the hot colour; tooltips of custom controls that rely on hover now appear (Q-Dir's "Back [ALT+LEFT]").

**`SystemParametersInfo(SPI_GETNONCLIENTMETRICS)`** wrote the five `LOGFONT`s at the wrong offsets (one 4-byte int assumed
between each; the real `NONCLIENTMETRICS` has two ints after the caption and small-caption fonts and none after the menu
font) and always as `LOGFONTW`. Q-Dir builds its menu-bar and tab fonts from `lfMenuFont` and therefore read a shifted
struct — face name `e UI`, italic = 'S', strikeout = 'e' — which is why its menu bar and tabs were drawn italic + struck
through. A/W layouts (92 / 60-byte `LOGFONT`s), Windows 10 metrics (border 1, scroll 17, caption 22/36, menu 19, padded
border 4) and an A/W `SPI_GETICONTITLELOGFONT`. `reports/qdir_hot.png`.

**PuTTY configuration (x64).** Clicking a category never switched the panel: PuTTY is an ANSI build and compares the
tree-view code with `TVN_SELCHANGEDA`, but a dialog created through `CreateDialogParamA` was registered as a Unicode window,
so the control sent `TVN_SELCHANGEDW`. The `...A` dialog entry points now create ANSI windows (`IsWindowUnicode` is per
window; list/tree views already pick A/W notifications from the parent). The Terminal panel then called a null import:
PuTTY fills "Printer to send ANSI printer output to" through `winspool.drv` and does not expect `EnumPrinters` to be
missing. `winspool.drv` shims: `EnumPrintersA/W` (no printers), `GetDefaultPrinter` (none), `OpenPrinter` →
`ERROR_INVALID_PRINTER_NAME`, `StartDocPrinter` / `WritePrinter` fail, `ClosePrinter` ok, `DocumentProperties` → −1.
Session → Terminal → Appearance panels switch and the old controls are destroyed (new `tests/run.sh` checks).
`reports/putty.png`.

**Menus.** The mnemonic underline was placed with a fixed cell width; with proportional fonts it ended up after the word
(7-Zip's "Select All_"). It is now measured from the prefix and the glyph itself. `reports/7zfm.png`.

**Tests.** `tests/run.sh` all green (incl. the new Notepad++ Preferences / Replace All, PuTTY panel and tme checks),
browser e2e 46/46.

**Known limits (honest).** Preference pages render and react, but settings that need a running Scintilla change (e.g. a new
font) are only visually checked; no OLE drag & drop; `MS Shell Dlg` text is still ~4% wider than Windows (long button
captions in the Find dialog touch the edges); keyboard cues are always drawn in menus (Windows hides them until Alt).

# v1.36.0 — symbol glyphs, Notepad++ Find Next / Replace work exactly like Windows (Android)

**Symbol glyphs that no bundled font covers.** Windows falls back to Segoe UI Symbol for geometric glyphs; the bundled
PT Sans / Cousine have none of them, so Notepad++'s tab-bar menu showed `? ? ?` instead of `＋ ▼ ✕`. The font engine now has
a synthetic symbol face (`emu/x64/ttf.inc`, `g_syms`): 44 code points described as vector paths in a 1000-unit em and
rasterized through the same coverage rasterizer as TrueType outlines — arrowheads U+2BC5–2BC8 / U+23F4–23F7, triangles
U+25B4/25B6/25B8/25BE/25C0/25C2, diamonds U+25C6/25C7, ballot boxes U+2610–2612, U+2630 ☰, check marks U+2713/2714,
crosses U+2715–2718, fullwidth plus U+FF0B, heavy plus/minus U+2795/2796, heavy arrows U+2B05–2B07/2B95/27A1, U+21B5 ↵,
U+23CE, U+27A4 ➤, stars U+2605/2606, chevrons U+276E/276F, ellipses U+22EE/22EF, U+2318 ⌘. Stroked glyphs thicken for
bold faces; italic shears them like real glyphs; advances are real so layout and hit-testing agree. `fnt_glyph` only reaches
the synthetic face after the requested face, the paired mono/sans face and the regular weight all miss (Cousine already
covers ▲▼►◄■□●○← → ─│┌ etc.). `X64_FONTLOG=1` logs code points that still have no glyph.

**Notepad++ Find dialog (x64 and x86).** Three real Windows behaviours were missing and together made "Find Next" a no-op:
- a drop-down combo box selects its whole edit text when it receives focus (Windows `CBSetFocus`), so typing replaces the
  pre-filled word — before, the typed text was appended ("abcabc") and nothing was found. `CB_SETEDITSEL` / `CB_GETEDITSEL`
  now act on the edit;
- the status bar supports `WM_PRINT` / `WM_PRINTCLIENT` into a caller's DC (Notepad++ double-buffers its status bars),
  `SBT_OWNERDRAW` parts (`WM_DRAWITEM` with a comctl32-style `DRAWITEMSTRUCT`: CtlType 0, itemID = part, itemData = the
  `SB_SETTEXT` lParam) — also when the parent is a dialog with a dialog procedure — and `WM_ERASEBKGND` on a memory DC
  paints the bar colour (Notepad++'s `getCtrlBgColor` samples it);
- the WS_CLIPSIBLINGS pixel-snapshot clip protected at most 24 higher siblings per DC; the Find dialog's full-size tab
  control lies under ~60 controls, so the status bar (the last one) was overpainted by the tab pane. Limit raised to 128.
Result: Ctrl+F, type, Enter → selection jumps to the first match (Col 4), Enter again → next match (Col 8), the dialog's
status bar shows the green "Find: Reached document end, first occurrence from the top found." text; the editor's right-click
menu (Cut/Copy/Paste/…/Hide Lines) is complete; the same flow works in the 32-bit Notepad++ (`reports/npp32_fonts.png`).

**Tests.** `tests/font.c` 28/28 (x64 + x86: +symbol widths and ink); `tests/run.sh` all green; browser e2e 46/46.

**Known limits (unchanged / honest).** Symbol coverage is the 44 glyphs above plus what Cousine ships — emoji, Wingdings,
Marlett and MDL2 icon fonts still fall back to the 8x8 bitmap or `?`. "Find All in All Opened Documents" is a few pixels
wider in PT Sans than in MS Sans Serif, so that button caption touches its edges.

# v1.35.0 — real TrueType text: embedded font engine, proportional GDI everywhere, Cyrillic, font enumeration (Android)

**Why.** Until now every program drew text with an 8x8 bitmap font stretched into fixed-width cells, which is why
Notepad++ captions were clipped, dialogs looked nothing like Windows and Cyrillic rendered as boxes. Windows programs
assume a real font system: proportional advances, `GetTextExtentExPoint` partial widths for caret placement, dialog base
units derived from the dialog font, `EnumFontFamiliesEx` for font pickers. This release adds all of that as real behaviour
(no stub, no host dependency — the rasterizer is part of the emulator and runs identically on Android/WebAssembly).

**Font engine (emu/x64/ttf.inc).** A TrueType parser (`head`/`maxp`/`loca`/`glyf`/`cmap` formats 4 and 12/`hhea`/`hmtx`/
`OS/2`), simple and composite glyphs (nested transforms), quadratic contours flattened and rasterized with a signed-area
accumulation buffer (exact coverage, no supersampling; mild stem darkening for unhinted small sizes), a 4096-entry glyph
cache keyed by face/size/style/code point, synthetic italic (shear), underline and strikeout, horizontal scaling
(`lfWidth`). Faces are ordinary files in the guest file system, `C:\Windows\Fonts\{ptsans,ptsansb,cousine,cousineb}.ttf`
(PT Sans regular/bold for the UI — Latin, Cyrillic, Greek; Cousine regular/bold for monospace — metric-compatible with
Courier New; both SIL OFL, licences shipped next to them). `x64run` mounts `fonts/` next to the executable, the web/APK
runtime fetches them into the VFS before the program starts (`public/x64/fonts`, precached by the PWA). Without the files
everything falls back to the old bitmap glyphs.

**Font selection (CreateFont/CreateFontIndirect A/W).** `lfHeight < 0` = em pixels, `> 0` = cell height (converted through
`usWinAscent + usWinDescent`), `0` = 12 px; weight ≥ 600 picks the bold face; `FIXED_PITCH` or a monospace family name
(Courier*, Consolas, Lucida Console, Fixedsys, Terminal, Cascadia, *Mono, …) picks Cousine; `MS Shell Dlg`, `Microsoft Sans
Serif`, `Tahoma`, `Arial`, `Helvetica` are drawn 12 % condensed because dialogs were laid out for those narrower faces.
Stock fonts are real now (`SYSTEM_FONT` bold 12 px, `DEFAULT_GUI_FONT` MS Shell Dlg 11 px, fixed stock fonts Cousine);
`NONCLIENTMETRICS` reports Segoe UI 9 pt; tooltips use that 9 pt font.

**GDI metrics and measurement — proportional everywhere.** `GetTextExtentPoint32/GetTextExtentPoint A/W` read the
string (they used to multiply the length), `GetTextExtentExPoint A/W` (per-unit cumulative widths, `nFit`, surrogate
pairs), `GetCharWidth32/GetCharWidth/GetCharWidthFloat/GetCharABCWidths A/W` (new), `GetTextMetrics A/W` (real
ascent/descent/internal leading/average/max width, pitch-and-family flags, charset, proper A/W layouts), `GetTextFace
A/W` (the selected face name), `GetObject(LOGFONT A/W)` (height, weight, italic/underline/strikeout, pitch, face name),
`TextOut`/`ExtTextOut` alignment (`TA_RIGHT/CENTER/BASELINE/BOTTOM/UPDATECP`) from measured widths and the real ascent,
`DrawText` word-wrap by pixel width, `DT_END_ELLIPSIS`/`DT_PATH_ELLIPSIS`, `DT_CALCRECT` equal to the extent. GDI+
`DrawString`/`MeasureString` lay out with the same engine (family, bold/italic/underline/strikeout, wrap, alignment).

**Controls and dialogs.** The `Edit` control measures text instead of counting columns: caret, selection highlight,
click/drag hit testing, `ES_AUTOHSCROLL` scrolling (scrolls back after deletions), centre/right alignment, multi-line
word wrap at pixel width; combo boxes, list boxes, menus, tabs, toolbars, status bars, headers, list/tree views, tooltips
and group-box captions all go through `text_width`/`fnt_draw`. Dialog base units come from the dialog font (the 52-letter
average the way Windows derives them), with the exact Windows values for the two standard dialog fonts (8 pt → 6x13,
9 pt → 7x15) so templates designed on Windows keep their proportions.

**Font enumeration (new).** `EnumFontFamiliesEx A/W`, `EnumFontFamilies A/W`, `EnumFonts A/W` call the guest callback
per family (Segoe UI, Microsoft Sans Serif, Tahoma, Arial, MS Shell Dlg, Verdana, Courier New, Consolas, Lucida Console,
PT Sans, Cousine, System, Fixedsys, Terminal) with `ENUMLOGFONTEX` + `NEWTEXTMETRICEX` (`TRUETYPE_FONTTYPE`, Latin/
Greek/Cyrillic signature), honour the face filter and a `0` return — through continuations, x86 and x64.

**Tests.** `tests/font.c` (26 checks, x64 + x86 in `run.sh`): proportional extents, partial widths, char widths, metrics,
face names, `LOGFONT` round trip, enumeration (count / stop / filter), `DT_CALCRECT`/`DT_WORDBREAK`, Courier New 8 px cells,
Cyrillic. The Inno Setup GUI test clicks moved with the VCL layout (forms scale by the font's average width). Full native
suite (`run.sh`) and the browser e2e suite pass; Notepad++ x64 screenshots in `reports/` show the Find dialog and the
editor with real glyphs.

**Honest limits.** No hinting (small text is anti-aliased, not pixel-snapped like GDI); no kerning or OpenType shaping
(Arabic/Indic shaping, ligatures are not done; combining marks overprint); only two families are bundled, every other
family maps onto them (face names are still reported as requested); symbol fonts (Marlett, Wingdings, Segoe MDL2) fall
back to bitmap glyphs or `?`; `GetGlyphOutline`, `GetFontData`, `AddFontResource`, `GetKerningPairs` are not implemented;
ClearType-style sub-pixel rendering is not done (grayscale coverage only). Dialog proportions for fonts other than the two
standard sizes are computed, not table-driven, and can differ by a pixel or two from Windows.

# v1.34.0 — in-place rename (F2 / context menu / `LVM_EDITLABEL`), `IFolderView2`, DefView re-entry fix: Q-Dir x86/x64 (Android)

**Why.** Renaming a file is the one thing every file manager does from the list itself: F2 or *Rename* in the right-click
menu opens an edit box over the label, Enter commits and the shell renames the file on disk. In v1.33.0 `LVM_EDITLABEL`
was a no-op, and the second half of the problem was hidden behind it: after a right-click menu in the shell view, the
view's window procedure swallowed the *next* notification that arrived at the same stack depth (the modal re-entry
check matched on address only), so even a correct edit box never got its `LVN_BEGINLABELEDIT`. Everything below is real
behaviour, no stub.

**List-view label editing (emu/x64/listview.inc).** With `LVS_EDITLABELS`, `LVM_EDITLABELA/W` (`-1` cancels), F2 on the
focused item and `IFolderView2::DoRename` start an edit: the parent gets `LVN_BEGINLABELEDITA/W` (`NMLVDISPINFO`, a
`FALSE` return refuses), an `Edit` child (`ES_AUTOHSCROLL`, text selected, focused) is created over the label rectangle
of the current layout (report / icon / small icon / list), `LVM_GETEDITCONTROL` returns it; Enter commits and Escape
cancels (`LVN_ENDLABELEDITA/W` with the new text or `pszText = NULL`; a non-zero return applies the text to the item),
the box is also closed when the edit loses focus, when the item is deleted or the list is destroyed; the edit box is
destroyed from the list's own message (private `0x10FF`) after the notification chain — never from inside a
continuation. Keyboard: Home/End/←/→/Shift-selection, Delete/Backspace are the regular `Edit` control.

**Shell default view (emu/x64/defview.inc, ctxmenu.inc).** `LVN_BEGINLABELEDIT` is accepted for file-system items;
`LVN_ENDLABELEDIT` renames on disk through the VFS (`MoveFileW` semantics: the hidden Explorer extension is re-attached
when the label was shown without it, a name that already exists or an empty/`\ / : * ? " < > |` name is refused and the
old label stays), then the view re-enumerates and keeps the renamed item selected. The Explorer item context menu gained
*Rename* (`rename` verb, `GetCommandString` A/W) — it posts `LVM_EDITLABELW` for the clicked item, so the menu's own
modal finishes first. `IFolderView2` is now exposed by the view (`QueryInterface`): `Get/SetCurrentFolderFlags`,
`GetSortColumnCount/GetSortColumns/SetSortColumns` (name column, direction), `GetItem/GetVisibleItem/GetSelectedItem`,
`GetSelection` (`IShellItemArray` of the selected items), `GetSelectionState`, `Set/GetViewModeAndIconSize`,
`SetRedraw`, `IsMoveInSameFolder`, `DoRename`; grouping is reported as none (one flat list). x86 signatures added.

**Modal re-entry fix (defview.inc, user.inc).** The default view remembers the originating `NM_RCLICK` message
(`msg/wParam/lParam`) when it opens a context menu; a re-execution at the same `rip/rsp` is treated as the menu's
re-entry only if the arguments match, so other notifications that arrive at that depth (label editing, selection
changes) are handled normally and the menu's modal is finished when the real re-execution comes. This was the reason
the first rename attempt produced no edit box in Q-Dir.

**Verified on Q-Dir 12.82 (x64 and x86 installs inside the emulator):** right-click a file → *Rename* → edit box over
the name → type → Enter → the file is renamed in the mounted host folder (`oldname.txt` → `newname.txt`), the list shows
the new name selected; Escape leaves the file untouched. Test `tests/lvedit.c` (20 checks: begin/end notifications,
refuse, cancel, commit, edit control, focus loss; x86 and x64) is part of `run.sh` (235 checks, ALL OK), Playwright e2e
46/46 in the WebView engine.

**Known gaps (next).** No OLE drag & drop yet (`DoDragDrop/RegisterDragDrop` are not implemented — Q-Dir's drag between
panes does nothing); no *Sort by* / *Group by* sub-menus; no Recycle Bin (delete is permanent); `IShellView::GetItemObject`
does not serve `IID_IDataObject`; `SHAutoComplete`, `uxtheme #49/#106` and the dark-mode ordinals are reported as missing
by Q-Dir (it falls back); `CLSID_NamespaceTreeControl` is not provided (Q-Dir uses its classic tree instead).

Android-only build (APK via GitHub Actions); x86 and x64 paths tested with `emu/tests/run.sh` (native, both bitnesses)
and the Playwright e2e suite in the APK's WebView engine.

# v1.33.0 — Explorer context menus, real list-view modes and the shell View menu: Q-Dir x86/x64 (Android)

**Why.** A file manager hosting the shell's default folder view (`SHCreateShellFolderView` / `IShellView`) gets its
right-click menus, its «View ▸ Large icons / Small icons / List / Details» commands and the view-mode switching from
the shell itself. Q-Dir 12.82 builds its own *View* menu that way: in `WM_INITMENUPOPUP` it throws away its static
items and merges the background context menu of the active pane (`IShellView::GetItemObject(SVGIO_BACKGROUND,
IID_IContextMenu)` → `QueryContextMenu`). We returned `E_NOINTERFACE` there, so the menu had three items and nothing in
it changed the view. Everything below is real behaviour, not a stub.

**Explorer context menus (emu/x64/ctxmenu.inc).** A `CtxMenu` COM object (`IContextMenu`, `IContextMenu2`,
`IContextMenu3`) over a folder and a set of items: `QueryContextMenu` inserts the Explorer item set at the requested
position with ids from `idCmdFirst` (items: *Open, Explore, Cut, Copy, Paste, Delete, Properties*; background: *View ▸,
Refresh, Paste, New folder, Properties*; drive roots hide Cut/Delete; `CMF_DEFAULTONLY` honoured; the default verb is
bold), `GetCommandString` returns the verbs (`open explore cut copy paste delete properties refresh NewFolder
LargeIcons SmallIcons List Details`, A and W, `GCS_HELPTEXT` too) and `InvokeCommand` executes them for real: open =
`ShellExecute`/navigate, explore = `IShellBrowser::BrowseObject`, cut/copy = CF_HDROP (`DROPFILES`, wide) in the
clipboard and the «cut» ghosting, paste = copies/moves the clipboard files into the folder (including into a
sub-folder when one is selected), delete = removes files and directories recursively, new folder = creates
`New folder (n)`, refresh = re-enumerates the view, properties = the Properties sheet;
`HandleMenuMsg(2)` for owner-draw forwarding. `IShellFolder::GetUIObjectOf(IID_IContextMenu)` and the default view's
own right-click (on an item and on the background) return the same object, so Explorer-style hosts and the built-in
view behave identically.

**View modes.** `IShellView::GetItemObject` now serves `SVGIO_BACKGROUND` (0), `SVGIO_SELECTION` (1) and `SVGIO_ALLVIEW`
(2) for `IID_IContextMenu/2/3`. The background menu carries the *View* sub-menu with the four modes, the current one
radio-checked; choosing one switches the view through the real list-view (`LVM_SETVIEW`) and is remembered in the
`IShellView` (`FOLDERSETTINGS.ViewMode`). The list-view control itself (emu/x64/listview.inc) gained the real
`LVS_ICON`, `LVS_SMALLICON` and `LVS_LIST` layouts next to `LVS_REPORT`: icon grid with wrapped two-line labels,
small-icon and list (column-major, horizontal scrolling) layouts, `LVM_SETVIEW/GETVIEW` (`LV_VIEW_*`), `LVM_ARRANGE`,
`LVM_GETITEMPOSITION/SETITEMPOSITION`, `LVM_GETITEMSPACING`, `LVM_GETORIGIN`, hit-testing and keyboard navigation per
layout (←→ moves between columns in list mode, PgUp/PgDn by page), `LVM_GETITEMRECT` for `LVIR_BOUNDS/ICON/LABEL`,
`LVM_SCROLL` and `EnsureVisible` for all four; scroll bars follow the layout (vertical for icon/report, horizontal for
list). Test `tests/lvview.c` (25 checks, x86 and x64) is part of `run.sh`.

**shlwapi `IUnknown_SetSite` (ordinal 174)** — host sites the menu object: a guest `QueryInterface(IID_IObjectWithSite)`
→ `SetSite(site)` → `Release` chain executed as a continuation (three guest COM calls), so it works for the program's own
objects as well as ours, x86 and x64.

**Fixed on the way.** `ToolbarWindow32` forwards `WM_COMMAND` to its parent like comctl32 — Q-Dir tracks its menus with
the toolbar as the owner window and handles the command in the main window's subclass; a sub-menu that overlaps its
parent menu by its 3 px shadow no longer repaints it in a loop (82 000 `WM_PAINT` per second, the sub-menu never showed);
`DestroyMenu` of a menu that is still attached as a sub-menu leaves the parent intact (Q-Dir's installer
`InitLangCombo-ERR`). Debug aid: `X64_MENUDUMP=1` logs the items of every popup as it is sized.

**Seen on Q-Dir 12.82 (x64 and x86 installs inside the emulator):** *View* menu = Status Bar, Full Screen, View ▸ (Large
icons / Small icons / List / Details), Refresh, New folder, Properties, «For the Active Q-View 1»; picking *Large icons*
or *Small icons* re-lays the active pane; right-click on a drive / folder / background shows the Explorer menus and the
verbs act on the mounted folders.

**Known gaps (next).** `IFolderView2` is not exposed by the view yet (Q-Dir asks for it from its toolbar view button); no
*Sort by* / *Group by* sub-menus; no Recycle Bin (delete is permanent); no in-place rename yet (`LVM_EDITLABEL` is a
no-op in this version).

Android-only build (APK via GitHub Actions); x86 and x64 paths tested with `emu/tests/run.sh` (native, both bitnesses)
and the Playwright e2e suite in the APK's WebView engine.

# v1.32.0 — real tooltips (tooltips_class32): toolbar hints in Notepad++ and Q-Dir, x86 and x64 (Android)

**Why.** `tooltips_class32` was the last common control that was still a placeholder («not implemented yet — shown as an
empty box»): every real program creates one (toolbars with `TBSTYLE_TOOLTIPS`, Q-Dir's own tooltip windows, Notepad++'s
document tabs), and a hint that never appears is a visible difference from Windows.

**Tooltip control (emu/x64/tooltip.inc).** Tools are registered with `TTM_ADDTOOL` (a window — `TTF_IDISHWND` — or a
rectangle of a window's client area; text inline, a string resource of `hinst`, or `LPSTR_TEXTCALLBACK`); hover tracking
runs from the message pump: the pointer resting on a tool for the initial delay (500 ms, `TTM_SETDELAYTIME`) posts a
`WM_TIMER` to the tooltip window, whose procedure fetches the text — for callback tools through `WM_NOTIFY
TTN_GETDISPINFOW/A` to the tool's window (`lpszText` = `szText`, a pointer, a string-resource id + `hinst`, or
`TTF_DI_SETITEM` to cache it) — and shows the bubble below the cursor (white, 1 px grey border, 3 px margins, multi-line
text, word-wrap at `TTM_SETMAXTIPWIDTH`, optional title, `TTF_CENTERTIP`, tracking tooltips through
`TTM_TRACKACTIVATE/TRACKPOSITION`); it hides when the pointer leaves the tool, a button goes down or after the autopop time
(5 s), reshows after 100 ms on the next tool, never steals activation or focus, and is transparent to the mouse.
Messages: `TTM_ACTIVATE, ADDTOOL, DELTOOL, NEWTOOLRECT, RELAYEVENT, GETTOOLINFO, SETTOOLINFO, HITTEST, GETTEXT,
UPDATETIPTEXT, GETTOOLCOUNT, ENUMTOOLS, GETCURRENTTOOL, WINDOWFROMPOINT, TRACKACTIVATE, TRACKPOSITION, SETTIPBKCOLOR,
SETTIPTEXTCOLOR, GETDELAYTIME, SETDELAYTIME, GETTIPBKCOLOR, GETTIPTEXTCOLOR, SETMAXTIPWIDTH, GETMAXTIPWIDTH, SETMARGIN,
GETMARGIN, POP, UPDATE, GETBUBBLESIZE, ADJUSTRECT, SETTITLE, POPUP, GETTITLE` (A and W variants), `WM_SETFONT/GETFONT`.
TOOLINFO V1/V2/V3 sizes, x86 and x64 layouts.

**Toolbars.** A `ToolbarWindow32` created with `TBSTYLE_TOOLTIPS` owns a tooltip window (`TB_GETTOOLTIPS`,
`TB_SETTOOLTIPS`) whose tools are its buttons: hovering a button sends `TTN_GETDISPINFO` with the button's command id to the
toolbar, which forwards it to its parent exactly like comctl32 (through the ReBar when the toolbar sits in a band) and falls
back to the button's own label when the parent leaves the text empty.

**Fixed on the way (both found with Notepad++).** `ChildWindowFromPointEx` honours `CWP_SKIPINVISIBLE / SKIPDISABLED /
SKIPTRANSPARENT` and returns the topmost child (a parked invisible Scintilla at 0,0 no longer shadows the ReBar);
`RealChildWindowFromPoint` added; menu lookup by command id is depth-first in item order like user32 — Notepad++ has
`IDM_FILE_CLOSE` both as File ▸ Close and as the «✕» button at the right of the menu bar, and `GetMenuString` must return
«Close».

**Verified.** Notepad++ 8.8.9 x64 and x86: hovering the toolbar shows «Save» / «Close» (the real text produced by
Notepad++'s own `TTN_GETDISPINFO` handler via `GetMenuString`); Q-Dir x64: «Up [BACKSPACE] / Please test R-mouse click :-)»
(Q-Dir's own two-line tooltip). New tests `tooltip.exe` / `tooltip32.exe` (17 checks each: window tool, rectangle tool,
callback text, toolbar tooltip through the parent, show/hide timing, `TTM_GETCURRENTTOOL/GETTEXT/GETTOOLCOUNT/DELTOOL/
SETMAXTIPWIDTH`). Native suite 229 ok (ALL OK), browser e2e 46/46.

**Limits.** `TTN_SHOW/TTN_POP/TTN_NEEDTEXT`-time repositioning by the owner is not sent; balloon style (`TTS_BALLOON`) and the
title icon draw as a plain rectangle; `TTM_RELAYEVENT` is accepted but hover tracking is global (identical result for
programs that relay mouse messages).

# v1.31.0 — GDI+, shell icon services and the real shell folder view: Q-Dir (x64 and x86) browses the virtual disk (Android)

**Why.** Q-Dir 12.8x (a real quad-pane file manager, ATL/WTL, ~2 MB) was the next «works like on Windows» target after
Notepad++, 7-Zip, PuTTY and Inno. It needed three Windows subsystems that did not exist at all: GDI+ (its toolbars are
PNG resources drawn through `GdipDrawImage`), the shell icon services (`SHGetFileInfo`, system image lists) and the
shell's default folder view — Q-Dir does not list folders itself, it asks `IShellFolder::CreateViewObject(IID_IShellView)`
for the Explorer view object and embeds its window (`SHELLDLL_DefView` + `SysListView32`), exactly like the common
file-open dialog and every Explorer-hosting program.

**GDI+ flat API (emu/x64/gdiplus.inc, ~330 `Gdip*` entry points).** `GdiplusStartup/Shutdown`; images as 32-bpp GDI
bitmaps (`GdipCreateBitmapFromScan0/FromHBITMAP/FromHICON/FromFile/FromStream/FromResource`, PNG/BMP/ICO decoding through
the WIC code already used for icons, PNG/BMP save, `GdipBitmapLockBits/UnlockBits`, `GdipCreateHBITMAPFromBitmap`,
`GdipCreateHICONFromBitmap`, `GdipCloneBitmapArea`, `GdipGetImagePixelFormat/Width/Height/Bounds`); Graphics on an HDC,
an HWND or an image with world transform (translate/scale), clipping, compositing and interpolation modes; `GdipDrawImage*`
(alpha blending, scaling with box/bilinear filters, negative widths = mirrored, `ImageAttributes` colour matrix / colour key
/ gamma), solid / linear-gradient / texture brushes, pens (width, dash style, caps), lines, rectangles, ellipses, polygons, paths
(`GdipCreatePath`, `AddPathLine/Arc/Rectangle/Ellipse/Polygon`, fill / draw / `GdipDrawPath`), `GdipDrawString/MeasureString`
with GDI fonts (`GdipCreateFontFromDC/FromLogfontW`, families, string formats), `GdipGraphicsClear`, `GdipSetClipRect`,
`GdipGetDC/ReleaseDC`. Plus the pieces Q-Dir needed around it: IStream `CopyTo/Clone`, `CreateStreamOnHGlobal`,
`GetHGlobalFromStream`, `SHCreateMemStream`, `wvsprintfA/W`, `CopyRect/IsRectEmpty/EqualRect`, `GlobalReAlloc/Handle/Flags`,
`SHChangeNotifyRegister`.

**Shell icon services (emu/x64/shellicon.inc).** System image lists SHIL 0–4 (16/32/48/256 px and the “extra large”)
seeded with the shell32 icon set; per-file icons by type, including the icon embedded in `.exe/.dll/.ico` files read from
their PE resources on the virtual disk; `SHGetFileInfoA/W` complete (icon, system icon index, display name, type name,
attributes, exe type, PIDL input, `SHGFI_USEFILEATTRIBUTES`, link overlay, selected state; NULL pidl = Desktop);
`ExtractIcon/ExtractIconEx/ExtractAssociatedIcon/PrivateExtractIcons/SHDefExtractIcon`, `SHGetStockIconInfo`,
`SHGetImageList` + the `IImageList`/`IImageList2` COM object, `Shell_GetImageLists`, `Shell_GetCachedImageIndex`. System
DLL pseudo-modules are now backed by a real empty PE image (MZ/PE headers, export directory, empty `.rsrc`), so programs
that walk `GetModuleHandle` results as PE headers (Q-Dir, many ATL programs) no longer fault.

**Shell default folder view (emu/x64/defview.inc).** `IShellFolder::CreateViewObject(IID_IShellView)` returns a real view
object (`IShellView`, `IShellView2`, `IFolderView`): `CreateViewWindow` makes a `SHELLDLL_DefView` child of the host's
`IShellBrowser::GetWindow` with a `SysListView32` filled from the virtual disk (system image list icons, Name/Size/Type/Date
columns, folders first), `UIActivate`, `Refresh`, `GetCurrentInfo`, `SelectItem`, `GetItemObject`, `IFolderView::ItemCount
/ Item / GetFocusedItem / GetSelectionMarkedItem / SelectItem`; the host subclassing the view and the list (Q-Dir does both)
works because the windows are created with `WM_CREATE` and shown like Windows does. Activation of a folder calls the
host's `IShellBrowser::BrowseObject(child pidl, SBSP_RELATIVE)`, Backspace `BrowseObject(NULL, SBSP_PARENT)` — the real
DefView protocol, so the host (Q-Dir, a file-open dialog) stays in control of navigation; activating a file goes through
`ShellExecute`. Only `LVN_ITEMACTIVATE` opens an item (a double click or Enter), as on Windows.

**Windows-fidelity fixes found with Q-Dir.** `ReleaseDC` on a memory DC is a no-op that returns 0 (Q-Dir calls it on its
double-buffer right before blitting from it — the address bars were black rectangles); toolbar button text strips `&`
mnemonics (`&File` → `File`) and measures the stripped label; `lstrcpy/lstrcpyn/lstrcat/lstrlen` are SEH-protected like on
Windows (bad pointer → NULL/0 instead of a crash — Q-Dir passes NULL once at start-up); 32-bit zero-argument stdcall APIs
(`GetVersion`, `TlsAlloc`, `GetCurrentThreadId`) are no longer reported as “unknown cleanup size”.

**Proof (native runner, both bitnesses).** Q-Dir 12.82 x64 and Q-Dir 12.83 x86 (Unicode), portable zips from softwareok.com,
mounted as `C:\Program Files\Q-Dir`: licence dialog → main window with four panes, each with address bar (GDI+ toolbar +
`ComboBoxEx32` showing the folder), tab (`SysTabControl32` with icon and name), the embedded Explorer view listing
“Local Disk (C:)” of the Desktop; a double click on the drive navigates the pane through Q-Dir's own `BrowseObject` →
address bar `C:\`, tab “Local Disk (C:)”, list of the virtual disk's folders (nebula, Program Files, tmp, Users, Windows),
status bar “5 Objects … Free: 1.99 GB of 4.00 GB”. 30 s runs, no faults, no unsupported-API reports.

**Honest limits.** The folder view is details-only (no icon/tile/thumbnail modes yet), no in-place rename, no drag & drop,
no context menus from `IContextMenu` (`GetItemObject` returns `E_NOINTERFACE` for them), `IFolderView2`
(`{1af3a467-…}`) and `IShellFolderViewDual` are not implemented — Q-Dir asks for them and falls back. GDI+ has no
anti-aliased path rasteriser (shapes are drawn with GDI quality), no metafiles, no arcs/Béziers/curves as direct draw calls (only inside
paths), image decoding is PNG/BMP/ICO only (JPEG/GIF/TIFF files are rejected with `UnknownImageFormat`), encoding PNG/BMP
only, `GdipDrawDriverString` draws glyph indices as plain characters, no regions beyond rectangles. Q-Dir's
tooltips (`tooltips_class32`) are still empty boxes; its `SHAutoComplete`, `uxtheme` dark-mode and `Magnification.dll`
imports are reported missing and skipped. Speed: Q-Dir runs at ~5–6 MIPS in the native runner (ATL message loops poll a
lot), a phone will feel slow but usable.

# v1.30.0 — OpenGL on the device's WebGL2: Shattered Pixel Dungeon (Java/LWJGL) reaches its title screen; NSIS installers really run their plugins (Android)

**Why.** Every OpenGL program (LWJGL/libGDX games, GLFW/SDL programs, Java games) stopped at `ChoosePixelFormat` /
`wglCreateContext` — opengl32.dll did not exist. Following «всё чего нету бери напрямую через то, с какого устройства
запущено», opengl32 is now a bridge: GL calls made by the emulated program are executed on the Android device's own WebGL2
context, the frame is read back into the program's window like any other Windows window.

**OpenGL bridge (emu/x64/opengl.inc, emu/x64/gl_funcs.inc, src/lib/x64/gl.ts, src/lib/x64/gltable.ts, emu/tools/glgen.py).**
- opengl32.dll + gdi32 pixel formats: `ChoosePixelFormat/SetPixelFormat/DescribePixelFormat/GetPixelFormat`, `SwapBuffers`
  (paced to the swap interval), `wglCreateContext`, `wglCreateContextAttribsARB` (version/profile/flags honoured up to
  3.3 core), `wglMakeCurrent` / `wglMakeContextCurrentARB`, `wglShareLists`, `wglGetProcAddress`,
  `wglGetExtensionsString{ARB,EXT}`, `wglChoosePixelFormatARB` / `wglGetPixelFormatAttrib{iv,fv}ARB`, `wglSwapIntervalEXT`.
  Multiple contexts and windows per process (GLFW creates a hidden probe window first).
- 762 GL entry points in one generated table shared by C and TypeScript (ids identical on both sides): GL 1.1 exports of
  the real opengl32.dll (361 names from Wine's opengl32.spec) + GL 1.2 … 3.3 core + the ARB/EXT extensions WebGL2 can back
  (VBO, VAO, FBO, shader objects, sync, sampler objects, instancing, transform feedback, uniform buffers …).
  `glGetString` reports `3.3.0 NebulaGL (WebGL2 bridge)` and the device's real renderer string; `GL_EXTENSIONS` /
  `glGetStringi` list only what is actually implemented; `wglGetProcAddress` returns NULL for anything else (so LWJGL,
  GLEW and GLAD mark those extensions unavailable instead of crashing later).
- GLSL → GLSL ES: `#version 110/120` → ESSL 1.00 (attribute/varying, gl_FragColor, texture2D), `#version 130…330` →
  `#version 300 es` (in/out renamed, `texture*` → `texture`, `gl_FragColor` → declared out, `layout(location)` kept,
  precision prelude only where ESSL allows it). `GL_QUADS` draws go through a generated index buffer; client-side vertex
  arrays through a scratch VBO; `glMapBuffer` is a CPU copy written back on unmap.
- x86 (32-bit) programs: the same bridge; `GLintptr/GLsizeiptr` are 4 bytes on x86 and 8 on x64 (new signature letter
  `z`), `GLint64/GLuint64` 8 on both, doubles on the x86 stack, `APIENTRY` = stdcall cleanup computed per signature.
- Programs importing opengl32.dll are classified *partial* (what works / what does not) instead of *unsupported*.

**Proof.** `gltri.exe` / `gltri32.exe` (emu/tests/gltri.c, also in the Programs samples): pixel format, WGL context 3.3,
GLSL 330 VAO/VBO triangle + GLSL 120 `GL_QUADS` client-array quad, `glReadPixels` checks the colours, `SwapBuffers`,
`glBegin` correctly fails with `GL_INVALID_OPERATION` in a core profile. e2e reads the screen back and finds the yellow quad
and the shaded triangle, x64 and x86. **Shattered Pixel Dungeon v4.0.2 (Windows/GOG build, a real Java 17 + LWJGL 3 +
libGDX game, 108 MB zip)** installed from the device through “Open .exe / .zip…”: HotSpot starts, GLFW creates its window,
LWJGL binds GL through `wglGetProcAddress`, the title screen (logo, text, “Enter the Dungeon”) is drawn by the WebGL2
bridge and the music plays through the audio bridge (e2e/shots/site-spd-2-run.png). It needed on top: `SetThreadExecutionState`
(the game crashed on it right after window creation).

**NSIS 32-bit installers (Notepad++ x86 setup) now run their real plugins.** `SetWindowLongW(hDlg, DWLP_DLGPROC)` returned 0
instead of the previous procedure, so nsDialogs.dll chained to NULL and swallowed every button click after the language
dialog (the installer looked “stuck” on the components page). Fixed in user.inc for x86 and x64 (`DWLP_MSGRESULT/DLGPROC/USER`
= 0/4/8 on x86, 0/8/16 on x64). The native test drives the whole wizard: LangDLL → license → location → components (nsDialogs)
→ install → finish, 17 files land in the chosen folder.

**Test runner.** `X64_GL_NULL=1` gives the native runner a null OpenGL driver (version strings, limits, “compiled/linked”,
generated names; nothing drawn) so GL programs run to their game loop under `run.sh` and `--apitrace` — Shattered Pixel
Dungeon renders 1 190 frames in 8 minutes natively this way. `X64_GL_TRACE=1` logs every GL call. `e2e/sitecheck.mjs
<zip|exe>` tries a third-party program through the UI exactly as a user would and keeps screenshots; files over 50 MB are
passed by path.

**Honest limits.** WebGL2 has no fixed-function pipeline: `glBegin/glEnd`, matrix stack, display lists, `wglUseFontBitmaps`,
alpha test, `glPolygonMode(GL_LINE)`, `glPointSize` > 1, 1D / rectangle / multisample / buffer textures, geometry and
tessellation shaders, `BaseVertex` draws, program binaries, `glCopyImageSubData`, `GL_TIME_ELAPSED` queries are not
available — they return `GL_INVALID_OPERATION` (one log line each) and `wglGetProcAddress` says NULL for the extension
names, so programs that need GL ≤ 1.5 immediate mode or GL 4.x will not draw. `glClientWaitSync` never blocks. Speed:
Shattered Pixel Dungeon is a JVM with its JIT output interpreted by the emulator — the title screen appears after
~7 minutes at 15–20 MIPS in the headless test browser and runs at a few frames per second; a phone will be in the same
range. The game reserves 2.6 GB of guest address space (HotSpot heap) — devices with little RAM may not get there.
Q-Dir (GDI+) and the DirectInput8 class (`CoCreateInstance` for SDL/jamepad gamepads) are the next missing pieces.

# v1.29.0 — File Explorer and Notepad on the real C:\ (Android)

**Why.** File Explorer showed a hard-coded demo tree (“118 GB free of 237 GB”, fake exe sizes) and Notepad a canned text —
stubs. The desktop now has ONE disk: the persistent C:\ the emulator's Windows programs use (IndexedDB, restored before
any program runs). Explorer lists exactly what is there; Notepad reads and writes the same bytes Notepad++ / 7-Zip inside
Programs see.

**File Explorer (src/components/apps/ExplorerApp.tsx, src/lib/x64/diskbus.ts).**
- Browses the persistent disk: Program Files, ProgramData (Start Menu shortcuts), Users\user, Windows (the registry hive).
  Implicit parent folders are shown (installers write deep paths without a mkdir for every level). Date modified, type,
  real sizes; `.exe` rows show the icon extracted from the file's PE resources.
- Double-click (or a second tap on a touch screen): `.exe`/`.com` → starts in **Programs** in its own folder; `.bat`/`.cmd`
  → `cmd.exe /c`; `.lnk` → the shortcut target with its arguments and working directory (MS-SHLLINK parsed); text files →
  Notepad; other binaries → downloaded to the device.
- Import files from the device (file picker / drag-and-drop), New folder, Rename (files and whole folders), Delete
  (recursive), Download. Every change goes to the store AND to every live emulator instance, so a program started next
  sees it; before listing, Explorer asks live emulators to flush what their programs have written.
- “Local Disk (C:)” free space is `navigator.storage.estimate()` — the browser's real quota for this origin on the device.
- Temporary paths (C:\tmp, …\Temp, C:\nebula scratch) are deliberately not persisted and are not listed.

**Notepad (src/components/apps/NotepadApp.tsx, src/lib/docs.ts).** Opens files from Explorer (UTF-8 / UTF-8 BOM /
UTF-16 LE/BE detected), Save / Save As… to any path on C:\ (default C:\Users\user\Documents), Open from the device,
Download to the device. Ln/Col from the real caret.

**Programs.** `requestX64Launch` accepts a full path on the disk (+ working directory) — the Explorer launch path. A closed
Programs window now releases its emulator: `X64Runtime.dispose()` leaves the disk bus and closes its IndexedDB connection.
Found with the new `e2e/memcheck.mjs` (7 open/run/close cycles): with an open IDB connection holding an `onversionchange`
handler the renderer grew ~115 MB per cycle and never came back; now it plateaus at ~425 MB like v1.28.1.

**Tests.** New e2e “explorer + notepad”: after the 7-Zip install test, Explorer navigates to C:\Program Files\7-Zip
(15 items, 7zFM.exe = 998 912 bytes with its icon), double-click starts 7zFM.exe in Programs (frames drawn); a file
imported from the device opens in Notepad, is edited (Cyrillic) and saved (61 bytes), survives a reload, is visible from
the Programs Files tab under C:\Users\user\Documents, rename / new folder / delete work. Local: 43/44 (the .NET pack
exists only on the GitHub build). `E2E_GREP` now also takes a regex (`/seh32|cmd\.exe/`).

**Honest limits.** The Command Prompt app's `dir` still walks the old demo tree (its `real` command opens the actual
cmd.exe in Programs, which does see C:\). No file preview for images/PDF — they are downloaded. Large (multi-GB) games
still need the OPFS-backed disk (next).

# v1.28.1 — Programs & Games library (Android): real installers run from the device, a persistent C:\ drive, tiles with the programs' own icons, Play / fullscreen — the fake Steam / Epic demos are gone

**Why.** The “x64 Runner” was a developer tool (a list of sample exes and a Run button) and Steam / Epic Games were stand‑in windows with a
`setInterval` progress bar. The user flow on Android must be the Windows one: open a setup program from the phone → it runs with its own
wizard → the installed program appears with its icon → Play → its screen, full‑screen for games — and it is still there after the app restarts.

**What is real now** (measured: `emu/tests/run.sh` native suite x86 + x64 **ALL OK**; e2e headless Chromium incl. the new end‑to‑end install
test — the official `7z2501-x64.exe` setup → `7-Zip File Manager` tile with the icon from `7zFM.exe` → Play draws the 7‑Zip window → page reload →
the tile is restored from IndexedDB and Play works again):

* **Persistent disk C:\** (`src/lib/x64/disk.ts`, `host_fs_*` imports in `emu/x64/host_wasm.c`): every file a program writes and closes is
  handed to the page and stored in IndexedDB (`nebula-x64-disk`) — installed programs, the registry hive `C:\Windows\nebula.reg`, Start Menu
  `.lnk` shortcuts, saved documents, configs. Deletes and renames follow. On start everything is put back before any program runs
  (`x64_vfs_sync` flushes files a program left open; `x64_chdir` sets the shortcut's working directory). Scratch paths are not persisted:
  `C:\tmp`, `*\Temp\*`, the `C:\nebula` session folder. Limit: the disk lives in memory while the app runs (fine for programs of tens or a few
  hundred MB; a lazy‑loading OPFS disk is the next step for multi‑GB games).
* **Programs app** (renamed from x64 Runner; desktop, Start menu, taskbar): **Library** tab — tiles of what is installed, found exactly the way
  Windows finds it: Start Menu / Desktop `.lnk` files (parsed MS‑SHLLINK: target, arguments, working dir, icon) and the registry
  `HKLM\…\CurrentVersion\Uninstall` keys (`DisplayName`, `DisplayIcon`, `InstallLocation`, `UninstallString`, `Publisher`, `DisplayVersion`).
  Nothing is invented: no shortcut / Uninstall key pointing at an exe that exists → no tile. Helper links (Uninstall, Help, readme, website…) are
  filtered out. **Open / Install…** opens the device file picker: a setup `.exe` runs at once with its real wizard (the Screen tab opens on its
  first frame, the library card shows *Installing…*, frames, processes, MIPS, files written / saving / saved); when it exits the library rescans
  and a toast names what was installed. A plain `.exe` runs at once too; **Add to library** copies it to `C:\Program Files\<name>\` with a real
  Start Menu shortcut. A **.zip** of a portable program / game folder is unpacked (browser `DecompressionStream`, ZIP64 aware) to
  `C:\Program Files\<folder>` with a shortcut to the main exe — on Android there is no folder picker, the archive is the unit. **Install folder…**
  does the same from a directory picker where the WebView offers one.
* **Icons** straight from the PE resources (`src/lib/x64/peicon.ts`): `RT_GROUP_ICON` → best `RT_ICON` (PNG entries passed through, DIB
  32/24/16/8/4/1 bpp with AND mask decoded by hand), PE32 and PE32+; honours the shortcut's icon path / index. **Set cover image…** per tile
  (device photo, stored locally) — the game's artwork over its icon, like a launcher.
* **Play** starts the installed exe from its own folder (`x64_chdir`) with the shortcut's arguments; **fullscreen / game mode** button on the
  Screen tab: the whole app becomes the program's screen (WebView `requestFullscreen` where available + kiosk layout), touch bar and soft keyboard
  stay; back gesture / button leaves it. **Uninstall** runs the program's own uninstaller from `UninstallString` (its real dialog); programs
  without one are removed from `C:\Program Files\<name>` with their shortcut.
* **Emulator fix found by this flow:** `cpu_reset` now clears `mode32` — a Proc slot reused after a 32‑bit process (the 7‑Zip x64 setup is a 32‑bit
  stub) made the next 64‑bit exe run in IA‑32 mode (`call [rip+disp]` decoded as `[disp32]` → access violation at its first import). Affects a
  64‑bit program started after a 32‑bit one in the same session and 32‑bit launchers spawning 64‑bit children.
* **Fake apps removed:** Steam and Epic Games Launcher windows (scripted “installs” of Nebula Strike). A real Steam client cannot run here
  (no raw TCP from a WebView page); saying so is better than a demo.
* The self‑test / sample programs (hello.exe, threads.exe, sqlite3, jq, lua, python, NebulaStrike…) moved behind a **tests** toggle and download on
  demand — the app no longer pulls ~40 MB of samples at start.

**v1.28.1** = v1.28.0 + one e2e fix: the d3d11tri test reads the console after explicitly selecting the Console tab (a console program that opens a window is shown on the Screen tab once its first frame arrives — a race that failed the v1.27.0 CI run on the runner, so v1.27.0 got no APK).

**Honest limits.** One Windows program (process tree) at a time per Programs window. MSI packages do not run (no `msiexec`). The library relies on
the installer leaving a Start Menu shortcut or an Uninstall key (NSIS, Inno Setup, 7‑Zip's SFX setup and MSI‑less installers do); a bare copy of
files shows up through *Add to library* / zip install. Android `Install folder…` depends on the WebView's directory picker (often absent) — use a
.zip. Network inside programs is still absent.

# v1.27.0 — Nebula x64 (Android): real Windows hooks (WH_KEYBOARD / WH_CALLWNDPROC / CallNextHookEx), IFileDialogEvents, IOleWindow; Notepad++ Save As appends the extension itself (x86 + x64)

**Why.** In v1.26.0 Notepad++ saved `hello` instead of `hello.txt`. On Windows the extension is not added by the dialog — Notepad++ does it
itself through a chain of real APIs: `IFileDialog::Advise` events (`OnTypeChange` → `OnFolderChanging` → `OnFolderChange` → `OnSelectionChange`,
`OnFileOk`), `IOleWindow::GetWindow` to get the dialog HWND, `EnumChildWindows` + `FindWindowEx(ComboBox, "Edit", "")` to find the file‑name
edit and the OK button, a `WH_KEYBOARD` hook that catches Enter in that edit and rewrites the name with `GetFileName`/`SetFileName`, and a
`WH_CALLWNDPROC` hook that sees the OK button's `WM_COMMAND`. All of these were stubs (hooks returned a handle and were never called; `Advise`
accepted the sink and never called it; there was no `IOleWindow`; the name field was a plain Edit). They are real now — no `SetDefaultExtension`
hack, the program does exactly what it does on Windows.

**Measured** (`emu/tests/run.sh` native suite x86 + x64 **ALL OK** (219 checks incl. the two new Notepad++ Save As checks); `tests/fdlg.exe` /
`fdlg32.exe` **FDLG OK** with the new stage 5; Notepad++ **x64 and x86**: `Ctrl+Alt+S` → type `hello` → Enter → `hello.txt` created with the
typed text; e2e in headless Chromium 23/24, the only failure is the .NET pack test that needs the GitHub‑built pack):

* **Windows hooks** (`user.inc`): `SetWindowsHookExA/W`, `UnhookWindowsHookEx`, `CallNextHookEx` keep a real per‑process hook table.
  `WH_KEYBOARD` is called when a key message is retrieved — from `GetMessage`, `PeekMessage(PM_REMOVE)` **and from the modal dialog loop**
  (modal dialogs pump their own messages); a non‑zero result discards the key (the program receives `WM_NULL` in its place, the modal loop simply
  drops it). `WH_CALLWNDPROC` is called with a heap `CWPSTRUCT` (x64 32 B / x86 16 B layouts) before every message sent to a window of the
  process — `SendMessage`, control notifications, dialog‑manager `WM_COMMAND` — with a re‑entry guard (hook procs may `SendMessage` themselves).
  `CallNextHookEx` walks to the next hook of the same type (verified with two keyboard hooks chained). Other hook types are accepted and kept but
  never called (`WH_MOUSE`, `WH_CBT`, `WH_GETMESSAGE`, low‑level hooks).
* **IFileDialogEvents** fired from the dialog in the order of the real dialog: `OnTypeChange` → `OnFolderChanging(IShellItem)` → `OnFolderChange`
  before the name is shown, then `OnSelectionChange`; `OnTypeChange` again on every change of the “Files of type” combo; `OnFileOk` when the user
  accepts (`S_FALSE` keeps the dialog open). `GetFileName` / `GetFolder` read the **live** dialog (the edit text, the current folder) while it is
  shown; `SetFileName` while shown updates the edit (that is how Notepad++ rewrites `hello` → `hello.txt`).
* **IOleWindow** on the dialog object (`QueryInterface(IID_IOleWindow)`, `GetWindow` → dialog HWND).
* **File‑name field is a ComboBox (CBS_DROPDOWN) with a real Edit child**, like comdlg32 — found by `FindWindowEx(hCombo, NULL, "Edit", "")`;
  `GetFocus()` returns that Edit while typing, Enter → `IDOK`. `FindWindowEx` now distinguishes `NULL` (any title) from `""` (empty title).
* **shlwapi**: `PathCombineA/W`, `PathAppendA/W`, `PathCanonicalizeA/W` (join + `.`/`..` normalisation) — Notepad++ calls `PathCombineW` in the
  Save As path.
* **32‑bit robustness**: a 32‑bit call to a Win32 shim whose stdcall cleanup size is unknown is now reported (`emu/tools/stdcall32.py` must be
  re‑run after adding shims). This is exactly what broke x86 Save As during development: `PathCombineW` without a cleanup entry left 12 bytes on
  the caller's stack, `edi` was restored from the wrong slot and Notepad++'s `_currentType` read garbage.
* **Tests**: `tests/fdlg.c` stage 5 (what Notepad++'s `CustomFileDialog` does: events sink in C, `IOleWindow`, `EnumChildWindows` →
  ComboBox/Edit + leftmost push button, two `WH_KEYBOARD` hooks + `WH_CALLWNDPROC`, a key eaten by the hook, name rewritten on Enter →
  `C:\nebula\draft.md`); `run.sh`: Notepad++ x64 **and** x86 `Save As hello → hello.txt` with content check; the About/Open chain check now
  prints the offending line and has more timing margin for the slower GitHub runner.

**Honest limits.** No overwrite prompt (`OnOverwrite` is never fired); only the two hook types above are called; `WH_KEYBOARD` is not called for
`PeekMessage(PM_NOREMOVE)`; hooks are per process (no thread filter); still single selection, no places bar, no custom controls drawn, bitmap
font clipping in dialogs. Android only — no other platform is built or tested.

# v1.26.0 — Nebula x64 (Android): real file dialogs (Open / Save As, Win32 + COM), Windows z‑order, owner‑draw controls; Notepad++ opens and saves files

**Why.** A Windows program that cannot open or save a file is a toy. Notepad++ (x86 and x64) showed its editor, accepted typing — and
`Ctrl+O` / `Ctrl+Alt+S` did nothing, because the Vista‑style **IFileOpenDialog / IFileSaveDialog** COM objects and `GetOpenFileName` were
stubs, and the accelerator table (`LoadAccelerators` / `TranslateAccelerator`) was not wired. On top of that the editor went **blank**
after any dialog (About, Open): the document tab bar painted over the Scintilla view. The root cause was a wrong z‑order model, now
replaced by the Windows one. Everything below was tested on **x86 and x64 together** before it was committed.

**Measured** (`emu/tests/run.sh` **220 native checks ALL OK**, x86 + x64; `tests/fdlg.exe` / `fdlg32.exe` **FDLG OK**; `tests/dlg.exe`
157/157; Notepad++ x64 end‑to‑end: F1 About → Esc → Ctrl+O → `change.log` typed + Enter → file loaded, editor intact; Notepad++ x86 Ctrl+O →
`change.log` loaded; Notepad++ x64 Ctrl+Alt+S → `hello` saved with the typed text; e2e in headless Chromium):

* **File dialogs (comdlg32)** — `GetOpenFileNameA/W`, `GetSaveFileNameA/W`: a real modal dialog (folder path, file list with directories,
  file name edit, file‑type combo from `lpstrFilter`, Open/Save + Cancel, Enter/Esc, double click, `..`), `OFN_FILEMUSTEXIST`, `lpstrInitialDir`,
  `lpstrTitle`, `lpstrDefExt`, `nFileOffset` / `nFileExtension` / `nFilterIndex` / `lpstrFileTitle` filled exactly as Windows does, both
  x64 and x86 `OPENFILENAME` layouts; Cancel returns 0 (`CommDlgExtendedError()` = 0).
* **COM file dialogs (shell32)** — `CoCreateInstance(CLSID_FileOpenDialog / CLSID_FileSaveDialog)` returns a real object implementing
  `IFileDialog` / `IFileOpenDialog` / `IFileSaveDialog` (all 27 + 5 methods), `IFileDialogCustomize` (check buttons keep state),
  `IShellItem` (`GetDisplayName` with the `SIGDN_*` family, `GetParent`, `GetAttributes`, `Compare`), `IShellItemArray`,
  `SHCreateItemFromParsingName`, `SHCreateItemFromIDList`, `SHGetIDListFromObject`; `Show` runs the same modal dialog, `GetResult` /
  `GetResults` / `GetFileTypeIndex` / `GetFolder` return what the user picked; Cancel = `HRESULT_FROM_WIN32(ERROR_CANCELLED)`.
* **Accelerators** — `LoadAcceleratorsA/W` (RT_ACCELERATOR resources), `DestroyAcceleratorTable`,
  `TranslateAcceleratorA/W` (FVIRTKEY / FCONTROL / FALT / FSHIFT, `WM_COMMAND` with the accelerator flag). Notepad++'s Ctrl+O, Ctrl+S,
  Ctrl+Alt+S, F1 … reach the program.
* **Windows z‑order model** (`user.inc`): a new **child** window is linked at the **bottom** of its siblings and a new top‑level window
  at the top — exactly what `CreateWindowEx` does (first‑created control = top of the z‑order = first in the tab order); `SetWindowPos` /
  `DeferWindowPos` honour `HWND_TOP`, `HWND_BOTTOM`, `HWND_TOPMOST/NOTOPMOST` and *insert‑after a sibling*; `GetWindow(GW_CHILD /
  HWNDFIRST / HWNDLAST / HWNDNEXT / HWNDPREV)`, `GetTopWindow`, `FindWindowEx(after)` and `GetNextDlgTabItem` follow the same list.
  Children are painted **top‑down** like Windows dispatches `WM_PAINT`; with `WS_CLIPSIBLINGS` the pixels of higher siblings are
  protected (the DC clip snapshot), without it the lower sibling overwrites — dialog statics behave as on Windows, and Notepad++'s
  Scintilla view (created before the tab bar) stays visible after every dialog.
* **Owner‑draw controls** — `BS_OWNERDRAW` buttons and `SS_OWNERDRAW` statics send `WM_DRAWITEM` with a real `DRAWITEMSTRUCT`
  (x64 and x86 layouts, `ODS_SELECTED/DISABLED/FOCUS/CHECKED`) to the parent instead of painting themselves.
* **Activation** — when the active top‑level window is hidden or destroyed, activation moves to its owner (or the top visible window of the
  process) with `WM_ACTIVATE` + `WM_SETFOCUS`, so the keyboard returns to the main window after a dialog (Notepad++ puts the caret back
  into the editor; accelerators work again).
* Also: `InternetCrackUrlA/W`, `AssocQueryStringA/W`; `fdlg.exe` / `fdlg32.exe` in the sample list.

**Honest limits.** File dialog: single selection only (`OFN_ALLOWMULTISELECT` returns one file), no `OFN_ENABLEHOOK` / template hooks,
no overwrite prompt, no `CreateAcceleratorTable`, no `IFileDialogEvents` callbacks and no `WH_KEYBOARD` / `WH_CALLWNDPROC` hooks yet — therefore Notepad++'s own
"append extension" trick does not run: Save As `hello` writes `hello`, not `hello.txt` (type the extension); the custom controls of
`IFileDialogCustomize` are accepted but not drawn; no "Recent" / "Desktop" places, the dialog browses the emulator's drives. The
built‑in 8×8 font is wider than Segoe UI, so some dialog texts are clipped (Notepad++ About). No pinch‑zoom; no gamepad; ASCII /
Latin‑1 / Cyrillic glyphs only; OLE drag‑and‑drop and clipboard formats other than text inert; no HTTPS/TLS, no WinForms/WPF; D3D11
partial, no D3D9/DDraw; runtime packs x64‑only; visual styles off.

# v1.25.0 — Nebula x64 (Android): touch and the soft keyboard drive real Windows programs; 7‑Zip File Manager x86 + x64 as on Windows

**Why.** Android is the only platform being shipped. On a phone the emulator screen had mouse‑only input: no drag, no right click,
no way to type into a Windows program at all (there was no text field for the soft keyboard), no mouse wheel. 7‑Zip's File Manager
(`7zFM.exe`) — the most common "real program" after Notepad++ — still relied on stubs (`CreateToolbarEx`, `CreateStatusWindow`,
`ComboBoxEx32`). Everything below was tested for **x86 and x64 together** before it was committed.

**Measured** (`emu/tests/run.sh` **211 native checks ALL OK**, x86 + x64; e2e in headless Chromium incl. a touch/IME scenario on an
Android‑like viewport; `tests/dlg.exe`/`dlg32.exe` **157/157**; `tests/typing.exe`/`typing32.exe` TYPING OK; `tests/input.exe` 30/30):

* **Touch on the guest screen** (`X64App`, pointer events, `touch-action: none`): tap = left click where the finger landed; drag = press
  and move (scrollbars, selections, window moves); hold still 550 ms = right click (haptic tick); a second finger while holding = right
  click; two‑finger vertical drag = mouse wheel; mouse/pen pointers keep their real buttons (middle button included); the wheel on
  desktops/Chromebooks now reaches the program (`WM_MOUSEWHEEL` to the focus window, pointer position untouched — it used to corrupt it).
* **Soft keyboard / IME** — the ⌨ button on the screen bar opens an invisible text field; whatever the Android keyboard inserts
  (Latin, Cyrillic, CJK, emoji, autocorrect replacements) is diffed and delivered to the program: characters the US layout can produce go
  through a real `WM_KEYDOWN` → `WM_CHAR` → `WM_KEYUP` sequence (accelerators and Scintilla see the key), everything else through
  `VK_PACKET` exactly like `SendInput(KEYEVENTF_UNICODE)` on Windows — order is preserved, Backspace/Enter/arrows work, hardware keyboards
  still work directly. New emulator entry point `x64_char`, native runner option `--type "text@ms"`, new test `typing.exe` (x86 + x64).
* **Sticky modifiers on the touch bar** — Ctrl / Alt / Shift stay down until the next key (Ctrl+S, Ctrl+Z, Alt+F, Shift+arrows on a phone),
  plus Esc, Tab, ⌫, ↵, arrows, Space.
* **7‑Zip File Manager x86 + x64**: `ComboBoxEx32` is a real wrapper over a real ComboBox + Edit (`CBEM_INSERT/SET/GETITEM`, image list,
  indent), `CBS_DROPDOWN` combos own a real Edit (`GetComboBoxInfo.hwndItem`), `CreateToolbarEx` / `CreateStatusWindowA/W` are real,
  toolbar labels under icons grow the button/bar, `WM_LBUTTONDBLCLK`/`WM_RBUTTONDBLCLK` are generated (500 ms, 4 px, `CS_DBLCLKS`),
  the rebar forwards `WM_COMMAND`/`WM_NOTIFY`, `GetWindowPlacement` uses parent‑client coordinates for children, `LsaOpenPolicy` & co,
  `FindFirstChangeNotification`. The suite navigates 7zFM folders by double click on both bitnesses.
* x64 Runner toolbar is one scrollable row on phones (buttons no longer wrap over each other).

**Honest limits.** No pinch‑zoom of the guest screen yet (the 1280×720 desktop is letter‑boxed to the phone, so small controls are small);
no gamepad; the built‑in 8×8 font covers ASCII, Latin‑1 and Cyrillic (typed «Привет» renders), CJK/emoji glyphs render as `?`; no HTTPS/TLS, no WinForms/WPF, D3D11 partial, no D3D9/DDraw;
runtime packs are x64‑only; file dialogs cancel; visual styles off.

# v1.24.0 — Nebula x64 (Android): Notepad++ x86 **and** x64 look and work as on Windows — real icons, class menus, ReBar, TB_ADDBITMAP, WM_PRINTCLIENT, text alignment; three emulator bugs that broke every MSVC program

**Why.** The acceptance target is «как на Windows, без заглушек»: a real application must look and behave the same as on Windows,
on both bitnesses. Notepad++ 8.8.9 was the yardstick — the x64 build started but had fake icons, no class menu, a black strip instead of
the tab bar and status bar, and `$STR_REPLACE2$` in the status bar; the x86 build died at `WM_CREATE` with an access violation.
Every change was tested for **x86 and x64 together** before it was committed. Android is the only shipped platform from this release on
(the CI builds the emulator, runs the native + browser suites and produces the signed APK; the desktop/bridge jobs are gone).

**Measured** (`emu/tests/run.sh` **203 native checks ALL OK**, x86 + x64 — Notepad++ x64 *and* x86 start to their main window and accept
typing (title becomes `*new 1`), 7‑Zip, PuTTY, Inno Setup/Compil32 unchanged; e2e in headless Chromium; `tests/dlg.exe`/`dlg32.exe`
**151/151**; `tests/input.exe` 30/30):

* **Icons and cursors are real** — `RT_GROUP_ICON`/`RT_ICON` are decoded (1/4/8/24/32‑bpp + mask, PNG‑in‑ICO), `LoadIcon`/`LoadImage`/
  `LoadIconWithScaleDown`/`LoadIconMetric`, `DrawIcon(Ex)`, `GetIconInfo`, `CreateIconIndirect`, `DestroyIcon`, `CopyImage`; class icons,
  caption icons, `WM_SETICON`, `STM_SETICON`, the `MessageBox` system icons, `ImageList_AddIcon/GetIcon/ReplaceIcon` all use them.
* **Class menus** — `WNDCLASS.lpszMenuName` is loaded for windows created without `hMenu` (`GetMenu` returned 0 before → Notepad++ had no menu bar).
* **Toolbar `TB_ADDBITMAP`** — the `HINST_COMMCTRL` standard sets (IDB_STD/VIEW/HIST) and application bitmap strips (Notepad++, 7‑Zip)
  feed the toolbar's own image list; `TB_GETBITMAP`, `TB_CHANGEBITMAP`, `TB_LOADIMAGES`; button size always fits the image
  (`TB_SETBUTTONSIZE` is a minimum, `TB_GETPADDING` defaults 7×6 as on Windows).
* **ReBarWindow32** — a real rebar control: bands with gripper/text/child, rows, `RB_INSERTBAND(A/W)`, `RB_SETBANDINFO`, `RB_GETBARHEIGHT`,
  `RB_GETRECT`, `RB_GETBANDINFO`, `RB_IDTOINDEX`, `RB_SHOWBAND`, `RB_MINIMIZEBAND/MAXIMIZEBAND`, `RBN_HEIGHTCHANGE`; the bar keeps its own
  height after `MoveWindow(…, 0)` exactly like comctl32 (Notepad++ relies on this), `CCS_NOPARENTALIGN` is honoured by rebar + status bar.
* **`WM_PRINT` / `WM_PRINTCLIENT`** for every built‑in control — double‑buffering subclasses (Notepad++ paints its status bar and tab bar
  into a memory DC and blits), `AnimateWindow`‑style code and print preview now get real pixels.
* **`SetTextAlign` / `GetTextAlign`** — `TA_BASELINE`, `TA_BOTTOM`, `TA_RIGHT`, `TA_CENTER`, `TA_UPDATECP` (saved/restored by `SaveDC`).
  Scintilla draws every line with `TA_BASELINE`; before this the typed text landed below the line box and was invisible.
* `ReplaceFileW/A` (Notepad++ autosave/backup faulted on it as unimplemented).
* **Emulator bugs found through Notepad++ and fixed for every program:**
  1. **`pcmpestri`/`pcmpistri` equal‑ordered** did not count a needle prefix that runs off the end of the register (SDM: i = 0…UpperBound−j).
     MSVC's `std::wstring::find` (`__std_search_2`) uses exactly that — only the *first* occurrence of a substring was ever found.
  2. **32‑bit stdcall cleanup lost after a C++ throw**: a continuation abandoned by a non‑local exit hid the one a shim pushed afterwards,
     so `SendMessageW` → guest window procedure returned without popping its 16 bytes → `esi/edi/ebx` of the caller were garbage → the
     x86 Notepad++ "Exception On WM_CREATE: Access violation".
  3. **Missing stdcall arities** for comctl32's ordinal‑only exports (`LoadIconWithScaleDown`, `LoadIconMetric`, `TaskDialog(Indirect)`),
     `ImmSetHotKey`, `DrawThemeParentBackgroundEx`, WIC proxies — a 32‑bit caller's stack drifted by 20 bytes per call.

**Honest limits.** Font glyphs outside ASCII/Latin‑1/Cyrillic are drawn as `?` (Notepad++'s «?» menu is one); NSIS header bitmap is still
missing; OLE drag‑and‑drop and the clipboard are inert; no HTTPS/TLS, no WinForms/WPF; D3D11 partial, no D3D9/DDraw; runtime packs are
x64‑only; US keyboard layout; file dialogs cancel; visual styles (uxtheme) are off.

# v1.23.0 — Nebula x64: game input (Raw Input + DirectInput 8), real menus, common controls (status bar / toolbar / tab control), image lists, WIC, RichEdit — x86 and x64 together

**Why.** A user's GOG (Inno Setup, Delphi) installer reported `Imports: 120 total · 119 implemented · 1 missing — user32.dll!GetKeyboardType`;
games and their installers also expect Raw Input and DirectInput 8, which the emulator did not have at all. Every change below was
built and tested for **x86 and x64 together** (native suite both bitnesses + browser e2e) before it was committed.

**Measured** (`emu/tests/run.sh` **197 native checks ALL OK**, x86 + x64; e2e 22/23 in headless Chromium — the 23rd scenario needs the
CI‑built .NET pack; `tests/dlg.exe`/`dlg32.exe` 88/88; `tests/input.exe`/`input32.exe` 30/30):

* **Raw Input (user32)** — `RegisterRawInputDevices`, `GetRegisteredRawInputDevices`, `GetRawInputData`, `GetRawInputBuffer`,
  `GetRawInputDeviceList`, `GetRawInputDeviceInfoA/W`, `DefRawInputProc`: real keyboard/mouse events arrive as `WM_INPUT` with
  `RAWKEYBOARD` (make/break, VKey, scan code) and `RAWMOUSE` (relative deltas, buttons, wheel); `RIDEV_NOLEGACY` suppresses the
  ordinary `WM_KEY*`/`WM_MOUSE*` messages as on Windows.
* **DirectInput 8 (dinput8.dll)** — `DirectInput8Create`, full `IDirectInput8` (11 methods) and `IDirectInputDevice8` (32 methods)
  COM objects for the system keyboard and mouse: `EnumDevices` through the guest callback, `CreateDevice(GUID_SysKeyboard/SysMouse)`,
  `SetDataFormat`/`SetCooperativeLevel`/`Acquire`/`Unacquire`/`Poll`, immediate `GetDeviceState` (DIK scan‑code table, `DIMOUSESTATE/2`
  relative axes), buffered `GetDeviceData` with `DIPROP_BUFFERSIZE`, `GetCapabilities`/`GetDeviceInfo`/`GetObjectInfo`, correct error
  codes (`DIERR_NOTACQUIRED`, `DIERR_DEVICENOTREG`, `S_FALSE` on re‑acquire). No game controllers yet (enumerated as none).
* `GetKeyboardType`, `OemKeyScan`, `GetMouseMovePointsEx`, `EnableMouseInPointer` — the GOG installer's import table is now complete.
* **Menus** (user32): menu bar in the non‑client area, popup menus as `#32768` windows with cascades, keyboard + mouse navigation,
  `WM_INITMENUPOPUP`/`WM_MENUSELECT`/`WM_COMMAND`, owner‑drawn items, `TrackPopupMenu(Ex)` modal loop, `WM_CONTEXTMENU`.
* **Common controls** (comctl32): real **StatusBar**, **Toolbar**, **TabControl**, subclass chains (`SetWindowSubclass`), real
  **image lists** (`ImageList_*`, VCL `TImageList` streams), built‑in **WIC** (PNG/BMP/ICO decoders, converter, scaler),
  **RichEdit** + multi‑line EDIT (word wrap, scrollbars, `EM_STREAMIN` RTF→text), native 1/4/8/16/24‑bpp DIB sections.
* Notepad++ 8.8.9 **x64** now starts to its main window (Scintilla) after a silent install inside the emulator; Inno Setup's
  **Compil32 IDE** opens with menus, toolbar, status bar and context menu; the Inno GUI wizard is driven end‑to‑end by mouse in the suite.

**Honest limits.** DirectInput covers keyboard + mouse only (no joysticks/gamepads, no XInput, no force feedback); TreeView/ListView
are still pending (PuTTY config, Compil32 file lists); Notepad++ toolbar icons / menu bar / status bar are not yet drawn; no HTTPS,
no WinForms/WPF, D3D11 partial, no D3D9/DDraw; runtime packs are x64‑only; US keyboard layout.

# v1.22.0 — Nebula x64: NSIS and Inno Setup installers run (x86 and x64), ISCC compiles installers inside the emulator, real ECDSA, a CPU bug that broke every Delphi binary

**Why.** After 7‑Zip (v1.21) the two installer engines almost every Windows program ships with are **NSIS** (Notepad++,
countless games/tools) and **Inno Setup** (Delphi/VCL — the biggest class of 32‑bit Windows software). Both are now proven
end‑to‑end on **both bitnesses together**: every change was tested with the x86 *and* x64 suites before it was committed.

**Measured** (test suite `[19]`, 185 native checks ALL OK for x86 + x64; e2e 22/23 in headless Chromium — the 23rd scenario
needs the CI‑built .NET pack; `docs/screenshots/x64-npp-setup.png`):

* **Notepad++ 8.8.9 (NSIS), x86 and x64 setups**: silent `/S` → exit 0, `notepad++.exe` (PE32 resp. PE32+) + plugins/themes/
  localization on disk; the x86 setup also installs **through the GUI wizard** (license → components → directory → install →
  finish, 560 presented frames).
* **Inno Setup 6.5.4 installing itself** (`innosetup-6.5.4.exe`, Delphi): `SetupLdr` → `Setup` child process → `Compil32 /ASSOC`
  third process, 117 files incl. `Compil32.exe`, `ISCC.exe`, `ISCmplr.dll`, languages, examples — exit 0, ~75 s at 63 MIPS.
  Inno 6.5 verifies **ECDSA P‑256 `.issig` signatures of its own DLLs** through BCrypt; that verification is now real
  (see below), so the compiler DLLs load.
* **The installed `ISCC.exe` compiles an Inno script inside the emulator** — ISPP preprocessor, `islzma.dll` with **LZMA2 worker
  threads** (ring buffers between threads, `lock xadd`), `iszlib.dll`; `lzma2/max` + solid → `Successful compile` in 90 s;
  the produced `nebula-setup.exe` (an Inno installer *built by Inno inside the emulator*) **installs** (`/VERYSILENT`) in 2–10 s
  and the payload (`MyProg.exe`) is **byte‑identical** after the compress/decompress round trip; `unins000.exe` is written.

**What was wrong and is fixed** (`emu/x64/cpu.c`, `win32_bcrypt.inc`, `ecdsa.inc`, `user.inc`, `win32_fs.inc`, `win32_shell.inc`,
`win32_inno.inc`, `win32_vcl.inc`, `seh.inc`, `seh32.inc`, `x87.inc`, `pe.c`, `api.c`, `gdi.c`):

| area | change |
|---|---|
| **CPU bug (affects every Delphi x86 and x64 binary)** | `LEA` honoured segment overrides: Delphi pads code with `64 66 8D 80 00 00 00 00` (`lea ax, fs:[eax]`) multi‑byte NOPs, and the emulator added the FS base (TEB) to AX — the SSE `Move()` source pointer lost 16 bits → access violations in the LZMA ring buffer. LEA now ignores `fs:`/`gs:`. |
| CPU | `CMPXCHG8B`/`CMPXCHG16B`, `RDRAND`/`RDSEED` (pseudo‑random), x87 `FBLD`/`FBSTP` (Delphi RTL) |
| **BCrypt: real ECDSA P‑256** | `BCryptImportKeyPair` (ECCPUBLICBLOB), `BCryptExportKey`, `BCryptVerifySignature` — Montgomery arithmetic over p and n, Jacobian point add/double, Fermat inversion, point‑on‑curve check, truncation of long hashes; verified against RFC 6979 A.2.5 vectors (SHA‑256 and SHA‑1), tampered hash/signature rejected. Nothing is faked: a forged `.issig` is still refused. |
| USER32 | **per‑process window classes** (+ windows/classes are released when a child process exits) — `Compil32` launched from Setup found Setup's `TApplication` class and jumped into the wrong process's import table; cross‑process `PostMessage`/`SendMessage` (degrades to post), per‑process builtin wndproc slots, `GetClassInfo*`, `CallWindowProc`, `UnregisterClass`, `MsgWaitForMultipleObjects` polls real handles, `SetFocus`/`GetFocus`, palettes (`CreatePalette`/`SelectPalette`/`RealizePalette`), VCL/uxtheme/dwmapi/DPI shims (visual styles off, 96 DPI), ~100 previously unknown imports used by VCL |
| KERNEL32 | `SearchPathA/W` (ISPP locates `ISPPBuiltins.iss`), INI API (`Get/WritePrivateProfile*`), `wsprintfW`, `GetLogicalProcessorInformation` 32‑bit layout, `VirtualQuery` in mode32, `UnhandledExceptionFilter` semantics (no infinite resume), `GetTokenInformation`/`CheckTokenMembership`/`ConvertStringSecurityDescriptorToSecurityDescriptor` (one admin token, SDDL accepted), Restart Manager `Rm*` (empty), ordinal imports resolved through Wine spec tables |
| SHELL/OLE | `IShellLink` gets an `IPropertyStore` (AppUserModelID accepted, no‑op), `ShellExecute` sniffs `MZ`, `VariantCopyInd`/`Ole*`, `FOLDERID_UserProgramFiles(+Common)` (`{autopf}` with `PrivilegesRequired=lowest`), `SHGetKnownFolderPath` Downloads |
| diagnostics | `RaiseException`/NULL‑call reports show the x86 frame chain and the last executed blocks; `X64_WATCH=rip,…` and `X64_DWATCH=addr` log registers / every change of a dword — these found the LEA bug |

**Honest limits.** The Inno Setup **GUI** wizard (non‑silent) opens its window but VCL does not paint yet (silent installs are
complete; GUI installs work for NSIS and 7‑Zip); `Compil32.exe` (the IDE) stops at `ImageList_Read` (streamed VCL image lists
are not implemented); `LZMAUseSeparateProcess=yes` needs `CreateFileMapping` across processes (not implemented — the default
in‑process threads work); menus are not drawn; `SysTreeView32`/`SysListView32`/`SysTabControl32`/RichEdit are placeholders;
no HTTPS/TLS, no browser networking, no WinForms/WPF, D3D11 partial, no D3D9/DirectDraw/DirectInput; runtime packs x64‑only;
US keyboard, 8×8 font; file dialogs cancel; visual styles off; BCrypt = SHA‑1/256/MD5/HMAC/RNG/ECDSA‑verify only (no RSA, no
signing); RDRAND is pseudo‑random; cross‑process `SendMessage` does not wait for the reply.

# v1.21.0 — Nebula x64: the USER32 dialog manager (x64 **and** x86), standard controls, resources, real installers

**Why.** Almost every Windows installer and most small GUI programs are built from *dialog templates* (`DialogBoxParam`,
`CreateDialogParam`, `#32770`) with the standard controls (static/edit/button/checkbox/radio/listbox/combobox) and
messages/strings/icons from the resource section. v1.20 ran 32-bit code but stopped at `CreateDialogParamW`; v1.21 adds
the dialog manager to both bitnesses at once, so nothing that worked for x64 regresses for x86 and vice versa.

**What is new** (`emu/x64/user.inc`, `win32_shell.inc`, `font8x8_ext.h`):

| | |
|---|---|
| dialog templates (`DLGTEMPLATE` and `DLGTEMPLATEEX`), `DialogBoxParamA/W`, `DialogBoxIndirectParam`, `CreateDialogParamA/W`, `CreateDialogIndirectParam`, `EndDialog`, `IsDialogMessage`, `GetDlgItem*`, `SetDlgItemText/Int`, `GetDlgItemText/Int`, `CheckDlgButton`, `CheckRadioButton`, `IsDlgButtonChecked`, `MapDialogRect` (6×13 base units like Windows), `GetNextDlgTabItem`, `DefDlgProc`, `DS_CENTER`, `WM_INITDIALOG` focus rules | works, x64 + x86 |
| **nested modal dialogs** and `MessageBoxA/W` (modal loop is a continuation, so a dialog proc that opens another dialog or a message box from inside `WM_COMMAND` works) | works |
| standard controls as built-in window classes: `Button` (push/default/check/3-state/radio/group box/auto), `Static` (text/icon/frame), `Edit` (single line, multi-line read-only, password, caret, selection, `EM_*`), `ListBox` (`LB_*`), `ComboBox` (drop-down list with its popup), `ScrollBar`, `msctls_progress32`, `SysLink` | works |
| `WM_COMMAND`/`EN_*`/`BN_*`/`LBN_*`/`CBN_*` notifications, `WM_CTLCOLOR*`, `WM_SETFONT`/`WM_GETFONT`, accelerators `&`, Tab/Shift+Tab/Enter/Escape navigation, `IDOK`/`IDCANCEL` defaults, `WM_DESTROY`/`WM_NCDESTROY` delivery, `PostQuitMessage` from a dialog | works |
| resources: `LoadStringA/W`, `LoadIconA/W`, `LoadImage`, `FindResource`/`LoadResource`/`SizeofResource`, `EnumResourceNames`, string tables with **Cyrillic and Latin‑1** text | works |
| shell: `SHGetFolderPathA/W`, `SHGetSpecialFolderPath`, `SHGetKnownFolderPath`, `SHBrowseForFolder` (returns the typed path), `IShellLink` + `IPersistFile` (`.lnk` shortcuts on the virtual disk), `ShellExecute` (`open` of files/folders is recorded, URLs are reported) | works |
| **text**: the bitmap font now has **212 extra glyphs** — Latin‑1, Cyrillic (U+0400–045F, Ґґ), dashes/quotes/ellipsis/€/№/arrows/✓ — all user32/gdi32 text paths (`TextOut`, `DrawText`, controls, captions) decode UTF‑8/UTF‑16; dialog fonts have a 13 px cell with the glyph in an 11 px em like an 8 pt Tahoma, so multi-line text no longer overlaps | works |
| `IsWow64Process`/`IsWow64Process2` report WOW64 for 32-bit processes — a 32-bit setup stub that checks for a 64-bit OS (7‑Zip's x64 setup) now takes its 64-bit path | works |
| top-level windows that centre themselves on the 1280×720 "desktop" are kept inside the presented framebuffer (PuTTY's configuration dialog used to appear off-screen) | works |

**Measured** (test suite section `[19]`, e2e scenarios `dlg.exe`, `dlg32.exe`; `docs/screenshots/x64-7zip-setup.png`,
`docs/screenshots/x64-putty-dialogs.png`):

* `emu/tests/dlg.c` — a self-driving dialog test built **twice**, as x64 `dlg.exe` and x86 `dlg32.exe`: **42/42 checks pass
  in both bitnesses**, natively and in the browser (wasm64, headless Chromium): resources, `LoadString` with Cyrillic,
  every standard control and its notifications, `MapDialogRect`, nested modal dialog + `MessageBox`, `SHGetFolderPath`,
  `IShellLink`/`IPersistFile`, a modeless dialog driven by Tab/Escape, `WM_DESTROY` → `PostQuitMessage`.
* **The official 7‑Zip 25.01 installers, both of them** (`7z2501.exe` x86 and `7z2501-x64.exe` x64): silent `/S` install
  → exit 0 and `7z.exe`, `7z.dll`, `Lang\` on disk; the **installed `7z.exe`** (a 32-bit PE32 resp. a 64-bit PE32+ that loads
  `7z.dll` through `GetProcAddress`) compresses and lists an archive; the **GUI install** — the setup dialog from the
  resource template, *Install* clicked, progress, *Close* clicked — exits 0 and writes the same files (~16 s each).
* **PuTTY 0.85 x64** (MSVC/clang‑cl, `putty.org`) now shows its configuration dialog and the About box opens and renders
  (v1.20: stopped at `CreateDialogParamA`). The category tree is a `SysTreeView32`, which is reported as not implemented.

**Honest limits.** Common controls beyond progress bar/SysLink (`SysTreeView32`, `SysListView32`, `SysTabControl32`,
`RichEdit`, toolbars, status bars, up-down) are drawn as empty boxes and reported; menus are not drawn; no owner-draw
buttons; the keyboard is US layout; edit selection offsets are UTF‑8 bytes (differ from UTF‑16 units for non-ASCII text in
`EM_GETSEL`); the font is a scaled 8×8 bitmap font (ASCII + Latin‑1 + Cyrillic, other scripts show `?`); COM classes
remain ShellLink/IMalloc/audio only, and the audio COM vtables are x64-only. Runtime packs (VC++/.NET/DirectX) are
still x64-only.

# v1.20.0 — Nebula x64: 32-bit x86 (PE32) processes

**Why.** Most Windows installers and a large share of games/programs are still 32-bit; until now the emulator rejected every PE32 up front ("unsupported").

**What is new** (details: `docs/X86-32BIT.md`): an IA-32 compatibility mode in the CPU core; a PE32 loader with a 32-bit address map; a generated stdcall/cdecl marshaller for all 1256 shimmed APIs (argument sizes from Wine's `.spec` files); 32-bit TEB/PEB and 32-bit layouts of every Win32 structure the shims touch; the x86 structured-exception protocol (`fs:[0]` chain, `RaiseException`, `RtlUnwind`, `_except_handler3/4`, `_local_unwind2/4`, `_global_unwind2`, `longjmp`) and MSVC x86 C++ exceptions (`__CxxFrameHandler`); the inspector reports PE32 verdicts like PE32+.

**Measured** (test suite section `[18]`, e2e scenarios `hello32.exe`, `seh32.exe`, `NebulaStrike32.exe`): the official 32-bit `7za.exe` 25.01 compresses/extracts bit-exact with worker threads and its MSVC C++ error path; zig-built 32-bit console, SEH (9/9), C++ EH, winpthreads, Winsock and GDI-game programs pass natively and in Chromium (wasm64).

**Honest limits.** No x86 runtime packs yet (VC++/.NET/DirectX packs are x64 only); no dialog manager/common controls, so GUI installers (e.g. `7z2501.exe`) load and stop at `CreateDialogParamW` — next release; no 16-bit code; `IsWow64Process` = not WOW64.

# v1.11.0 — Nebula x64: a real `cmd.exe` (shell for `system()`, `_popen`, `subprocess`, batch files) + alertable I/O

**Why.** Many programs do not spawn children directly — they call `system()`, `_popen()`, `os.system`, `subprocess(shell=True)`,
`rg --pre`, `fd -x`. All of those run `cmd.exe /c …`, which the emulator did not have; so far they failed with “shell not
available”. Taking Microsoft's `cmd.exe` is impossible (not redistributable, and it needs half of the NT console subsystem), so
this release ships **our own `cmd.exe`**: `emu/cmd/cmd.c`, a freestanding Windows x86‑64 program (53 KB, imports only
`kernel32`, built with `zig cc -target x86_64-windows-gnu -nostdlib`). It is a *real guest executable* — it runs inside the
emulator like any other PE32+, and the same bytes are embedded in the emulator (`emu/x64/cmd_exe.h`) and mounted at
`C:\Windows\System32\cmd.exe`, so `%COMSPEC%`, `GetSystemDirectory()\cmd.exe` and the CRT's `system()` find it.

**What works** (test suite section `[16]`, 14 checks; e2e scenario `x64: cmd.exe`):

| | |
|---|---|
| `cmd /c "…"`, `cmd /k`, batch files, the interactive prompt (console `ReadFile`, `^Z` ends it) | works |
| builtins: `echo set set /a set /p if (EQU/NEQ/LSS/… , exist, defined, errorlevel, /i) for (plain, /l, /d, /r, /f tokens= delims= skip= eol=) call :label goto exit [/b] n dir [/b /s /a-d] type copy move del ren md rd cd pushd popd find [/c /i /v] findstr [/i /v /c:] sort [/r] more cls ver date time pause rem title color prompt path vol where shift setlocal/endlocal` | works |
| operators `& && \|\| \|`, groups `(…)`, redirections `> >> < 2>&1 2>nul`, multi‑line `if … ( … ) else ( … )` in batch files | works |
| `%VAR%`, `%1…%9 %*`, `%~n1 %~x1 %~dp1 %~f1`, `%VAR:~2,3%`, `%ERRORLEVEL% %CD% %RANDOM% %DATE% %TIME%` | works |
| external programs: `.exe` lookup in cwd, `%PATH%`, `C:\Windows\System32`; `%ERRORLEVEL%` from the child; a pipeline of externals and builtins (`type f \| find /c x`, `dir /b \| sort /r`, `echo … \| jq`) | works |
| **CRT `system()` / `_popen("r")` / `_popen("w")` / `_pclose`** (UCRT and mingw CRTs — both call `cmd.exe /c` via `CreateProcess`) | works |
| **Python** `os.system`, `os.popen`, `subprocess.run(…, shell=True)` chains with `&&` and `\|` | works |
| **ripgrep `--pre C:\nebula\pre.cmd`** (a batch preprocessor per file), **fd `-x cmd /c echo found {}`** / `-X jq.exe` | works |
| `demo.bat` in the x64 Runner: `for /f` + `set /a` counting, `dir \| sort`, `find /c`, `jq.exe` and `lua54.exe` as children, `call :sub`, `exit /b 7` | works |

**Honest limits of this `cmd.exe`:** no delayed expansion (`setlocal EnableDelayedExpansion` / `!VAR!`), no `usebackq` in `for /f`, no `attrib`, `mklink`, `choice`, `timeout`, `where`, `xcopy`, `robocopy`, `tasklist`,
`start` (start a program directly instead), `%%` in `/c` mode stays literal (as in the real interactive cmd), `echo` trims trailing
spaces, no command history/editing in the interactive prompt (the console line input does that), no Unicode (UTF‑16) batch files,
no `cmd /u`. Error messages copy the real ones (`'x' is not recognized as an internal or external command…`, exit code 9009).

**Emulator changes that this needed** (`win32.c`, `win32_fs.inc`, `emu.h`):

* `system`/`_wsystem`/`_popen`/`_wpopen`/`_pclose` shims that spawn `C:\Windows\System32\cmd.exe /c …` and wait; the child's
  pipe end is marked non‑inheritable before the spawn (a `_popen("…", "w")` deadlock: the child inherited the parent's write end,
  so writers never reached zero and the child never saw EOF);
* `ReadFileEx` / `WriteFileEx` with **completion routines**, a per‑thread **APC queue** (`QueueUserAPC`, delivered by alertable
  `SleepEx`, which returns `WAIT_IO_COMPLETION`), `CreateNamedPipeW/A` + `ConnectNamedPipe` + `\\.\pipe\` names in `CreateFile`,
  `RtlNtStatusToDosError` (`0xC000014B` → `ERROR_BROKEN_PIPE`), `CancelIo(Ex)` — this is how Rust's `std::process` reads a
  child's stdout/stderr (`--pre` in ripgrep, `-x` in fd), see `library/std/src/sys/pal/windows/pipe.rs`;
* `fgets` on a pipe now blocks (used to return EOF when the child had not written yet);
* `resolve_exe` understands `cmd`, `cmd.exe`, `%COMSPEC%`, bare names via `%PATH%` and `.exe`/`.bat`/`.cmd` extensions
  (`.bat`/`.cmd` → `cmd.exe /c file`);
* `GetStdHandle` values `0x10/0x11/0x12` are resolved through the current process's inherited descriptors (so a child's child
  inherits redirected pipes correctly).

**Desktop:** the Command Prompt window gained a `real` command (and a *real cmd.exe* button) that opens the x64 Runner with
`cmd.exe` already running; the x64 Runner has `cmd.exe` in its sample list (`/k demo.bat`). CI now builds `cmd.exe` from source
(`BUILD_CMD=1 ./emu/x64/build.sh`) and checks it matches the committed binary. Test suite: 96 native checks; e2e 32/32.

# v1.10.0 — Nebula x64: CPython 3.12.10 runs in the browser

**A real Python.** The official python.org *embeddable* build of CPython 3.12.10 (`python.exe` + `python312.dll` 4.3 MB + the
standard library in `python312.zip` + 17 C‑extension `.pyd` files, MSVC v.1943, unmodified except `import site` enabled in
`python312._pth`) now runs inside the wasm64 emulator. It appears in the x64 Runner as `python.exe` (15 MB bundle, downloaded
lazily the first time you press *Run*; the console shows the download progress).

Verified native and in Chromium (test suite section `[15]`, e2e scenario `x64: CPython 3.12`):

| feature | status |
|---|---|
| `-c`, scripts, `-i -q` **interactive REPL over the console** (`ReadConsoleW` 16 K wchar path of `myreadline.c`, `^Z` = EOF, Cyrillic I/O) | works |
| stdlib via **zipimport**: `json re collections dataclasses decimal datetime zoneinfo statistics itertools functools typing traceback` | works |
| C extensions: `_sqlite3` (+ `sqlite3.dll` 3.49.1, file and `:memory:` databases), `_decimal` (libmpdec), `_ctypes` (+ `libffi-8.dll`, `ctypes.windll.kernel32.GetCurrentProcessId()` → 4242), `_bz2 _lzma pyexpat _elementtree unicodedata _queue select _socket _asyncio _zoneinfo _uuid _multiprocessing` | load and work |
| `threading` / `queue` / `concurrent.futures` (CPython's GIL on top of SRW locks + condition variables) | works |
| **`subprocess`** — `python.exe` spawning `python.exe` with `capture_output`, `input=`, `check_output`, `returncode`, `stderr` | works |
| `hashlib` (pure‑Python fallbacks — no `_hashlib`/OpenSSL), `math` (`erf/erfc/gamma/lgamma/log/pow` within 1 ulp of the host), `os.environ`, `platform.uname()` → `Windows NEBULA 10 10.0.19045 AMD64` | works |
| file I/O, `os.listdir/stat/rename/remove`, `tempfile`, `shutil`, tracebacks with source lines, `sys.exit(n)` → exit code | works |
| `ssl`, real sockets, `tkinter`, `_wmi`, `winsound`, `_msi` | **not available** (no network/COM in the sandbox; `import ssl` fails cleanly) |

Performance, honestly: the interpreter start‑up is ≈ 60 M guest instructions — about 2 s native, ~4 s in Chromium at ~18 MIPS;
the bundled `demo.py` (json / sqlite / decimal / dataclasses / threads / traceback) is 268 M instructions ≈ 9 s native, ~15 s in the
browser. A pure‑Python integer loop costs ≈ 2 350 emulated instructions per iteration, i.e. Python inside the emulator is roughly
500–1000× slower than Python on the host CPU. Fine for scripts and the REPL; not for number crunching.

**What CPython needed from the Win32/CRT layer** (`win32.c`, `win32_fs.inc`, `win32_compat.inc`):

* UCRT: `setlocale/_wsetlocale`, `_dclass/_fdclass` with the real UCRT constants (CPython's `Py_IS_FINITE` is `_dclass(x) <= 0`),
  `__sys_nerr/__sys_errlist`, `_wputenv_s`, `wcsncpy_s/wcscpy_s/wcscat_s` and the rest of the `wcs*` family, a correct
  `_localtime64_s`, a real wide `_wenviron` array kept in sync with `_environ`, `erf/erfc` rewritten (Taylor series + continued
  fraction, ≤ 1e‑13 vs the host — the previous approximation was 1e‑7 off and `math.erf` tests showed it);
* kernel32: `GetLongPathNameW/A`, `GetShortPathNameW`, `GetVolumePathNameW`, `GetFileInformationByHandleEx`,
  `GetNumberOfConsoleInputEvents`, `GetActiveProcessorCount/GetMaximumProcessorCount/GetActiveProcessorGroupCount`,
  `Initialize/Update/DeleteProcThreadAttributeList` (STARTUPINFOEX handle lists), `DisableThreadLibraryCalls`,
  `PROCESSOR_*` environment variables; `api-ms-win-core-path!PathCch*`; the `ws2_32` ordinal‑only imports resolved by name;
* a rewritten `ReadConsoleW` (UTF‑8 console input → UTF‑16, partial reads, `^Z`), and a **`DuplicateHandle` bug fix**: the shim read
  `dwDesiredAccess` instead of `bInheritHandle`, so a duplicated pipe end was never inheritable and `subprocess` pipes silently fell
  back to the console. Found with the new native `--apitrace` flag (`X64_LOG=1 x64run --apitrace … 3>&1`), which prints every
  Win32 call with its first four arguments and return value.

Tests: `emu/tests/run.sh` grows to **82 checks** (12 new: version/platform, zipimport, sqlite3, decimal/hashlib/math, threads,
ctypes, file I/O + traceback, exit code, subprocess pipes, REPL over piped stdin, `demo.py`); the Playwright suite gains the CPython
scenario (one‑liner + REPL with `import this`) — 31 scenarios, all green.

Licence: CPython is distributed under the PSF License 2.0 (`public/x64/apps/py/LICENSE.txt`); see `public/x64/samples/README.md`.

---

# v1.9.0 — Nebula x64: hardware exceptions as SEH, Rust programs (ripgrep, fd)

**Hardware faults are now Windows exceptions.** Until v1.8 an access violation, an integer division by zero or a `ud2` inside the
guest stopped the emulator with a report. Now (`cpu.c`, `seh.inc`):

* the interpreter records the fault (`c->exc_code/exc_addr/exc_info`), rewinds `rip` to the faulting instruction and
  `w32_hw_exception` raises it through the same two‑phase dispatcher as `RaiseException`: `EXCEPTION_ACCESS_VIOLATION` (0xC0000005,
  `ExceptionInformation = {read/write, address}`), `EXCEPTION_INT_DIVIDE_BY_ZERO` (0xC0000094), `EXCEPTION_INT_OVERFLOW` (0xC0000095),
  `EXCEPTION_ILLEGAL_INSTRUCTION` (0xC000001D); `ExceptionAddress == ContextRecord->Rip`; the exceptions are continuable;
* **vectored exception handlers** (`AddVectoredExceptionHandler` first/last, `RemoveVectoredExceptionHandler`) run before the frame
  handlers; `EXCEPTION_CONTINUE_EXECUTION` resumes with the (possibly patched) `CONTEXT` — a handler can skip the faulting instruction
  and set registers;
* `SetUnhandledExceptionFilter`: the top‑level filter is called with `EXCEPTION_POINTERS` when no frame handles the exception
  (mingw's CRT installs one at start‑up; a program's own filter can log and `ExitProcess`); otherwise the run stops with the
  familiar report, now including the original fault text (`… (write fault at 0x10)`);
* nested faults inside handlers are bounded (16 levels) so a crashing handler still produces a report.

**Rust programs run.** Two popular Rust tools, official `x86_64-pc-windows-msvc` builds with the static CRT, join the bundled apps
(`public/x64/apps/`, licences in `public/x64/samples/README.md`):

| app | verified |
|---|---|
| **ripgrep 14.1.1 `rg.exe`** (5.4 MB) | `--version`, single‑file and directory‑tree searches, `-n`, `-c`, `-w`, `-i`, alternations, `--stats`, `-j4` worker threads; counts identical to GNU grep; bad regex → proper error, exit 2 |
| **fd 10.2.0 `fd.exe`** | `-e ext`, `-t f`, `-S +1m`, coloured output, `--version` |

What Rust's std needed from the Win32 layer: `bcryptprimitives!ProcessPrng` (and `BCryptGenRandom`) for HashMap seeds,
`GetFinalPathNameByHandleA/W` (symlink‑loop detection in the directory walker), `ntdll!NtReadFile`/`NtWriteFile` with
`IO_STATUS_BLOCK` and `STATUS_END_OF_FILE`/`STATUS_PIPE_BROKEN` (Rust's `File::read`/`write` go through them), **waitable timers**
(`CreateWaitableTimerExW` high‑resolution, `SetWaitableTimer` relative/absolute, `CancelWaitableTimer`, waitable in `WaitFor*Objects`
and honoured by the scheduler's sleep calculation — `thread::sleep` on Rust ≥ 1.75), `CompareStringOrdinal`. Rust's panic machinery
(`_CxxThrowException` + a statically linked `__CxxFrameHandler3`) sits on the ntdll unwinder from v1.8 (`RtlUnwindEx`,
`RtlLookupFunctionEntry`, `RtlVirtualUnwind`).

Tests: `emu/tests/hwexc.exe` (section [13], 6 checks) and section [14] (4 checks) — 70 native checks total; new e2e scenario
(hwexc + rg + fd in Chromium wasm64, screenshot `docs/x64-rust.png`) — 31 scenarios. All previous checks unchanged.

Limits (honest): stack‑overflow guard pages are not emulated (a runaway recursion ends with an unmapped‑stack report, not
`EXCEPTION_STACK_OVERFLOW`); `_set_se_translator` is a stub; `fd -x`/`rg --pre` need a child interpreter (`cmd.exe`) that does not exist
on the virtual disk; `__CxxFrameHandler4` still skipped.

# v1.8.0 — Nebula x64: structured exception handling and C++ exceptions

Before this release any Windows program that **threw a C++ exception** (or used `__try/__except`) stopped the emulator —
7‑Zip's error paths (`7z x missing.7z`), every `std::vector::at` out of range, every `throw` in an MSVC program were a dead end.
The emulator now implements the x64 exception machinery of ntdll **and** the two C++ runtimes that sit on top of it.

What was built (`emu/x64/seh.inc`, `emu/x64/cxx.inc`, `pe.c` keeps `.pdata` of every module):

* **Table‑based unwinding**: `RUNTIME_FUNCTION`/`UNWIND_INFO` interpretation (all `UWOP_*` codes, chained info, frame registers,
  epilogue detection), `RtlLookupFunctionEntry`, `RtlVirtualUnwind`, `RtlCaptureContext`, `RtlPcToFileHeader`.
* **Two‑phase dispatch**: `RaiseException` → walk the frames calling every exception handler (phase 1) → `RtlUnwindEx` (phase 2,
  `EXCEPTION_UNWINDING`/`TARGET_UNWIND`/`EXIT_UNWIND` flags) → `RtlRestoreContext`, including `STATUS_UNWIND_CONSOLIDATE`
  callbacks (run on the deep dispatcher stack, like Windows, so the exception object in the thrower's frame survives the catch) and
  **collided unwinds** (libunwind's `_Unwind_Resume`/`STATUS_GCC_UNWIND` re‑entering `RtlUnwindEx` from a handler).
  Handlers, filters and funclets are guest calls driven by the emulator's continuation stack; walks that reach a continuation
  boundary continue in the logical parent frame (the same effect as `KiUserExceptionDispatcher`'s machine frame).
* **`__C_specific_handler`**: scope tables, filters with `EXCEPTION_POINTERS`, `EXCEPTION_CONTINUE_EXECUTION`, `__finally` during
  unwinding, targeted unwinds (`__try/__except/__finally` from MSVC, clang `-fms-extensions`, mingw).
* **MSVC `__CxxFrameHandler` / `__CxxFrameHandler3`** (msvcrt.dll and vcruntime140.dll ABI), modelled on Wine's `cxx_frame_handler`:
  `FuncInfo` (magic 0x19930520..22) with unwind map, try blocks, IP‑to‑state map, `unwindHelp` slot; cleanup funclets in state order;
  catch matching through `ThrowInfo`/`CatchableTypeArray` (type names, const/volatile, `catch(...)`), copying the exception object
  by value (memcpy or copy constructor, virtual bases), by reference or as a pointer with `this` adjustment; the catch funclet run as a
  consolidate callback, its return value is the continuation address; destructor of the exception object (shared objects of nested
  catches are kept alive); **`throw;` rethrow** (re‑uses the record of the innermost active catch); exceptions thrown **inside a catch
  block** (nested, `unwindHelp[1]` bookkeeping, funclet frame → parent frame via the handler's `dispFrame`).
* **gcc/libunwind path**: `_CxxThrowException`, `STATUS_GCC_THROW` records, `_GCC_specific_handler`'s phase‑1/phase‑2 protocol.
* `FormatMessageA/W` for the common Win32/HRESULT codes (7‑Zip prints `The system cannot find the file specified.` instead of `Error #80070002`).

Verified (`emu/tests/run.sh` section [12] — 6 new checks, 60 total; e2e scenario in Chromium wasm64, screenshot `docs/x64-exceptions.png`):

| test | result |
|---|---|
| `cxxexc.exe` (zig c++ ‑O1, libunwind): catch `std::exception`/`what()`, custom struct, nested rethrow as `int`, `vector::at`, `stoi`, destructors during unwinding in order, **1000 throws in a loop** | all correct, exit 0, 2.96 M instr |
| `sehtest.exe` (`-fms-extensions`): filter called, inner `__finally` during unwind, `__except` body, normal `__finally`, nested `__except` | all correct |
| **7‑Zip 25.01** `x nonexistent.7z` / `a -tbogus` / corrupt archive / wrong password (AES header) — MSVC throws inside `7z.dll`, caught in `7z.exe` | `System ERROR: The system cannot find the file specified.` exit 2 · `Unsupported archive type` · `Cannot open the file as archive` · `Wrong password?` — the same output as on Windows |
| the 54 earlier checks and 28 earlier e2e scenarios | unchanged, green |

Limits (honest): `__CxxFrameHandler4` (the compressed tables of VS2019+/`vcruntime140_1.dll`) is recognised but skipped — such
frames are treated as "no catch here"; hardware faults (access violation, divide by zero) still stop the emulator with a report
instead of becoming SEH exceptions; `std::current_exception`/`exception_ptr`, `_set_se_translator` and `UnhandledExceptionFilter`
callbacks are stubs. Nothing changed for the GPU/JIT verdict: Unity/D3D11 games remain out of reach for an interpreter.

# v1.7.0 — Nebula x64: real third‑party applications, DLL loader, x87

**Five unmodified, officially distributed Windows programs now run inside Nebula OS** (wasm64 + WebGL, also on Android) and ship as
one‑click samples in the x64 Runner (`public/x64/apps/`, licences in `public/x64/samples/README.md`):

| app | what was verified |
|---|---|
| SQLite 3.50.4 `sqlite3.exe` (MSVC) | `:memory:` queries; **file databases**: `create table`/2000 inserts/`create index`/`group by`/`pragma integrity_check` → `ok`; the db file persists on the virtual disk between runs |
| jq 1.8.1 `jq.exe` | filters, `select`, object construction, `-s`, `sqrt`, coloured and `-M` output |
| Lua 5.4.2 `lua54.exe` + **`lua54.dll`** | script execution, closures, `string.format`, `io.open`/`io.lines`, `os.date` |
| 7‑Zip 25.01 `7z.exe` + **`7z.dll`** (and `7za.exe`) | `a`/`x`/`l`/`i`/`b`: 7z (LZMA2) and zip round trips are bit‑exact; `7z i` lists 100+ formats/codecs from the DLL |
| Zstandard 1.5.7 `zstd.exe` (MSVC) | compress/decompress round trips, `-b1 -e3` benchmark (≈1 MB/s compress, 2.4 MB/s decompress in the emulator) |

What had to be built for that:

* **Private DLL loader** (`pe.c: pe_map/pe_load_dll`, `win32.c: mod_*`): DLLs imported statically or via `LoadLibrary(Ex)A/W` are found on the
  virtual disk (as given → next to the exe → `C:\nebula`), mapped at their preferred base when free (else relocated above `0x190000000`),
  their imports resolved recursively (system DLLs → shims, other private DLLs → exports; forwarders `KERNEL32.Foo` followed),
  `DllMain(base, DLL_PROCESS_ATTACH, 0)` run as a guest continuation before TLS callbacks/`main` (and synchronously inside `LoadLibrary`),
  exports by name or ordinal for `GetProcAddress`, `GetModuleHandle(Ex)A/W` (incl. `FROM_ADDRESS`), `GetModuleFileName` for module handles.
  Circular dependencies terminate (module registered before its imports are walked).
* **Static TLS per image**: every exe/DLL with a `.tls` directory gets its own slot in the per‑thread TLS pointer array (`TEB+0x58`),
  `_tls_index` is written into the image, blocks are created for all live threads at load time and for every thread created later
  (`emu/tests/tlsuse.c`: `_Thread_local` in a DLL used by 3 threads).
* **x87 FPU subset** (`emu/x64/x87.inc`): the register stack, `fld/fst/fild/fist(t)p/fbld`, arithmetic incl. reverse/pop forms,
  `fcom/fucom/fcomi`, `fxch`, `fchs/fabs/fsqrt/fscale/frndint/fprem/fsin/fcos/fptan/fpatan/fyl2x/f2xm1`, `fldcw/fnstcw/fnstsw`, `fnstenv/fldenv`,
  `fxsave/fxrstor` registers. Needed by `long double` printf (mingw), `strtold`, and the MSVC/UCRT math paths in SQLite/zstd.
  **Precision is 53‑bit** (host doubles), not 64‑bit — documented limit.
* **CPU**: SSE3 `movddup`/`movsldup`/`movshdup` (F2 0F 12 was executed as `movlps` — found by zstd), `popcnt`, `bsf/bsr`, `cmpxchg16b` paths exercised by MSVC CRT.
* **Win32 fixes found by the apps**: `ReadFile`/`WriteFile` honour `OVERLAPPED.Offset/OffsetHigh` (x64 layout, +16/+20 — SQLite's `pread`‑style
  VFS depended on it), `LockFileEx/UnlockFileEx`, `GetFileInformationByHandle`, `GetTempPath/GetFullPathName`, `FILE` EOF flags on every read path,
  `vfs_truncate` zero‑fill on growth, `GetConsoleMode`/`GetFileType` say *not a console* for redirected std handles (SQLite in a pipeline used to
  read from the console), `SetFileAttributes`, UTF‑8 → UTF‑16 for `W` strings, ~90 msvcrt/UCRT/kernel32 compat shims (`emu/x64/win32_compat.inc`).
* **Inspector**: imports from a program's own DLLs are reported separately (`privateDllImports`, "Own DLLs" row, verdict *partial* with the
  explanation) instead of being counted as missing.
* **x64 Runner**: real apps appear in the executable list with ready‑made default arguments and demo inputs (`demo.sql`, `demo.json`, `demo.lua`,
  `demo.txt`); all samples are also placed on the virtual disk so `busybox64.exe sh -c './7z.exe …'` pipelines work.
* Tests: `emu/tests/run.sh` 45 → 54 checks (`[9] private DLLs`, `[10] real applications`, `[11] x87`), e2e 27 → 28 scenarios (all five apps in Chromium).
* Honest limits: C++ exceptions / SEH unwinding are not implemented (7‑Zip's *error* paths that `throw` stop with a fault; the success paths are fine),
  no sockets/TLS, no GUI beyond GDI, DLL `DLL_THREAD_ATTACH/DETACH` and `PROCESS_DETACH` notifications are not delivered, interpreter speed ≈ 30 MIPS
  (7‑Zip `-mx5` on megabytes takes minutes — use `-mx1` or small inputs).

# v1.6.0 — Nebula x64: threads, synchronisation, a real heap

* **Threads** (`emu/x64/win32_threads.inc`): `CreateThread`/`_beginthread(ex)`/`_endthreadex`/`ExitThread` create real guest threads — own 1 MB
  stack, own TEB and static‑TLS block, thread id — that the emulator schedules round‑robin together with the process scheduler of v1.5
  (400 k‑instruction slices, a blocked thread never burns CPU). `GetCurrentThreadId`, `GetThreadId`, `OpenThread`, `SuspendThread`/`ResumeThread`
  (create‑suspended), `GetExitCodeThread`, `TerminateThread`, `Get/SetThreadPriority`, `GetThreadTimes`, `SwitchToThread`, `SetThreadDescription`,
  `QueueUserAPC` (+ alertable waits), `TlsAlloc/Get/Set` per thread, `__tls_index`/static TLS copies per thread.
* **Synchronisation with Windows semantics**: critical sections (recursive, `TryEnter`, spin count), SRW locks (shared/exclusive, `Try*`),
  condition variables (`SleepConditionVariableCS/SRW`, `Wake(All)ConditionVariable`), events (manual/auto reset, `PulseEvent`, direct hand‑off to one
  waiter), mutexes (recursive, abandoned → `WAIT_ABANDONED`, `ERROR_NOT_OWNER`), semaphores (`ERROR_TOO_MANY_POSTS`), waitable timers,
  `WaitForSingleObject(Ex)`/`WaitForMultipleObjects(Ex)` (wait‑any and wait‑all, timeouts → `WAIT_TIMEOUT`, thread/process handles as waitable),
  `SignalObjectAndWait`, `WaitOnAddress`/`WakeByAddressSingle/All`, `InitOnceExecuteOnce/BeginInitialize/Complete`, named objects shared between
  processes (`ERROR_ALREADY_EXISTS`), `Interlocked*` are honoured by the single‑core model. A deadlock of *all* threads of *all* processes is reported
  as a fault with the blocked objects, never a hang.
* **mingw winpthreads works unmodified**: `pthread_create/join/detach`, mutex, cond, rwlock, barrier, once, keys, `sched_yield`, spin locks
  (`emu/tests/pthreads.c`, 9 checks) — plus `_set_errno/_get_errno`, `GetSystemTimeAdjustment`, `SetProcessAffinityMask`, `SetSystemTime`,
  `mbstowcs/mbsrtowcs` so the import scan says *should run*.
* **Heap rewritten**: 16‑byte headers in guest memory + size‑class free lists → `malloc/free/realloc/HeapAlloc/HeapFree/HeapSize` are O(1)
  regardless of the number of live blocks (was an O(n) table capped at 65 536 blocks), and a block can no longer straddle two 16 MB heap regions
  (that was the real cause of busybox `sort -n` corrupting its line array at exactly 16 384 lines). `seq 1 100000 | sort -rn` now works.
* **Two CPU/ABI bugs found by the thread tests and fixed**: `punpcklqdq` (0F 6C) returned the *high* qwords (it broke clang's vectorised loops
  comparing four counters at once), and callback continuations (`qsort`/`bsearch` comparators, window procedures) leaked 32–48 bytes of guest
  stack per call — `qsort` of 10^6 elements overflowed the 8 MB stack. The qsort shim is now a bottom‑up merge sort (was insertion sort).
* UI: *N threads* badge in the x64 Runner status bar (`x64_thread_count` export), samples `threads.exe`/`pthreads.exe`, verdict text updated
  (threads/processes/pipes are emulated; GPU/audio/network are not).
* Tests: `emu/tests/run.sh` 40 → 45 checks (`[7] threads & synchronisation`, `[8] heap`), e2e 26 → 27 scenarios (threads in the browser).
* Known limits: a single wasm thread executes all guest threads (no parallel speed‑up, correct interleaving); `SuspendThread` of a running thread
  takes effect at the next slice boundary; kernel‑object handles have no generation counter (a stale handle re‑used after `CloseHandle` may address a new object).

# v1.5.0 — Nebula x64: processes and pipes

* **Process table + cooperative scheduler** (`emu/x64/api.c`): every `CreateProcessA/W` / `_spawnv(e)` / `_execv(e)` loads the target PE into a *new*
  address space (own `Mem`, `Cpu`, `Win32`, fd table) inside the same wasm module; up to 32 live processes are round‑robined by `x64_run` with
  blocking rules for stdin, child exit (`WaitForSingleObject(Ex)`, `WaitForMultipleObjects`, `_cwait`), pipe full/empty and `Sleep`.
  Root exit ends the session; a faulting child becomes exit code `0xC0000005` (busybox prints `SEGV`), never a host crash. Memory of exited
  processes goes back to a pool. A deadlock (everyone blocked on each other) is reported as a fault instead of a hang.
* **Anonymous pipes** (`CreatePipe`, `_pipe`, `PeekNamedPipe`, `GetFileType`=PIPE): 64 KB ring buffers; readers see EOF when the last writer closes,
  writers get `ERROR_NO_DATA`/`EPIPE` when the last reader is gone (`yes | head -1` terminates). Partial writes resume correctly after a block.
* **Handle inheritance** done the Windows way: `SECURITY_ATTRIBUTES.bInheritHandle`, `_O_NOINHERIT`, `SetHandleInformation/GetHandleInformation`,
  `DuplicateHandle`, `STARTF_USESTDHANDLES`; children get a copy of the parent's inheritable fd table at the same indexes (msvcrt semantics) plus
  the parent's environment; `_spawnve` honours `envp`. Closed std descriptors behave POSIX‑like (`close(0); open("/dev/null")` returns 0).
* **File mappings become kernel objects** that survive across processes (busybox‑w32 `--forkshell` passes a mapping handle to the child);
  views are flushed on `UnmapViewOfFile`/`FlushViewOfFile`/before spawning.
* **Process introspection** for `ps`/`kill`: `CreateToolhelp32Snapshot` + `Process32First/Next(W)` with correct x64 `PROCESSENTRY32` layout,
  `GetCurrentProcessId/_getpid`, `OpenProcess`, `GetProcessId`, `GetExitCodeProcess`, `TerminateProcess`, `GetProcessTimes` (real creation time),
  psapi `GetProcessImageFileNameA/W`, `QueryFullProcessImageNameA/W`, `EnumProcessModules(Ex)`, `GetModuleFileNameExA/W`, `GetModuleBaseNameA`,
  `ReadProcessMemory` (busybox `ps` reads the command line out of sibling processes), `IsWow64Process(2)`,
  `CreateRemoteThread(ExitProcess, code)` = how busybox `kill` delivers signals. A synthetic parent `explorer.exe` (pid 1000) keeps `getppid()!=1`.
* **CRT additions**: `sscanf` (was an unimplemented‑API stop), `_putenv` rebuilds `_environ` immediately (busybox `clearenv()` loops on it),
  `NUL`/`/dev/null`/`/dev/zero` devices, `strerror` for all CRT errnos (`Broken pipe`), MSVC command‑line quoting rules in the argv splitter.
* **Measured on busybox‑w32 `sh`** (native + wasm64 in Chromium): `echo hello | tr a-z A-Z`, `seq 1 5 | sort -rn | head -2`, `x=$(echo sub)`,
  nested `$( $( ) )`, `false; (exit 7); sh -c "exit 3"` exit codes, `sleep 1 & ps; wait`, `kill -9 $!` → 137, `yes | head`,
  `head -c 1000000 /dev/zero | wc -c` (busybox spawns `dd` for `/dev/zero`), `printf … | while read`, `xargs -I{} sh -c`, `FOO=baz sh -c`,
  stdin shared between parent and child, `echo payload | gzip -c | gunzip -c`, 40 sequential spawns in 0.46 s native.
* `x64run --trace` / `--trace2` print the scheduler decisions and per‑process API deltas (`X64_LOG=1`); `x64_proc_count` export and a
  “N processes” badge in the x64 Runner status bar.
* Tests: `emu/tests/run.sh` 26 → 40 checks (new section `[3c] processes + pipes`), e2e 25 → 26 scenarios (busybox pipelines/`ps` in the browser).
* Still missing (honest): threads, sockets, Direct3D/OpenGL/audio, dialog resources; a pipeline deeper than 32 stages fails with “unable to spawn shell”.

# v1.4.0 — Nebula x64: virtual disk, stdin, environment

* **Virtual file system** (`emu/x64/vfs.c`): an in‑memory `C:\` drive shared by every program run in the session (cwd `C:\nebula`, `C:\tmp`).
  Win32: `CreateFileA/W, ReadFile, WriteFile, CloseHandle, GetFileSize(Ex), SetFilePointer(Ex), SetEndOfFile, GetFileType, GetFileAttributes(Ex)A/W,
  GetFileInformationByHandle, FindFirstFile(Ex)A/W / FindNextFile / FindClose (wildcards), DeleteFile, CreateDirectory, RemoveDirectory, MoveFile(Ex), CopyFile,
  Get/SetCurrentDirectory, GetFullPathName, GetTempPath, GetTempFileName, CreateFileMapping/MapViewOfFile (copy semantics)`.
  CRT: `fopen/_wfopen/fopen_s/freopen/fclose/fread/fwrite/fgets/fgetc/getc/getchar/ungetc/fseek/_fseeki64/ftell/rewind/feof/clearerr/_fileno/_fdopen,
  _filbuf/_flsbuf (MinGW's inline getc/putc), _open/_wopen/_sopen_s/_close/_read/_write/_lseek(i64)/_tell/_eof/_chsize/_dup/_dup2, _stat64*/_fstat64*,
  _access/_unlink/remove/rename/_mkdir/_rmdir/_getcwd/_chdir/_fullpath, _findfirst64/_findnext64`.
  std handles can be redirected (`dup2` onto 0/1/2), so busybox `sh` redirections `>`, `>>`, `<`, `1>&2` work.
* **stdin**: blocking reads (`ReadFile`, `ReadConsoleA/W`, `ReadConsoleInputW`, `_read`, `fgets`, `getchar`, `_getch`, `gets_s`) suspend the guest until the
  console input line (Enter) or **EOF** button delivers data — no busy loop; the native `x64run` reads the real stdin (`--no-stdin` to force EOF).
* **Environment block** is dynamic now: `getenv/_wgetenv/getenv_s/_dupenv_s/_putenv/_putenv_s/GetEnvironmentVariable/SetEnvironmentVariable/
  ExpandEnvironmentStrings/GetEnvironmentStrings/_environ` all see the same table (`busybox env` shows what the program exported).
* **Fixed silent stubs** that returned 0: `strspn/strcspn/strpbrk/strtok/strtok_s/_strlwr/_strupr/_strrev/_itoa/_ltoa/_ultoa/_i64toa/strerror/strcpy_s/strcat_s/
  strncpy_s/memcpy_s/_mktemp`, `bsearch` with a guest comparator (was a hard fault), `setbuf/setvbuf`, Toolhelp `Process32First/Next`.
* **x64 Runner UI**: “Open folder…” mounts a whole directory under `C:\nebula\` (and lists the .exe files it contains), **Files** tab shows the virtual disk
  with *written* badges, download and delete, drag‑and‑drop of data files; console gets an input line + EOF; new status `WAITING-STDIN`.
* `x64run --mount <dir>[=C:\path]` copies a host folder into the VFS for headless tests; `emu/tests/run.sh` grew a VFS/stdin section (26 checks).
* Measured on busybox‑w32: `ls -l cat wc sort head tail grep sed awk cut find diff cmp cp tee xxd base64 md5sum sha256sum stat du mkdir sort -r (stdin) sh -c "for…; read x; [ -f ]; > <"` work.
* Still missing (honest): child processes / pipes (`sh` pipelines, `$(…)`, non‑builtin applets inside `sh` say “unable to spawn shell”), threads, sockets,
  Direct3D/OpenGL/audio, dialog resources — planned next: in‑emulator process table + anonymous pipes.
* e2e: 25 scenarios (6 for the emulator; the new one mounts a file, sorts it, feeds stdin to `sort -r`, writes `out.txt` with `tee`, reads it back, downloads it).

