// Windows 10 OS shell — global system context (window manager + filesystem + notifications)
import { createContext, useCallback, useContext, useRef, useState, type ReactNode } from 'react';
import { W as VW, H as VH } from './nebula';

export type AppId =
  | 'edge' | 'nebula' | 'steam' | 'epic' | 'terminal' | 'opencode'
  | 'explorer' | 'notepad' | 'settings' | 'downloads' | 'snipping' | 'x64';

export interface WinState {
  id: string;
  app: AppId;
  title: string;
  icon: string; // lucide icon key or emoji-free glyph handled by component
  z: number;
  minimized: boolean;
  maximized: boolean;
  pos: { x: number; y: number };
  size: { w: number; h: number };
}

export interface FsNode {
  name: string;
  type: 'dir' | 'file';
  children?: FsNode[];
  content?: string;
  size?: number;
}

export interface Toast { id: number; title: string; body: string; icon: string }

interface OSContextType {
  windows: WinState[];
  openApp: (app: AppId, title?: string, size?: { w: number; h: number }) => void;
  closeWindow: (id: string) => void;
  toggleMinimize: (id: string) => void;
  toggleMaximize: (id: string) => void;
  focusWindow: (id: string) => void;
  moveWindow: (id: string, pos: { x: number; y: number }) => void;
  resizeWindow: (id: string, size: { w: number; h: number }) => void;
  zCounter: React.MutableRefObject<number>;
  fs: FsNode;
  saveFile: (path: string[], name: string, content: string) => void;
  readFile: (path: string[], name: string) => string | null;
  toasts: Toast[];
  pushToast: (title: string, body: string, icon?: string) => void;
  dismissToast: (id: number) => void;
  startOpen: boolean;
  setStartOpen: (v: boolean) => void;
  bestScore: number;
  setBestScore: (n: number) => void;
  soundEnabled: boolean;
  setSoundEnabled: (v: boolean) => void;
}

const OSContext = createContext<OSContextType | null>(null);

const defaultFs: FsNode = {
  name: 'C:', type: 'dir', children: [
    { name: 'Users', type: 'dir', children: [
      { name: 'BigPickle', type: 'dir', children: [
        { name: 'Desktop', type: 'dir', children: [
          { name: 'NEBULA STRIKE.lnk', type: 'file', size: 1024 },
        ]},
        { name: 'Documents', type: 'dir', children: [
          { name: 'project-nebula-strike.txt', type: 'file', size: 2048,
            content: 'NEBULA STRIKE — Big Pickle Studios\n=================================\nBuild: NebulaStrike.exe (x86_64-windows-gnu)\nEngine: shared deterministic core (nebula_core.inc)\nWASM: wasm32-wasi + wasm64-wasi (memory64)\nAutotest: WIN / 7500 / wave 5 / 2563 frames\n' },
        ]},
        { name: 'Downloads', type: 'dir', children: [
          { name: 'NebulaStrike.exe', type: 'file', size: 234496 },
          { name: 'nebula_strike_wasm64.wasm', type: 'file', size: 25191 },
          { name: 'nebula_strike_wasm32.wasm', type: 'file', size: 1054237 },
          { name: 'NebulaStrike_BigPickle_Source.zip', type: 'file', size: 5111037 },
        ]},
      ]},
    ]},
    { name: 'Program Files', type: 'dir', children: [
      { name: 'Big Pickle Studios', type: 'dir', children: [
        { name: 'NebulaStrike', type: 'dir', children: [
          { name: 'NebulaStrike.exe', type: 'file', size: 234496 },
          { name: 'README.md', type: 'file', size: 4096, content: '# NEBULA STRIKE\n\nControls: Arrows move, Space fire, R restart, Esc quit.\n\nBuilt with zig cc for Windows x86_64.\n' },
        ]},
      ]},
    ]},
    { name: 'Windows', type: 'dir', children: [
      { name: 'System32', type: 'dir', children: [
        { name: 'notepad.exe', type: 'file', size: 192000 },
        { name: 'cmd.exe', type: 'file', size: 256000 },
        { name: 'taskmgr.exe', type: 'file', size: 832000 },
        { name: 'kernel32.dll', type: 'file', size: 768000 },
      ]},
      { name: 'explorer.exe', type: 'file', size: 4180000 },
    ]},
  ],
};

function findNode(root: FsNode, path: string[]): FsNode | null {
  let cur: FsNode | undefined = root;
  for (const seg of path) {
    if (!cur || !cur.children) return null;
    cur = cur.children.find(c => c.name.toLowerCase() === seg.toLowerCase());
  }
  return cur ?? null;
}

export function OSProvider({ children }: { children: ReactNode }) {
  const [windows, setWindows] = useState<WinState[]>([]);
  const [fs, setFs] = useState<FsNode>(defaultFs);
  const [toasts, setToasts] = useState<Toast[]>([]);
  const [startOpen, setStartOpen] = useState(false);
  const [bestScore, setBestScoreState] = useState<number>(() => {
    try { return Number(localStorage.getItem('nebula-best') ?? '0') || 0; } catch { return 0; }
  });
  const [soundEnabled, setSoundEnabled] = useState(true);
  const zCounter = useRef(10);
  const idCounter = useRef(0);

  const focusWindow = useCallback((id: string) => {
    zCounter.current += 1;
    setWindows(ws => ws.map(w => (w.id === id ? { ...w, z: zCounter.current, minimized: false } : w)));
  }, []);

  const openApp = useCallback((app: AppId, title?: string, size?: { w: number; h: number }) => {
    setStartOpen(false);
    zCounter.current += 1;
    const meta: Record<AppId, { title: string; icon: string; size: { w: number; h: number } }> = {
      edge:      { title: 'Microsoft Edge',            icon: 'edge',      size: { w: 980, h: 640 } },
      nebula:    { title: 'NEBULA STRIKE — NebulaStrike.exe', icon: 'game', size: { w: 856, h: 560 } },
      steam:     { title: 'Steam',                     icon: 'steam',     size: { w: 980, h: 640 } },
      epic:      { title: 'Epic Games Launcher',       icon: 'epic',      size: { w: 980, h: 640 } },
      terminal:  { title: 'Command Prompt — C:\\Windows\\system32\\cmd.exe', icon: 'terminal', size: { w: 900, h: 560 } },
      opencode:  { title: 'opencode — TUI (C:\\dev\\game)',    icon: 'opencode',  size: { w: 920, h: 600 } },
      explorer:  { title: 'File Explorer',              icon: 'explorer',  size: { w: 900, h: 560 } },
      notepad:   { title: 'Notepad',                    icon: 'notepad',   size: { w: 640, h: 500 } },
      settings:  { title: 'Settings',                   icon: 'settings',  size: { w: 900, h: 580 } },
      downloads: { title: 'Downloads',                  icon: 'downloads', size: { w: 760, h: 480 } },
      x64:       { title: 'x64 Runner — Nebula x64 (wasm64 + WebGL)', icon: 'x64', size: { w: 1040, h: 660 } },
      snipping:  { title: 'Snipping Tool',              icon: 'snipping',  size: { w: 560, h: 420 } },
    };
    const m = meta[app];
    const multiInstance: AppId[] = ['terminal', 'notepad', 'explorer'];
    const n = (idCounter.current += 1);
    const offset = (n % 6) * 28;
    // fit the window into the current viewport (phones, small laptops, rotated tablets)
    const vw = typeof window !== 'undefined' ? window.innerWidth : 1920;
    const vh = typeof window !== 'undefined' ? window.innerHeight : 1080;
    const wanted = size ?? m.size;
    const fitted = { w: Math.min(wanted.w, Math.max(320, vw - 8)), h: Math.min(wanted.h, Math.max(240, vh - 56)) };
    const pos = { x: Math.max(0, Math.min(120 + offset, vw - fitted.w - 4)), y: Math.max(0, Math.min(40 + offset, vh - fitted.h - 52)) };
    const small = vw < 640;
    setWindows(ws => {
      if (!multiInstance.includes(app) && !title) {
        const existing = ws.find(w => w.app === app);
        if (existing) return ws.map(w => (w.id === existing.id ? { ...w, z: zCounter.current, minimized: false } : w));
      }
      return [
        ...ws,
        {
          id: `${app}-${n}`, app, title: title ?? m.title, icon: m.icon,
          z: zCounter.current, minimized: false, maximized: small,
          pos, size: fitted,
        },
      ];
    });
  }, []);

  const closeWindow = useCallback((id: string) => setWindows(ws => ws.filter(w => w.id !== id)), []);
  const toggleMinimize = useCallback((id: string) => setWindows(ws => ws.map(w => (w.id === id ? { ...w, minimized: !w.minimized } : w))), []);
  const toggleMaximize = useCallback((id: string) => setWindows(ws => ws.map(w => (w.id === id ? { ...w, maximized: !w.maximized, z: w.maximized ? w.z : (zCounter.current += 1) } : w))), []);
  const moveWindow = useCallback((id: string, pos: { x: number; y: number }) => setWindows(ws => ws.map(w => (w.id === id ? { ...w, pos } : w))), []);
  const resizeWindow = useCallback((id: string, size: { w: number; h: number }) => setWindows(ws => ws.map(w => (w.id === id ? { ...w, size } : w))), []);

  const saveFile = useCallback((path: string[], name: string, content: string) => {
    setFs(prev => {
      const clone: FsNode = JSON.parse(JSON.stringify(prev));
      const dir = findNode(clone, path);
      if (dir && dir.type === 'dir') {
        dir.children = dir.children ?? [];
        const existing = dir.children.find(c => c.name === name);
        if (existing) { existing.content = content; existing.size = content.length; }
        else dir.children.push({ name, type: 'file', content, size: content.length });
      }
      return clone;
    });
  }, []);

  const readFile = useCallback((path: string[], name: string): string | null => {
    const node = findNode(fs, [...path, name]);
    return node?.content ?? null;
  }, [fs]);

  const pushToast = useCallback((title: string, body: string, icon = 'info') => {
    idCounter.current += 1;
    const id = idCounter.current;
    setToasts(t => [...t.slice(-2), { id, title, body, icon }]);
    window.setTimeout(() => setToasts(t => t.filter(x => x.id !== id)), 5200);
  }, []);
  const dismissToast = useCallback((id: number) => setToasts(t => t.filter(x => x.id !== id)), []);

  const setBestScore = useCallback((n: number) => {
    setBestScoreState(prev => {
      const v = Math.max(prev, n);
      try { localStorage.setItem('nebula-best', String(v)); } catch { /* noop */ }
      return v;
    });
  }, []);

  void VW; void VH;
  return (
    <OSContext.Provider value={{
      windows, openApp, closeWindow, toggleMinimize, toggleMaximize, focusWindow, moveWindow, resizeWindow,
      zCounter, fs, saveFile, readFile, toasts, pushToast, dismissToast, startOpen, setStartOpen,
      bestScore, setBestScore, soundEnabled, setSoundEnabled,
    }}>
      {children}
    </OSContext.Provider>
  );
}

// eslint-disable-next-line react-refresh/only-export-components
export function useOS() {
  const ctx = useContext(OSContext);
  if (!ctx) throw new Error('useOS must be used within OSProvider');
  return ctx;
}
