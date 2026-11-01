// x64 Runner — runs real Windows x86-64 executables inside Nebula OS through the wasm64 emulator (emu/x64) and presents them with WebGL.
import { useCallback, useEffect, useRef, useState } from 'react';
import { Cpu, FolderOpen, FolderInput, Play, Square, Terminal as TerminalIcon, Monitor, FileSearch, AlertTriangle, CheckCircle2, HelpCircle, XCircle, Gamepad2, HardDrive, Download, Trash2, SendHorizontal, Keyboard } from 'lucide-react';
import { X64Runtime, vkFromEvent, wasm64Supported, type PeInfo, type RunStats, type VfsEntry } from '../../lib/x64/runtime';
import { FramePresenter } from '../../lib/x64/present';
import { takeX64Launch, onX64Launch, type X64Launch } from '../../lib/x64/launch';

interface ExeEntry { id: number; name: string; bytes: Uint8Array; info: PeInfo; source: 'sample' | 'file'; pending?: string[]; path?: string }   // path: guest path when not C:\nebula\<name>   // pending: companion files fetched on first Run (big runtimes)

const SAMPLE_DATA = ['demo.sql', 'demo.json', 'demo.lua', 'demo.txt', 'demo.py', 'demo.bat'];   // small inputs for the real apps, placed in C:\nebula
const PY_FILES = ['python312.dll', 'python312.zip', 'python312._pth', '_sqlite3.pyd', 'sqlite3.dll', '_decimal.pyd', '_ctypes.pyd', 'libffi-8.dll', '_bz2.pyd', '_lzma.pyd', 'pyexpat.pyd', '_elementtree.pyd', 'unicodedata.pyd', '_queue.pyd', '_socket.pyd', 'select.pyd', '_asyncio.pyd', '_zoneinfo.pyd', '_uuid.pyd', '_multiprocessing.pyd'];
const SAMPLES: { name: string; href: string; desc: string; args?: string; files?: string[]; lazy?: boolean; needsPack?: boolean; dir?: string }[] = [   // dir: subdirectory of C:\nebula for the exe + files
  { name: 'NebulaStrike.exe', href: '/downloads/NebulaStrike.exe', desc: 'Win32 GDI game, PE32+ x64 (light GUI exe)' },
  { name: 'd3d11tri.exe', href: '/x64/samples/d3d11tri.exe', desc: 'Direct3D 11 → WebGL2: device + swap chain, vertex/index/constant buffers, input layout, precompiled DXBC vertex/pixel shaders translated to GLSL, Draw + DrawIndexed + Present (3 frames). Args: number of frames', args: '3' },
  { name: 'd3d11clear.exe', href: '/x64/samples/d3d11clear.exe', desc: 'Direct3D 11: D3D11CreateDeviceAndSwapChain, GetBuffer, CreateRenderTargetView, ClearRenderTargetView + Present in three colours' },
  { name: 'msvcp.exe', href: '/x64/samples/msvcp.exe', needsPack: true, desc: 'Runtime pack test: loads the REAL msvcp140.dll (Microsoft Visual C++ 2015–2022 Redistributable, served from /x64/sys32 as C:\\Windows\\System32) and drives std::cout through its exports — static init, locale facets and iostream output of a vendor runtime DLL on top of the emulator\'s UCRT' },
  { name: 'nebula-dotnet-test.exe', href: '/x64/samples/dotnet/nebula-dotnet-test.exe', desc: '.NET 10 (C#): the OFFICIAL Microsoft .NET Runtime (CoreCLR + RyuJIT, win-x64) runs on the emulator — installed on demand into C:\\Program Files\\dotnet (the .NET pack, ~34 MB). apphost launcher + nebula-dotnet-test.dll. Args pick the steps: linq, throw, nre (hardware null-reference → managed exception), json, reflect, regex, culture, async, thread, taskrun, bgspin (GC suspends a spinning thread), proc (Process.Start cmd.exe), http (HttpClient), gc', args: 'linq-throw-nre-json-reflect-regex-gc', files: ['nebula-dotnet-test.dll', 'nebula-dotnet-test.runtimeconfig.json', 'nebula-dotnet-test.deps.json'], dir: 'dotnet' },
  { name: 'fx.exe', href: '/x64/samples/dotnet/fx.exe', desc: 'Classic .NET Framework 4.8 console exe (AnyCPU, IL only, 5 KB): started directly, the emulator hands it to dotnet.exe with a generated runtimeconfig — it runs on the .NET 10 runtime in compatibility mode (LINQ, exceptions, File IO, args). Exit code 5', args: 'a b', dir: 'dotnet' },
  { name: 'hello.exe', href: '/x64/samples/hello.exe', desc: 'UCRT console: printf/qsort/malloc/math (light console exe)' },
  { name: 'cmd.exe', href: '/x64/apps/cmd.exe', desc: 'Command Prompt: the emulator\'s own cmd.exe (52 KB, kernel32 only; the same binary is embedded at C:\\Windows\\System32\\cmd.exe so system(), _popen and subprocess(shell=True) work). Default args run demo.bat and then stay interactive — type dir, echo, set, if, for, pipes and redirections into the console, or start any program on the virtual disk (jq.exe, python.exe …). Try: /c "dir /b | sort /r" · /c "python.exe -c print(42) | findstr 42"', args: '/k demo.bat' },
  { name: 'powershell.exe', href: '/x64/apps/powershell.exe', desc: 'Windows PowerShell subset: the emulator\'s own powershell.exe (91 KB, kernel32 only; embedded at C:\\Windows\\System32\\WindowsPowerShell\\v1.0 so installers that shell out to PowerShell work — opencode unpacks ripgrep with Expand-Archive through it). Cmdlets: Expand-Archive / [IO.Compression.ZipFile]::ExtractToDirectory (deflate + stored), Get-FileHash, Get-ChildItem, Get/Set-Content, Select-String, Where/Select/Measure-Object, Test-Path, Join-Path, New/Copy/Remove-Item, $env:, -f, -join, try/catch, if/foreach/while, & external programs, exit N. Not implemented: objects/properties beyond strings and numbers, modules, classes, .NET beyond ZipFile/Path.', args: '-NoProfile -File demo.ps1' },
  { name: 'bench.exe', href: '/x64/samples/bench.exe', desc: 'CPU benchmark: sieve, mandelbrot, memcpy (heavy compute)' },
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
  likely:      { label: 'should run',   cls: 'bg-emerald-100 text-emerald-800 border-emerald-300', Icon: CheckCircle2 },
  partial:     { label: 'partial',      cls: 'bg-amber-100 text-amber-800 border-amber-300',       Icon: HelpCircle },
  unlikely:    { label: 'unlikely',     cls: 'bg-orange-100 text-orange-800 border-orange-300',    Icon: AlertTriangle },
  unsupported: { label: 'unsupported',  cls: 'bg-rose-100 text-rose-800 border-rose-300',          Icon: XCircle },
  invalid:     { label: 'not a PE',     cls: 'bg-slate-100 text-slate-700 border-slate-300',       Icon: XCircle },
};

const TOUCH_KEYS: { label: string; vk: number }[] = [
  { label: '◀', vk: 0x25 }, { label: '▶', vk: 0x27 }, { label: '▲', vk: 0x26 }, { label: '▼', vk: 0x28 }, { label: 'SPACE', vk: 0x20 }, { label: 'R', vk: 0x52 }, { label: 'ENTER', vk: 0x0d }, { label: 'ESC', vk: 0x1b },
];

/* Runtime pack: the x64 DLLs of the VC++ 2015–2022 Redistributable, built by emu/tools/runtime-pack.sh into public/x64/sys32/
   at build time and installed on demand into C:\Windows\System32 of the virtual disk (the emulator prefers these real
   vendor-runtime DLLs over its built-in shims; core OS DLLs stay built-in). */
type PackManifest = { pack: string; files: { name: string; size: number; version: string }[] };
type PackState = { status: 'probing' | 'available' | 'installing' | 'installed' | 'unavailable'; manifest?: PackManifest; done?: number; note?: string };
const PACK_URL = '/x64/sys32/';
const PACK_BUILTIN = new Set(['vcruntime140.dll']);   // shipped in the pack for completeness, but the emulator always uses its own implementation (pe_dll_native_ok)
const exeWantsPack = (dlls: string[] | undefined, man: PackManifest | undefined) => !!man && !!dlls && dlls.some(d => !PACK_BUILTIN.has(d.toLowerCase()) && man.files.some(f => f.name.toLowerCase() === d.toLowerCase()));

/* .NET pack: the official Microsoft .NET Runtime 10 (win-x64), built by emu/tools/dotnet-pack.sh into public/x64/dotnet/ (per-file gzip)
   and installed on demand into C:\Program Files\dotnet when a managed exe/dll or an apphost is started. */
type DotnetManifest = { pack: string; version: string; files: { path: string; size: number; gz: string; gzsize: number }[] };
type DotnetState = { status: 'probing' | 'available' | 'installing' | 'installed' | 'unavailable'; manifest?: DotnetManifest; done?: number; note?: string };
const DOTNET_URL = '/x64/dotnet/';
const DOTNET_DIR = 'C:\\Program Files\\dotnet\\';
async function fetchGz(url: string, attempt = 0): Promise<Uint8Array> {   // some static servers (vite preview, nginx gzip_static) send .gz files with Content-Encoding: gzip → the browser already inflated them
  const ac = new AbortController(); const timer = setTimeout(() => ac.abort(), 90_000); let raw: Uint8Array<ArrayBuffer>;
  try { const r = await fetch(url, { signal: ac.signal }); if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`); raw = new Uint8Array(await r.arrayBuffer()); }
  catch (e) { clearTimeout(timer); if (attempt < 2) return fetchGz(url, attempt + 1); throw new Error(`${url}: ${ac.signal.aborted ? 'timed out' : String(e)}`); }
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
  const [tab, setTab] = useState<'screen' | 'console' | 'files' | 'report'>('screen');
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
  const [moduleKB, setModuleKB] = useState(0);
  const [apiStats, setApiStats] = useState('');
  const [frames, setFrames] = useState(0);
  const nextId = useRef(1);
  const consoleBuf = useRef('');

  const launchByName = useCallback((rt: X64Runtime, list: ExeEntry[], want: X64Launch) => {
    const e = list.find(x => x.name.toLowerCase() === want.exe.toLowerCase()); if (!e || e.pending) return;
    if (rt.status === 'running' || rt.status === 'waiting-stdin') rt.stop();
    setSelected(e.id); setArgs(want.args); consoleBuf.current = ''; setConsoleText(''); setLog([]);
    const err = rt.start(e.bytes, e.name, want.args.trim() ? `${e.name} ${want.args.trim()}` : e.name);
    if (err) setLog(l => [...l, 'LOADER: ' + err]);
    setStats(rt.stats()); setFiles(rt.vfsList()); setTab('console');
  }, []);

  // init runtime once
  useEffect(() => {
    let alive = true;
    if (!wasm64Supported()) return;
    const rt = new X64Runtime(); rtRef.current = rt;
    rt.onConsole = (_fd, text) => { consoleBuf.current = (consoleBuf.current + text).slice(-60000); setConsoleText(consoleBuf.current); };
    rt.onLog = (text) => setLog(l => [...l.slice(-199), text.trimEnd()]);
    rt.onStdinWait = (w) => { setWaitingStdin(w); if (w) { setTab('console'); setTimeout(() => stdinRef.current?.focus(), 30); } };
    rt.onPresent = (px, w, h) => { presenterRef.current?.present(px, w, h); setFrames(f => f + 1); };
    rt.onStop = (s) => { setStats(s); setApiStats(rt.apiStats()); setFiles(rt.vfsList()); if (s.status !== 'exited' || !s.hasWindow) setTab(t => (s.hasWindow && s.status === 'exited') ? t : (s.status === 'fault' ? 'report' : t === 'screen' && !s.hasWindow ? 'console' : t)); };
    rt.init().then(async () => {
      if (!alive) return;
      // preload samples (best effort)
      const loaded: ExeEntry[] = [];
      for (const s of SAMPLES) {
        try {
          const r = await fetch(s.href); if (!r.ok) continue; const bytes = new Uint8Array(await r.arrayBuffer());
          const d = s.href.slice(0, s.href.lastIndexOf('/') + 1);
          const gd = 'C:\\nebula\\' + (s.dir ? s.dir + '\\' : '');
          loaded.push({ id: nextId.current++, name: s.name, bytes, info: rt.inspect(bytes), source: 'sample', pending: s.lazy && s.files ? s.files.map(f => d + f) : undefined, path: s.dir ? gd + s.name : undefined });
          rt.vfsAdd(gd + s.name, bytes);   // on the virtual disk too, so busybox sh can chain them and DLLs find their exe
          if (!s.lazy) for (const f of s.files ?? []) { const rf = await fetch(d + f); if (rf.ok) rt.vfsAdd(gd + f, new Uint8Array(await rf.arrayBuffer())); }
        } catch { /* sample missing */ }
      }
      for (const f of SAMPLE_DATA) { try { const r = await fetch('/x64/apps/' + f); if (r.ok) rt.vfsAdd('C:\\nebula\\' + f, new Uint8Array(await r.arrayBuffer())); } catch { /* optional */ } }
      if (!alive) return;
      rt.vfsAdd('C:\\nebula\\readme.txt', new TextEncoder().encode('Nebula x64 virtual disk\nThis file lives in C:\\nebula\\readme.txt inside the emulator.\nTry: busybox64.exe cat readme.txt | wc -l | sort | sha256sum readme.txt\n'));
      setFiles(rt.vfsList());
      setModuleKB(rt.moduleBytes / 1024);
      setExes(loaded); if (loaded.length) setSelected(loaded[0].id);
      setReady('ready');
      const want = takeX64Launch();   // another app (the Command Prompt's `real` command) asked us to start a program right away
      if (want) launchByName(rt, loaded, want);
    }).catch(e => { if (alive) { setInitError(String(e)); setReady('error'); } });
    return () => { alive = false; rt.stop(); };
  }, [launchByName]);

  // launch requests from other apps while we are open (Command Prompt → `real`)
  useEffect(() => onX64Launch(want => { const rt = rtRef.current; if (rt && ready === 'ready') launchByName(rt, exes, want); }), [ready, exes, launchByName]);

  // presenter bound to the canvas
  useEffect(() => {
    const c = canvasRef.current; if (!c) return;
    if (presenterRef.current?.canvas === c) return;   // the canvas is re-created whenever the Screen tab is left and re-entered
    try { const p = new FramePresenter(c); presenterRef.current = p; setGlInfo(`${p.api.toUpperCase()} · ${p.renderer}`); } catch (e) { setGlInfo('WebGL unavailable: ' + String(e)); return; }
    const last = rtRef.current?.lastPresented(); if (last) presenterRef.current.present(last.px, last.w, last.h);   // show the final frame of a finished program
  }, [ready, tab]);

  // stats ticker while running
  useEffect(() => {
    const t = setInterval(() => { const rt = rtRef.current; if (rt && (rt.status === 'running' || rt.status === 'waiting-stdin')) setStats(rt.stats()); }, 500);
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
  const addFolder = useCallback(async (list: FileList | File[]) => {
    const rt = rtRef.current; if (!rt) return;
    const added: ExeEntry[] = []; let n = 0;
    for (const f of Array.from(list)) {
      const rel = ((f as File & { webkitRelativePath?: string }).webkitRelativePath || f.name).split('/').filter(Boolean);
      const parts = rel.length > 1 ? rel.slice(1) : rel; const path = 'C:\\nebula\\' + parts.join('\\');
      const bytes = new Uint8Array(await f.arrayBuffer());
      if (rt.vfsAdd(path, bytes)) n++;
      if (/\.exe$/i.test(f.name)) added.push({ id: nextId.current++, name: parts.join('\\'), bytes, info: rt.inspect(bytes), source: 'file' });
    }
    setExes(l => [...l, ...added]); if (added.length) setSelected(added[0].id);
    setFiles(rt.vfsList()); setLog(l => [...l, `mounted ${n} file(s) under C:\\nebula\\`]); setTab('files');
  }, []);
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

  const [fetching, setFetching] = useState<string | null>(null);
  const [pack, setPack] = useState<PackState>({ status: 'probing' });
  useEffect(() => { let alive = true; (async () => {
    try { const r = await fetch(PACK_URL + 'manifest.json'); if (!r.ok) throw new Error('HTTP ' + r.status); const man = await r.json() as PackManifest; if (alive) setPack({ status: 'available', manifest: man }); }
    catch (e) { if (alive) setPack({ status: 'unavailable', note: 'not built into this deployment (' + String(e) + ')' }); }
  })(); return () => { alive = false; }; }, []);
  const installPack = useCallback(async (rt: X64Runtime): Promise<boolean> => {
    if (pack.status === 'installed') return true;
    if (pack.status !== 'available' || !pack.manifest) { setLog(l => [...l, 'RUNTIME PACK: ' + (pack.note ?? pack.status)]); return false; }
    const man = pack.manifest; let done = 0, bytes = 0;
    setPack({ status: 'installing', manifest: man, done: 0 }); setFetching(`installing runtime pack (${man.files.length} DLLs)…`);
    try {
      for (const f of man.files) {
        const r = await fetch(PACK_URL + f.name); if (!r.ok) throw new Error(`${f.name}: HTTP ${r.status}`);
        const b = new Uint8Array(await r.arrayBuffer()); bytes += b.length; done++;
        rt.vfsAdd('C:\\Windows\\System32\\' + f.name, b);
        setPack({ status: 'installing', manifest: man, done }); setFetching(`installing runtime pack: ${done}/${man.files.length}, ${(bytes / 1048576).toFixed(1)} MB`);
      }
    } catch (e) { setFetching(null); setPack({ status: 'available', manifest: man, note: String(e) }); setLog(l => [...l, 'RUNTIME PACK: ' + String(e)]); return false; }
    setFetching(null); setPack({ status: 'installed', manifest: man, done }); setFiles(rt.vfsList());
    setLog(l => [...l, `runtime pack installed: ${done} DLLs, ${(bytes / 1048576).toFixed(1)} MB → C:\\Windows\\System32 (${man.pack})`]);
    return true;
  }, [pack]);
  const [dotnet, setDotnet] = useState<DotnetState>({ status: 'probing' });
  useEffect(() => { let alive = true; (async () => {
    try { const r = await fetch(DOTNET_URL + 'manifest.json'); if (!r.ok) throw new Error('HTTP ' + r.status); const man = await r.json() as DotnetManifest; if (alive) setDotnet({ status: 'available', manifest: man }); }
    catch (e) { if (alive) setDotnet({ status: 'unavailable', note: 'not built into this deployment (' + String(e) + ')' }); }
  })(); return () => { alive = false; }; }, []);
  const installDotnet = useCallback(async (rt: X64Runtime): Promise<boolean> => {
    if (dotnet.status === 'installed') return true;
    if (dotnet.status !== 'available' || !dotnet.manifest) { setLog(l => [...l, '.NET PACK: ' + (dotnet.note ?? dotnet.status)]); return false; }
    const man = dotnet.manifest; let done = 0, bytes = 0;
    setDotnet({ status: 'installing', manifest: man, done: 0 }); setFetching(`installing .NET pack (${man.files.length} files)…`); setTab('console');
    try {
      const total = man.files.reduce((a, f) => a + f.size, 0);
      if (!rt.reserve(total + (total >> 4) + (1 << 20))) throw new Error(`cannot grow guest memory by ${(total / 1048576).toFixed(0)} MB`);   // one memory.grow, not one per file
      const queue = man.files.slice(); const PAR = 6; const t0 = performance.now(); const inflight = new Set<string>(); let failed = false;
      const label = () => `installing .NET pack: ${done}/${man.files.length}, ${(bytes / 1048576).toFixed(1)} MB` + (done + inflight.size >= man.files.length ? ` — waiting for ${[...inflight].join(', ')}` : '');
      const worker = async () => { for (;;) { const f = queue.shift(); if (!f || failed) return; inflight.add(f.gz.slice(f.gz.lastIndexOf('/') + 1));
        let b: Uint8Array;
        try { b = await fetchGz(DOTNET_URL + f.gz); } catch (e) { failed = true; throw e; }
        if (failed) return;   // another worker failed meanwhile: do not touch the state again (it already says 'available' + the error)
        if (b.length !== f.size) { failed = true; throw new Error(`${f.path}: ${b.length} bytes, expected ${f.size}`); }
        if (!rt.vfsAdd(DOTNET_DIR + f.path, b)) { failed = true; throw new Error(`${f.path}: virtual disk refused the file`); }
        bytes += b.length; done++; inflight.delete(f.gz.slice(f.gz.lastIndexOf('/') + 1));
        if (done % 50 === 0) setLog(l => [...l, `.NET pack: ${done}/${man.files.length} files, ${(bytes / 1048576).toFixed(1)} MB in ${((performance.now() - t0) / 1000).toFixed(1)} s`]);
        setDotnet({ status: 'installing', manifest: man, done }); setFetching(label()); } };
      await Promise.all(Array.from({ length: PAR }, worker));
    } catch (e) { setFetching(null); setDotnet({ status: 'available', manifest: man, note: String(e) }); setLog(l => [...l, '.NET PACK: ' + String(e)]); return false; }
    setFetching(null); setDotnet({ status: 'installed', manifest: man, done }); setFiles(rt.vfsList());
    setLog(l => [...l, `.NET pack installed: ${done} files, ${(bytes / 1048576).toFixed(1)} MB → ${DOTNET_DIR} (${man.pack})`]);
    return true;
  }, [dotnet]);
  const run = useCallback(async () => {
    const rt = rtRef.current; if (!rt || !current) return;
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
    const sample = current.source === 'sample' ? SAMPLES.find(x => x.name === current.name) : undefined;
    if (pack.status !== 'installed' && (sample?.needsPack || exeWantsPack(current.info.dlls, pack.manifest))) await installPack(rt);   // the program imports a vendor-runtime DLL we ship
    if (current.info.dotnet && dotnet.status !== 'installed' && !(await installDotnet(rt))) {   // managed code needs the real runtime
      setLog(l => [...l, `${current.name} is a .NET program (${current.info.dotnet}): the .NET runtime pack could not be installed, so it cannot run here`]); setStats(rt.stats()); setTab('report'); return;
    }
    const base = current.name.split('\\').pop() || current.name;
    const err = rt.start(current.bytes, current.path ?? base, args.trim() ? `${base} ${args.trim()}` : base);
    if (err) { setLog(l => [...l, 'LOADER: ' + err]); setStats(rt.stats()); setTab('report'); return; }
    setStats(rt.stats()); setFiles(rt.vfsList());
    setTab(current.info.subsystem === 'GUI' ? 'screen' : 'console');
    setTimeout(() => screenRef.current?.focus(), 50);
  }, [current, args, pack, installPack, dotnet, installDotnet]);
  const stop = useCallback(() => { const rt = rtRef.current; if (!rt) return; rt.stop(); setStats(rt.stats()); setFiles(rt.vfsList()); }, []);

  // keyboard → guest
  const guestKey = useCallback((e: React.KeyboardEvent, down: boolean) => {
    const rt = rtRef.current; if (!rt || rt.status !== 'running') return;
    const vk = vkFromEvent(e.nativeEvent); if (!vk) return;
    rt.key(vk, down); e.preventDefault(); e.stopPropagation();
  }, []);
  const touchKey = useCallback((vk: number, down: boolean) => { rtRef.current?.key(vk, down); }, []);
  const guestMouse = useCallback((e: React.MouseEvent, ev: number) => {
    const rt = rtRef.current, p = presenterRef.current; if (!rt || !p || rt.status !== 'running') return;
    const r = p.canvas.getBoundingClientRect(); const { w, h } = rt.screenSize(); const g = p.toGuest(e.clientX - r.left, e.clientY - r.top, w, h); if (!g) return;
    rt.mouse(g.x, g.y, ev); if (ev) screenRef.current?.focus();
  }, []);

  const V = (v?: string) => VERDICT[v ?? 'invalid'] ?? VERDICT.invalid;

  return (
    <div className="flex h-full flex-col bg-[#f3f3f3] text-[12px] text-[#1b1b1b]" data-x64-status={stats?.status ?? 'idle'}>
      {/* toolbar */}
      <div className="flex h-11 shrink-0 items-center gap-2 border-b border-slate-200 bg-white px-3">
        <Cpu size={16} className="text-indigo-600" />
        <span className="font-semibold">x64 Runner</span>
        <span className="hidden text-[10px] text-slate-400 sm:inline">x86-64 · PE32+ · wasm64 + WebGL</span>
        <label className="ml-3 flex cursor-pointer items-center gap-1 rounded border border-slate-300 bg-white px-2 py-1 hover:bg-slate-50">
          <FolderOpen size={13} /> Open .exe…
          <input type="file" accept=".exe,application/x-msdownload,application/octet-stream" multiple className="hidden" data-x64-file onChange={e => { if (e.target.files) void addFiles(e.target.files); e.target.value = ''; }} />
        </label>
        <label className="flex cursor-pointer items-center gap-1 rounded border border-slate-300 bg-white px-2 py-1 hover:bg-slate-50" title="Mount a whole folder as the program's disk (C:\\nebula\\)">
          <FolderInput size={13} /> Open folder…
          <input type="file" multiple className="hidden" data-x64-folder {...({ webkitdirectory: '', directory: '' } as Record<string, string>)} onChange={e => { if (e.target.files) void addFolder(e.target.files); e.target.value = ''; }} />
        </label>
        <input value={args} onChange={e => setArgs(e.target.value)} placeholder="arguments" className="w-32 rounded border border-slate-300 px-2 py-1 text-[11px] outline-none focus:border-indigo-400" />
        <button data-x64-run disabled={!current || ready !== 'ready' || !!running || !!fetching} onClick={() => { void run(); }} className="flex items-center gap-1 rounded bg-indigo-600 px-3 py-1 font-semibold text-white disabled:opacity-40 hover:bg-indigo-700"><Play size={12} /> Run</button>
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
        <div className="ml-auto flex items-center gap-1 rounded bg-slate-100 p-0.5">
          {(['screen', 'console', 'files', 'report'] as const).map(t => (
            <button key={t} data-x64-tab={t} onClick={() => setTab(t)} className={`flex items-center gap-1 rounded px-2 py-1 capitalize ${tab === t ? 'bg-white shadow-sm font-semibold' : 'text-slate-500 hover:text-slate-800'}`}>
              {t === 'screen' ? <Monitor size={12} /> : t === 'console' ? <TerminalIcon size={12} /> : t === 'files' ? <HardDrive size={12} /> : <FileSearch size={12} />}{t}
            </button>
          ))}
        </div>
      </div>

      <div className="flex min-h-0 flex-1">
        {/* file list */}
        <div className="flex w-56 shrink-0 flex-col border-r border-slate-200 bg-white" onDragOver={e => e.preventDefault()} onDrop={e => { e.preventDefault(); void addFiles(e.dataTransfer.files); }}>
          <div className="border-b border-slate-100 px-3 py-1.5 text-[10px] font-semibold uppercase tracking-wide text-slate-400">Executables ({exes.length})</div>
          <div className="min-h-0 flex-1 overflow-y-auto">
            {ready === 'loading' && <div className="p-3 text-slate-400">Loading wasm64 emulator…</div>}
            {ready === 'unsupported' && <div className="p-3 text-rose-700">This browser has no WebAssembly memory64 (wasm64). Chrome/Edge ≥ 133, Firefox ≥ 134 or Android WebView ≥ 133 is required.</div>}
            {ready === 'error' && <div className="p-3 text-rose-700">Emulator failed to load: {initError}</div>}
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
        </div>

        {/* main */}
        <div className="flex min-w-0 flex-1 flex-col">
          {tab === 'screen' && (
            <div ref={screenRef} tabIndex={0} onKeyDown={e => guestKey(e, true)} onKeyUp={e => guestKey(e, false)} className="relative flex min-h-0 flex-1 items-center justify-center bg-black outline-none" onClick={() => screenRef.current?.focus()}>
              <canvas ref={canvasRef} data-x64-screen data-frames={frames} className="h-full w-full" style={{ imageRendering: 'pixelated' }}
                onMouseMove={e => guestMouse(e, 0)} onMouseDown={e => guestMouse(e, e.button === 2 ? 3 : 1)} onMouseUp={e => guestMouse(e, e.button === 2 ? 4 : 2)} onContextMenu={e => e.preventDefault()} />
              {!running && frames === 0 && (
                <div className="pointer-events-none absolute inset-0 flex flex-col items-center justify-center text-center text-slate-400">
                  <Monitor size={40} strokeWidth={1} />
                  <div className="mt-2 text-[12px]">{current ? `Press Run to start ${current.name}` : 'Select or open an executable'}</div>
                  <div className="mt-1 text-[10px] text-slate-500">GUI windows render here through WebGL; console programs write to the Console tab.</div>
                </div>
              )}
              {running && stats?.hasWindow && (
                <div className={`absolute bottom-2 left-1/2 flex -translate-x-1/2 gap-1 rounded-full bg-black/50 px-2 py-1 ${touch ? '' : 'opacity-0 hover:opacity-100'}`} onMouseDown={e => e.stopPropagation()}>
                  <Gamepad2 size={14} className="my-auto text-white/60" />
                  {TOUCH_KEYS.map(k => (
                    <button key={k.vk} className="min-w-9 select-none rounded-full bg-white/15 px-2 py-1.5 text-[11px] font-bold text-white active:bg-indigo-500"
                      onPointerDown={e => { e.preventDefault(); e.currentTarget.setPointerCapture(e.pointerId); touchKey(k.vk, true); }}
                      onPointerUp={e => { e.preventDefault(); touchKey(k.vk, false); }}
                      onPointerCancel={() => touchKey(k.vk, false)}>{k.label}</button>
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
                <HardDrive size={13} className="text-indigo-600" /> <b>Virtual disk</b> <span className="text-slate-500">— in-memory C:\ seen by the program (cwd C:\nebula). Files written by programs appear here and can be downloaded.</span>
                <label className="ml-auto flex cursor-pointer items-center gap-1 rounded border border-slate-300 bg-white px-2 py-0.5 hover:bg-slate-50"><FolderOpen size={12} /> Add files…<input type="file" multiple className="hidden" data-x64-addfiles onChange={e => { if (e.target.files) void addDataFiles(e.target.files); e.target.value = ''; }} /></label>
                <button onClick={() => setFiles(rtRef.current?.vfsList() ?? [])} className="rounded border border-slate-300 bg-white px-2 py-0.5 hover:bg-slate-50">Refresh</button>
              </div>
              <div className="min-h-0 flex-1 overflow-auto">
                {files.length === 0 && <div className="p-4 text-slate-400">Empty. Use “Open folder…” to mount a directory, drop files here, or run a program that creates files.</div>}
                <table className="w-full text-[11px]">
                  <tbody>
                    {files.map(f => (
                      <tr key={f.path} data-x64-vfs={f.path} className="border-b border-slate-100 hover:bg-indigo-50/40">
                        <td className="px-3 py-1 font-mono">{f.dir ? '📁 ' : ''}{f.path}{!!f.written && !f.dir && <span className="ml-2 rounded bg-emerald-100 px-1 text-[9px] font-semibold text-emerald-800">written</span>}</td>
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
              {current ? <PeReport e={current} stats={stats} log={log} apiStats={apiStats} /> : <div className="text-slate-400">Select an executable to see its compatibility report.</div>}
            </div>
          )}
          {/* status bar */}
          <div className="flex h-7 shrink-0 items-center gap-3 border-t border-slate-200 bg-white px-3 text-[10px] text-slate-600">
            <span className={`font-semibold ${stats?.status === 'fault' ? 'text-rose-700' : stats?.status === 'running' ? 'text-emerald-700' : stats?.status === 'waiting-stdin' ? 'text-amber-700' : ''}`}>{(stats?.status ?? 'idle').toUpperCase()}{stats?.status === 'exited' ? ` (code ${stats.exitCode})` : ''}</span>
            {stats && <span>{(stats.icount / 1e6).toFixed(1)} M insns · {stats.mips.toFixed(1)} MIPS · {stats.frames} frames · {stats.memMB.toFixed(0)} MB guest{stats.procs > 1 ? <span data-x64-procs={stats.procs}> · {stats.procs} processes</span> : null}{stats.threads > 1 ? <span data-x64-threads={stats.threads}> · {stats.threads} threads</span> : null}</span>}
            <span className="ml-auto truncate">{glInfo}</span>
            <span className="shrink-0">wasm64 {moduleKB ? moduleKB.toFixed(0) + ' KB' : ''}</span>
          </div>
        </div>
      </div>
    </div>
  );
}

function PeReport({ e, stats, log, apiStats }: { e: ExeEntry; stats: RunStats | null; log: string[]; apiStats: string }) {
  const i = e.info; const v = VERDICT[i.ok ? (i.verdict ?? 'invalid') : 'invalid'] ?? VERDICT.invalid;
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
              <dt className="text-slate-500">Architecture</dt><dd>{i.arch} (machine 0x{(i.machine ?? 0).toString(16)}){i.pe32plus ? ', PE32+' : ', PE32'}</dd>
              <dt className="text-slate-500">Subsystem</dt><dd>{i.subsystem}{i.dll ? ' · DLL' : ''}{i.clr ? ' · .NET' : ''}</dd>
              <dt className="text-slate-500">Image</dt><dd>base {i.imageBase}, {(i.sizeOfImage ?? 0 / 1024).toLocaleString()} bytes, entry {i.entry}, {i.sections} sections ({i.sectionNames?.join(' ')})</dd>
              <dt className="text-slate-500">Features</dt><dd>{[i.relocs && 'relocations', i.tls && 'TLS', i.exceptions && 'SEH tables', (i.resources ?? 0) > 0 && 'resources'].filter(Boolean).join(', ') || '—'}</dd>
              <dt className="text-slate-500">Imports</dt><dd>{i.imports} total · {i.knownImports} implemented · <span className={(i.unknownImportsCount ?? 0) ? 'font-semibold text-rose-700' : ''}>{i.unknownImportsCount} missing</span></dd>
              {!!i.privateDllImports && (<><dt className="text-slate-500">Own DLLs</dt><dd>{i.privateDllImports} import(s) come from the program's own DLLs — put those .dll files next to the exe (same folder / C:\nebula) and they are loaded like on Windows: static imports, LoadLibrary, GetProcAddress, DllMain, per-thread TLS.</dd></>)}
              <dt className="text-slate-500">DLLs</dt><dd className="break-words">{i.dlls?.join(', ')}</dd>
              {!!i.unknownImports && (<><dt className="text-slate-500">Missing APIs</dt><dd className="break-words font-mono text-[10px] text-rose-700">{i.unknownImports}</dd></>)}
            </dl>
          </>
        ) : <div className="mt-1 text-rose-700">{i.error}</div>}
      </div>
      {stats && (
        <div className="rounded-lg border border-slate-200 bg-white p-3 text-[11px]">
          <div className="font-semibold">Last run: <span className={stats.status === 'fault' ? 'text-rose-700' : 'text-emerald-700'}>{stats.status}</span>{stats.status === 'exited' ? ` with code ${stats.exitCode}` : ''}</div>
          <div className="mt-1 text-slate-600">{stats.icount.toLocaleString()} instructions in {(stats.elapsedMs / 1000).toFixed(2)} s ({stats.mips.toFixed(1)} MIPS) · {stats.frames} frames presented · {stats.memMB.toFixed(1)} MB host memory reserved this session{stats.title ? ` · window “${stats.title}”` : ''}</div>
          {stats.status === 'fault' && <div className="mt-1 rounded bg-rose-50 p-2 font-mono text-[10px] text-rose-800" data-x64-fault>STOP at RIP {stats.errorRip}: {stats.error}<br />bytes: {stats.errorBytes}</div>}
          {stats.report && <pre className="mt-2 whitespace-pre-wrap rounded bg-amber-50 p-2 font-mono text-[10px] text-amber-900">{stats.report}</pre>}
          {apiStats && <details className="mt-2"><summary className="cursor-pointer text-slate-500">Win32 API calls ({apiStats.split(',').length})</summary><div className="mt-1 break-words font-mono text-[10px] text-slate-600">{apiStats.split(',').join('  ')}</div></details>}
          {log.length > 0 && <details className="mt-2"><summary className="cursor-pointer text-slate-500">Emulator log ({log.length})</summary><pre className="mt-1 max-h-40 overflow-auto font-mono text-[10px]">{log.join('\n')}</pre></details>}
        </div>
      )}
      <div className="rounded-lg border border-slate-200 bg-slate-50 p-3 text-[10px] leading-relaxed text-slate-600">
        <b>What this is:</b> a user-mode x86-64 interpreter (integer ISA + SSE2, PE32+ loader, kernel32/UCRT/user32/gdi32 shims) compiled to wasm64 and drawing through WebGL. Programs get an in-memory C:\ drive (Files tab), stdin from the console input line, and a real environment block. It is honest about its limits: 32-bit, .NET, Direct3D/OpenGL/Vulkan, audio and networking are not emulated (threads, child processes, pipes and the program's own DLLs are) — such programs stop at the first missing API, and the report above tells you which one.
      </div>
    </div>
  );
}
