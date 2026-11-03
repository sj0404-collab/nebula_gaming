// One C:\ for the whole desktop.
// The persistent disk (disk.ts, IndexedDB) is the store; a running emulator (runtime.ts) keeps a copy of it in wasm
// memory. File Explorer and Notepad work on the same disk through this module: every change they make goes to the
// store AND to every live emulator (so a program started afterwards sees it), and before they read they ask the live
// emulators to flush what their programs have written. Nothing here is simulated — a file saved in Notepad is the
// same bytes Notepad++ or 7-Zip inside the emulator will open, and a folder the installer created is what Explorer lists.
import { Disk, diskKey, isScratch, type DiskFile } from './disk';

export interface LiveVfs {
  vfsAdd(path: string, data: Uint8Array): boolean;
  vfsMkdir(path: string): boolean;
  vfsRemove(path: string): boolean;
  vfsRead(path: string): Uint8Array | null;
  syncDisk(): Promise<void>;
}
export interface DiskEntry { path: string; name: string; dir: boolean; size: number; mtime: number }

const live = new Set<LiveVfs>();
const listeners = new Set<() => void>();
let shared: Disk | null = null;
const store = () => (shared ??= new Disk());

/** the emulator registers itself while it is mounted (runtime.ts init / dispose) */
export function registerLiveVfs(rt: LiveVfs) { live.add(rt); dbg.registered++; return () => { live.delete(rt); }; }
/** diagnostics (devtools / e2e): how many emulator instances were created, how many are still mounted */
export const dbg = { registered: 0, get live() { return live.size; } };
if (typeof window !== 'undefined') (window as unknown as { __x64dbg?: typeof dbg }).__x64dbg = dbg;
export function onDiskChange(fn: () => void) { listeners.add(fn); return () => { listeners.delete(fn); }; }
export function notifyDiskChange() { for (const fn of listeners) fn(); }
export function diskAvailable() { return store().available; }

export function parentOf(path: string) { const i = path.lastIndexOf('\\'); return i > 2 ? path.slice(0, i) : i === 2 ? path.slice(0, 3) : path; }
export function baseName(path: string) { return path.slice(path.lastIndexOf('\\') + 1); }
export function joinPath(dir: string, name: string) { return (dir.endsWith('\\') ? dir : dir + '\\') + name; }

/** everything on the persistent disk, with implicit parent directories filled in (an installer may write
 *  C:\Program Files\7-Zip\7z.dll without an explicit mkdir for every level) */
export async function diskList(): Promise<DiskEntry[]> {
  for (const rt of live) { try { await rt.syncDisk(); } catch { /* a stopped runtime */ } }
  const s = store(); await s.flush();
  const all = await s.loadAll();
  const seen = new Map<string, DiskEntry>();
  const addDir = (p: string) => { const k = diskKey(p); if (!seen.has(k)) seen.set(k, { path: p, name: baseName(p), dir: true, size: 0, mtime: 0 }); };
  const ensureParents = (p: string) => { let d = parentOf(p); while (d.length > 3 && !seen.has(diskKey(d))) { addDir(d); d = parentOf(d); } };
  for (const f of all) {
    const k = diskKey(f.path);
    if (f.data === null) { if (!seen.has(k)) seen.set(k, { path: f.path, name: baseName(f.path), dir: true, size: 0, mtime: f.mtime }); }
    else seen.set(k, { path: f.path, name: baseName(f.path), dir: false, size: f.data.length, mtime: f.mtime });
    ensureParents(f.path);
  }
  return [...seen.values()];
}

/** read a file: a live emulator has the freshest copy (a program may still hold it open), the store otherwise */
export async function diskRead(path: string): Promise<Uint8Array | null> {
  for (const rt of live) { try { const b = rt.vfsRead(path); if (b) return b; } catch { /* ignore */ } }
  await store().flush();
  const f = await store().read(path);
  return f?.data ?? null;
}

export async function diskWrite(path: string, data: Uint8Array): Promise<boolean> {
  if (isScratch(path)) return false;
  for (const rt of live) { try { rt.vfsAdd(path, data); } catch { /* ignore */ } }
  const s = store(); s.put(path, data); await s.flush(); notifyDiskChange(); return true;
}
export async function diskMkdir(path: string): Promise<void> {
  for (const rt of live) { try { rt.vfsMkdir(path); } catch { /* ignore */ } }
  const s = store(); s.mkdir(path); await s.flush(); notifyDiskChange();
}
/** delete a file, or a directory with everything below it */
export async function diskRemove(path: string): Promise<number> {
  const s = store(); await s.flush();
  const k = diskKey(path); const prefix = k.endsWith('\\') ? k : k + '\\';
  const victims = (await s.loadAll()).filter(f => { const fk = diskKey(f.path); return fk === k || fk.startsWith(prefix); }).map(f => f.path);
  if (!victims.some(v => diskKey(v) === k)) victims.push(path);   // an implicit directory (no stored entry of its own)
  victims.sort((a, b) => b.length - a.length);                     // deepest first
  for (const v of victims) { for (const rt of live) { try { rt.vfsRemove(v); } catch { /* ignore */ } } s.remove(v); }
  await s.flush(); notifyDiskChange(); return victims.length;
}
/** rename / move a file or a whole directory */
export async function diskRename(from: string, to: string): Promise<void> {
  const s = store(); await s.flush();
  const k = diskKey(from); const prefix = k + '\\';
  const all = await s.loadAll();
  const moves = all.filter(f => { const fk = diskKey(f.path); return fk === k || fk.startsWith(prefix); });
  if (!moves.length) { s.mkdir(to); s.remove(from); }
  for (const f of moves) {
    const dst = to + f.path.slice(from.length);
    for (const rt of live) { try { if (f.data) rt.vfsAdd(dst, f.data); else rt.vfsMkdir(dst); rt.vfsRemove(f.path); } catch { /* ignore */ } }
    if (f.data) s.put(dst, f.data); else s.mkdir(dst);
    s.remove(f.path);
  }
  await s.flush(); notifyDiskChange();
}

/** real numbers from the browser's storage manager (the quota this origin has on the device) */
export async function diskUsage(): Promise<{ used: number; quota: number } | null> {
  try { if (!navigator.storage?.estimate) return null; const e = await navigator.storage.estimate(); return { used: e.usage ?? 0, quota: e.quota ?? 0 }; } catch { return null; }
}

export type { DiskFile };
