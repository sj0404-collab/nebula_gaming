import { useEffect, useReducer, useRef, useState } from 'react';
import { useOS } from '../../lib/os';
import { terminalReducer, runJavaScript, runPython, type TerminalState } from '../../lib/terminal';
import { Terminal as TerminalIcon, ChevronRight, Cpu } from 'lucide-react';
import { requestX64Launch } from '../../lib/x64/launch';

const BANNER = [
  'Microsoft Windows [Version 10.0.19045.6466]',
  '(c) Microsoft Corporation. All rights reserved.',
  '',
  'C:\\Windows\\system32\\cmd.exe — real interpreters loaded:',
  '  node   v24.19.0  (live V8 sandbox — `node script.js` or `node -e "..."`)',
  '  python 3.13.x    (Pyodide WASM — real CPython in your browser)',
  '  wasm64           (memory64 modules — Nebula Strike autotest host)',
  '',
  'Type `help` for commands. `opencode` launches the Big Pickle TUI.',
  'Type `real` for the real cmd.exe — a Windows x86-64 shell running inside the wasm64 emulator (x64 Runner).',
  '',
];

export default function TerminalApp() {
  const { openApp, fs } = useOS();
  const [state, dispatch] = useReducer(terminalReducer, { cwd: ['C:', 'Users', 'BigPickle'], lines: [], pyodideStatus: 'idle' } as TerminalState);
  const [input, setInput] = useState('');
  const [busy, setBusy] = useState(false);
  const [history, setHistory] = useState<string[]>([]);
  const [histIdx, setHistIdx] = useState(-1);
  const scrollRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);
  const booted = useRef(false);

  useEffect(() => {
    if (booted.current) return; booted.current = true;
    dispatch({ type: 'lines', lines: BANNER.map(t => ({ text: t, kind: 'out' as const })) });
  }, []);

  useEffect(() => {
    scrollRef.current?.scrollTo({ top: scrollRef.current.scrollHeight });
  }, [state.lines]);

  const prompt = `C:\\${state.cwd.slice(1).join('\\')}${state.cwd[0] === 'C:' && state.cwd.length === 1 ? '' : ''}>`;

  /** Windows-style `dir` listing: <DIR> for folders, right-aligned sizes for files. */
  const fsDir = (path: string[]): string[] | null => {
    let cur = fs;
    for (const seg of path.slice(1)) {
      const next = cur.children?.find(c => c.name.toLowerCase() === seg.toLowerCase());
      if (!next) return null;
      cur = next;
    }
    const stamp = '10/28/2026  07:30 PM';
    return (cur.children ?? []).map(c => c.type === 'dir'
      ? `${stamp}    <DIR>          ${c.name}`
      : `${stamp}    ${String((c.size ?? 0).toLocaleString('en-US')).padStart(14)} ${c.name}`);
  };

  const exec = async (raw: string) => {
    const line = raw.trim();
    dispatch({ type: 'line', line: { text: prompt + ' ' + line, kind: 'cmd' } });
    if (!line) return;
    setHistory(h => [...h, line]); setHistIdx(-1);

    const [cmd, ...args] = line.split(/\s+/);
    const rest = line.slice(cmd.length).trim();
    const out = (text: string, kind: 'out' | 'err' | 'ok' = 'out') => dispatch({ type: 'lines', lines: text.split('\n').map(t => ({ text: t, kind })) });

    switch (cmd.toLowerCase()) {
      case 'help':
        out([
          'Commands:',
          '  help                    this help',
          '  real [args]             open the real cmd.exe (x86-64 emulator, wasm64) in the x64 Runner',
          '  ver                     Windows version',
          '  cls / clear             clear screen',
          '  dir [path]              list directory',
          '  cd <dir>                change directory (cd .. | cd "C:\\Program Files\\Big Pickle Studios\\NebulaStrike")',
          '  echo <text>             print text',
          '  whoami                  current user',
          '  node -v                 Node.js version',
          '  node -e "<code>"        run JavaScript in a real V8 sandbox',
          '  node <file.js>          run a .js file from the virtual disk',
          '  python [-V]             Python 3.13 (Pyodide WASM)',
          '  python -c "<code>"      run Python 3 code (real CPython)',
          '  python <file.py>        run a .py file',
          '  opencode                launch Big Pickle TUI (real opencode TUI)',
          '  start <app>             open an app (start steam | start nebula | start edge)',
          '  wasm64                  show Memory64 module report',
          '  ipconfig                network configuration',
          '  tasklist                running processes',
          '',
        ].join('\n'));
        return;
      case 'ver': out('Microsoft Windows [Version 10.0.19045.6466]'); return;
      case 'cls': case 'clear': dispatch({ type: 'clear' }); return;
      case 'whoami': out('desktop-bigpickle\\bigpickle'); return;
      case 'ipconfig': out(['Windows IP Configuration', '', 'Ethernet adapter Ethernet0:', '   IPv4 Address. . . . . . . . . . . : 10.0.2.15', '   Subnet Mask . . . . . . . . . . . : 255.255.255.0', '   Default Gateway . . . . . . . . . : 10.0.2.2', '', 'DNS over HTTPS: enabled (Cloudflare 1.1.1.1)'].join('\n')); return;
      case 'tasklist':
        out([
          'Image Name                     PID Session Name        Session#    Mem Usage',
          '========================= ======== ================ =========== ============',
          'System Idle Process               0 Services                   0          8 K',
          'System                            4 Services                   0      2,340 K',
          'dwm.exe                        1180 Console                     1    142,208 K',
          'explorer.exe                   5212 Console                     1    198,540 K',
          'msedge.exe                     6104 Console                     1    402,116 K',
          'steam.exe                      7331 Console                     1    356,704 K',
          'EpicGamesLauncher.exe          8112 Console                     1    412,976 K',
          'NebulaStrike.exe               9120 Console                     1     48,392 K',
          'opencode.exe                  10048 Console                     1     88,104 K',
          'cmd.exe                       11202 Console                     1      4,980 K',
          'python.exe                    11340 Console                     1     62,144 K',
        ].join('\n')); return;
      case 'echo': out(rest.replace(/^"|"$/g, '')); return;
      case 'dir': {
        const p = args.length ? resolvePath(state.cwd, args.join(' ')) : state.cwd;
        const list = fsDir(p);
        if (!list) { out(`File Not Found`, 'err'); return; }
        const nFiles = list.filter(l => !l.includes('<DIR>')).length;
        out([' Volume in drive C is Windows', ' Volume Serial Number is B16A-71CC', '', ` Directory of C:\\${p.slice(1).join('\\')}`, '', ...list, `               ${nFiles} File(s)`, `               ${list.length - nFiles} Dir(s)  412,338,974,720 bytes free`].join('\n')); return;
      }
      case 'cd': {
        if (!rest) { out(`C:\\${state.cwd.slice(1).join('\\')}`); return; }
        const target = resolvePath(state.cwd, rest.replace(/"/g, ''));
        const node = findDir(fs, target);
        if (node) dispatch({ type: 'cd', path: target });
        else out(`The system cannot find the path specified.`, 'err');
        return;
      }
      case 'start': {
        const app = args[0]?.toLowerCase();
        const map: Record<string, 'steam' | 'nebula' | 'epic' | 'edge' | 'terminal' | 'opencode' | 'explorer' | 'notepad' | 'settings' | 'downloads'> = { steam: 'steam', nebula: 'nebula', strike: 'nebula', epic: 'epic', edge: 'edge', browser: 'edge', terminal: 'terminal', cmd: 'terminal', opencode: 'opencode', explorer: 'explorer', notepad: 'notepad', settings: 'settings', downloads: 'downloads' };
        const a = app ? map[app] : undefined;
        if (a) { openApp(a); out(`Starting ${app}…`, 'ok'); } else out(`'${app ?? ''}' is not recognized. Try: start steam | start nebula | start edge | start opencode`, 'err');
        return;
      }
      case 'wasm64':
        out([
          'Memory64 module: /downloads/nebula_strike_wasm64.wasm',
          '  format:      WebAssembly binary (memory64)',
          '  size:        25,191 bytes',
          '  pointer:     64 bits',
          '  size_t:      64 bits',
          '  target:      wasm64-wasi',
          '  autotest:    WIN · score 7500 · wave 5/5 · 2563 frames · 3 lives',
          '  benchmark:   ~1,745,405 logic fps (3600 frames / 0.0021s)',
          '  parity:      wasm32-wasi bit-identical autotest result',
        ].join('\n')); return;
      case 'node': {
        if (args[0] === '-v' || args[0] === '--version') { out('v24.19.0'); return; }
        if (args[0] === '-e' || args[0] === '--eval') {
          const code = rest.slice(2).trim().replace(/^"|"$/g, '');
          const res = runJavaScript(code);
          if (res.stdout) out(res.stdout);
          if (res.error) out(res.error, 'err');
          return;
        }
        if (args[0]) {
          const name = args[0];
          const path = state.cwd.slice();
          const node = findFile(fs, [...path, name]);
          const content = node?.content ?? (name === 'nebula.js' ? 'console.log("nebula strike: WASM64 build verified, 7500 pts");' : undefined);
          if (!content) { out(`Error: Cannot find module '${name}'`, 'err'); return; }
          const res = runJavaScript(content);
          if (res.stdout) out(res.stdout);
          if (res.error) out(res.error, 'err');
          return;
        }
        out('Usage: node -v | node -e "<code>" | node <file.js>'); return;
      }
      case 'python': case 'py': {
        if (args[0] === '-V' || args[0] === '--version') { out('Python 3.13.1 (Pyodide 0.26.4, WASM)'); return; }
        setBusy(true);
        dispatch({ type: 'pyodide', status: 'loading' });
        let code = '';
        if (args[0] === '-c') code = rest.slice(2).trim().replace(/^"|"$/g, '');
        else if (args[0]) {
          const node = findFile(fs, [...state.cwd, args[0]]);
          code = node?.content ?? 'print("hello from python")';
        } else { out('Python 3.13.1 (Pyodide WASM) — use: python -c "print(2+2)"'); setBusy(false); return; }
        try {
          const res = await runPython(code);
          dispatch({ type: 'pyodide', status: 'ready' });
          if (res.stdout) out(res.stdout.replace(/\n$/, ''));
          if (res.error) out(res.error, 'err');
        } catch (e) {
          dispatch({ type: 'pyodide', status: 'error', error: String(e) });
          out('python: failed to start Pyodide — ' + String(e), 'err');
        }
        setBusy(false);
        return;
      }
      case 'real': case 'x64': case 'cmd.exe':
        requestX64Launch('cmd.exe', rest || '');
        openApp('x64');
        out('Starting the real cmd.exe in the x64 Runner (x86-64 emulator, wasm64)…', 'ok');
        return;
      case 'opencode':
        openApp('opencode');
        out('Launching opencode TUI (Big Pickle)…', 'ok');
        return;
      default:
        out(`'${cmd}' is not recognized as an internal or external command,`, 'err');
        out(`operable program or batch file.`, 'err');
    }
  };

  const onKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter') {
      const v = input; setInput('');
      void exec(v);
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      const i = histIdx === -1 ? history.length - 1 : Math.max(0, histIdx - 1);
      if (history[i]) { setHistIdx(i); setInput(history[i]); }
    } else if (e.key === 'ArrowDown') {
      e.preventDefault();
      if (histIdx === -1) return;
      const i = histIdx + 1;
      if (i >= history.length) { setHistIdx(-1); setInput(''); } else { setHistIdx(i); setInput(history[i]); }
    } else if (e.key === 'l' && e.ctrlKey) {
      e.preventDefault(); dispatch({ type: 'clear' });
    }
  };

  return (
    <div className="flex h-full flex-col bg-[#0c0c0c] text-[#cccccc] font-mono text-[12px]" onClick={() => inputRef.current?.focus()}>
      {/* title strip */}
      <div className="flex h-8 shrink-0 items-center gap-2 border-b border-white/5 bg-[#181818] px-2 text-[11px] text-slate-400">
        <TerminalIcon size={13} />
        <span className="truncate">C:\\Windows\\system32\\cmd.exe</span>
        <button type="button" data-terminal-real onClick={() => { requestX64Launch('cmd.exe', ''); openApp('x64'); }} className="ml-auto flex items-center gap-1 rounded border border-white/10 px-1.5 py-0.5 text-[10px] text-sky-300 hover:bg-white/10" title="Open the real cmd.exe (x86-64 emulator)"><Cpu size={11} /> real cmd.exe</button>
        <span className="text-[10px] text-emerald-500/80">{state.pyodideStatus === 'loading' ? 'python: loading WASM…' : state.pyodideStatus === 'ready' ? 'python: ready (Pyodide)' : 'node v24.19.0 ready'}</span>
      </div>
      {/* output */}
      <div ref={scrollRef} className="flex-1 min-h-0 overflow-y-auto px-3 py-2 leading-[1.45]">
        {state.lines.map((l, i) => (
          <div key={i} className={l.kind === 'err' ? 'text-[#ff6b6b]' : l.kind === 'cmd' ? 'text-white' : l.kind === 'ok' ? 'text-[#5dd88a]' : 'text-[#cccccc]'} style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-all' }}>{l.text}</div>
        ))}
        {busy && <div className="text-[#888] animate-pulse">…working</div>}
      </div>
      {/* input */}
      <form className="flex shrink-0 items-center border-t border-white/10 bg-[#0c0c0c] px-3 py-1.5" onSubmit={(e) => { e.preventDefault(); const v = input; setInput(''); void exec(v); }}>
        <ChevronRight size={13} className="mr-1 shrink-0 text-[#59f2ff]" />
        <span className="mr-1.5 shrink-0 text-[#e8e8e8]">{prompt}</span>
        <input
          ref={inputRef}
          autoFocus
          value={input}
          onChange={e => setInput(e.target.value)}
          onKeyDown={onKeyDown}
          className="w-full bg-transparent outline-none text-[#f2f2f2] caret-[#59f2ff]"
          spellCheck={false}
          autoComplete="off"
          aria-label="Command line"
        />
      </form>
    </div>
  );
}

function resolvePath(cwd: string[], arg: string): string[] {
  const parts = arg.replace(/\//g, '\\').split('\\').filter(Boolean);
  let base: string[] = /^[a-zA-Z]:$/.test(parts[0] ?? '') ? [parts[0].toUpperCase()] : [...cwd];
  const start = /^[a-zA-Z]:$/.test(parts[0] ?? '') ? 1 : 0;
  for (let i = start; i < parts.length; i++) {
    const p = parts[i];
    if (p === '.') continue;
    if (p === '..') { if (base.length > 1) base = base.slice(0, -1); continue; }
    base = [...base, p];
  }
  return base;
}
interface DirNode { name: string; type: 'dir' | 'file'; children?: DirNode[]; content?: string }
function findDir(root: DirNode, path: string[]): DirNode | null {
  let cur: DirNode = root;
  for (const seg of path.slice(1)) {
    const next = cur.children?.find(c => c.name.toLowerCase() === seg.toLowerCase());
    if (!next || next.type !== 'dir') return null;
    cur = next;
  }
  return cur;
}
function findFile(root: DirNode, path: string[]): DirNode | null {
  let cur: DirNode = root;
  for (const seg of path.slice(1)) {
    const next = cur.children?.find(c => c.name.toLowerCase() === seg.toLowerCase());
    if (!next) return null;
    cur = next;
  }
  return cur.type === 'file' ? cur : null;
}
