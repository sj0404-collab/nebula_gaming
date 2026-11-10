// x64 Runner — runs real Windows x86-64 executables inside Nebula OS through the wasm64 emulator (emu/x64) and presents them with WebGL.
import { useCallback, useEffect, useRef, useState } from 'react';
import { registerPlugin } from '@capacitor/core';
import { nativePlatform } from '../../lib/bridge';
import { Cpu, FolderOpen, FolderInput, Play, Square, Terminal as TerminalIcon, Monitor, FileSearch, AlertTriangle, CheckCircle2, HelpCircle, XCircle, Gamepad2, HardDrive, Download, Trash2, SendHorizontal, Keyboard, LayoutGrid, Maximize2, Minimize2, Image as ImageIcon, FlaskConical, Loader2, PackagePlus, X, Mouse, MousePointerClick, Expand } from 'lucide-react';
import { scanLibrary, classifyExe, loadPrefs, savePrefs, makeLnk, type LibraryItem, type LibraryPrefs } from '../../lib/x64/library';
import { peIconDataUrl } from '../../lib/x64/peicon';
import { notifyDiskChange } from '../../lib/x64/diskbus';
import { diskKey } from '../../lib/x64/disk';
import { zipEntries, zipRead } from '../../lib/x64/zip';
import { useOS } from '../../lib/os';
import { X64Runtime, vkFromEvent, wasm64Supported, type JitInfo, type PeInfo, type RunStats, type VfsEntry } from '../../lib/x64/runtime';
import { FramePresenter } from '../../lib/x64/present';
import { VirtualPad } from './VirtualPad';
import { TouchMouse, type MouseBtn } from './TouchMouse';
import { type PadState } from '../../lib/x64/pad';
import { takeX64Launch, onX64Launch, type X64Launch } from '../../lib/x64/launch';

interface FolderInfo { selected: boolean; name?: string; error?: string }
const NebulaStorage = registerPlugin<{ folderInfo(): Promise<FolderInfo>; chooseFolder(): Promise<FolderInfo> }>('NebulaStorage');

interface ExeEntry { id: number; name: string; bytes: Uint8Array; info: PeInfo; source: 'sample' | 'file' | 'disk'; pending?: string[]; path?: string; kind?: 'installer' | 'program'; cwd?: string }   // path: guest path when not C:\nebula\<name>   // pending: companion files fetched on first Run (big runtimes)

const SAMPLE_DATA = ['demo.sql', 'demo.json', 'demo.lua', 'demo.txt', 'demo.py', 'demo.bat'];   // small inputs for the real apps, placed in C:\nebula
const PY_FILES = ['python312.dll', 'python312.zip', 'python312._pth', '_sqlite3.pyd', 'sqlite3.dll', '_decimal.pyd', '_ctypes.pyd', 'libffi-8.dll', '_bz2.pyd', '_lzma.pyd', 'pyexpat.pyd', '_elementtree.pyd', 'unicodedata.pyd', '_queue.pyd', '_socket.pyd', 'select.pyd', '_asyncio.pyd', '_zoneinfo.pyd', '_uuid.pyd', '_multiprocessing.pyd'];
const SAMPLES: { name: string; href: string; desc: string; args?: string; files?: string[]; lazy?: boolean; needsPack?: boolean; needsDx?: boolean; dir?: string }[] = [   // dir: subdirectory of C:\nebula for the exe + files
  { name: 'NebulaStrike.exe', href: '/downloads/NebulaStrike.exe', desc: 'Win32 GDI game, PE32+ x64 (light GUI exe)' },
  { name: 'NebulaStrike32.exe', href: '/x64/samples/NebulaStrike32.exe', desc: 'The same Win32 GDI game built as a 32-bit x86 PE32 exe: runs as a 32-bit process (IA-32 mode, fs:[0] TEB, stdcall shims)' },
  { name: 'typing.exe', href: '/x64/samples/typing.exe', desc: 'Text input test: an EDIT control reports what arrived from the keyboard — on Android open the ⌨ button on the screen and type (Latin, Cyrillic, punctuation); prints TYPING OK for the reference sentence', args: '20000' },
  { name: 'fdlg.exe', href: '/x64/samples/fdlg.exe', desc: 'File dialogs (x64): GetOpenFileNameW, GetSaveFileNameA and the COM IFileOpenDialog / IFileSaveDialog (what Notepad++ uses) — four real modal dialogs one after another; type a name (fd_test.txt, report, fd_test.txt, Enter to accept notes.txt) and the program checks what came back — prints FDLG OK' },
  { name: 'fdlg32.exe', href: '/x64/samples/fdlg32.exe', desc: 'The same file-dialog test as a 32-bit PE32 exe (OPENFILENAME x86 layout, COM vtables through stdcall)' },
  { name: 'dlg.exe', href: '/x64/samples/dlg.exe', desc: 'Dialog-manager self-test (x64): DialogBoxParam from a resource template, 42 checks — static/edit/button/checkbox/radio/listbox/combobox, nested modal + MessageBox, LoadString with Cyrillic text, SHGetFolderPath, IShellLink, modeless dialog with Tab/Escape' },
  { name: 'dlg32.exe', href: '/x64/samples/dlg32.exe', desc: 'The same dialog-manager self-test built as a 32-bit PE32 exe (IA-32 mode, stdcall shims) — must also print ALL OK 42/42' },
  { name: 'hello32.exe', href: '/x64/samples/hello32.exe', args: 'a b', desc: '32-bit x86 console exe (PE32): argv, heap, 64-bit math on 32-bit, threads, TLS, files — exit code 42' },
  { name: 'seh32.exe', href: '/x64/samples/seh32.exe', desc: '32-bit SEH self-test: fs:[0] registration chain, RaiseException → handler → RtlUnwind → longjmp, access-violation fix-up through the CONTEXT, vectored handlers, top-level filter' },
  { name: 'gltri.exe', href: '/x64/samples/gltri.exe', desc: 'OpenGL → WebGL2 (x64): opengl32.dll on the device GPU — ChoosePixelFormat/SetPixelFormat, wglCreateContext/MakeCurrent, wglGetProcAddress, GLSL 330 + VAO/VBO triangle, a GLSL 120 program drawing GL_QUADS from a client-side array (translated/emulated by the bridge), glReadPixels check, SwapBuffers' },
  { name: 'glff.exe', href: '/x64/samples/glff.exe', desc: 'OpenGL 1.x fixed-function pipeline + GLU (x64): glBegin/glEnd, matrix stacks, display lists, lighting, texturing (texenv, 1D, gluBuild2DMipmaps), fog, alpha test, clip planes, glPolygonMode, glDrawPixels/glBitmap, vertex arrays, gluPerspective/gluLookAt/gluSphere/gluProject — 90+ pixel checks' },
  { name: 'glff32.exe', href: '/x64/samples/glff32.exe', desc: 'The same OpenGL 1.x / GLU self-check as a 32-bit exe (glu32.dll doubles on the x86 stack)' },
  { name: 'gltri32.exe', href: '/x64/samples/gltri32.exe', desc: 'The same OpenGL test as a 32-bit PE32 exe (stdcall GL entry points with float/double arguments on the x86 stack)' },
  { name: 'd3d11tri.exe', href: '/x64/samples/d3d11tri.exe', desc: 'Direct3D 11 → WebGL2: device + swap chain, vertex/index/constant buffers, input layout, precompiled DXBC vertex/pixel shaders translated to GLSL, Draw + DrawIndexed + Present (3 frames). Args: number of frames', args: '3' },
  { name: 'd3d11clear.exe', href: '/x64/samples/d3d11clear.exe', desc: 'Direct3D 11: D3D11CreateDeviceAndSwapChain, GetBuffer, CreateRenderTargetView, ClearRenderTargetView + Present in three colours' },
  { name: 'msvcp.exe', href: '/x64/samples/msvcp.exe', needsPack: true, desc: 'Runtime pack test: loads the REAL msvcp140.dll (Microsoft Visual C++ 2015–2022 Redistributable, served from /x64/sys32 as C:\\Windows\\System32) and drives std::cout through its exports — static init, locale facets and iostream output of a vendor runtime DLL on top of the emulator\'s UCRT' },
  { name: 'nebula-dotnet-test.exe', href: '/x64/samples/dotnet/nebula-dotnet-test.exe', desc: '.NET 10 (C#): the OFFICIAL Microsoft .NET Runtime (CoreCLR + RyuJIT, win-x64) runs on the emulator — installed on demand into C:\\Program Files\\dotnet (the .NET pack, ~34 MB). apphost launcher + nebula-dotnet-test.dll. Args pick the steps: linq, throw, nre (hardware null-reference → managed exception), json, reflect, regex, culture, async, thread, taskrun, bgspin (GC suspends a spinning thread), proc (Process.Start cmd.exe), http (HttpClient), gc', args: 'linq-throw-nre-json-reflect-regex-gc', files: ['nebula-dotnet-test.dll', 'nebula-dotnet-test.runtimeconfig.json', 'nebula-dotnet-test.deps.json'], dir: 'dotnet' },
  { name: 'fx.exe', href: '/x64/samples/dotnet/fx.exe', desc: 'Classic .NET Framework 4.8 console exe (AnyCPU, IL only, 5 KB): started directly, the emulator hands it to dotnet.exe with a generated runtimeconfig — it runs on the .NET 10 runtime in compatibility mode (LINQ, exceptions, File IO, args). Exit code 5', args: 'a b', dir: 'dotnet' },
  { name: 'd3dcompile.exe', href: '/x64/samples/d3dcompile.exe', needsDx: true, desc: 'DirectX pack test: loads the REAL D3DCompiler_43.dll (Microsoft DirectX End-User Runtimes, June 2010) from C:\\Windows\\System32, compiles HLSL vs_4_0/ps_4_0 at run time, disassembles it and draws the result through Direct3D 11 → WebGL2 (installs the DirectX pack on first use)' },
  { name: 'dxpack.exe', href: '/x64/samples/dxpack.exe', needsDx: true, desc: 'DirectX pack test: d3dx9_43 matrix/vector math, D3DX11CompileFromMemory, X3DAudio1_7 3-D panning, XInput (controller slot empty unless a pad is connected) — all real Microsoft DLLs except XInput' },
  { name: 'audio.exe', href: '/x64/samples/audio.exe', needsDx: true, desc: 'Sound through the device: waveOut (event + function callbacks), DirectSound 8 (secondary buffer, notifications, volume), WASAPI (IMMDeviceEnumerator → IAudioClient event-driven → IAudioRenderClient), PlaySound(SND_MEMORY|SND_SYNC) and the REAL XAudio2_7.dll from the DirectX pack — five tones, ~2.5 s. Tap/click once if the browser keeps audio suspended until a gesture.' },
  { name: 'hello.exe', href: '/x64/samples/hello.exe', desc: 'UCRT console: printf/qsort/malloc/math (light console exe)' },
  { name: 'cmd.exe', href: '/x64/apps/cmd.exe', desc: 'Command Prompt: the emulator\'s own cmd.exe (52 KB, kernel32 only; the same binary is embedded at C:\\Windows\\System32\\cmd.exe so system(), _popen and subprocess(shell=True) work). Default args run demo.bat and then stay interactive — type dir, echo, set, if, for, pipes and redirections into the console, or start any program on the virtual disk (jq.exe, python.exe …). Try: /c "dir /b | sort /r" · /c "python.exe -c print(42) | findstr 42"', args: '/k demo.bat' },
  { name: 'powershell.exe', href: '/x64/apps/powershell.exe', desc: 'Windows PowerShell subset: the emulator\'s own powershell.exe (91 KB, kernel32 only; embedded at C:\\Windows\\System32\\WindowsPowerShell\\v1.0 so installers that shell out to PowerShell work — opencode unpacks ripgrep with Expand-Archive through it). Cmdlets: Expand-Archive / [IO.Compression.ZipFile]::ExtractToDirectory (deflate + stored), Get-FileHash, Get-ChildItem, Get/Set-Content, Select-String, Where/Select/Measure-Object, Test-Path, Join-Path, New/Copy/Remove-Item, $env:, -f, -join, try/catch, if/foreach/while, & external programs, exit N. Not implemented: objects/properties beyond strings and numbers, modules, classes, .NET beyond ZipFile/Path.', args: '-NoProfile -File demo.ps1' },
  { name: 'bench.exe', href: '/x64/samples/bench.exe', desc: 'CPU benchmark: sieve, mandelbrot, memcpy (heavy compute)' },
  { name: 'pad.exe', href: '/x64/samples/pad.exe', desc: 'Game controller test: XInput 1.4, winmm joystick, DirectInput8 game pad — plug in a Bluetooth/USB pad (or open the on-screen pad) within 2 s of starting; it expects A+START, LT, left stick' },
  { name: 'busybox64.exe', href: '/x64/samples/busybox64.exe', desc: 'Real third-party binary: busybox-w32 (mingw64). Try args: ls -l · cat readme.txt · wc · sort · grep · sed · awk · sha256sum · gzip -f · date -u · factor 1234567891; type into the console line when it reads stdin' },
  { name: 'plink.exe', href: '/x64/samples/plink.exe', desc: 'Real third-party binary: PuTTY plink 0.85 (MSVC/clang-cl). Try args: -V' },
  { name: 'threads.exe', href: '/x64/samples/threads.exe', desc: 'Win32 threads & sync self-test: CreateThread, critical sections, SRW locks, condition variables, events, mutexes, semaphores, WaitForMultipleObjects, TLS, Interlocked (39 checks)' },
  { name: 'cxxexc.exe', href: '/x64/samples/cxxexc.exe', desc: 'C++ exceptions self-test (zig c++ / libunwind): throw/catch std::exception, destructors during unwinding, nested rethrow, vector::at, 1000 throws in a loop — x64 SEH tables + RtlUnwindEx emulated' },
  { name: 'hwexc.exe', href: '/x64/samples/hwexc.exe', desc: 'Hardware exceptions as SEH self-test: access violation / divide by zero / ud2 caught by __except (with EXCEPTION_RECORD details), __finally during the unwind, a vectored handler that fixes up the fault and continues execution, 2000 faults in a loop, SetUnhandledExceptionFilter → exit 77' },
  { name: 'pthreads.exe', href: '/x64/samples/pthreads.exe', desc: 'winpthreads (mingw64 libpthread) self-test: pthread_create/join, mutex, cond, rwlock, barrier, once, TLS keys, 4-thread parallel sum (9 checks)' },
  { name: 'sqlite3.exe', href: '/x64/apps/sqlite3.exe', desc: 'Real app: SQLite 3.50.4 shell (sqlite.org, MSVC). Default args run demo.sql against a file database (2000 rows, index, integrity_check). Try: :memory: "select sqlite_version(), 6*7;"', args: '-init demo.sql demo.db .quit' },
  { name: 'jq.exe', href: '/x64/apps/jq.exe', desc: 'Real app: jq 1.8.1 JSON processor (jqlang, mingw64). Default args query demo.json', args: '-M -c ".users[] | select(.age > 30) | {name, n: (.langs|length)}" demo.json' },
  { name: 'lua54.exe', href: '/x64/apps/lua54.exe', desc: 'Real app: Lua 5.4.2 interpreter (LuaBinaries) — lua54.exe imports lua54.dll, loaded from the virtual disk like on Windows. Default args run demo.lua', args: 'demo.lua', files: ['lua54.dll'] },
  { name: '7z.exe', href: '/x64/apps/7z.exe', desc: 'Real app: 7-Zip 25.01 console (7z.exe + 7z.dll loaded via LoadLibrary/GetProcAddress). Default args pack demo.txt; also try: x -y -oout demo.7z · l demo.7z · i · b -mmt1 1 -md20 (benchmark, ~1 min)', args: 'a -bb0 -mx1 demo.7z demo.txt', files: ['7z.dll'] },
  { name: 'rg.exe', href: '/x64/apps/rg.exe', desc: 'Real app, Rust: ripgrep 14.1.1 (BurntSushi, x86_64-pc-windows-msvc, static CRT — Rust std on Win32: NtReadFile, SRW locks, WaitOnAddress, waitable timers, threads). Default args search the demo files; also try: -c -w the · --stats -j4 the · --version', args: '-n -i "lua|sqlite" demo.lua demo.sql demo.json' },
  { name: 'fd.exe', href: '/x64/apps/fd.exe', desc: 'Real app, Rust: fd 10.2.0 (sharkdp) file finder. Default args list the demo inputs by extension; also try: -t f -S +100k · --version', args: '-e lua -e sql -e json' },
  { name: 'zstd.exe', href: '/x64/apps/zstd.exe', desc: 'Real app: Zstandard 1.5.7 (facebook/zstd, MSVC, SSE2/BMI paths). Default args run the built-in benchmark on demo.txt; also try: -f demo.txt · -d -f demo.txt.zst -o out.txt', args: '-b1 -e3 -i0 demo.txt' },
  { name: 'python.exe', href: '/x64/apps/py/python.exe', desc: 'Real app (heavy): CPython 3.12.10 — the official python.org Windows x64 build (MSVC 19.43), python312.dll 6.9 MB + the stdlib in python312.zip + C extensions (_sqlite3, _decimal, _ctypes, unicodedata …) loaded from the virtual disk. ~15 MB, fetched on first Run. Default args run demo.py; also try: -i -q (REPL — type into the console) · -c "print(2**100)" · -m json.tool demo.json · -m timeit "sum(range(100))" · -c "import this"', args: 'demo.py', files: PY_FILES, lazy: true },
];

const VERDICT: Record<string, { label: string; cls: string; Icon: typeof CheckCircle2 }> = {
  likely:      { label: 'imports supported',   cls: 'bg-emerald-100 text-emerald-800 border-emerald-300', Icon: CheckCircle2 },
  partial:     { label: 'partial',      cls: 'bg-amber-100 text-amber-800 border-amber-300',       Icon: HelpCircle },
  unlikely:    { label: 'unlikely',     cls: 'bg-orange-100 text-orange-800 border-orange-300',    Icon: AlertTriangle },
  unsupported: { label: 'unsupported',  cls: 'bg-rose-100 text-rose-800 border-rose-300',          Icon: XCircle },
  invalid:     { label: 'not a PE',     cls: 'bg-slate-100 text-slate-700 border-slate-300',       Icon: XCircle },
};

const TOUCH_KEYS: { label: string; vk: number; sticky?: boolean }[] = [
  { label: 'Ctrl', vk: 0x11, sticky: true }, { label: 'Alt', vk: 0x12, sticky: true }, { label: 'Shift', vk: 0x10, sticky: true },
  { label: 'Esc', vk: 0x1b }, { label: 'Tab', vk: 0x09 }, { label: '⌫', vk: 0x08 }, { label: '↵', vk: 0x0d }, { label: '◀', vk: 0x25 }, { label: '▲', vk: 0x26 }, { label: '▼', vk: 0x28 }, { label: '▶', vk: 0x27 }, { label: '␣', vk: 0x20 }, { label: 'R', vk: 0x52 },
];
/* Touch on the guest screen (Android): tap = left click, drag = press and move, hold still = right click (released on lift), two fingers = wheel.
   Mouse / pen pointers keep their real buttons. */
interface TouchState { id: number; sx: number; sy: number; gx: number; gy: number; t0: number; pressed: boolean; done: boolean; timer: number; fingers: number; lastY2: number }
/* touchpad mode: the finger moves a virtual cursor (relative, like a laptop touchpad) instead of pointing at the spot under it */
interface PadTouch { id: number; sx: number; sy: number; lx: number; ly: number; t0: number; moved: boolean; drag: boolean; done: boolean; timer: number; fingers: number; lastY2: number }
const PAD_GAIN = 1.3;
function boolPref(k: string, d: boolean): boolean { try { const v = localStorage.getItem(k); return v === null ? d : v === '1'; } catch { return d; } }
function setBoolPref(k: string, v: boolean) { try { localStorage.setItem(k, v ? '1' : '0'); } catch { /* ignore */ } }
const IME_SENTINEL = '\u200b\u200b';   // the IME field is never empty, so Backspace on an empty guest line still produces an input event

/* Runtime pack: the x64 DLLs of the VC++ 2015–2022 Redistributable, built by emu/tools/runtime-pack.sh into public/x64/sys32/
   at build time and installed on demand into C:\Windows\System32 of the virtual disk (the emulator prefers these real
   vendor-runtime DLLs over its built-in shims; core OS DLLs stay built-in). */
type PackManifest = { pack: string; files: { name: string; size: number; version: string }[] };
type PackState = { status: 'probing' | 'available' | 'installing' | 'installed' | 'unavailable'; manifest?: PackManifest; done?: number; note?: string };
const PACK_URL = '/x64/sys32/';
const PACK_BUILTIN = new Set(['vcruntime140.dll']);
/* every Visual C++ redistributable the build extracts (emu/tools/runtime-pack.sh → /x64/sys32/packs.json); the x64 2015–2022 pack is the flat one above */
type VcPack = { id: string; label: string; arch: 'x86' | 'x64'; guest_dir: string; base: string; source: string; files: { name: string; size: number; version: string }[] };
type VcState = { status: 'available' | 'installing' | 'installed' | 'error'; done?: number; note?: string };
type CompLogEntry = { t: number; pack: string; ok: boolean; text: string };
const COMP_LOG_KEY = 'nebula_x64_components_log';
const loadCompLog = (): CompLogEntry[] => { try { const v = JSON.parse(localStorage.getItem(COMP_LOG_KEY) ?? '[]'); return Array.isArray(v) ? v as CompLogEntry[] : []; } catch { return []; } };
/* import DLL name → redistributable family (the arch suffix comes from the exe) */
const VC_FAMILY: [RegExp, string][] = [
  [/^(msvcr|msvcp|msvcm|mfc|mfcm|atl)80(u|d)?\.dll$/i, 'vc2005'], [/^vcomp\.dll$/i, 'vc2005'],
  [/^(msvcr|msvcp|msvcm|mfc|mfcm|atl|vcomp)90(u|d)?\.dll$/i, 'vc2008'],
  [/^(msvcr|msvcp|mfc|mfcm|atl|vcomp)100(u|d)?\.dll$/i, 'vc2010'],
  [/^(msvcr|msvcp|mfc|mfcm|atl|vcomp|vccorlib|vcamp)110(u|d)?\.dll$/i, 'vc2012'],
  [/^(msvcr|msvcp|mfc|mfcm|atl|vcomp|vccorlib|vcamp)120(u|d)?\.dll$/i, 'vc2013'],
  [/^(msvcp|concrt|vcomp|vcamp|vccorlib|mfc|mfcm)140(u|d)?(_\w+)?\.dll$|^vcruntime140_\w+\.dll$/i, 'vc2015'],
];
const vcPacksFor = (dlls: string[] | undefined, pe32plus: boolean | undefined): string[] => {
  const out = new Set<string>();
  for (const d0 of dlls ?? []) { const d = d0.toLowerCase().endsWith('.dll') ? d0.toLowerCase() : d0.toLowerCase() + '.dll'; for (const [re, fam] of VC_FAMILY) if (re.test(d)) out.add(fam + (pe32plus ? '-x64' : '-x86')); }
  return [...out];
};   // shipped in the pack for completeness, but the emulator always uses its own implementation (pe_dll_native_ok)
const exeWantsPack = (dlls: string[] | undefined, man: PackManifest | undefined) => !!man && !!dlls && dlls.some(d => !PACK_BUILTIN.has(d.toLowerCase()) && man.files.some(f => f.name.toLowerCase() === d.toLowerCase()));

/* .NET pack: the official Microsoft .NET Runtime 10 (win-x64), built by emu/tools/dotnet-pack.sh into public/x64/dotnet/ (per-file gzip)
   and installed on demand into C:\Program Files\dotnet when a managed exe/dll or an apphost is started. */
type DotnetManifest = { pack: string; version: string; files: { path: string; size: number; gz: string; gzsize: number }[] };
type DotnetState = { status: 'probing' | 'available' | 'installing' | 'installed' | 'unavailable'; manifest?: DotnetManifest; done?: number; note?: string };
const DOTNET_URL = '/x64/dotnet/';
const DOTNET_DIR = 'C:\\Program Files\\dotnet\\';
/* DirectX pack: the x64 user-mode DLLs of the official DirectX End-User Runtimes (June 2010) — D3DCompiler_33…43, d3dx9/10/11,
   d3dcsx, X3DAudio — built by emu/tools/dx-pack.sh into public/x64/dx/ (per-file gzip, same manifest layout as the .NET pack)
   and installed on demand into C:\Windows\System32 when a program imports one of them (or asks for it with LoadLibrary). */
const DX_URL = '/x64/dx/';
const DX_DIR = 'C:\\Windows\\System32\\';
type GzPack = { key: 'dotnet' | 'dx' | 'dx32'; url: string; dir: string; label: string };
const DX32_URL = '/x64/dx/x86/';
const DX32_DIR = 'C:\\Windows\\SysWOW64\\';
const GZ_PACKS: Record<'dotnet' | 'dx' | 'dx32', GzPack> = { dotnet: { key: 'dotnet', url: DOTNET_URL, dir: DOTNET_DIR, label: '.NET pack' }, dx32: { key: 'dx32', url: DX32_URL, dir: DX32_DIR, label: 'DirectX pack (x86)' }, dx: { key: 'dx', url: DX_URL, dir: DX_DIR, label: 'DirectX pack' } };
const exeWantsDx = (dlls: string[] | undefined, man: DotnetManifest | undefined) => !!man && !!dlls && dlls.some(d => man.files.some(f => f.path.toLowerCase() === d.toLowerCase()));
async function fetchGz(url: string, attempt = 0, plainUrl?: string): Promise<Uint8Array> {   // some static servers (vite preview, nginx gzip_static) send .gz files with Content-Encoding: gzip → the browser already inflated them
  const ac = new AbortController(); const timer = setTimeout(() => ac.abort(), 90_000); let raw: Uint8Array<ArrayBuffer>;
  try { let r = await fetch(url, { signal: ac.signal }); if (r.status === 404 && plainUrl) r = await fetch(plainUrl, { signal: ac.signal }); if (!r.ok) throw new Error(`${r.url || url}: HTTP ${r.status}`); raw = new Uint8Array(await r.arrayBuffer()); }
  catch (e) { clearTimeout(timer); if (attempt < 2) return fetchGz(url, attempt + 1, plainUrl); throw new Error(`${url}: ${ac.signal.aborted ? 'timed out' : String(e)}`); }
  clearTimeout(timer);
  if (raw.length < 2 || raw[0] !== 0x1f || raw[1] !== 0x8b) return raw;
  if (typeof DecompressionStream === 'undefined') throw new Error('DecompressionStream not supported by this browser');
  const buf = await new Response(new Blob([raw]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer();
  return new Uint8Array(buf);
}

export default function X64App() {
  const rtRef = useRef<X64Runtime | null>(null);
  const presenterRef = useRef<FramePresenter | null>(null);
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const consoleRef = useRef<HTMLPreElement | null>(null);
  const screenRef = useRef<HTMLDivElement | null>(null);
  const [ready, setReady] = useState<'loading' | 'ready' | 'unsupported' | 'error'>(() => (wasm64Supported() ? 'loading' : 'unsupported'));
  const [initError, setInitError] = useState('');
  const [exes, setExes] = useState<ExeEntry[]>([]);
  const [selected, setSelected] = useState<number | null>(null);
  const [tab, setTab] = useState<'library' | 'screen' | 'console' | 'files' | 'report' | 'components'>('library');
  const { pushToast } = useOS();
  const [showTests, setShowTests] = useState(false);          // the emulator's self-test / sample programs (hidden by default; loaded on demand)
  const samplesLoaded = useRef<Promise<ExeEntry[]> | null>(null);
  const [samplesReady, setSamplesReady] = useState(false);
  const [library, setLibrary] = useState<LibraryItem[]>([]);  // what is installed on the persistent C:\ (Start Menu shortcuts + Uninstall registry keys)
  const libIds = useRef<Set<string>>(new Set());
  const [icons, setIcons] = useState<Record<string, string>>({});
  const iconCache = useRef(new Map<string, string | null>());
  const [prefs, setPrefs] = useState<Record<string, LibraryPrefs>>(() => loadPrefs());
  const [fs, setFs] = useState(false);                        // fullscreen program view (game mode)
  const [vpad, setVpad] = useState(false);                    // on-screen game controller (controller 0 for XInput/DirectInput/winmm)
  const [padInfo, setPadInfo] = useState('');
  const onVpad = useCallback((p: PadState) => { rtRef.current?.setVirtualPad(p); }, []);
  const rootRef = useRef<HTMLDivElement | null>(null);
  const [disk, setDisk] = useState({ files: 0, writes: 0, bytes: 0, queued: 0 });
  const [menuFor, setMenuFor] = useState<string | null>(null);
  const [diskReady, setDiskReady] = useState(false);
  const [storageFolder, setStorageFolder] = useState<FolderInfo>({ selected: false });
  const [storageBusy, setStorageBusy] = useState(false);
  const [storageMessage, setStorageMessage] = useState('');
  useEffect(() => {
    if (nativePlatform() !== 'android') return;
    let active = true;
    void NebulaStorage.folderInfo().then(info => { if (active) setStorageFolder(info); }).catch(() => { if (active) setStorageMessage('Не удалось проверить разрешение на папку.'); });
    return () => { active = false; };
  }, []);
  const chooseStorageFolder = async () => {
    setStorageBusy(true); setStorageMessage('');
    try { setStorageFolder(await NebulaStorage.chooseFolder()); }
    catch (error) { setStorageMessage(String(error)); }
    finally { setStorageBusy(false); }
  };
  const rescanRef = useRef<(announce: boolean) => void>(() => {});
  const [files, setFiles] = useState<VfsEntry[]>([]);
  const [stdinLine, setStdinLine] = useState('');
  const [waitingStdin, setWaitingStdin] = useState(false);
  const stdinRef = useRef<HTMLInputElement | null>(null);
  const [consoleText, setConsoleText] = useState('');
  const [log, setLog] = useState<string[]>([]);
  const [stats, setStats] = useState<RunStats | null>(null);
  const [args, setArgs] = useState('');
  const [glInfo, setGlInfo] = useState('');
  const [touch] = useState(() => typeof matchMedia === 'function' && matchMedia('(pointer: coarse)').matches);
  const [padMode, setPadMode] = useState(() => boolPref('nebula_x64_touchpad', touch));   // touchpad: relative cursor (default on phones/tablets)
  const [tmShow, setTmShow] = useState(() => boolPref('nebula_x64_tmouse', touch));       // on-screen mouse buttons
  const [deskFit, setDeskFit] = useState(() => boolPref('nebula_x64_deskfit', touch));
  const [fpsShow, setFpsShow] = useState(() => boolPref('nebula_x64_fps', true));          // FPS / MIPS counter in the corner of the screen
  const [fps, setFps] = useState(0); const fpsRef = useRef({ t: 0, f: 0 });    // the guest desktop = the device screen (portrait and landscape)
  useEffect(() => setBoolPref('nebula_x64_touchpad', padMode), [padMode]);
  useEffect(() => setBoolPref('nebula_x64_tmouse', tmShow), [tmShow]);
  useEffect(() => setBoolPref('nebula_x64_deskfit', deskFit), [deskFit]);
  useEffect(() => setBoolPref('nebula_x64_fps', fpsShow), [fpsShow]);
  const cur = useRef({ x: -1, y: -1 });   // virtual cursor in guest pixels (-1 = not placed yet → screen centre); also the last known pointer spot for the on-screen buttons
  const cursorRef = useRef<HTMLDivElement | null>(null);
  const ensureVC10Ref = useRef<(rt: X64Runtime, e: ExeEntry) => Promise<boolean>>(async () => true);
  const applyDesktopRef = useRef<() => void>(() => {});   // measured desktop size → emulator, callable from the start paths declared above its definition
  const [moduleKB, setModuleKB] = useState(0);
  const [apiStats, setApiStats] = useState('');
  const [jitInfo, setJitInfo] = useState<JitInfo | null>(null);
  const [frames, setFrames] = useState(0);
  const autoTabRef = useRef(true);   // true until the user picks a tab during a run: a console program that opens a window (dlg.exe, a Python GUI) is then shown on the screen tab as soon as its first frame arrives, like on Windows
  const nextId = useRef(1);
  const consoleBuf = useRef('');

  /** download one bundled sample (or test program) onto the virtual disk; cached per name */
  const loadSample = useCallback(async (rt: X64Runtime, sName: string): Promise<ExeEntry | null> => {
    const sm = SAMPLES.find(x => x.name === sName); if (!sm) return null;
    try {
      const r = await fetch(sm.href); if (!r.ok) return null; const bytes = new Uint8Array(await r.arrayBuffer());
      const d = sm.href.slice(0, sm.href.lastIndexOf('/') + 1); const gd = 'C:\\nebula\\' + (sm.dir ? sm.dir + '\\' : '');
      rt.vfsAdd(gd + sm.name, bytes);
      if (!sm.lazy) for (const f of sm.files ?? []) { const rf = await fetch(d + f); if (rf.ok) rt.vfsAdd(gd + f, new Uint8Array(await rf.arrayBuffer())); }
      return { id: nextId.current++, name: sm.name, bytes, info: rt.inspect(bytes), source: 'sample', pending: sm.lazy && sm.files ? sm.files.map(f => d + f) : undefined, path: sm.dir ? gd + sm.name : undefined };
    } catch { return null; }
  }, []);
  const loadSamples = useCallback((rt: X64Runtime): Promise<ExeEntry[]> => {
    if (samplesLoaded.current) return samplesLoaded.current;
    samplesLoaded.current = (async () => {
      const loaded: ExeEntry[] = [];
      for (const sm of SAMPLES) { const e = await loadSample(rt, sm.name); if (e) { loaded.push(e); setExes(l => l.some(x => x.name === e.name && x.source === 'sample') ? l : [...l, e]); } }
      for (const f of SAMPLE_DATA) { try { const r = await fetch('/x64/apps/' + f); if (r.ok) rt.vfsAdd('C:\\nebula\\' + f, new Uint8Array(await r.arrayBuffer())); } catch { /* optional */ } }
      rt.vfsAdd('C:\\nebula\\readme.txt', new TextEncoder().encode('Nebula x64 virtual disk\nThis file lives in C:\\nebula\\readme.txt inside the emulator.\nTry: busybox64.exe cat readme.txt | wc -l | sort | sha256sum readme.txt\n'));
      setFiles(rt.vfsList()); setSamplesReady(true); return loaded;
    })();
    return samplesLoaded.current;
  }, [loadSample]);
  const launchByName = useCallback(async (rt: X64Runtime, list: ExeEntry[], want: X64Launch) => {
    let e = list.find(x => x.name.toLowerCase() === want.exe.toLowerCase());
    if (!e && want.exe.includes('\\')) {   // a full path on the persistent disk (File Explorer → double-click an .exe or a shortcut)
      const bytes = rt.vfsRead(want.exe);
      if (!bytes) { pushToast('Programs', `${want.exe} is not on the disk`, 'x64'); return; }
      const dir = want.exe.slice(0, want.exe.lastIndexOf('\\'));
      e = { id: nextId.current++, name: want.exe.slice(dir.length + 1), bytes, info: rt.inspect(bytes), source: 'disk', path: want.exe, cwd: want.cwd || dir, kind: classifyExe(want.exe, bytes) };
      const got = e; setExes(l => [...l.filter(x => !(x.source === 'disk' && x.path?.toLowerCase() === got.path?.toLowerCase())), got]);
    }
    if (!e) { const got = await loadSample(rt, want.exe); if (!got) return; e = got; setExes(l => [...l, got]); }
    if (!e || e.pending) return;
    if (rt.status === 'running' || rt.status === 'waiting-stdin') rt.stop();
    if (!await ensureVC10Ref.current(rt, e)) return;
    setSelected(e.id); setArgs(want.args); consoleBuf.current = ''; setConsoleText(''); setLog([]);
    const argv0 = e.path && e.path.includes(' ') ? `"${e.path}"` : e.path ?? e.name;   // like Explorer: a quoted full path when it has spaces (launchers re-parse GetCommandLineW)
    applyDesktopRef.current();
    const err = rt.start(e.bytes, e.path ?? e.name, want.args.trim() ? `${argv0} ${want.args.trim()}` : argv0, e.cwd);
    if (err) { setLog(l => [...l, 'LOADER: ' + err]); pushToast('Programs', 'Launch failed: ' + err, 'x64'); }
    setStats(rt.stats()); setFiles(rt.vfsList()); setTab(err ? 'report' : 'console');
  }, [loadSample, pushToast]);

  // init runtime once
  useEffect(() => {
    let alive = true;
    if (!wasm64Supported()) return;
    const rt = new X64Runtime(); rtRef.current = rt;
    (window as unknown as { __x64?: X64Runtime }).__x64 = rt;   // debugging aid (e2e / devtools): the live runtime of the last opened Programs window
    rt.onConsole = (_fd, text) => { consoleBuf.current = (consoleBuf.current + text).slice(-60000); setConsoleText(consoleBuf.current); };
    rt.onLog = (text) => setLog(l => [...l.slice(-199), text.trimEnd()]);
    rt.onStdinWait = (w) => { setWaitingStdin(w); if (w) { setTab('console'); setTimeout(() => stdinRef.current?.focus(), 30); } };
    rt.onPresent = (px, w, h) => { presenterRef.current?.present(px, w, h); setFrames(f => { if (f === 0 && autoTabRef.current) setTab('screen'); return f + 1; }); };
    rt.onGlPresent = (cv, x, y, w, h) => presenterRef.current?.setGl(cv, x, y, w, h) ?? false;
    rt.onGlFlip = () => { presenterRef.current?.redraw(); setFrames(f => f + 1); };
    rt.onStop = (s) => { void rt.syncDisk().then(notifyDiskChange);
      setFetching(f => f && f.startsWith('Java: one-time') ? null : f);
      if (s.status === 'exited' && rt.relaunchRequested() && relaunches.current < 2 && curRef.current) { relaunches.current++; setLog(l => [...l, 'Java: class-data archive built — starting the program again']); setTimeout(() => void runRef.current(curRef.current ?? undefined, argsRef.current), 300); return; }
      if (s.status === 'fault') { setFs(false); if (document.fullscreenElement) void document.exitFullscreen().catch(() => {}); } setStats(s); setApiStats(rt.apiStats()); setJitInfo(rt.jitStats()); setFiles(rt.vfsList()); rescanRef.current(true); if (s.status !== 'exited' || !s.hasWindow) setTab(t => (s.hasWindow && s.status === 'exited') ? t : (s.status === 'fault' ? 'report' : t === 'screen' && !s.hasWindow ? 'console' : t)); };
    rt.onDisk = () => setDisk({ files: rt.diskFiles, writes: rt.disk.writes, bytes: rt.disk.bytes, queued: rt.disk.queued() });
    rt.init().then(async () => {
      if (!alive) return;
      setFiles(rt.vfsList()); setModuleKB(rt.moduleBytes / 1024);
      setDisk({ files: rt.diskFiles, writes: 0, bytes: 0, queued: 0 }); setDiskReady(rt.disk.available);
      setReady('ready');
      rescanRef.current(false);
      const want = takeX64Launch();   // another app (the Command Prompt's `real` command) asked us to start a program right away
      if (want) void launchByName(rt, [], want);
    }).catch(e => { if (alive) { setInitError(String(e)); setReady('error'); } });
    return () => { alive = false; rt.dispose(); const g = window as unknown as { __x64?: X64Runtime }; if (g.__x64 === rt) delete g.__x64; };
  }, [launchByName]);

  // launch requests from other apps while we are open (Command Prompt → `real`)
  useEffect(() => onX64Launch(want => { const rt = rtRef.current; if (rt && ready === 'ready') void launchByName(rt, exes, want); }), [ready, exes, launchByName]);
  // the test programs panel: download the bundled samples the first time it is opened
  useEffect(() => { const rt = rtRef.current; if (showTests && rt && ready === 'ready') void loadSamples(rt).then(l => { if (l.length) setSelected(sel => sel ?? l[0].id); }); }, [showTests, ready, loadSamples]);
  // exit fullscreen when the browser/WebView leaves it (back gesture)
  useEffect(() => { const h = () => { if (!document.fullscreenElement) setFs(false); }; document.addEventListener('fullscreenchange', h); return () => document.removeEventListener('fullscreenchange', h); }, []);

  /* ---- the library: installed programs & games on the persistent disk ---- */
  const loadIcons = useCallback(async (items: LibraryItem[]) => {
    const rt = rtRef.current; if (!rt) return;
    for (const it of items) {
      const key = it.iconPath.toLowerCase() + '#' + it.iconIndex; if (iconCache.current.has(key)) continue;
      iconCache.current.set(key, null);
      const src = /\.(exe|dll)$/i.test(it.iconPath) ? rt.vfsRead(it.iconPath) ?? rt.vfsRead(it.exe) : rt.vfsRead(it.exe); if (!src) continue;
      const url = await peIconDataUrl(src, 96, it.iconIndex); iconCache.current.set(key, url);
      if (url) setIcons(m => ({ ...m, [it.id]: url }));
    }
  }, []);
  const rescan = useCallback((announce: boolean) => {
    const rt = rtRef.current; if (!rt) return;
    const items = scanLibrary(rt.vfsList(), p => rt.vfsRead(p));
    if (announce) { const fresh = items.filter(i => !libIds.current.has(i.id)); if (fresh.length) pushToast('Programs', `Installed: ${fresh.map(f => f.name).join(', ')}`, 'x64'); }
    libIds.current = new Set(items.map(i => i.id)); setLibrary(items); void loadIcons(items);
  }, [pushToast, loadIcons]);
  useEffect(() => { rescanRef.current = rescan; }, [rescan]);
  const updatePrefs = useCallback((id: string, patch: LibraryPrefs) => setPrefs(p => { const n = { ...p, [id]: { ...p[id], ...patch } }; savePrefs(n); return n; }), []);
  const toggleFullscreen = useCallback(() => {
    setFs(v => {
      const n = !v; const el = rootRef.current;
      if (n && el && document.fullscreenEnabled && !document.fullscreenElement) el.requestFullscreen().catch(() => { /* kiosk layout alone */ });
      if (!n && document.fullscreenElement) document.exitFullscreen().catch(() => { /* ignore */ });
      return n;
    });
    setTimeout(() => screenRef.current?.focus(), 50);
  }, []);

  /* desktop mode: tell the emulator how big the device screen is (in guest pixels) so Windows sees a desktop that fills the view in either orientation.
     Phones: the desktop is scaled so its short side stays about 720 px (a 1080x2400 phone gets a 720x1600 / 1600x720 desktop) — Windows UI remains legible. */
  const applyDesktop = useCallback(() => {
    const rt = rtRef.current; if (!rt) return;
    const el = canvasRef.current ?? screenRef.current ?? rootRef.current;
    if (!deskFit || !el) { rt.setDesktop(0, 0); return; }
    const cw = el.clientWidth, ch = el.clientHeight; if (cw < 200 || ch < 150) return;
    const dpr = Math.min(2, window.devicePixelRatio || 1); const pw = cw * dpr, ph = ch * dpr;
    const scale = touch ? Math.max(1, Math.min(2, Math.round(Math.min(pw, ph) / 720 * 4) / 4)) : 1;
    let dw = pw / scale, dh = ph / scale; const big = Math.max(dw, dh); if (big > 1920) { dw *= 1920 / big; dh *= 1920 / big; }   // same aspect as the view (no bars), long side ≤ 1920
    rt.setDesktop(Math.round(dw), Math.round(dh));
  }, [deskFit, touch]);
  useEffect(() => { applyDesktopRef.current = applyDesktop; }, [applyDesktop]);
  useEffect(() => {
    applyDesktop(); const el = canvasRef.current ?? rootRef.current; if (!el || typeof ResizeObserver === 'undefined') return;
    let t = 0; const ro = new ResizeObserver(() => { clearTimeout(t); t = window.setTimeout(applyDesktop, 250); }); ro.observe(el);
    return () => { ro.disconnect(); clearTimeout(t); };
  }, [applyDesktop, tab, fs]);
  /* the virtual cursor (touchpad mode): guest px → canvas px with the presenter's aspect-fit mapping */
  const placeCursor = useCallback(() => {
    const rt = rtRef.current, p = presenterRef.current, el = cursorRef.current; if (!rt || !p || !el) return;
    const { w, h } = rt.screenSize(); const cw = p.canvas.clientWidth, ch = p.canvas.clientHeight; if (!w || !h || !cw || !ch) return;
    if (cur.current.x < 0) cur.current = { x: w / 2, y: h / 2 };
    if (cur.current.x > w - 1 || cur.current.y > h - 1) cur.current = { x: Math.min(cur.current.x, w - 1), y: Math.min(cur.current.y, h - 1) };   // the desktop shrank (rotation): keep the cursor on it
    const s = Math.min(cw / w, ch / h); const ox = (cw - w * s) / 2, oy = (ch - h * s) / 2;
    el.style.transform = `translate(${ox + cur.current.x * s}px, ${oy + cur.current.y * s}px)`;
  }, []);
  const moveCursor = useCallback((dx: number, dy: number, silent?: boolean) => {   /* silent: place/clamp only (before a button event), no WM_MOUSEMOVE */
    const rt = rtRef.current; if (!rt) return; const { w, h } = rt.screenSize(); if (!w || !h) return;
    if (cur.current.x < 0) cur.current = { x: w / 2, y: h / 2 };
    cur.current = { x: Math.max(0, Math.min(w - 1, cur.current.x + dx)), y: Math.max(0, Math.min(h - 1, cur.current.y + dy)) };
    if (!silent) rt.mouse(cur.current.x, cur.current.y, 0); placeCursor();
  }, [placeCursor]);
  // presenter bound to the canvas
  useEffect(() => {
    const c = canvasRef.current; if (!c) return;
    if (presenterRef.current?.canvas === c) return;   // the canvas is re-created whenever the Screen tab is left and re-entered
    try { const p = new FramePresenter(c); presenterRef.current = p; setGlInfo(`${p.api.toUpperCase()} · ${p.renderer}`); } catch (e) { setGlInfo('WebGL unavailable: ' + String(e)); return; }
    const g = rtRef.current?.lastGlSurface(); if (g) presenterRef.current.setGl(g.canvas, g.x, g.y, g.w, g.h);   // the GL surface outlives the canvas: re-upload it for the new presenter
    const last = rtRef.current?.lastPresented(); if (last) presenterRef.current.present(last.px, last.w, last.h);   // show the final frame of a finished program
  }, [ready, tab]);

  // stats ticker while running
  useEffect(() => {
    const t = setInterval(() => { const rt = rtRef.current; if (rt && (rt.status === 'running' || rt.status === 'waiting-stdin')) { const st = rt.stats(); setStats(st); setDisk({ files: rt.diskFiles, writes: rt.disk.writes, bytes: rt.disk.bytes, queued: rt.disk.queued() }); setPadInfo(rt.pads.describe());
      const now = performance.now(), r = fpsRef.current; if (r.t) setFps(Math.round((st.frames - r.f) * 1000 / Math.max(1, now - r.t))); r.t = now; r.f = st.frames; } }, 500);
    return () => clearInterval(t);
  }, []);

  useEffect(() => { if (consoleRef.current) consoleRef.current.scrollTop = consoleRef.current.scrollHeight; }, [consoleText]);

  const addFiles = useCallback(async (files: FileList | File[]) => {
    const rt = rtRef.current; if (!rt) return;
    const added: ExeEntry[] = [];
    for (const f of Array.from(files)) { const bytes = new Uint8Array(await f.arrayBuffer()); added.push({ id: nextId.current++, name: f.name, bytes, info: rt.inspect(bytes), source: 'file' }); }
    setExes(list => [...list, ...added]); if (added.length) { setSelected(added[added.length - 1].id); setTab('report'); }
  }, []);

  // a whole folder (webkitdirectory) → virtual disk under C:\nebula\ (top-level folder name stripped); .exe files also join the list
  /** files with their relative paths → C:\nebula (mount for a test run) or C:\Program Files\<top> on the persistent disk (+ Start Menu shortcut) */
  const installFiles = useCallback(async (items: { rel: string[]; bytes: () => Promise<Uint8Array>; size: number }[], top: string, install: boolean) => {
    const rt = rtRef.current; if (!rt || !items.length) return;
    const added: ExeEntry[] = []; let n = 0;
    const root = install ? 'C:\\Program Files\\' + top.replace(/[<>:"|?*]/g, '_') : 'C:\\nebula';
    if (install) { rt.vfsMkdir(root); rt.disk.mkdir(root); }
    let done = 0;
    for (const f of items) {
      const parts = f.rel; if (!parts.length) continue; const path = root + '\\' + parts.join('\\');
      const bytes = await f.bytes(); done++;
      if (rt.vfsAdd(path, bytes)) { n++; if (install) rt.disk.put(path, bytes); }
      if (install && (done % 20 === 0 || done === items.length)) setFetching(`installing ${top}: ${done}/${items.length} files`);
      if (/\.exe$/i.test(parts[parts.length - 1])) added.push({ id: nextId.current++, name: parts.join('\\'), bytes, info: rt.inspect(bytes), source: install ? 'disk' : 'file', path, cwd: path.slice(0, path.lastIndexOf('\\')) });
    }
    if (install) {   // a real Start Menu shortcut to the main program (the biggest top-level exe that is not a helper), like an installer would leave
      const mains = added.filter(e => !e.name.includes('\\') && !/unins|setup|install|crash|update|redist/i.test(e.name)).sort((a, b) => b.bytes.length - a.bytes.length);
      const main = mains[0] ?? added.sort((a, b) => b.bytes.length - a.bytes.length)[0];
      if (main?.path) { const lnk = 'C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\' + top + '.lnk'; const b = makeLnk(main.path, main.cwd ?? root, top); rt.vfsAdd(lnk, b); rt.disk.put(lnk, b); }
      setFetching(null); await rt.disk.flush(); rescan(true); setTab('library'); setFiles(rt.vfsList());
      setLog(l => [...l, `installed ${n} file(s) under ${root}\\`]); if (!main) pushToast('Programs', `${top}: no .exe inside — files were copied to ${root}`, 'x64'); return;
    }
    setExes(l => [...l, ...added]); if (added.length) setSelected(added[0].id);
    setFiles(rt.vfsList()); setLog(l => [...l, `mounted ${n} file(s) under C:\\nebula\\`]); setTab('files');
  }, [rescan, pushToast]);
  const addFolder = useCallback(async (list: FileList | File[], install = false) => {
    const arr = Array.from(list); if (!arr.length) return;
    const first = ((arr[0] as File & { webkitRelativePath?: string }).webkitRelativePath || '').split('/').filter(Boolean); const top = first.length > 1 ? first[0] : (arr.length === 1 ? arr[0].name.replace(/\.[^.]+$/, '') : 'Program');
    const items = arr.map(f => { const rel = ((f as File & { webkitRelativePath?: string }).webkitRelativePath || f.name).split('/').filter(Boolean); return { rel: rel.length > 1 ? rel.slice(1) : rel, size: f.size, bytes: async () => new Uint8Array(await f.arrayBuffer()) }; });
    await installFiles(items, top, install);
  }, [installFiles]);
  /** a .zip of a portable program / game folder (the practical unit on Android, which has no folder picker) */
  const installZip = useCallback(async (f: File) => {
    const rt = rtRef.current; if (!rt) return;
    try {
      const zip = new Uint8Array(await f.arrayBuffer()); const entries = zipEntries(zip).filter(e => !e.dir && !/(^|\/)__MACOSX\//.test(e.name));
      if (!entries.length) throw new Error('empty archive');
      // strip one common top-level folder (game/ game/bin/x.exe → bin/x.exe) and name the install after it
      const firsts = new Set(entries.map(e => e.name.split('/')[0])); const common = firsts.size === 1 && entries.every(e => e.name.includes('/')) ? [...firsts][0] : '';
      const top = common || f.name.replace(/\.zip$/i, '');
      const items = entries.map(e => { const parts = e.name.split('/').filter(Boolean); return { rel: common ? parts.slice(1) : parts, size: e.size, bytes: () => zipRead(zip, e) }; }).filter(i => i.rel.length);
      setFetching(`unpacking ${f.name}: ${items.length} files…`);
      await installFiles(items, top, true);
    } catch (e) { setFetching(null); pushToast('Programs', `${f.name}: ${String((e as Error).message ?? e)}`, 'x64'); }
  }, [installFiles, pushToast]);
  const addDataFiles = useCallback(async (list: FileList | File[]) => {
    const rt = rtRef.current; if (!rt) return; let n = 0;
    for (const f of Array.from(list)) { if (rt.vfsAdd('C:\\nebula\\' + f.name, new Uint8Array(await f.arrayBuffer()))) n++; }
    setFiles(rt.vfsList()); setLog(l => [...l, `added ${n} file(s) to C:\\nebula\\`]);
  }, []);
  const downloadFile = useCallback((path: string) => {
    const rt = rtRef.current; if (!rt) return; const data = rt.vfsRead(path); if (!data) return;
    const url = URL.createObjectURL(new Blob([data as BlobPart])); const a = document.createElement('a'); a.href = url; a.download = path.split('\\').pop() || 'file'; a.click(); setTimeout(() => URL.revokeObjectURL(url), 5000);
  }, []);
  const removeFile = useCallback((path: string) => { const rt = rtRef.current; if (!rt) return; rt.vfsRemove(path); setFiles(rt.vfsList()); }, []);
  const sendStdin = useCallback((eof: boolean) => {
    const rt = rtRef.current; if (!rt) return;
    if (eof) { rt.closeStdin(); consoleBuf.current += '^Z\n'; }
    else { const line = stdinLine + '\n'; rt.writeStdin(line); consoleBuf.current = (consoleBuf.current + line).slice(-60000); setStdinLine(''); }
    setConsoleText(consoleBuf.current);
  }, [stdinLine]);

  const current = exes.find(e => e.id === selected) ?? null;
  const running = stats?.status === 'running' || stats?.status === 'waiting-stdin';
  /* phones: a program that opens a window takes the whole screen (game mode) once per run; the user can still leave with the ⤡ button */
  const autoFs = useRef(false);
  useEffect(() => { if (!running) { autoFs.current = false; return; } if (touch && stats?.hasWindow && !fs && !autoFs.current) { autoFs.current = true; setTab('screen'); toggleFullscreen(); } }, [running, stats?.hasWindow, touch, fs, toggleFullscreen]);

  const [fetching, setFetching] = useState<string | null>(null);
  const attempted = useRef({ pack: false, dx: false, dx32: false, dotnet: false });
  const [pack, setPack] = useState<PackState>({ status: 'probing' });
  useEffect(() => { let alive = true; (async () => {
    try { const r = await fetch(PACK_URL + 'manifest.json'); if (!r.ok) throw new Error('HTTP ' + r.status); const man = await r.json() as PackManifest; if (alive) setPack({ status: 'available', manifest: man }); }
    catch (e) { if (alive) setPack({ status: 'unavailable', note: 'not built into this deployment (' + String(e) + ')' }); }
  })(); return () => { alive = false; }; }, []);
  /* Components log: the result of every runtime-pack install (VC++, DirectX, .NET), kept in localStorage and shown in the Components tab */
  const [compLog, setCompLog] = useState<CompLogEntry[]>(loadCompLog);
  const logComp = useCallback((pack: string, ok: boolean, text: string) => {
    setCompLog(l => { const n = [...l, { t: Date.now(), pack, ok, text }].slice(-200); try { localStorage.setItem(COMP_LOG_KEY, JSON.stringify(n)); } catch { /* quota */ } return n; });
    setLog(l => [...l, `${pack}: ${text}`]);
  }, []);
  const installPack = useCallback(async (rt: X64Runtime): Promise<boolean> => {
    if (pack.status === 'installed') return true;
    if (pack.status !== 'available' || !pack.manifest) { setLog(l => [...l, 'RUNTIME PACK: ' + (pack.note ?? pack.status)]); return false; }
    attempted.current.pack = true;
    const man = pack.manifest; let done = 0, bytes = 0;
    setPack({ status: 'installing', manifest: man, done: 0 }); setFetching(`installing runtime pack (${man.files.length} DLLs)…`);
    try {
      for (const f of man.files) {
        const r = await fetch(PACK_URL + f.name); if (!r.ok) throw new Error(`${f.name}: HTTP ${r.status}`);
        const b = new Uint8Array(await r.arrayBuffer()); if (b.length !== f.size) throw new Error(`${f.name}: unexpected size`); bytes += b.length; done++;
        if (!rt.vfsAddPersistent('C:\\Windows\\System32\\' + f.name, b)) throw new Error(`${f.name}: cannot persist file`);
        setPack({ status: 'installing', manifest: man, done }); setFetching(`installing runtime pack: ${done}/${man.files.length}, ${(bytes / 1048576).toFixed(1)} MB`);
      }
      await rt.disk.flushChecked();
    } catch (e) { setFetching(null); setPack({ status: 'available', manifest: man, note: String(e) }); logComp('Visual C++ 2015–2022 x64', false, `failed after ${done}/${man.files.length} DLLs: ${String(e)}`); return false; }
    setFetching(null); setPack({ status: 'installed', manifest: man, done }); setFiles(rt.vfsList());
    logComp('Visual C++ 2015–2022 x64', true, `installed: ${done} DLLs, ${(bytes / 1048576).toFixed(1)} MB → C:\\Windows\\System32 (${man.pack})`);
    return true;
  }, [pack, logComp]);
  const [dotnet, setDotnet] = useState<DotnetState>({ status: 'probing' });
  const [dx, setDx] = useState<DotnetState>({ status: 'probing' });
  const [dx32, setDx32] = useState<DotnetState>({ status: 'probing' });
  useEffect(() => { let alive = true; const probe = async (url: string, set: (st: DotnetState) => void) => {
    try { const r = await fetch(url + 'manifest.json'); if (!r.ok) throw new Error('HTTP ' + r.status); const man = await r.json() as DotnetManifest; if (alive) set({ status: 'available', manifest: man }); }
    catch (e) { if (alive) set({ status: 'unavailable', note: 'not built into this deployment (' + String(e) + ')' }); } };
  void probe(DOTNET_URL, setDotnet); void probe(DX_URL, setDx); void probe(DX32_URL, setDx32); return () => { alive = false; }; }, []);
  /* shared installer for the per-file-gzip packs (.NET, DirectX): parallel fetch → inflate → virtual disk, one memory.grow up front */
  const installGz = useCallback(async (rt: X64Runtime, pk: GzPack, st: DotnetState, set: (v: DotnetState) => void): Promise<boolean> => {
    if (st.status === 'installed') return true;
    if (st.status !== 'available' || !st.manifest) { setLog(l => [...l, pk.label.toUpperCase() + ': ' + (st.note ?? st.status)]); return false; }
    attempted.current[pk.key] = true;
    const man = st.manifest; let done = 0, bytes = 0; const t0 = performance.now();
    set({ status: 'installing', manifest: man, done: 0 }); setFetching(`installing ${pk.label} (${man.files.length} files)…`); setTab('console');
    try {
      // the files stay on the page side (lazy nodes); only the DLLs a program actually loads enter wasm memory
      const queue = man.files.slice(); const PAR = 6; const inflight = new Set<string>(); let failed = false;
      const label = () => `installing ${pk.label}: ${done}/${man.files.length}, ${(bytes / 1048576).toFixed(1)} MB` + (done + inflight.size >= man.files.length ? ` — waiting for ${[...inflight].join(', ')}` : '');
      const worker = async () => { for (;;) { const f = queue.shift(); if (!f || failed) return; inflight.add(f.gz.slice(f.gz.lastIndexOf('/') + 1));
        let b: Uint8Array;
        try { b = await fetchGz(pk.url + f.gz, 0, pk.url + f.path.replace(/\\/g, '/')); } catch (e) { failed = true; throw e; }
        if (failed) return;   // another worker failed meanwhile: do not touch the state again (it already says 'available' + the error)
        if (b.length !== f.size) { failed = true; throw new Error(`${f.path}: ${b.length} bytes, expected ${f.size}`); }
        if (!rt.vfsAddPersistent(pk.dir + f.path, b)) { failed = true; throw new Error(`${f.path}: virtual disk refused the file`); }
        bytes += b.length; done++; inflight.delete(f.gz.slice(f.gz.lastIndexOf('/') + 1));
        if (done % 50 === 0) setLog(l => [...l, `${pk.label}: ${done}/${man.files.length} files, ${(bytes / 1048576).toFixed(1)} MB in ${((performance.now() - t0) / 1000).toFixed(1)} s`]);
        set({ status: 'installing', manifest: man, done }); setFetching(label()); } };
      await Promise.all(Array.from({ length: PAR }, worker));
      await rt.disk.flushChecked();
    } catch (e) { setFetching(null); set({ status: 'available', manifest: man, note: String(e) }); logComp(pk.label, false, `failed after ${done}/${man.files.length} files: ${String(e)}`); return false; }
    setFetching(null); set({ status: 'installed', manifest: man, done }); setFiles(rt.vfsList());
    logComp(pk.label, true, `installed: ${done} files, ${(bytes / 1048576).toFixed(1)} MB → ${pk.dir} in ${((performance.now() - t0) / 1000).toFixed(1)} s (${man.pack})`);
    return true;
  }, [logComp]);
  /* ---- Visual C++ redistributables 2005…2022, x86 and x64, from /x64/sys32/packs.json ---- */
  const [vcPacks, setVcPacks] = useState<VcPack[]>([]);
  const [vcState, setVcState] = useState<Record<string, VcState>>({});
  const vcInstalled = useRef<Set<string>>(new Set());
  const vcAttempted = useRef<Set<string>>(new Set());
  useEffect(() => { let alive = true; (async () => {
    try { const r = await fetch(PACK_URL + 'packs.json'); if (!r.ok) throw new Error('HTTP ' + r.status); const idx = await r.json() as { packs: VcPack[] };
      if (alive && Array.isArray(idx.packs)) setVcPacks(idx.packs.filter(p => p.id !== 'vc2015-x64' && Array.isArray(p.files) && p.files.length > 0)); }
    catch { /* an older deployment without packs.json: only the flat x64 pack exists */ }
  })(); return () => { alive = false; }; }, []);
  const installVcPack = useCallback(async (rt: X64Runtime, id: string): Promise<boolean> => {
    if (vcInstalled.current.has(id)) return true;
    const pk = vcPacks.find(p => p.id === id);
    if (!pk) { logComp(id, false, 'this pack is not built into this deployment'); return false; }
    vcAttempted.current.add(id);
    setVcState(st => ({ ...st, [id]: { status: 'installing', done: 0 } })); setFetching(`Installing ${pk.label}…`);
    const t0 = performance.now(); let done = 0, bytes = 0;
    try {
      for (const file of pk.files) {
        if (!/^[a-z0-9_.-]+\.dll$/i.test(file.name)) throw new Error(`invalid DLL name: ${file.name}`);
        const r = await fetch(PACK_URL + pk.base + file.name); if (!r.ok) throw new Error(`${file.name}: HTTP ${r.status}`);
        const b = new Uint8Array(await r.arrayBuffer()); if (b.length !== file.size) throw new Error(`${file.name}: ${b.length} bytes, expected ${file.size}`);
        if (!rt.vfsAddPersistent(`${pk.guest_dir}\\${file.name}`, b)) throw new Error(`${file.name}: virtual disk refused the file`);
        bytes += b.length; done++; setVcState(st => ({ ...st, [id]: { status: 'installing', done } })); setFetching(`Installing ${pk.label}: ${done}/${pk.files.length}, ${(bytes / 1048576).toFixed(1)} MB`);
      }
      await rt.disk.flushChecked();
      vcInstalled.current.add(id); setVcState(st => ({ ...st, [id]: { status: 'installed', done } })); setFetching(null); setFiles(rt.vfsList());
      logComp(pk.label, true, `installed: ${done} DLLs, ${(bytes / 1048576).toFixed(1)} MB → ${pk.guest_dir} in ${((performance.now() - t0) / 1000).toFixed(1)} s (${pk.files.map(f => f.name.replace(/\.dll$/i, '')).join(', ')})`);
      return true;
    } catch (e) {
      setVcState(st => ({ ...st, [id]: { status: 'error', done, note: String(e) } })); setFetching(null);
      logComp(pk.label, false, `failed after ${done}/${pk.files.length} files: ${String(e)}`);
      pushToast('Programs', `${pk.label} could not be installed: ${String(e)}`, 'x64'); return false;
    }
  }, [vcPacks, logComp, pushToast]);
  /* before a program starts: every VC++ family it imports, for its own bitness (the x64 2015–2022 pack is handled by installPack) */
  ensureVC10Ref.current = async (rt: X64Runtime, entry: ExeEntry) => {
    for (const id of vcPacksFor(entry.info.dlls, entry.info.pe32plus)) {
      if (id === 'vc2015-x64') continue;
      if (!vcPacks.some(p => p.id === id)) { setLog(l => [...l, `${id}: the runtime pack is not built into this deployment; the program's own copies of the DLLs (next to the exe) are used if present`]); continue; }
      if (!(await installVcPack(rt, id))) return false;
    }
    return true;
  };
  // The runtime's cache is populated from IndexedDB during init. Only advertise
  // a restored package if every manifest entry exists at the expected size.
  useEffect(() => {
    const rt = rtRef.current; if (ready !== 'ready' || !rt) return;
    const complete = (items: { path: string; size: number }[]) => items.length > 0 && items.every(f => rt.cache.get(diskKey(f.path))?.length === f.size);
    if (!attempted.current.pack && pack.status === 'available' && pack.manifest && complete(pack.manifest.files.map(f => ({ path: 'C:\\Windows\\System32\\' + f.name, size: f.size })))) setPack({ ...pack, status: 'installed', done: pack.manifest.files.length });
    if (!attempted.current.dx && dx.status === 'available' && dx.manifest && complete(dx.manifest.files.map(f => ({ path: DX_DIR + f.path, size: f.size })))) setDx({ ...dx, status: 'installed', done: dx.manifest.files.length });
    if (!attempted.current.dx32 && dx32.status === 'available' && dx32.manifest && complete(dx32.manifest.files.map(f => ({ path: DX32_DIR + f.path, size: f.size })))) setDx32({ ...dx32, status: 'installed', done: dx32.manifest.files.length });
    if (!attempted.current.dotnet && dotnet.status === 'available' && dotnet.manifest && complete(dotnet.manifest.files.map(f => ({ path: DOTNET_DIR + f.path, size: f.size })))) setDotnet({ ...dotnet, status: 'installed', done: dotnet.manifest.files.length });
    for (const pk of vcPacks) if (!vcAttempted.current.has(pk.id) && !vcInstalled.current.has(pk.id) && complete(pk.files.map(f => ({ path: `${pk.guest_dir}\\${f.name}`, size: f.size })))) { vcInstalled.current.add(pk.id); setVcState(st => ({ ...st, [pk.id]: { status: 'installed', done: pk.files.length } })); }
  }, [ready, pack, dx, dx32, dotnet, vcPacks]);
  const installDotnet = useCallback((rt: X64Runtime) => installGz(rt, GZ_PACKS.dotnet, dotnet, setDotnet), [installGz, dotnet]);
  const installDx = useCallback((rt: X64Runtime) => installGz(rt, GZ_PACKS.dx, dx, setDx), [installGz, dx]);
  const installDx32 = useCallback((rt: X64Runtime) => installGz(rt, GZ_PACKS.dx32, dx32, setDx32), [installGz, dx32]);
  const run = useCallback(async (entry?: ExeEntry, argv?: string) => {
    const rt = rtRef.current; const current = entry ?? curRef.current; if (!rt || !current) return;
    const args = argv ?? argsRef.current;
    consoleBuf.current = ''; setConsoleText(''); setLog([]); setFrames(0); setWaitingStdin(false);
    if (current.pending && current.pending.length) {   // heavy runtime: fetch its companion files on first Run
      const urls = current.pending; let done = 0, bytes = 0;
      try {
        setFetching(`downloading ${current.name} runtime (${urls.length} files)…`); setTab('console');
        for (const u of urls) {
          const r = await fetch(u); if (!r.ok) throw new Error(`${u}: HTTP ${r.status}`);
          const b = new Uint8Array(await r.arrayBuffer()); bytes += b.length; done++;
          rt.vfsAdd('C:\\nebula\\' + u.slice(u.lastIndexOf('/') + 1), b);
          setFetching(`downloading ${current.name} runtime: ${done}/${urls.length} files, ${(bytes / 1048576).toFixed(1)} MB`);
        }
        setExes(list => list.map(e => e.id === current.id ? { ...e, pending: undefined } : e));
        setLog(l => [...l, `runtime files loaded: ${done} files, ${(bytes / 1048576).toFixed(1)} MB → C:\\nebula\\`]);
      } catch (e) { setFetching(null); setLog(l => [...l, 'DOWNLOAD: ' + String(e)]); setTab('report'); return; }
      setFetching(null);
    }
    if (!await ensureVC10Ref.current(rt, current)) { setTab('report'); return; }
    const sample = current.source === 'sample' ? SAMPLES.find(x => x.name === current.name) : undefined;
    if (current.info.pe32plus && pack.status !== 'installed' && (sample?.needsPack || exeWantsPack(current.info.dlls, pack.manifest))) await installPack(rt);   // the program imports a vendor-runtime DLL we ship
    if (current.info.pe32plus !== false && dx.status !== 'installed' && (sample?.needsDx || exeWantsDx(current.info.dlls, dx.manifest))) await installDx(rt);
    if (current.info.pe32plus === false && dx32.status !== 'installed' && exeWantsDx(current.info.dlls, dx32.manifest)) await installDx32(rt);   // a 32-bit program: the x86 DLLs into SysWOW64   // the program imports a DirectX DLL we ship (D3DX, D3DCompiler …)
    if (current.info.dotnet && dotnet.status !== 'installed' && !(await installDotnet(rt))) {   // managed code needs the real runtime
      setLog(l => [...l, `${current.name} is a .NET program (${current.info.dotnet}): the .NET runtime pack could not be installed, so it cannot run here`]); setStats(rt.stats()); setTab('report'); return;
    }
    const base = current.name.split('\\').pop() || current.name;
    const a0 = current.path ?? base; const argv0 = a0.includes(' ') ? `"${a0}"` : a0;   // a name with spaces is quoted as argv[0] (an opened "Setup (1).exe" used to be split into two parameters)
    applyDesktopRef.current();
    const err = rt.start(current.bytes, current.path ?? base, args.trim() ? `${argv0} ${args.trim()}` : argv0, current.cwd);
    if (err) { setLog(l => [...l, 'LOADER: ' + err]); setStats(rt.stats()); setTab('report'); return; }
    cur.current = { x: -1, y: -1 };
    if (rt.cdsDumpPass()) setFetching('Java: one-time preparation — building the class-data archive for this program (a few minutes on a phone); it starts by itself afterwards');
    setStats(rt.stats()); setFiles(rt.vfsList()); setFrames(0);
    setTab(current.info.subsystem === 'GUI' && !rt.cdsDumpPass() ? 'screen' : 'console'); autoTabRef.current = true;
    setTimeout(() => screenRef.current?.focus(), 50);
  }, [pack, installPack, dotnet, installDotnet, dx, installDx, dx32, installDx32]);
  const curRef = useRef<ExeEntry | null>(null); curRef.current = current;
  const runRef = useRef(run); runRef.current = run; const relaunches = useRef(0);
  const argsRef = useRef(''); argsRef.current = args;

  /** the library's Play button: the installed exe from the persistent disk, started in its own folder with the shortcut's arguments */
  const launchItem = useCallback((it: LibraryItem) => {
    const rt = rtRef.current; if (!rt) return; const bytes = rt.vfsRead(it.exe); if (!bytes) { pushToast('Programs', `${it.exe} is missing from the disk`, 'x64'); rescan(false); return; }
    const entry: ExeEntry = { id: nextId.current++, name: it.exe.slice(it.exe.lastIndexOf('\\') + 1), bytes, info: rt.inspect(bytes), source: 'disk', path: it.exe, cwd: it.workDir || undefined, kind: 'program' };
    setExes(l => [...l.filter(e => !(e.source === 'disk' && e.path?.toLowerCase() === it.exe.toLowerCase())), entry]); setSelected(entry.id); setArgs(it.args);
    updatePrefs(it.id, { lastRun: Date.now(), runs: (prefs[it.id]?.runs ?? 0) + 1 });
    void run(entry, it.args);
  }, [run, pushToast, rescan, updatePrefs, prefs]);
  /** Open / Install…: a setup program runs at once and shows its own wizard; a plain program runs at once too (“Add to library” keeps it) */
  const openProgram = useCallback(async (list: FileList | File[]) => {
    const rt = rtRef.current; if (!rt) return; const f = Array.from(list)[0]; if (!f) return;
    if (/\.zip$/i.test(f.name)) { await installZip(f); return; }
    const bytes = new Uint8Array(await f.arrayBuffer()); const info = rt.inspect(bytes);
    if (!info.ok) { pushToast('Programs', `${f.name}: not a Windows program (${info.error ?? 'not a PE file'})`, 'x64'); return; }
    const kind = classifyExe(f.name, bytes);
    const entry: ExeEntry = { id: nextId.current++, name: f.name, bytes, info, source: 'file', kind };
    setExes(l => [...l, entry]); setSelected(entry.id); setArgs('');
    void run(entry, '');
  }, [run, pushToast, installZip]);
  /** keep a program the user opened: copy it to C:\Program Files\<name>\ on the persistent disk and create its Start Menu shortcut */
  const addToLibrary = useCallback(async (e: ExeEntry) => {
    const rt = rtRef.current; if (!rt) return;
    const base = e.name.split('\\').pop() || e.name; const title = base.replace(/\.exe$/i, '');
    const dir = 'C:\\Program Files\\' + title.replace(/[<>:"|?*]/g, '_'); const exe = dir + '\\' + base;
    rt.vfsMkdir(dir); rt.disk.mkdir(dir); rt.vfsAdd(exe, e.bytes); rt.disk.put(exe, e.bytes);
    const lnk = 'C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\' + title + '.lnk'; const b = makeLnk(exe, dir, title); rt.vfsAdd(lnk, b); rt.disk.put(lnk, b);
    await rt.disk.flush(); rescan(true); setTab('library');
  }, [rescan]);
  /** Uninstall: the program's own uninstaller when the registry names one, otherwise its folder and shortcut are deleted from the disk */
  const uninstallItem = useCallback((it: LibraryItem) => {
    const rt = rtRef.current; if (!rt) return; setMenuFor(null);
    if (it.uninstall) {
      const m = /^"([^"]+)"\s*(.*)$/.exec(it.uninstall.trim()); const exe = m ? m[1] : it.uninstall.trim().split(' ')[0]; const a = m ? m[2] : it.uninstall.trim().slice(exe.length).trim();
      const bytes = rt.vfsRead(exe); if (bytes) { const entry: ExeEntry = { id: nextId.current++, name: exe.slice(exe.lastIndexOf('\\') + 1), bytes, info: rt.inspect(bytes), source: 'disk', path: exe, cwd: exe.slice(0, exe.lastIndexOf('\\')) }; setExes(l => [...l, entry]); setSelected(entry.id); void run(entry, a); return; }
    }
    const dir = it.exe.slice(0, it.exe.lastIndexOf('\\')).toLowerCase();
    if (/^c:\\program files( \(x86\))?\\[^\\]+$/.test(dir) || /^c:\\users\\user\\appdata\\local\\programs\\[^\\]+$/.test(dir)) {
      const all = rt.vfsList().filter(f => f.path.toLowerCase().startsWith(dir + '\\') || f.path.toLowerCase() === dir).sort((a, b) => b.path.length - a.path.length);
      for (const f of all) rt.vfsRemove(f.path);
    } else rt.vfsRemove(it.exe);
    if (it.shortcut) rt.vfsRemove(it.shortcut);
    setPrefs(p => { const n = { ...p }; delete n[it.id]; savePrefs(n); return n; });
    void rt.disk.flush().then(() => { rescan(false); setFiles(rt.vfsList()); pushToast('Programs', `${it.name} removed`, 'x64'); });
  }, [run, rescan, pushToast]);
  const setCover = useCallback(async (id: string, file: File | null) => {
    setMenuFor(null); if (!file) { updatePrefs(id, { cover: undefined }); return; }
    const url = await new Promise<string>((res, rej) => { const im = new Image(); const u = URL.createObjectURL(file); im.onload = () => { const c = document.createElement('canvas'); const s = Math.min(1, 512 / Math.max(im.width, im.height)); c.width = Math.round(im.width * s); c.height = Math.round(im.height * s); c.getContext('2d')!.drawImage(im, 0, 0, c.width, c.height); URL.revokeObjectURL(u); res(c.toDataURL('image/jpeg', 0.85)); }; im.onerror = () => { URL.revokeObjectURL(u); rej(new Error('bad image')); }; im.src = u; }).catch(() => '');
    if (url) updatePrefs(id, { cover: url });
  }, [updatePrefs]);
  const stop = useCallback(() => { const rt = rtRef.current; if (!rt) return; rt.stop(); void rt.syncDisk().then(notifyDiskChange); setStats(rt.stats()); setApiStats(rt.apiStats()); setJitInfo(rt.jitStats()); setFiles(rt.vfsList()); }, []);   // a manual stop shows the same statistics as a normal exit (JIT regions, Win32 calls)

  // keyboard → guest
  const guestKey = useCallback((e: React.KeyboardEvent, down: boolean) => {
    const rt = rtRef.current; if (!rt || rt.status !== 'running') return;
    const vk = vkFromEvent(e.nativeEvent); if (!vk) return;
    rt.key(vk, down); e.preventDefault(); e.stopPropagation();
  }, []);
  const [sticky, setSticky] = useState<number[]>([]);   // modifier VKs held down by the touch bar until the next key
  const stickyRef = useRef<number[]>([]);
  const releaseSticky = useCallback(() => { const rt = rtRef.current; for (const vk of stickyRef.current) rt?.key(vk, false); stickyRef.current = []; setSticky([]); }, []);
  const touchKey = useCallback((vk: number, down: boolean, isSticky?: boolean) => {
    const rt = rtRef.current; if (!rt) return;
    if (isSticky) { if (!down) return; if (stickyRef.current.includes(vk)) { rt.key(vk, false); stickyRef.current = stickyRef.current.filter(v => v !== vk); } else { rt.key(vk, true); stickyRef.current = [...stickyRef.current, vk]; } setSticky([...stickyRef.current]); return; }
    rt.key(vk, down); if (!down) releaseSticky();
  }, [releaseSticky]);
  const toGuest = useCallback((clientX: number, clientY: number) => {
    const rt = rtRef.current, p = presenterRef.current; if (!rt || !p || rt.status !== 'running') return null;
    const r = p.canvas.getBoundingClientRect(); const { w, h } = rt.screenSize(); return p.toGuest(clientX - r.left, clientY - r.top, w, h);
  }, []);
  const imeRef = useRef<HTMLTextAreaElement | null>(null);
  const imePrev = useRef(IME_SENTINEL);
  const [imeOpen, setImeOpen] = useState(false);
  const focusGuest = useCallback(() => { if (imeOpen && imeRef.current) imeRef.current.focus({ preventScroll: true }); else screenRef.current?.focus(); }, [imeOpen]);
  const pt = useRef<PadTouch | null>(null); const lastTap = useRef(0);
  const padPointer = useCallback((e: React.PointerEvent, kind: 'down' | 'move' | 'up' | 'cancel', rt: X64Runtime) => {
    const t = pt.current; const clearT = () => { if (t?.timer) { clearTimeout(t.timer); t.timer = 0; } };
    if (kind === 'down') {
      if (t && !t.done) { t.fingers++; t.lastY2 = e.clientY; t.done = true; clearT(); return; }   // a second finger: two-finger scroll (no click on release)
      e.currentTarget.setPointerCapture(e.pointerId); focusGuest();
      const now = performance.now();
      const st: PadTouch = { id: e.pointerId, sx: e.clientX, sy: e.clientY, lx: e.clientX, ly: e.clientY, t0: now, moved: false, drag: false, done: false, timer: 0, fingers: 1, lastY2: 0 };
      if (now - lastTap.current < 300) { st.drag = true; lastTap.current = 0; moveCursor(0, 0, true); rt.mouse(cur.current.x, cur.current.y, 1); }   // tap, then touch again and move = drag (laptop touchpad)
      else st.timer = window.setTimeout(() => { const c = pt.current; if (!c || c !== st || c.moved || c.drag || c.done) return; c.done = true; c.timer = 0; moveCursor(0, 0, true); rt.mouse(cur.current.x, cur.current.y, 3); rt.mouse(cur.current.x, cur.current.y, 4); if (navigator.vibrate) navigator.vibrate(15); }, 550);   // hold = right click
      pt.current = st; return;
    }
    if (!t) return;
    if (kind === 'move') {
      if (t.fingers >= 2 && e.pointerId !== t.id) { const dy = e.clientY - t.lastY2; if (Math.abs(dy) >= 24) { rt.mouse(dy < 0 ? -120 : 120, 0, 7); t.lastY2 = e.clientY; } return; }
      if (e.pointerId !== t.id || t.fingers >= 2 || (t.done && !t.drag)) return;
      const dx = e.clientX - t.lx, dy = e.clientY - t.ly;
      if (!t.moved) { if (Math.hypot(e.clientX - t.sx, e.clientY - t.sy) < 6) return; t.moved = true; clearT(); }
      t.lx = e.clientX; t.ly = e.clientY;
      const { w, h } = rt.screenSize(); const p = presenterRef.current; const cw = p?.canvas.clientWidth || 1, ch = p?.canvas.clientHeight || 1; const s = (w && h ? Math.min(cw / w, ch / h) : 1) || 1;
      const v = Math.hypot(dx, dy); const gain = PAD_GAIN * (v > 14 ? 1.6 : v > 6 ? 1.25 : 1);   // light acceleration: slow strokes are precise, fast ones cross the screen
      moveCursor(dx / s * gain, dy / s * gain); return;
    }
    if (e.pointerId !== t.id) { if (t.fingers > 0) t.fingers--; return; }
    clearT(); pt.current = null;
    if (t.drag) { rt.mouse(cur.current.x, cur.current.y, 2); return; }
    if (t.done || kind === 'cancel') return;
    if (!t.moved && performance.now() - t.t0 < 350) { moveCursor(0, 0, true); rt.mouse(cur.current.x, cur.current.y, 1); rt.mouse(cur.current.x, cur.current.y, 2); lastTap.current = performance.now(); }   // a tap = left click at the cursor
  }, [focusGuest, moveCursor]);
  /* on-screen mouse buttons act at the virtual cursor (touchpad) or the last touched spot */
  const tmButton = useCallback((b: MouseBtn, down: boolean) => {
    const rt = rtRef.current; if (!rt || rt.status !== 'running') return; moveCursor(0, 0, true);
    const base = b === 'l' ? 1 : b === 'r' ? 3 : 5; rt.mouse(cur.current.x, cur.current.y, down ? base : base + 1); if (down) focusGuest();
  }, [focusGuest, moveCursor]);
  const tmWheel = useCallback((dir: -1 | 1) => { const rt = rtRef.current; if (rt && rt.status === 'running') rt.mouse(dir > 0 ? 120 : -120, 0, 7); }, []);
  const ts = useRef<TouchState | null>(null);
  const clearHold = () => { const t = ts.current; if (t?.timer) { clearTimeout(t.timer); t.timer = 0; } };
  const guestPointer = useCallback((e: React.PointerEvent, kind: 'down' | 'move' | 'up' | 'cancel') => {
    const rt = rtRef.current; if (!rt || rt.status !== 'running') return;
    if (e.pointerType !== 'touch') {   // mouse / pen: real buttons
      const g = toGuest(e.clientX, e.clientY); if (!g) return;
      cur.current = { x: g.x, y: g.y };
      if (kind === 'down') { e.currentTarget.setPointerCapture(e.pointerId); rt.mouse(g.x, g.y, e.button === 2 ? 3 : e.button === 1 ? 5 : 1); focusGuest(); }
      else if (kind === 'up') rt.mouse(g.x, g.y, e.button === 2 ? 4 : e.button === 1 ? 6 : 2);
      else if (kind === 'move') rt.mouse(g.x, g.y, 0);
      return;
    }
    e.preventDefault();
    if (padMode) { padPointer(e, kind, rt); return; }
    const t = ts.current;
    if (kind === 'down') {
      if (t && !t.done) {   // a second finger while the first is held: right click (or the start of a two-finger scroll)
        t.fingers++; t.lastY2 = e.clientY; clearHold();
        if (!t.pressed) { rt.mouse(t.gx, t.gy, 3); rt.mouse(t.gx, t.gy, 4); t.done = true; }
        return;
      }
      const g = toGuest(e.clientX, e.clientY); if (!g) return;
      e.currentTarget.setPointerCapture(e.pointerId); focusGuest();
      rt.mouse(g.x, g.y, 0); cur.current = { x: g.x, y: g.y };
      const st: TouchState = { id: e.pointerId, sx: e.clientX, sy: e.clientY, gx: g.x, gy: g.y, t0: performance.now(), pressed: false, done: false, timer: 0, fingers: 1, lastY2: 0 };
      st.timer = window.setTimeout(() => { const c = ts.current; if (!c || c !== st || c.pressed || c.done) return; c.done = true; c.timer = 0; rt.mouse(c.gx, c.gy, 3); rt.mouse(c.gx, c.gy, 4); if (navigator.vibrate) navigator.vibrate(15); }, 550);
      ts.current = st; return;
    }
    if (!t) return;
    if (kind === 'move') {
      if (t.fingers >= 2 && e.pointerId !== t.id) {   // two-finger vertical drag → wheel notches
        const dy = e.clientY - t.lastY2; if (Math.abs(dy) >= 24) { rt.mouse(dy < 0 ? -120 : 120, 0, 7); t.lastY2 = e.clientY; } return;
      }
      if (e.pointerId !== t.id || t.done) return;
      const g = toGuest(e.clientX, e.clientY); if (!g) return;
      if (!t.pressed) { if (Math.hypot(e.clientX - t.sx, e.clientY - t.sy) < 8) return; clearHold(); rt.mouse(t.gx, t.gy, 1); t.pressed = true; }   // a drag: press where the finger landed
      rt.mouse(g.x, g.y, 0); return;
    }
    if (e.pointerId !== t.id) { if (t.fingers > 0) t.fingers--; return; }
    clearHold(); ts.current = null;
    if (t.done) return;
    const g = toGuest(e.clientX, e.clientY) ?? { x: t.gx, y: t.gy };
    if (kind === 'cancel') { if (t.pressed) rt.mouse(g.x, g.y, 2); return; }
    if (t.pressed) rt.mouse(g.x, g.y, 2); else { rt.mouse(t.gx, t.gy, 1); rt.mouse(t.gx, t.gy, 2); }   // a tap: click where the finger landed
  }, [toGuest, focusGuest, padMode, padPointer]);
  useEffect(() => { placeCursor(); }, [placeCursor, frames, padMode, tab, fs, deskFit, stats?.hasWindow, running]);
  const guestWheel = useCallback((e: React.WheelEvent) => { const rt = rtRef.current; if (!rt || rt.status !== 'running') return; const n = e.deltaMode === 1 ? e.deltaY * 40 : e.deltaY; if (n) rt.mouse(n < 0 ? 120 : -120, 0, 7); }, []);
  /* the IME field: whatever the soft keyboard inserts is diffed against the previous value and sent as text; deletions become Backspace */
  const imeInput = useCallback(() => {
    const el = imeRef.current, rt = rtRef.current; if (!el || !rt) return; const v = el.value, p = imePrev.current;
    let k = 0; while (k < v.length && k < p.length && v[k] === p[k]) k++;
    let del = p.length - k; let ins = v.slice(k);
    if (del > 0 && ins.length > 0) { let tail = 0; while (tail < del && tail < ins.length && p[p.length - 1 - tail] === ins[ins.length - 1 - tail]) tail++; del -= tail; ins = ins.slice(0, ins.length - tail); }
    for (let i = 0; i < del; i++) { rt.key(0x08, true); rt.key(0x08, false); }
    if (ins) { rt.text(ins); releaseSticky(); }
    imePrev.current = v;
    const composing = (el as unknown as { isComposing?: boolean }).isComposing;
    if (!composing && (v.length > 400 || v.length < IME_SENTINEL.length)) { el.value = IME_SENTINEL; imePrev.current = IME_SENTINEL; el.setSelectionRange(IME_SENTINEL.length, IME_SENTINEL.length); }
  }, [releaseSticky]);
  const imeKey = useCallback((e: React.KeyboardEvent<HTMLTextAreaElement>, down: boolean) => {
    e.stopPropagation();   // never let the screen's key handler see these: printable keys reach the guest through the input event
    if (e.nativeEvent.isComposing || e.keyCode === 229) return;   // the soft keyboard composes; the input event carries the text
    const k = e.key; const printable = k.length === 1 && !e.ctrlKey && !e.altKey && !e.metaKey;
    if (printable || k === 'Backspace' || k === 'Process' || k === 'Unidentified') return;   // reaches us through the input event (Backspace is a deletion there)
    if (k === 'Enter' && !e.ctrlKey && !e.altKey && !e.shiftKey) { if (down) { imePrev.current = (e.currentTarget.value || IME_SENTINEL); } }
    guestKey(e as unknown as React.KeyboardEvent, down); e.preventDefault();
  }, [guestKey]);
  const toggleIme = useCallback(() => { setImeOpen(o => { const n = !o; setTimeout(() => { if (n && imeRef.current) { imeRef.current.value = IME_SENTINEL; imePrev.current = IME_SENTINEL; imeRef.current.focus({ preventScroll: true }); imeRef.current.setSelectionRange(IME_SENTINEL.length, IME_SENTINEL.length); } else screenRef.current?.focus(); }, 0); return n; }); }, []);
  useEffect(() => { if (!running) { setImeOpen(false); ts.current = null; } }, [running]);

  const V = (v?: string) => VERDICT[v ?? 'invalid'] ?? VERDICT.invalid;

  return (
    <div ref={rootRef} className={`flex h-full flex-col bg-[#f3f3f3] text-[12px] text-[#1b1b1b] ${fs ? 'fixed inset-0 z-[500]' : ''}`} data-x64-status={stats?.status ?? 'idle'} data-x64-fullscreen={fs ? '1' : undefined}>
      {fs && <div className="absolute right-2 top-2 z-[600] flex gap-2" data-x64-fullscreen-controls>
        <button className="rounded bg-red-700 px-3 py-2 font-semibold text-white shadow" onClick={() => { stop(); toggleFullscreen(); setTab('report'); }}>Stop</button>
        <button className="rounded bg-slate-800 px-3 py-2 font-semibold text-white shadow" onClick={toggleFullscreen}>Exit fullscreen</button>
      </div>}
      {/* toolbar (hidden in fullscreen / game mode) */}
      {!fs && <div className="flex h-11 shrink-0 items-center gap-2 overflow-x-auto whitespace-nowrap border-b border-slate-200 bg-white px-3 [scrollbar-width:none]">   {/* phones: one scrollable row instead of wrapped, overlapping buttons */}
        <Cpu size={16} className="shrink-0 text-indigo-600" />
        <span className="shrink-0 font-semibold">Programs</span>
        <span className="hidden text-[10px] text-slate-400 sm:inline">Windows x86-64 + x86 · wasm64 + WebGL</span>
        <label className="ml-3 flex cursor-pointer items-center gap-1 rounded bg-indigo-600 px-2 py-1 font-semibold text-white hover:bg-indigo-700" title="Open a Windows program or installer from this device: it starts right away, with its own windows">
          <FolderOpen size={13} /> Open / Install…
          <input type="file" accept=".exe,.zip,application/x-msdownload,application/zip,application/octet-stream" className="hidden" data-x64-open onChange={e => { if (e.target.files) void openProgram(e.target.files); e.target.value = ''; }} />
        </label>
        <label className="flex cursor-pointer items-center gap-1 rounded border border-slate-300 bg-white px-2 py-1 hover:bg-slate-50" title="Install a portable program or game folder: copied to C:\\Program Files\\<folder> on the persistent disk, with a Start Menu shortcut">
          <FolderInput size={13} /> Install folder…
          <input type="file" multiple className="hidden" data-x64-install-folder {...({ webkitdirectory: '', directory: '' } as Record<string, string>)} onChange={e => { if (e.target.files) void addFolder(e.target.files, true); e.target.value = ''; }} />
        </label>
        {showTests && <>
          <label className="flex cursor-pointer items-center gap-1 rounded border border-slate-300 bg-white px-2 py-1 hover:bg-slate-50">
            <FolderOpen size={13} /> Open .exe…
            <input type="file" accept=".exe,application/x-msdownload,application/octet-stream" multiple className="hidden" data-x64-file onChange={e => { if (e.target.files) void addFiles(e.target.files); e.target.value = ''; }} />
          </label>
          <label className="flex cursor-pointer items-center gap-1 rounded border border-slate-300 bg-white px-2 py-1 hover:bg-slate-50" title="Mount a whole folder as the program's disk (C:\\nebula\\)">
            <FolderInput size={13} /> Open folder…
            <input type="file" multiple className="hidden" data-x64-folder {...({ webkitdirectory: '', directory: '' } as Record<string, string>)} onChange={e => { if (e.target.files) void addFolder(e.target.files); e.target.value = ''; }} />
          </label>
          <input value={args} onChange={e => setArgs(e.target.value)} placeholder="arguments" className="w-32 rounded border border-slate-300 px-2 py-1 text-[11px] outline-none focus:border-indigo-400" />
          <button data-x64-run disabled={!current || ready !== 'ready' || !!running || !!fetching} onClick={() => { void run(); }} className="flex items-center gap-1 rounded bg-indigo-600 px-3 py-1 font-semibold text-white disabled:opacity-40 hover:bg-indigo-700"><Play size={12} /> Run</button>
        </>}
        <button data-x64-stop disabled={!running} onClick={stop} className="flex items-center gap-1 rounded border border-slate-300 bg-white px-2 py-1 disabled:opacity-40 hover:bg-slate-50"><Square size={11} /> Stop</button>
        <button data-x64-pack={pack.status} disabled={pack.status !== 'available' || !!running || !!fetching} onClick={() => { const rt = rtRef.current; if (rt) void installPack(rt); }}
          title={pack.status === 'installed' ? `Runtime pack installed in C:\\Windows\\System32 (${pack.done} DLLs)` : pack.status === 'available' ? `Install the VC++ 2015–2022 runtime DLLs (${pack.manifest?.files.length} files, ${((pack.manifest?.files.reduce((a, f) => a + f.size, 0) ?? 0) / 1048576).toFixed(1)} MB) into C:\\Windows\\System32 — also happens automatically when a program needs them` : pack.status === 'unavailable' ? 'Runtime pack not available: ' + pack.note : 'Runtime pack…'}
          className={`hidden items-center gap-1 rounded border px-2 py-1 sm:flex ${pack.status === 'installed' ? 'border-emerald-300 bg-emerald-50 text-emerald-800' : 'border-slate-300 bg-white hover:bg-slate-50 disabled:opacity-40'}`}>
          <Download size={11} /> {pack.status === 'installed' ? 'VC++ runtime ✓' : pack.status === 'installing' ? `VC++ runtime ${pack.done}/${pack.manifest?.files.length}` : pack.status === 'unavailable' ? 'VC++ runtime n/a' : 'VC++ runtime'}
        </button>
        <button data-x64-dotnet={dotnet.status} data-x64-dotnet-note={dotnet.note ?? ''} disabled={dotnet.status !== 'available' || !!running || !!fetching} onClick={() => { const rt = rtRef.current; if (rt) void installDotnet(rt); }}
          title={dotnet.status === 'installed' ? `.NET pack installed in C:\\Program Files\\dotnet (${dotnet.done} files, ${dotnet.manifest?.pack})` : dotnet.status === 'available' ? `Install the official Microsoft .NET Runtime ${dotnet.manifest?.version} (win-x64; ${dotnet.manifest?.files.length} files, ${((dotnet.manifest?.files.reduce((a, f) => a + f.gzsize, 0) ?? 0) / 1048576).toFixed(1)} MB download) into C:\\Program Files\\dotnet — also happens automatically when a .NET program is started` : dotnet.status === 'unavailable' ? '.NET pack not available: ' + dotnet.note : '.NET pack…'}
          className={`hidden items-center gap-1 rounded border px-2 py-1 sm:flex ${dotnet.status === 'installed' ? 'border-emerald-300 bg-emerald-50 text-emerald-800' : 'border-slate-300 bg-white hover:bg-slate-50 disabled:opacity-50'}`}>
          <Download size={11} /> {dotnet.status === 'installed' ? '.NET 10 ✓' : dotnet.status === 'installing' ? `.NET 10 ${dotnet.done}/${dotnet.manifest?.files.length}` : dotnet.status === 'unavailable' ? '.NET n/a' : '.NET 10'}
        </button>
        <button data-x64-dx={dx.status} data-x64-dx-note={dx.note ?? ''} disabled={dx.status !== 'available' || !!running || !!fetching} onClick={() => { const rt = rtRef.current; if (rt) void installDx(rt); }}
          title={dx.status === 'installed' ? `DirectX pack installed in C:\\Windows\\System32 (${dx.done} DLLs, ${dx.manifest?.pack})` : dx.status === 'available' ? `Install the x64 user-mode DLLs of the official DirectX End-User Runtimes (June 2010): D3DCompiler_33…43, d3dx9_24…43, d3dx10/11, d3dcsx, X3DAudio (${dx.manifest?.files.length} files, ${((dx.manifest?.files.reduce((a, f) => a + f.gzsize, 0) ?? 0) / 1048576).toFixed(1)} MB download) — also happens automatically when a program imports one of them` : dx.note ?? 'DirectX pack'}
          className={`hidden items-center gap-1 rounded border px-2 py-1 sm:flex ${dx.status === 'installed' ? 'border-emerald-300 bg-emerald-50 text-emerald-800' : 'border-slate-300 bg-white hover:bg-slate-50 disabled:opacity-50'}`}>
          <Download size={11} /> {dx.status === 'installed' ? 'DirectX ✓' : dx.status === 'installing' ? `DirectX ${dx.done}/${dx.manifest?.files.length}` : dx.status === 'unavailable' ? 'DirectX n/a' : 'DirectX'}
        </button>
        <div className="ml-auto flex items-center gap-1 rounded bg-slate-100 p-0.5">
          {(['library', 'screen', 'console', 'files', 'report', 'components'] as const).map(t => (
            <button key={t} data-x64-tab={t} onClick={() => { autoTabRef.current = false; setTab(t); }} className={`flex items-center gap-1 rounded px-2 py-1 capitalize ${tab === t ? 'bg-white shadow-sm font-semibold' : 'text-slate-500 hover:text-slate-800'}`}>
              {t === 'library' ? <LayoutGrid size={12} /> : t === 'screen' ? <Monitor size={12} /> : t === 'console' ? <TerminalIcon size={12} /> : t === 'files' ? <HardDrive size={12} /> : t === 'components' ? <PackagePlus size={12} /> : <FileSearch size={12} />}{t === 'components' ? 'Компоненты' : t}
            </button>
          ))}
          <button data-x64-tests aria-pressed={showTests} onClick={() => setShowTests(v => !v)} title="Emulator test programs (self-tests and bundled real console tools)" className={`flex items-center gap-1 rounded px-2 py-1 ${showTests ? 'bg-white shadow-sm font-semibold' : 'text-slate-500 hover:text-slate-800'}`}><FlaskConical size={12} />tests</button>
        </div>
      </div>}

      <div className="flex min-h-0 flex-1">
        {/* test programs list (self-tests + bundled console tools); hidden unless “tests” is on */}
        {showTests && !fs && <div className="flex w-56 shrink-0 flex-col border-r border-slate-200 bg-white" onDragOver={e => e.preventDefault()} onDrop={e => { e.preventDefault(); void addFiles(e.dataTransfer.files); }}>
          <div className="border-b border-slate-100 px-3 py-1.5 text-[10px] font-semibold uppercase tracking-wide text-slate-400" data-x64-samples={samplesReady ? 'ready' : 'loading'}>Executables ({exes.length}){!samplesReady && <Loader2 size={10} className="ml-1 inline animate-spin" />}</div>
          <div className="min-h-0 flex-1 overflow-y-auto">
            {ready === 'loading' && <div className="p-3 text-slate-400">Loading wasm64 emulator…</div>}
            {ready === 'unsupported' && <div className="p-3 text-rose-700">This browser has no WebAssembly memory64 (wasm64). Chrome/Edge ≥ 133, Firefox ≥ 134 or Android WebView ≥ 133 is required.</div>}
            {ready === 'error' && <div className="p-3 text-rose-700">Emulator failed to load: {initError}</div>}
            {ready === 'ready' && exes.length === 0 && <div className="p-3 text-slate-400"><Loader2 size={14} className="mr-1 inline animate-spin" />Downloading the test programs…</div>}
            {exes.map(e => { const v = V(e.info.ok ? e.info.verdict : 'invalid'); return (
              <button key={e.id} data-x64-exe={e.name} onClick={() => { setSelected(e.id); const d = SAMPLES.find(x => x.name === e.name)?.args; if (e.source === 'sample' && d !== undefined) setArgs(d); }} className={`flex w-full items-start gap-2 border-b border-slate-100 px-3 py-2 text-left hover:bg-indigo-50 ${selected === e.id ? 'bg-indigo-50' : ''}`}>
                <v.Icon size={14} className={`mt-0.5 shrink-0 ${v.cls.split(' ')[1]}`} />
                <span className="min-w-0 flex-1">
                  <span className="block truncate font-semibold">{e.name}</span>
                  <span className="block text-[10px] text-slate-500">{e.info.ok ? `${e.info.arch} · ${e.info.subsystem} · ${(e.bytes.length / 1024).toFixed(0)} KB` : 'not a PE file'}</span>
                  <span className={`mt-0.5 inline-block rounded border px-1 text-[9px] font-semibold ${v.cls}`} data-x64-verdict={e.info.verdict ?? 'invalid'}>{v.label}</span>
                </span>
              </button>); })}
            {ready === 'ready' && <div className="p-3 text-[10px] text-slate-400">Drop .exe files here or use “Open .exe…”. Files never leave this device — everything runs in the page.</div>}
          </div>
        </div>}

        {/* main */}
        <div className="flex min-w-0 flex-1 flex-col">
          {tab === 'components' && !fs && (
            <div className="min-h-0 flex-1 overflow-auto p-4" data-x64-components>
              <h2 className="mb-2 text-lg font-bold">Системные компоненты</h2>
              <p className="mb-4 text-sm text-slate-600">Установка в виртуальный диск Nebula, а не в Android. Некоторые компоненты устанавливаются автоматически при запуске программы. Файлы сохраняются на виртуальном диске между запусками.</p>
              <div className="space-y-3">
                <div className="rounded border p-3"><b>Visual C++ 2015–2022 x64</b><p className="text-sm">Библиотеки Microsoft для 64-битных программ. Статус: {pack.status}{pack.note ? ` — ${pack.note}` : ''}</p><button data-x64-vc14x64={pack.status} disabled={ready !== 'ready' || pack.status !== 'available' || !!running || !!fetching} onClick={() => { const rt = rtRef.current; if (rt) void installPack(rt); }} className="mt-2 rounded bg-indigo-600 px-3 py-2 text-white disabled:opacity-40">Установить</button></div>
                <div className="rounded border p-3"><b>DirectX June 2010 x64</b><p className="text-sm">Пользовательские DLL; не драйвер и не полная реализация DirectX. Статус: {dx.status}{dx.note ? ` — ${dx.note}` : ''}</p><button data-x64-component-dx={dx.status} disabled={ready !== 'ready' || dx.status !== 'available' || !!running || !!fetching} onClick={() => { const rt = rtRef.current; if (rt) void installDx(rt); }} className="mt-2 rounded bg-indigo-600 px-3 py-2 text-white disabled:opacity-40">Установить</button></div>
<div className="rounded border p-3"><b>DirectX June 2010 x86</b><p className="text-sm">Те же DLL (D3DX, D3DCompiler, XAudio2, X3DAudio) в 32-битном варианте → C:\\Windows\\SysWOW64 для 32-битных игр и программ. Статус: {dx32.status}{dx32.note ? ` — ${dx32.note}` : ''}</p><button data-x64-component-dx32={dx32.status} disabled={ready !== 'ready' || dx32.status !== 'available' || !!running || !!fetching} onClick={() => { const rt = rtRef.current; if (rt) void installDx32(rt); }} className="mt-2 rounded bg-indigo-600 px-3 py-2 text-white disabled:opacity-40">Установить</button></div>
                <div className="rounded border p-3"><b>.NET 10 x64</b><p className="text-sm">Среда Microsoft для поддерживаемых управляемых программ. Статус: {dotnet.status}{dotnet.note ? ` — ${dotnet.note}` : ''}</p><button data-x64-component-dotnet={dotnet.status} disabled={ready !== 'ready' || dotnet.status !== 'available' || !!running || !!fetching} onClick={() => { const rt = rtRef.current; if (rt) void installDotnet(rt); }} className="mt-2 rounded bg-indigo-600 px-3 py-2 text-white disabled:opacity-40">Установить</button></div>
                {vcPacks.map(pk => { const st = vcState[pk.id] ?? { status: 'available' as const }; const mb = pk.files.reduce((a, f) => a + f.size, 0) / 1048576; return (
                  <div key={pk.id} className="rounded border p-3" data-x64-vcpack={pk.id} data-x64-vcpack-status={st.status}>
                    <b>{pk.label}</b>
                    <p className="text-sm">{pk.arch === 'x64' ? 'Библиотеки Microsoft для 64-битных программ' : 'Библиотеки Microsoft для 32-битных программ'} → {pk.guest_dir}: {pk.files.map(f => f.name.replace(/\.dll$/i, '')).join(', ')} ({mb.toFixed(1)} MB, {pk.files[0]?.version}).
                      {' '}{st.status === 'installed' ? 'Установлено.' : st.status === 'installing' ? `Устанавливается… ${st.done ?? 0}/${pk.files.length}` : st.status === 'error' ? `Ошибка: ${st.note}` : 'Доступно для установки (ставится автоматически, когда программа импортирует эти DLL).'}</p>
                    <button data-x64-vcpack-btn={pk.id} {...(pk.id === 'vc2010-x86' ? { 'data-x64-vc10': st.status } : pk.id === 'vc2015-x86' ? { 'data-x64-vc14x86': st.status } : {})} disabled={ready !== 'ready' || !!running || !!fetching || st.status === 'installed' || st.status === 'installing'} onClick={() => { const rt = rtRef.current; if (rt) void installVcPack(rt, pk.id); }} className="mt-2 rounded bg-indigo-600 px-3 py-2 text-white disabled:opacity-40">{st.status === 'error' ? 'Повторить' : 'Установить'}</button>
                  </div>); })}
                {vcPacks.length === 0 && <p className="text-sm text-slate-500">Пакеты VC++ 2005–2013 и 2015–2022 x86 не собраны в этой сборке (нет /x64/sys32/packs.json).</p>}
              </div>
              {vcPacks.length > 0 && <button data-x64-vc-all disabled={ready !== 'ready' || !!running || !!fetching || vcPacks.every(pk => vcState[pk.id]?.status === 'installed')} onClick={() => { const rt = rtRef.current; if (!rt) return; void (async () => { for (const pk of vcPacks) if (vcState[pk.id]?.status !== 'installed') await installVcPack(rt, pk.id); })(); }} className="mt-3 rounded border border-indigo-600 px-3 py-2 text-indigo-700 disabled:opacity-40">Установить все Visual C++ (x86 и x64)</button>}
              <h3 className="mb-1 mt-5 text-sm font-bold">Журнал установки</h3>
              {compLog.length === 0 ? <p className="text-sm text-slate-500">Пока пусто: здесь будет результат каждой установки (файлы, размер, время, ошибки).</p> : (
                <div className="max-h-64 overflow-auto rounded border bg-white p-2 font-mono text-[10px]" data-x64-complog>
                  {compLog.slice().reverse().map((e, i) => <div key={i} className={e.ok ? 'text-emerald-800' : 'text-rose-700'}>{new Date(e.t).toLocaleString()} · {e.pack} · {e.text}</div>)}
                </div>)}
              {compLog.length > 0 && <button onClick={() => { setCompLog([]); try { localStorage.removeItem(COMP_LOG_KEY); } catch { /* ignore */ } }} className="mt-2 rounded border px-2 py-1 text-xs">Очистить журнал</button>}
              <p className="mt-4 text-sm text-slate-600">Все пакеты извлекаются при сборке из официальных установщиков Microsoft (vcredist / vc_redist) и ставятся на виртуальный диск C:\ Nebula, а не в Android. Драйверы ядра Windows установить нельзя: системные API и устройства реализуются эмулятором, их поддержка неполная.</p>
            </div>
          )}
          {tab === 'library' && !fs && (
            <div className="min-h-0 flex-1 overflow-auto" data-x64-library onClick={() => setMenuFor(null)}>
              {ready === 'loading' && <div className="p-4 text-slate-400"><Loader2 size={14} className="mr-1 inline animate-spin" />Loading the emulator and the persistent disk…</div>}
              {ready === 'unsupported' && <div className="p-4 text-rose-700">This WebView has no WebAssembly memory64 (wasm64). Android System WebView ≥ 133 is required.</div>}
              {ready === 'error' && <div className="p-4 text-rose-700">Emulator failed to load: {initError}</div>}
              {/* what is running / just ran */}
              {current && (stats || fetching) && (
                <div className="m-3 rounded-lg border border-slate-200 bg-white p-3" data-x64-activity={running ? 'running' : stats?.status ?? 'idle'}>
                  <div className="flex items-center gap-2">
                    {running ? <Loader2 size={16} className="shrink-0 animate-spin text-indigo-600" /> : stats?.status === 'fault' ? <XCircle size={16} className="shrink-0 text-rose-600" /> : <CheckCircle2 size={16} className="shrink-0 text-emerald-600" />}
                    <div className="min-w-0 flex-1">
                      <div className="truncate text-[13px] font-semibold">{current.kind === 'installer' ? 'Installing: ' : ''}{current.path ?? current.name}{stats?.title && stats.title !== current.name ? <span className="font-normal text-slate-500"> — {stats.title}</span> : null}</div>
                      <div className="text-[10px] text-slate-500">
                        {fetching ? fetching : running ? (stats?.status === 'waiting-stdin' ? 'waiting for console input' : stats?.hasWindow ? `running · ${stats.frames} frames · ${stats.procs} process${stats.procs === 1 ? '' : 'es'}` : 'starting…') : stats?.status === 'exited' ? `finished (exit code ${stats.exitCode}) in ${((stats.elapsedMs) / 1000).toFixed(1)} s` : stats?.status === 'fault' ? `stopped with an error: ${stats.error}` : stats?.status}
                        {stats ? ` · ${stats.mips.toFixed(0)} MIPS` : ''}{padInfo ? <span> · 🎮 {padInfo}</span> : null}{disk.writes ? ` · ${disk.writes} files written (${(disk.bytes / 1048576).toFixed(1)} MB)${disk.queued ? ' · saving…' : ' · saved'}` : ''}
                      </div>
                    </div>
                    {running && stats?.hasWindow && <button data-x64-show-screen onClick={() => { autoTabRef.current = false; setTab('screen'); }} className="flex items-center gap-1 rounded bg-indigo-600 px-2 py-1 font-semibold text-white hover:bg-indigo-700"><Monitor size={12} /> Show</button>}
                    {running && <button onClick={stop} className="flex items-center gap-1 rounded border border-slate-300 px-2 py-1 hover:bg-slate-50"><Square size={11} /> Stop</button>}
                    {!running && current.source === 'file' && current.kind === 'program' && current.info.ok && <button data-x64-add-library onClick={() => { void addToLibrary(current); }} className="flex items-center gap-1 rounded border border-indigo-300 bg-indigo-50 px-2 py-1 font-semibold text-indigo-800 hover:bg-indigo-100"><PackagePlus size={12} /> Add to library</button>}
                    {!running && stats?.status === 'fault' && <button onClick={() => setTab('report')} className="rounded border border-rose-300 bg-rose-50 px-2 py-1 text-rose-800">Details</button>}
                  </div>
                  {running && current.kind === 'installer' && <div className="mt-2 rounded bg-indigo-50 px-2 py-1 text-[11px] text-indigo-900">The setup program is running with its own wizard — it appears on the Screen tab as soon as it draws its first window. Finish it there; the installed program is added here when setup exits.</div>}
                </div>
              )}
              {ready === 'ready' && (
                <div className="flex items-center gap-2 px-4 pt-3 text-[10px] text-slate-500">
                  <HardDrive size={12} /> <span data-x64-disk={diskReady ? 'persistent' : 'volatile'}>{diskReady ? `Persistent disk C:\\ — ${disk.files} files restored${disk.writes ? `, ${disk.writes} written this session` : ''}` : 'IndexedDB unavailable: installs last until the app is closed'}</span>
                  <span className="ml-auto">{library.length} installed</span>
                </div>
              )}
              {ready === 'ready' && library.length === 0 && (
                <div className="m-4 rounded-lg border border-dashed border-slate-300 bg-white p-6 text-center text-slate-500">
                  <LayoutGrid size={36} strokeWidth={1} className="mx-auto text-slate-400" />
                  <div className="mt-2 text-[13px] font-semibold text-slate-700">Nothing installed yet</div>
                  <div className="mt-1 text-[11px]">Use <b>Open / Install…</b> to run a Windows program or its setup (.exe) from this device — or pick a <b>.zip</b> of a portable program / game folder to install it. Setup programs install onto the persistent disk C:\\ and their Start Menu shortcuts become tiles here.</div>
                </div>
              )}
              <div className="grid grid-cols-2 gap-3 p-3 sm:grid-cols-3 md:grid-cols-4 lg:grid-cols-5">
                {library.filter(it => !prefs[it.id]?.hidden).map(it => { const cover = prefs[it.id]?.cover; const icon = icons[it.id]; return (
                  <div key={it.id} data-x64-item={it.name} className="group relative flex flex-col overflow-hidden rounded-lg border border-slate-200 bg-white shadow-sm" onClick={e => e.stopPropagation()}>
                    <button onClick={() => launchItem(it)} disabled={!!running || !!fetching} className="relative block aspect-[4/3] w-full overflow-hidden bg-gradient-to-br from-slate-700 to-slate-900 text-left disabled:cursor-default" title={`Start ${it.exe}`}>
                      {cover ? <img src={cover} alt="" className="h-full w-full object-cover" /> : icon ? <img src={icon} alt="" data-x64-icon className="absolute left-1/2 top-1/2 h-14 w-14 -translate-x-1/2 -translate-y-1/2 drop-shadow-[0_2px_6px_rgba(0,0,0,0.6)]" style={{ imageRendering: 'auto' }} /> : <Cpu size={32} strokeWidth={1} className="absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2 text-white/60" />}
                      <span className="absolute inset-0 flex items-center justify-center bg-black/0 opacity-0 transition group-hover:bg-black/30 group-hover:opacity-100"><span className="flex items-center gap-1 rounded-full bg-white px-3 py-1 text-[11px] font-semibold text-slate-900"><Play size={12} /> Play</span></span>
                      {cover && icon && <img src={icon} alt="" className="absolute bottom-1 left-1 h-6 w-6 drop-shadow" />}
                    </button>
                    <div className="flex items-start gap-1 px-2 py-1.5">
                      <div className="min-w-0 flex-1">
                        <div className="truncate text-[12px] font-semibold" title={it.name}>{it.name}</div>
                        <div className="truncate text-[10px] text-slate-500">{[it.publisher, it.version, it.group].filter(Boolean).join(' · ') || it.exe.replace(/^c:\\program files( \(x86\))?\\/i, '')}</div>
                      </div>
                      <button data-x64-item-menu onClick={() => setMenuFor(m => m === it.id ? null : it.id)} className="rounded p-1 text-slate-500 hover:bg-slate-100" title="More">⋯</button>
                    </div>
                    {menuFor === it.id && (
                      <div className="absolute right-1 top-1 z-10 w-44 rounded-md border border-slate-200 bg-white py-1 text-[11px] shadow-lg">
                        <button onClick={() => { setMenuFor(null); launchItem(it); }} className="flex w-full items-center gap-2 px-3 py-1.5 hover:bg-slate-100"><Play size={12} /> Play</button>
                        <label className="flex w-full cursor-pointer items-center gap-2 px-3 py-1.5 hover:bg-slate-100"><ImageIcon size={12} /> Set cover image…<input type="file" accept="image/*" className="hidden" data-x64-cover onChange={e => { void setCover(it.id, e.target.files?.[0] ?? null); e.target.value = ''; }} /></label>
                        {cover && <button onClick={() => { void setCover(it.id, null); }} className="flex w-full items-center gap-2 px-3 py-1.5 hover:bg-slate-100"><X size={12} /> Remove cover</button>}
                        <button onClick={() => { setMenuFor(null); setTab('files'); }} className="flex w-full items-center gap-2 px-3 py-1.5 hover:bg-slate-100"><HardDrive size={12} /> Open files</button>
                        <button data-x64-uninstall onClick={() => uninstallItem(it)} className="flex w-full items-center gap-2 px-3 py-1.5 text-rose-700 hover:bg-rose-50"><Trash2 size={12} /> {it.uninstall ? 'Uninstall' : 'Remove'}</button>
                      </div>
                    )}
                  </div>); })}
              </div>
            </div>
          )}
          {tab === 'screen' && (
            <div ref={screenRef} tabIndex={0} onKeyDown={e => guestKey(e, true)} onKeyUp={e => guestKey(e, false)} className="relative flex min-h-0 flex-1 items-center justify-center bg-black outline-none" onClick={focusGuest}>
              <canvas ref={canvasRef} data-x64-screen data-frames={frames} className="h-full w-full" style={{ imageRendering: 'pixelated', touchAction: 'none' }}
                onPointerDown={e => guestPointer(e, 'down')} onPointerMove={e => guestPointer(e, 'move')} onPointerUp={e => guestPointer(e, 'up')} onPointerCancel={e => guestPointer(e, 'cancel')} onWheel={guestWheel} onContextMenu={e => e.preventDefault()} />
              {/* IME / soft-keyboard field: invisible, focused while the keyboard toggle is on; its text is diffed and sent to the guest */}
              <textarea ref={imeRef} aria-label="Type into the Windows program" data-x64-ime autoCapitalize="off" autoCorrect="off" autoComplete="off" spellCheck={false} rows={1} defaultValue={IME_SENTINEL}
                className="absolute bottom-0 left-0 h-px w-px resize-none opacity-0" style={{ fontSize: 16, pointerEvents: imeOpen ? 'auto' : 'none' }} tabIndex={imeOpen ? 0 : -1}
                onInput={imeInput} onCompositionEnd={imeInput} onKeyDown={e => imeKey(e, true)} onKeyUp={e => imeKey(e, false)} onBlur={() => { if (imeOpen && running) setTimeout(() => { const a = document.activeElement; if (imeOpen && a && a !== imeRef.current && !a.closest?.('[data-x64-touchbar]')) setImeOpen(false); }, 0); }} />
              <button data-x64-fs onClick={e => { e.stopPropagation(); toggleFullscreen(); }} title={fs ? 'Exit fullscreen' : 'Fullscreen (game mode)'} className={`absolute right-2 top-2 rounded-full bg-black/50 p-2 text-white ${touch || fs ? '' : 'opacity-0 hover:opacity-100'}`} onPointerDown={e => e.stopPropagation()}>{fs ? <Minimize2 size={14} /> : <Maximize2 size={14} />}</button>
              {running && fpsShow && stats && (
                <div data-x64-fps className="pointer-events-none absolute left-2 top-2 rounded bg-black/50 px-1.5 py-0.5 font-mono text-[11px] leading-tight text-lime-300">{fps} FPS · {stats.mips.toFixed(0)} MIPS</div>
              )}
              {!running && frames === 0 && (
                <div className="pointer-events-none absolute inset-0 flex flex-col items-center justify-center text-center text-slate-400">
                  <Monitor size={40} strokeWidth={1} />
                  <div className="mt-2 text-[12px]">{current ? `${current.name} is starting…` : 'Start a program from the Library'}</div>
                  <div className="mt-1 text-[10px] text-slate-500">GUI windows render here through WebGL; console programs write to the Console tab.</div>
                </div>
              )}
              {running && padMode && stats?.hasWindow && (
                <div ref={cursorRef} data-x64-cursor className="pointer-events-none absolute left-0 top-0 z-10 will-change-transform" style={{ filter: 'drop-shadow(0 1px 1px rgba(0,0,0,.8))' }}>
                  <svg width="18" height="26" viewBox="0 0 18 26"><path d="M1 1v20l5-5 3.5 8 3-1.3-3.4-7.7H16Z" fill="#fff" stroke="#000" strokeWidth="1.2" strokeLinejoin="round" /></svg>
                </div>
              )}
              {running && vpad && <VirtualPad onChange={onVpad} onClose={() => setVpad(false)} />}
              {running && tmShow && stats?.hasWindow && <TouchMouse onButton={tmButton} onWheel={tmWheel} />}
              {running && stats?.hasWindow && (
                <div data-x64-touchbar className={`absolute bottom-2 left-1/2 flex max-w-full -translate-x-1/2 gap-1 overflow-x-auto rounded-full bg-black/50 px-2 py-1 ${touch ? '' : 'opacity-0 hover:opacity-100'}`} onMouseDown={e => e.stopPropagation()}>
                  <button data-x64-kbd className={`min-w-9 select-none rounded-full px-2 py-1.5 text-white ${imeOpen ? 'bg-indigo-500' : 'bg-white/15'}`} title="Soft keyboard: type into the Windows program" aria-pressed={imeOpen}
                    onPointerDown={e => { e.preventDefault(); e.stopPropagation(); }} onClick={e => { e.preventDefault(); toggleIme(); }}><Keyboard size={14} className="mx-auto" /></button>
                  <button data-x64-vpad-toggle className={`min-w-9 select-none rounded-full px-2 py-1.5 text-white ${vpad ? 'bg-indigo-500' : 'bg-white/15'}`} title="On-screen game controller (XInput / DirectInput controller 0)" aria-pressed={vpad}
                    onPointerDown={e => { e.preventDefault(); e.stopPropagation(); }} onClick={e => { e.preventDefault(); setVpad(v => !v); }}><Gamepad2 size={14} className="mx-auto" /></button>
                  <button data-x64-touchpad className={`min-w-9 select-none rounded-full px-2 py-1.5 text-white ${padMode ? 'bg-indigo-500' : 'bg-white/15'}`} title="Touchpad: drag anywhere to move the cursor; tap = click; tap, then touch and move = drag; hold = right click; two fingers = scroll.  Off = tap where you point" aria-pressed={padMode}
                    onPointerDown={e => { e.preventDefault(); e.stopPropagation(); }} onClick={e => { e.preventDefault(); setPadMode(v => !v); }}><Mouse size={14} className="mx-auto" /></button>
                  <button data-x64-tmouse className={`min-w-9 select-none rounded-full px-2 py-1.5 text-white ${tmShow ? 'bg-indigo-500' : 'bg-white/15'}`} title="On-screen mouse buttons (left · scroll up / middle / scroll down · right); drag them by the ⋮⋮ grip" aria-pressed={tmShow}
                    onPointerDown={e => { e.preventDefault(); e.stopPropagation(); }} onClick={e => { e.preventDefault(); setTmShow(v => !v); }}><MousePointerClick size={14} className="mx-auto" /></button>
                  <button data-x64-deskfit className={`min-w-9 select-none rounded-full px-2 py-1.5 text-white ${deskFit ? 'bg-indigo-500' : 'bg-white/15'}`} title="Desktop fills the screen (follows rotation).  Off = the view follows the program's window" aria-pressed={deskFit}
                    onPointerDown={e => { e.preventDefault(); e.stopPropagation(); }} onClick={e => { e.preventDefault(); setDeskFit(v => !v); }}><Expand size={14} className="mx-auto" /></button>
                  <button data-x64-fpstoggle className={`min-w-9 select-none rounded-full px-2 py-1.5 text-[11px] font-bold text-white ${fpsShow ? 'bg-indigo-500' : 'bg-white/15'}`} title="FPS / MIPS counter in the corner" aria-pressed={fpsShow}
                    onPointerDown={e => { e.preventDefault(); e.stopPropagation(); }} onClick={e => { e.preventDefault(); setFpsShow(v => !v); }}>FPS</button>
                  {TOUCH_KEYS.map(k => (
                    <button key={k.vk} className={`min-w-9 shrink-0 select-none rounded-full px-2 py-1.5 text-[11px] font-bold text-white active:bg-indigo-500 ${sticky.includes(k.vk) ? 'bg-indigo-500' : 'bg-white/15'}`} aria-pressed={k.sticky ? sticky.includes(k.vk) : undefined}
                      onPointerDown={e => { e.preventDefault(); e.currentTarget.setPointerCapture(e.pointerId); touchKey(k.vk, true, k.sticky); }}
                      onPointerUp={e => { e.preventDefault(); touchKey(k.vk, false, k.sticky); }}
                      onPointerCancel={() => touchKey(k.vk, false, k.sticky)}>{k.label}</button>
                  ))}
                </div>
              )}
            </div>
          )}
          {tab === 'console' && (
            <div className="flex min-h-0 flex-1 flex-col bg-[#0c0c0c]">
              <pre ref={consoleRef} data-x64-console className="m-0 min-h-0 flex-1 overflow-auto p-3 font-mono text-[12px] leading-[1.35] text-[#cccccc] whitespace-pre-wrap">{consoleText || (fetching ? <span data-x64-fetching className="text-amber-300">{fetching}</span> : running ? '' : '(no console output yet)')}</pre>
              <form className={`flex shrink-0 items-center gap-1 border-t px-2 py-1 ${waitingStdin ? 'border-amber-500/60 bg-amber-950/40' : 'border-white/10'}`} onSubmit={e => { e.preventDefault(); sendStdin(false); }}>
                <Keyboard size={13} className={waitingStdin ? 'text-amber-300' : 'text-white/40'} />
                <input ref={stdinRef} data-x64-stdin value={stdinLine} onChange={e => setStdinLine(e.target.value)} disabled={!running} placeholder={waitingStdin ? 'program is waiting for input — type a line and press Enter' : running ? 'stdin (sent with Enter)' : 'stdin — run a program first'}
                  className="min-w-0 flex-1 bg-transparent px-1 font-mono text-[12px] text-white outline-none placeholder:text-white/30" autoComplete="off" spellCheck={false} />
                <button type="submit" data-x64-stdin-send disabled={!running} className="rounded bg-white/10 px-2 py-0.5 text-[10px] font-semibold text-white disabled:opacity-30 hover:bg-white/20"><SendHorizontal size={12} /></button>
                <button type="button" data-x64-stdin-eof disabled={!running} onClick={() => sendStdin(true)} title="close stdin (Ctrl-D / Ctrl-Z)" className="rounded bg-white/10 px-2 py-0.5 text-[10px] font-semibold text-white disabled:opacity-30 hover:bg-white/20">EOF</button>
              </form>
            </div>
          )}
          {tab === 'files' && (
            <div className="flex min-h-0 flex-1 flex-col" data-x64-files onDragOver={e => e.preventDefault()} onDrop={e => { e.preventDefault(); void addDataFiles(e.dataTransfer.files); }}>
              <div className="flex shrink-0 items-center gap-2 border-b border-slate-200 bg-white px-3 py-1.5 text-[11px]">
                <HardDrive size={13} className="text-indigo-600" /> <b>Disk C:\</b> <span className="text-slate-500">— {diskReady ? 'persistent (IndexedDB): everything programs write is kept across restarts, except C:\\tmp, Temp folders and the C:\\nebula scratch folder' : 'in memory for this session'}. Files can be downloaded to the device.</span>
                <label className="ml-auto flex cursor-pointer items-center gap-1 rounded border border-slate-300 bg-white px-2 py-0.5 hover:bg-slate-50"><FolderOpen size={12} /> Add files…<input type="file" multiple className="hidden" data-x64-addfiles onChange={e => { if (e.target.files) void addDataFiles(e.target.files); e.target.value = ''; }} /></label>
                <button onClick={() => setFiles(rtRef.current?.vfsList() ?? [])} className="rounded border border-slate-300 bg-white px-2 py-0.5 hover:bg-slate-50">Refresh</button>
              </div>
              {nativePlatform() === 'android' && <div className="border-b border-amber-200 bg-amber-50 px-3 py-2 text-xs" data-x64-storage-folder>
                <b>Папка Nebula:</b> {storageFolder.selected ? storageFolder.name : 'не выбрана'}
                <button disabled={storageBusy || !!running} onClick={() => { void chooseStorageFolder(); }} className="ml-2 rounded border border-indigo-600 px-2 py-1 text-indigo-700 disabled:opacity-40">{storageBusy ? 'Выбор…' : 'Выбрать папку'}</button>
                <div>Доступ сохраняется Android. Перенос и использование папки ещё не подключены: все текущие игры и системные файлы остаются на прежнем виртуальном диске.</div>
                {(storageFolder.error || storageMessage) && <div role="status" className="text-rose-800">{storageMessage || storageFolder.error}</div>}
              </div>}
              <div className="min-h-0 flex-1 overflow-auto">
                {files.length === 0 && <div className="p-4 text-slate-400">Empty. Use “Open folder…” to mount a directory, drop files here, or run a program that creates files.</div>}
                <table className="w-full text-[11px]">
                  <tbody>
                    {files.map(f => (
                      <tr key={f.path} data-x64-vfs={f.path} className="border-b border-slate-100 hover:bg-indigo-50/40">
                        <td className="px-3 py-1 font-mono">{f.dir ? '📁 ' : ''}{f.path}{!!f.written && !f.dir && <span className="ml-2 rounded bg-emerald-100 px-1 text-[9px] font-semibold text-emerald-800" title="written this session; not on the persistent disk (scratch path)">written</span>}</td>
                        <td className="px-2 py-1 text-right text-slate-500">{f.dir ? '' : f.size.toLocaleString() + ' B'}</td>
                        <td className="px-2 py-1 text-right whitespace-nowrap">
                          {!f.dir && <button title="download" data-x64-download onClick={() => downloadFile(f.path)} className="rounded p-0.5 text-slate-500 hover:bg-white hover:text-indigo-700"><Download size={13} /></button>}
                          <button title="delete" onClick={() => removeFile(f.path)} className="rounded p-0.5 text-slate-500 hover:bg-white hover:text-rose-700"><Trash2 size={13} /></button>
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </div>
          )}
          {tab === 'report' && (
            <div className="min-h-0 flex-1 overflow-auto p-4" data-x64-report>
              {current ? <PeReport e={current} stats={stats} log={log} apiStats={apiStats} jit={jitInfo} runtime={{ packs: vcPacks, status: id => vcState[id]?.status ?? 'available', install: id => { const rt = rtRef.current; if (rt) void installVcPack(rt, id); }, busy: !!running || !!fetching }} /> : <div className="text-slate-400">Select an executable to see its compatibility report.</div>}
            </div>
          )}
          {/* status bar */}
          {!fs && <div className="flex h-7 shrink-0 items-center gap-3 border-t border-slate-200 bg-white px-3 text-[10px] text-slate-600">
            <span className={`font-semibold ${stats?.status === 'fault' ? 'text-rose-700' : stats?.status === 'running' ? 'text-emerald-700' : stats?.status === 'waiting-stdin' ? 'text-amber-700' : ''}`}>{(stats?.status ?? 'idle').toUpperCase()}{stats?.status === 'exited' ? ` (code ${stats.exitCode})` : ''}</span>
            {stats && <span>{(stats.icount / 1e6).toFixed(1)} M insns · {stats.mips.toFixed(1)} MIPS · {stats.frames} frames · {stats.memMB.toFixed(0)} MB guest{stats.procs > 1 ? <span data-x64-procs={stats.procs}> · {stats.procs} processes</span> : null}{stats.threads > 1 ? <span data-x64-threads={stats.threads}> · {stats.threads} threads</span> : null}{padInfo ? <span data-x64-pads={padInfo}> · 🎮 {padInfo}</span> : null}{stats.audioFrames > 0 ? <span data-x64-audio={stats.audioFrames} title={stats.audio === 'suspended' ? 'the browser keeps audio suspended until you tap/click once' : 'sound is playing through the device output'}> · 🔊 {(stats.audioFrames / 48000).toFixed(1)} s{stats.audio === 'suspended' ? ' (tap to unmute)' : ''}</span> : null}</span>}
            <span className="ml-auto truncate">{glInfo}</span>
            <span className="shrink-0">wasm64 {moduleKB ? moduleKB.toFixed(0) + ' KB' : ''}</span>
          </div>}
        </div>
      </div>
    </div>
  );
}

function PeReport({ e, stats, log, apiStats, jit, runtime }: { e: ExeEntry; stats: RunStats | null; log: string[]; apiStats: string; jit: JitInfo | null; runtime?: { packs: { id: string; label: string }[]; status: (id: string) => string; install: (id: string) => void; busy: boolean } }) {
  const i = e.info; const v = VERDICT[i.ok ? (i.verdict ?? 'invalid') : 'invalid'] ?? VERDICT.invalid;
  const exceptionExit = stats?.status === 'exited' && (stats.exitCode >>> 0) === 0xc0000005;
  const [digest, setDigest] = useState<{ bytes: Uint8Array; value: string } | null>(null);
  const sha256 = digest?.bytes === e.bytes ? digest.value : '';
  useEffect(() => {
    let cancelled = false;
    // Hash the actual loaded executable bytes, not the filename or a manifest entry.
    const bytes = e.bytes;
    void Promise.resolve().then(async () => {
      if (!globalThis.crypto?.subtle) return 'недоступен в этой среде';
      const copy = new Uint8Array(bytes);
      const hash = await globalThis.crypto.subtle.digest('SHA-256', copy.buffer);
      return Array.from(new Uint8Array(hash), b => b.toString(16).padStart(2, '0')).join('');
    }).then(value => { if (!cancelled) setDigest({ bytes, value }); })
      .catch(() => { if (!cancelled) setDigest({ bytes, value: 'не удалось вычислить' }); });
    return () => { cancelled = true; };
  }, [e.bytes]);
  return (
    <div className="space-y-3">
      <div className="rounded-lg border border-slate-200 bg-white p-3">
        <div className="flex items-center gap-2">
          <v.Icon size={16} className={v.cls.split(' ')[1]} />
          <span className="text-[13px] font-semibold">{e.name}</span>
          <span className={`rounded border px-1.5 py-0.5 text-[10px] font-semibold ${v.cls}`}>{v.label}</span>
        </div>
        {i.ok ? (
          <>
            <div className="mt-1 text-[11px] text-slate-600">{i.why}</div>
            <dl className="mt-2 grid grid-cols-[120px_1fr] gap-x-3 gap-y-0.5 text-[11px]">
              <dt className="text-slate-500">Путь EXE</dt><dd className="break-all">{e.path ?? (e.source === 'file' ? 'открыт с устройства; путь Android недоступен' : 'встроенный образец')}</dd>
              <dt className="text-slate-500">Размер файла</dt><dd>{e.bytes.length.toLocaleString()} bytes</dd>
              <dt className="text-slate-500">SHA-256 EXE</dt><dd className="break-all font-mono" data-x64-exe-sha256>{sha256 || 'вычисляется…'}</dd>
              <dt className="text-slate-500">Architecture</dt><dd>{i.arch} (machine 0x{(i.machine ?? 0).toString(16)}){i.pe32plus ? ', PE32+' : ', PE32'}</dd>
              <dt className="text-slate-500">Subsystem</dt><dd>{i.subsystem}{i.dll ? ' · DLL' : ''}{i.clr ? ' · .NET' : ''}</dd>
              <dt className="text-slate-500">Image</dt><dd>base {i.imageBase}, {(i.sizeOfImage ?? 0 / 1024).toLocaleString()} bytes, entry {i.entry}, {i.sections} sections ({i.sectionNames?.join(' ')})</dd>
              <dt className="text-slate-500">Features</dt><dd>{[i.relocs && 'relocations', i.tls && 'TLS', i.exceptions && 'SEH tables', (i.resources ?? 0) > 0 && 'resources'].filter(Boolean).join(', ') || '—'}</dd>
              <dt className="text-slate-500">Imports</dt><dd>{i.imports} total · {i.knownImports} implemented · <span className={(i.unknownImportsCount ?? 0) ? 'font-semibold text-rose-700' : ''}>{i.unknownImportsCount} missing</span>{!!i.runtimeImports && <> · {i.runtimeImports} from the Visual C++ runtime pack (real Microsoft DLLs, installed on demand)</>}</dd>
              {!!i.privateDllImports && (<><dt className="text-slate-500">Own DLLs</dt><dd>{i.privateDllImports} import(s) come from the program's own DLLs — put those .dll files next to the exe (same folder / C:\nebula) and they are loaded like on Windows: static imports, LoadLibrary, GetProcAddress, DllMain, per-thread TLS.</dd></>)}
              <dt className="text-slate-500">DLLs</dt><dd className="break-words">{i.dlls?.join(', ')}</dd>
              {vcPacksFor(i.dlls, i.pe32plus).filter(id => id !== 'vc2015-x64').length > 0 && (<><dt className="text-slate-500">Runtime</dt><dd>{vcPacksFor(i.dlls, i.pe32plus).filter(id => id !== 'vc2015-x64').map(id => { const pk = runtime?.packs.find(p => p.id === id); const st = runtime?.status(id) ?? 'available'; return <span key={id} className="mr-2">{pk ? pk.label : id}: {st === 'installed' ? 'installed' : pk && runtime ? <button disabled={runtime.busy} onClick={() => runtime.install(id)} className="rounded border border-indigo-600 px-1 text-indigo-700">install</button> : 'not in this build'}</span>; })}</dd></>)}
              {!!i.unknownImports && (<><dt className="text-slate-500">Missing APIs</dt><dd className="break-words font-mono text-[10px] text-rose-700">{i.unknownImports}</dd></>)}
            </dl>
          </>
        ) : <div className="mt-1 text-rose-700">{i.error}</div>}
      </div>
      {stats && (
        <div className="rounded-lg border border-slate-200 bg-white p-3 text-[11px]">
          <div className="font-semibold">Last run: <span className={stats.status === 'fault' || exceptionExit ? 'text-rose-700' : 'text-emerald-700'}>{exceptionExit ? 'exited with possible Windows access violation' : stats.status}</span>{stats.status === 'exited' ? ` with code ${stats.exitCode}${exceptionExit ? ` (0x${(stats.exitCode >>> 0).toString(16).padStart(8, '0')})` : ''}` : ''}</div>
          <div className="mt-1 text-slate-600">{stats.icount.toLocaleString()} instructions in {(stats.elapsedMs / 1000).toFixed(2)} s ({stats.mips.toFixed(1)} MIPS) · {stats.frames} frames presented · {stats.memMB.toFixed(1)} MB host memory reserved this session{stats.title ? ` · window “${stats.title}”` : ''}</div>
          {stats.status === 'fault' && <div className="mt-1 rounded bg-rose-50 p-2 font-mono text-[10px] text-rose-800" data-x64-fault>STOP at RIP {stats.errorRip}: {stats.error}<br />bytes: {stats.errorBytes}</div>}
          {stats.report && <pre className="mt-2 whitespace-pre-wrap rounded bg-amber-50 p-2 font-mono text-[10px] text-amber-900">{stats.report}</pre>}
          {jit && <div className="mt-1 text-slate-600" data-x64-jit={jit.enabled ? jit.modules : 'off'}>x86&rarr;wasm JIT: {jit.enabled ? `${jit.modules} regions (${(jit.bytes / 1024).toFixed(0)} KB wasm, compiled in ${jit.compileMs.toFixed(0)} ms${jit.failed ? `, ${jit.failed} failed` : ''})` : 'off'}{jit.stats ? <span className="font-mono text-[10px]"> &middot; {jit.stats}</span> : null}</div>}
          {apiStats && <details className="mt-2"><summary className="cursor-pointer text-slate-500">Win32 API calls ({apiStats.split(',').length})</summary><div className="mt-1 break-words font-mono text-[10px] text-slate-600">{apiStats.split(',').join('  ')}</div></details>}
          {log.length > 0 && <details className="mt-2"><summary className="cursor-pointer text-slate-500">Emulator log ({log.length})</summary><pre className="mt-1 max-h-40 overflow-auto font-mono text-[10px]">{log.join('\n')}</pre></details>}
        </div>
      )}
      <div className="rounded-lg border border-slate-200 bg-slate-50 p-3 text-[10px] leading-relaxed text-slate-600">
        <b>What this is:</b> a user-mode x86-64 / x86 interpreter (integer ISA + SSE2, PE32+ and PE32 loader, 32-bit processes in IA-32 compatibility mode, kernel32/UCRT/user32/gdi32 shims) compiled to wasm64 and drawing through WebGL. Programs get a persistent C:\\ drive (Files tab), stdin from the console input line, and a real environment block. x86 (32-bit) and x86-64 programs, threads, child processes, pipes, the program's own DLLs, .NET (through the installed runtime pack), Direct3D 11 and OpenGL (both translated to this device's WebGL2) and audio work; what is still missing is said honestly: Direct3D 9/12, Vulkan, networking, the OpenGL fixed-function pipeline, geometry/tessellation shaders — such programs stop at the first missing API, and the report above tells you which one.
      </div>
    </div>
  );
}
