// Persistent virtual disk C:\ for the x64 emulator.
// The emulator keeps files in wasm memory; every file a program writes and closes (installers, saved documents,
// the registry hive C:\Windows\nebula.reg, Start Menu .lnk shortcuts…) is handed to this module through the
// host_fs_* imports (emu/x64/host_wasm.c → runtime.ts) and stored in IndexedDB. On the next start everything is
// put back on the disk before any program runs, so installed programs and games survive app restarts — like a
// real C: drive. Scratch paths (C:\tmp, *\Temp\*) are deliberately not persisted.

export interface DiskFile { path: string; data: Uint8Array | null; mtime: number }   // data === null → directory

const DB_NAME = 'nebula-x64-disk', STORE = 'files', VERSION = 1;

export function diskKey(path: string) { return path.replace(/\//g, '\\').toLowerCase(); }
export function isScratch(path: string) { const k = diskKey(path); return k.startsWith('c:\\tmp\\') || k === 'c:\\tmp' || k.includes('\\temp\\') || k.endsWith('\\temp') || k.startsWith('c:\\nebula\\'); }

export class Disk {
  private db: IDBDatabase | null = null;
  private pending = new Map<string, DiskFile | null>();   // key → file to write, or null → delete
  private timer = 0;
  private flushing: Promise<void> | null = null;
  writes = 0; bytes = 0;                                   // counters for the UI ("disk activity")
  onActivity: (() => void) | null = null;
  available = typeof indexedDB !== 'undefined';

  async open(): Promise<boolean> {
    if (this.db || !this.available) return !!this.db;
    try {
      this.db = await new Promise<IDBDatabase>((res, rej) => {
        const r = indexedDB.open(DB_NAME, VERSION);
        r.onupgradeneeded = () => { const d = r.result; if (!d.objectStoreNames.contains(STORE)) d.createObjectStore(STORE); };
        r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); r.onblocked = () => rej(new Error('blocked'));
      });
      return true;
    } catch { this.available = false; return false; }
  }

  /** every stored entry (directories first, then files) */
  async loadAll(): Promise<DiskFile[]> {
    if (!(await this.open())) return [];
    const db = this.db!;
    const list = await new Promise<DiskFile[]>((res, rej) => {
      const out: DiskFile[] = []; const tx = db.transaction(STORE, 'readonly'); const req = tx.objectStore(STORE).openCursor();
      req.onsuccess = () => { const c = req.result; if (c) { const v = c.value as DiskFile; if (v && typeof v.path === 'string') out.push(v); c.continue(); } else res(out); };
      req.onerror = () => rej(req.error);
    });
    list.sort((a, b) => (a.data === null ? 0 : 1) - (b.data === null ? 0 : 1) || a.path.length - b.path.length);
    return list;
  }

  put(path: string, data: Uint8Array) { if (isScratch(path)) return; this.writes++; this.bytes += data.length; this.pending.set(diskKey(path), { path, data: data.slice(), mtime: Date.now() }); this.schedule(); }
  mkdir(path: string) { if (isScratch(path)) return; this.pending.set(diskKey(path), { path, data: null, mtime: Date.now() }); this.schedule(); }
  remove(path: string) { if (isScratch(path)) return; this.pending.set(diskKey(path), null); this.schedule(); }
  async rename(from: string, to: string) {
    // the emulator renames the node in place; we move the stored bytes (or the pending ones) under the new key
    const fk = diskKey(from); const p = this.pending.get(fk);
    if (p !== undefined) { this.pending.delete(fk); if (p) this.pending.set(diskKey(to), { ...p, path: to }); this.pending.set(fk, null); this.schedule(); return; }
    if (!(await this.open())) return;
    const cur = await this.get(fk); this.pending.set(fk, null); if (cur) this.pending.set(diskKey(to), { ...cur, path: to }); this.schedule();
  }
  private get(key: string): Promise<DiskFile | null> {
    return new Promise(res => { const req = this.db!.transaction(STORE, 'readonly').objectStore(STORE).get(key); req.onsuccess = () => res((req.result as DiskFile) ?? null); req.onerror = () => res(null); });
  }
  private schedule() { this.onActivity?.(); if (!this.timer) this.timer = window.setTimeout(() => { this.timer = 0; void this.flush(); }, 300); }

  /** write everything queued; resolves when the data is durable in IndexedDB */
  flush(): Promise<void> {
    if (this.flushing) return this.flushing.then(() => this.pending.size ? this.flush() : undefined);
    if (!this.pending.size) return Promise.resolve();
    const batch = this.pending; this.pending = new Map();
    this.flushing = (async () => {
      if (!(await this.open())) return;
      const db = this.db!;
      await new Promise<void>((res, rej) => {
        const tx = db.transaction(STORE, 'readwrite'); const st = tx.objectStore(STORE);
        for (const [k, v] of batch) { if (v) st.put(v, k); else st.delete(k); }
        tx.oncomplete = () => res(); tx.onerror = () => rej(tx.error); tx.onabort = () => rej(tx.error ?? new Error('aborted'));
      }).catch(() => { for (const [k, v] of batch) if (!this.pending.has(k)) this.pending.set(k, v); });
      this.onActivity?.();
    })().finally(() => { this.flushing = null; });
    return this.flushing;
  }
  queued() { return this.pending.size + (this.flushing ? 1 : 0); }

  /** wipe the whole disk (Settings → reset) */
  async clear() { this.pending.clear(); if (!(await this.open())) return; await new Promise<void>(res => { const tx = this.db!.transaction(STORE, 'readwrite'); tx.objectStore(STORE).clear(); tx.oncomplete = () => res(); tx.onerror = () => res(); }); }
  async usage(): Promise<{ files: number; bytes: number }> { const all = await this.loadAll(); let bytes = 0; for (const f of all) bytes += f.data?.length ?? 0; return { files: all.length, bytes }; }
}
