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

