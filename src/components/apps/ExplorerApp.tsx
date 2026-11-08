// File Explorer on the real C:\ — the persistent disk shared with the x86/x64 emulator (Programs).
// Everything listed here exists: folders and files the installers wrote (Program Files, ProgramData, the Start Menu
// shortcuts, the registry hive), documents saved by Notepad or by Windows programs. Double-click an .exe or a .lnk
// to start it in Programs, a text file to open it in Notepad. Files can be imported from the device, exported to it,
// renamed, deleted; folders created. Free space comes from the browser's storage manager — not a made-up number.
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { useOS } from '../../lib/os';
import { AppIcon } from '../icons';
import { ChevronRight, Monitor, RotateCw, Search, ArrowLeft, ArrowRight, ArrowUp, File as FileIcon, Folder, FolderPlus, List, LayoutGrid, HardDrive, Upload, Download, Trash2, Pencil, Play, FileText, Loader2, Link2, Archive, Image as ImageIcon, Plus, X } from 'lucide-react';
import { diskList, diskRead, diskWrite, diskMkdir, diskRemove, diskRename, diskUsage, diskAvailable, onDiskChange, joinPath, parentOf, baseName, type DiskEntry } from '../../lib/x64/diskbus';
import { requestX64Launch } from '../../lib/x64/launch';
import { requestDocOpen, TEXT_EXT } from '../../lib/docs';
import { parseLnk } from '../../lib/x64/library';
import { peIconDataUrl } from '../../lib/x64/peicon';

const HOME = 'C:\\Users\\user';
const QUICK: { label: string; path: string }[] = [
  { label: 'Desktop', path: `${HOME}\\Desktop` },
  { label: 'Documents', path: `${HOME}\\Documents` },
  { label: 'Downloads', path: `${HOME}\\Downloads` },
  { label: 'Program Files', path: 'C:\\Program Files' },
  { label: 'This PC', path: 'C:' },
];
const keyOf = (p: string) => p.toLowerCase().replace(/^([a-z]:)\\$/, '$1');
const fmtSize = (s: number) => s < 1024 ? `${s} B` : s < 1048576 ? `${(s / 1024).toFixed(1)} KB` : s < 1073741824 ? `${(s / 1048576).toFixed(1)} MB` : `${(s / 1073741824).toFixed(2)} GB`;
const fmtDate = (t: number) => t ? new Date(t).toLocaleString(undefined, { year: '2-digit', month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit' }) : '';
const typeOf = (e: DiskEntry) => {
  if (e.dir) return 'File folder'; const n = e.name.toLowerCase(); const i = n.lastIndexOf('.'); const ext = i >= 0 ? n.slice(i + 1) : '';
  return ({ exe: 'Application', dll: 'Application extension', lnk: 'Shortcut', txt: 'Text Document', ini: 'Configuration settings', reg: 'Registration Entries', zip: 'Compressed (zipped) Folder', '7z': '7Z File', bat: 'Windows Batch File', cmd: 'Windows Command Script', chm: 'Compiled HTML Help', png: 'PNG File', jpg: 'JPEG image', bmp: 'Bitmap image', ico: 'Icon', log: 'Text Document', xml: 'XML Document', json: 'JSON File', html: 'HTML Document', pdf: 'PDF File' } as Record<string, string>)[ext] ?? (ext ? `${ext.toUpperCase()} File` : 'File');
};

export default function ExplorerApp() {
  const { openApp, pushToast } = useOS();
  const [all, setAll] = useState<DiskEntry[]>([]);
  const [loading, setLoading] = useState(true);
  const [path, setPath] = useState<string>(`${HOME}\\Documents`);
  const [history, setHistory] = useState<string[]>([]);
  const [future, setFuture] = useState<string[]>([]);
  // Each tab keeps its own location and navigation history, all on the real persistent C: drive.
  type Tab = { id: number; path: string; history: string[]; future: string[] };
  const [tabs, setTabs] = useState<Tab[]>([{ id: 1, path: `${HOME}\\Documents`, history: [], future: [] }]);
  const [activeTab, setActiveTab] = useState(1);
  const nextTab = useRef(2);
  const saveCurrent = (tab: Tab): Tab => tab.id === activeTab ? { ...tab, path, history, future } : tab;
  const activate = (tab: Tab) => {
    setActiveTab(tab.id); setPath(tab.path); setHistory(tab.history); setFuture(tab.future);
    setQuery(''); setSelected(null); setRenaming(null); setNewFolder(null);
  };
  const switchTab = (id: number) => {
    if (id === activeTab) return;
    const tab = tabs.find(t => t.id === id); if (!tab) return;
    setTabs(tabs.map(saveCurrent)); activate(tab);
  };
  const addTab = () => {
    const tab: Tab = { id: nextTab.current++, path: 'C:', history: [], future: [] };
    setTabs([...tabs.map(saveCurrent), tab]); activate(tab);
  };
  const closeTab = (id: number) => {
    if (tabs.length === 1) return;
    const index = tabs.findIndex(t => t.id === id);
    const remaining = tabs.map(saveCurrent).filter(t => t.id !== id);
    setTabs(remaining);
    if (id === activeTab) activate(remaining[Math.min(index, remaining.length - 1)]);
  };
  const [view, setView] = useState<'list' | 'grid'>('list');
  const [query, setQuery] = useState('');
  const [selected, setSelected] = useState<string | null>(null);
  const [renaming, setRenaming] = useState<{ path: string; name: string } | null>(null);
  const [newFolder, setNewFolder] = useState<string | null>(null);
  const [usage, setUsage] = useState<{ used: number; quota: number } | null>(null);
  const [busy, setBusy] = useState<string | null>(null);
  const [icons, setIcons] = useState<Record<string, string>>({});
  const iconCache = useRef(new Map<string, string | null>());
  const touch = useRef(false);   // on a touch screen a second tap on the selected item opens it (there is no double-click)
  const [wide, setWide] = useState(() => (typeof window === 'undefined' ? true : window.innerWidth > 720));
  useEffect(() => { const onResize = () => setWide(window.innerWidth > 720); window.addEventListener('resize', onResize); return () => window.removeEventListener('resize', onResize); }, []);

  const refresh = useCallback(async () => {
    const t = window.setTimeout(() => setLoading(true), 0);
    let list: DiskEntry[] | null = null;
    try { list = await diskList(); } catch (e) { pushToast('File Explorer', 'Disk read failed: ' + String(e), 'explorer'); }
    window.clearTimeout(t);
    if (list) setAll(list);
    setLoading(false);
    setUsage(await diskUsage());
  }, [pushToast]);
  useEffect(() => { const t = window.setTimeout(() => { void refresh(); }, 0); const off = onDiskChange(() => { void refresh(); }); return () => { window.clearTimeout(t); off(); }; }, [refresh]);

  // folder contents (implicit directories are part of `all`)
  const dirs = useMemo(() => { const s = new Set<string>(['c:']); for (const e of all) if (e.dir) s.add(keyOf(e.path)); return s; }, [all]);
  const items = useMemo(() => {
    const pk = keyOf(path); const q = query.trim().toLowerCase();
    const list = q
      ? all.filter(e => keyOf(e.path).startsWith(pk + '\\') && e.name.toLowerCase().includes(q))
      : all.filter(e => keyOf(parentOf(e.path)) === pk && keyOf(e.path) !== pk);
    return list.sort((a, b) => (a.dir === b.dir ? a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }) : a.dir ? -1 : 1));
  }, [all, path, query]);
  const exists = dirs.has(keyOf(path));

  const navigate = useCallback((p: string) => { setHistory(h => [...h, path]); setFuture([]); setPath(p); setQuery(''); setSelected(null); }, [path]);
  const back = () => { if (history.length) { setFuture(f => [path, ...f]); setPath(history[history.length - 1]); setHistory(h => h.slice(0, -1)); setSelected(null); } };
  const fwd = () => { if (future.length) { setHistory(h => [...h, path]); setPath(future[0]); setFuture(f => f.slice(1)); setSelected(null); } };
  const up = () => { if (path.length > 2) navigate(parentOf(path)); };

  // real icons for .exe files in the current folder (RT_GROUP_ICON from the PE), loaded lazily and cached
  useEffect(() => {
    let alive = true;
    (async () => {
      for (const e of items) {
        if (e.dir || !/\.exe$/i.test(e.name) || e.size > (64 << 20)) continue;
        const k = keyOf(e.path) + ':' + e.size + ':' + e.mtime; if (iconCache.current.has(k)) continue;
        iconCache.current.set(k, null);
        const b = await diskRead(e.path); if (!b || !alive) continue;
        const url = await peIconDataUrl(b, 64).catch(() => null); iconCache.current.set(k, url);
        if (url && alive) setIcons(m => ({ ...m, [keyOf(e.path)]: url }));
      }
    })();
    return () => { alive = false; };
  }, [items]);

  const download = async (e: DiskEntry) => {
    const b = await diskRead(e.path); if (!b) { pushToast('File Explorer', `${e.name} could not be read`, 'explorer'); return; }
    const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([b.slice()])); a.download = e.name; a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 2000);
  };
  const open = useCallback(async (e: DiskEntry) => {
    if (e.dir) { navigate(e.path); return; }
    const n = e.name.toLowerCase();
    if (n.endsWith('.exe') || n.endsWith('.com') || n.endsWith('.bat') || n.endsWith('.cmd')) {
      if (n.endsWith('.bat') || n.endsWith('.cmd')) requestX64Launch('C:\\Windows\\System32\\cmd.exe', `/c "${e.path}"`, parentOf(e.path));
      else requestX64Launch(e.path, '', parentOf(e.path));
      openApp('x64'); return;
    }
    if (n.endsWith('.lnk')) {
      const b = await diskRead(e.path); const s = b ? parseLnk(e.path, b) : null;
      if (!s || !s.target) { pushToast('File Explorer', `${e.name}: the shortcut target could not be read`, 'explorer'); return; }
      if (/\.(exe|com|bat|cmd)$/i.test(s.target)) { requestX64Launch(s.target, s.args, s.workDir || parentOf(s.target)); openApp('x64'); }
      else if (dirs.has(keyOf(s.target))) navigate(s.target);
      else if (TEXT_EXT.test(s.target)) { requestDocOpen(s.target); openApp('notepad'); }
      else pushToast('File Explorer', `${s.target}: nothing on this desktop opens that target`, 'explorer');
      return;
    }
    if (TEXT_EXT.test(n) || e.size === 0) { requestDocOpen(e.path); openApp('notepad'); return; }
    await download(e);   // binary files (archives, images, help files…): hand them to the device
  }, [navigate, openApp, pushToast, dirs]); // eslint-disable-line react-hooks/exhaustive-deps

  const importFiles = async (list: FileList | null) => {
    if (!list?.length) return; let n = 0, bytes = 0;
    for (const f of Array.from(list)) {
      setBusy(`Copying ${f.name} (${fmtSize(f.size)})…`);
      const rel = (f as File & { webkitRelativePath?: string }).webkitRelativePath || f.name;
      const dst = joinPath(path, rel.replace(/\//g, '\\'));
      const ok = await diskWrite(dst, new Uint8Array(await f.arrayBuffer())); if (ok) { n++; bytes += f.size; }
    }
    setBusy(null); pushToast('File Explorer', `${n} file(s), ${fmtSize(bytes)} copied to ${path}`, 'explorer');
  };
  const remove = async (e: DiskEntry) => {
    if (!confirm(`Delete ${e.dir ? 'folder' : 'file'} "${e.name}"${e.dir ? ' and everything in it' : ''}? This cannot be undone.`)) return;
    setBusy(`Deleting ${e.name}…`); const n = await diskRemove(e.path); setBusy(null); setSelected(null);
    pushToast('File Explorer', `${e.name} deleted (${n} item${n === 1 ? '' : 's'})`, 'explorer');
  };
  const commitRename = async () => {
    if (!renaming) return; const name = renaming.name.trim(); const from = renaming.path; setRenaming(null);
    if (!name || name === baseName(from) || /[\\/:*?"<>|]/.test(name)) return;
    setBusy(`Renaming…`); await diskRename(from, joinPath(parentOf(from), name)); setBusy(null);
  };
  const commitNewFolder = async () => {
    if (newFolder === null) return; const name = newFolder.trim(); setNewFolder(null);
    if (!name || /[\\/:*?"<>|]/.test(name)) return;
    await diskMkdir(joinPath(path, name));
  };
  const sel = items.find(e => keyOf(e.path) === (selected ?? '')) ?? null;
  const crumbs = path.split('\\');
  const folderBytes = items.reduce((a, e) => a + (e.dir ? 0 : e.size), 0);

  return (
    <div className="flex h-full bg-white text-[#1b1b1b] text-[12px]" data-explorer data-explorer-path={path}
      onDragOver={e => { e.preventDefault(); }} onDrop={e => { e.preventDefault(); void importFiles(e.dataTransfer.files); }}>
      {/* nav pane */}
      <div className={`${wide ? 'w-44' : 'w-12'} shrink-0 overflow-y-auto border-r border-slate-200 bg-[#f7f7f7] py-2`}>
        <div className="px-3 py-1 text-[10px] font-bold tracking-wider text-slate-500">{wide && 'Quick access'}</div>
        {QUICK.map(q => (
          <button key={q.label} onClick={() => navigate(q.path)} title={q.label} data-explorer-quick={q.label}
            className={`flex w-full items-center gap-2 px-3 py-1.5 hover:bg-slate-200 ${keyOf(q.path) === keyOf(path) ? 'bg-sky-100 text-sky-800' : ''}`}>
            {q.label === 'This PC' ? <Monitor size={14} className="shrink-0 text-slate-600" /> : <Folder size={14} className="shrink-0 text-amber-500" fill="#fcd34d" />}
            {wide && <span className="truncate">{q.label}</span>}
          </button>
        ))}
        <div className="mt-2 px-3 py-1 text-[10px] font-bold tracking-wider text-slate-500">{wide && 'This PC'}</div>
        <button onClick={() => navigate('C:')} className="flex w-full items-start gap-2 px-3 py-1.5 text-left text-slate-600 hover:bg-slate-200" title="Local Disk (C:)">
          <HardDrive size={13} className="mt-0.5 shrink-0" />
          {wide && <span className="leading-tight">Local Disk (C:)<br /><span className="text-[10px] text-slate-400" data-explorer-usage>{usage ? `${fmtSize(usage.used)} used · ${fmtSize(Math.max(0, usage.quota - usage.used))} free (browser quota)` : diskAvailable() ? 'measuring…' : 'persistent storage unavailable'}</span></span>}
        </button>
        {wide && <div className="mt-3 px-3 text-[9px] leading-snug text-slate-400">The same C:\ the Windows programs in <b>Programs</b> use. Temporary paths (C:\tmp, …\Temp) are not kept.</div>}
      </div>

      {/* content */}
      <div className="flex min-w-0 flex-1 flex-col">
        <div className="flex h-10 shrink-0 items-end gap-1 overflow-x-auto border-b border-slate-300 bg-slate-100 px-2 pt-1" role="tablist" aria-label="Explorer tabs">
          {tabs.map(tab => {
            const currentPath = tab.id === activeTab ? path : tab.path;
            return <div key={tab.id} className={`flex h-8 min-w-0 max-w-44 items-center rounded-t-md border px-1 ${tab.id === activeTab ? 'border-slate-300 border-b-white bg-white' : 'border-transparent bg-slate-200 hover:bg-slate-300'}`}>
              <button role="tab" aria-selected={tab.id === activeTab} data-explorer-tab={tab.id} title={currentPath} onClick={() => switchTab(tab.id)} className="flex min-w-0 items-center gap-1.5 px-1.5 text-left"><Folder size={14} className="shrink-0 text-amber-500" /><span className="truncate">{baseName(currentPath) || 'C:'}</span></button>
              {tabs.length > 1 && <button aria-label={`Close tab ${baseName(currentPath) || 'C:'}`} data-explorer-close-tab={tab.id} onClick={() => closeTab(tab.id)} className="ml-1 rounded p-1 hover:bg-slate-300"><X size={12} /></button>}
            </div>;
          })}
          <button onClick={addTab} title="New tab" aria-label="New tab" data-explorer-new-tab className="mb-1 rounded p-1.5 hover:bg-slate-300"><Plus size={15} /></button>
        </div>
        <div className="flex h-11 shrink-0 items-center gap-1 overflow-x-auto whitespace-nowrap border-b border-slate-200 px-2">
          <button onClick={back} className="rounded p-1.5 hover:bg-slate-100 disabled:opacity-30" disabled={!history.length} title="Back"><ArrowLeft size={15} /></button>
          <button onClick={fwd} className="rounded p-1.5 hover:bg-slate-100 disabled:opacity-30" disabled={!future.length} title="Forward"><ArrowRight size={15} /></button>
          <button onClick={up} className="rounded p-1.5 hover:bg-slate-100 disabled:opacity-30" disabled={path.length <= 2} title="Up" data-explorer-up><ArrowUp size={15} /></button>
          <button onClick={() => void refresh()} className="rounded p-1.5 hover:bg-slate-100" title="Refresh">{loading ? <Loader2 size={14} className="animate-spin" /> : <RotateCw size={14} />}</button>
          <span className="mx-1 h-5 w-px bg-slate-200" />
          <label className="flex cursor-pointer items-center gap-1 rounded px-2 py-1 hover:bg-slate-100" title="Copy files from this device into this folder"><Upload size={13} /><span className="hidden sm:inline">Import files</span><input type="file" multiple className="hidden" data-explorer-import onChange={e => { void importFiles(e.target.files); e.target.value = ''; }} /></label>
          <button onClick={() => setNewFolder('New folder')} className="flex items-center gap-1 rounded px-2 py-1 hover:bg-slate-100" title="New folder" data-explorer-newfolder><FolderPlus size={13} /><span className="hidden sm:inline">New folder</span></button>
          {sel && <>
            <span className="mx-1 h-5 w-px bg-slate-200" />
            <button onClick={() => void open(sel)} className="flex items-center gap-1 rounded px-2 py-1 hover:bg-slate-100" title="Open" data-explorer-open>{/\.(exe|lnk|bat|cmd)$/i.test(sel.name) ? <Play size={13} /> : <FileText size={13} />}<span>{/\.(exe|lnk|bat|cmd)$/i.test(sel.name) ? 'Run' : 'Open'}</span></button>
            {!sel.dir && <button onClick={() => void download(sel)} className="flex items-center gap-1 rounded px-2 py-1 hover:bg-slate-100" title="Download to this device" data-explorer-download><Download size={13} /></button>}
            <button onClick={() => setRenaming({ path: sel.path, name: sel.name })} className="flex items-center gap-1 rounded px-2 py-1 hover:bg-slate-100" title="Rename" data-explorer-rename><Pencil size={13} /></button>
            <button onClick={() => void remove(sel)} className="flex items-center gap-1 rounded px-2 py-1 text-red-700 hover:bg-red-50" title="Delete" data-explorer-delete><Trash2 size={13} /></button>
          </>}
          <form className="ml-auto flex w-40 items-center gap-1.5 rounded border border-slate-300 bg-slate-50 px-2 py-1 focus-within:border-sky-500" onSubmit={e => e.preventDefault()}>
            <Search size={12} className="text-slate-400" />
            <input value={query} onChange={e => setQuery(e.target.value)} placeholder={`Search ${baseName(path) || 'C:'}`} className="w-full bg-transparent text-[11px] outline-none" spellCheck={false} />
          </form>
          <button onClick={() => setView('list')} className={`rounded p-1.5 ${view === 'list' ? 'bg-sky-100 text-sky-700' : 'hover:bg-slate-100'}`}><List size={15} /></button>
          <button onClick={() => setView('grid')} className={`rounded p-1.5 ${view === 'grid' ? 'bg-sky-100 text-sky-700' : 'hover:bg-slate-100'}`}><LayoutGrid size={15} /></button>
        </div>
        <div className="flex h-8 shrink-0 items-center gap-0.5 overflow-x-auto whitespace-nowrap border-b border-slate-200 bg-slate-50 px-3 text-[11px] text-slate-600">
          <Monitor size={12} className="mr-1 shrink-0 text-slate-400" />
          {crumbs.map((seg, i) => (
            <span key={i} className="flex items-center">
              <button className="rounded px-1 py-0.5 hover:bg-slate-200 hover:text-slate-900" onClick={() => navigate(crumbs.slice(0, i + 1).join('\\'))}>{seg}</button>
              {i < crumbs.length - 1 && <ChevronRight size={11} className="text-slate-400" />}
            </span>
          ))}
          {busy && <span className="ml-auto flex items-center gap-1 text-[10px] text-sky-700"><Loader2 size={10} className="animate-spin" />{busy}</span>}
        </div>
        <div className="min-h-0 flex-1 overflow-y-auto p-3" onClick={() => setSelected(null)}>
          {newFolder !== null && (
            <form className="mb-2 flex items-center gap-2" onSubmit={e => { e.preventDefault(); void commitNewFolder(); }} onClick={e => e.stopPropagation()}>
              <Folder size={16} className="text-amber-400" fill="#fcd34d" />
              <input autoFocus value={newFolder} onChange={e => setNewFolder(e.target.value)} onBlur={() => void commitNewFolder()} onKeyDown={e => { if (e.key === 'Escape') setNewFolder(null); }} className="rounded border border-sky-400 px-1 py-0.5 text-[11px] outline-none" data-explorer-newfolder-name />
            </form>
          )}
          {!loading && items.length === 0 && (
            <div className="mt-8 text-center text-slate-400">
              {query ? `No items match "${query}".` : exists ? 'This folder is empty.' : <>This folder does not exist on the disk yet.<br /><span className="text-[10px]">It is created when the first file is saved or imported here.</span></>}
            </div>
          )}
          {view === 'grid' ? (
            <div className="grid grid-cols-4 gap-2 sm:grid-cols-5 lg:grid-cols-6">
              {items.map(e => (
                <button key={e.path} data-explorer-item={e.name} onPointerDown={ev => { touch.current = ev.pointerType === 'touch'; }} onClick={ev => { ev.stopPropagation(); if (touch.current && (e.dir || selected === keyOf(e.path))) void open(e); else setSelected(keyOf(e.path)); }} onDoubleClick={() => void open(e)}
                  className={`group flex flex-col items-center gap-1.5 rounded-md p-2 outline-none ${selected === keyOf(e.path) ? 'bg-sky-100' : 'hover:bg-sky-50'}`}>
                  <ItemIcon e={e} size={40} icon={icons[keyOf(e.path)]} />
                  {renaming?.path === e.path ? <RenameBox value={renaming.name} onChange={n => setRenaming({ path: e.path, name: n })} onDone={() => void commitRename()} onCancel={() => setRenaming(null)} /> : <span className="w-full truncate text-center text-[10px] leading-tight">{e.name}</span>}
                </button>
              ))}
            </div>
          ) : (
            <table className="w-full text-[11px]">
              <thead><tr className="text-left text-slate-500"><th className="py-1 font-medium">Name</th><th className="font-medium">Date modified</th><th className="font-medium">Type</th><th className="text-right font-medium">Size</th></tr></thead>
              <tbody>
                {items.map(e => (
                  <tr key={e.path} data-explorer-item={e.name} onPointerDown={ev => { touch.current = ev.pointerType === 'touch'; }} onClick={ev => { ev.stopPropagation(); if (touch.current && (e.dir || selected === keyOf(e.path))) void open(e); else setSelected(keyOf(e.path)); }} onDoubleClick={() => void open(e)}
                    className={`cursor-default border-t border-slate-100 ${selected === keyOf(e.path) ? 'bg-sky-100' : 'hover:bg-sky-50'}`}>
                    <td className="flex items-center gap-2 py-1"><ItemIcon e={e} size={16} icon={icons[keyOf(e.path)]} />
                      {renaming?.path === e.path ? <RenameBox value={renaming.name} onChange={n => setRenaming({ path: e.path, name: n })} onDone={() => void commitRename()} onCancel={() => setRenaming(null)} /> : <span className="truncate">{e.name}</span>}
                      {query && <span className="ml-2 truncate text-[9px] text-slate-400">{parentOf(e.path)}</span>}</td>
                    <td className="whitespace-nowrap text-slate-500">{fmtDate(e.mtime)}</td>
                    <td className="whitespace-nowrap text-slate-500">{typeOf(e)}</td>
                    <td className="whitespace-nowrap text-right text-slate-500" data-explorer-size={e.dir ? '' : e.size}>{e.dir ? '' : fmtSize(e.size)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
        <div className="flex h-6 shrink-0 items-center gap-3 border-t border-slate-200 bg-slate-50 px-3 text-[10px] text-slate-500">
          <span data-explorer-count>{items.length} item{items.length === 1 ? '' : 's'}</span>
          <span>|</span>
          <span>{fmtSize(folderBytes)}</span>
          {sel && <><span>|</span><span className="truncate">{sel.name}{sel.dir ? '' : ` · ${fmtSize(sel.size)}`}</span></>}
          <span className="ml-auto truncate">{all.filter(e => !e.dir).length} files on C:\</span>
        </div>
      </div>
    </div>
  );
}

function RenameBox({ value, onChange, onDone, onCancel }: { value: string; onChange: (v: string) => void; onDone: () => void; onCancel: () => void }) {
  return <input autoFocus value={value} onChange={e => onChange(e.target.value)} onClick={e => e.stopPropagation()} onBlur={onDone}
    onKeyDown={e => { e.stopPropagation(); if (e.key === 'Enter') onDone(); if (e.key === 'Escape') onCancel(); }} className="rounded border border-sky-400 px-1 py-0.5 text-[11px] outline-none" data-explorer-rename-name />;
}

function ItemIcon({ e, size, icon }: { e: DiskEntry; size: number; icon?: string }) {
  if (e.dir) return <Folder size={size} className="shrink-0 text-amber-400" fill="#fcd34d" strokeWidth={1.4} />;
  const n = e.name.toLowerCase();
  if (icon) return <img src={icon} width={size} height={size} alt="" className="shrink-0" style={{ imageRendering: size <= 16 ? 'auto' : 'pixelated' }} />;
  if (n.endsWith('.exe')) return <AppIcon id="x64" size={size} />;
  if (n.endsWith('.lnk')) return <span className="relative inline-block shrink-0" style={{ width: size, height: size }}><AppIcon id="x64" size={size} /><Link2 size={Math.max(8, size / 2.5)} className="absolute bottom-0 left-0 rounded-sm bg-white text-sky-700" /></span>;
  if (/\.(zip|7z|rar|cab|gz)$/.test(n)) return <Archive size={size} className="shrink-0 text-amber-600" strokeWidth={1.4} />;
  if (/\.(png|jpe?g|bmp|gif|ico|webp)$/.test(n)) return <ImageIcon size={size} className="shrink-0 text-emerald-600" strokeWidth={1.4} />;
  if (TEXT_EXT.test(n)) return <FileText size={size} className="shrink-0 text-slate-500" strokeWidth={1.4} />;
  return <FileIcon size={size} className="shrink-0 text-slate-400" strokeWidth={1.4} />;
}
